$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot 'loopback_plan_fake_api.js'
$wrapper = Join-Path $PSScriptRoot 'with-loopback-route.ps1'

function Invoke-PlanFixture([string]$ClientName, [bool]$AllowOtherClients, [bool]$ShouldPass) {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()
    $server = Start-Process -FilePath (Get-Command node).Source -ArgumentList @(
        $fixture, $port.ToString(), $ClientName
    ) -WindowStyle Hidden -PassThru
    try {
        $uri = "http://127.0.0.1:$port/api/v1/state"
        $ready = $false
        for ($attempt = 0; $attempt -lt 50; ++$attempt) {
            try { $null = Invoke-RestMethod -Uri $uri -TimeoutSec 1; $ready = $true; break }
            catch { Start-Sleep -Milliseconds 100 }
        }
        if (-not $ready) { throw 'Fake API did not start.' }
        $arguments = @('-NoProfile', '-File', $wrapper, '-BaseUri',
            "http://127.0.0.1:$port", '-PlanOnly')
        if ($AllowOtherClients) { $arguments += '-AllowOtherClients' }
        $output = & pwsh @arguments 2>&1
        $passed = $LASTEXITCODE -eq 0
        if ($passed -ne $ShouldPass) {
            throw "Unexpected plan result for $ClientName (allow=$AllowOtherClients): $output"
        }
        if ($passed) {
            $plan = ($output -join "`n") | ConvertFrom-Json
            if (-not $plan.canRun -or $plan.activeClients[0].processName -ne $ClientName) {
                throw "Invalid plan output: $output"
            }
        }
    }
    finally {
        if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
        $server.Dispose()
    }
}

Invoke-PlanFixture 'mixxx.exe' $true $true
Invoke-PlanFixture 'vasio-probe-send.exe' $true $false
Invoke-PlanFixture 'mixxx.exe' $false $false
'Concurrent loopback plan tests passed.'
