const test = require('node:test');
const assert = require('node:assert/strict');
const { sameProcess, prepareEngine, terminateVerified, inspectProcess } = require('./engine-ownership');
const current = { pid: 41, path: 'C:\\Timox\\engine.exe', createdFileTime: '134000001234567890', sha256: 'a'.repeat(64) };
const old = { ...current, sha256: 'b'.repeat(64) };
const state = process => ({ process, mutexPresent: !!process });
function fixture(sequence, options = {}) {
    const calls = []; let index = 0;
    return { calls, args: { engineSha256: current.sha256, receipt: old,
        inspect: async () => sequence[Math.min(index++, sequence.length - 1)],
        healthy: async () => { calls.push('healthy'); return 52525; },
        stopAudio: async identity => { calls.push(['stop', identity]); },
        terminate: async identity => { calls.push(['terminate', identity]); }, wait: async () => {}, ...options } };
}
test('healthy pinned engine is reused without stopping audio', async () => {
    const f = fixture([state(current)]);
    assert.deepEqual(await prepareEngine(f.args), { port: 52525, identity: current });
    assert.deepEqual(f.calls, ['healthy']);
});
test('absent listener and mutex permit launch', async () => {
    const f = fixture([state(null)]); assert.equal(await prepareEngine(f.args), null); assert.deepEqual(f.calls, []);
});
test('known obsolete engine stops audio then terminates only the attested identity', async () => {
    const f = fixture([state(old), state(old), { process: null, mutexPresent: true }, state(null)]);
    assert.equal(await prepareEngine(f.args), null);
    assert.deepEqual(f.calls, [['stop', old], ['terminate', old]]);
});
test('unresponsive pinned engine is replaced after identity verification', async () => {
    const f = fixture([state(current), state(current), state(null)], { healthy: async () => { throw new Error('timeout'); } });
    assert.equal(await prepareEngine(f.args), null); assert.deepEqual(f.calls, [['stop', current], ['terminate', current]]);
});
test('same binary with missing required API contract fails without stopping', async () => {
    const f = fixture([state(current)], { healthy: async () => { throw Object.assign(new Error('missing capability'), { code: 'API_CONTRACT_MISMATCH' }); } });
    await assert.rejects(prepareEngine(f.args), /missing capability/); assert.deepEqual(f.calls, []);
});
for (const changed of [{ ...old, pid: 42 }, { ...old, createdFileTime: '134000001234567891' },
    { ...old, path: 'C:\\foreign.exe' }, { ...old, sha256: 'c'.repeat(64) }]) {
    test('foreign or reused PID is refused before any stop', async () => {
        const f = fixture([state(changed)]); await assert.rejects(prepareEngine(f.args), /unverified/); assert.deepEqual(f.calls, []);
    });
}
test('identity race before replacement refuses every mutation', async () => {
    const f = fixture([state(old), state({ ...old, createdFileTime: '134000001234567899' })]);
    await assert.rejects(prepareEngine(f.args), /identity changed/); assert.deepEqual(f.calls, []);
});
test('another process taking the port prevents launch', async () => {
    const f = fixture([state(old), state(old), state(current)]);
    await assert.rejects(prepareEngine(f.args), /Another process/);
});
test('missing listener with held engine mutex never permits a second process', async () => {
    const f = fixture([{ process: null, mutexPresent: true }]);
    await assert.rejects(prepareEngine(f.args), /audio lock/); assert.deepEqual(f.calls, []);
});
test('port and mutex release timeout remains fail closed', async () => {
    const f = fixture([state(old)]); await assert.rejects(prepareEngine(f.args), /did not release/);
});
test('verified termination uses held handle, PID, creation time, path and hash before Kill without tree kill', async () => {
    await terminateVerified(old, async (_exe, args, options) => {
        const script = Buffer.from(args.at(-1), 'base64').toString('utf16le');
        assert.match(script, /\$heldHandle = \$process.Handle/);
        for (const key of ['pid', 'path', 'createdFileTime', 'sha256']) assert.match(script, new RegExp(`\\$actual\\.${key}`));
        assert.match(script, /\$process.Kill\(\)/); assert.doesNotMatch(script, /taskkill|Stop-Process|Kill\(\$true/);
        assert.ok(script.indexOf('Engine identity changed') < script.indexOf('$process.Kill()'));
        assert.equal(options.windowsHide, true); assert.equal(options.timeout, 10000);
        return { stdout: '{"terminated":true}' };
    });
});
test('invalid termination identity never invokes a subprocess', async () => {
    await assert.rejects(terminateVerified({ ...old, createdFileTime: '' }, () => { throw new Error('must not execute'); }), /Invalid engine identity/);
});
test('read-only process inspection validates the PID and parses the subprocess identity', async () => {
    assert.deepEqual(await inspectProcess(41, async () => ({ stdout: JSON.stringify(current) })), current);
    await assert.rejects(inspectProcess('41;evil'), /Invalid process ID/);
    assert.equal(sameProcess(old, { ...old, path: old.path.toUpperCase() }), true);
});
