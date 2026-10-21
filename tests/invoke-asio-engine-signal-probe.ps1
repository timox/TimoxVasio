#Requires -Version 7.0
param(
    [Parameter(Mandatory)][ValidateRange(1, 65535)][int]$ApiPort,
    [Parameter(Mandatory)][string]$DriverPath,
    [Parameter(Mandatory)][string]$ProbePath,
    [ValidateRange(1, 298)][int]$Seconds = 5,
    [switch]$AllowExistingClients,
    [string]$ResultsRoot = (Join-Path $PSScriptRoot 'results')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$baseUri = "http://127.0.0.1:$ApiPort"
$sessionId = [Guid]::NewGuid().ToString('N')
$resultDirectory = Join-Path $ResultsRoot "asio-engine-$sessionId"
[void](New-Item -ItemType Directory -Path $resultDirectory -Force)
$processes = [Collections.Generic.List[object]]::new()
$report = [ordered]@{ passed = $false; apiPort = $ApiPort; seconds = $Seconds; restore = 'not-needed'; error = $null }
$original = $null
$ownedRevision = $null
$routeId = "asio-signal-$sessionId"

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
        $socket.ConnectAsync([Uri]"ws://127.0.0.1:$ApiPort/api/v1/ws", $deadline.Token).GetAwaiter().GetResult()
        $id = [Guid]::NewGuid().ToString('N')
        $request = @{ id = $id; command = 'configuration.apply'; payload = $Payload } | ConvertTo-Json -Depth 50 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($request)
        $socket.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text,
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
    $process = [Diagnostics.Process]::Start($start)
    $entry = [pscustomobject]@{ role = $Role; process = $process; ready = $null;
        readyTask = $process.StandardOutput.ReadLineAsync(); outputTask = $null;
        errorTask = $process.StandardError.ReadToEndAsync() }
    $processes.Add($entry)
    return $entry
}
try {
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
    $before = Get-State
    if ($before.engine.clockSource.kind -ne 'software' -or $before.engine.sampleRate -lt 8000 -or
        $before.engine.bufferFrames -lt 16) { throw 'Configure the isolated engine with a software clock before running this test.' }
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
    $temporary.routes = @($original.routes) + @(@{ id = $routeId; sourceEndpointId = $source[0].id;
        destinationEndpointId = $destination[0].id; gainDb = 0.0; mute = $false })
    $temporary.expectedConfigurationRevision = [ulong]$before.configurationRevision
    $ownedRevision = [ulong]$before.configurationRevision + [ulong]1
    Invoke-Apply $temporary
    $applied = Get-State
    if ($applied.configurationRevision -ne $ownedRevision -or
        -not @($applied.routes | Where-Object id -EQ $routeId).Count) { throw 'Temporary route was not confirmed by the engine.' }
    $applied | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'applied.json')
    foreach ($entry in @($sender, $receiver)) {
        $write = $entry.process.StandardInput.WriteLineAsync('start')
        if (-not $write.Wait(3000)) { throw 'ASIO start command deadline exceeded.' }
        $write.GetAwaiter().GetResult()
        $entry.process.StandardInput.Close()
    }
    $runDeadline = [DateTime]::UtcNow.AddSeconds($Seconds + 20)
    foreach ($entry in $processes) {
        $remaining = [Math]::Max(1, [int]($runDeadline - [DateTime]::UtcNow).TotalMilliseconds)
        if (-not $entry.process.WaitForExit($remaining)) { throw "$($entry.role) completion deadline exceeded." }
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
    Get-State | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath (Join-Path $resultDirectory 'after.json')
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
            if ($current.configurationRevision -eq $ownedRevision -and
                @($current.configuredRoutes | Where-Object id -EQ $routeId).Count -eq 1) {
                $original.expectedConfigurationRevision = $ownedRevision
                Invoke-Apply $original
                $restored = Get-State
                if ($restored.configurationRevision -ne ($ownedRevision + [ulong]1) -or
                    @($restored.configuredRoutes | Where-Object id -EQ $routeId).Count) { throw 'Restoration was not confirmed.' }
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
