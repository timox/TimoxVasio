import { defaultProcessCopyName, processCopyTemplate, processCopyCanConnect } from './process-copy';

const bridge = { id: 'app_1', kind: 'processLoopback', enabled: true, processId: 42,
    processCreationTime: '123456789012345678', processImagePath: 'C:\\Apps\\Firefox.exe', bufferDuration100ns: 0 };

test('default name and catalogue retain the executable, never the runtime identity', () => {
    expect(defaultProcessCopyName(bridge.processImagePath)).toBe('ProcessCopy+firefox.exe');
    expect(processCopyTemplate(bridge)).toEqual({ id: 'app_1', name: 'ProcessCopy+firefox.exe',
        processName: 'firefox.exe', bufferDuration100ns: 0 });
    expect(processCopyTemplate({ ...bridge, name: 'Browser audio' }).name).toBe('Browser audio');
});

test('an unavailable process bridge cannot be offered for a new connection', () => {
    for (const state of ['unavailable', 'failed', 'stopped', 'retrying', 'disabled'])
        expect(processCopyCanConnect(bridge, { state })).toBe(false);
    expect(processCopyCanConnect(bridge, { state: 'running', peakDbfs: null })).toBe(true);
    expect(processCopyCanConnect(bridge, { state: 'priming' })).toBe(true);
    expect(processCopyCanConnect(bridge, undefined)).toBe(false);
    expect(processCopyCanConnect(bridge, undefined, true)).toBe(true);
});
