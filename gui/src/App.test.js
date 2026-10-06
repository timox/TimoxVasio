import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import App from './App';

const state = {
    apiVersion: '1.0',
    engine: { state: 'stopped', physicalDriverId: 'interface-1', sampleRate: 48000, bufferFrames: 512, lastError: null },
    physicalDrivers: [{
        id: 'interface-1', name: 'Interface réelle',
        inputEndpoints: [{ id: 'physical:interface-1:input:1', name: 'Entrée 1', direction: 'input', channel: 1 }],
        outputEndpoints: [{ id: 'physical:interface-1:output:1', name: 'Sortie 1', direction: 'output', channel: 1 }],
        sampleRates: [44100, 48000], bufferSizes: null, capabilitiesKnown: false
    }],
    virtualDrivers: [{
        id: 'TimoxVasio', channelsPerDirection: 256, sampleRate: null, bufferFrames: null,
        clients: [{ pid: 42, processName: 'AudioApp.exe', inputChannels: [1], outputChannels: [1] }],
        inputEndpoints: [{ id: 'virtual:TimoxVasio:42:input:1', name: 'TimoxVasio AudioApp.exe In 1', direction: 'input', channel: 1 }],
        outputEndpoints: [{ id: 'virtual:TimoxVasio:42:output:1', name: 'TimoxVasio AudioApp.exe Out 1', direction: 'output', channel: 1 }]
    }],
    routes: []
};

test('graphical connections and matrix edit the same pending routing configuration', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = {
        getState: jest.fn().mockResolvedValue(state),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}),
        onDisconnect: jest.fn(() => () => {}),
        applyConfiguration
    };
    window.electronAPI = {
        onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {})
    };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.querySelector('main > section:first-child h2')?.textContent).toBe('Routing');
        expect(container.querySelector('.routing-matrix')).not.toBeNull();
        expect(container.querySelector('.patchbay')).toBeNull();
        await act(async () => Simulate.click(container.querySelector('[data-routing-view="patchbay"]')));
        expect(container.querySelector('.patchbay')).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')));
        await act(async () => Simulate.click(container.querySelector('[data-port-id="physical:interface-1:output:1"]')));
        await act(async () => Simulate.click(container.querySelector('.configured-routes .highlight-route')));
        expect(container.querySelector('.patchbay-cables path.selected')).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector('[data-routing-view="matrix"]')));
        expect(container.querySelector('.routing-matrix')).not.toBeNull();
        expect(container.textContent).toContain('1 route');
        expect(container.textContent).toContain('Pending changes');
        await act(async () => { await Simulate.click(container.querySelector('.apply-configuration')); });
        expect(applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({ routes: [expect.objectContaining({
            sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
            destinationEndpointId: 'physical:interface-1:output:1'
        })] }));
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('switching physical drivers identifies old hardware routes before applying', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const physicalRoute = { id: 'master', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    const virtualRoute = { id: 'virtual', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'virtual:TimoxVasio:42:input:1', gainDb: 0, mute: false };
    const driverState = {
        ...state,
        engine: { ...state.engine, physicalDriverId: 'interface-1', sampleRate: 48000, bufferFrames: 512 },
        physicalDrivers: [
            { ...state.physicalDrivers[0], id: 'interface-1' },
            { id: 'asio4all-1', name: 'ASIO4ALL v2', inputEndpoints: [], outputEndpoints: [],
                sampleRates: [], bufferSizes: null, capabilitiesKnown: false }
        ],
        routes: [physicalRoute, virtualRoute]
    };
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = {
        getState: jest.fn().mockResolvedValue(driverState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}),
        onDisconnect: jest.fn(() => () => {}),
        applyConfiguration
    };
    window.electronAPI = {
        onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {})
    };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const driverSelect = [...container.querySelectorAll('select')].find(select => select.value === 'interface-1');
        await act(async () => Simulate.change(driverSelect, { target: { value: 'asio4all-1' } }));
        expect(container.textContent).toContain('1 route still targets the previous physical driver');
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(applyConfiguration).not.toHaveBeenCalled();
        await act(async () => Simulate.click(container.querySelector('.remove-incompatible-routes')));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            physicalDriverId: 'asio4all-1',
            routes: [virtualRoute]
        }));
        await act(async () => Simulate.change(driverSelect, { target: { value: 'asio4all-1' } }));
        await act(async () => Simulate.click(container.querySelector('.clear-all-routes')));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(applyConfiguration.mock.lastCall[0]).toEqual(expect.objectContaining({
            physicalDriverId: 'asio4all-1',
            routes: []
        }));
    } finally {
        await act(async () => root.unmount());
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});

test('a connection label and color persist locally without changing the audio API payload', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    const route = { id: 'saved-route', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    const savedState = { ...state, routes: [route] };
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = {
        getState: jest.fn().mockResolvedValue(savedState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration
    };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    let root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await act(async () => Simulate.click(container.querySelector('[data-routing-view="patchbay"]')));
        await act(async () => Simulate.click(container.querySelector('.configured-routes .highlight-route')));
        await act(async () => Simulate.change(container.querySelector('.route-label-input'), { target: { value: 'Headphone' } }));
        await act(async () => Simulate.change(container.querySelector('.route-color-input'), { target: { value: '#bd3f47' } }));
        expect(container.querySelector('.patchbay-cables path.connection.selected').parentElement.getAttribute('style')).toContain('#bd3f47');
        expect(container.textContent).toContain('Headphone');
        expect(container.textContent).toContain('No pending changes.');
        await act(async () => { await Simulate.click(container.querySelector('.apply-configuration')); });
        expect(applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({ routes: [route] }));
        expect(applyConfiguration.mock.calls[0][0].routes[0]).not.toHaveProperty('label');
        expect(applyConfiguration.mock.calls[0][0].routes[0]).not.toHaveProperty('color');
        await act(async () => root.unmount());
        window.vasio.getState.mockResolvedValue({ ...savedState, routes: [{ ...route, id: 'engine-reassigned-id' }] });
        root = createRoot(container);
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.textContent).toContain('Headphone');
    } finally {
        await act(async () => root.unmount());
        container.remove();
        window.localStorage.clear();
        delete window.vasio; delete window.electronAPI;
    }
});

test('bulk appearance edits update several connections without changing their audio routes', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    const routes = [
        { id: 'speaker-route', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
            destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false },
        { id: 'monitor-route', sourceEndpointId: 'physical:interface-1:input:1',
            destinationEndpointId: 'virtual:TimoxVasio:42:input:1', gainDb: 0, mute: false }
    ];
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = {
        getState: jest.fn().mockResolvedValue({ ...state, routes }),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration
    };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await act(async () => Simulate.click(container.querySelector('[data-routing-view="patchbay"]')));
        await act(async () => Simulate.click(container.querySelector('.bulk-edit-toggle')));
        const checkboxes = container.querySelectorAll('.bulk-route-checkbox');
        expect(checkboxes).toHaveLength(2);
        for (const checkbox of checkboxes) await act(async () => Simulate.change(checkbox, { target: { checked: true } }));
        await act(async () => Simulate.change(container.querySelector('.bulk-label-input'), { target: { value: 'Headphone' } }));
        await act(async () => Simulate.change(container.querySelector('.bulk-color-input'), { target: { value: '#bd3f47' } }));
        await act(async () => Simulate.click(container.querySelector('.bulk-apply')));
        expect(container.querySelectorAll('.route-custom-label')).toHaveLength(2);
        expect([...container.querySelectorAll('.patchbay-cables g')].map(group => group.getAttribute('style')))
            .toEqual(expect.arrayContaining([expect.stringContaining('#bd3f47'), expect.stringContaining('#bd3f47')]));
        await act(async () => Simulate.change(container.querySelector('.bulk-property-toggle input'), { target: { checked: false } }));
        await act(async () => Simulate.change(container.querySelector('.bulk-color-input'), { target: { value: '#3f725a' } }));
        await act(async () => Simulate.click(container.querySelector('.bulk-apply')));
        expect([...container.querySelectorAll('.route-custom-label')].map(label => label.textContent)).toEqual(['Headphone', 'Headphone']);
        expect([...container.querySelectorAll('.patchbay-cables g')].every(group => group.getAttribute('style').includes('#3f725a'))).toBe(true);
        expect(container.textContent).toContain('No pending changes.');
        await act(async () => { await Simulate.click(container.querySelector('.apply-configuration')); });
        expect(applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({ routes }));
        expect(applyConfiguration.mock.calls[0][0].routes.every(route => !('label' in route) && !('color' in route))).toBe(true);
    } finally {
        await act(async () => root.unmount());
        container.remove(); window.localStorage.clear();
        delete window.vasio; delete window.electronAPI;
    }
});

test('the GUI waits for API inventory and renders only the returned drivers and endpoints', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    let resolveState;
    let resolveProfiles;
    window.vasio = {
        getState: jest.fn(() => new Promise(resolve => { resolveState = resolve; })),
        getApplicationProfiles: jest.fn(() => new Promise(resolve => { resolveProfiles = resolve; })),
        replaceApplicationProfiles: jest.fn(),
        subscribeEvents: jest.fn(() => () => {}),
        onDisconnect: jest.fn(() => () => {}),
        applyConfiguration: jest.fn()
    };
    window.electronAPI = {
        onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {})
    };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); });
        expect(container.querySelectorAll('.driver-setting')).toHaveLength(0);
        expect(container.querySelector('.apply-configuration').disabled).toBe(true);

        await act(async () => {
            resolveState(state);
            resolveProfiles({ profiles: [{ processName: 'mixxx.exe', inputChannels: 255, outputChannels: 255 }] });
            await Promise.resolve();
        });
        expect(container.textContent).toContain('Interface réelle');
        expect(container.textContent).toContain('Application channel profiles');
        expect(container.querySelector('[aria-label="Input channels 1"]').value).toBe('255');
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.textContent).toContain('AudioApp.exe · PID 42');
        expect(container.textContent).toContain('Inputs: 1');
        expect(container.textContent).toContain('TimoxVasio AudioApp.exe Out 1');
        const routingDetails = container.querySelector('.routing-details');
        expect(routingDetails.open).toBe(true);
        const destinationZone = container.querySelectorAll('.matrix-controls select')[1];
        await act(async () => Simulate.change(destinationZone, { target: { value: 'virtual-input' } }));
        expect(container.textContent).toContain('TimoxVasio AudioApp.exe In 1');
        expect(container.querySelector('.route-cell')).not.toBeNull();
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});

test('the application profile editor saves the full list through the API and reports clients to restart', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const profiles = [{ processName: 'mixxx.exe', inputChannels: 255, outputChannels: 192 }];
    window.vasio = {
        getState: jest.fn().mockResolvedValue(state),
        getApplicationProfiles: jest.fn().mockResolvedValue({
            profiles: [{ processName: 'mixxx.exe', inputChannels: 255, outputChannels: 255 }]
        }),
        replaceApplicationProfiles: jest.fn().mockResolvedValue({
            profiles,
            restartRequiredClients: [{ pid: 42, processName: 'AudioApp.exe' }]
        }),
        subscribeEvents: jest.fn(() => () => {}),
        onDisconnect: jest.fn(() => () => {}),
        applyConfiguration: jest.fn()
    };
    window.electronAPI = {
        onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {})
    };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const outputChannels = container.querySelector('[aria-label="Output channels 1"]');
        await act(async () => {
            Simulate.change(outputChannels, { target: { value: '192' } });
        });
        await act(async () => {
            container.querySelector('button[aria-label="Save profiles"]').click();
            await Promise.resolve();
        });
        expect(window.vasio.replaceApplicationProfiles).toHaveBeenCalledWith(profiles);
        expect(container.textContent).toContain('Restart AudioApp.exe');
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});

test('the API and diagnostics views use local Swagger assets and the diagnostics API', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.vasio = {
        getState: jest.fn().mockResolvedValue(state),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        getDiagnostics: jest.fn().mockResolvedValue({ level: 'debug', entries: [
            { timestamp: '2026-10-04T12:00:00.000Z', level: 'info', component: 'engine', message: 'Engine ready' }
        ] }),
        setDiagnosticLevel: jest.fn().mockResolvedValue({ level: 'debug' }),
        subscribeEvents: jest.fn(() => () => {}),
        onDisconnect: jest.fn(() => () => {}),
        applyConfiguration: jest.fn()
    };
    window.electronAPI = {
        onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}),
        startEngine: jest.fn(), stopEngine: jest.fn()
    };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const tabs = container.querySelectorAll('.view-tabs button');
        await act(async () => Simulate.click(tabs[3]));
        expect(container.querySelector('iframe[title="TimoxVasio Swagger documentation"]')?.getAttribute('src')).toBe('swagger.html');
        expect(container.textContent).toContain('engine.stop');
        await act(async () => Simulate.click(tabs[4]));
        await act(async () => Promise.resolve());
        expect(window.vasio.getDiagnostics).toHaveBeenCalledWith(300);
        expect(container.textContent).toContain('Engine ready');
        expect(container.textContent).toContain('Start audio engine');
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});

test('active channels show meters and the Analyse and API views call documented commands', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const pair = { id: 'stereo:TimoxVasio:42:output:1-2', leftEndpointId: 'virtual:TimoxVasio:42:output:1',
        rightEndpointId: 'virtual:TimoxVasio:42:output:2', label: 'AudioApp.exe · Sorties 1–2' };
    const activeState = {
        ...state,
        engine: { ...state.engine, state: 'running', physicalDriverId: 'interface-1', sampleRate: 48000, bufferFrames: 256 },
        physicalDrivers: [{ ...state.physicalDrivers[0], outputEndpoints: [
            { id: 'physical:interface-1:output:1', name: 'Sortie 1', direction: 'output', channel: 1 },
            { id: 'physical:interface-1:output:2', name: 'Sortie 2', direction: 'output', channel: 2 }
        ] }],
        virtualDrivers: [{ ...state.virtualDrivers[0], clients: [{ ...state.virtualDrivers[0].clients[0], outputChannels: [1, 2] }],
            outputEndpoints: [
                { id: pair.leftEndpointId, name: 'TimoxVasio AudioApp.exe Out 1', direction: 'output', channel: 1 },
                { id: pair.rightEndpointId, name: 'TimoxVasio AudioApp.exe Out 2', direction: 'output', channel: 2 }
            ] }],
        routes: [
            { id: 'r1', sourceEndpointId: pair.leftEndpointId, destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false },
            { id: 'r2', sourceEndpointId: pair.rightEndpointId, destinationEndpointId: 'physical:interface-1:output:2', gainDb: 0, mute: false }
        ],
        stereoPairs: [pair]
    };
    let publishEvent;
    window.vasio = {
        getState: jest.fn().mockResolvedValue(activeState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(callback => { publishEvent = callback; return () => {}; }),
        onDisconnect: jest.fn(() => () => {}),
        applyConfiguration: jest.fn(),
        startCorrelation: jest.fn().mockResolvedValue({ accepted: true }),
        stopCorrelation: jest.fn(),
        executeCommand: jest.fn().mockResolvedValue({ accepted: true }),
        httpRequest: jest.fn().mockResolvedValue({ status: 200, body: {} })
    };
    window.electronAPI = {
        onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}),
        startEngine: jest.fn(), stopEngine: jest.fn()
    };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const tabs = container.querySelectorAll('.view-tabs button');
        await act(async () => Simulate.click(tabs[1]));
        await act(async () => publishEvent({ event: 'audio.meter', payload: { endpointId: pair.leftEndpointId, peakDbfs: -9, underruns: 0, overruns: 0 } }));
        expect(container.querySelectorAll('.channel-meter')).toHaveLength(2);
        expect(container.textContent).toContain('-9.0 dBFS');

        await act(async () => Simulate.click(tabs[2]));
        expect(container.querySelector('.correlation-graph')).not.toBeNull();
        await act(async () => Simulate.change(container.querySelector('.analysis-view select'), { target: { value: pair.id } }));
        await act(async () => container.querySelector('.analysis-actions button').click());
        expect(window.vasio.startCorrelation).toHaveBeenCalledWith(pair.id);
        await act(async () => publishEvent({ event: 'audio.correlation', payload: { stereoPairId: pair.id,
            leftEndpointId: pair.leftEndpointId, rightEndpointId: pair.rightEndpointId, correlation: -0.5, state: 'measuring' } }));
        expect(container.querySelector('.correlation-readout strong').textContent).toBe('-0.50');

        await act(async () => Simulate.click(tabs[3]));
        expect(container.textContent).toContain('Console API');
        await act(async () => container.querySelector('.api-console-panel .analysis-actions button').click());
        expect(window.vasio.executeCommand).toHaveBeenCalledWith(expect.objectContaining({ command: 'engine.start' }));
    } finally {
        await act(async () => { root.unmount(); });
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});
