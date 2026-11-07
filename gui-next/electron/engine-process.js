const { spawn } = require('node:child_process');

function launchEngine(command, args, cwd) {
    return spawn(command, args, {
        cwd,
        detached: true,
        windowsHide: true,
        stdio: ['ignore', 'pipe', 'pipe']
    });
}

function releaseEngine(child) {
    child.stdout?.destroy();
    child.stderr?.destroy();
    child.unref();
}

module.exports = { launchEngine, releaseEngine };
