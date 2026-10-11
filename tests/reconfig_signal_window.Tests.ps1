$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'reconfig_signal_window.ps1')

$events = @(
    [pscustomobject]@{ kind = 1; qpc = 105; corrupted = 1; missing = 0; duplicated = 0 },
    [pscustomobject]@{ kind = 1; qpc = 111; corrupted = 0; missing = 0; duplicated = 0 },
    [pscustomobject]@{ kind = 1; qpc = 120; corrupted = 0; missing = 0; duplicated = 0 },
    [pscustomobject]@{ kind = 1; qpc = 130; corrupted = 0; missing = 0; duplicated = 0 }
)
$clean = Get-ReconfigSignalWindow -Events $events -Role receive -ChangeQpc 100 `
    -EndQpc 140 -GraceTicks 10 -MinCallbacks 3
if (-not $clean.pass -or $clean.callbacks -ne 3 -or $clean.sampleErrors -ne 0) {
    throw 'A clean post-reset receiver window was rejected.'
}
$events[2].missing = 1
$corrupt = Get-ReconfigSignalWindow -Events $events -Role receive -ChangeQpc 100 `
    -EndQpc 140 -GraceTicks 10 -MinCallbacks 3
if ($corrupt.pass -or $corrupt.sampleErrors -ne 1) {
    throw 'A receiver that resumed with missing samples was treated as success.'
}
$short = Get-ReconfigSignalWindow -Events $events -Role receive -ChangeQpc 100 `
    -EndQpc 125 -GraceTicks 10 -MinCallbacks 3
if ($short.pass -or $short.callbacks -ne 2) {
    throw 'A short post-reset receiver window was treated as success.'
}
$empty = Get-ReconfigSignalWindow -Events @() -Role receive -ChangeQpc 100 `
    -EndQpc 140 -GraceTicks 10 -MinCallbacks 3
if ($empty.pass -or $empty.callbacks -ne 0) {
    throw 'An empty post-reset receiver window was treated as success.'
}
'Reconfiguration signal window test passed.'
