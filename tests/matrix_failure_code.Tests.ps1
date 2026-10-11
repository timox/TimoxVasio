$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'matrix_failure_code.ps1')

$passing = @(
    [ordered]@{ mode = 'doctor'; verdict = 'orange'; exitCode = 0 },
    [ordered]@{ mode = 'cadence'; verdict = 'pass'; exitCode = 0 }
)
if ((Get-MatrixFailureCode -Results $passing -DoctorVerdict orange) -ne 0) {
    throw 'Passing matrix entries returned a failure code.'
}
$watchdog = @($passing) + @([ordered]@{
    mode = 'loopback-receiver-first'; verdict = 'fail'; exitCode = 124
})
if ((Get-MatrixFailureCode -Results $watchdog -DoctorVerdict orange) -ne 6) {
    throw 'A mode-4 child watchdog was treated as success.'
}
$failedChild = @($passing) + @([ordered]@{
    mode = 'reconfig-2'; verdict = 'pass'; exitCode = 5
})
if ((Get-MatrixFailureCode -Results $failedChild -DoctorVerdict orange) -ne 6) {
    throw 'A nonzero child exit was treated as success.'
}
if (Test-MatrixEntryPass -Entry $failedChild[2]) {
    throw 'A nonzero child exit was allowed through the next-mode gate.'
}
$inconclusive = @($passing) + @([ordered]@{
    mode = 'cadence'; verdict = 'inconclusive'; exitCode = 0
})
if ((Get-MatrixFailureCode -Results $inconclusive -DoctorVerdict orange) -ne 6) {
    throw 'An inconclusive child was treated as success.'
}
if ((Get-MatrixFailureCode -Results $passing -DoctorVerdict red) -ne 7) {
    throw 'A red doctor preflight was treated as success.'
}
'Matrix fatal-result gate passed.'
