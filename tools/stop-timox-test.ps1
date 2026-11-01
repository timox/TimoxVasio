[CmdletBinding()]
param(
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
$apiPort = 52525
$processNames = @(
    'TimoxVSSC',
    'TimoxVirtualAsioEngine',
    'PwarAsioClient',
    'Timox VASIO Control'
)

function Stop-TimoxProcess {
    param([System.Diagnostics.Process]$Process)

    if ($WhatIf) {
        Write-Host "[what-if] arrêt de $($Process.ProcessName) (PID $($Process.Id))"
        return
    }

    if ($Process.MainWindowHandle -ne 0) {
        [void]$Process.CloseMainWindow()
        if ($Process.WaitForExit(3000)) {
            Write-Host "arrêt propre de $($Process.ProcessName) (PID $($Process.Id))"
            return
        }
    }

    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
    if (Get-Process -Id $Process.Id -ErrorAction SilentlyContinue) {
        throw "Impossible d'arrêter $($Process.ProcessName) (PID $($Process.Id))."
    }
    Write-Host "arrêt forcé de $($Process.ProcessName) (PID $($Process.Id))"
}

$processes = @(
    Get-Process -Name $processNames -ErrorAction SilentlyContinue |
        Sort-Object Id -Unique
)

foreach ($process in $processes) {
    Stop-TimoxProcess $process
}

if ($WhatIf) {
    Write-Host 'vérification réelle ignorée avec -WhatIf'
    exit 0
}

Start-Sleep -Milliseconds 500
$listeners = @(Get-NetTCPConnection -LocalPort $apiPort -State Listen -ErrorAction SilentlyContinue)
if ($listeners.Count -gt 0) {
    $owners = $listeners | ForEach-Object {
        $owner = Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue
        if ($owner) { "$($owner.ProcessName) (PID $($owner.Id))" } else { "PID $($_.OwningProcess)" }
    }
    throw "Le port API $apiPort est encore occupé : $($owners -join ', '). Aucun test ne doit démarrer."
}

Write-Host "Timox GUI, Engine et PWAR arrêtés ; port API $apiPort libre."
