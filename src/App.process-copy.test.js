import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import App from './App';

jest.mock('./RoutingPatchbay', () => props => <div>
    <button onClick={() => props.onEditBridge('app_1')}>Edit copy</button>
    <button onClick={props.onShowProfiles}>Catalogue</button>
    <button onClick={() => props.onCreateConnection('')}>New connection</button>
    <button onClick={props.onCreateBridge}>New bridge</button>
    <pre data-testid="graph">{JSON.stringify({ sources: props.sourceGroups, routes: props.routes })}</pre>
</div>);
jest.mock('./QuickRouting', () => props => <pre data-testid="connection-sources">{JSON.stringify(props.sourceGroups)}</pre>);

const process = { processId: 42, creationTime: '100', imagePath: 'C:\\Apps\\firefox.exe', displayName: 'firefox.exe', renderEndpoints: ['Output 5/6 (SSL)'] };
const bridge = { id: 'app_1', kind: 'processLoopback', enabled: true, processId: 42,
    processCreationTime: '100', processImagePath: process.imagePath, bufferDuration100ns: 0 };
const template = { id: 'app_1', name: 'ProcessCopy+firefox.exe', processName: 'firefox.exe', bufferDuration100ns: 0 };
const initialState = {
    apiVersion: '1.1', capabilities: { processCopyTemplates: true, wasapiProcessLoopback: true },
    engine: { state: 'running', clockSource: { kind: 'software' }, physicalDriverId: '', sampleRate: 48000, bufferFrames: 128 },
    physicalDrivers: [], virtualDrivers: [], routes: [], configuredRoutes: [], configuredWasapiBridges: [bridge],
    wasapiBridges: [{ id: 'app_1', state: 'unavailable', inputEndpoints: [], outputEndpoints: [] }], wasapiDevices: [], stereoPairs: []
};

const button = text => [...document.querySelectorAll('button')].find(item => item.textContent === text);
async function withApp(state, processes, check) {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.clear();
    window.vasio = { getState: jest.fn().mockResolvedValue(state),
        getApplicationProfiles: jest.fn().mockResolvedValue({ profiles: [] }),
        getProcessCopyTemplates: jest.fn().mockResolvedValue({ templates: [template] }),
        replaceProcessCopyTemplates: jest.fn().mockImplementation(async templates => ({ templates })),
        getWindowsAudioApplications: jest.fn().mockResolvedValue({ windowsAudioApplications: processes, windowsAudioApplicationInventoryError: null }),
        getDiagnostics: jest.fn().mockResolvedValue({ level: 'info', entries: [] }),
        subscribeEvents: jest.fn(() => () => {}), onDisconnect: jest.fn(() => () => {}) };
    window.electronAPI = { onEngineConnection: jest.fn(() => () => {}), onEngineDiagnostic: jest.fn(() => () => {}) };
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<App />));
        await act(async () => Simulate.click(button('Routing')));
        await check();
    } finally {
        await act(async () => root.unmount()); container.remove();
        delete window.vasio; delete window.electronAPI;
    }
}

test('process copy receives a default human name and saves a template without runtime identity', async () => {
    await withApp(initialState, [process], async () => {
        await act(async () => Simulate.click(button('Edit copy')));
        expect(document.querySelector('[aria-label="Bridge name"]').value).toBe('ProcessCopy+firefox.exe');
        await act(async () => Simulate.click(button('Save process copy to catalogue')));
        expect(window.vasio.replaceProcessCopyTemplates).toHaveBeenCalledWith([template]);
    });
});

test('selecting a process for a new bridge generates the default name and preserves a custom name', async () => {
    await withApp(initialState, [process], async () => {
        await act(async () => Simulate.click(button('New bridge')));
        await act(async () => Simulate.change([...document.querySelectorAll('input[name="wasapi-bridge-scope"]')][1]));
        const select = document.querySelector('[aria-label="Process to copy"]');
        await act(async () => Simulate.change(select, { target: { value: '42:100' } }));
        expect(document.querySelector('[aria-label="Bridge name"]').value).toBe('ProcessCopy+firefox.exe');
        expect(document.querySelector('[aria-label="Bridge ID"]').value).toBe('app_2');
        expect(document.querySelector('[aria-label="Original Windows outputs"]').textContent).toBe('Output 5/6 (SSL)');
        await act(async () => Simulate.change(document.querySelector('[aria-label="Bridge name"]'), { target: { value: 'Browser' } }));
        await act(async () => Simulate.change(select, { target: { value: '42:100' } }));
        expect(document.querySelector('[aria-label="Bridge name"]').value).toBe('Browser');
    });
});

test('reusing a bridge ID for another process does not overwrite its previous catalogue template', async () => {
    const chrome = { ...bridge, processImagePath: 'C:\\Apps\\chrome.exe' };
    await withApp({ ...initialState, configuredWasapiBridges: [chrome] }, [], async () => {
        await act(async () => Simulate.click(button('Edit copy')));
        await act(async () => Simulate.click(button('Save process copy to catalogue')));
        expect(window.vasio.replaceProcessCopyTemplates).toHaveBeenCalledWith([template, {
            ...template, id: 'app_1_2', processName: 'chrome.exe', name: 'ProcessCopy+chrome.exe'
        }]);
    });
});

test('catalogue rediscovers the current instance identity and its Windows output when applying a template', async () => {
    await withApp(initialState, [{ ...process, processId: 99, creationTime: '200', renderEndpoints: ['Speakers (USB DAC)'] }], async () => {
        await act(async () => Simulate.click(button('Catalogue')));
        await act(async () => Simulate.click(button('Apply template ProcessCopy+firefox.exe')));
        expect(document.querySelector('[aria-label="Process to copy"]').value).toBe('99:200');
        expect(document.querySelector('[aria-label="Original Windows outputs"]').textContent).toBe('Speakers (USB DAC)');
        await act(async () => Simulate.click(button('Save bridge to draft')));
        const graph = JSON.parse(document.querySelector('[data-testid="graph"]').textContent);
        expect(graph.sources.find(item => item.bridgeId === 'app_2').label).toContain('ProcessCopy+firefox.exe');
        expect(graph.sources.find(item => item.bridgeId === 'app_2').availableForConnection).toBe(true);
    });
});

test('several matching processes require an explicit instance selection', async () => {
    await withApp(initialState, [process, { ...process, processId: 99, creationTime: '200' }], async () => {
        await act(async () => Simulate.click(button('Catalogue')));
        await act(async () => Simulate.click(button('Apply template ProcessCopy+firefox.exe')));
        expect(document.querySelector('[aria-label="Process to copy"]').value).toBe('');
        expect(button('Save bridge to draft').disabled).toBe(true);
        expect(document.body.textContent).toContain('Several instances');
    });
});

test('an absent process leaves its template unapplied rather than reviving an old PID', async () => {
    await withApp(initialState, [], async () => {
        await act(async () => Simulate.click(button('Catalogue')));
        await act(async () => Simulate.click(button('Apply template ProcessCopy+firefox.exe')));
        expect(document.querySelector('[aria-label="Process to copy"]').value).toBe('');
        expect(button('Save bridge to draft').disabled).toBe(true);
        expect(document.querySelector('[aria-label="Original Windows outputs"]').textContent).toContain('Unknown');
        expect(document.body.textContent).toContain('No current audio process named firefox.exe');
    });
});

test('refreshing to a reused PID does not show another process instance output on the old bridge', async () => {
    await withApp(initialState, [{ ...process, creationTime: '200', renderEndpoints: ['Other output'] }], async () => {
        await act(async () => Simulate.click(button('Edit copy')));
        await act(async () => Simulate.click(button('Refresh active apps')));
        expect(document.querySelector('[aria-label="Original Windows outputs"]').textContent).toContain('Unknown');
    });
});

test('removing a template leaves its configured bridge intact', async () => {
    await withApp(initialState, [], async () => {
        await act(async () => Simulate.click(button('Catalogue')));
        await act(async () => Simulate.click(button('Remove template ProcessCopy+firefox.exe')));
        expect(window.vasio.replaceProcessCopyTemplates).toHaveBeenCalledWith([]);
        expect(JSON.parse(document.querySelector('[data-testid="graph"]').textContent).sources[0].bridgeId).toBe('app_1');
    });
});

test('unavailable Firefox is absent from new connections but its saved graph remains', async () => {
    const routes = [{ id: 'saved', sourceEndpointId: 'wasapi:app_1:input:1', destinationEndpointId: 'virtual:TimoxVasio:app:daw.exe:input:1', gainDb: 0, mute: false }];
    await withApp({ ...initialState, configuredRoutes: routes }, [process], async () => {
        await act(async () => Simulate.click(button('New connection')));
        expect(JSON.parse(document.querySelector('[data-testid="connection-sources"]').textContent)).toEqual([]);
        const graph = JSON.parse(document.querySelector('[data-testid="graph"]').textContent);
        expect(graph.sources.some(item => item.bridgeId === 'app_1')).toBe(true);
        expect(graph.routes).toEqual(routes);
    });
});

test('a running bridge remains connectable when its process has no active audio session', async () => {
    await withApp({ ...initialState, wasapiBridges: [{ ...initialState.wasapiBridges[0], state: 'running' }] }, [], async () => {
        await act(async () => Simulate.click(button('New connection')));
        expect(JSON.parse(document.querySelector('[data-testid="connection-sources"]').textContent)[0].bridgeId).toBe('app_1');
    });
});

test('a saved bridge without runtime cannot be offered as if it were a new draft', async () => {
    await withApp({ ...initialState, wasapiBridges: [] }, [process], async () => {
        await act(async () => Simulate.click(button('New connection')));
        expect(JSON.parse(document.querySelector('[data-testid="connection-sources"]').textContent)).toEqual([]);
    });
});

test('refresh invalidates old choices immediately and a timeout cannot leave a stale bridge saveable', async () => {
    await withApp(initialState, [process], async () => {
        await act(async () => Simulate.click(button('New bridge')));
        await act(async () => Simulate.change([...document.querySelectorAll('input[name="wasapi-bridge-scope"]')][1]));
        await act(async () => Simulate.change(document.querySelector('[aria-label="Process to copy"]'), { target: { value: '42:100' } }));
        expect(button('Save bridge to draft').disabled).toBe(false);
        let reject;
        window.vasio.getWindowsAudioApplications.mockImplementationOnce(() => new Promise((_resolve, fail) => { reject = fail; }));
        await act(async () => Simulate.click(button('Refresh active apps')));
        expect([...document.querySelector('[aria-label="Process to copy"]').options].some(option => option.value === '42:100')).toBe(false);
        expect(button('Save bridge to draft').disabled).toBe(true);
        await act(async () => reject(new Error('Windows audio inventory request timed out')));
        expect([...document.querySelector('[aria-label="Process to copy"]').options].some(option => option.value === '42:100')).toBe(false);
        expect(button('Save bridge to draft').disabled).toBe(true);
        expect(document.body.textContent).toContain('Windows audio inventory request timed out');
        await act(async () => window.vasio.subscribeEvents.mock.calls[0][0]({ event: 'devices.changed', payload: {
            ...initialState, windowsAudioApplications: [process]
        } }));
        expect([...document.querySelector('[aria-label="Process to copy"]').options].some(option => option.value === '42:100')).toBe(false);
        expect(button('Save bridge to draft').disabled).toBe(true);
    });
});

test('a partial inventory reporting an error never supplies selectable process identities', async () => {
    await withApp(initialState, [process], async () => {
        window.vasio.getWindowsAudioApplications.mockResolvedValueOnce({ windowsAudioApplications: [process],
            windowsAudioApplicationInventoryError: 'Session enumeration failed' });
        await act(async () => Simulate.click(button('New bridge')));
        await act(async () => Simulate.change([...document.querySelectorAll('input[name="wasapi-bridge-scope"]')][1]));
        expect([...document.querySelector('[aria-label="Process to copy"]').options].some(option => option.value === '42:100')).toBe(false);
        expect(button('Save bridge to draft').disabled).toBe(true);
    });
});

test('disconnect invalidates pending and previously selected process identities', async () => {
    await withApp(initialState, [process], async () => {
        await act(async () => Simulate.click(button('New bridge')));
        await act(async () => Simulate.change([...document.querySelectorAll('input[name="wasapi-bridge-scope"]')][1]));
        await act(async () => Simulate.change(document.querySelector('[aria-label="Process to copy"]'), { target: { value: '42:100' } }));
        let resolve;
        window.vasio.getWindowsAudioApplications.mockImplementationOnce(() => new Promise(done => { resolve = done; }));
        await act(async () => Simulate.click(button('Refresh active apps')));
        await act(async () => window.vasio.onDisconnect.mock.calls[0][0]());
        await act(async () => resolve({ windowsAudioApplications: [process], windowsAudioApplicationInventoryError: null }));
        expect([...document.querySelector('[aria-label="Process to copy"]').options].some(option => option.value === '42:100')).toBe(false);
        expect(button('Save bridge to draft').disabled).toBe(true);
    });
});

test('general state polling cannot restore a process inventory invalidated by a failed scan', async () => {
    jest.useFakeTimers();
    try {
        await withApp(initialState, [process], async () => {
            await act(async () => Simulate.click(button('New bridge')));
            await act(async () => Simulate.change([...document.querySelectorAll('input[name="wasapi-bridge-scope"]')][1]));
            await act(async () => Simulate.change(document.querySelector('[aria-label="Process to copy"]'), { target: { value: '42:100' } }));
            window.vasio.getWindowsAudioApplications.mockRejectedValueOnce(new Error('Inventory timed out'));
            await act(async () => Simulate.click(button('Refresh active apps')));
            window.vasio.getState.mockResolvedValue({ ...initialState, windowsAudioApplications: [process] });
            await act(async () => jest.advanceTimersByTime(5000));
            expect([...document.querySelector('[aria-label="Process to copy"]').options].some(option => option.value === '42:100')).toBe(false);
            expect(button('Save bridge to draft').disabled).toBe(true);
        });
    } finally { jest.useRealTimers(); }
});

test('an older overlapping scan cannot replace the latest process inventory', async () => {
    await withApp(initialState, [process], async () => {
        let resolveOlder;
        window.vasio.getWindowsAudioApplications.mockImplementationOnce(() => new Promise(done => { resolveOlder = done; }));
        await act(async () => Simulate.click(button('New bridge')));
        window.vasio.getWindowsAudioApplications.mockResolvedValueOnce({ windowsAudioApplications: [
            { ...process, processId: 99, creationTime: '200' }
        ], windowsAudioApplicationInventoryError: null });
        await act(async () => Simulate.click(button('New bridge')));
        await act(async () => resolveOlder({ windowsAudioApplications: [process], windowsAudioApplicationInventoryError: null }));
        await act(async () => Simulate.change([...document.querySelectorAll('input[name="wasapi-bridge-scope"]')][1]));
        const values = [...document.querySelector('[aria-label="Process to copy"]').options].map(option => option.value);
        expect(values).toContain('99:200');
        expect(values).not.toContain('42:100');
    });
});
