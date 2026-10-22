#Requires -Version 7.0
param(
    [Parameter(Mandatory)][ValidateRange(1, 65535)][int]$ApiPort,
    [Parameter(Mandatory)][string]$DriverPath,
    [Parameter(Mandatory)][string]$ProbePath,
    [ValidateRange(1, 1198)][int]$Seconds = 5,
    [switch]$AllowExistingClients,
    [ValidateSet('AsioDirect', 'WasapiLoopback')][string]$RouteMode = 'AsioDirect',
    [string]$WasapiEndpointId,
    [switch]$RequireStableBridges,
    [ValidateRange(0, 1198)][int]$MinimumStableSeconds = 0,
    [string]$ResultsRoot = (Join-Path $PSScriptRoot 'results')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'wasapi-stability.ps1')
$baseUri = "http://127.0.0.1:$ApiPort"
$sessionId = [Guid]::NewGuid().ToString('N')
$resultDirectory = Join-Path $ResultsRoot "asio-engine-$sessionId"
[void](New-Item -ItemType Directory -Path $resultDirectory -Force)
$processes = [Collections.Generic.List[object]]::new()
$report = [ordered]@{ passed = $false; apiPort = $ApiPort; seconds = $Seconds; restore = 'not-needed'; error = $null }
$report.signalPresencePassed = $false
$report.stabilityPassed = if ($RequireStableBridges) { $false } else { $null }
$stabilityIssues = [Collections.Generic.List[object]]::new()
$stabilityBaseline = $null
$stabilityPrevious = $null
$stability = [ordered]@{ required = [bool]$RequireStableBridges; baselineElapsedSeconds = $null;
    lastElapsedSeconds = $null; observedSeconds = 0.0; samples = 0; maximumSampleGapSeconds = 0.0;
    baselineLastIssues = @();
    minimumRequiredSeconds = $(if ($MinimumStableSeconds) { $MinimumStableSeconds } else { [Math]::Max(1, $Seconds - 5) });
    issues = $stabilityIssues }
$report.stability = $stability
$original = $null
$ownedRevision = $null
$previousOwnedRevision = $null
$finalTransactionAttempted = $false
$routeId = "asio-signal-$sessionId"
$renderBridgeId = "signal_render_$sessionId"
$loopbackBridgeId = "signal_loopback_$sessionId"

function Get-State {
    Invoke-RestMethod -Uri "$baseUri/api/v1/state" -TimeoutSec 5
}
function Get-Configuration($State) {
    [ordered]@{
        physicalDriverId = $State.engine.physicalDriverId
        sampleRate = $State.engine.sampleRate
        bufferFrames = $State.engine.bufferFrames
        clockSource = $State.engine.clockSource
        routes = @($State.configuredRoutes)
        wasapiBridges = @($State.configuredWasapiBridges)
    }
}
function Invoke-Apply($Payload) {
    $socket = [Net.WebSockets.ClientWebSocket]::new()
    $socket.Options.AddSubProtocol('vasio.api.v1')
    $deadline = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(15))
    try {
        [void]$socket.ConnectAsync([Uri]"ws://127.0.0.1:$ApiPort/api/v1/ws", $deadline.Token).GetAwaiter().GetResult()
        $id = [Guid]::NewGuid().ToString('N')
        $request = @{ id = $id; command = 'configuration.apply'; payload = $Payload } | ConvertTo-Json -Depth 50 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($request)
        [void]$socket.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text,
            $true, $deadline.Token).GetAwaiter().GetResult()
        while (-not $deadline.IsCancellationRequested) {
            $stream = [IO.MemoryStream]::new()
            try {
                $buffer = [byte[]]::new(8192)
                do {
                    $received = $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $deadline.Token).GetAwaiter().GetResult()
                    if ($received.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Text) { throw 'API returned non-text or closed WebSocket.' }
                    $stream.Write($buffer, 0, $received.Count)
                    if ($stream.Length -gt 1048576) { throw 'API message exceeded 1 MiB.' }
                } while (-not $received.EndOfMessage)
                $message = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
                if (-not $message.PSObject.Properties['id'] -or $message.id -ne $id) { continue }
                if (-not $message.success -or -not $message.result.accepted) {
                    throw "configuration.apply rejected: $($message | ConvertTo-Json -Depth 10 -Compress)"
                }
                return
            } finally { $stream.Dispose() }
        }
        throw 'API apply deadline exceeded.'
    } finally { $socket.Dispose(); $deadline.Dispose() }
}
function Start-Probe([string]$Role, [int]$Duration) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    # Persistence identifies applications by executable name. Distinct copies
    # prevent an ambiguous durable route when both hosts use the same binary.
    $copy = Join-Path $resultDirectory "AsioSignal-$Role.exe"
    Copy-Item -LiteralPath $ProbePath -Destination $copy
    if ((Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $ProbePath).Hash) {
        throw 'Probe copy hash mismatch.'
    }
    $start.FileName = (Resolve-Path -LiteralPath $copy).Path
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('--dll', $DriverPath, '--role', $Role, '--seconds', "$Duration")) {
        [void]$start.ArgumentList.Add($argument)
    }
    if ($RouteMode -eq 'WasapiLoopback' -and $Role -eq 'receive') {
        [void]$start.ArgumentList.Add('--analysis'); [void]$start.ArgumentList.Add('bridge')
    }
    $process = [Diagnostics.Process]::Start($start)
    $entry = [pscustomobject]@{ role = $Role; process = $process; ready = $null;
        readyTask = $process.StandardOutput.ReadLineAsync(); outputTask = $null;
        errorTask = $process.StandardError.ReadToEndAsync() }
    $processes.Add($entry)
    return $entry
}
try {
    if ($RequireStableBridges -and ($RouteMode -ne 'WasapiLoopback' -or $Seconds -lt 10)) {
        throw '-RequireStableBridges requires WasapiLoopback and Seconds >= 10.'
    }
    if ($MinimumStableSeconds -and (-not $RequireStableBridges -or $MinimumStableSeconds -gt $Seconds)) {
        throw '-MinimumStableSeconds requires stability qualification and cannot exceed Seconds.'
    }
    $DriverPath = (Resolve-Path -LiteralPath $DriverPath).Path
    $ProbePath = (Resolve-Path -LiteralPath $ProbePath).Path
    $report.binaries = @($DriverPath, $ProbePath | ForEach-Object {
        $file = Get-Item -LiteralPath $_
        [ordered]@{ path = $file.FullName; lastWriteUtc = $file.LastWriteTimeUtc.ToString('o');
            sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    })
    # The host calls DllGetClassObject with this CLSID, then CreateInstance with
    # the ASIO interface ID. A foreign DLL is rejected rather than registered.
    $report.driverClsid = '{A4D39126-78CB-4D89-9E0A-54494D4F5856}'
    $report.routeMode = $RouteMode
    $report.qualificationScope = if ($RouteMode -eq 'WasapiLoopback') { 'tone-presence-only' } else { 'direct-signal-integrity' }
    if ($RequireStableBridges) { $report.qualificationScope = 'tone-presence-and-observed-bridge-stability'; $report.stabilityPassed = $false }
    if ($RouteMode -eq 'WasapiLoopback' -and [string]::IsNullOrWhiteSpace($WasapiEndpointId)) {
        throw '-WasapiEndpointId is required for WasapiLoopback; resolve the intended default multimedia endpoint explicitly before the test.'
    }
    $before = Get-State
    if ($before.engine.state -ne 'running' -or $before.engine.clockSource.kind -ne 'software' -or
        $before.engine.sampleRate -lt 8000 -or $before.engine.bufferFrames -lt 16) {
        throw 'Start the isolated engine with a software clock before running this test; stopped state is not changed by the probe.'
    }
    $clients = @($before.virtualDrivers | ForEach-Object { $_.clients } | Where-Object { $null -ne $_ })
    $report.allowExistingClients = [bool]$AllowExistingClients
    $report.preexistingClients = $clients
    $report.environment = if ($clients.Count) {
        'shared client environment, not isolated performance evidence'
    } else { 'no preexisting ASIO clients observed' }
    if ($clients.Count -and -not $AllowExistingClients) {
        throw 'This qualification requires an isolated engine without existing ASIO clients. Use -AllowExistingClients only for coordinated signal qualification.'
    }
    $original = Get-Configuration $before
    $before | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'before.json')
    $original | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'restore.json')
    $sender = Start-Probe 'send' ($Seconds + 2)
    $receiver = Start-Probe 'receive' $Seconds
    foreach ($entry in $processes) {
        if (-not $entry.readyTask.Wait(15000)) { throw "$($entry.role) ready deadline exceeded." }
        $entry.ready = $entry.readyTask.GetAwaiter().GetResult() | ConvertFrom-Json
        $entry.outputTask = $entry.process.StandardOutput.ReadToEndAsync()
        if (-not $entry.ready -or $entry.ready.event -ne 'ready' -or $entry.ready.pid -ne $entry.process.Id -or
            $entry.ready.sampleRate -ne $before.engine.sampleRate -or $entry.ready.frames -ne $before.engine.bufferFrames) {
            throw "$($entry.role) failed the ASIO readiness/format contract."
        }
    }
    $sourceId = "virtual:TimoxVasio:$($sender.process.Id):output:1"
    $destinationId = "virtual:TimoxVasio:$($receiver.process.Id):input:1"
    $discoveryDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $state = Get-State
        $outputs = @($state.virtualDrivers | ForEach-Object { $_.outputEndpoints })
        $inputs = @($state.virtualDrivers | ForEach-Object { $_.inputEndpoints })
        $source = @($outputs | Where-Object id -EQ $sourceId)
        $destination = @($inputs | Where-Object id -EQ $destinationId)
        if ($source.Count -eq 1 -and $destination.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $discoveryDeadline)
    if ($source.Count -ne 1 -or $destination.Count -ne 1) { throw 'Real client endpoints were not discovered after createBuffers.' }
    if ($state.configurationRevision -ne $before.configurationRevision) { throw 'Configuration changed before the test route could be applied.' }
    $temporary = Get-Configuration $before
    $nextRevision = [ulong]$before.configurationRevision
    if ($RouteMode -eq 'WasapiLoopback') {
        $temporary.wasapiBridges = @($original.wasapiBridges) + @(
            @{ id = $renderBridgeId; enabled = $true; kind = 'render'; endpointId = $WasapiEndpointId },
            @{ id = $loopbackBridgeId; enabled = $true; kind = 'endpointLoopback'; endpointId = $WasapiEndpointId })
        $temporary.expectedConfigurationRevision = $nextRevision
        $ownedRevision = $nextRevision + [ulong]1
        Invoke-Apply $temporary
        $nextRevision = $ownedRevision
        $bridgeDeadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $bridgeState = Get-State
            if ($bridgeState.configurationRevision -ne $ownedRevision) { throw 'Configuration changed during bridge discovery.' }
            $render = @($bridgeState.wasapiBridges | Where-Object id -EQ $renderBridgeId)
            $loopback = @($bridgeState.wasapiBridges | Where-Object id -EQ $loopbackBridgeId)
            if ($render.Count -eq 1 -and $loopback.Count -eq 1 -and
                @($render[0].outputEndpoints).Count -gt 0 -and @($loopback[0].inputEndpoints).Count -gt 0) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $bridgeDeadline)
        if ($render.Count -ne 1 -or $loopback.Count -ne 1 -or
            @($render[0].outputEndpoints).Count -eq 0 -or @($loopback[0].inputEndpoints).Count -eq 0) { throw 'WASAPI bridge ports were not discovered.' }
        if ($render[0].effectiveInfo.endpointId -ne $WasapiEndpointId -or $loopback[0].effectiveInfo.endpointId -ne $WasapiEndpointId) {
            throw 'WASAPI bridges did not open the requested endpoint.'
        }
        $bridgeState | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'bridges.json')
        $temporary.routes = @($original.routes) + @(
            @{ id = $routeId; sourceEndpointId = $source[0].id; destinationEndpointId = $render[0].outputEndpoints[0].id; gainDb = 0.0; mute = $false },
            @{ id = "$routeId-return"; sourceEndpointId = $loopback[0].inputEndpoints[0].id; destinationEndpointId = $destination[0].id; gainDb = 0.0; mute = $false })
    } else {
        $temporary.routes = @($original.routes) + @(@{ id = $routeId; sourceEndpointId = $source[0].id;
            destinationEndpointId = $destination[0].id; gainDb = 0.0; mute = $false })
    }
    $temporary.expectedConfigurationRevision = $nextRevision
    $previousOwnedRevision = $ownedRevision
    $ownedRevision = $nextRevision + [ulong]1
    $finalTransactionAttempted = $true
    Invoke-Apply $temporary
    $applied = Get-State
    if ($applied.configurationRevision -ne $ownedRevision -or
        -not @($applied.routes | Where-Object id -EQ $routeId).Count) { throw 'Temporary route was not confirmed by the engine.' }
    $applied | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'applied.json')
    foreach ($entry in @($sender, $receiver)) {
        $write = $entry.process.StandardInput.WriteLineAsync('start')
        if (-not $write.Wait(3000)) { throw 'ASIO start command deadline exceeded.' }
        [void]$write.GetAwaiter().GetResult()
        $entry.process.StandardInput.Close()
    }
    $runClock = [Diagnostics.Stopwatch]::StartNew()
    $nextPollSeconds = 0.0
    while (@($processes | Where-Object { -not $_.process.HasExited }).Count) {
        if ($runClock.Elapsed.TotalSeconds -gt $Seconds + 20) { throw 'Probe completion deadline exceeded.' }
        # Never count the sender's extra tail after the receiver has exited as
        # part of a bidirectional qualification interval.
        if ($RouteMode -eq 'WasapiLoopback' -and -not $sender.process.HasExited -and
            -not $receiver.process.HasExited -and $runClock.Elapsed.TotalSeconds -ge $nextPollSeconds) {
            try {
                $sampleState = Get-State
                $sampleSeconds = $runClock.Elapsed.TotalSeconds
                $bothStillRunning = -not $sender.process.HasExited -and -not $receiver.process.HasExited
                [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); elapsedSeconds = $sampleSeconds;
                    bothProbesRunning = $bothStillRunning; state = $sampleState } |
                    ConvertTo-Json -Depth 50 -Compress |
                    Add-Content -LiteralPath (Join-Path $resultDirectory 'stability-snapshots.jsonl')
                if ($RequireStableBridges -and $bothStillRunning -and $sampleSeconds -ge 2) {
                    $sampleGap = if ($null -eq $stabilityPrevious) { 1.0 } else { $sampleSeconds - $stability.lastElapsedSeconds }
                    $evaluation = Get-WasapiStabilitySample -Snapshot $sampleState -BridgeIds @($renderBridgeId, $loopbackBridgeId) -Baseline $stabilityBaseline -Previous $stabilityPrevious -GapSeconds $sampleGap
                    if ($null -eq $stabilityBaseline) {
                        $stability.baselineLastIssues = @($evaluation.issues)
                        if ($evaluation.ready) {
                            $stabilityBaseline = $evaluation
                            $stability.baselineElapsedSeconds = $sampleSeconds
                            $stability.lastElapsedSeconds = $sampleSeconds
                            $evaluation | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $resultDirectory 'stability-baseline.json')
                        }
                    } else {
                        $gap = $sampleSeconds - $stability.lastElapsedSeconds
                        $stability.maximumSampleGapSeconds = [Math]::Max($stability.maximumSampleGapSeconds, $gap)
                        $stability.lastElapsedSeconds = $sampleSeconds
                        $stability.observedSeconds = $sampleSeconds - $stability.baselineElapsedSeconds
                        foreach ($issue in $evaluation.issues) { $stabilityIssues.Add([pscustomobject]@{ elapsedSeconds = $sampleSeconds; issue = $issue }) }
                    }
                    if ($null -ne $stabilityBaseline) { $stability.samples++; $stabilityPrevious = $evaluation }
                }
            } catch {
                $stabilityIssues.Add([pscustomobject]@{ elapsedSeconds = $runClock.Elapsed.TotalSeconds;
                    issue = "Snapshot unavailable: $($_.Exception.Message)" })
            }
            $nextPollSeconds = $runClock.Elapsed.TotalSeconds + 1
        }
        Start-Sleep -Milliseconds 100
    }
    $report.actualRunWallSeconds = $runClock.Elapsed.TotalSeconds
    if ($RequireStableBridges) {
        if ($null -eq $stabilityBaseline) { $stabilityIssues.Add([pscustomobject]@{ issue = 'No eligible running baseline without priming timeout.' }) }
        if ($stability.observedSeconds -lt $stability.minimumRequiredSeconds) {
            $stabilityIssues.Add([pscustomobject]@{ issue = 'Observed stability interval shorter than required minimum.' })
        }
        $report.stabilityPassed = $stabilityIssues.Count -eq 0
    }
    foreach ($entry in $processes) {
        if (-not $entry.outputTask.Wait(3000) -or -not $entry.errorTask.Wait(3000)) { throw 'Probe output deadline exceeded.' }
        $output = $entry.outputTask.GetAwaiter().GetResult()
        $errors = $entry.errorTask.GetAwaiter().GetResult()
        $output | Set-Content -LiteralPath (Join-Path $resultDirectory "$($entry.role).stdout.jsonl")
        $errors | Set-Content -LiteralPath (Join-Path $resultDirectory "$($entry.role).stderr.txt")
        $result = @($output -split '\r?\n' | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object event -EQ 'result')
        if ($entry.process.ExitCode -ne 0 -or $result.Count -ne 1 -or -not $result[0].passed) {
            throw "$($entry.role) signal qualification failed (exit $($entry.process.ExitCode))."
        }
        $report[$entry.role] = $result[0]
    }
    $report.signalPresencePassed = $true
    $after = Get-State
    $after | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'after.json')
    if ($RouteMode -eq 'WasapiLoopback') {
        $finalRender = @($after.wasapiBridges | Where-Object id -EQ $renderBridgeId)
        $finalLoopback = @($after.wasapiBridges | Where-Object id -EQ $loopbackBridgeId)
        if ($finalRender.Count -ne 1 -or $finalLoopback.Count -ne 1 -or
            $finalRender[0].health.renderedFrames -le 0 -or $finalLoopback[0].health.capturedFrames -le 0 -or
            $finalRender[0].health.error -ne 0 -or $finalLoopback[0].health.error -ne 0) {
            throw 'WASAPI bridge counters did not confirm a successful render/capture path.'
        }
        $report.bridgeEvidence = @($finalRender[0], $finalLoopback[0])
    }
    if ($RequireStableBridges -and -not $report.stabilityPassed) { throw 'Bridge stability qualification failed; see stability issues and snapshots.' }
    $report.passed = $true
} catch {
    $report.error = $_.Exception.Message
} finally {
    # Only child process objects created above are eligible for termination.
    foreach ($entry in $processes) {
        try {
            if (-not $entry.process.HasExited) { $entry.process.Kill(); [void]$entry.process.WaitForExit(3000) }
            if ($entry.readyTask.Wait(3000)) {
                $entry.readyTask.GetAwaiter().GetResult() |
                    Set-Content -LiteralPath (Join-Path $resultDirectory "$($entry.role).ready.json")
                if ($null -eq $entry.outputTask) { $entry.outputTask = $entry.process.StandardOutput.ReadToEndAsync() }
                if ($entry.outputTask.Wait(3000)) {
                    $entry.outputTask.GetAwaiter().GetResult() |
                        Set-Content -LiteralPath (Join-Path $resultDirectory "$($entry.role).stdout.jsonl")
                }
            }
            if ($entry.errorTask.Wait(3000)) {
                $entry.errorTask.GetAwaiter().GetResult() |
                    Set-Content -LiteralPath (Join-Path $resultDirectory "$($entry.role).stderr.txt")
            }
        }
        catch { $report.passed = $false; $report.error = "Probe cleanup: $($_.Exception.Message)" }
        finally { $entry.process.Dispose() }
    }
    if ($null -ne $ownedRevision) {
        try {
            $current = Get-State
            # A lost acknowledgement is recoverable only when both revision and
            # our unique route establish ownership of the published transaction.
            $hasOwnedRoute = @($current.configuredRoutes | Where-Object id -EQ $routeId).Count -eq 1
            $hasOwnedBridges = @($current.configuredWasapiBridges | Where-Object { $_.id -in @($renderBridgeId, $loopbackBridgeId) }).Count -eq 2
            $ownsCurrent = $current.configurationRevision -eq $ownedRevision -and
                $(if ($finalTransactionAttempted) { $hasOwnedRoute } else { $hasOwnedBridges })
            $ownsBridgeOnly = $null -ne $previousOwnedRevision -and
                $current.configurationRevision -eq $previousOwnedRevision -and $hasOwnedBridges -and -not $hasOwnedRoute
            if ($ownsCurrent -or $ownsBridgeOnly) {
                $restoreRevision = [ulong]$current.configurationRevision
                $original.expectedConfigurationRevision = $restoreRevision
                Invoke-Apply $original
                $restored = Get-State
                if ($restored.configurationRevision -ne ($restoreRevision + [ulong]1) -or
                    @($restored.configuredRoutes | Where-Object id -EQ $routeId).Count -or
                    @($restored.configuredWasapiBridges | Where-Object { $_.id -in @($renderBridgeId, $loopbackBridgeId) }).Count) { throw 'Restoration was not confirmed.' }
                $report.restore = 'restored'
            } elseif ($current.configurationRevision -eq $before.configurationRevision) {
                $report.restore = 'unchanged'
            } else {
                $report.restore = 'skipped-concurrent-change'; $report.passed = $false
            }
        } catch { $report.restore = 'failed'; $report.passed = $false; $report.restoreError = $_.Exception.Message }
    }
    $report | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'report.json')
}
Write-Output "Results: $resultDirectory"
$report | ConvertTo-Json -Depth 50
if (-not $report.passed) { exit 1 }
