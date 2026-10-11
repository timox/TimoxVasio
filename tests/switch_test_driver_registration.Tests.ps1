$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'switch_test_driver_registration.ps1'
$report = (& $scriptPath -Action Inspect -BaseUri 'http://127.0.0.1:1') |
    ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw "Inspection exited with code $LASTEXITCODE." }
if ($report.apiAvailable -ne $false -or $report.ready -ne $false) {
    throw "Unavailable engine API was reported as ready: $($report | ConvertTo-Json -Compress)"
}
'PASS: registration inspection stays not ready when the engine API is unavailable.'
