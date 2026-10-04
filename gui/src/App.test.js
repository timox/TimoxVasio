import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
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
    window.vasio = {
        getState: jest.fn(() => new Promise(resolve => { resolveState = resolve; })),
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

        await act(async () => { resolveState(state); await Promise.resolve(); });
        expect(container.textContent).toContain('AudioApp.exe · PID 42');
        expect(container.textContent).toContain('Entrées : 1');
        const options = Array.from(container.querySelectorAll('select option')).map(option => option.textContent);
        expect(options).toContain('Interface réelle');
        expect(options).toContain('TimoxVasio AudioApp.exe Out 1 (virtual:TimoxVasio:42:output:1)');
        expect(options).toContain('TimoxVasio AudioApp.exe In 1 (virtual:TimoxVasio:42:input:1)');
        const [sourceSelect, destinationSelect] = container.querySelectorAll('.route-editor select');
        const sourceOptions = Array.from(sourceSelect.options).map(option => option.value);
        const destinationOptions = Array.from(destinationSelect.options).map(option => option.value);
        expect(sourceOptions).toEqual(['', 'virtual:TimoxVasio:42:output:1', 'physical:interface-1:input:1']);
        expect(destinationOptions).toEqual(['', 'virtual:TimoxVasio:42:input:1', 'physical:interface-1:output:1']);
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});
