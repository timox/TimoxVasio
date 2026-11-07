import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import App from './App';

jest.mock('./RoutingPatchbay', () => props =>
    <pre data-testid="graph-observations">{JSON.stringify({ meters: props.meters, sources: props.sourceGroups })}</pre>);

const endpointId = 'virtual:TimoxVasio:42:output:1';
const initialState = {
    apiVersion: '1.1',
    engine: { state: 'running', clockSource: { kind: 'software' }, physicalDriverId: '', sampleRate: 48000, bufferFrames: 128 },
    physicalDrivers: [],
    virtualDrivers: [{ id: 'TimoxVasio', channelsPerDirection: 256,
        sampleRate: 48000, bufferFrames: 128,
        clients: [{ pid: 42, processCreationTime: '100', processName: 'AudioApp.exe', inputChannels: [], outputChannels: [1],
            processCpuPercent: 1, processCpuWindowMs: 500,
            activity: { available: true, stable: true, state: 'running', callbackCount: 10, exchangedFrames: 1280 } }],
        inputEndpoints: [], outputEndpoints: [{ id: endpointId, name: 'Output 1', direction: 'output', channel: 1 }] }],
    routes: [], configuredRoutes: [], configuredWasapiBridges: [], wasapiBridges: [], wasapiDevices: [], stereoPairs: []
};

async function withApp(check) {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    window.vasio = { getState: jest.fn().mockResolvedValue(initialState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        getDiagnostics: jest.fn().mockResolvedValue({ level: 'info', entries: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<App />));
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs > button')]
            .find(button => button.textContent === 'Routing')));
        const publish = async message => act(async () => window.vasio.subscribeEvents.mock.calls[0][0](message));
        const read = () => JSON.parse(container.querySelector('[data-testid="graph-observations"]').textContent);
        await check(publish, read);
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
}

const meter = { endpointId, peakDbfs: -12, shortTermLufs: -18, underruns: 0, overruns: 0 };

test('changing activity inside legacy devices events does not erase a channel measurement', async () => {
    await withApp(async (publish, read) => {
        await publish({ event: 'audio.meter', payload: meter });
        const before = read().meters[endpointId];
        const driver = initialState.virtualDrivers[0];
        await publish({ event: 'devices.changed', payload: { physicalDrivers: [], stereoPairs: [],
            virtualDrivers: [{ ...driver, clients: [{ ...driver.clients[0], processCpuPercent: 12,
                activity: { ...driver.clients[0].activity, callbackCount: 20 } }] }] } });
        expect(read().meters[endpointId]).toEqual(before);
        expect(read().sources[0].activity.callbackCount).toBe(20);
    });
});

test('supervision observations refresh activity without changing topology or clearing meters', async () => {
    await withApp(async (publish, read) => {
        await publish({ event: 'audio.meter', payload: meter });
        const before = read().meters[endpointId];
        await publish({ event: 'supervision.observations', payload: { owner: 'engine', readAtUnixMs: 1000,
            clients: [{ driverId: 'TimoxVasio', processId: 42, processCreationTime: '100',
                activity: { ...initialState.virtualDrivers[0].clients[0].activity, callbackCount: 30 },
                processCpuPercent: 7, processCpuWindowMs: 500 }] } });
        expect(read().sources[0].activity.callbackCount).toBe(30);
        expect(read().meters[endpointId]).toEqual(before);
    });
});

test('removing a process really invalidates its old meter', async () => {
    await withApp(async (publish, read) => {
        await publish({ event: 'audio.meter', payload: meter });
        await publish({ event: 'devices.changed', payload: { physicalDrivers: [], stereoPairs: [],
            virtualDrivers: [{ ...initialState.virtualDrivers[0], clients: [], outputEndpoints: [] }] } });
        expect(read().meters[endpointId]).toBeUndefined();
    });
});

test('an unavailable measurement is not replaced by a held signal', async () => {
    await withApp(async (publish, read) => {
        await publish({ event: 'audio.meter', payload: meter });
        await publish({ event: 'audio.meter', payload: { ...meter, peakDbfs: null, shortTermLufs: null } });
        expect(read().meters[endpointId].peakDbfs).toBeNull();
        expect(read().meters[endpointId].displayPeakDbfs).toBeNull();
    });
});

test('observations from an earlier process instance cannot update a reused PID', async () => {
    await withApp(async (publish, read) => {
        await publish({ event: 'supervision.observations', payload: { owner: 'engine', readAtUnixMs: 1000,
            clients: [{ driverId: 'TimoxVasio', processId: 42, processCreationTime: '99',
                activity: { callbackCount: 999 }, processCpuPercent: 99, processCpuWindowMs: 500 }] } });
        expect(read().sources[0].activity.callbackCount).toBe(10);
    });
});

test.each([{ available: true, state: 'stopped' }, { available: true, state: 'detached' }, { available: false },
    { available: true, state: 'initialized' }, { available: true, state: 'buffers-ready' },
    { available: true, state: 'running', successfulExchangeCount: 0 }])(
    'unavailable client activity hides the old signal (%j)', async activity => {
        await withApp(async (publish, read) => {
            await publish({ event: 'audio.meter', payload: meter });
            await publish({ event: 'supervision.observations', payload: { owner: 'engine', clients: [
                { driverId: 'TimoxVasio', processId: 42, processCreationTime: '100', activity }
            ] } });
            expect(read().meters[endpointId].peakDbfs).toBeNull();
            expect(read().meters[endpointId].displayPeakDbfs).toBeNull();
            await publish({ event: 'supervision.observations', payload: { owner: 'engine', clients: [
                { driverId: 'TimoxVasio', processId: 42, processCreationTime: '100', activity: { available: true, state: 'running' } }
            ] } });
            expect(read().meters[endpointId].peakDbfs).toBeNull();
        });
    });

test('a stopped client does not invalidate a level produced by the engine toward its input', async () => {
    await withApp(async (publish, read) => {
        const inputId = 'virtual:TimoxVasio:42:input:1';
        await publish({ event: 'audio.meter', payload: { ...meter, endpointId: inputId } });
        await publish({ event: 'supervision.observations', payload: { owner: 'engine', clients: [
            { driverId: 'TimoxVasio', processId: 42, processCreationTime: '100', activity: { available: true, state: 'stopped' } }
        ] } });
        expect(read().meters[inputId].peakDbfs).toBe(-12);
    });
});

test('losing the API connection clears the displayed measurements', async () => {
    await withApp(async (publish, read) => {
        await publish({ event: 'audio.meter', payload: meter });
        await act(async () => window.vasio.onDisconnect.mock.calls[0][0]());
        expect(read().meters[endpointId]).toBeUndefined();
    });
});

test.each(['process replacement', 'engine stopped'])('a polled snapshot invalidates meters after %s', async reason => {
    jest.useFakeTimers();
    try {
        await withApp(async (publish, read) => {
            await publish({ event: 'audio.meter', payload: meter });
            const state = reason === 'engine stopped'
                ? { ...initialState, engine: { ...initialState.engine, state: 'stopped' } }
                : { ...initialState, virtualDrivers: [{ ...initialState.virtualDrivers[0],
                    clients: [{ ...initialState.virtualDrivers[0].clients[0], processCreationTime: '200' }] }] };
            window.vasio.getState.mockResolvedValue(state);
            await act(async () => jest.advanceTimersByTime(5000));
            expect(read().meters[endpointId]).toBeUndefined();
        });
    } finally { jest.useRealTimers(); }
});
