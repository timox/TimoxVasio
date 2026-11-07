const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { checkSource, checkEngine, checkPwar, writeManifest } = require('./release-metadata.cjs');
const { execFileSync } = require('node:child_process');
test('release packaging rejects incompatible engine/PWAR versions, pins, receipts and binaries', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'timox-release-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const lock = structuredClone(require('../artifacts.lock.json'));
    const backend = path.join(root, 'artifacts/backend');
    fs.mkdirSync(backend, { recursive: true });
    fs.mkdirSync(path.join(root, 'artifacts/driver'), { recursive: true });
    fs.copyFileSync(path.join(__dirname, '..', 'artifacts/driver/TimoxVasio.dll'), path.join(root, 'artifacts/driver/TimoxVasio.dll'));
    fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({ version: lock.release.guiVersion }));
    for (const item of lock.schemas) {
        fs.mkdirSync(path.dirname(path.join(root, item.path)), { recursive: true });
        fs.copyFileSync(path.join(__dirname, '..', item.path), path.join(root, item.path));
    }
    fs.writeFileSync(path.join(backend, 'TimoxVirtualAsioEngine.exe'), 'fixture engine');
    const engine = lock.artifacts.find(item => item.repository === 'TimoxVAudioEngine');
    engine.sha256 = crypto.createHash('sha256').update('fixture engine').digest('hex');
    const pwar = lock.artifacts.find(item => item.repository === 'Timox-Pwar');
    fs.writeFileSync(path.join(backend, 'PwarAsioClient.exe'), 'fixture pwar');
    pwar.revision = lock.release.pwar.revision;
    pwar.sha256 = crypto.createHash('sha256').update('fixture pwar').digest('hex');
    const pwarReceipt = { formatVersion: 1, repository: 'timox/Timox-Pwar', version: lock.release.pwar.version,
        controlProtocolVersion: 1, revision: pwar.revision, dirty: false, platform: 'windows-x64',
        binaryName: 'PwarAsioClient.exe', binarySha256: pwar.sha256 };
    const writePwar = () => fs.writeFileSync(path.join(backend, 'pwar-build.json'), JSON.stringify(pwarReceipt));
    writePwar();
    const receipt = { formatVersion: 1, repository: 'timox/TimoxVAudioEngine',
        version: lock.release.engine.version, apiVersion: '1.1', revision: engine.revision,
        pwarClientVersion: lock.release.pwar.version, pwarClientRevision: pwar.revision, pwarClientSha256: pwar.sha256,
        dirty: false, windowsMidiServices: true, binarySha256: engine.sha256 };
    const write = () => fs.writeFileSync(path.join(backend, 'engine-build.json'), JSON.stringify(receipt));
    write();
    checkEngine(root, lock);
    checkPwar(root, lock);
    const git = (...args) => execFileSync('git', ['-C', root, ...args], {encoding:'utf8'}).trim();
    fs.writeFileSync(path.join(root, '.gitignore'), '/build/\n');
    git('init', '-q'); git('add', '.');
    git('-c', 'user.name=Release test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture');
    assert.equal(writeManifest(root, lock).gui.revision, git('rev-parse', 'HEAD'));
    fs.writeFileSync(path.join(root, 'uncommitted.txt'), 'dirty');
    assert.throws(() => writeManifest(root, lock), /Commit GUI changes/);
    fs.unlinkSync(path.join(root, 'uncommitted.txt'));
    for (const [key, wrong] of Object.entries({version:'1.2.7', revision:'0'.repeat(40), dirty:true,
        windowsMidiServices:false, binarySha256:'f'.repeat(64), apiVersion:'1.0', pwarClientSha256:'0'.repeat(64)})) {
        const previous = receipt[key]; receipt[key] = wrong; write();
        assert.throws(() => checkEngine(root, lock), /Engine build does not match/);
        receipt[key] = previous;
    }
    write();
    for (const [key, wrong] of Object.entries({version:'0.0.0', controlProtocolVersion:2,
        revision:'0'.repeat(40), dirty:true, binarySha256:'0'.repeat(64)})) {
        const previous = pwarReceipt[key]; pwarReceipt[key] = wrong; writePwar();
        assert.throws(() => checkPwar(root, lock), /PWAR build does not match/);
        pwarReceipt[key] = previous;
    }
    writePwar();
    fs.writeFileSync(path.join(backend, 'PwarAsioClient.exe'), 'substitution');
    assert.throws(() => checkPwar(root, lock), /PWAR build does not match/);
    fs.writeFileSync(path.join(backend, 'TimoxVirtualAsioEngine.exe'), 'substitution');
    assert.throws(() => checkEngine(root, lock), /Engine build does not match/);
    fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({version:'0.1.0'}));
    assert.throws(() => checkSource(root, lock), /candidate version/);
});
