param(
    [string]$BaseUri = 'http://127.0.0.1:52525',
    [ValidateSet(2, 4, 8)][int]$ClientCount = 2,
    [ValidateRange(12, 86400)][int]$Seconds = 20,
    [int]$Seed = 314159,
    [switch]$Reconfigure,
    [int]$TargetRate = 0,
    [int]$TargetBufferFrames = 0,
    [string]$ProbePath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-probe.exe'),
    [string]$StatusPath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-status.exe'),
    [string]$ThresholdsPath = (Join-Path $PSScriptRoot '..\tools\vasio-probe\thresholds.json'),
    [string]$SessionPath = ''
)

$ErrorActionPreference = 'Stop'
$probe = (Resolve-Path -LiteralPath $ProbePath -ErrorAction Stop).Path
$status = (Resolve-Path -LiteralPath $StatusPath -ErrorAction Stop).Path
$thresholds = (Resolve-Path -LiteralPath $ThresholdsPath -ErrorAction Stop).Path
$session = if ($SessionPath) { $SessionPath } else {
    Join-Path $PSScriptRoot ('results\multiclient-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') +
        '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
}
if (Test-Path -LiteralPath $session) { throw "Result directory already exists: $session" }

function Read-ApiMessage([System.Net.WebSockets.ClientWebSocket]$Socket) {
    $stream = [System.IO.MemoryStream]::new()
    $buffer = [byte[]]::new(8192)
    $deadline = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(20))
    try {
        do {
            $result = $Socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $deadline.Token).GetAwaiter().GetResult()
            if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
                throw 'Engine WebSocket closed during configuration.apply.'
            }
            $stream.Write($buffer, 0, $result.Count)
            if ($stream.Length -gt 1048576) { throw 'Engine response exceeded 1 MiB.' }
        } while (-not $result.EndOfMessage)
        return [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
    } finally { $deadline.Dispose(); $stream.Dispose() }
}

function Invoke-ConfigurationApply($Configuration) {
    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    $socket.Options.AddSubProtocol('vasio.api.v1')
    $uri = $BaseUri.Replace('http://', 'ws://').Replace('https://', 'wss://') + '/api/v1/ws'
    try {
        [void]$socket.ConnectAsync([Uri]$uri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        $requestId = [Guid]::NewGuid().ToString('N')
        $request = @{ id = $requestId; command = 'configuration.apply'; payload = $Configuration } |
            ConvertTo-Json -Depth 20 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($request)
        [void]$socket.SendAsync([ArraySegment[byte]]::new($bytes),
            [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        for ($index = 0; $index -lt 40; $index++) {
            $message = Read-ApiMessage $socket
            if ($message.id -ne $requestId) { continue }
            if (-not $message.success -or -not $message.result.accepted) {
                throw "configuration.apply rejected: $($message | ConvertTo-Json -Compress -Depth 10)"
            }
            return
        }
        throw 'No configuration.apply acknowledgement from the engine.'
    } finally { $socket.Dispose() }
}

function Restore-Configuration($Configuration) {
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        try { Invoke-ConfigurationApply $Configuration; return }
        catch {
            if ($attempt -eq 10 -or $_.Exception.Message -notmatch 'UNKNOWN_ENDPOINT') { throw }
            Start-Sleep -Milliseconds 300
        }
    }
}

function Wait-Client([string]$ProcessName, [int]$ExpectedPid = 0) {
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $snapshot = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
        $clients = @($snapshot.virtualDrivers | ForEach-Object clients | Where-Object { $_ })
        if (@($clients | Where-Object {
            $_.processName -eq $ProcessName -and
            ($ExpectedPid -eq 0 -or $_.pid -eq $ExpectedPid)
        }).Count) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Client $ProcessName did not attach within 5 seconds."
}

function Start-Probe($Spec, [int]$DurationSeconds = 0) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Spec.exe
    $start.WorkingDirectory = $session
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $duration = if ($DurationSeconds -gt 0) { $DurationSeconds } else {
        $Seconds + $ClientCount * 2 + $(if ($Reconfigure) { 20 } else { 0 })
    }
    foreach ($argument in @('loopback', '--role', $Spec.role, '--driver', 'TimoxVasio',
        '--channel', ($Spec.lane - 1).ToString(),
        '--seconds', $duration.ToString(),
        '--output', $Spec.report, '--thresholds', $thresholds)) {
        [void]$start.ArgumentList.Add($argument)
    }
    return [System.Diagnostics.Process]::Start($start)
}

function Measure-SampleErrors($Events) {
    [long]$total = 0
    foreach ($event in $Events) {
        $total += [long]$event.corrupted + [long]$event.missing + [long]$event.duplicated
    }
    return $total
}
. (Join-Path $PSScriptRoot 'analyze_multiclient_events.ps1')

$state = $null
$activeClients = @()
for ($attempt = 0; $attempt -lt 50; $attempt++) {
    $state = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
    $activeClients = @($state.virtualDrivers | ForEach-Object clients | Where-Object { $_ })
    if (-not $activeClients.Count) { break }
    Start-Sleep -Milliseconds 100
}
if ($activeClients.Count) { throw 'Close other virtual ASIO clients before the multiclient test.' }
if (-not $state.engine.physicalDriverId -or -not $state.engine.sampleRate -or -not $state.engine.bufferFrames) {
    throw 'Select a physical ASIO driver, rate and buffer in Control before this test.'
}
$physical = @($state.physicalDrivers | Where-Object id -eq $state.engine.physicalDriverId)
if ($physical.Count -ne 1 -or -not @($physical[0].inputEndpoints).Count -or
    -not @($physical[0].outputEndpoints).Count) {
    throw 'A physical input and output are required to preserve the clock.'
}
$chosenRate = 0
$chosenBuffer = 0
if ($Reconfigure) {
    $rates = @($physical[0].sampleRates)
    $chosenRate = if ($TargetRate) { $TargetRate } else {
        @($rates | Where-Object { $_ -ne $state.engine.sampleRate } | Sort-Object)[0]
    }
    if (-not $chosenRate -or $chosenRate -eq $state.engine.sampleRate -or $chosenRate -notin $rates) {
        throw 'Target sample rate must differ from the current rate and be declared by the physical driver.'
    }
    $sizes = $physical[0].bufferSizes
    $candidates = @(512, 256, 128, 1024, 64, 2048) | Where-Object {
        $_ -ne $state.engine.bufferFrames -and $_ -ge $sizes.minimumFrames -and
        $_ -le $sizes.maximumFrames
    }
    $chosenBuffer = if ($TargetBufferFrames) { $TargetBufferFrames } else { @($candidates)[0] }
    if (-not $chosenBuffer -or $chosenBuffer -eq $state.engine.bufferFrames -or
        $chosenBuffer -lt $sizes.minimumFrames -or $chosenBuffer -gt $sizes.maximumFrames -or
        ($sizes.granularity -eq -1 -and ($chosenBuffer -band ($chosenBuffer - 1)) -ne 0)) {
        throw 'Target buffer must differ from the current size and satisfy the physical driver bounds.'
    }
}
$profiles = @((Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/application-profiles" -TimeoutSec 5).profiles)
$originalConfiguration = @{ physicalDriverId = $state.engine.physicalDriverId;
    sampleRate = $state.engine.sampleRate; bufferFrames = $state.engine.bufferFrames;
    routes = @($state.configuredRoutes) }
$newProfiles = @($profiles)
$routes = @($originalConfiguration.routes)
$specs = [System.Collections.Generic.List[object]]::new()
for ($lane = 1; $lane -le $ClientCount / 2; ++$lane) {
    $sender = "vasio-load-send-$lane.exe"
    $receiver = "vasio-load-receive-$lane.exe"
    $source = "virtual:TimoxVasio:app:$($sender):output:$lane"
    $destination = "virtual:TimoxVasio:app:$($receiver):input:$lane"
    $routes += @{ id = "vasio-load-$lane"; sourceEndpointId = $source;
        destinationEndpointId = $destination; gainDb = 0.0; mute = $false }
    $routes += @{ id = "vasio-load-clock-send-$lane"; sourceEndpointId = $source;
        destinationEndpointId = $physical[0].outputEndpoints[0].id; gainDb = 0.0; mute = $true }
    $routes += @{ id = "vasio-load-clock-receive-$lane";
        sourceEndpointId = $physical[0].inputEndpoints[0].id;
        destinationEndpointId = $destination; gainDb = 0.0; mute = $true }
    foreach ($role in @('send', 'receive')) {
        $name = if ($role -eq 'send') { $sender } else { $receiver }
        $newProfiles += @{ processName = $name; inputChannels = $lane; outputChannels = $lane }
        $specs.Add([ordered]@{ lane = $lane; role = $role; name = $name;
            exe = Join-Path $session $name; report = Join-Path $session ($name + '.json');
            process = $null; killed = $false })
    }
}
$temporary = @{ physicalDriverId = $originalConfiguration.physicalDriverId;
    sampleRate = $originalConfiguration.sampleRate; bufferFrames = $originalConfiguration.bufferFrames;
    routes = $routes }
New-Item -ItemType Directory -Path $session -Force | Out-Null
@{ configuration = $originalConfiguration; profiles = $profiles; seed = $Seed;
    clientCount = $ClientCount; routes = $routes } |
    ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $session 'restore.json') -Encoding utf8
$probeHash = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash
foreach ($spec in $specs) {
    Copy-Item -LiteralPath $probe -Destination $spec.exe
    if ((Get-FileHash -LiteralPath $spec.exe -Algorithm SHA256).Hash -ne $probeHash) {
        throw "Probe copy hash mismatch: $($spec.name)"
    }
}
$rng = [Random]::new($Seed)
$launchOrder = @($specs.ToArray())
for ($index = $launchOrder.Count - 1; $index -gt 0; --$index) {
    $swap = $rng.Next($index + 1)
    $item = $launchOrder[$index]
    $launchOrder[$index] = $launchOrder[$swap]
    $launchOrder[$swap] = $item
}
$victim = if ($Reconfigure) { $null } else { $launchOrder[$rng.Next($launchOrder.Count)] }
$profilesTouched = $false
$configurationTouched = $false
$statusProcess = $null
$killQpc = $null
$restartQpc = $null
$restarted = $null
$rateChangeQpc = $null
$bufferChangeQpc = $null
$rateSnapshot = $null
$bufferSnapshot = $null
$windowStartQpc = $null
$windowEndQpc = $null
$runExit = 1
try {
    $body = @{ profiles = $newProfiles } | ConvertTo-Json -Depth 10
    $profilesTouched = $true
    [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
    $configurationTouched = $true
    Invoke-ConfigurationApply $temporary
    $statusProcess = Start-Process -FilePath $status -ArgumentList @(
        '--json', '--seconds', ($Seconds + $ClientCount * 2 + $(if ($Reconfigure) { 30 } else { 10 })).ToString(), '--history',
        (Join-Path $session 'status.jsonl'), '--api-port', ([Uri]$BaseUri).Port.ToString()
    ) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'status.stdout.jsonl') -RedirectStandardError (Join-Path $session 'status.stderr.log')
    foreach ($spec in $launchOrder) {
        $spec.process = Start-Probe $spec
        Wait-Client $spec.name $spec.process.Id
        Start-Sleep -Milliseconds 100
    }
    $windowStartQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
    $beforeKillSeconds = [Math]::Max(2, [int][Math]::Floor($Seconds / 3))
    Start-Sleep -Seconds $beforeKillSeconds
    if ($Reconfigure) {
        . (Join-Path $PSScriptRoot 'reconfig_signal_window.ps1')
        $limits = (Get-Content -LiteralPath $thresholds -Raw | ConvertFrom-Json).multiclient
        $graceTicks = [long]($limits.restartGraceSeconds * [System.Diagnostics.Stopwatch]::Frequency)
        $rateChangeQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $rateConfiguration = @{ physicalDriverId = $temporary.physicalDriverId;
            sampleRate = $chosenRate; bufferFrames = $temporary.bufferFrames; routes = $routes }
        Invoke-ConfigurationApply $rateConfiguration
        $rateSnapshot = (Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5).engine
        Start-Sleep -Seconds $beforeKillSeconds
        $bufferChangeQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $bufferConfiguration = @{ physicalDriverId = $temporary.physicalDriverId;
            sampleRate = $chosenRate; bufferFrames = $chosenBuffer; routes = $routes }
        Invoke-ConfigurationApply $bufferConfiguration
        $bufferSnapshot = (Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5).engine
        Start-Sleep -Seconds ($Seconds - 2 * $beforeKillSeconds)
    } else {
        if ($victim.process.HasExited) { throw "Selected victim $($victim.name) exited before the planned stop." }
        $killQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $victim.process.Kill($true)
        [void]$victim.process.WaitForExit(5000)
        $victim.killed = $true
        $beforeRestartSeconds = [Math]::Max(1,
            [int][Math]::Floor(($Seconds - $beforeKillSeconds) / 2))
        Start-Sleep -Seconds $beforeRestartSeconds
        $restarted = [ordered]@{ lane = $victim.lane; role = $victim.role;
            name = $victim.name; exe = $victim.exe;
            report = Join-Path $session ($victim.name + '.restart.json'); process = $null }
        $restartQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $remainingSeconds = $Seconds - $beforeKillSeconds - $beforeRestartSeconds
        $restarted.process = Start-Probe $restarted ($remainingSeconds + $ClientCount * 2 + 2)
        Wait-Client $restarted.name $restarted.process.Id
        Start-Sleep -Seconds $remainingSeconds
    }
    $windowEndQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
    foreach ($spec in $specs) {
        $process = $spec.process
        if (-not $process.WaitForExit(($Seconds + $ClientCount * 2 + 40) * 1000)) {
            $process.Kill($true)
            throw "$($spec.name) exceeded its timeout."
        }
        $process.StandardOutput.ReadToEnd() | Set-Content -LiteralPath (Join-Path $session ($spec.name + '.stdout.txt'))
        $process.StandardError.ReadToEnd() | Set-Content -LiteralPath (Join-Path $session ($spec.name + '.stderr.txt'))
    }
    if ($restarted) {
        if (-not $restarted.process.WaitForExit(($Seconds + $ClientCount * 2 + 40) * 1000)) {
            $restarted.process.Kill($true)
            throw "$($restarted.name) restart exceeded its timeout."
        }
        $restarted.process.StandardOutput.ReadToEnd() |
            Set-Content -LiteralPath (Join-Path $session ($restarted.name + '.restart.stdout.txt'))
        $restarted.process.StandardError.ReadToEnd() |
            Set-Content -LiteralPath (Join-Path $session ($restarted.name + '.restart.stderr.txt'))
    }
    if (-not $statusProcess.WaitForExit(45000)) {
        throw 'vasio-status did not finish writing its history within 45 seconds.'
    }
    if ($statusProcess.ExitCode -ne 0) {
        throw "vasio-status exited with code $($statusProcess.ExitCode); see status.stderr.log."
    }
    if ($Reconfigure) {
        $summary = [ordered]@{ mode = 'reconfig'; clientCount = $ClientCount;
            seconds = $Seconds; seed = $Seed;
            originalFormat = @{ sampleRate = $temporary.sampleRate; bufferFrames = $temporary.bufferFrames };
            targetRate = $chosenRate; targetBufferFrames = $chosenBuffer;
            rateChangeQpc = $rateChangeQpc; bufferChangeQpc = $bufferChangeQpc;
            rateEngineState = $rateSnapshot; bufferEngineState = $bufferSnapshot;
            windowStartQpc = $windowStartQpc; windowEndQpc = $windowEndQpc;
            probeSha256 = $probeHash; statusHistory = Join-Path $session 'status.jsonl';
            clients = @(); verdict = 'inconclusive' }
        $allPass = $rateSnapshot.state -eq 'running' -and
            $rateSnapshot.sampleRate -eq $chosenRate -and
            $bufferSnapshot.state -eq 'running' -and
            $bufferSnapshot.sampleRate -eq $chosenRate -and
            $bufferSnapshot.bufferFrames -eq $chosenBuffer
        foreach ($spec in $specs) {
            $report = Get-Content -LiteralPath $spec.report -Raw | ConvertFrom-Json
            $events = @(Get-Content -LiteralPath ($spec.report + '.events.jsonl') |
                ForEach-Object { $_ | ConvertFrom-Json } | Where-Object {
                    $_.qpc -ge $windowStartQpc -and $_.qpc -le $windowEndQpc })
            $beforeRate = @($events | Where-Object { $_.kind -eq 1 -and $_.qpc -lt $rateChangeQpc })
            $afterRate = @($events | Where-Object {
                $_.kind -eq 1 -and $_.qpc -ge $rateChangeQpc -and $_.qpc -lt $bufferChangeQpc })
            $afterBuffer = @($events | Where-Object { $_.kind -eq 1 -and $_.qpc -ge $bufferChangeQpc })
            $rateSignal = Get-ReconfigSignalWindow -Events $events -Role $spec.role `
                -ChangeQpc $rateChangeQpc -EndQpc $bufferChangeQpc `
                -GraceTicks $graceTicks -MinCallbacks $limits.minPostRestartCallbacks
            $bufferSignal = Get-ReconfigSignalWindow -Events $events -Role $spec.role `
                -ChangeQpc $bufferChangeQpc -EndQpc $windowEndQpc `
                -GraceTicks $graceTicks -MinCallbacks $limits.minPostRestartCallbacks
            $rateResets = @($events | Where-Object {
                $_.kind -eq 2 -and $_.value -eq 3 -and
                $_.qpc -ge $rateChangeQpc -and $_.qpc -lt $bufferChangeQpc })
            $bufferResets = @($events | Where-Object {
                $_.kind -eq 2 -and $_.value -eq 3 -and $_.qpc -ge $bufferChangeQpc })
            $rateNotifications = @($events | Where-Object { $_.kind -eq 3 -and $_.qpc -ge $rateChangeQpc })
            $completedRateRestarts = @($report.hostRestarts | Where-Object {
                $_.resumedQpc -and $_.requestQpc -ge $rateChangeQpc -and
                $_.requestQpc -lt $bufferChangeQpc -and
                $_.sampleRate -eq $chosenRate -and $_.bufferFrames -eq $temporary.bufferFrames })
            $completedBufferRestarts = @($report.hostRestarts | Where-Object {
                $_.resumedQpc -and $_.requestQpc -ge $bufferChangeQpc -and
                $_.sampleRate -eq $chosenRate -and $_.bufferFrames -eq $chosenBuffer })
            $pass = $beforeRate.Count -ge $limits.minPostRestartCallbacks -and
                $rateSignal.pass -and $bufferSignal.pass -and $rateResets.Count -gt 0 -and
                $bufferResets.Count -gt 0 -and
                $completedRateRestarts.Count -gt 0 -and $completedBufferRestarts.Count -gt 0 -and
                $report.finalSampleRate -eq $chosenRate -and
                $report.finalBufferFrames -eq $chosenBuffer -and
                $report.droppedCallbackEvents -eq 0 -and
                $report.stopped -and $report.disposed -and $report.closed
            if (-not $pass) { $allPass = $false }
            $summary.clients += [ordered]@{ name = $spec.name; lane = $spec.lane;
                role = $spec.role; callbacksBeforeRate = $beforeRate.Count;
                callbacksAfterRate = $afterRate.Count; callbacksAfterBuffer = $afterBuffer.Count;
                rateSignal = $rateSignal; bufferSignal = $bufferSignal;
                rateResetRequests = $rateResets.Count; bufferResetRequests = $bufferResets.Count;
                completedRateRestarts = $completedRateRestarts.Count;
                completedBufferRestarts = $completedBufferRestarts.Count;
                finalSampleRate = $report.finalSampleRate;
                finalBufferFrames = $report.finalBufferFrames;
                sampleRateNotifications = $rateNotifications.Count;
                rawVerdict = $report.verdict; verdict = if ($pass) { 'pass' } else { 'fail' } }
        }
        $summary.statusSamples = if (Test-Path -LiteralPath $summary.statusHistory) {
            @(Get-Content -LiteralPath $summary.statusHistory).Count } else { 0 }
        if (-not $summary.statusSamples) { $allPass = $false }
        $summary.verdict = if ($allPass) { 'pass' } else { 'fail' }
        $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $session 'summary.json') -Encoding utf8
        Write-Output "Results: $session"
        $summary | ConvertTo-Json -Compress -Depth 5 | Write-Output
        $runExit = if ($allPass) { 0 } else { 6 }
    } else {
    $summary = [ordered]@{ mode = 'multiclient'; clientCount = $ClientCount;
        seconds = $Seconds; seed = $Seed; launchOrder = @($launchOrder | ForEach-Object name);
        killedClient = $victim.name; killedAtQpc = $killQpc;
        restartedClient = $restarted.name; restartedAtQpc = $restartQpc;
        windowStartQpc = $windowStartQpc; windowEndQpc = $windowEndQpc;
        probeSha256 = $probeHash;
        statusHistory = Join-Path $session 'status.jsonl'; clients = @(); verdict = 'inconclusive' }
    $limits = (Get-Content -LiteralPath $thresholds -Raw | ConvertFrom-Json).multiclient
    $minimumCoverage = $limits.minPostKillCoverage
    $stableRestartQpc = $restartQpc +
        [long]($limits.restartGraceSeconds * [System.Diagnostics.Stopwatch]::Frequency)
    $allPass = $true
    $restartReport = Get-Content -LiteralPath $restarted.report -Raw | ConvertFrom-Json
    $restartWindow = Get-MulticlientEventWindow -Path ($restarted.report + '.events.jsonl') `
        -StartQpc $restartQpc -KillQpc $restartQpc -StableQpc $stableRestartQpc `
        -EndQpc $windowEndQpc -TheoreticalMs ($restartReport.bufferFrames * 1000.0 / $restartReport.sampleRate) `
        -ExpectedFrames $restartReport.bufferFrames -QpcFrequency $restartReport.qpcFrequency
    $restartErrors = if ($restarted.role -eq 'receive') {
        $restartWindow.stableErrors } else { 0 }
    $restartPass = $restartWindow.stableCount -ge $limits.minPostRestartCallbacks -and
        $restartErrors -eq 0 -and
        $restartReport.droppedCallbackEvents -eq 0 -and
        $restartReport.stopped -and $restartReport.disposed -and $restartReport.closed
    if (-not $restartPass) { $allPass = $false }
    $summary.restart = [ordered]@{ pid = $restarted.process.Id;
        report = $restarted.report; callbacks = $restartWindow.afterCount;
        callbacksAfterGrace = $restartWindow.stableCount; errorsAfterGrace = $restartErrors;
        rawVerdict = $restartReport.verdict;
        verdict = if ($restartPass) { 'pass' } else { 'fail' } }
    foreach ($spec in $specs) {
        if ($spec.killed) {
            $summary.clients += [ordered]@{ name = $spec.name; lane = $spec.lane;
                role = $spec.role; killed = $true; verdict = 'terminated' }
            continue
        }
        $report = Get-Content -LiteralPath $spec.report -Raw | ConvertFrom-Json
        $theoreticalMs = $report.bufferFrames * 1000.0 / $report.sampleRate
        $window = Get-MulticlientEventWindow -Path ($spec.report + '.events.jsonl') `
            -StartQpc $windowStartQpc -KillQpc $killQpc -StableQpc $stableRestartQpc `
            -EndQpc $windowEndQpc -TheoreticalMs $theoreticalMs `
            -ExpectedFrames $report.bufferFrames -QpcFrequency $report.qpcFrequency
        $expected = [Math]::Max(0.0, ($windowEndQpc - $killQpc) /
            [double]$report.qpcFrequency) * $report.sampleRate / $report.bufferFrames
        $coverage = if ($expected -gt 0) { $window.afterCount / $expected } else { 0.0 }
        $unaffectedReceiver = $spec.role -eq 'receive' -and $spec.lane -ne $victim.lane
        $beforeErrors = $window.beforeErrors
        $afterErrors = if ($unaffectedReceiver) { $window.afterErrors } else { 0 }
        $afterRestartErrors = if ($spec.role -eq 'receive') {
            $window.stableErrors } else { 0 }
        $cadence = $window.cadence
        $pass = $window.beforeCount -ge 3 -and $coverage -ge $minimumCoverage -and
            $beforeErrors -eq 0 -and $afterErrors -eq 0 -and
            $window.stableCount -ge $limits.minPostRestartCallbacks -and
            $afterRestartErrors -eq 0 -and
            $report.droppedCallbackEvents -eq 0 -and
            $cadence.intervals -ge 3 -and
            $cadence.jitterP99Ms -le $theoreticalMs * $limits.maxJitterFraction -and
            $cadence.positionStepErrors -le $limits.maxPositionStepErrors
        if (-not $pass) { $allPass = $false }
        $summary.clients += [ordered]@{ name = $spec.name; lane = $spec.lane;
            role = $spec.role; killed = $false; callbacksBeforeKill = $window.beforeCount;
            callbacksAfterKill = $window.afterCount; expectedCallbacksAfterKill = [Math]::Round($expected, 1);
            callbacksAfterRestartGrace = $window.stableCount;
            coverageAfterKill = [Math]::Round($coverage, 3);
            errorsBeforeKill = $beforeErrors; errorsAfterKillOnUnaffectedReceiver = $afterErrors;
            errorsAfterRestartGrace = $afterRestartErrors;
            cadenceAfterKill = $cadence;
            rawVerdict = $report.verdict; verdict = if ($pass) { 'pass' } else { 'fail' } }
    }
    $summary.statusSamples = if (Test-Path -LiteralPath $summary.statusHistory) {
        @(Get-Content -LiteralPath $summary.statusHistory).Count } else { 0 }
    if (-not $summary.statusSamples) { $allPass = $false }
    $summary.verdict = if ($allPass) { 'pass' } else { 'fail' }
    $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $session 'summary.json') -Encoding utf8
    Write-Output "Results: $session"
    $summary | ConvertTo-Json -Compress -Depth 5 | Write-Output
    $runExit = if ($allPass) { 0 } else { 6 }
    }
}
finally {
    if ($restarted -and $restarted.process) {
        if (-not $restarted.process.HasExited) {
            $restarted.process.Kill($true)
            $restarted.process.WaitForExit()
        }
        $restarted.process.Dispose()
    }
    foreach ($spec in $specs) {
        if ($spec.process) {
            if (-not $spec.process.HasExited) { $spec.process.Kill($true); $spec.process.WaitForExit() }
            $spec.process.Dispose()
        }
    }
    if ($statusProcess) {
        if (-not $statusProcess.HasExited) { $statusProcess.Kill(); [void]$statusProcess.WaitForExit(5000) }
        $statusProcess.Dispose()
    }
    $restoreErrors = [System.Collections.Generic.List[string]]::new()
    if ($configurationTouched) {
        try { Restore-Configuration $originalConfiguration }
        catch { $restoreErrors.Add("configuration: $($_.Exception.Message)") }
    }
    if ($profilesTouched) {
        try {
            $body = @{ profiles = $profiles } | ConvertTo-Json -Depth 10
            [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
        } catch { $restoreErrors.Add("profiles: $($_.Exception.Message)") }
    }
    if ($restoreErrors.Count) { throw "Automatic restore failed; use $session\restore.json. $($restoreErrors -join '; ')" }
}
exit $runExit
