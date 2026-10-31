const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { validateRuntimeDependencies } = require('./runtime-dependencies');

const artifactPaths = ['artifacts/backend/TimoxVirtualAsioEngine.exe', 'artifacts/backend/PwarAsioClient.exe'];
const schemaPaths = ['api/openapi-v1.json', 'api/schemas/api-v1.json'];
const sha256 = content => crypto.createHash('sha256').update(content).digest('hex');
function fixture(t, isPackaged = false) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'timox-runtime-pins-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const options = { appRoot: path.join(root, 'app'), resourcesPath: path.join(root, 'resources'), isPackaged };
    fs.mkdirSync(options.appRoot, { recursive: true });
    const files = new Map();
    const content = ['engine', 'pwar', JSON.stringify({ info: { version: '1.1.0' } }),
        JSON.stringify({ definitions: { state: { properties: { apiVersion: { enum: ['1.0', '1.1'] } } } } })];
    const entries = [...artifactPaths, ...schemaPaths].map((name, index) => {
        const file = isPackaged ? path.join(options.resourcesPath, 'backend', index < 2 ? path.basename(name) : name.slice(4))
            : path.join(options.appRoot, name);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, content[index]); files.set(name, file);
        return { path: name, sha256: sha256(content[index]), ...(index < 2 ? { revision: 'test-revision' } : {}) };
    });
    const lock = { formatVersion: 1, requiredApiVersion: '1.1', requiredCapabilities: ['processCopyTemplates'],
        artifacts: entries.slice(0, 2), schemas: entries.slice(2) };
    const save = () => fs.writeFileSync(path.join(options.appRoot, 'artifacts.lock.json'), JSON.stringify(lock));
    save();
    return { options, lock, save, files };
}

for (const isPackaged of [false, true]) {
    test(`validates ${isPackaged ? 'packaged' : 'development'} runtime and returns launch contract`, t => {
        const f = fixture(t, isPackaged);
        assert.deepEqual(validateRuntimeDependencies(f.options), {
            enginePath: f.files.get(artifactPaths[0]), engineSha256: f.lock.artifacts[0].sha256,
            requiredApiVersion: '1.1', requiredCapabilities: ['processCopyTemplates']
        });
    });
    for (const name of [...artifactPaths, ...schemaPaths]) {
        for (const mutation of ['missing', 'tampered']) test(`${isPackaged ? 'packaged' : 'dev'} rejects ${mutation} ${name}`, t => {
            const f = fixture(t, isPackaged);
            if (mutation === 'missing') fs.unlinkSync(f.files.get(name));
            else fs.appendFileSync(f.files.get(name), 'changed');
            assert.throws(() => validateRuntimeDependencies(f.options), { code: 'RUNTIME_DEPENDENCY_INVALID' });
        });
    }
}

const malformed = [
    ['lock version', lock => { lock.formatVersion = 2; }],
    ['API version', lock => { lock.requiredApiVersion = '1.0'; }],
    ['missing capabilities', lock => { delete lock.requiredCapabilities; }],
    ['missing required capability', lock => { lock.requiredCapabilities = []; }],
    ['duplicate capability', lock => { lock.requiredCapabilities.push('processCopyTemplates'); }],
    ['malformed capability', lock => { lock.requiredCapabilities.push('../other'); }],
    ['duplicate artifact', lock => { lock.artifacts[1] = lock.artifacts[0]; }],
    ['extra artifact', lock => { lock.artifacts.push({ path: '../outside', sha256: 'a'.repeat(64) }); }],
    ['traversal artifact', lock => { lock.artifacts[0].path = '../outside'; }],
    ['absolute schema', lock => { lock.schemas[0].path = 'C:/outside.json'; }],
    ['missing revision', lock => { delete lock.artifacts[0].revision; }],
    ['malformed hash', lock => { lock.artifacts[0].sha256 = 'no'; }]
];
for (const [name, mutate] of malformed) test(`rejects ${name}`, t => {
    const f = fixture(t); mutate(f.lock); f.save();
    assert.throws(() => validateRuntimeDependencies(f.options), { code: 'RUNTIME_DEPENDENCY_INVALID' });
});
for (const variant of ['missing', 'invalid JSON', 'oversized']) test(`rejects ${variant} lock`, t => {
    const f = fixture(t), file = path.join(f.options.appRoot, 'artifacts.lock.json');
    if (variant === 'missing') fs.unlinkSync(file);
    else fs.writeFileSync(file, variant === 'invalid JSON' ? '{' : ' '.repeat(65537));
    assert.throws(() => validateRuntimeDependencies(f.options), { code: 'RUNTIME_DEPENDENCY_INVALID' });
});
for (const index of [0, 1]) test(`rejects pinned schema ${index} with incompatible API version`, t => {
    const f = fixture(t);
    const content = JSON.stringify(index === 0 ? { info: { version: '2.0.0' } }
        : { definitions: { state: { properties: { apiVersion: { enum: ['2.0'] } } } } });
    fs.writeFileSync(f.files.get(schemaPaths[index]), content);
    f.lock.schemas[index].sha256 = sha256(content); f.save();
    assert.throws(() => validateRuntimeDependencies(f.options), { code: 'RUNTIME_DEPENDENCY_INVALID' });
});
test('does not fall back to development artifacts when packaged dependencies are missing', t => {
    const f = fixture(t);
    assert.throws(() => validateRuntimeDependencies({ ...f.options, isPackaged: true }), { code: 'RUNTIME_DEPENDENCY_INVALID' });
});
for (const [name, size] of [[artifactPaths[0], 64 * 1024 * 1024 + 1], [schemaPaths[0], 4 * 1024 * 1024 + 1], [artifactPaths[1], 0]]) {
    test(`rejects invalid dependency size ${size} for ${name}`, t => {
        const f = fixture(t);
        fs.truncateSync(f.files.get(name), size);
        assert.throws(() => validateRuntimeDependencies(f.options), error =>
            error.code === 'RUNTIME_DEPENDENCY_INVALID' && /file size/.test(error.message));
    });
}
