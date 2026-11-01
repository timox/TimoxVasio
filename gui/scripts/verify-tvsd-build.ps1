param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('PrePackage', 'PostPackage')]
    [string]$Stage
)

$ErrorActionPreference = 'Stop'
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$configPath = Join-Path $guiRoot 'electron-builder.tvsd.json'
$enginePath = Join-Path $repoRoot 'build_codex_110\Release\TimoxVirtualAsioEngine.exe'
$pwarClientPath = Join-Path $repoRoot 'build_pwar_reconciled_tests\Release\PwarAsioClient.exe'
$expectedPwarClientHash = '9E47FDE737D54552E0882D8FFF2D49AB912BDB92CE3247A39341634720CA8441'
$driverPath = Join-Path $repoRoot 'build_driver_110\Release\TimoxVasio.dll'
$outputRoot = Join-Path $guiRoot 'dist\tvsd-0.1.0'
$package = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json

function Get-Hash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Require-Timox127([string]$Path, [string]$Role) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Role absent : $Path" }
    $version = (Get-Item -LiteralPath $Path).VersionInfo.ProductVersion
    if ($version -ne '1.2.7') { throw "$Role doit être en version 1.2.7, version trouvée '$version' : $Path" }
    Write-Host "OK $Role $version : $(Get-Hash $Path)"
}

if ($package.appId -ne 'com.timox.vsounddealer' -or $package.productName -ne 'TimoxVSoundDealer' -or
    $package.extraMetadata.version -ne '0.1.0' -or $package.directories.output -ne 'dist/tvsd-0.1.0') {
    throw 'Identité, version ou dossier de sortie TVSD incorrect.'
}
if ($package.win.target -notcontains 'portable' -or $package.win.target -contains 'nsis') {
    throw 'TVSD doit rester portable et ne pas installer ou enregistrer le pilote ASIO.'
}
$engineResource = $package.extraResources | Where-Object { $_.to -eq 'backend/TimoxVirtualAsioEngine.exe' } | Select-Object -First 1
if (-not $engineResource -or $engineResource.from -ne '../build_codex_110/Release/TimoxVirtualAsioEngine.exe') {
    throw 'TVSD doit embarquer le moteur canonique TimoxVasio.'
}
$pwarClientResource = $package.extraResources | Where-Object { $_.to -eq 'backend/PwarAsioClient.exe' } | Select-Object -First 1
if (-not $pwarClientResource -or $pwarClientResource.from -ne '../build_pwar_reconciled_tests/Release/PwarAsioClient.exe') {
    throw 'TVSD doit embarquer le client PWAR de chemin fixe vérifié.'
}

Require-Timox127 $enginePath 'Moteur TimoxVasio'
Require-Timox127 $driverPath 'Pilote TimoxVasio de référence'
if (-not (Test-Path -LiteralPath $pwarClientPath -PathType Leaf)) { throw "Client PWAR absent : $pwarClientPath" }
if ((Get-Hash $pwarClientPath) -ne $expectedPwarClientHash) { throw 'Le SHA-256 du client PWAR ne correspond pas au handoff vérifié.' }
Write-Host "OK client PWAR vérifié : $expectedPwarClientHash"

if ($Stage -eq 'PostPackage') {
    $unpackedEngine = Join-Path $outputRoot 'win-unpacked\resources\backend\TimoxVirtualAsioEngine.exe'
    $unpackedPwarClient = Join-Path $outputRoot 'win-unpacked\resources\backend\PwarAsioClient.exe'
    $applicationArchive = Join-Path $outputRoot 'win-unpacked\resources\app.asar'
    $portablePath = Join-Path $outputRoot 'TimoxVSoundDealer-0.1.0-portable.exe'
    foreach ($path in @($unpackedEngine, $unpackedPwarClient, $applicationArchive, $portablePath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Artefact TVSD manquant : $path" }
    }
    if ((Get-Hash $enginePath) -ne (Get-Hash $unpackedEngine)) {
        throw 'Le moteur empaqueté dans TVSD diffère du moteur canonique 1.2.7.'
    }
    if ((Get-Hash $unpackedPwarClient) -ne $expectedPwarClientHash) {
        throw 'Le client PWAR empaqueté dans TVSD ne correspond pas au binaire vérifié.'
    }
    Write-Host "OK moteur TVSD empaqueté : $(Get-Hash $unpackedEngine)"
    Write-Host "OK client PWAR empaqueté : $(Get-Hash $unpackedPwarClient)"
    Write-Host "OK interface TVSD empaquetée : $(Get-Hash $applicationArchive)"
    Write-Host "TVSD 0.1 portable : $portablePath"
}
