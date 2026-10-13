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

test('an existing route opens on its matrix pages and can be revealed in connections', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const channels = Array.from({ length: 80 }, (_, index) => index + 1);
    const route = { id: 'saved-route', sourceEndpointId: 'virtual:TimoxVasio:42:output:73',
        destinationEndpointId: 'virtual:TimoxVasio:42:input:74', gainDb: 0, mute: false };
    const routedState = { ...state, routes: [route], virtualDrivers: [{ ...state.virtualDrivers[0],
        clients: [{ pid: 42, processName: 'AudioApp.exe', inputChannels: channels, outputChannels: channels }],
        inputEndpoints: channels.map(channel => ({ id: `virtual:TimoxVasio:42:input:${channel}`,
            name: `AudioApp In ${channel}`, direction: 'input', channel })),
        outputEndpoints: channels.map(channel => ({ id: `virtual:TimoxVasio:42:output:${channel}`,
            name: `AudioApp Out ${channel}`, direction: 'output', channel }))
    }] };
    window.vasio = { getState: jest.fn().mockResolvedValue(routedState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.querySelector('.route-legend').textContent).toContain('1 configured overall · 1 on these pages');
        expect(container.querySelectorAll('.route-cell.active')).toHaveLength(1);
        expect(container.querySelector('.matrix-pagers').textContent).toContain('Sources : 65–80 / 80');
        expect(container.querySelector('.matrix-pagers').textContent).toContain('Destinations : 65–80 / 80');
        await act(async () => Simulate.click(container.querySelector('[data-routing-view="patchbay"]')));
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Show a configured connection')));
        expect(container.querySelector('.patchbay-cables path.connection.selected')).not.toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('profiled virtual ports can be routed before their applications open channels', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const idleState = {
        ...state,
        engine: { ...state.engine, sampleRate: 44100, state: 'stopped' },
        virtualDrivers: [{ ...state.virtualDrivers[0], clients: [], inputEndpoints: [], outputEndpoints: [] }],
        routes: []
    };
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = {
        getState: jest.fn().mockResolvedValue(idleState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [
            { processName: 'mixxx.exe', inputChannels: 0, outputChannels: 2 },
            { processName: 'scsynth.exe', inputChannels: 2, outputChannels: 0 }
        ] }),
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
        const sourceId = 'virtual:TimoxVasio:app:mixxx.exe:output:1';
        const destinationId = 'virtual:TimoxVasio:app:scsynth.exe:input:1';
        expect(container.querySelector(`[data-port-id="${sourceId}"]`)).not.toBeNull();
        expect(container.querySelector(`[data-port-id="${destinationId}"]`)).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector(`[data-port-id="${sourceId}"]`)));
        await act(async () => Simulate.click(container.querySelector(`[data-port-id="${destinationId}"]`)));
        await act(async () => { await Simulate.click(container.querySelector('.apply-configuration')); });
        expect(applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            sampleRate: 44100,
            routes: [expect.objectContaining({ sourceEndpointId: sourceId, destinationEndpointId: destinationId })]
        }));
        expect(container.textContent).toContain('Configured routes are waiting for their application channels to open.');
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('a configured but stopped driver can be released from the logs view', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const startAudio = jest.fn().mockResolvedValue({ accepted: true });
    const stopAudio = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = {
        getState: jest.fn().mockResolvedValue({ ...state, virtualDrivers: [{ ...state.virtualDrivers[0], clients: [] }] }),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        startAudio, stopAudio
    };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const logsTab = [...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Logs');
        await act(async () => Simulate.click(logsTab));
        const startButton = container.querySelector('.engine-start');
        expect(startButton).not.toBeNull();
        await act(async () => { Simulate.click(startButton); await new Promise(resolve => setTimeout(resolve, 200)); });
        expect(startAudio).toHaveBeenCalledTimes(1);
        const stopButton = container.querySelector('.engine-stop');
        expect(stopButton).not.toBeNull();
        await act(async () => { await Simulate.click(stopButton); });
        expect(stopAudio).toHaveBeenCalledTimes(1);
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('performance view distinguishes measured ASIO buffer pressure from missing observations', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const measured = { ...state, bufferHealth: { state: 'warning', sampleRate: 44100,
        bufferFrames: 512, windowDurationMs: 5000, callbackCount: 430,
        delayedStartCount: 7, processingOverrunCount: 6, blockPeriodNs: 11609977,
        worstProcessingNs: 12500000, worstMarginNs: -890023 } };
    window.vasio = { getState: jest.fn().mockResolvedValue(measured),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        expect(container.querySelector('main > section:first-child')).toBe(container.querySelector('.buffer-health'));
        expect(container.querySelector('main > section:nth-child(2)')).toBe(container.querySelector('.transport-health'));
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Slowest callback');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Time available');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Delayed starts');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Processing overruns');
        expect(container.querySelector('.buffer-health')?.textContent).toContain('Callback timing incidents were observed.');
        expect(container.querySelector('.buffer-health')?.textContent).toContain('44100 Hz');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Processing overruns6');
        expect(container.querySelector('.buffer-health')?.textContent).toContain('7 starts were unusually late');
        expect(container.querySelector('.buffer-health')?.textContent).toContain('The slowest callback took 12.50 ms, exceeding the 11.61 ms available per block');
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('performance view explains physical buffer measurements without suggesting another size', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    let onEvent;
    const measured = { ...state, routes: [{ id: 'test-route', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false }], bufferHealth: { state: 'healthy', sampleRate: 48000,
        bufferFrames: 1024, windowDurationMs: 5000, callbackCount: 235,
        delayedStartCount: 0, processingOverrunCount: 0, blockPeriodNs: 21333333,
        worstProcessingNs: 2500000, worstMarginNs: 18833333 } };
    window.vasio = { getState: jest.fn().mockResolvedValue(measured),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(handler => { onEvent = handler; return () => {}; }), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Logs')));
        expect(container.querySelector('.buffer-health')).toBeNull();
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        const clockDiagram = container.querySelector('.clock-source');
        expect(clockDiagram?.textContent).toContain('Interface réelle');
        expect(clockDiagram?.textContent).toContain('TimoxVasio');
        expect(clockDiagram?.textContent).toContain('drift');
        expect(clockDiagram?.textContent).toContain('jitter');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('ASIO application');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('Physical ASIO driver');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('shared ring');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('next physical callback');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('SuperCollider');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('not measured');
        expect(container.querySelector('.transport-health')?.textContent).toContain('AudioApp.exe output');
        expect(container.querySelector('.transport-health')?.textContent).toContain('—');
        await act(async () => onEvent({ event: 'audio.meter', payload: { endpointId: 'virtual:TimoxVasio:42:output:1', peakDbfs: -12, underruns: 2, overruns: 1 } }));
        expect(container.querySelector('.transport-table')?.textContent).toContain('AudioApp.exe output21');
        expect(container.querySelector('.buffer-timeline')?.getAttribute('aria-label')).toContain('2.50 ms of processing within a 21.33 ms block');
        const explanation = container.querySelector('.buffer-health .buffer-explanation')?.textContent;
        expect(explanation).toContain('235 callbacks were observed over 5.0 seconds');
        expect(explanation).toContain('None started unusually late or took longer than the 21.33 ms available per block');
        expect(explanation).toContain('The slowest callback took 2.50 ms, leaving 18.83 ms');
        expect(explanation).not.toMatch(/try|recommended|optimal/i);
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('performance view refreshes callback timing while it stays open', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    jest.useFakeTimers();
    const first = { ...state, bufferHealth: null };
    const updated = { ...state, bufferHealth: { state: 'healthy', sampleRate: 48000,
        bufferFrames: 1024, windowDurationMs: 5000, callbackCount: 235,
        delayedStartCount: 0, processingOverrunCount: 0, blockPeriodNs: 21333333,
        worstProcessingNs: 43800, worstMarginNs: 21289533 } };
    window.vasio = { getState: jest.fn().mockResolvedValueOnce(first).mockResolvedValue(updated),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        expect(container.querySelector('.buffer-health')?.textContent).toContain('No callback observation yet');
        await act(async () => { jest.advanceTimersByTime(2000); await Promise.resolve(); });
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('43.8 µs');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Callbacks observed235');
    } finally {
        await act(async () => root.unmount());
        jest.useRealTimers();
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
        await act(async () => Simulate.click([...tabs].find(button => button.textContent === 'API and Swagger')));
        expect(container.querySelector('iframe[title="TimoxVasio Swagger documentation"]')?.getAttribute('src')).toBe('swagger.html');
        expect(container.textContent).toContain('engine.stop');
        await act(async () => Simulate.click([...tabs].find(button => button.textContent === 'Logs')));
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

        await act(async () => Simulate.click([...tabs].find(button => button.textContent === 'API and Swagger')));
        expect(container.textContent).toContain('Console API');
        await act(async () => container.querySelector('.api-console-panel .analysis-actions button').click());
        expect(window.vasio.executeCommand).toHaveBeenCalledWith(expect.objectContaining({ command: 'engine.start' }));
    } finally {
        await act(async () => { root.unmount(); });
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});
const measurementWindow = {
    durationMs: 5000,
    clients: [{ processId: 42, driverId: 'TimoxVasio',
        hostCallback: { samples: 4, meanNs: 43800, worstNs: 1120000 },
        ringWrite: { samples: 4, meanNs: 1000, worstNs: 2000 },
        ringWait: { samples: 3, meanNs: 2000000, worstNs: 3000000 },
        ringWaitDiscardedBlocks: 1 }],
    physical: { clientRead: { samples: 2, meanNs: 50000, worstNs: 70000 },
        routing: { samples: 2, meanNs: 100000, worstNs: 120000 },
        outputCopy: { samples: 2, meanNs: 30000, worstNs: 40000 } }
};

test.each([
    ['off', { enabled: false, sampleEveryCallbacks: 16, window: null }, 'Measurements off'],
    ['collecting', { enabled: true, sampleEveryCallbacks: 16, window: null }, 'Collecting first 5 s window'],
    ['no samples', { enabled: true, sampleEveryCallbacks: 16, window: {
        durationMs: 5000, clients: [{ processId: 42, driverId: 'TimoxVasio',
            hostCallback: null, ringWrite: null, ringWait: null, ringWaitDiscardedBlocks: 0 }],
        physical: { clientRead: null, routing: null, outputCopy: null } } }, 'No stage samples']
])('performance measurements %s state', async (_name, measurements, message) => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.vasio = { getState: jest.fn().mockResolvedValue({ ...state, performanceMeasurements: measurements }),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        expect(container.querySelector('.performance-flow').textContent).toContain(message);
        expect(container.querySelector('.stage-measurements')?.textContent).toContain('—');
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('performance measurements show stages and send documented toggle command', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const executeCommand = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = { getState: jest.fn().mockResolvedValue({ ...state, performanceMeasurements:
            { enabled: true, sampleEveryCallbacks: 16, window: measurementWindow } }),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        executeCommand };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        const flow = container.querySelector('.performance-flow').textContent;
        expect(flow).toContain('43.8 µs');
        expect(flow).toContain('1.12 ms');
        expect(flow).toContain('4 samples');
        expect(flow).toContain('5 s window');
        expect(flow).toContain('Routing');
        expect(flow).toContain('Discarded blocks: 1');
        await act(async () => { Simulate.change(container.querySelector('.stage-toggle input'), { target: { checked: false } }); await Promise.resolve(); });
        expect(executeCommand).toHaveBeenCalledWith({ id: expect.any(String),
            command: 'performance.measurements.set', payload: { enabled: false } });
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});
test('performance measurements keep separate client observations', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const second = { ...measurementWindow.clients[0], processId: 99,
        hostCallback: { samples: 2, meanNs: 80000, worstNs: 90000 }, ringWaitDiscardedBlocks: 0 };
    window.vasio = { getState: jest.fn().mockResolvedValue({ ...state, performanceMeasurements:
            { enabled: true, sampleEveryCallbacks: 16, window: { ...measurementWindow,
                clients: [...measurementWindow.clients, second] } } }),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        expect(container.querySelectorAll('.transport-steps li:first-child .stage-client')).toHaveLength(2);
        expect(container.querySelector('.transport-steps li:first-child').textContent).toContain('PID 99');
        expect(container.querySelector('.transport-steps li:first-child').textContent).toContain('80.0 µs');
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('performance measurements report rejected command and retain API state', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.vasio = { getState: jest.fn().mockResolvedValue({ ...state, performanceMeasurements:
            { enabled: false, sampleEveryCallbacks: 16, window: null } }),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        executeCommand: jest.fn().mockRejectedValue(new Error('Measurement command rejected')) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Performance')));
        await act(async () => { Simulate.change(container.querySelector('.stage-toggle input'),
            { target: { checked: true } }); await Promise.resolve(); });
        expect(window.vasio.executeCommand).toHaveBeenCalledWith({ id: expect.any(String),
            command: 'performance.measurements.set', payload: { enabled: true } });
        expect(container.querySelector('.performance-flow [role="alert"]').textContent)
            .toContain('Measurement command rejected');
        expect(container.querySelector('.stage-toggle input').checked).toBe(false);
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});
