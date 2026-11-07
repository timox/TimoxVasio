const test = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');

test('engine process keeps running when its launcher exits', async () => {
    const helper = path.join(__dirname, 'engine-process');
    const parentScript = `
        const { launchEngine, releaseEngine } = require(${JSON.stringify(helper)});
        const child = launchEngine(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], process.cwd());
        child.once('spawn', () => {
            process.stdout.write(String(child.pid) + '\\n');
            releaseEngine(child);
        });
        child.once('error', error => { console.error(error); process.exitCode = 1; });
    `;
    const parent = spawn(process.execPath, ['-e', parentScript], {
        cwd: __dirname, stdio: ['ignore', 'pipe', 'pipe']
    });
    let stdout = '';
    let stderr = '';
    parent.stdout.on('data', data => { stdout += data; });
    parent.stderr.on('data', data => { stderr += data; });
    const exitCode = await new Promise(resolve => parent.once('close', resolve));
    assert.equal(exitCode, 0, stderr);
    const pid = Number(stdout.trim());
    assert.ok(Number.isInteger(pid) && pid > 0, `Missing child PID: ${stdout}`);
    try {
        assert.doesNotThrow(() => process.kill(pid, 0), 'engine exited with its launcher');
    } finally {
        process.kill(pid);
    }
});
