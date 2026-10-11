$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\vasio-doctor\ClockMeasurementAcceptance.ps1')

$regression = [pscustomobject]@{ available = $true; driftPpm = 12.5 }
$passReport = [pscustomobject]@{ verdict = 'pass'; clockRegression = $regression }
$inconclusiveReport = [pscustomobject]@{ verdict = 'inconclusive'; clockRegression = $regression }
$accepted = Get-ClockMeasurementAcceptance -ExitCode 0 -Report $passReport
if (-not $accepted.usable -or $accepted.regression.driftPpm -ne 12.5) {
    throw 'Passing cadence regression was rejected.'
}
$rejected = Get-ClockMeasurementAcceptance -ExitCode 6 -Report $inconclusiveReport
if ($rejected.usable -or $null -ne $rejected.regression) {
    throw 'Inconclusive cadence regression was treated as clock evidence.'
}
$failed = Get-ClockMeasurementAcceptance -ExitCode 6 -Report $passReport
if ($failed.usable -or $null -ne $failed.regression) {
    throw 'Nonzero cadence process exit was treated as clock evidence.'
}
'Clock measurement acceptance test passed.'
