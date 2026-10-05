import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import App from './App';

const state = {
    apiVersion: '1.0',
    engine: { state: 'stopped', physicalDriverId: null, sampleRate: null, bufferFrames: null, lastError: null },
    physicalDrivers: [{
        id: 'physical:interface-1', name: 'Interface réelle',
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
        expect(container.textContent).toContain('AudioApp.exe · PID 42');
        expect(container.textContent).toContain('Entrées : 1');
        expect(container.textContent).toContain('Interface réelle');
        expect(container.textContent).toContain('TimoxVasio AudioApp.exe Out 1');
        const destinationZone = container.querySelectorAll('.matrix-controls select')[1];
        await act(async () => Simulate.change(destinationZone, { target: { value: 'virtual-input' } }));
        expect(container.textContent).toContain('TimoxVasio AudioApp.exe In 1');
        expect(container.querySelector('.route-cell')).not.toBeNull();
        expect(container.textContent).toContain('Profils de canaux par application');
        expect(container.querySelector('[aria-label="Canaux d’entrée 1"]').value).toBe('255');
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
        const outputChannels = container.querySelector('[aria-label="Canaux de sortie 1"]');
        await act(async () => {
            Simulate.change(outputChannels, { target: { value: '192' } });
        });
        await act(async () => {
            container.querySelector('button[aria-label="Enregistrer les profils"]').click();
            await Promise.resolve();
        });
        expect(window.vasio.replaceApplicationProfiles).toHaveBeenCalledWith(profiles);
        expect(container.textContent).toContain('Redémarrez AudioApp.exe');
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
        await act(async () => Simulate.click(tabs[1]));
        expect(container.querySelector('iframe[title="Documentation Swagger de TimoxVasio"]')?.getAttribute('src')).toBe('swagger.html');
        expect(container.textContent).toContain('engine.stop');
        await act(async () => Simulate.click(tabs[2]));
        await act(async () => Promise.resolve());
        expect(window.vasio.getDiagnostics).toHaveBeenCalledWith(300);
        expect(container.textContent).toContain('Engine ready');
        expect(container.textContent).toContain('Arrêter le moteur');
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});
