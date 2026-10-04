param(
    [string]$BaseUri = "http://127.0.0.1:52525"
)

$ErrorActionPreference = 'Stop'
$schemaPath = Join-Path $PSScriptRoot '..\schemas\api-v1.json'
function Assert-ApiSchema($value, [string]$description) {
    $jsonText = ConvertTo-Json -InputObject $value -Depth 20 -Compress
    if (-not (Test-Json -Json $jsonText -SchemaFile $schemaPath)) {
        throw "$description does not match schemas/api-v1.json"
    }
}

$httpState = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state"
if ($httpState.apiVersion -ne '1.0' -or $httpState.engine.state -notin @('stopped', 'running', 'error', 'reconfiguring')) {
    throw 'GET /api/v1/state returned an invalid runtime state.'
}
Assert-ApiSchema $httpState 'GET /api/v1/state'
$driverState = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/drivers"
if ($driverState.virtualDrivers.Count -ne 1 -or $driverState.virtualDrivers[0].id -ne 'TimoxVasio') {
    throw 'GET /api/v1/drivers did not return the unique TimoxVasio driver.'
}
Assert-ApiSchema $driverState 'GET /api/v1/drivers'

$webSocket = [System.Net.WebSockets.ClientWebSocket]::new()
$webSocket.Options.AddSubProtocol('vasio.api.v1')
$socketUri = $BaseUri.Replace('http://', 'ws://').Replace('https://', 'wss://') + '/api/v1/ws'
[void]$webSocket.ConnectAsync([Uri]$socketUri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

function Read-Message {
    $messageBuffer = [System.IO.MemoryStream]::new()
    $receiveBuffer = [byte[]]::new(8192)
    do {
        $segment = [ArraySegment[byte]]::new($receiveBuffer)
        $received = $webSocket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($received.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
            throw 'WebSocket closed before the expected message arrived.'
        }
        $messageBuffer.Write($receiveBuffer, 0, $received.Count)
    } while (-not $received.EndOfMessage)
    $messageText = [Text.Encoding]::UTF8.GetString($messageBuffer.ToArray())
    $messageBuffer.Dispose()
    return $messageText | ConvertFrom-Json
}

try {
    $initialStatus = Read-Message
    $initialDevices = Read-Message
    if ($initialStatus.event -ne 'engine.status' -or $initialDevices.event -ne 'devices.changed') {
        throw 'WebSocket did not send the initial status and inventory events.'
    }
    Assert-ApiSchema $initialStatus 'WebSocket engine.status'
    Assert-ApiSchema $initialDevices 'WebSocket devices.changed'

    $invalidBufferRequestId = 'smoke-invalid-buffer'
    $invalidBufferRequest = @{
        id = $invalidBufferRequestId
        command = 'configuration.apply'
        payload = @{
            physicalDriverId = $null
            sampleRate = $null
            bufferFrames = 0
            routes = @()
        }
    } | ConvertTo-Json -Depth 8 -Compress
    $invalidBufferBytes = [Text.Encoding]::UTF8.GetBytes($invalidBufferRequest)
    [void]$webSocket.SendAsync([ArraySegment[byte]]::new($invalidBufferBytes),
        [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
        [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $invalidBufferResponse = Read-Message
    if ($invalidBufferResponse.id -ne $invalidBufferRequestId -or $invalidBufferResponse.success -or
        $invalidBufferResponse.error.code -ne 'INVALID_CONFIGURATION') {
        throw 'Runtime API accepted a zero physical buffer size.'
    }

    $requestId = 'smoke-apply-1'
    $applyRequest = @{
        id = $requestId
        command = 'configuration.apply'
        payload = @{
            physicalDriverId = $null
            sampleRate = $null
            bufferFrames = $null
            routes = @()
        }
    } | ConvertTo-Json -Depth 8 -Compress
    $sendBytes = [Text.Encoding]::UTF8.GetBytes($applyRequest)
    $sendSegment = [ArraySegment[byte]]::new($sendBytes)
    [void]$webSocket.SendAsync($sendSegment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
        [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    $seenStates = [System.Collections.Generic.List[string]]::new()
    $accepted = $false
    $routesChanged = $false
    for ($messageIndex = 0; $messageIndex -lt 12 -and -not $routesChanged; $messageIndex++) {
        $message = Read-Message
        if ($message.event -eq 'engine.status') { $seenStates.Add($message.payload.state) }
        if ($message.event -in @('engine.status', 'routes.changed')) {
            Assert-ApiSchema $message "WebSocket $($message.event)"
        }
        if ($message.id -eq $requestId) {
            if (-not $message.success -or -not $message.result.accepted) {
                throw "configuration.apply failed: $($message | ConvertTo-Json -Compress -Depth 8)"
            }
            $accepted = $true
        }
        if ($message.event -eq 'routes.changed') { $routesChanged = $true }
    }
    if (-not $accepted -or -not $routesChanged -or
        $seenStates -notcontains 'reconfiguring' -or $seenStates -notcontains 'stopped') {
        throw "WebSocket transaction incomplete (accepted=$accepted routes=$routesChanged states=$($seenStates -join ','))"
    }
    $finalState = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state"
    if ($finalState.engine.state -ne 'stopped' -or $finalState.routes.Count -ne 0) {
        throw 'HTTP state did not reflect the accepted stopped configuration.'
    }

    $invalidRequestId = 'smoke-apply-invalid'
    $invalidRequest = @{
        id = $invalidRequestId
        command = 'configuration.apply'
        payload = @{
            physicalDriverId = $null
            sampleRate = $null
            bufferFrames = $null
            routes = @(
                @{ id = 'missing-endpoints'; sourceEndpointId = 'virtual:TimoxVasio:999999:output:1';
                    destinationEndpointId = 'virtual:TimoxVasio:999998:input:1'; gainDb = 0; mute = $false }
            )
        }
    } | ConvertTo-Json -Depth 8 -Compress
    $invalidBytes = [Text.Encoding]::UTF8.GetBytes($invalidRequest)
    [void]$webSocket.SendAsync([ArraySegment[byte]]::new($invalidBytes),
        [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
        [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $errorStates = [System.Collections.Generic.List[string]]::new()
    $rejected = $false
    $engineError = $false
    for ($messageIndex = 0; $messageIndex -lt 12 -and -not $engineError; $messageIndex++) {
        $message = Read-Message
        if ($message.event -eq 'engine.status') { $errorStates.Add($message.payload.state) }
        if ($message.event -in @('engine.status', 'engine.error')) {
            Assert-ApiSchema $message "WebSocket $($message.event)"
        }
        if ($message.id -eq $invalidRequestId) {
            if ($message.success -or $message.error.code -ne 'AUDIO_CONFIGURATION_FAILED') {
                throw 'Invalid routing configuration was not rejected by the engine.'
            }
            $rejected = $true
        }
        if ($message.event -eq 'engine.error') { $engineError = $true }
    }
    if (-not $rejected -or -not $engineError -or
        $errorStates -notcontains 'reconfiguring' -or $errorStates -notcontains 'error') {
        throw "Failed configuration did not leave the engine in error (rejected=$rejected errorEvent=$engineError states=$($errorStates -join ','))"
    }
    $failedState = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state"
    if ($failedState.engine.state -ne 'error' -or $failedState.routes.Count -ne 0) {
        throw 'Failed configuration left a partial route or an incorrect engine state.'
    }
    Write-Output 'PASS: runtime HTTP inventory and WebSocket stop/apply/status events.'
}
finally {
    if ($webSocket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        [void]$webSocket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, 'done',
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $webSocket.Dispose()
}
