param(
    [string]$BaseUri = 'http://127.0.0.1:52525',
    [ValidateRange(3, 86400)][int]$Seconds = 12,
    [ValidateRange(1, 256)][int]$Channel = 1,
    [ValidateSet('receiver-first', 'sender-first')][string]$Order = 'receiver-first',
    [switch]$AllowOtherClients,
    [switch]$PlanOnly,
    [switch]$Self,
    [ValidateSet('none', 'send', 'receive')][string]$KillRole = 'none',
    [ValidateRange(1, 86399)][int]$KillAfterSeconds = 3,
    [string]$ProbePath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-probe.exe'),
    [string]$StatusPath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-status.exe'),
    [string]$ThresholdsPath = (Join-Path $PSScriptRoot '..\tools\vasio-probe\thresholds.json'),
    [string]$SessionPath = ''
)

$ErrorActionPreference = 'Stop'
$generatedSession = Join-Path $PSScriptRoot ('results\loopback-session-' +
    [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' +
    [Guid]::NewGuid().ToString('N').Substring(0, 8))
$session = if ($SessionPath) { $SessionPath } else { $generatedSession }
if (Test-Path -LiteralPath $session) { throw "Result directory already exists: $session" }
$probe = (Resolve-Path -LiteralPath $ProbePath -ErrorAction Stop).Path
$statusPathResolved = (Resolve-Path -LiteralPath $StatusPath -ErrorAction Stop).Path
$thresholds = (Resolve-Path -LiteralPath $ThresholdsPath -ErrorAction Stop).Path
if ($KillRole -ne 'none' -and $KillAfterSeconds -ge $Seconds - 2) {
    throw 'KillAfterSeconds must leave at least two seconds for the surviving client.'
}
if ($Self -and $KillRole -ne 'none') { throw '-Self cannot be combined with -KillRole.' }
if ($AllowOtherClients -and ($Self -or $KillRole -ne 'none')) {
    throw '-AllowOtherClients is only for the normal two-process sender/receiver check.'
}

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
    }
    finally { $deadline.Dispose(); $stream.Dispose() }
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
    }
    finally { $socket.Dispose() }
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

function Start-Probe([string]$File, [string]$Role, [int]$Duration) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $File
    $start.WorkingDirectory = (Split-Path -Parent $File)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('loopback', '--role', $Role, '--driver', 'TimoxVasio',
        '--output', (Join-Path $session "$Role.json"), '--thresholds', $thresholds,
        '--seconds', $Duration.ToString(), '--channel', ($Channel - 1).ToString())) {
        [void]$start.ArgumentList.Add($argument)
    }
    return [System.Diagnostics.Process]::Start($start)
}

function Wait-Client([string]$ProcessName) {
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $state = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
        $clients = @($state.virtualDrivers | ForEach-Object { $_.clients } | Where-Object { $null -ne $_ })
        if (@($clients | Where-Object { $_.processName -ieq $ProcessName }).Count) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Client $ProcessName did not attach within 5 seconds."
}

$state = $null
$activeClients = @()
$probeNames = if ($Self) { @('vasio-probe-self.exe') } else {
    @('vasio-probe-send.exe', 'vasio-probe-receive.exe') }
$clientPolls = if ($AllowOtherClients) { 1 } else { 50 }
for ($attempt = 0; $attempt -lt $clientPolls; ++$attempt) {
    $state = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
    $activeClients = @($state.virtualDrivers | ForEach-Object { $_.clients } | Where-Object { $null -ne $_ })
    if (-not $activeClients.Count) { break }
    Start-Sleep -Milliseconds 100
}
$existingProbes = @($activeClients | Where-Object {
    $_.processName -match '^vasio-probe-(send|receive|self)\.exe$'
})
if ($existingProbes.Count) {
    throw "A loopback probe is already attached: $(@($existingProbes | ForEach-Object processName) -join ', ')."
}
if ($activeClients.Count -and -not $AllowOtherClients) {
    throw "Close other virtual ASIO clients before the loopback route test: $(@($activeClients | ForEach-Object processName) -join ', ')."
}
if (-not $state.engine.physicalDriverId -or -not $state.engine.sampleRate -or -not $state.engine.bufferFrames) {
    throw 'Select a physical ASIO driver, rate and buffer in Control before this test.'
}
$profiles = @((Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/application-profiles" -TimeoutSec 5).profiles)
$originalConfiguration = @{
    physicalDriverId = $state.engine.physicalDriverId
    sampleRate = $state.engine.sampleRate
    bufferFrames = $state.engine.bufferFrames
    routes = @($state.configuredRoutes)
}
$newProfiles = @($profiles)
foreach ($name in $probeNames) {
    $existingProfile = @($newProfiles | Where-Object { $_.processName -ieq $name })
    if ($existingProfile.Count -gt 1 -or ($existingProfile.Count -eq 1 -and
        ($existingProfile[0].inputChannels -lt $Channel -or
         $existingProfile[0].outputChannels -lt $Channel))) {
        throw "The application profile for $name cannot expose channel $Channel."
    }
    if (-not $existingProfile.Count) {
        $newProfiles += @{ processName = $name; inputChannels = $Channel; outputChannels = $Channel }
    }
}
$sourceName = if ($Self) { 'vasio-probe-self.exe' } else { 'vasio-probe-send.exe' }
$destinationName = if ($Self) { 'vasio-probe-self.exe' } else { 'vasio-probe-receive.exe' }
$route = @{
    id = 'vasio-probe-loopback-' + [Guid]::NewGuid().ToString('N')
    sourceEndpointId = "virtual:TimoxVasio:app:$($sourceName):output:$Channel"
    destinationEndpointId = "virtual:TimoxVasio:app:$($destinationName):input:$Channel"
    gainDb = 0.0
    mute = $false
}
if ($AllowOtherClients -and @($originalConfiguration.routes | Where-Object {
    $_.sourceEndpointId -eq $route.sourceEndpointId -or
    $_.destinationEndpointId -eq $route.destinationEndpointId
}).Count) {
    throw 'The chosen probe channel is already routed; choose a free channel for the concurrent host check.'
}
$clockRoute = $null
if ($KillRole -ne 'none') {
    $physical = @($state.physicalDrivers | Where-Object id -eq $originalConfiguration.physicalDriverId)
    if ($physical.Count -ne 1 -or
        ($KillRole -eq 'send' -and -not @($physical[0].inputEndpoints).Count) -or
        ($KillRole -eq 'receive' -and -not @($physical[0].outputEndpoints).Count)) {
        throw 'The physical endpoint needed to keep the test clock running is unavailable.'
    }
    $clockRoute = @{
        id = 'vasio-probe-clock-' + [Guid]::NewGuid().ToString('N')
        sourceEndpointId = if ($KillRole -eq 'send') { $physical[0].inputEndpoints[0].id } else { $route.sourceEndpointId }
        destinationEndpointId = if ($KillRole -eq 'send') { $route.destinationEndpointId } else { $physical[0].outputEndpoints[0].id }
        gainDb = 0.0
        mute = $true
    }
}
$temporary = @{
    physicalDriverId = $originalConfiguration.physicalDriverId
    sampleRate = $originalConfiguration.sampleRate
    bufferFrames = $originalConfiguration.bufferFrames
    routes = @($originalConfiguration.routes) + @($route) + @($clockRoute | Where-Object { $_ })
}
if ($PlanOnly) {
    [ordered]@{
        canRun = $true
        allowOtherClients = [bool]$AllowOtherClients
        activeClients = @($activeClients | ForEach-Object {
            [ordered]@{ pid = $_.pid; processName = $_.processName }
        })
        sampleRate = $originalConfiguration.sampleRate
        bufferFrames = $originalConfiguration.bufferFrames
        existingRouteCount = @($originalConfiguration.routes).Count
        temporaryRoute = $route
    } | ConvertTo-Json -Depth 8
    exit 0
}
New-Item -ItemType Directory -Path $session -Force | Out-Null
@{ configuration = $originalConfiguration; profiles = $profiles } |
    ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $session 'restore.json') -Encoding utf8
$senderFile = Join-Path $session 'vasio-probe-send.exe'
$receiverFile = Join-Path $session 'vasio-probe-receive.exe'
$selfFile = Join-Path $session 'vasio-probe-self.exe'
if ($Self) { Copy-Item -LiteralPath $probe -Destination $selfFile }
else {
    Copy-Item -LiteralPath $probe -Destination $senderFile
    Copy-Item -LiteralPath $probe -Destination $receiverFile
}
$sourceHash = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash
$copies = if ($Self) { @($selfFile) } else { @($senderFile, $receiverFile) }
if (@($copies | Where-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash -ne $sourceHash }).Count) {
    throw 'Probe copy hash mismatch.'
}
$profilesTouched = $false
$configurationTouched = $false
$sender = $null
$receiver = $null
$selfProcess = $null
$statusProcess = $null
$runExit = 1
try {
    $body = @{ profiles = $newProfiles } | ConvertTo-Json -Depth 10
    $profilesTouched = $true
    [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
    $configurationTouched = $true
    Invoke-ConfigurationApply $temporary
    $statusProcess = Start-Process -FilePath $statusPathResolved -ArgumentList @(
        '--json', '--seconds', $Seconds.ToString(), '--history',
        (Join-Path $session 'status.jsonl'), '--api-port', ([Uri]$BaseUri).Port.ToString()
    ) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'status.stdout.jsonl') -RedirectStandardError (Join-Path $session 'status.stderr.log')
    if ($Self) {
        $selfProcess = Start-Probe $selfFile 'both' $Seconds
        Wait-Client 'vasio-probe-self.exe'
    } elseif ($Order -eq 'receiver-first') {
        $receiver = Start-Probe $receiverFile 'receive' ($Seconds - 2)
        Wait-Client 'vasio-probe-receive.exe'
        Start-Sleep -Milliseconds 500
        $sender = Start-Probe $senderFile 'send' $Seconds
    } else {
        $sender = Start-Probe $senderFile 'send' $Seconds
        Wait-Client 'vasio-probe-send.exe'
        Start-Sleep -Milliseconds 500
        $receiver = Start-Probe $receiverFile 'receive' ($Seconds - 2)
    }
    $killQpc = $null
    if ($KillRole -ne 'none') {
        Wait-Client 'vasio-probe-send.exe'
        Wait-Client 'vasio-probe-receive.exe'
        Start-Sleep -Seconds $KillAfterSeconds
        $victim = if ($KillRole -eq 'send') { $sender } else { $receiver }
        if ($victim.HasExited) { throw "$KillRole exited before the planned abrupt stop." }
        $killQpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
        $victim.Kill($true)
        [void]$victim.WaitForExit(5000)
    }
    $processes = if ($Self) { @(@{ process = $selfProcess; role = 'both' }) } else {
        @(@{ process = $sender; role = 'send' }, @{ process = $receiver; role = 'receive' }) }
    foreach ($item in $processes) {
        $process = $item.process
        if (-not $process.WaitForExit(($Seconds + 20) * 1000)) {
            $process.Kill($true)
            throw "$($item.role) probe exceeded its timeout."
        }
        $process.StandardOutput.ReadToEnd() | Set-Content -LiteralPath (Join-Path $session "$($item.role).stdout.txt")
        $process.StandardError.ReadToEnd() | Set-Content -LiteralPath (Join-Path $session "$($item.role).stderr.txt")
    }
    $sendReport = if (Test-Path -LiteralPath (Join-Path $session 'send.json')) {
        Get-Content -LiteralPath (Join-Path $session 'send.json') -Raw | ConvertFrom-Json } else { $null }
    $receiveReport = if (Test-Path -LiteralPath (Join-Path $session 'receive.json')) {
        Get-Content -LiteralPath (Join-Path $session 'receive.json') -Raw | ConvertFrom-Json } else { $null }
    $selfReport = if ($Self) {
        Get-Content -LiteralPath (Join-Path $session 'both.json') -Raw | ConvertFrom-Json } else { $null }
    $selfLatency = $null
    if ($Self) {
        $latencyCount = 0
        $latencyMin = [long]::MaxValue
        $latencyMax = [long]::MinValue
        $reader = [System.IO.StreamReader]::new((Join-Path $session 'both.json.events.jsonl'))
        try {
            while (($line = $reader.ReadLine()) -ne $null) {
                $event = $line | ConvertFrom-Json
                if ($event.kind -ne 1 -or -not $event.positionValid) { continue }
                $samples = ([long]$event.samplePosition - [long]$event.counter) -band 1048575
                $latencyMin = [Math]::Min($latencyMin, $samples)
                $latencyMax = [Math]::Max($latencyMax, $samples)
                ++$latencyCount
            }
        } finally { $reader.Dispose() }
        $selfLatency = [ordered]@{ matchedBlocks = $latencyCount;
            latencyMinSamples = if ($latencyCount) { $latencyMin } else { 0 };
            latencyMaxSamples = if ($latencyCount) { $latencyMax } else { 0 };
            latencyStable = $latencyCount -gt 0 -and $latencyMin -eq $latencyMax }
    }
    $latency = if (-not $Self -and $KillRole -eq 'none') {
        & (Join-Path $PSScriptRoot 'analyze_loopback.ps1') -SendEvents (Join-Path $session 'send.json.events.jsonl') -ReceiveEvents (Join-Path $session 'receive.json.events.jsonl') -ThresholdsPath $thresholds
    } else { $null }
    if (-not $statusProcess.WaitForExit(5000)) {
        throw 'vasio-status did not finish writing its history within five seconds.'
    }
    if ($statusProcess.ExitCode -ne 0) {
        throw "vasio-status exited with code $($statusProcess.ExitCode); see status.stderr.log."
    }
    $statusHistory = Join-Path $session 'status.jsonl'
    $statusSamples = if (Test-Path -LiteralPath $statusHistory) {
        @(Get-Content -LiteralPath $statusHistory).Count } else { 0 }
    $summary = [ordered]@{
        mode = 'loopback'
        self = [bool]$Self
        order = if ($Self) { $null } else { $Order }
        killedRole = $KillRole
        killedAtQpc = $killQpc
        driver = 'TimoxVasio'
        channel = $Channel
        probeSha256 = $sourceHash
        statusHistory = $statusHistory
        statusSamples = $statusSamples
        route = $route
        clockRoute = $clockRoute
        sendVerdict = if ($Self) { $null } elseif ($sendReport) { $sendReport.verdict } else { 'terminated' }
        receiveVerdict = if ($Self) { $null } elseif ($receiveReport) { $receiveReport.verdict } else { 'terminated' }
        selfVerdict = if ($selfReport) { $selfReport.verdict } else { $null }
        corruptedSamples = if ($Self) { $selfReport.corruptedSamples } elseif ($receiveReport) { $receiveReport.corruptedSamples } else { $null }
        missingSamples = if ($Self) { $selfReport.missingSamples } elseif ($receiveReport) { $receiveReport.missingSamples } else { $null }
        duplicatedSamples = if ($Self) { $selfReport.duplicatedSamples } elseif ($receiveReport) { $receiveReport.duplicatedSamples } else { $null }
        latency = if ($Self) { $selfLatency } else { $latency }
    }
    if ($KillRole -ne 'none') {
        $survivorRole = if ($KillRole -eq 'send') { 'receive' } else { 'send' }
        $eventsPath = Join-Path $session "$survivorRole.json.events.jsonl"
        $events = @(Get-Content -LiteralPath $eventsPath | ForEach-Object { $_ | ConvertFrom-Json } |
            Where-Object kind -eq 1)
        $before = @($events | Where-Object { $_.qpc -lt $killQpc })
        $after = @($events | Where-Object { $_.qpc -gt $killQpc })
        $preKillErrors = [ordered]@{
            corrupted = [long](($before | Measure-Object corrupted -Sum).Sum)
            missing = [long](($before | Measure-Object missing -Sum).Sum)
            duplicated = [long](($before | Measure-Object duplicated -Sum).Sum)
        }
        $survivorReport = if ($survivorRole -eq 'send') { $sendReport } else { $receiveReport }
        $postKillSeconds = if ($survivorReport) {
            [Math]::Max(0.0, ($survivorReport.runEndedQpc - $killQpc) / [double]$survivorReport.qpcFrequency)
        } else { 0.0 }
        $expectedPostKillCallbacks = if ($survivorReport -and $survivorReport.bufferFrames -gt 0) {
            $postKillSeconds * $survivorReport.sampleRate / $survivorReport.bufferFrames
        } else { 0.0 }
        $postKillCoverage = if ($expectedPostKillCallbacks -gt 0) {
            $after.Count / $expectedPostKillCallbacks
        } else { 0.0 }
        $summary.survivor = [ordered]@{ role = $survivorRole;
            callbacksBeforeKill = $before.Count; callbacksAfterKill = $after.Count;
            expectedCallbacksAfterKill = [Math]::Round($expectedPostKillCallbacks, 1);
            callbackCoverageAfterKill = [Math]::Round($postKillCoverage, 3);
            preKillErrors = $preKillErrors }
        $limits = (Get-Content -LiteralPath $thresholds -Raw | ConvertFrom-Json).loopback
        $runExit = if ($before.Count -ge $limits.minCallbacks -and
            $after.Count -ge $limits.minPostKillCallbacks -and
            $postKillCoverage -ge $limits.minPostKillCoverage -and
            $preKillErrors.corrupted -eq 0 -and $preKillErrors.missing -eq 0 -and
            $preKillErrors.duplicated -eq 0) { 0 } else { 6 }
    }
    if ($Self) { $runExit = if ($selfReport.verdict -eq 'pass' -and $selfLatency.latencyStable) { 0 } else { 6 } }
    if ($statusSamples -eq 0) { $runExit = 6 }
    if (-not $Self -and $KillRole -eq 'none' -and $statusSamples -gt 0 -and
        $sendReport.verdict -eq 'pass' -and $receiveReport.verdict -eq 'pass' -and
        $latency.unmatchedBlocks -eq 0 -and $latency.latencyStable) { $runExit = 0 }
    $summary.verdict = if ($runExit -eq 0) { 'pass' } else { 'fail' }
    $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $session 'summary.json') -Encoding utf8
    Write-Output "Results: $session"
    $summary | ConvertTo-Json -Compress -Depth 5 | Write-Output
}
finally {
    foreach ($process in @($sender, $receiver, $selfProcess)) {
        if ($null -ne $process) {
            if (-not $process.HasExited) { $process.Kill($true); $process.WaitForExit() }
            $process.Dispose()
        }
    }
    if ($statusProcess) {
        if (-not $statusProcess.HasExited) {
            $statusProcess.Kill()
            [void]$statusProcess.WaitForExit(5000)
        }
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
    if ($restoreErrors.Count) {
        throw "Automatic restore failed; use $session\restore.json. $($restoreErrors -join '; ')"
    }
}
exit $runExit
