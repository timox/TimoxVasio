param(
    [ValidateSet('install', 'uninstall', 'list', 'clean')]
    [string]$Action = 'install',
    [string]$RegistryRoot = 'HKLM:\SOFTWARE',
    [string]$DllDirectory = 'C:\Program Files\Steinberg\VirtualASIO\1.1.0'
)

$ErrorActionPreference = 'Stop'
$driverName = 'TimoxVasio'
$driverClsid = '{A4D39126-78CB-4D89-9E0A-54494D4F5856}'
$legacyDrivers = [ordered]@{
    VASIO1 = '{7A9F4D01-4D7D-4D54-9A61-564153494F31}'
    VASIO2 = '{7A9F4D02-4D7D-4D54-9A61-564153494F32}'
    VASIO3 = '{7A9F4D03-4D7D-4D54-9A61-564153494F33}'
    VASIO4 = '{7A9F4D04-4D7D-4D54-9A61-564153494F34}'
}

function Test-IsMachineRegistry {
    return $RegistryRoot.StartsWith('HKLM:\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-RegistryPermission {
    if (-not (Test-IsMachineRegistry)) { return }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Une installation ou désinstallation sous HKLM nécessite une console administrateur.'
    }
}

function Get-AsioKey([string]$name) {
    return Join-Path (Join-Path $RegistryRoot 'ASIO') $name
}

function Get-ComKey([string]$clsid) {
    return Join-Path (Join-Path (Join-Path $RegistryRoot 'Classes\CLSID') $clsid) 'InprocServer32'
}

function Remove-KnownVasioRegistrations {
    foreach ($name in $legacyDrivers.Keys) {
        $asioKey = Get-AsioKey $name
        if (Test-Path -LiteralPath $asioKey) { Remove-Item -LiteralPath $asioKey -Recurse -Force }

        $clsidKey = Split-Path -Parent (Get-ComKey $legacyDrivers[$name])
        if (Test-Path -LiteralPath $clsidKey) { Remove-Item -LiteralPath $clsidKey -Recurse -Force }
    }
    $currentAsioKey = Get-AsioKey $driverName
    if (Test-Path -LiteralPath $currentAsioKey) { Remove-Item -LiteralPath $currentAsioKey -Recurse -Force }
    $currentClsidKey = Split-Path -Parent (Get-ComKey $driverClsid)
    if (Test-Path -LiteralPath $currentClsidKey) { Remove-Item -LiteralPath $currentClsidKey -Recurse -Force }
}

function Install-VASIODriver {
    $dllPath = [IO.Path]::GetFullPath((Join-Path $DllDirectory 'TimoxVasio.dll'))
    if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
        throw "DLL manquante : $dllPath. Aucune entrée de registre n'a été modifiée."
    }
    Assert-RegistryPermission
    Remove-KnownVasioRegistrations

    $asioKey = Get-AsioKey $driverName
    $clsidKey = Split-Path -Parent (Get-ComKey $driverClsid)
    $inprocKey = Get-ComKey $driverClsid
    New-Item -Path $asioKey -Force | Out-Null
    New-ItemProperty -LiteralPath $asioKey -Name 'CLSID' -Value $driverClsid -PropertyType String -Force | Out-Null
    New-ItemProperty -LiteralPath $asioKey -Name 'Description' -Value $driverName -PropertyType String -Force | Out-Null
    New-Item -Path $clsidKey -Force | Out-Null
    Set-Item -LiteralPath $clsidKey -Value $driverName
    New-Item -Path $inprocKey -Force | Out-Null
    Set-Item -LiteralPath $inprocKey -Value $dllPath
    New-ItemProperty -LiteralPath $inprocKey -Name 'ThreadingModel' -Value 'Apartment' -PropertyType String -Force | Out-Null
    Write-Output 'Le pilote TimoxVasio unique est enregistré.'
}

function Uninstall-VASIODriver {
    Assert-RegistryPermission
    Remove-KnownVasioRegistrations
    Write-Output 'Les entrées TimoxVasio et VASIO historiques connues ont été supprimées.'
}

function Show-VASIODriver {
    $asioKey = Get-AsioKey $driverName
    if (-not (Test-Path -LiteralPath $asioKey)) {
    Write-Output 'TimoxVasio : absent'
        return
    }
    $entry = Get-ItemProperty -LiteralPath $asioKey
    $inprocKey = Get-ComKey ([string]$entry.CLSID)
    $path = if (Test-Path -LiteralPath $inprocKey) { (Get-Item -LiteralPath $inprocKey).GetValue('') } else { '<InprocServer32 absent>' }
    Write-Output "TimoxVasio : CLSID=$($entry.CLSID) ; DLL=$path"
}

switch ($Action) {
    'install' { Install-VASIODriver }
    'uninstall' { Uninstall-VASIODriver }
    'clean' { Uninstall-VASIODriver }
    'list' { Show-VASIODriver }
}
