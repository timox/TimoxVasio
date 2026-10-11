param(
    [string]$BaseUri = 'http://127.0.0.1:52525',
    [string]$ProbePath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-probe.exe'),
    [string]$ThresholdsPath = (Join-Path $PSScriptRoot '..\tools\vasio-probe\thresholds.json'),
    [string]$DoctorPath = (Join-Path $PSScriptRoot '..\tools\vasio-doctor\vasio-doctor.ps1'),
    [string]$StatusPath = (Join-Path $PSScriptRoot '..\build_vasio_probe\Release\vasio-status.exe'),
    [string]$Driver = 'TimoxVasio',
    [string]$ResultsRoot = (Join-Path $PSScriptRoot 'results'),
    [ValidateRange(1, 1000000)][int]$LifecycleCycles = 1000,
    [ValidateRange(0, 86400)][int]$CadenceSeconds = 0,
    [ValidateRange(0, 86400)][int]$LoopbackSeconds = 0,
    [ValidateRange(0, 86400)][int]$MulticlientSeconds = 0,
    [ValidateRange(0, 86400)][int]$ReconfigSeconds = 0,
    [switch]$Sweep,
    [switch]$PlanOnly,
    [string[]]$LongFormats = @(),
    [string]$RestoreSnapshot
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$snapshotId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' +
    [Guid]::NewGuid().ToString('N').Substring(0, 8)
$snapshotDir = Join-Path $PSScriptRoot ('results\route-session-' + $snapshotId)

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

if ($RestoreSnapshot) {
    $saved = Get-Content -LiteralPath $RestoreSnapshot -Raw | ConvertFrom-Json
    $currentProfiles = @( (Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/application-profiles" -TimeoutSec 5).profiles )
    if (-not @($currentProfiles | Where-Object { $_.processName -ieq 'vasio-probe.exe' }).Count) {
        $currentProfiles += @{ processName = 'vasio-probe.exe'; inputChannels = 1; outputChannels = 1 }
        $body = @{ profiles = $currentProfiles } | ConvertTo-Json -Depth 10
        [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
    }
    Restore-Configuration $saved.configuration
    $body = @{ profiles = @($saved.profiles) } | ConvertTo-Json -Depth 10
    [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
    Write-Output "Restored configuration and profiles from $RestoreSnapshot"
    exit 0
}

$state = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
$activeClients = @($state.virtualDrivers | ForEach-Object { $_.clients } | Where-Object { $null -ne $_ })
if (-not $state.engine.physicalDriverId -or -not $state.engine.sampleRate -or -not $state.engine.bufferFrames) {
    throw 'Select a physical ASIO driver, rate and buffer in Control before this test.'
}
$physical = $state.physicalDrivers | Where-Object { $_.id -eq $state.engine.physicalDriverId } | Select-Object -First 1
if (-not $physical -or @($physical.outputEndpoints).Count -eq 0) {
    throw 'The selected physical ASIO driver has no output channel for the muted test route.'
}
$sweepCombinations = @()
if ($Sweep -or $PlanOnly) {
    $sizes = $physical.bufferSizes
    foreach ($rate in @(44100, 48000, 96000)) {
        if ($rate -notin @($physical.sampleRates)) { continue }
        foreach ($frames in @(64, 128, 256, 512)) {
            if ($frames -lt $sizes.minimumFrames -or $frames -gt $sizes.maximumFrames) { continue }
            if ($sizes.granularity -eq -1 -and ($frames -band ($frames - 1)) -ne 0) { continue }
            if ($sizes.granularity -gt 0 -and
                (($frames - $sizes.minimumFrames) % $sizes.granularity) -ne 0) { continue }
            if ($sizes.granularity -eq 0 -and $frames -ne $sizes.preferredFrames) { continue }
            $key = "$rate/$frames"
            $sweepCombinations += [ordered]@{ sampleRate = $rate; bufferFrames = $frames;
                durationSeconds = if ($key -in $LongFormats) { 3600 } else { 300 }; key = $key }
        }
    }
    $knownFormats = @($sweepCombinations | ForEach-Object key)
    foreach ($key in $LongFormats) {
        if ($key -notin $knownFormats) { throw "Long format is unsupported: $key" }
    }
    if (-not $sweepCombinations.Count) { throw 'No requested frequency/buffer combination is declared supported.' }
}
if ($PlanOnly) {
    [ordered]@{ physicalDriver = $physical.name; activeClients = @($activeClients |
        ForEach-Object { [ordered]@{ pid = $_.pid; process = $_.processName } });
        executableNow = $activeClients.Count -eq 0; combinations = $sweepCombinations;
        clientCounts = @(1, 2, 4, 8);
        modeClientCounts = [ordered]@{ cadence = @(1); loopback = @(1, 2);
            multiclient = @(2, 4, 8); reconfig = @(2, 4, 8) };
        shortSeconds = 300; longSeconds = 3600 } |
        ConvertTo-Json -Depth 8
    exit 0
}
if ($activeClients.Count -gt 0) {
    $names = ($activeClients | ForEach-Object { "$($_.processName) PID $($_.pid)" }) -join ', '
    throw "Close ASIO clients before the temporary route test: $names"
}
$profilesResponse = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/application-profiles" -TimeoutSec 5
$originalProfiles = @($profilesResponse.profiles)
$originalConfiguration = @{
    physicalDriverId = $state.engine.physicalDriverId
    sampleRate = $state.engine.sampleRate
    bufferFrames = $state.engine.bufferFrames
    routes = @($state.configuredRoutes)
}
$newProfiles = @($originalProfiles)
if (-not @($newProfiles | Where-Object { $_.processName -ieq 'vasio-probe.exe' }).Count) {
    $newProfiles += @{ processName = 'vasio-probe.exe'; inputChannels = 1; outputChannels = 1 }
}
$route = @{
    id = 'vasio-probe-muted-' + [Guid]::NewGuid().ToString('N')
    sourceEndpointId = 'virtual:TimoxVasio:app:vasio-probe.exe:output:1'
    destinationEndpointId = $physical.outputEndpoints[0].id
    gainDb = 0.0
    mute = $true
}
$temporaryConfiguration = @{
    physicalDriverId = $originalConfiguration.physicalDriverId
    sampleRate = $originalConfiguration.sampleRate
    bufferFrames = $originalConfiguration.bufferFrames
    routes = @($originalConfiguration.routes) + @($route)
}
New-Item -ItemType Directory -Path $snapshotDir -Force | Out-Null
@{ configuration = $originalConfiguration; profiles = $originalProfiles } |
    ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $snapshotDir 'restore.json') -Encoding utf8

$profilesTouched = $false
$configurationTouched = $false
$runExit = 1
try {
    $body = @{ profiles = $newProfiles } | ConvertTo-Json -Depth 10
    $profilesTouched = $true
    [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
    $configurationTouched = $true
    Invoke-ConfigurationApply $temporaryConfiguration

    $runs = if ($Sweep) { @($sweepCombinations) } else {
        @([ordered]@{ sampleRate = $state.engine.sampleRate;
            bufferFrames = $state.engine.bufferFrames; durationSeconds = 0; key = 'current' })
    }
    $runExit = 0
    $sweepResults = [System.Collections.Generic.List[object]]::new()
    foreach ($run in $runs) {
        if ($Sweep) {
            $before = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
            $others = @($before.virtualDrivers | ForEach-Object { $_.clients } |
                Where-Object { $null -ne $_ })
            if ($others.Count) { throw "ASIO clients appeared before sweep format $($run.key); stopping." }
            $temporaryConfiguration.sampleRate = $run.sampleRate
            $temporaryConfiguration.bufferFrames = $run.bufferFrames
            Invoke-ConfigurationApply $temporaryConfiguration
            $applied = (Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5).engine
            if ($applied.sampleRate -ne $run.sampleRate -or
                $applied.bufferFrames -ne $run.bufferFrames) {
                throw "Sweep format $($run.key) was not confirmed by the engine."
            }
        }
        $stem = if ($Sweep) { "matrix-$($run.sampleRate)-$($run.bufferFrames)" } else { 'matrix' }
        $seconds = if ($Sweep) { [int]$run.durationSeconds } else { 0 }
        $start = [System.Diagnostics.ProcessStartInfo]::new()
        $start.FileName = (Get-Command pwsh -ErrorAction Stop).Source
        $start.WorkingDirectory = $repoRoot
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($argument in @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'run_matrix.ps1'),
            '-BaseUri', $BaseUri, '-ProbePath', $ProbePath,
            '-ThresholdsPath', $ThresholdsPath, '-DoctorPath', $DoctorPath,
            '-StatusPath', $StatusPath, '-Driver', $Driver,
            '-ResultsRoot', $ResultsRoot, '-LifecycleCycles', $LifecycleCycles.ToString(),
            '-CadenceSeconds', $(if ($Sweep) { $seconds } else { $CadenceSeconds }).ToString(),
            '-LoopbackSeconds', $(if ($Sweep) { $seconds } else { $LoopbackSeconds }).ToString(),
            '-MulticlientSeconds', $(if ($Sweep) { $seconds } else { $MulticlientSeconds }).ToString(),
            '-ReconfigSeconds', $(if ($Sweep) { 30 } else { $ReconfigSeconds }).ToString())) {
            [void]$start.ArgumentList.Add($argument)
        }
        $process = [System.Diagnostics.Process]::Start($start)
        try {
            $stdoutTask = $process.StandardOutput.ReadToEndAsync()
            $stderrTask = $process.StandardError.ReadToEndAsync()
            $process.WaitForExit()
            $stdout = $stdoutTask.GetAwaiter().GetResult()
            $stderr = $stderrTask.GetAwaiter().GetResult()
            if ($process.ExitCode -ne 0) { $runExit = $process.ExitCode }
            $stdout | Set-Content -LiteralPath (Join-Path $snapshotDir "$stem.stdout.txt") -Encoding utf8
            $stderr | Set-Content -LiteralPath (Join-Path $snapshotDir "$stem.stderr.txt") -Encoding utf8
            $resultPath = if ($stdout -match '(?m)^Results: (.+)$') { $Matches[1].Trim() } else { $null }
            $sweepResults.Add([ordered]@{ sampleRate = $run.sampleRate;
                bufferFrames = $run.bufferFrames; durationSeconds = $seconds;
                exitCode = $process.ExitCode; stdout = "$stem.stdout.txt";
                stderr = "$stem.stderr.txt"; matrixResults = $resultPath })
            if ($Sweep) {
                [ordered]@{ physicalDriver = $physical.name; planned = $sweepCombinations;
                    runs = $sweepResults.ToArray(); updatedUtc = [DateTime]::UtcNow.ToString('o') } |
                    ConvertTo-Json -Depth 8 |
                    Set-Content -LiteralPath (Join-Path $snapshotDir 'sweep-summary.json') -Encoding utf8
            }
            Write-Output $stdout
            if ($stderr) { Write-Warning $stderr }
        }
        finally { $process.Dispose() }
    }
    if ($Sweep) {
        [ordered]@{ physicalDriver = $physical.name; runs = $sweepResults.ToArray();
            completedUtc = [DateTime]::UtcNow.ToString('o') } |
            ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath (Join-Path $snapshotDir 'sweep-summary.json') -Encoding utf8
    }
}
finally {
    $restoreErrors = [System.Collections.Generic.List[string]]::new()
    if ($configurationTouched) {
        try { Restore-Configuration $originalConfiguration }
        catch { $restoreErrors.Add("configuration: $($_.Exception.Message)") }
    }
    if ($profilesTouched -and -not $restoreErrors.Count) {
        try {
            $body = @{ profiles = $originalProfiles } | ConvertTo-Json -Depth 10
            [void](Invoke-RestMethod -Method Put -Uri "$BaseUri/api/v1/application-profiles" -Body $body -ContentType 'application/json' -TimeoutSec 10)
        }
        catch { $restoreErrors.Add("profiles: $($_.Exception.Message)") }
    }
    if ($restoreErrors.Count) {
        throw "Automatic restore failed; use $snapshotDir\restore.json. $($restoreErrors -join '; ')"
    }
}
Write-Output "Original configuration restored from $snapshotDir"
exit $runExit
