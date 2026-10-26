const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const { EventEmitter } = require('node:events');

async function launch(ownsLock) {
    const calls = { fetch: 0, spawn: 0, windows: 0, quit: 0, focus: 0, restore: 0 };
    const app = Object.assign(new EventEmitter(), {
        isPackaged: true,
        requestSingleInstanceLock: () => ownsLock,
        whenReady: () => Promise.resolve(),
        quit: () => { calls.quit++; }
    });
    class Window extends EventEmitter {
        constructor() { super(); calls.windows++; this.webContents = { send() {} }; }
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
            if (name === './engine-process') return { launchEngine() { calls.spawn++; throw new Error('Unexpected spawn'); } };
            if (name === './engine-discovery') return { discoverEngine: async () => { calls.fetch++; return 52525; } };
            return require(name);
        },
        __dirname, process, console: { log() {}, error() {} }
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
    assert.equal(calls.windows, 0);
});
test('first GUI reuses engine and second launch restores its existing window', async () => {
    const { calls, app } = await launch(true);
    assert.equal(calls.fetch, 1);
    assert.equal(calls.windows, 1);
    app.emit('second-instance');
    assert.equal(calls.restore, 1);
    assert.equal(calls.focus, 1);
    assert.equal(calls.windows, 1);
    assert.equal(calls.spawn, 0);
});
