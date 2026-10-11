param(
    [string]$ProbePath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-probe.exe'),
    [string]$ThresholdsPath = (Join-Path $PSScriptRoot '..\tools\vasio-probe\thresholds.json'),
    [string]$BaseUri = 'http://127.0.0.1:52525',
    [string]$DoctorPath = (Join-Path $PSScriptRoot '..\tools\vasio-doctor\vasio-doctor.ps1'),
    [string]$StatusPath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-status.exe'),
    [string]$Driver = 'TimoxVasio',
    [ValidateRange(1, 1000000)][int]$LifecycleCycles = 1000,
    [ValidateRange(0, 86400)][int]$CadenceSeconds = 0,
    [ValidateRange(0, 86400)][int]$LoopbackSeconds = 0,
    [ValidateRange(0, 86400)][int]$MulticlientSeconds = 0,
    [ValidateRange(0, 86400)][int]$ReconfigSeconds = 0,
    [switch]$Sweep,
    [switch]$PlanOnly,
    [string[]]$LongFormats = @(),
    [string]$ResultsRoot = (Join-Path $PSScriptRoot 'results')
)

$ErrorActionPreference = 'Stop'
if ($Sweep -or $PlanOnly) {
    & (Join-Path $PSScriptRoot 'with-probe-route.ps1') -BaseUri $BaseUri `
        -ProbePath $ProbePath -ThresholdsPath $ThresholdsPath `
        -DoctorPath $DoctorPath -StatusPath $StatusPath -Driver $Driver `
        -ResultsRoot $ResultsRoot -LifecycleCycles $LifecycleCycles `
        -Sweep -PlanOnly:$PlanOnly -LongFormats $LongFormats
    exit $LASTEXITCODE
}
. (Join-Path $PSScriptRoot 'probe_process.ps1')
. (Join-Path $PSScriptRoot 'matrix_failure_code.ps1')
$probe = (Resolve-Path -LiteralPath $ProbePath -ErrorAction Stop).Path
$thresholds = (Resolve-Path -LiteralPath $ThresholdsPath -ErrorAction Stop).Path
$apiPort = ([Uri]$BaseUri).Port
$sessionId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' +
    [Guid]::NewGuid().ToString('N').Substring(0, 8)
$session = Join-Path $ResultsRoot $sessionId
New-Item -ItemType Directory -Path $session -Force | Out-Null

$entries = [System.Collections.Generic.List[object]]::new()
$loopbackFailed = $false
$multiclientFailed = $false
$reconfigFailed = $false
$doctorReport = Join-Path $session 'doctor.json'
$doctorExit = Invoke-ProbeProcess -FilePath (Get-Command pwsh).Source -Arguments @(
    '-NoProfile', '-File', (Resolve-Path -LiteralPath $DoctorPath).Path,
    '--output', $doctorReport, '--api-port', $apiPort.ToString(),
    '--status-path', $StatusPath
) -TimeoutSeconds 120
$doctor = Get-Content -LiteralPath $doctorReport -Raw | ConvertFrom-Json
$doctorEntry = [ordered]@{ mode = 'doctor'; report = 'doctor.json';
    exitCode = $doctorExit; verdict = $doctor.verdict }
if ($doctorExit -ne 0 -or $doctor.verdict -eq 'red') {
    $redCodes = @($doctor.findings | Where-Object severity -eq 'red' | ForEach-Object code)
    $reason = if ($doctorExit -ne 0) {
        "Doctor exited with code $doctorExit; no ASIO mode was started."
    } else {
        "Doctor reported red ($($redCodes -join ', ')); no ASIO mode was started."
    }
    [ordered]@{ createdUtc = [DateTime]::UtcNow.ToString('o'); driver = $Driver;
        driverBinary = $null; results = @($doctorEntry); nextAction = $reason } |
        ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $session 'summary.json') -Encoding utf8
    @('# TimoxVasio probe matrix', '', "Driver: $Driver", '',
        '| Mode | Verdict | Report |', '| --- | --- | --- |',
        "| doctor | $($doctor.verdict) | doctor.json |", '', $reason, '') |
        Set-Content -LiteralPath (Join-Path $session 'SUMMARY.md') -Encoding utf8
    throw $reason
}
$preflightState = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
$preflightClients = @($preflightState.virtualDrivers | ForEach-Object { $_.clients } |
    Where-Object { $null -ne $_ })
if ($preflightClients.Count -gt 0) {
    $names = ($preflightClients | ForEach-Object { "$($_.processName) PID $($_.pid)" }) -join ', '
    $reason = "Close ASIO clients before running the test matrix: $names"
    $blockedSummary = [ordered]@{
        createdUtc = [DateTime]::UtcNow.ToString('o')
        driver = $Driver
        driverBinary = $null
        results = @($doctorEntry)
        nextAction = $reason
    }
    $blockedSummary | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $session 'summary.json') -Encoding utf8
    @('# TimoxVasio probe matrix', '', "Driver: $Driver", '',
        '| Mode | Verdict | Report |', '| --- | --- | --- |',
        "| doctor | $($doctor.verdict) | doctor.json |", '', $reason, '') |
        Set-Content -LiteralPath (Join-Path $session 'SUMMARY.md') -Encoding utf8
    throw $reason
}
$capsPath = Join-Path $session 'caps.json'
$capsExit = Invoke-ProbeProcess -FilePath $probe -Arguments @('caps', '--driver', $Driver,
    '--output', $capsPath) -TimeoutSeconds 120
$caps = Get-Content -LiteralPath $capsPath -Raw | ConvertFrom-Json
$entries.Add([ordered]@{ mode = 'caps'; report = 'caps.json'; exitCode = $capsExit; verdict = $caps.verdict })
$driverBinary = $null
if ($caps.registeredPath -and (Test-Path -LiteralPath $caps.registeredPath -PathType Leaf)) {
    $driverFile = Get-Item -LiteralPath $caps.registeredPath
    $driverBinary = [ordered]@{
        path = $driverFile.FullName
        lastWriteUtc = $driverFile.LastWriteTimeUtc.ToString('o')
        length = $driverFile.Length
        sha256 = (Get-FileHash -LiteralPath $driverFile.FullName -Algorithm SHA256).Hash
    }
}

if (-not (Test-MatrixEntryPass -Entry $entries[0]) -or -not $driverBinary) {
    $reason = if (-not $driverBinary) {
        'The registered driver binary could not be identified and hashed. Later modes were not started.'
    } else {
        'The caps mode did not pass. Later modes were not started.'
    }
} else {
    $state = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
    $hasProbeRoute = @($state.configuredRoutes | Where-Object {
        $_.sourceEndpointId -match 'virtual:TimoxVasio:app:vasio-probe\.exe:' -or
        $_.destinationEndpointId -match 'virtual:TimoxVasio:app:vasio-probe\.exe:'
    }).Count -gt 0
    if (-not $hasProbeRoute) {
        $reason = 'No saved route for vasio-probe.exe. Lifecycle callbacks require an active route; the current audio configuration was not changed.'
    } else {
        $timeout = [Math]::Max(120, [Math]::Ceiling($LifecycleCycles * 10))
        $activeMode = 'lifecycle'
        try {
            $maxStageMs = (Get-Content -LiteralPath $thresholds -Raw | ConvertFrom-Json).lifecycle.maxStageMs
            foreach ($variant in @('lifecycle', 'lifecycle-reload')) {
                $activeMode = $variant
                $lifecyclePath = Join-Path $session "$variant.json"
                $lifecycleArguments = @('lifecycle', '--driver', $Driver,
                    '--output', $lifecyclePath, '--thresholds', $thresholds,
                    '--cycles', $LifecycleCycles.ToString())
                if ($variant -eq 'lifecycle-reload') { $lifecycleArguments += '--reload' }
                $lifecycleCall = @{
                    Arguments = $lifecycleArguments
                    FilePath = $probe
                    TimeoutSeconds = $timeout
                    StageLogPath = "$lifecyclePath.events.jsonl.stages.jsonl"
                    MaxStageMs = $maxStageMs
                }
                $lifecycleExit = Invoke-ProbeProcess @lifecycleCall
                $lifecycle = Get-Content -LiteralPath $lifecyclePath -Raw | ConvertFrom-Json
                $entries.Add([ordered]@{ mode = $variant; report = "$variant.json";
                    exitCode = $lifecycleExit; verdict = $lifecycle.verdict })
                if (-not (Test-MatrixEntryPass -Entry $entries[$entries.Count - 1])) { break }
            }
            $reason = if ($entries.Count -eq 3 -and (Test-MatrixEntryPass -Entry $entries[2])) {
                'Modes 1 and 2 passed, including DLL reload. Mode 3 was not requested in this run.'
            } else {
                'Lifecycle did not pass. Modes 3 through 7 were not started.'
            }
            if ($CadenceSeconds -gt 0 -and $entries.Count -eq 3 -and
                (Test-MatrixEntryPass -Entry $entries[2])) {
                $activeMode = 'cadence'
                $cadencePath = Join-Path $session 'cadence.json'
                $status = (Resolve-Path -LiteralPath $StatusPath -ErrorAction Stop).Path
                $statusHistory = Join-Path $session 'cadence-status.jsonl'
                $statusProcess = Start-Process -FilePath $status -ArgumentList @(
                    '--json', '--seconds', $CadenceSeconds.ToString(), '--history', $statusHistory,
                    '--api-port', $apiPort.ToString()
                ) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'cadence-status.stdout.jsonl') -RedirectStandardError (Join-Path $session 'cadence-status.stderr.log')
                try {
                    $cadenceExit = Invoke-ProbeProcess -FilePath $probe -Arguments @(
                        'cadence', '--driver', $Driver, '--output', $cadencePath,
                        '--thresholds', $thresholds, '--seconds', $CadenceSeconds.ToString()
                    ) -TimeoutSeconds ($CadenceSeconds + 120)
                    if (-not $statusProcess.WaitForExit(5000)) {
                        throw 'vasio-status did not finish the cadence history within five seconds.'
                    }
                    if ($statusProcess.ExitCode -ne 0) {
                        throw "vasio-status exited with code $($statusProcess.ExitCode); see cadence-status.stderr.log."
                    }
                } finally {
                    if (-not $statusProcess.HasExited) {
                        $statusProcess.Kill()
                        [void]$statusProcess.WaitForExit(5000)
                    }
                    $statusProcess.Dispose()
                }
                $cadence = Get-Content -LiteralPath $cadencePath -Raw | ConvertFrom-Json
                $statusSamples = if (Test-Path -LiteralPath $statusHistory) {
                    @(Get-Content -LiteralPath $statusHistory).Count } else { 0 }
                if ($statusSamples -eq 0) { throw 'Cadence status history contains no observations.' }
                $entries.Add([ordered]@{ mode = 'cadence'; report = 'cadence.json';
                    exitCode = $cadenceExit; verdict = $cadence.verdict;
                    statusSamples = $statusSamples })
                $reason = if (Test-MatrixEntryPass -Entry $entries[3]) {
                    'Modes 1 through 3 passed. Mode 4 was not requested in this run; mode 7 runs separately.'
                } else {
                    'Cadence did not pass. Modes 4 through 7 were not started.'
                }
            }
            if ($LoopbackSeconds -gt 0) {
                if ($LoopbackSeconds -lt 5) { throw 'LoopbackSeconds must be at least 5.' }
                if ($CadenceSeconds -eq 0 -or $entries.Count -lt 4 -or
                    -not (Test-MatrixEntryPass -Entry $entries[3])) {
                    $reason = 'Mode 4 requires a passing cadence run in this matrix session.'
                    $loopbackFailed = $true
                } else {
                    $loopbackScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'with-loopback-route.ps1')).Path
                    $killAfter = [Math]::Max(1, [Math]::Min(3, $LoopbackSeconds - 3))
                    foreach ($variant in @('receiver-first', 'sender-first', 'self', 'kill-send', 'kill-receive')) {
                        $activeMode = "loopback-$variant"
                        $loopbackSession = Join-Path $session $activeMode
                        $loopbackArguments = @('-NoProfile', '-File', $loopbackScript,
                            '-BaseUri', $BaseUri, '-Seconds', $LoopbackSeconds.ToString(),
                            '-ProbePath', $probe, '-StatusPath', $StatusPath,
                            '-ThresholdsPath', $thresholds, '-SessionPath', $loopbackSession)
                        switch ($variant) {
                            'sender-first' { $loopbackArguments += @('-Order', 'sender-first') }
                            'self' { $loopbackArguments += '-Self' }
                            'kill-send' { $loopbackArguments += @('-KillRole', 'send', '-KillAfterSeconds', $killAfter.ToString()) }
                            'kill-receive' { $loopbackArguments += @('-KillRole', 'receive', '-KillAfterSeconds', $killAfter.ToString()) }
                        }
                        $loopbackExit = Invoke-ProbeProcess -FilePath (Get-Command pwsh).Source -Arguments $loopbackArguments -TimeoutSeconds ($LoopbackSeconds + 90) -OutputLogPath (Join-Path $session $activeMode)
                        if (-not (Test-Path -LiteralPath (Join-Path $loopbackSession 'summary.json'))) {
                            $childError = Get-Content -LiteralPath (Join-Path $session "$activeMode.stderr.txt") -Raw
                            throw "Loopback child produced no summary (exit $loopbackExit): $childError"
                        }
                        $loopbackSummary = Get-Content -LiteralPath (Join-Path $loopbackSession 'summary.json') -Raw | ConvertFrom-Json
                        $loopbackVerdict = if ($loopbackExit -eq 0 -and $loopbackSummary.verdict -eq 'pass') { 'pass' } else { 'fail' }
                        if ($loopbackVerdict -ne 'pass') { $loopbackFailed = $true }
                        $entries.Add([ordered]@{ mode = $activeMode;
                            report = "$activeMode/summary.json"; exitCode = $loopbackExit;
                            verdict = $loopbackVerdict; statusSamples = $loopbackSummary.statusSamples })
                    }
                    $reason = if ($loopbackFailed) {
                        'Mode 4 found a loopback failure; modes 5 and 6 were not started. See the variant reports.'
                    } else {
                        'Modes 1 through 4 passed. Mode 5 was not requested in this run.'
                    }
                }
            }
            if ($MulticlientSeconds -gt 0) {
                if ($MulticlientSeconds -lt 12) { throw 'MulticlientSeconds must be at least 12.' }
                if ($LoopbackSeconds -eq 0 -or $loopbackFailed) {
                    $reason = 'Mode 5 requires a passing mode-4 run in this matrix session.'
                    $multiclientFailed = $true
                } else {
                    $multiclientScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'with-multiclient-route.ps1')).Path
                    foreach ($clientCount in @(2, 4, 8)) {
                        $activeMode = "multiclient-$clientCount"
                        $multiSession = Join-Path $session $activeMode
                        $multiArguments = @('-NoProfile', '-File', $multiclientScript,
                            '-BaseUri', $BaseUri, '-ClientCount', $clientCount.ToString(),
                            '-Seconds', $MulticlientSeconds.ToString(), '-Seed', '42',
                            '-ProbePath', $probe, '-StatusPath', $StatusPath,
                            '-ThresholdsPath', $thresholds, '-SessionPath', $multiSession)
                        $multiExit = Invoke-ProbeProcess -FilePath (Get-Command pwsh).Source -Arguments $multiArguments -TimeoutSeconds ($MulticlientSeconds + 2 * $clientCount + 120) -OutputLogPath (Join-Path $session $activeMode)
                        if (-not (Test-Path -LiteralPath (Join-Path $multiSession 'summary.json'))) {
                            $childError = Get-Content -LiteralPath (Join-Path $session "$activeMode.stderr.txt") -Raw
                            throw "Multiclient child produced no summary (exit $multiExit): $childError"
                        }
                        $multiSummary = Get-Content -LiteralPath (Join-Path $multiSession 'summary.json') -Raw | ConvertFrom-Json
                        $multiVerdict = if ($multiExit -eq 0 -and $multiSummary.verdict -eq 'pass') { 'pass' } else { 'fail' }
                        if ($multiVerdict -ne 'pass') { $multiclientFailed = $true }
                        $entries.Add([ordered]@{ mode = $activeMode;
                            report = "$activeMode/summary.json"; exitCode = $multiExit;
                            verdict = $multiVerdict; statusSamples = $multiSummary.statusSamples })
                    }
                    $reason = if ($multiclientFailed) {
                        'Mode 5 found a multiclient failure; mode 6 was not started. See the per-client reports.'
                    } else {
                        'Modes 1 through 5 passed. Mode 6 was not requested in this run.'
                    }
                }
            }
            if ($ReconfigSeconds -gt 0) {
                if ($ReconfigSeconds -lt 12) { throw 'ReconfigSeconds must be at least 12.' }
                if ($MulticlientSeconds -eq 0 -or $multiclientFailed) {
                    $reason = 'Mode 6 requires a passing mode-5 run in this matrix session.'
                    $reconfigFailed = $true
                } else {
                    $reconfigScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'with-multiclient-route.ps1')).Path
                    foreach ($clientCount in @(2, 4, 8)) {
                        $activeMode = "reconfig-$clientCount"
                        $reconfigSession = Join-Path $session $activeMode
                        $reconfigArgs = @('-NoProfile', '-File', $reconfigScript, '-Reconfigure',
                            '-BaseUri', $BaseUri, '-ClientCount', $clientCount.ToString(),
                            '-Seconds', $ReconfigSeconds.ToString(), '-Seed', '42',
                            '-ProbePath', $probe, '-StatusPath', $StatusPath,
                            '-ThresholdsPath', $thresholds, '-SessionPath', $reconfigSession)
                        $reconfigExit = Invoke-ProbeProcess -FilePath (Get-Command pwsh).Source -Arguments $reconfigArgs -TimeoutSeconds ($ReconfigSeconds + 2 * $clientCount + 150) -OutputLogPath (Join-Path $session $activeMode)
                        if (-not (Test-Path -LiteralPath (Join-Path $reconfigSession 'summary.json'))) {
                            $childError = Get-Content -LiteralPath (Join-Path $session "$activeMode.stderr.txt") -Raw
                            throw "Reconfig child produced no summary (exit $reconfigExit): $childError"
                        }
                        $reconfigSummary = Get-Content -LiteralPath (Join-Path $reconfigSession 'summary.json') -Raw | ConvertFrom-Json
                        $reconfigVerdict = if ($reconfigExit -eq 0 -and $reconfigSummary.verdict -eq 'pass') { 'pass' } else { 'fail' }
                        if ($reconfigVerdict -ne 'pass') { $reconfigFailed = $true }
                        $entries.Add([ordered]@{ mode = $activeMode;
                            report = "$activeMode/summary.json"; exitCode = $reconfigExit;
                            verdict = $reconfigVerdict; statusSamples = $reconfigSummary.statusSamples })
                    }
                    $reason = if ($reconfigFailed) {
                        'Mode 6 found a reconfiguration failure. See the 2-, 4- and 8-client restart reports.'
                    } else { 'Modes 1 through 6 passed. Mode 7 requires a separately chosen hardware driver.' }
                }
            }
        }
        catch {
            $watchdog = [ordered]@{ mode = $activeMode; driver = $Driver;
                verdict = 'fail'; error = $_.Exception.Message;
                createdUtc = [DateTime]::UtcNow.ToString('o') }
            $watchdogName = "$activeMode-watchdog.json"
            $watchdog | ConvertTo-Json -Depth 5 |
                Set-Content -LiteralPath (Join-Path $session $watchdogName) -Encoding utf8
            $entries.Add([ordered]@{ mode = $activeMode; report = $watchdogName;
                exitCode = 124; verdict = 'fail' })
            $reason = "$activeMode watchdog stopped the probe: $($_.Exception.Message)"
        }
    }
}

if ($doctor.verdict -eq 'red') {
    $redCodes = @($doctor.findings | Where-Object severity -eq 'red' | ForEach-Object code)
    $reason = "Doctor reported red: $($redCodes -join ', '). $reason"
}
$summary = [ordered]@{
    createdUtc = [DateTime]::UtcNow.ToString('o')
    driver = $Driver
    driverBinary = $driverBinary
    results = @($doctorEntry) + $entries.ToArray()
    nextAction = $reason
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $session 'summary.json') -Encoding utf8
$markdown = @('# TimoxVasio probe matrix', '', "Driver: $Driver", '', '| Mode | Verdict | Report |',
    '| --- | --- | --- |')
foreach ($entry in $summary.results) {
    $markdown += "| $($entry.mode) | $($entry.verdict) | $($entry.report) |"
}
$markdown += @('', $reason, '')
$markdown | Set-Content -LiteralPath (Join-Path $session 'SUMMARY.md') -Encoding utf8
Write-Output "Results: $session"
Write-Output $reason
$failureCode = Get-MatrixFailureCode -Results @($summary.results) -DoctorVerdict $doctor.verdict
if ($failureCode -ne 0) { exit $failureCode }
if ($entries.Count -eq 0 -or -not (Test-MatrixEntryPass -Entry $entries[0])) { exit 1 }
if ($entries.Count -lt 3 -or -not (Test-MatrixEntryPass -Entry $entries[1]) -or
    -not (Test-MatrixEntryPass -Entry $entries[2])) { exit 6 }
if ($CadenceSeconds -gt 0 -and ($entries.Count -lt 4 -or
    -not (Test-MatrixEntryPass -Entry $entries[3]))) { exit 6 }
if ($LoopbackSeconds -gt 0 -and $loopbackFailed) { exit 6 }
if ($MulticlientSeconds -gt 0 -and $multiclientFailed) { exit 6 }
if ($ReconfigSeconds -gt 0 -and $reconfigFailed) { exit 6 }
