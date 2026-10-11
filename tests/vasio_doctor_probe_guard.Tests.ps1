$ErrorActionPreference = 'Stop'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$ghostPid = 4294967000
$reportPath = Join-Path $PSScriptRoot ('results\doctor-probe-guard-' + [Guid]::NewGuid().ToString('N') + '.json')
$server = Start-Process -FilePath (Get-Command node -ErrorAction Stop).Source `
    -ArgumentList @((Join-Path $PSScriptRoot 'vasio_status_fake_api.js'),
        $port.ToString(), $ghostPid.ToString()) -PassThru -WindowStyle Hidden
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
    & (Join-Path $PSScriptRoot '..\tools\vasio-doctor\vasio-doctor.ps1') `
        --probe TimoxVasio --api-port $port --probe-path 'C:\missing-vasio-probe.exe' `
        --output $reportPath | Out-Null
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $refusal = @($report.findings | Where-Object code -eq 'PROBE_REFUSED_IN_USE')
    if ($refusal.Count -ne 1 -or $refusal[0].detail -notmatch 'vanished-test-client.exe') {
        throw "Engine client was not named in probe refusal: $($refusal | ConvertTo-Json -Compress)"
    }
    'PASS: doctor refuses ASIO probe when engine reports a client.'
}
finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    $server.Dispose()
    if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
}
