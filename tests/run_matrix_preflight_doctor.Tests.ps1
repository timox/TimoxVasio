$ErrorActionPreference = 'Stop'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$resultsRoot = Join-Path $PSScriptRoot ('results\matrix-preflight-' + [Guid]::NewGuid().ToString('N'))
$server = Start-Process -FilePath (Get-Command node -ErrorAction Stop).Source `
    -ArgumentList @((Join-Path $PSScriptRoot 'vasio_status_fake_api.js'),
        $port.ToString(), '4294967000') -PassThru -WindowStyle Hidden
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            [void](Invoke-RestMethod -Uri "http://127.0.0.1:$port/api/v1/state" -TimeoutSec 1)
            $ready = $true
            break
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $ready) { throw 'Fake engine API did not start.' }
    try {
        & (Join-Path $PSScriptRoot 'run_matrix.ps1') `
            -BaseUri "http://127.0.0.1:$port" `
            -DoctorPath (Join-Path $PSScriptRoot 'run_matrix_doctor_fixture.ps1') `
            -ResultsRoot $resultsRoot | Out-Null
    } catch {
        if ($_.Exception.Message -notmatch 'Close ASIO clients') { throw }
    }
    $sessions = @(Get-ChildItem -LiteralPath $resultsRoot -Directory)
    if ($sessions.Count -ne 1) { throw 'Expected one blocked test session.' }
    $doctor = Join-Path $sessions[0].FullName 'doctor.json'
    $summaryPath = Join-Path $sessions[0].FullName 'summary.json'
    if (-not (Test-Path -LiteralPath $doctor) -or -not (Test-Path -LiteralPath $summaryPath)) {
        throw 'Blocked matrix run did not preserve doctor report and summary.'
    }
    $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
    if ($summary.results[0].mode -ne 'doctor' -or $summary.results.Count -ne 1 -or
        $summary.nextAction -notmatch 'vanished-test-client.exe') {
        throw 'Blocked matrix summary lacks the diagnostic or client name.'
    }
    try {
        & (Join-Path $PSScriptRoot 'run_matrix.ps1') `
            -BaseUri "http://127.0.0.1:$port" `
            -DoctorPath (Join-Path $PSScriptRoot 'run_matrix_doctor_red_fixture.ps1') `
            -ResultsRoot $resultsRoot | Out-Null
        throw 'Red doctor fixture unexpectedly passed.'
    } catch {
        if ($_.Exception.Message -notmatch 'Doctor reported red') { throw }
    }
    $sessions = @(Get-ChildItem -LiteralPath $resultsRoot -Directory)
    if ($sessions.Count -ne 2) { throw 'Expected a second blocked test session.' }
    $redSummary = @($sessions | ForEach-Object {
        Get-Content -LiteralPath (Join-Path $_.FullName 'summary.json') -Raw | ConvertFrom-Json
    } | Where-Object { $_.nextAction -match 'Doctor reported red' })
    if ($redSummary.Count -ne 1 -or $redSummary[0].results.Count -ne 1 -or
        $redSummary[0].results[0].mode -ne 'doctor' -or
        $redSummary[0].nextAction -notmatch 'TEST_CONFLICT') {
        throw 'Red doctor did not stop the matrix before caps and preserve its finding.'
    }
    'PASS: blocked matrix records doctor and summary before ASIO modes.'
}
finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    $server.Dispose()
    $resolvedResults = [IO.Path]::GetFullPath($resultsRoot)
    $allowedRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'results')).TrimEnd('\') + '\'
    if (-not $resolvedResults.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove results outside tests/results.'
    }
    if (Test-Path -LiteralPath $resolvedResults) {
        Remove-Item -LiteralPath $resolvedResults -Recurse -Force
    }
}
