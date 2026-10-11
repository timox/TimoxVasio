param(
    [string]$StatusPath = (Join-Path $PSScriptRoot '..\build_status_v7\Release\vasio-status.exe')
)

$ErrorActionPreference = 'Stop'
$ghostPid = 4294967000
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
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
    $report = (& (Resolve-Path -LiteralPath $StatusPath).Path --json --once --api-port $port) |
        ConvertFrom-Json
    $alerts = @($report.alerts | Where-Object {
        $_.code -eq 'GHOST_CLIENT' -and $_.pid -eq $ghostPid
    })
    if ($alerts.Count -ne 1) { throw 'A missing engine client was not reported exactly once.' }
    'PASS: absent PID still listed by engine is a ghost on first observation.'
}
finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    $server.Dispose()
}
