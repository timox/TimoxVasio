const fs = require('node:fs');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execute = promisify(execFile);

function sameProcess(left, right) {
    return !!left && !!right && Number.isInteger(left.pid) && left.pid > 0 && left.pid === right.pid &&
        typeof left.path === 'string' && typeof right.path === 'string' && left.path.toLowerCase() === right.path.toLowerCase() &&
        typeof left.createdFileTime === 'string' && /^\d+$/.test(left.createdFileTime) &&
        left.createdFileTime === right.createdFileTime && /^[a-f0-9]{64}$/.test(left.sha256 || '') && left.sha256 === right.sha256;
}

async function powershell(script, run = execute) {
    const encoded = Buffer.from("$ErrorActionPreference='Stop'; $env:PSModulePath=$PSHOME+'\\Modules'; $ProgressPreference='SilentlyContinue'; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);\n" + script, 'utf16le').toString('base64');
    const executable = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32/WindowsPowerShell/v1.0/powershell.exe');
    try {
        const { stdout } = await run(executable, ['-NoProfile', '-NonInteractive', '-EncodedCommand', encoded],
            { windowsHide: true, timeout: 10000, maxBuffer: 1024 * 1024 });
        return JSON.parse(stdout.replace(/^\uFEFF/, '').trim());
    } catch (cause) {
        throw new Error(cause.killed ? 'Windows engine verification timed out. No competing engine was started.' :
            'Windows could not verify or release the engine process. Check access permissions before retrying.', { cause });
    }
}

const snapshotScript = `
function Read-Identity($process) {
    $heldHandle = $process.Handle
    if ($process.HasExited) { return $null }
    $image = $process.MainModule.FileName
    $stream = [IO.File]::OpenRead($image)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
    [pscustomobject]@{pid=$process.Id;path=$image;createdFileTime=$process.StartTime.ToFileTimeUtc().ToString();sha256=$hash}
}
`;

async function inspectEngine(run) {
    return powershell(snapshotScript + String.raw`
$listeners = @(& ($env:SystemRoot+'\System32\netstat.exe') -ano -p tcp | ForEach-Object {
    if ($_ -match '^\s*TCP\s+\S+:52525\s+\S+\s+LISTENING\s+(\d+)\s*$') { [int]$matches[1] }
} | Select-Object -Unique)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect local TCP listeners.' }
if ($listeners.Count -gt 1) { throw 'Multiple processes own the engine port; cannot establish ownership.' }
$identity = $null
if ($listeners.Count -eq 1) {
    $process = [System.Diagnostics.Process]::GetProcessById($listeners[0])
    try { $identity = Read-Identity $process } finally { $process.Dispose() }
}
$mutexPresent = $false
try { $mutex = [System.Threading.Mutex]::OpenExisting('Global\TimoxVasio.Engine.v1'); $mutexPresent = $true; $mutex.Dispose() }
catch [System.Threading.WaitHandleCannotBeOpenedException] { }
@{process=$identity;mutexPresent=$mutexPresent} | ConvertTo-Json -Compress
`, run);
}

async function inspectProcess(pid, run) {
    if (!Number.isInteger(pid) || pid < 1) throw new Error('Invalid process ID.');
    return powershell(snapshotScript + `
$process = [System.Diagnostics.Process]::GetProcessById(${pid})
try { Read-Identity $process | ConvertTo-Json -Compress } finally { $process.Dispose() }
`, run);
}

async function terminateVerified(expected, run) {
    if (!sameProcess(expected, expected)) throw new Error('Invalid engine identity; refusing process termination.');
    const encoded = Buffer.from(JSON.stringify(expected), 'utf8').toString('base64');
    return powershell(snapshotScript + `
$expected = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('${encoded}')) | ConvertFrom-Json
try { $process = [System.Diagnostics.Process]::GetProcessById($expected.pid) }
catch [System.ArgumentException] { @{terminated=$false;alreadyExited=$true} | ConvertTo-Json -Compress; exit }
try {
    $actual = Read-Identity $process
    if ($null -eq $actual) { @{terminated=$false;alreadyExited=$true} | ConvertTo-Json -Compress; exit }
    if ($actual.pid -ne $expected.pid -or $actual.path -ine $expected.path -or $actual.createdFileTime -cne $expected.createdFileTime -or $actual.sha256 -cne $expected.sha256) {
        throw 'Engine identity changed; refusing to terminate a different process.'
    }
    $process.Kill()
    if (!$process.WaitForExit(5000)) { throw 'The verified engine did not exit in time.' }
    @{terminated=$true} | ConvertTo-Json -Compress
} finally { $process.Dispose() }
`, run);
}

function readReceipt(file) {
    try {
        const value = JSON.parse(fs.readFileSync(file, 'utf8'));
        if (!sameProcess(value, value)) throw new Error('Invalid engine ownership receipt.');
        return value;
    } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}

function writeReceipt(file, identity) {
    if (!sameProcess(identity, identity)) throw new Error('Cannot record an unverified engine identity.');
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const temporary = file + '.tmp';
    fs.writeFileSync(temporary, JSON.stringify(identity, null, 2));
    fs.renameSync(temporary, file);
}

async function prepareEngine({ engineSha256, receipt, inspect = inspectEngine, healthy, stopAudio,
    terminate = terminateVerified, wait = ms => new Promise(resolve => setTimeout(resolve, ms)), onReplacing = () => {} }) {
    const observed = await inspect();
    if (!observed.process) {
        if (observed.mutexPresent) throw new Error('An engine is starting or owns the audio lock without a reachable API. No second engine was started.');
        return null;
    }
    const identity = observed.process;
    if (!sameProcess(identity, identity)) throw new Error('The API listener identity could not be verified.');
    const current = identity.sha256 === engineSha256;
    if (!current && !sameProcess(identity, receipt))
        throw new Error('The API port belongs to an unverified process or unmanaged engine. TimoxVSSC will not stop it or start a competing engine.');
    if (current) {
        try {
            const port = await healthy();
            if (port && sameProcess(identity, (await inspect()).process)) return { port, identity };
        } catch (error) {
            if (error.code === 'API_CONTRACT_MISMATCH') throw error;
            // A verified but unresponsive engine is replaced below.
        }
    }
    onReplacing(identity);
    if (!sameProcess(identity, (await inspect()).process)) throw new Error('Engine identity changed before replacement.');
    try { await stopAudio(identity); } catch (_) { /* Older/unresponsive APIs may not acknowledge the stop. */ }
    await terminate(identity);
    const deadline = Date.now() + 5000;
    for (let attempt = 0; attempt < 20 && Date.now() < deadline; ++attempt) {
        const after = await inspect();
        if (!after.process && !after.mutexPresent) return null;
        if (after.process && !sameProcess(identity, after.process)) throw new Error('Another process took the API port during replacement.');
        await wait(250);
    }
    throw new Error('The previous engine did not release its API port and audio lock. No second engine was started.');
}

module.exports = { sameProcess, inspectEngine, inspectProcess, terminateVerified, readReceipt, writeReceipt, prepareEngine };
