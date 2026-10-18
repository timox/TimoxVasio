param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('install', 'uninstall')]
    [string]$Action,
    [string]$SourceDll,
    [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) {
    throw 'Le pilote ASIO x64 doit être enregistré depuis PowerShell 64 bits.'
}
if (-not $InstallRoot) {
    $InstallRoot = if ($env:ProgramW6432) { $env:ProgramW6432 } else { [Environment]::GetFolderPath('ProgramFiles') }
}
$version = '1.2.5'
$driverDirectory = Join-Path $InstallRoot "Steinberg\VirtualASIO\$version"
$installedDll = Join-Path $driverDirectory 'TimoxVasio.dll'
$registrationScript = Join-Path $PSScriptRoot 'register_drivers.ps1'

if ($Action -eq 'install') {
    if (-not (Test-Path -LiteralPath $SourceDll -PathType Leaf)) {
        throw "DLL du Setup manquante : $SourceDll"
    }
    New-Item -ItemType Directory -Path $driverDirectory -Force | Out-Null
    $sourceHash = (Get-FileHash -LiteralPath $SourceDll -Algorithm SHA256).Hash
    if (-not (Test-Path -LiteralPath $installedDll -PathType Leaf) -or
        (Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash -ne $sourceHash) {
        Copy-Item -LiteralPath $SourceDll -Destination $installedDll -Force
    }
    if ((Get-FileHash -LiteralPath $installedDll -Algorithm SHA256).Hash -ne $sourceHash) {
        throw 'La DLL installée diffère de celle contenue dans le Setup.'
    }
    & $registrationScript install -DllDirectory $driverDirectory
    if ($LASTEXITCODE) { throw "Enregistrement du pilote échoué : $LASTEXITCODE" }
    exit 0
}

$inprocKey = 'HKLM:\SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}\InprocServer32'
$registeredDll = if (Test-Path -LiteralPath $inprocKey) {
    (Get-Item -LiteralPath $inprocKey).GetValue('')
} else { $null }
if ($registeredDll -and [string]::Equals($registeredDll, $installedDll,
        [StringComparison]::OrdinalIgnoreCase)) {
    & $registrationScript uninstall
    if ($LASTEXITCODE) { throw "Désenregistrement du pilote échoué : $LASTEXITCODE" }
}
if (Test-Path -LiteralPath $installedDll) {
    try {
        Remove-Item -LiteralPath $installedDll -Force
        Remove-Item -LiteralPath $driverDirectory -ErrorAction SilentlyContinue
    } catch [IO.IOException] {
        # Une application audio peut garder la DLL chargée jusqu'à son prochain arrêt.
    }
}
exit 0
