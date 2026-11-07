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

test('TimoxVSSC explicitly requires API 1.1, independently of its product version', async () => {
    const response = apiVersion => async () => ({ok:true, json:async () => ({apiVersion, virtualDrivers:[{id:'TimoxVasio'}]})});
    await assert.rejects(discoverEngine(response('1.0'), {requiredApiVersion:'1.1'}), /TimoxVSSC requires engine API 1.1/);
    assert.equal(await discoverEngine(response('1.1'), {requiredApiVersion:'1.1'}), 52525);
});

test('rejects a stale engine version or revision even when API 1.1 is compatible', async () => {
    const expected = { requiredApiVersion: '1.1', requiredEngineVersion: '1.3.0-alpha.2', requiredEngineRevision: 'a'.repeat(40) };
    const response = engine => async () => ({ ok: true, json: async () => ({ apiVersion: '1.1',
        virtualDrivers: [{ id: 'TimoxVasio' }], engine }) });
    await assert.rejects(discoverEngine(response({ version: '1.2.7', revision: 'b'.repeat(40) }), expected), /requires engine 1.3.0-alpha.2/);
    await assert.rejects(discoverEngine(response({ version: '1.3.0-alpha.2', revision: 'b'.repeat(40) }), expected), /requires engine 1.3.0-alpha.2/);
    assert.equal(await discoverEngine(response({ version: '1.3.0-alpha.2', revision: 'a'.repeat(40) }), expected), 52525);
});
