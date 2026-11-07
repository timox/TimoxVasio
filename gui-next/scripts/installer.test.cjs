const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

test('preview installer bundles and conditionally removes only its ASIO registration', () => {
    const root = path.join(__dirname, '..');
    const lock = JSON.parse(fs.readFileSync(path.join(root, 'artifacts.lock.json'), 'utf8'));
    const driver = lock.artifacts.find(item => item.path === 'artifacts/driver/TimoxVasio.dll');
    const installer = fs.readFileSync(path.join(root, 'assets/installer.nsh'), 'utf8');
    const stopScript = fs.readFileSync(path.join(root, 'assets/stop-preview-processes.ps1'), 'utf8');
    const hash = crypto.createHash('sha256').update(fs.readFileSync(path.join(root, driver.path))).digest('hex');

    assert.equal(driver.sha256, hash);
    assert.match(installer, /artifacts\\driver\\TimoxVasio\.dll/);
    assert.match(installer, /VirtualASIO\\1\.2\.7-test\\TimoxVasio\.dll/);
    assert.match(installer, /\$R0 == "\$PROGRAMFILES64\\Steinberg\\VirtualASIO\\1\.2\.7-test\\TimoxVasio\.dll"/);
    assert.match(installer, /stop-preview-processes\.ps1/);
    assert.match(stopScript, /StartsWith\(\$root/);
    assert.match(stopScript, /Modules\.FileName -contains \$DriverPath/);
    assert.match(stopScript, /\$StopAllTimox/);
    assert.match(installer, /-StopAllTimox/);
    assert.match(stopScript, /TimoxVirtualAsioEngine\.exe/);
    assert.match(stopScript, /PwarAsioClient\.exe/);
    assert.match(installer, /SetShellVarContext all/);
    assert.match(installer, /Delete "\$SMPROGRAMS\\Timox VASIO Control\.lnk"/);
});
