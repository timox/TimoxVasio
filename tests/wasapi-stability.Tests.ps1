#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'wasapi-stability.ps1')

# API-shaped fixtures include startup silence and discontinuities: absolute
# counters need not be zero, but a later increment must never be accepted.
$fixture = @'
{"wasapiBridges":[
 {"id":"render","generationId":"31","state":"running","effectiveInfo":{"kind":"render"},"health":{"renderedFrames":96000,
  "error":0,"primingTimedOut":false,"droppedCaptureFrames":0,"rejectedFrames":0,
  "renderSilencedInputFrames":0,"silencedFrames":0,"timestampErrors":0,
  "discontinuities":1,"renderSilencedDeviceFrames":4800,"bufferedFrames":8128,"correctionPpm":-87.5}},
 {"id":"capture","generationId":"32","state":"running","effectiveInfo":{"kind":"endpointLoopback"},"health":{"capturedFrames":96000,
  "error":0,"primingTimedOut":false,"droppedCaptureFrames":0,"rejectedFrames":0,
  "renderSilencedInputFrames":0,"silencedFrames":16384,"timestampErrors":0,
  "discontinuities":1,"renderSilencedDeviceFrames":0,"bufferedFrames":7800,"correctionPpm":120.0}}
]}
'@
$checks = 0
function Assert-Condition([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:checks++
}
$ids = @('render', 'capture')
$baseline = Get-WasapiStabilitySample ($fixture | ConvertFrom-Json) $ids
Assert-Condition $baseline.ready 'Startup counters do not prevent a stable baseline'
$advanced = $fixture | ConvertFrom-Json
$advanced.wasapiBridges[0].health.renderedFrames = 144000
$advanced.wasapiBridges[1].health.capturedFrames = 144000
$same = Get-WasapiStabilitySample $advanced $ids $baseline
Assert-Condition $same.ready 'Unchanged incident counters and advancing native audio preserve stability'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[0].health.renderedFrames = 144000
$sample.wasapiBridges[1].health.capturedFrames = 144000
$sample.wasapiBridges[0].health.bufferedFrames = 1
$sample.wasapiBridges[1].health.correctionPpm = 2000
Assert-Condition (Get-WasapiStabilitySample $sample $ids $baseline).ready 'Fill and correction remain descriptive, not arbitrary pass thresholds'
foreach ($name in @('droppedCaptureFrames', 'rejectedFrames', 'renderSilencedInputFrames',
    'silencedFrames', 'timestampErrors', 'discontinuities', 'renderSilencedDeviceFrames')) {
    $sample = $fixture | ConvertFrom-Json
    $sample.wasapiBridges[0].health.$name++
    $result = Get-WasapiStabilitySample $sample $ids $baseline
    Assert-Condition (-not $result.ready -and [bool]($result.issues -match $name)) "Increment $name is detected"
}
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[1].health.silencedFrames = 0
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $baseline).ready) 'Counter reset cannot erase previous incidents'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[0].generationId = '33'
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $baseline).ready) 'Reconnection to a new generation fails the interval'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[1].state = 'priming'
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $baseline).ready) 'Reprime state is detected'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[1].health.error = -2004287484
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $baseline).ready) 'Backend HRESULT is detected'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[1].health.primingTimedOut = $true
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids).ready) 'Timeout prevents the baseline'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[1].health.PSObject.Properties.Remove('silencedFrames')
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $baseline).ready) 'Missing counter cannot silently become zero'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges = @($sample.wasapiBridges[0])
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $baseline).ready) 'Missing bridge is detected'
$sample = $fixture | ConvertFrom-Json
$sample.wasapiBridges[0].health.discontinuities = [decimal]'18446744073709551614'
$largeBaseline = Get-WasapiStabilitySample $sample $ids
$sample.wasapiBridges[0].health.discontinuities = [decimal]'18446744073709551615'
Assert-Condition (-not (Get-WasapiStabilitySample $sample $ids $largeBaseline).ready) 'One-frame increment beyond double precision is detected'
Assert-Condition (-not (Get-WasapiStabilitySample ($fixture | ConvertFrom-Json) $ids $baseline).ready) 'Frozen native counters fail despite a running state'
Assert-Condition (-not (Get-WasapiStabilitySample $advanced $ids $baseline $same 1).ready) 'Frozen second snapshot fails even though counters exceed baseline'
$regressed = $fixture | ConvertFrom-Json
$regressed.wasapiBridges[0].health.renderedFrames = 143999
$regressed.wasapiBridges[1].health.capturedFrames = 192000
$result = Get-WasapiStabilitySample $regressed $ids $baseline $same 1
Assert-Condition (-not $result.ready -and [bool]($result.issues -match 'renderedFrames did not progress')) 'Render regression from preceding snapshot is detected'
$regressed.wasapiBridges[0].health.renderedFrames = 192000
$regressed.wasapiBridges[1].health.capturedFrames = 143999
$result = Get-WasapiStabilitySample $regressed $ids $baseline $same 1
Assert-Condition (-not $result.ready -and [bool]($result.issues -match 'capturedFrames did not progress')) 'Capture regression from preceding snapshot is detected'
$advanced.wasapiBridges[0].health.renderedFrames = 192000
$advanced.wasapiBridges[1].health.capturedFrames = 192000
Assert-Condition (Get-WasapiStabilitySample $advanced $ids $baseline $same 2.5).ready 'Both native counters advance at maximum permitted gap'
$result = Get-WasapiStabilitySample $advanced $ids $baseline $same 2.501
Assert-Condition (-not $result.ready -and [bool]($result.issues -match 'Snapshot gap')) 'Excessive observation gap fails even with advancing counters'
$advanced.wasapiBridges[0].effectiveInfo.kind = 'endpointLoopback'
Assert-Condition (-not (Get-WasapiStabilitySample $advanced $ids $baseline $same 1).ready) 'Wrong stream kind cannot select a convenient progress counter'
Write-Output "WASAPI stability snapshot tests passed ($checks checks)."
