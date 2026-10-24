const test = require('node:test');
const assert = require('node:assert/strict');
const { discoverEngine } = require('./engine-discovery');

for (const apiVersion of ['1.0', '1.1']) {
    test(`reuse running API ${apiVersion}`, async () => {
        assert.equal(await discoverEngine(async () => ({ ok: true, json: async () => ({
            apiVersion, virtualDrivers: [{ id: 'TimoxVasio' }]
        }) })), 52525);
    });
}
test('only connection refused permits a new engine', async () => {
    assert.equal(await discoverEngine(async () => {
        throw Object.assign(new TypeError('fetch failed'), { cause: { code: 'ECONNREFUSED' } });
    }), null);
});
for (const code of ['ETIMEDOUT', 'ECONNRESET', 'EACCES']) {
    test(`${code} must not permit a competing engine`, async () => {
        await assert.rejects(discoverEngine(async () => {
            throw Object.assign(new Error('fetch failed'), { cause: { code } });
        }), /second engine/);
    });
}
for (const response of [
    { ok: false, status: 503 },
    { ok: true, json: async () => { throw new SyntaxError('bad JSON'); } },
    { ok: true, json: async () => ({ apiVersion: '2.0', virtualDrivers: [{ id: 'TimoxVasio' }] }) },
    { ok: true, json: async () => ({ apiVersion: '1.1', virtualDrivers: [] }) },
    { ok: true, json: async () => null }
]) {
    test('occupied or unrecognized API must not permit a competing engine', async () => {
        await assert.rejects(discoverEngine(async () => response), /second engine/);
    });
}
