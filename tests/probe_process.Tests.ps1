$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'probe_process.ps1')
$pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
$stageLog = Join-Path $env:TEMP ('vasio-probe-watchdog-' + [Guid]::NewGuid().ToString('N') + '.jsonl')
try {
    $exitCode = Invoke-ProbeProcess -FilePath $pwshPath -Arguments @('-NoProfile', '-Command', 'exit 0') -TimeoutSeconds 5
    if ($exitCode -ne 0) { throw "Expected normal exit 0, got $exitCode" }

    $started = [System.Diagnostics.Stopwatch]::StartNew()
    $message = ''
    try {
        [void](Invoke-ProbeProcess -FilePath $pwshPath -Arguments @('-NoProfile', '-File',
            (Join-Path $PSScriptRoot 'probe_process_fixture.ps1'), '-StageLogPath', $stageLog) `
            -TimeoutSeconds 10 -StageLogPath $stageLog -MaxStageMs 100)
    }
    catch { $message = $_.Exception.Message }
    $started.Stop()
    if ($message -notlike 'ASIO stage fixture-hang exceeded*') {
        throw "Expected stage watchdog failure, got: $message"
    }
    if ($started.Elapsed.TotalSeconds -gt 5) {
        throw "Stage watchdog took too long: $($started.Elapsed.TotalSeconds) s"
    }
    Write-Output 'PASS: probe process exits normally or is killed when an ASIO stage stalls.'
}
finally {
    if (Test-Path -LiteralPath $stageLog) { Remove-Item -LiteralPath $stageLog -Force }
}
