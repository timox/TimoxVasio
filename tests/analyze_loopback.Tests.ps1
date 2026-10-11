$ErrorActionPreference = 'Stop'
$directory = Join-Path $env:TEMP ('vasio-loopback-analysis-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
try {
    $send = Join-Path $directory 'send.jsonl'
    $receive = Join-Path $directory 'receive.jsonl'
    @(
        '{"kind":1,"qpc":100,"counter":0,"samplePosition":0,"positionValid":true,"preSync":0}',
        '{"kind":1,"qpc":200,"counter":1024,"samplePosition":1024,"positionValid":true,"preSync":0}'
    ) | Set-Content -LiteralPath $send
    @(
        '{"kind":1,"qpc":90,"counter":0,"samplePosition":0,"positionValid":false,"preSync":1024}',
        '{"kind":1,"qpc":150,"counter":0,"samplePosition":1024,"positionValid":true,"preSync":0}',
        '{"kind":1,"qpc":250,"counter":1024,"samplePosition":2048,"positionValid":true,"preSync":0}'
    ) | Set-Content -LiteralPath $receive
    $result = & (Join-Path $PSScriptRoot 'analyze_loopback.ps1') -SendEvents $send -ReceiveEvents $receive
    if ($result.matchedBlocks -ne 2 -or
        $result.latencyMinSamples -ne 1024 -or $result.latencyMaxSamples -ne 1024) {
        throw "Unexpected loopback latency result: $($result | ConvertTo-Json -Compress)"
    }
    '{"kind":1,"qpc":150,"counter":0,"samplePosition":0,"positionValid":false,"preSync":1024}' |
        Set-Content -LiteralPath $receive
    try {
        $null = & (Join-Path $PSScriptRoot 'analyze_loopback.ps1') -SendEvents $send -ReceiveEvents $receive
        throw 'Silent loopback unexpectedly produced a latency result.'
    }
    catch {
        if ($_.Exception.Message -notmatch 'No emitted block could be matched') { throw }
    }
    $sendRows = for ($i = 0; $i -lt 200; $i++) {
        '{"kind":1,"qpc":' + (100 + 10 * $i) + ',"counter":' + (1024 * $i) +
            ',"samplePosition":' + (1024 * $i) + ',"positionValid":true,"preSync":0}'
    }
    $receiveRows = for ($i = 0; $i -lt 200; $i++) {
        '{"kind":1,"qpc":' + (105 + 10 * $i) + ',"counter":' + (1024 * $i) +
            ',"samplePosition":' + (1024 * ($i + 1)) + ',"positionValid":true,"preSync":0}'
    }
    $sendRows | Set-Content -LiteralPath $send
    $receiveRows | Set-Content -LiteralPath $receive
    $longResult = & (Join-Path $PSScriptRoot 'analyze_loopback.ps1') -SendEvents $send -ReceiveEvents $receive
    if ($longResult.matchedBlocks -ne 200 -or $longResult.peakPendingSenderBlocks -gt 2) {
        throw "Loopback analysis retained stale sender blocks: $($longResult | ConvertTo-Json -Compress)"
    }
    @(
        '{"kind":1,"qpc":100,"counter":0,"samplePosition":0,"positionValid":true,"preSync":0}',
        '{"kind":1,"qpc":110,"counter":0,"samplePosition":1048576,"positionValid":true,"preSync":0}'
    ) | Set-Content -LiteralPath $send
    @(
        '{"kind":1,"qpc":105,"counter":0,"samplePosition":1024,"positionValid":true,"preSync":0}',
        '{"kind":1,"qpc":115,"counter":0,"samplePosition":1049600,"positionValid":true,"preSync":0}'
    ) | Set-Content -LiteralPath $receive
    $wrapped = & (Join-Path $PSScriptRoot 'analyze_loopback.ps1') -SendEvents $send -ReceiveEvents $receive
    if ($wrapped.matchedBlocks -ne 2 -or $wrapped.latencyMinSamples -ne 1024 -or
        $wrapped.latencyMaxSamples -ne 1024) {
        throw "Counter wrap matched the wrong sender epoch: $($wrapped | ConvertTo-Json -Compress)"
    }
    $smallLimits = Join-Path $directory 'thresholds.json'
    '{"loopback":{"maxAnalysisPendingBlocks":2}}' | Set-Content -LiteralPath $smallLimits
    $sendRows | Set-Content -LiteralPath $send
    '{"kind":1,"qpc":135,"counter":999999,"samplePosition":1024,"positionValid":true,"preSync":0}' |
        Set-Content -LiteralPath $receive
    try {
        $null = & (Join-Path $PSScriptRoot 'analyze_loopback.ps1') -SendEvents $send `
            -ReceiveEvents $receive -ThresholdsPath $smallLimits
        throw 'Unbounded sender history unexpectedly passed.'
    }
    catch {
        if ($_.Exception.Message -notmatch 'exceeded 2 pending sender blocks') { throw }
    }
    Write-Output 'Loopback analysis test passed.'
}
finally { Remove-Item -LiteralPath $directory -Recurse -Force }
