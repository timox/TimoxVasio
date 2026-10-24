import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import App from './App';

const chooseContextAction = async (target, label) => {
    await act(async () => Simulate.contextMenu(target, { clientX: 100, clientY: 100 }));
    const action = [...document.querySelectorAll('.patchbay-context-menu [role="menuitem"]')]
        .find(button => button.textContent === label);
    expect(action).toBeDefined();
    await act(async () => Simulate.click(action));
};

const addFromMenu = async (container, sourceId, destinationId) => {
    await chooseContextAction(container.querySelector('.patchbay-flow'), 'Create a new connection');
    const q = selector => document.querySelector(selector);
    await act(async () => Simulate.click([...document.querySelectorAll('.quick-routing button')].find(button => button.textContent === 'Mono')));
    for (const [side, id] of [['source', sourceId], ['destination', destinationId]]) {
        const groupSelect = q('[aria-label="Quick route ' + side + '"]');
        for (const option of [...groupSelect.options]) {
            await act(async () => Simulate.change(groupSelect, { target: { value: option.value } }));
            if ([...q('[aria-label="Quick route ' + side + ' channels"]').options].some(item => item.value === id)) break;
        }
        await act(async () => Simulate.change(q('[aria-label="Quick route ' + side + ' channels"]'), { target: { value: id } }));
    }
    await act(async () => Simulate.click(q('.quick-routing-add')));
    const dialog = q('.quick-routing').closest('[role="dialog"]');
    await act(async () => Simulate.click([...dialog.querySelectorAll('button')].find(button => button.textContent === 'Close')));
};

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

const withClockApp = async (initialState, check) => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    window.vasio = { getState: jest.fn().mockResolvedValue(initialState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        getDiagnostics: jest.fn().mockResolvedValue({ level: 'info', entries: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration: jest.fn().mockResolvedValue({ accepted: true }),
        executeCommand: jest.fn().mockResolvedValue({}), stopAudio: jest.fn().mockResolvedValue({}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await check(container);
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
};

test('subject tabs isolate API and analysis while keeping the console draft', async () => {
    await withClockApp(state, async container => {
        const section = async name => act(async () => Simulate.click([...container.querySelectorAll('.view-tabs > button')].find(button => button.textContent === name)));
        await section('API and Swagger');
        expect(container.querySelector('#api-panel-swagger').hidden).toBe(false);
        expect(container.querySelector('#api-panel-console').hidden).toBe(true);
        await act(async () => Simulate.click(container.querySelector('#api-tab-console')));
        await act(async () => Simulate.change(container.querySelector('.api-console-panel textarea'), { target: { value: '{"id":"kept"}' } }));
        await act(async () => Simulate.click(container.querySelector('#api-tab-websocket')));
        expect(container.querySelector('#api-panel-console').hidden).toBe(true);
        await act(async () => Simulate.click(container.querySelector('#api-tab-console')));
        expect(container.querySelector('.api-console-panel textarea').value).toBe('{"id":"kept"}');
        expect(window.vasio.executeCommand).not.toHaveBeenCalled();
        await section('Analysis');
        expect(container.querySelector('#analysis-panel-timeline').hidden).toBe(false);
        expect(container.querySelector('#analysis-panel-correlation').hidden).toBe(true);
        await act(async () => Simulate.click(container.querySelector('#analysis-tab-correlation')));
        expect(container.querySelector('#analysis-panel-correlation').hidden).toBe(false);
        expect(container.querySelector('.analysis-dashboard').hidden).toBe(true);
    });
});

test('WASAPI field help preserves the bridge draft and live counters stay outside device setup', async () => {
    const bridge = { id: 'mic', kind: 'capture', endpointId: 'input-1', enabled: false, bufferDuration100ns: 0 };
    await withClockApp({ ...state, apiVersion: '1.1', configuredWasapiBridges: [bridge], wasapiDevices: [],
        wasapiBridges: [{ id: 'mic', state: 'stopped', health: { bufferedFrames: 12, rejectedFrames: 4, silencedFrames: 8 } }] }, async container => {
        await act(async () => Simulate.click(container.querySelector('#setup-tab-wasapi')));
        expect(container.querySelector('#wasapi-panel-devices').textContent).not.toContain('Frames currently queued');
        await act(async () => Simulate.click(container.querySelector('#wasapi-tab-status')));
        expect(container.querySelector('#wasapi-panel-status').hidden).toBe(false);
        expect(container.querySelector('.bridge-metrics').textContent).toContain('Frames currently queued12');
        await act(async () => Simulate.click(container.querySelector('#wasapi-tab-devices')));
        await act(async () => Simulate.click([...container.querySelectorAll('button')].find(button => button.textContent === 'Configure mic')));
        const dialog = document.querySelector('[aria-label="Configure Windows audio bridge"]');
        const help = dialog.querySelector('[aria-label="Choose the audio direction"]');
        help.focus();
        await act(async () => Simulate.click(help));
        expect(document.querySelector('.audio-help-dialog')).not.toBeNull();
        await act(async () => document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })));
        expect(document.querySelector('.audio-help-dialog')).toBeNull();
        expect(document.querySelector('[aria-label="Configure Windows audio bridge"]')).toBe(dialog);
        expect(dialog.querySelector('[aria-label="Bridge ID"]').value).toBe('mic');
        expect(document.activeElement).toBe(help);
        expect(window.vasio.applyConfiguration).not.toHaveBeenCalled();
    });
});

test('legacy engine explains unavailable WASAPI features without restarting audio', async () => {
    await withClockApp(state, async container => {
        expect(container.querySelector('[aria-label="Engine compatibility"]').textContent).toContain('API 1.0');
        expect(container.textContent).toContain('Closing this window does not stop the background engine');
        expect(window.vasio.executeCommand).not.toHaveBeenCalled();
        expect(window.vasio.stopAudio).not.toHaveBeenCalled();
    });
    await withClockApp({ ...state, apiVersion: '1.1' }, async container => {
        expect(container.querySelector('[aria-label="Engine compatibility"]')).toBeNull();
    });
});

test('native audio status opens only from graph menu and never sends configuration', async () => {
    const nativeAudio = { enabled: true, state: 'denied', attached: false, masterRunning: true,
        nativeFormat: { sampleRate: 48000, channels: 2, sampleType: 'pcm32' }, graphFormat: null,
        systemError: 5, retryInMs: 0, health: {} };
    await withClockApp({ ...state, apiVersion: '1.1', nativeAudio }, async container => {
        expect(document.querySelector('[aria-label="Native Windows audio"]')).toBeNull();
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('.patchbay-flow'), 'Native Windows audio status…');
        const dialog = document.querySelector('[role="dialog"][aria-label="Native Windows audio"]');
        expect(dialog).not.toBeNull();
        expect(dialog.textContent).toContain('Access denied');
        expect(dialog.textContent).toContain('48000 Hz');
        expect(dialog.textContent).toContain('Windows error: 5');
        expect(dialog.querySelectorAll('input,select')).toHaveLength(0);
        expect(window.vasio.applyConfiguration).not.toHaveBeenCalled();
        expect(window.vasio.executeCommand).not.toHaveBeenCalled();
        await act(async () => document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })));
        expect(document.querySelector('[aria-label="Native Windows audio"]')).toBeNull();
    });
});

test('native status waits for a fresh snapshot after reconnection', async () => {
    const initial = { ...state, nativeAudio: { state: 'running', enabled: true,
        nativeFormat: { sampleRate: 48000, channels: 2 }, graphFormat: { sampleRate: 44100, channels: 2, maxQuantum: 128 } } };
    await withClockApp(initial, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('.patchbay-flow'), 'Native Windows audio status…');
        const dialog = () => document.querySelector('[aria-label="Native Windows audio"]');
        expect(dialog().textContent).toContain('44100 Hz');
        await act(async () => window.vasio.onDisconnect.mock.calls[0][0]());
        expect(dialog().textContent).toContain('Connection lost');
        let resolveState;
        window.vasio.getState.mockImplementation(() => new Promise(resolve => { resolveState = resolve; }));
        await act(async () => window.electronAPI.onEngineConnection.mock.calls[0][0]({ connected: true }));
        expect(dialog().textContent).toContain('Connection lost');
        expect(dialog().textContent).not.toContain('44100 Hz');
        await act(async () => window.vasio.onDisconnect.mock.calls[0][0]());
        await act(async () => { resolveState(initial); await Promise.resolve(); });
        expect(dialog().textContent).toContain('Connection lost');
        expect(dialog().textContent).not.toContain('44100 Hz');
        await act(async () => window.electronAPI.onEngineConnection.mock.calls[0][0]({ connected: true }));
        await act(async () => { resolveState({ ...initial, nativeAudio: { ...initial.nativeAudio, state: 'unavailable', graphFormat: null } }); await Promise.resolve(); });
        expect(dialog().textContent).toContain('Unavailable');
        expect(dialog().textContent).not.toContain('44100 Hz');
    });
});

test('API 1.1 bridges create, edit, disable and remove through context actions and preserve routes', async () => {
    const initial = { ...state, apiVersion: '1.1', configurationRevision: 7, configuredWasapiBridges: [],
        wasapiBridges: [], wasapiDevices: [{ id: 'mic-device', name: 'Desk microphone', flow: 'capture', state: 1,
            mixFormat: null, error: 'Unsupported device format' }], wasapiInventoryError: null };
    await withClockApp(initial, async container => {
        const button = text => [...document.querySelectorAll('button')].find(item => item.textContent === text);
        expect(container.textContent).toContain('Windows audio devices (WASAPI)');
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('.patchbay-flow'), 'Add Windows audio bridge…');
        const field = name => document.querySelector(`[aria-label="${name}"]`);
        await act(async () => Simulate.change(field('Bridge ID'), { target: { value: 'desk' } }));
        await act(async () => Simulate.change(field('Windows endpoint'), { target: { value: 'mic-device' } }));
        expect(document.querySelector('[role="dialog"]').textContent).toContain('Unsupported device format');
        await act(async () => Simulate.click(button('Save bridge to draft')));
        expect(container.querySelector('[data-node-key="wasapi:desk"]')).not.toBeNull();
        await chooseContextAction(container.querySelector('[data-node-key="wasapi:desk"]'), 'Configure Windows audio bridge…');
        await act(async () => Simulate.change(field('Bridge selection'), { target: { value: 'default' } }));
        await act(async () => Simulate.change(field('Default endpoint role'), { target: { value: 'communications' } }));
        await act(async () => Simulate.change(field('Bridge enabled'), { target: { checked: false } }));
        await act(async () => Simulate.click(button('Save bridge to draft')));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({ expectedConfigurationRevision: 7,
            wasapiBridges: [{ id: 'desk', kind: 'capture', enabled: false, defaultRole: 'communications', bufferDuration100ns: 0 }] }));
    });
    const bridge = { id: 'desk', kind: 'capture', endpointId: 'mic-device', enabled: false, bufferDuration100ns: 0 };
    const route = { id: 'saved', sourceEndpointId: 'wasapi:desk:input:1', destinationEndpointId: 'virtual:TimoxVasio:app:audioapp.exe:input:1', gainDb: 0, mute: false };
    await withClockApp({ ...initial, configuredWasapiBridges: [bridge], configuredRoutes: [route] }, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('[data-node-key="wasapi:desk"]'), 'Remove Windows audio bridge…');
        expect(document.querySelector('[role="dialog"]').textContent).toContain('1 saved connection');
        await act(async () => Simulate.click([...document.querySelectorAll('button')].find(item => item.textContent === 'Remove bridge and its connections')));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({ wasapiBridges: [], routes: [] }));
    });
});

test('Audio Setup separates ASIO, Clock and WASAPI and keeps pending clock edits between tabs', async () => {
    await withClockApp({ ...state, apiVersion: '1.1', configuredWasapiBridges: [], wasapiBridges: [], wasapiDevices: [] }, async container => {
        const tabs = () => [...container.querySelectorAll('[role="tablist"][aria-label="Audio setup categories"] [role="tab"]')];
        expect(tabs().map(tab => tab.textContent)).toEqual(['ASIO', 'Clock', 'WASAPI']);
        expect(container.querySelector('#setup-asio').hidden).toBe(false);
        expect(container.querySelector('#setup-clock').hidden).toBe(true);
        await act(async () => Simulate.click(tabs()[1]));
        expect(container.querySelector('#setup-asio').hidden).toBe(true);
        expect(container.querySelector('#setup-clock').hidden).toBe(false);
        expect(container.querySelector('#setup-clock').textContent).not.toContain('Which clock should I choose');
        await act(async () => Simulate.click(container.querySelector('[aria-label="Help with Clock"]')));
        expect(document.querySelector('.audio-help-dialog').textContent).toContain('not MIDI tempo');
        expect(document.querySelector('.audio-help-dialog').textContent).toContain('WASAPI is not a selectable master');
        await act(async () => document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })));
        expect(document.querySelector('.audio-help-dialog')).toBeNull();
        await act(async () => Simulate.change(container.querySelector('[aria-label="Clock source"]'), { target: { value: 'software' } }));
        await act(async () => Simulate.change(container.querySelector('[aria-label="Software sample rate"]'), { target: { value: '96000' } }));
        await act(async () => Simulate.click(tabs()[2]));
        expect(container.querySelector('#setup-wasapi').hidden).toBe(false);
        await act(async () => Simulate.keyDown(tabs()[2], { key: 'ArrowLeft' }));
        expect(container.querySelector('#setup-clock').hidden).toBe(false);
        expect(container.querySelector('[aria-label="Software sample rate"]').value).toBe('96000');
        expect(window.vasio.applyConfiguration).not.toHaveBeenCalled();
    });
});

test('Audio Setup saves an unavailable endpoint offline without trying to enable audio', async () => {
    await withClockApp({ ...state, apiVersion: '1.1', configuredWasapiBridges: [], wasapiBridges: [],
        wasapiDevices: [{ id: 'offline-output', name: 'Output 3/4 (Interface)', flow: 'render', state: 4, mixFormat: null }] }, async container => {
        await act(async () => Simulate.change(container.querySelector('.wasapi-inactive-toggle input'), { target: { checked: true } }));
        await act(async () => Simulate.click(container.querySelector('[data-endpoint-id="offline-output"] button')));
        const dialog = document.querySelector('[aria-label="Configure Windows audio bridge"]');
        const enabled = dialog.querySelector('[aria-label="Bridge enabled"]');
        const save = [...dialog.querySelectorAll('button')].find(button => button.textContent === 'Save bridge to draft');
        expect(enabled.checked).toBe(false);
        expect(dialog.textContent).toContain('save this endpoint offline');
        expect(save.disabled).toBe(false);
        await act(async () => Simulate.change(enabled, { target: { checked: true } }));
        expect(save.disabled).toBe(true);
        expect(dialog.textContent).toContain('Turn off Enabled');
        await act(async () => Simulate.change(enabled, { target: { checked: false } }));
        await act(async () => Simulate.click(save));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            wasapiBridges: [{ id: 'render_1', kind: 'render', endpointId: 'offline-output', enabled: false, bufferDuration100ns: 0 }]
        }));
    });
});

test.each(['render', 'endpointLoopback'])('Windows bridge %s selects render endpoints and supports default roles', async kind => {
    await withClockApp({ ...state, apiVersion: '1.1', configuredWasapiBridges: [], wasapiBridges: [],
        wasapiDevices: [{ id: 'speaker', name: 'Speakers', flow: 'render', state: 1 }, { id: 'mic', name: 'Microphone', flow: 'capture', state: 1 }] }, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('.patchbay-flow'), 'Add Windows audio bridge…');
        const field = name => document.querySelector(`[aria-label="${name}"]`);
        await act(async () => Simulate.change(field('Bridge ID'), { target: { value: kind } }));
        await act(async () => Simulate.change(field('Bridge kind'), { target: { value: kind } }));
        expect([...field('Windows endpoint').options].map(option => option.value)).toEqual(['', 'speaker']);
        await act(async () => Simulate.change(field('Bridge selection'), { target: { value: 'default' } }));
        await act(async () => Simulate.change(field('Default endpoint role'), { target: { value: 'multimedia' } }));
        await act(async () => Simulate.click([...document.querySelectorAll('button')].find(button => button.textContent === 'Save bridge to draft')));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration.mock.calls[0][0].wasapiBridges).toEqual([
            { id: kind, kind, enabled: true, defaultRole: 'multimedia', bufferDuration100ns: 0 }]);
    });
});

test.each([0, 1, 123456, 10000000])('Windows bridge duration shows milliseconds and preserves API precision (%i)', async duration => {
    const bridge = { id: 'speakers', kind: 'render', enabled: true, defaultRole: 'multimedia', bufferDuration100ns: duration };
    await withClockApp({ ...state, apiVersion: '1.1', configuredWasapiBridges: [bridge], wasapiBridges: [], wasapiDevices: [] }, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        const open = () => chooseContextAction(container.querySelector('[data-node-key="wasapi:speakers"]'), 'Configure Windows audio bridge…');
        const save = () => Simulate.click([...document.querySelectorAll('button')].find(button => button.textContent === 'Save bridge to draft'));
        await open();
        expect(document.querySelector('[aria-label="Bridge buffer duration (ms)"]').value).toBe(String(duration / 10000));
        await act(async () => save());
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration.mock.calls[0][0].wasapiBridges[0].bufferDuration100ns).toBe(duration);
        await open();
        const field = document.querySelector('[aria-label="Bridge buffer duration (ms)"]');
        await act(async () => Simulate.change(field, { target: { value: '1000.1' } }));
        expect([...document.querySelectorAll('button')].find(button => button.textContent === 'Save bridge to draft').disabled).toBe(true);
        await act(async () => Simulate.change(field, { target: { value: '10' } }));
        await act(async () => save());
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration.mock.calls[1][0].wasapiBridges[0].bufferDuration100ns).toBe(100000);
    });
});

test('WASAPI capture and render routes apply stable ports while loopback self-render and disabled capture are unavailable', async () => {
    const desired = [
        { id: 'mic', kind: 'capture', endpointId: 'microphone', enabled: true, bufferDuration100ns: 0 },
        { id: 'speakers', kind: 'render', endpointId: 'speaker', enabled: true, bufferDuration100ns: 0 },
        { id: 'loop', kind: 'endpointLoopback', endpointId: 'speaker', enabled: true, bufferDuration100ns: 0 },
        { id: 'off', kind: 'capture', endpointId: 'microphone', enabled: false, bufferDuration100ns: 0 }];
    const runtime = desired.map(bridge => {
        const capture = bridge.kind !== 'render', direction = capture ? 'input' : 'output';
        return { id: bridge.id, state: bridge.enabled ? 'running' : 'disabled', effectiveInfo: { endpointId: bridge.endpointId },
            [capture ? 'inputEndpoints' : 'outputEndpoints']: [{ id: `wasapi:${bridge.id}:${direction}:1`, name: bridge.id, direction, channel: 1 }] };
    });
    await withClockApp({ ...state, apiVersion: '1.1', configuredWasapiBridges: desired, wasapiBridges: runtime, wasapiDevices: [] }, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await addFromMenu(container, 'wasapi:mic:input:1', 'virtual:TimoxVasio:42:input:1');
        await addFromMenu(container, 'virtual:TimoxVasio:42:output:1', 'wasapi:speakers:output:1');
        await chooseContextAction(container.querySelector('.patchbay-flow'), 'Create a new connection');
        const field = name => document.querySelector(`[aria-label="${name}"]`);
        await act(async () => Simulate.change(field('Quick route source'), { target: { value: 'wasapi:loop' } }));
        expect([...field('Quick route destination').options].map(option => option.value)).not.toContain('wasapi:speakers');
        await act(async () => Simulate.change(field('Quick route source'), { target: { value: 'wasapi:off' } }));
        expect(document.querySelector('.quick-routing-add').disabled).toBe(true);
        await act(async () => Simulate.click([...document.querySelector('[role="dialog"]').querySelectorAll('button')].find(button => button.textContent === 'Close')));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration.mock.calls[0][0]).toEqual(expect.objectContaining({ wasapiBridges: desired,
            routes: [expect.objectContaining({ sourceEndpointId: 'wasapi:mic:input:1', destinationEndpointId: 'virtual:TimoxVasio:app:audioapp.exe:input:1' }),
                expect.objectContaining({ sourceEndpointId: 'virtual:TimoxVasio:app:audioapp.exe:output:1', destinationEndpointId: 'wasapi:speakers:output:1' })] }));
    });
});

test.each(['open', 'ambiguous', 'missing-channel'])('saved ASIO ports identify an open client only when unambiguous (%s)', async scenario => {
    const ambiguous = scenario === 'ambiguous';
    const route = { id: 'saved-live', sourceEndpointId: 'virtual:TimoxVasio:app:audioapp.exe:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    const driver = state.virtualDrivers[0];
    await withClockApp({ ...state, configuredRoutes: [route], routes: [{ ...route,
        sourceEndpointId: 'virtual:TimoxVasio:42:output:1' }], virtualDrivers: [{ ...driver,
        clients: ambiguous ? [...driver.clients, { ...driver.clients[0], pid: 43 }] : driver.clients,
        outputEndpoints: scenario === 'missing-channel' ? [] : ambiguous ? [...driver.outputEndpoints, { ...driver.outputEndpoints[0], id: 'virtual:TimoxVasio:43:output:1' }] : driver.outputEndpoints }] }, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        const savedPort = [...container.querySelectorAll('.patchbay-port')].find(port =>
            port.getAttribute('aria-label')?.includes('select as source'));
        expect(savedPort).toBeDefined();
        expect(savedPort.getAttribute('aria-label').includes('Process not open')).toBe(scenario !== 'open');
        if (scenario === 'open') expect(savedPort.getAttribute('aria-label')).toContain('PID 42');
    });
});

test('editing a durable route preserves its app ID alongside an existing PID route', async () => {
    const routes = [
        { id: 'durable', sourceEndpointId: 'virtual:TimoxVasio:app:audioapp.exe:output:1', destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false },
        { id: 'pid-route', sourceEndpointId: 'virtual:TimoxVasio:42:output:1', destinationEndpointId: 'physical:interface-1:output:2', gainDb: 0, mute: false }
    ];
    await withClockApp({ ...state, configuredRoutes: routes,
        routes: routes.map(route => ({ ...route, sourceEndpointId: 'virtual:TimoxVasio:42:output:1' })),
        physicalDrivers: [{ ...state.physicalDrivers[0], outputEndpoints: [...state.physicalDrivers[0].outputEndpoints,
            { id: 'physical:interface-1:output:2', name: 'Output 2', direction: 'output', channel: 2 }] }] }, async container => {
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        const process = container.querySelector('[data-node-key="process:TimoxVasio:42"]');
        expect(process.querySelectorAll('.patchbay-port')).toHaveLength(2);
        expect(process.textContent).not.toContain('Process not open');
        await chooseContextAction(process, 'Modify a connection…');
        const choices = [...document.querySelectorAll('.process-dialog-routes button')].filter(button => button.textContent === 'Edit…');
        expect(choices).toHaveLength(2);
        await act(async () => Simulate.click(choices[0]));
        await act(async () => Simulate.change(document.querySelector('.route-gain-input'), { target: { value: '-3' } }));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            routes: expect.arrayContaining([{ ...routes[0], gainDb: -3 }, routes[1]])
        }));
        expect(window.vasio.applyConfiguration.mock.calls[0][0].routes).toHaveLength(2);
    });
});

test('PWAR settings open from the graph menu without changing audio configuration', async () => {
    await withClockApp(state, async container => {
        window.vasio.httpRequest = jest.fn().mockResolvedValue({ status: 200,
            body: { address: '192.168.66.2', port: 8321, exists: false } });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('.patchbay-flow'), 'Configure PWAR / PipeWire…');
        expect(document.querySelector('[aria-label="PWAR / PipeWire"]')).not.toBeNull();
        expect(window.vasio.httpRequest).toHaveBeenCalledWith('GET', '/api/v1/pwar', undefined);
        expect(window.vasio.applyConfiguration).not.toHaveBeenCalled();
        expect(window.vasio.executeCommand).not.toHaveBeenCalled();
    });
});

test('software clock stages explicit format and applies without a physical driver', async () => {
    await withClockApp(state, async container => {
        const source = container.querySelector('[aria-label="Clock source"]');
        expect(source).not.toBeNull();
        await act(async () => Simulate.change(source, { target: { value: 'software' } }));
        expect(container.querySelector('[aria-label="Software sample rate"]').value).toBe('48000');
        expect(container.querySelector('[aria-label="Software quantum"]').value).toBe('256');
        await act(async () => Simulate.change(container.querySelector('[aria-label="Software sample rate"]'), { target: { value: '96000' } }));
        await act(async () => Simulate.change(container.querySelector('[aria-label="Software quantum"]'), { target: { value: '128' } }));
        window.vasio.getState.mockResolvedValueOnce(state).mockResolvedValue({ ...state,
            engine: { ...state.engine, state: 'running', clockSource: { kind: 'software' },
                physicalDriverId: null, sampleRate: 96000, bufferFrames: 128 } });
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            clockSource: { kind: 'software' }, physicalDriverId: null, sampleRate: 96000, bufferFrames: 128, routes: [] }));
        expect(container.querySelector('.notice').textContent).not.toContain('remains stopped');
    });
});

test('software clock applies with hardware routes preserved dormant and can keep the running source', async () => {
    const route = { id: 'keep', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    await withClockApp({ ...state, routes: [route] }, async container => {
        const source = container.querySelector('[aria-label="Clock source"]');
        expect(source).not.toBeNull();
        await act(async () => Simulate.change(source, { target: { value: 'software' } }));
        expect(container.querySelector('.apply-configuration').disabled).toBe(false);
        expect(container.textContent).toContain('physical connections remain saved');
        await act(async () => Simulate.click([...container.querySelectorAll('button')].find(button => button.textContent === 'Keep running clock')));
        expect(source.value).toBe('asio');
        await act(async () => Simulate.change(source, { target: { value: 'software' } }));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            physicalDriverId: null, clockSource: { kind: 'software' }, routes: [route] }));
    });
});

test('running software clock exposes reset and stop and excludes hardware from routing', async () => {
    await withClockApp({ ...state, engine: { ...state.engine, state: 'running', physicalDriverId: null,
        clockSource: { kind: 'software' } }, virtualDrivers: [{ ...state.virtualDrivers[0], clients: [] }] }, async container => {
        expect(container.querySelector('.engine-reset')).not.toBeNull();
        await act(async () => { Simulate.click(container.querySelector('.engine-reset')); await new Promise(resolve => setTimeout(resolve, 170)); });
        expect(window.vasio.executeCommand).toHaveBeenCalledWith(expect.objectContaining({ command: 'engine.reset' }));
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.querySelector('[data-port-id="physical:interface-1:output:1"]')).toBeNull();
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[3]));
        expect(container.querySelector('.workspace-heading').textContent).toContain('Software');
        expect(container.querySelector('.buffer-health h2').textContent).toBe('Software clock callback');
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[5]));
        expect(container.querySelector('.engine-stop')).not.toBeNull();
        await act(async () => { Simulate.click(container.querySelector('.engine-stop')); await new Promise(resolve => setTimeout(resolve, 170)); });
        expect(window.vasio.stopAudio).toHaveBeenCalledTimes(1);
    });
});

test('software clock rejects invalid formats and switching back to hardware resets the source', async () => {
    await withClockApp(state, async container => {
        const source = container.querySelector('[aria-label="Clock source"]');
        await act(async () => Simulate.change(source, { target: { value: 'software' } }));
        for (const value of ['0', '7999', '384001', '48000.5']) {
            await act(async () => Simulate.change(container.querySelector('[aria-label="Software sample rate"]'), { target: { value } }));
            expect(container.querySelector('.apply-configuration').disabled).toBe(true);
        }
        await act(async () => Simulate.change(source, { target: { value: 'asio' } }));
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(window.vasio.applyConfiguration).toHaveBeenCalledWith(expect.objectContaining({
            physicalDriverId: 'interface-1' }));
        expect(window.vasio.applyConfiguration.mock.calls[0][0].clockSource?.kind || 'asio').toBe('asio');
    });
});

test('route updates from the engine keep unapplied edits available to Apply', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    let onEvent;
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = { getState: jest.fn().mockResolvedValue(state),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(callback => { onEvent = callback; return () => {}; }),
        onDisconnect: jest.fn(() => () => {}), applyConfiguration };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        await addFromMenu(container, 'virtual:TimoxVasio:42:output:1', 'physical:interface-1:output:1');
        expect(container.querySelector('.apply-bar button').disabled).toBe(false);
        await act(async () => { onEvent({ event: 'routes.changed', payload: { routes: [] } }); await Promise.resolve(); });
        expect(container.querySelector('.apply-bar button').disabled).toBe(false);
        expect(container.querySelector('.patchbay')).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector('.apply-bar button')));
        expect(applyConfiguration.mock.calls[0][0].routes).toHaveLength(1);
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('pending edits can be discarded without applying or deleting saved connections', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const applyConfiguration = jest.fn();
    window.vasio = { getState: jest.fn().mockResolvedValue(state),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        await addFromMenu(container, 'virtual:TimoxVasio:42:output:1', 'physical:interface-1:output:1');
        expect(container.querySelector('.discard-draft')).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector('.discard-draft')));
        expect(applyConfiguration).not.toHaveBeenCalled();
        expect(container.querySelector('.discard-draft')).toBeNull();
        expect(container.textContent).toContain('Saved connections remain unchanged');
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
});

test('routing graph retains saved application ports outside the live channel inventory', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const sourceId = 'virtual:TimoxVasio:app:traktor4.exe:output:9';
    const route = { id: 'saved-traktor', sourceEndpointId: sourceId,
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    const savedState = { ...state, configuredRoutes: [route], routes: [],
        virtualDrivers: [{ ...state.virtualDrivers[0], clients: [{
            pid: 42, processName: 'Traktor Pro 4.exe', inputChannels: [], outputChannels: [1, 2] }] }] };
    window.vasio = { getState: jest.fn().mockResolvedValue(savedState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.querySelector(`[data-port-id="${sourceId}"]`)).not.toBeNull();
        expect(container.textContent).toContain('traktor4.exe · Process not open');
        expect(container.textContent).not.toContain('Out 9 (saved)');
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
});

test('quick routing suggests the next free stereo pair and stages both channels for apply', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const channels = [1, 2, 3, 4];
    const routedState = { ...state, physicalDrivers: [{ ...state.physicalDrivers[0],
        outputEndpoints: channels.map(channel => ({ id: `physical:interface-1:output:${channel}`,
            name: `Sortie ${channel}`, direction: 'output', channel })) }],
    virtualDrivers: [{ ...state.virtualDrivers[0],
        clients: [{ pid: 42, processName: 'AudioApp.exe', inputChannels: [], outputChannels: channels }],
        inputEndpoints: [],
        outputEndpoints: channels.map(channel => ({ id: `virtual:TimoxVasio:42:output:${channel}`,
            name: `AudioApp Out ${channel}`, direction: 'output', channel })) }] };
    const applyConfiguration = jest.fn().mockResolvedValue({ accepted: true });
    window.vasio = { getState: jest.fn().mockResolvedValue(routedState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await act(async () => Simulate.contextMenu(container.querySelector('.react-flow__pane'), { clientX: 100, clientY: 100 }));
        await act(async () => Simulate.click(document.querySelector('.patchbay-context-menu [role="menuitem"]')));
        expect(document.querySelector('.routing-dialog .quick-routing')).not.toBeNull();
        expect(document.querySelector('.quick-routing-preview').textContent).toContain('1–2 → Interface réelle · Outputs 1–2');
        await act(async () => Simulate.click(document.querySelector('.quick-routing-add')));
        expect(document.querySelector('.quick-routing-preview').textContent).toContain('3–4 → Interface réelle · Outputs 3–4');
        await act(async () => Simulate.click(document.querySelector('.quick-routing-add')));
        expect(container.querySelector('.apply-bar').textContent).toContain('Pending changes');
        expect(applyConfiguration).not.toHaveBeenCalled();
        await act(async () => Simulate.click(container.querySelector('.apply-configuration')));
        expect(applyConfiguration.mock.calls[0][0].routes.map(route => [
            route.sourceEndpointId, route.destinationEndpointId
        ])).toEqual([
            ['virtual:TimoxVasio:app:audioapp.exe:output:1', 'physical:interface-1:output:1'],
            ['virtual:TimoxVasio:app:audioapp.exe:output:2', 'physical:interface-1:output:2'],
            ['virtual:TimoxVasio:app:audioapp.exe:output:3', 'physical:interface-1:output:3'],
            ['virtual:TimoxVasio:app:audioapp.exe:output:4', 'physical:interface-1:output:4']
        ]);
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
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        const sourceId = 'virtual:TimoxVasio:app:mixxx.exe:output:1';
        const destinationId = 'virtual:TimoxVasio:app:scsynth.exe:input:1';
        await addFromMenu(container, sourceId, destinationId);
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

test('resetting the physical driver keeps the saved configuration and does not apply the draft', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const executeCommand = jest.fn().mockResolvedValue({ accepted: true });
    const getState = jest.fn().mockResolvedValue({ ...state,
        engine: { ...state.engine, state: 'running', physicalDriverId: 'interface-1' } });
    window.vasio = { getState, executeCommand,
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const reset = container.querySelector('.engine-reset');
        expect(reset).not.toBeNull();
        await act(async () => { Simulate.click(reset); await new Promise(resolve => setTimeout(resolve, 200)); });
        expect(executeCommand).toHaveBeenCalledWith(expect.objectContaining({ command: 'engine.reset' }));
        expect(container.textContent).toContain('Pending edits were not applied');
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
});

test('performance view distinguishes measured ASIO buffer pressure from missing observations', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const measured = { ...state, engine: { ...state.engine, state: 'running', sampleRate: 44100, bufferFrames: 512 },
        bufferHealth: { state: 'warning', sampleRate: 44100,
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
        expect(container.querySelector('.performance-guide').open).toBe(false);
        expect(container.querySelector('main > section')).toBe(container.querySelector('.buffer-health'));
        expect(container.querySelector('.buffer-concepts')?.textContent).toContain('512 frames ÷ 44,100 frames/s = 11.61 ms');
        expect(container.querySelector('.buffer-concepts')?.textContent).toContain('A and B take turns at each callback');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Slowest callback');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Time available');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Delayed starts');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Processing overruns');
        expect(container.querySelector('.buffer-health')?.textContent).toContain('Callback timing incidents were observed.');
        expect(container.querySelector('.buffer-health')?.textContent).toContain('44100 Hz');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Processing overruns6');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Delayed starts7');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Over time0.89 ms');
    } finally {
        await act(async () => root.unmount());
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('performance view explains physical buffer measurements without suggesting another size', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    let onEvent;
    const measured = { ...state, engine: { ...state.engine, state: 'running', sampleRate: 48000, bufferFrames: 1024 },
        virtualDrivers: [{ ...state.virtualDrivers[0], clients: [{ ...state.virtualDrivers[0].clients[0],
            processCpuPercent: 6.25, processCpuWindowMs: 2000 }] }],
        routes: [{ id: 'test-route', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false }], bufferHealth: { state: 'healthy', sampleRate: 48000,
        bufferFrames: 1024, windowDurationMs: 5000, callbackCount: 235,
        delayedStartCount: 0, processingOverrunCount: 0, blockPeriodNs: 21333333,
        worstProcessingNs: 2500000, worstMarginNs: 18833333 },
        performanceMeasurements: { enabled: true, sampleEveryCallbacks: 16,
            window: { durationMs: 5000, clients: [{ processId: 42, driverId: 'TimoxVasio',
                hostCallback: { samples: 2, meanNs: 5000000, worstNs: 24000000 },
                ringWrite: null, ringWait: null, ringWaitDiscardedBlocks: 0 }],
            physical: { clientRead: null, routing: null, outputCopy: null } } } };
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
        expect(container.querySelector('.workspace-heading').textContent).toContain('Interface réelle');
        expect(container.querySelector('.performance-guide').open).toBe(false);
        expect(container.querySelector('.performance-flow')?.textContent).toContain('ASIO application');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('Physical ASIO driver');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('shared ring');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('next physical callback');
        expect(container.querySelector('.performance-flow')?.textContent).toContain('not measured');
        expect(container.querySelector('.transport-health')?.textContent).toContain('AudioApp.exe output');
        expect(container.querySelector('.routed-app-cpu')?.textContent).toContain('AudioApp.exe');
        expect(container.querySelector('.routed-app-cpu')?.textContent).toContain('6.3%');
        expect(container.querySelector('.routed-app-cpu')?.textContent).toContain('112.5%');
        expect(container.querySelector('.callback-over-budget')?.textContent).toContain('Audio deadline exceeded');
        expect(container.querySelector('.transport-health')?.textContent).toContain('—');
        await act(async () => onEvent({ event: 'audio.meter', payload: { endpointId: 'virtual:TimoxVasio:42:output:1', peakDbfs: -12, underruns: 2, overruns: 1 } }));
        expect(container.querySelector('.transport-table')?.textContent).toContain('AudioApp.exe output21');
        expect(container.querySelector('.buffer-timeline')?.getAttribute('aria-label')).toContain('2.50 ms of processing within a 21.33 ms block');
        expect(container.querySelector('.buffer-health').textContent).toContain('5.0 s window');
        await act(async () => Simulate.click(container.querySelector('.workspace-help')));
        expect(document.querySelector('.audio-help-dialog').textContent).toContain('Callback timing');
        expect(document.querySelector('.audio-help-dialog').textContent).toContain('application-to-speaker latency');
        await act(async () => document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })));
        await act(async () => Simulate.click(container.querySelector('#performance-tab-applications')));
        await act(async () => Simulate.click(container.querySelector('.workspace-help')));
        expect(document.querySelector('.audio-help-dialog').textContent).toContain('all logical processors');
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
        await act(async () => { jest.advanceTimersByTime(5000); await Promise.resolve(); });
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('43.8 µs');
        expect(container.querySelector('.performance-metrics')?.textContent).toContain('Callbacks observed235');
    } finally {
        await act(async () => root.unmount());
        jest.useRealTimers();
        container.remove(); delete window.vasio; delete window.electronAPI;
    }
});

test('switching physical drivers migrates existing hardware connections by channel', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    const physicalRoute = { id: 'hardware', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 3, mute: true };
    const virtualRoute = { id: 'virtual', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'virtual:TimoxVasio:42:input:1', gainDb: 0, mute: false };
    const driverState = { ...state, engine: { ...state.engine, physicalDriverId: 'interface-1' },
        physicalDrivers: [...state.physicalDrivers,
            { id: 'asio4all-1', name: 'ASIO4ALL v2', inputEndpoints: [], outputEndpoints: [],
                sampleRates: [], bufferSizes: null, capabilitiesKnown: false }],
        routes: [physicalRoute, virtualRoute], configuredRoutes: [physicalRoute, virtualRoute] };
    let currentState = driverState;
    const applyConfiguration = jest.fn().mockImplementation(async draft => {
        currentState = { ...currentState, engine: { ...currentState.engine, physicalDriverId: draft.physicalDriverId },
            physicalDrivers: currentState.physicalDrivers.map(driver => driver.id === 'asio4all-1'
                ? { ...driver, capabilitiesKnown: true, outputEndpoints: [
                    { id: 'physical:asio4all-1:output:1', name: 'ASIO4ALL Out 1', direction: 'output', channel: 1 }
                ] } : driver),
            routes: draft.routes, configuredRoutes: draft.routes };
        return { accepted: true };
    });
    window.vasio = { getState: jest.fn().mockImplementation(async () => currentState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        const driverSelect = [...container.querySelectorAll('select')].find(select => select.value === 'interface-1');
        await act(async () => Simulate.change(driverSelect, { target: { value: '' } }));
        expect(container.querySelector('.driver-change-blocker').textContent).toContain('choose a driver below');
        expect(container.querySelector('.migrate-hardware-routes').disabled).toBe(true);
        expect(container.querySelector('.migrate-hardware-routes').textContent).toBe('Choose a replacement driver first');
        await act(async () => Simulate.change(container.querySelector('[aria-label="Replacement physical ASIO driver"]'),
            { target: { value: 'asio4all-1' } }));
        expect(container.querySelector('.driver-change-blocker').textContent).toContain('Physical driver change not applied');
        expect(container.querySelector('.migrate-hardware-routes').disabled).toBe(false);
        expect(container.querySelector('.apply-configuration').disabled).toBe(true);
        await act(async () => { Simulate.click(container.querySelector('.migrate-hardware-routes'));
            await new Promise(resolve => setTimeout(resolve, 0)); });
        expect(applyConfiguration).toHaveBeenCalledTimes(2);
        expect(applyConfiguration.mock.calls[0][0].routes).toEqual([physicalRoute, virtualRoute]);
        expect(applyConfiguration.mock.calls[1][0].routes).toEqual([virtualRoute,
            { ...physicalRoute, destinationEndpointId: 'physical:asio4all-1:output:1' }]);
        expect(container.textContent).toContain('1 hardware connection transferred to ASIO4ALL v2');
        expect(window.localStorage.getItem('timoxvasio.hardware-transfer.v1')).toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove(); window.localStorage.clear();
        delete window.vasio; delete window.electronAPI;
    }
});

test('a missing new-driver channel keeps a recoverable copy of the old connection', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    const route = { id: 'right', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:2', gainDb: 0, mute: false };
    const target = { id: 'new-card', name: 'New card', inputEndpoints: [], outputEndpoints: [],
        sampleRates: [], bufferSizes: null, capabilitiesKnown: false };
    let currentState = { ...state, configuredRoutes: [route], routes: [route],
        physicalDrivers: [...state.physicalDrivers, target] };
    const applyConfiguration = jest.fn().mockImplementation(async draft => {
        currentState = { ...currentState, engine: { ...currentState.engine, physicalDriverId: draft.physicalDriverId },
            configuredRoutes: draft.routes, routes: draft.routes,
            physicalDrivers: [...state.physicalDrivers, { ...target, capabilitiesKnown: true,
                outputEndpoints: [{ id: 'physical:new-card:output:1' }] }] };
        return { accepted: true };
    });
    window.vasio = { getState: jest.fn().mockImplementation(async () => currentState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}),
        applyConfiguration };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.change([...container.querySelectorAll('select')]
            .find(select => select.value === 'interface-1'), { target: { value: 'new-card' } }));
        await act(async () => { Simulate.click(container.querySelector('.migrate-hardware-routes'));
            await new Promise(resolve => setTimeout(resolve, 0)); });
        expect(applyConfiguration).toHaveBeenCalledTimes(1);
        expect(container.textContent).toContain('new driver lacks output 2');
        expect(container.querySelector('.driver-change-recovery')).not.toBeNull();
        expect(window.localStorage.getItem('timoxvasio.hardware-transfer.v1')).toContain('physical:interface-1:output:2');
        expect(container.querySelector('.driver-change-recovery button').disabled).toBe(true);
        await act(async () => { Simulate.click([...container.querySelectorAll('.driver-change-recovery button')]
            .find(button => button.textContent.includes('Restore previous driver')));
            await new Promise(resolve => setTimeout(resolve, 0)); });
        expect(applyConfiguration).toHaveBeenCalledTimes(3);
        expect(applyConfiguration.mock.calls[2][0].routes).toEqual([route]);
        expect(container.textContent).toContain('Previous driver and connections restored.');
        expect(window.localStorage.getItem('timoxvasio.hardware-transfer.v1')).toBeNull();
    } finally {
        await act(async () => root.unmount()); container.remove(); window.localStorage.clear();
        delete window.vasio; delete window.electronAPI;
    }
});

test.each(['1.0', '1.1'])('failed hardware transfer restores software clock, dormant connections and bridges on API %s', async apiVersion => {
    const bridges = [{ id: 'keep', kind: 'capture', defaultRole: 'console', enabled: false, bufferDuration100ns: 0 }];
    const route = { id: 'dormant-right', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:2', gainDb: -3, mute: false };
    const target = { id: 'new-card', name: 'New card', inputEndpoints: [], outputEndpoints: [],
        sampleRates: [], bufferSizes: null, capabilitiesKnown: false };
    let current = { ...state, apiVersion, ...(apiVersion === '1.1' ? { configuredWasapiBridges: bridges, wasapiBridges: [], wasapiDevices: [] } : {}),
        engine: { ...state.engine, state: 'running', physicalDriverId: null,
        clockSource: { kind: 'software' }, sampleRate: 96000, bufferFrames: 128 },
        configuredRoutes: [route], routes: [], physicalDrivers: [target] };
    await withClockApp(current, async container => {
        window.vasio.getState.mockImplementation(async () => current);
        window.vasio.applyConfiguration.mockImplementation(async draft => {
            if (apiVersion === '1.1') expect(draft.wasapiBridges).toEqual(bridges);
            else expect(draft).not.toHaveProperty('wasapiBridges');
            const knownEndpoints = new Set([
                ...current.physicalDrivers.flatMap(driver => [...driver.inputEndpoints, ...driver.outputEndpoints].map(endpoint => endpoint.id)),
                ...current.virtualDrivers.flatMap(driver => [...driver.inputEndpoints, ...driver.outputEndpoints].map(endpoint => endpoint.id)),
                ...current.configuredRoutes.flatMap(saved => [saved.sourceEndpointId, saved.destinationEndpointId])
            ]);
            if (draft.routes.some(saved => [saved.sourceEndpointId, saved.destinationEndpointId].some(endpoint => !knownEndpoints.has(endpoint))))
                throw new Error('UNKNOWN_ENDPOINT');
            current = { ...current, engine: { ...current.engine, physicalDriverId: draft.physicalDriverId,
                clockSource: draft.clockSource, sampleRate: draft.sampleRate, bufferFrames: draft.bufferFrames },
                configuredRoutes: draft.routes, routes: [], physicalDrivers: [
                    { ...target, capabilitiesKnown: true, outputEndpoints: [{ id: 'physical:new-card:output:1' }] }] };
            return { accepted: true };
        });
        await act(async () => Simulate.change(container.querySelector('[aria-label="Clock source"]'), { target: { value: 'asio' } }));
        await act(async () => Simulate.change(container.querySelector('[aria-label="Replacement physical ASIO driver"]'), { target: { value: 'new-card' } }));
        await act(async () => Simulate.click(container.querySelector('.migrate-hardware-routes')));
        expect(container.textContent).toContain('new driver lacks output 2');
        const restore = [...container.querySelectorAll('.driver-change-recovery button')]
            .find(button => button.textContent.includes('Restore previous'));
        expect(restore).toBeDefined();
        await act(async () => Simulate.click(restore));
        expect(window.vasio.applyConfiguration).toHaveBeenLastCalledWith(expect.objectContaining({
            clockSource: { kind: 'software' }, physicalDriverId: null, sampleRate: 96000,
            bufferFrames: 128, routes: [route] }));
        expect(container.querySelector('.driver-change-recovery')).toBeNull();
        expect(container.querySelector('[aria-label="Clock source"]').value).toBe('software');
        expect(container.querySelector('[aria-label="Clock source"]').disabled).toBe(false);
        expect(window.localStorage.getItem('timoxvasio.hardware-transfer.v1')).toBeNull();
    });
});

test('a connection label and color persist locally without applying audio configuration', async () => {
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
        expect(document.querySelector('.patchbay-inspector')).toBeNull();
        await chooseContextAction(container.querySelector('[data-node-key="process:TimoxVasio:42"]'), 'Modify a connection…');
        await act(async () => Simulate.click(document.querySelector('.process-dialog-routes button')));
        await act(async () => Simulate.change(document.querySelector('.route-label-input'), { target: { value: 'Headphone' } }));
        await act(async () => Simulate.change(document.querySelector('.route-color-input'), { target: { value: '#bd3f47' } }));
        expect(document.querySelector('.route-color-input').value).toBe('#bd3f47');
        expect(document.body.textContent).toContain('Headphone');
        expect(container.textContent).toContain('Configuration applied');
        expect(container.querySelector('.apply-configuration').disabled).toBe(true);
        await act(async () => { await Simulate.click(container.querySelector('.apply-configuration')); });
        expect(applyConfiguration).not.toHaveBeenCalled();
        await act(async () => root.unmount());
        window.vasio.getState.mockResolvedValue({ ...savedState, routes: [{ ...route, id: 'engine-reassigned-id' }] });
        root = createRoot(container);
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        await chooseContextAction(container.querySelector('[data-node-key="process:TimoxVasio:42"]'), 'Modify a connection…');
        await act(async () => Simulate.click(document.querySelector('.process-dialog-routes button')));
        expect(document.querySelector('.route-label-input').value).toBe('Headphone');
        expect(document.querySelector('.route-color-input').value).toBe('#bd3f47');
    } finally {
        await act(async () => root.unmount());
        container.remove();
        window.localStorage.clear();
        delete window.vasio; delete window.electronAPI;
    }
});

test('bulk appearance edits update several connections without applying audio configuration', async () => {
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
        await chooseContextAction(container.querySelector('.react-flow__pane'), 'Edit appearance of several connections…');
        const checkboxes = document.querySelectorAll('.bulk-route-checkbox');
        expect(checkboxes).toHaveLength(2);
        for (const checkbox of checkboxes) await act(async () => Simulate.change(checkbox, { target: { checked: true } }));
        await act(async () => Simulate.change(document.querySelector('.bulk-label-input'), { target: { value: 'Headphone' } }));
        await act(async () => Simulate.change(document.querySelector('.bulk-color-input'), { target: { value: '#bd3f47' } }));
        await act(async () => Simulate.click(document.querySelector('.bulk-apply')));
        expect(window.localStorage.getItem('timoxvasio.route-visuals.v1')).toContain('Headphone');
        expect(window.localStorage.getItem('timoxvasio.route-visuals.v1')).toContain('#bd3f47');
        await act(async () => Simulate.change(document.querySelector('.bulk-property-toggle input'), { target: { checked: false } }));
        await act(async () => Simulate.change(document.querySelector('.bulk-color-input'), { target: { value: '#3f725a' } }));
        await act(async () => Simulate.click(document.querySelector('.bulk-apply')));
        expect(document.querySelector('.bulk-route-list').textContent).toContain('Headphone');
        expect(window.localStorage.getItem('timoxvasio.route-visuals.v1')).toContain('#3f725a');
        expect(container.textContent).toContain('Configuration applied');
        expect(container.querySelector('.apply-configuration').disabled).toBe(true);
        await act(async () => { await Simulate.click(container.querySelector('.apply-configuration')); });
        expect(applyConfiguration).not.toHaveBeenCalled();
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
        expect(container.querySelector('.setup-physical').compareDocumentPosition(container.querySelector('.setup-profiles'))
            & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
        expect(container.querySelector('.setup-profiles').textContent).toContain('mixxx.exe');
        expect(container.querySelector('.setup-profiles').textContent).toContain('255');
        await act(async () => Simulate.click(container.querySelectorAll('.view-tabs button')[1]));
        expect(container.textContent).not.toContain('Process and channel detail');
        expect(container.querySelector('.patchbay')).not.toBeNull();
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(0);
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        const appNode = container.querySelector('[data-node-key="process:TimoxVasio:42"]');
        expect(appNode.querySelectorAll('.patchbay-port')).toHaveLength(0);
        expect(appNode.querySelector('.patchbay-capacity').textContent).toContain('In 0/');
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});

test('Routing assigns a profile to an existing process and preserves other profiles', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const profiles = [{ processName: 'mixxx.exe', inputChannels: 255, outputChannels: 255 },
        { processName: 'AudioApp.exe', inputChannels: 2, outputChannels: 192 }];
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
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Routing')));
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        await chooseContextAction(container.querySelector('[data-node-key="process:TimoxVasio:42"]'), 'Configure process…');
        const outputChannels = document.querySelector('[aria-label="Process profile output channels"]');
        expect(document.querySelector('[aria-label="Process profile input channels"]').value).toBe('2');
        await act(async () => {
            Simulate.change(outputChannels, { target: { value: '192' } });
        });
        await act(async () => {
            Simulate.click([...document.querySelectorAll('.patchbay-process-editor button')]
                .find(button => button.textContent === 'Assign profile to this process'));
            await Promise.resolve();
        });
        expect(window.vasio.replaceApplicationProfiles).toHaveBeenCalledWith(profiles);
        expect(document.body.textContent).toContain('Restart the application');
    } finally {
        await act(async () => { root.unmount(); });
        container.remove();
        delete window.vasio;
        delete window.electronAPI;
    }
});

test('a process profile cannot hide a channel used by a saved connection', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const source = { id: 'virtual:TimoxVasio:42:output:3', name: 'AudioApp Out 3', direction: 'output', channel: 3 };
    const route = { id: 'third', sourceEndpointId: source.id,
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    const routedState = { ...state, routes: [route], virtualDrivers: [{ ...state.virtualDrivers[0],
        clients: [{ ...state.virtualDrivers[0].clients[0], outputChannels: [1, 2, 3] }],
        outputEndpoints: [...state.virtualDrivers[0].outputEndpoints, source] }] };
    const replaceApplicationProfiles = jest.fn();
    window.vasio = { getState: jest.fn().mockResolvedValue(routedState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        replaceApplicationProfiles, subscribeEvents: jest.fn(() => () => {}),
        onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Routing')));
        await chooseContextAction(container.querySelector('[data-node-key="process:TimoxVasio:42"]'), 'Configure process…');
        await act(async () => { Simulate.click([...document.querySelectorAll('.patchbay-process-editor button')]
            .find(button => button.textContent === 'Assign profile to this process'));
            await Promise.resolve(); });
        expect(replaceApplicationProfiles).not.toHaveBeenCalled();
        expect(document.querySelector('.patchbay-process-editor').textContent).toContain('Saved connection uses output channel 3');
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
});

test('removing a process configuration removes its routes and profile while preserving other routes', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const profile = { processName: 'AudioApp.exe', inputChannels: 2, outputChannels: 2 };
    const processRoute = { id: 'process-out', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:interface-1:output:1', gainDb: 0, mute: false };
    const profileRoute = { id: 'profile-in', sourceEndpointId: 'physical:interface-1:input:1',
        destinationEndpointId: 'virtual:TimoxVasio:app:audioapp.exe:input:1', gainDb: 0, mute: false };
    const otherRoute = { id: 'other', sourceEndpointId: 'physical:interface-1:input:1',
        destinationEndpointId: 'virtual:TimoxVasio:99:input:1', gainDb: 0, mute: false };
    let currentState = { ...state, routes: [processRoute, profileRoute, otherRoute],
        virtualDrivers: [{ ...state.virtualDrivers[0],
            clients: [...state.virtualDrivers[0].clients,
                { pid: 99, processName: 'Other.exe', inputChannels: [1], outputChannels: [] }],
            inputEndpoints: [...state.virtualDrivers[0].inputEndpoints,
                { id: 'virtual:TimoxVasio:99:input:1', name: 'Other In 1', direction: 'input', channel: 1 }] }] };
    const applyConfiguration = jest.fn().mockImplementation(async draft => {
        currentState = { ...currentState, routes: draft.routes, configuredRoutes: draft.routes };
        return { accepted: true };
    });
    const replaceApplicationProfiles = jest.fn().mockResolvedValue({ profiles: [], restartRequiredClients: [] });
    window.vasio = { getState: jest.fn().mockImplementation(async () => currentState),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [profile] }),
        replaceApplicationProfiles, applyConfiguration,
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}),
        onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => { root.render(<App />); await Promise.resolve(); });
        await act(async () => Simulate.click([...container.querySelectorAll('.view-tabs button')]
            .find(button => button.textContent === 'Routing')));
        await chooseContextAction(container.querySelector('[data-node-key="process:TimoxVasio:42"]'), 'Configure process…');
        expect(document.querySelector('.patchbay-process-editor').textContent).toContain('2 saved connections');
        await act(async () => Simulate.click(document.querySelector('.remove-process-configuration')));
        expect(document.querySelector('.process-remove-confirm').textContent).toContain('Windows process stays open');
        await act(async () => { Simulate.click(document.querySelector('.process-remove-confirm button'));
            await new Promise(resolve => setTimeout(resolve, 0)); });
        expect(applyConfiguration.mock.calls[0][0].routes).toEqual([otherRoute]);
        expect(replaceApplicationProfiles).toHaveBeenCalledWith([]);
        expect(container.textContent).toContain('2 saved connections removed and profile removed');
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
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
        await act(async () => Simulate.contextMenu(container.querySelector('.react-flow__pane'), { clientX: 100, clientY: 100 }));
        await act(async () => Simulate.click([...document.querySelectorAll('.patchbay-context-menu [role="menuitem"]')]
            .find(button => button.textContent === 'Process and channel detail')));
        await act(async () => publishEvent({ event: 'audio.meter', payload: { endpointId: pair.leftEndpointId, peakDbfs: -9, underruns: 0, overruns: 0 } }));
        expect(document.querySelectorAll('.routing-dialog .channel-meter')).toHaveLength(2);
        expect(document.querySelector('.routing-dialog').textContent).toContain('-9.0 dBFS');
        await act(async () => Simulate.click(document.querySelector('.routing-dialog-heading button')));

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
        physical: { clientRead: null, routing: null, outputCopy: null } } }, 'No stage samples'],
    ['no application path', { enabled: true, sampleEveryCallbacks: 16, window: {
        durationMs: 5000, clients: [], physical: {
            clientRead: null, routing: { samples: 2, meanNs: 20000, worstNs: 30000 },
            outputCopy: { samples: 2, meanNs: 5000, worstNs: 7000 } } } }, 'No active ASIO application path']
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
        expect(flow).toContain('full work the application performs');
        expect(flow).toContain('same complete block; not acoustic latency');
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
