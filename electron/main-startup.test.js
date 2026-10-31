const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const { EventEmitter } = require('node:events');

async function launch(ownsLock, dependencyError = null, lifecycle = null) {
    const calls = { fetch: 0, spawn: 0, windows: 0, quit: 0, focus: 0, restore: 0, paths: {}, name: null, verification: [], notices: [] };
    const app = Object.assign(new EventEmitter(), {
        isPackaged: true,
        getPath: () => '/app-data',
        setPath: (key, value) => { calls.paths[key] = value; },
        setName: value => { calls.name = value; },
        requestSingleInstanceLock: () => ownsLock,
        whenReady: () => Promise.resolve(),
        quit: () => { calls.quit++; }
    });
    class Window extends EventEmitter {
        constructor() { super(); calls.windows++; this.webContents = { send(channel, payload) { calls.notices.push({channel, payload}); } }; }
        loadURL() {}
        isDestroyed() { return false; }
        isMinimized() { return true; }
        restore() { calls.restore++; }
        focus() { calls.focus++; }
    }
    vm.runInNewContext(fs.readFileSync(require.resolve('./main'), 'utf8'), {
        require: name => {
            if (name === 'electron') return { app, BrowserWindow: Window,
                ipcMain: { handle() {} }, Menu: { buildFromTemplate: x => x, setApplicationMenu() {} } };
            if (name === 'fs' && lifecycle) return { ...fs, existsSync: () => true };
            if (name === './engine-process') return { launchEngine() { calls.spawn++; if (lifecycle) return lifecycle.child; throw new Error('Unexpected spawn'); } };
            if (name === './runtime-dependencies') return { validateRuntimeDependencies() {
                calls.verification.push('dependencies');
                if (dependencyError) throw dependencyError;
                return { enginePath: '/engine.exe', engineSha256: 'a'.repeat(64), requiredApiVersion: '1.1', requiredCapabilities: ['processCopyTemplates'] };
            } };
            if (name === './engine-ownership') return {
                readReceipt() { return null; }, writeReceipt() { calls.verification.push('receipt'); if (lifecycle?.receiptError) throw lifecycle.receiptError; },
                inspectProcess: () => lifecycle.inspection,
                inspectEngine: async () => ({ process: lifecycle.identity }),
                sameProcess: (left, right) => left === right,
                terminateVerified: async identity => { lifecycle.terminated.push(identity); },
                async prepareEngine(options) { calls.verification.push('ownership'); return lifecycle ? null : { port: await options.healthy(), identity: {} }; }
            };
            if (name === './engine-discovery') return { discoverEngine: async (_fetch, options) => { calls.fetch++; assert.equal(options.requiredApiVersion, '1.1'); return 52525; } };
            return require(name);
        },
        __dirname, process, fetch: () => {}, console: { log() {}, error() {} },
        setTimeout: handler => { calls.startupTimer = handler; return 1; }, clearTimeout() {}
    });
    await new Promise(resolve => setImmediate(resolve));
    return { calls, app };
}
test('second GUI quits before discovery, engine launch or window creation', async () => {
    const { calls, app } = await launch(false);
    app.emit('activate');
    assert.equal(calls.quit, 1);
    assert.equal(calls.fetch, 0);
    assert.equal(calls.spawn, 0);
    assert.deepEqual(calls.verification, []);
    assert.equal(calls.windows, 0);
});

function pendingLaunch() {
    const child = Object.assign(new EventEmitter(), { pid: 123, stdout: new EventEmitter(), stderr: new EventEmitter() });
    let resolveInspection;
    return { child, identity: { pid: 123, path: '/engine.exe', createdFileTime: '1234', sha256: 'a'.repeat(64) },
        inspection: new Promise(resolve => { resolveInspection = resolve; }), resolveInspection: value => resolveInspection(value), terminated: [] };
}
const turn = () => new Promise(resolve => setImmediate(resolve));
test('late spawn inspection must not terminate an engine whose API readiness already succeeded', async () => {
    const lifecycle = pendingLaunch();
    const { calls } = await launch(true, null, lifecycle);
    lifecycle.child.emit('spawn');
    lifecycle.child.stdout.emit('data', Buffer.from('{"event":"api.ready","port":52525}\n'));
    await turn();
    assert.equal(calls.notices.some(notice => notice.payload.connected === true), true);
    lifecycle.resolveInspection(lifecycle.identity);
    await turn();
    assert.deepEqual(lifecycle.terminated, []);
});
test('spawn verification failure is terminal and cleans up only its verified child', async () => {
    const lifecycle = pendingLaunch(); lifecycle.receiptError = new Error('Cannot persist ownership');
    const { calls } = await launch(true, null, lifecycle);
    lifecycle.child.emit('spawn'); lifecycle.resolveInspection(lifecycle.identity);
    await turn();
    lifecycle.child.stdout.emit('data', Buffer.from('{"event":"api.ready","port":52525}\n'));
    await turn();
    assert.equal(calls.notices.some(notice => notice.payload.connected === true), false);
    assert.equal(calls.fetch, 0, 'failed startup must not continue API confirmation');
    assert.deepEqual(lifecycle.terminated, [lifecycle.identity]);
});
test('late identity verification after a startup timeout cleans up without reporting ready', async () => {
    const lifecycle = pendingLaunch(); const { calls } = await launch(true, null, lifecycle);
    lifecycle.child.emit('spawn'); calls.startupTimer();
    lifecycle.resolveInspection(lifecycle.identity); await turn();
    lifecycle.child.stdout.emit('data', Buffer.from('{"event":"api.ready","port":52525}\n')); await turn();
    assert.equal(calls.notices.some(notice => notice.payload.connected === true), false);
    assert.deepEqual(lifecycle.terminated, [lifecycle.identity]);
});
test('first GUI reuses engine and second launch restores its existing window', async () => {
    const { calls, app } = await launch(true);
    assert.equal(calls.fetch, 1);
    assert.equal(calls.name, 'TimoxVSSC');
    assert.equal(calls.paths.userData, require('path').join('/app-data', 'timox-vsounddealer'));
    assert.equal(calls.windows, 1);
    app.emit('second-instance');
    assert.equal(calls.restore, 1);
    assert.equal(calls.focus, 1);
    assert.equal(calls.windows, 1);
    assert.equal(calls.spawn, 0);
    assert.deepEqual(calls.verification, ['dependencies', 'ownership', 'receipt']);
});

test('invalid runtime dependencies leave an actionable window without inspecting or launching an engine', async () => {
    const { calls } = await launch(true, new Error('PWAR dependency hash mismatch'));
    assert.deepEqual(calls.verification, ['dependencies']);
    assert.equal(calls.fetch, 0);
    assert.equal(calls.spawn, 0);
    assert.equal(calls.windows, 1);
    assert.equal(calls.notices.at(-1).payload.error, 'PWAR dependency hash mismatch');
});
