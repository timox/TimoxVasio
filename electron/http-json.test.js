const test = require('node:test');
const assert = require('node:assert/strict');
const { fetchJson } = require('./http-json');
test('HTTP timeout bounds stalled headers and aborts the request', async () => {
    let signal;
    await assert.rejects(fetchJson('http://127.0.0.1/test', {}, (_url, options) => {
        signal = options.signal; return new Promise(() => {});
    }, 20), /timed out/);
    assert.equal(signal.aborted, true);
});
test('HTTP timeout also bounds a stalled JSON body', async () => {
    await assert.rejects(fetchJson('http://127.0.0.1/test', {}, async () => ({ json: () => new Promise(() => {}) }), 20), /timed out/);
});
