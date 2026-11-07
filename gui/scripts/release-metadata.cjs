const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function checkSource(root, lock) {
    const version = JSON.parse(fs.readFileSync(path.join(root, 'package.json'))).version;
    const release = lock.release;
    if (!release || release.channel !== 'alpha' || release.productVersion !== '1.3.0' ||
        release.status !== '1.3.0-preview-not-released' || release.guiVersion !== version)
        throw new Error('Release metadata must match the GUI candidate version/channel');
    if (lock.requiredApiVersion !== '1.1' || release.engine.apiVersion !== lock.requiredApiVersion)
        throw new Error('Release API version mismatch');
    if (release.driver.version !== '1.2.7-test' || release.driver.bundled !== true)
        throw new Error('The preview candidate requires the bundled 1.2.7-test driver');
    const driver = lock.artifacts.find(entry => entry.path === 'artifacts/driver/TimoxVasio.dll');
    if (!driver || driver.sha256 !== release.driver.sha256 || hash(path.join(root, driver.path)) !== driver.sha256)
        throw new Error('Preview driver is missing or does not match its pinned SHA-256');
    for (const component of [release.driver, release.engine, release.pwar])
        if (!/^[a-f0-9]{40}$/.test(component.revision || '')) throw new Error('Missing exact component revision');
    if (release.engine.windowsMidiServices !== true)
        throw new Error('The MIDI candidate requires Windows MIDI Services enabled');
    if (!/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(release.pwar.version || '') || release.pwar.controlProtocolVersion !== 1)
        throw new Error('PWAR requires an independent product version and control protocol v1');
    for (const item of lock.schemas || [])
        if (hash(path.join(root, item.path)) !== item.sha256) throw new Error(`Schema hash mismatch: ${item.path}`);
    return release;
}
function checkEngine(root, lock) {
    const release = checkSource(root, lock);
    const item = lock.artifacts.find(entry => entry.component === 'engine');
    const receipt = JSON.parse(fs.readFileSync(path.join(root, 'artifacts/backend/engine-build.json')));
    const pwar = lock.artifacts.find(entry => entry.component === 'pwar');
    if (receipt.formatVersion !== 1 || receipt.repository !== 'timox/TimoxVasio' || receipt.component !== 'engine' ||
        receipt.version !== release.engine.version || receipt.apiVersion !== release.engine.apiVersion ||
        receipt.revision !== release.engine.revision || receipt.dirty !== false ||
        receipt.windowsMidiServices !== true || item.revision !== receipt.revision ||
        receipt.pwarClientVersion !== release.pwar.version || receipt.pwarClientRevision !== release.pwar.revision ||
        receipt.pwarClientSha256 !== pwar.sha256 ||
        item.sha256 !== receipt.binarySha256 || hash(path.join(root, item.path)) !== receipt.binarySha256)
        throw new Error('Engine build does not match the clean, MIDI-enabled release candidate');
    return receipt;
}
function checkPwar(root, lock) {
    const release = checkSource(root, lock);
    const item = lock.artifacts.find(entry => entry.component === 'pwar');
    const receipt = JSON.parse(fs.readFileSync(path.join(root, 'artifacts/backend/pwar-build.json')));
    if (receipt.formatVersion !== 1 || receipt.repository !== 'timox/TimoxVasio' || receipt.component !== 'pwar' ||
        receipt.version !== release.pwar.version || receipt.controlProtocolVersion !== 1 ||
        receipt.revision !== release.pwar.revision || receipt.dirty !== false ||
        receipt.platform !== 'windows-x64' || receipt.binaryName !== 'PwarAsioClient.exe' ||
        item.revision !== receipt.revision || item.sha256 !== receipt.binarySha256 ||
        hash(path.join(root, item.path)) !== receipt.binarySha256)
        throw new Error('PWAR build does not match the clean, versioned release candidate');
    return receipt;
}
function assertPackageSourcesCommitted(root) {
    const git = (...args) => execFileSync('git', ['-C', root, ...args], { encoding: 'utf8' }).trim();
    const repository = git('rev-parse', '--show-toplevel');
    const prefix = path.relative(repository, root).split(path.sep).filter(Boolean).join('/');
    const generated = [
        'artifacts.lock.json',
        'api/openapi-v1.json',
        'api/schemas/api-v1.json',
        'artifacts/driver/TimoxVasio.dll',
        'artifacts/backend/TimoxVirtualAsioEngine.exe',
        'artifacts/backend/PwarAsioClient.exe',
        'artifacts/backend/engine-build.json',
        'artifacts/backend/pwar-build.json'
    ].map(file => [prefix, file].filter(Boolean).join('/'));
    const changed = [...git('diff', '--name-only', 'HEAD').split(/\r?\n/),
        ...git('ls-files', '--others', '--exclude-standard').split(/\r?\n/)].filter(Boolean);
    const pending = [...new Set(changed)].filter(file => !generated.includes(file));
    if (pending.length) throw new Error(`Commit GUI changes before packaging: ${pending.join(', ')}`);
}
function writeManifest(root, lock) {
    checkEngine(root, lock);
    checkPwar(root, lock);
    const git = (...args) => execFileSync('git', ['-C', root, ...args], { encoding: 'utf8' }).trim();
    assertPackageSourcesCommitted(root);
    const manifest = { formatVersion: 1, ...lock.release,
        gui: { repository: 'timox/TimoxVasio', version: lock.release.guiVersion, revision: git('rev-parse', 'HEAD') },
        artifacts: lock.artifacts, schemas: lock.schemas,
        engineBuild: JSON.parse(fs.readFileSync(path.join(root, 'artifacts/backend/engine-build.json'))),
        pwarBuild: JSON.parse(fs.readFileSync(path.join(root, 'artifacts/backend/pwar-build.json'))) };
    fs.mkdirSync(path.join(root, 'build'), { recursive: true });
    fs.writeFileSync(path.join(root, 'build/release-manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
    return manifest;
}
module.exports = { checkSource, checkEngine, checkPwar, writeManifest };
if (require.main === module) {
    const root = path.resolve(__dirname, '..');
    checkSource(root, JSON.parse(fs.readFileSync(path.join(root, 'artifacts.lock.json'))));
    console.log('Candidate versions, component revisions and API schema pins match. Binary/hardware readiness not implied.');
}
