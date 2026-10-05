param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('PrePackage', 'PostPackage')]
    [string]$Stage
)

$ErrorActionPreference = 'Stop'
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$enginePath = Join-Path $repoRoot 'build_codex_110\Release\TimoxVirtualAsioEngine.exe'
$engineDir = Split-Path -Parent $enginePath
$driverPath = Join-Path $repoRoot 'build_driver_110\Release\TimoxVasio.dll'
$driverBundle = Join-Path $guiRoot 'dist\driver_bundle_1_1_0\TimoxVasio.dll'
$driverZip = Join-Path $guiRoot 'dist\TimoxVasio Driver 1.1.0.zip'
$packagedEngine = Join-Path $guiRoot 'dist\win-unpacked\resources\backend\TimoxVirtualAsioEngine.exe'

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Binaire ou ressource requis introuvable : $Path"
    }
}

function Require-Directory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Dossier requis introuvable : $Path"
    }
}

function Get-Hash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Require-SameHash([string]$ExpectedPath, [string]$ActualPath, [string]$Description) {
    Require-File $ExpectedPath
    Require-File $ActualPath
    $expected = Get-Hash $ExpectedPath
    $actual = Get-Hash $ActualPath
    if ($expected -ne $actual) {
        throw "$Description : SHA-256 différent. Attendu $expected depuis '$ExpectedPath', obtenu $actual depuis '$ActualPath'."
    }
    Write-Host "OK SHA-256 $Description : $actual"
}

function Require-ProductVersion([string]$Path, [string]$Description) {
    Require-File $Path
    $version = (Get-Item -LiteralPath $Path).VersionInfo.ProductVersion
    if ($version -ne '1.1.0') {
        throw "$Description : ProductVersion attendu 1.1.0, obtenu '$version' dans '$Path'."
    }
    Write-Host "OK version $Description : $version"
}

function Get-ZipEntryHash([string]$ArchivePath, [string]$EntryName) {
    Require-File $ArchivePath
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entry = $archive.Entries | Where-Object { $_.FullName -eq $EntryName } | Select-Object -First 1
        if (-not $entry) { throw "Entrée '$EntryName' absente de l’archive '$ArchivePath'." }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $sha.Dispose(); $stream.Dispose() }
    }
    finally { $archive.Dispose() }
}

$manifestPath = Join-Path $guiRoot 'package.json'
$package = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($package.version -ne '1.1.0') { throw "Version GUI attendue 1.1.0, trouvée $($package.version)." }
$mainSource = Get-Content -LiteralPath (Join-Path $guiRoot 'electron\main.js') -Raw
if ($mainSource -notmatch [regex]::Escape('../../build_codex_110/Release/TimoxVirtualAsioEngine.exe')) {
    throw 'Le chemin du moteur de développement dans electron/main.js ne correspond pas au build canonique.'
}
$engineResource = $package.build.extraResources | Where-Object { $_.to -eq 'backend/TimoxVirtualAsioEngine.exe' } | Select-Object -First 1
$configResource = $package.build.extraResources | Where-Object { $_.to -eq 'backend/config' } | Select-Object -First 1
if (-not $engineResource -or $engineResource.from -ne '../build_codex_110/Release/TimoxVirtualAsioEngine.exe') {
    throw 'Electron Builder ne référence pas le moteur canonique.'
}
if (-not $configResource -or $configResource.from -ne '../build_codex_110/Release/config') {
    throw 'Electron Builder ne référence pas la configuration du build canonique.'
}

Require-File $enginePath
Require-File $driverPath
Require-ProductVersion $enginePath 'moteur'
Require-ProductVersion $driverPath 'pilote ASIO'
Require-Directory (Join-Path $engineDir 'config')
Require-File (Join-Path $engineDir 'openapi-v1.json')
Require-File (Join-Path $engineDir 'schemas\api-v1.json')
Require-SameHash (Join-Path $repoRoot 'openapi-v1.json') (Join-Path $engineDir 'openapi-v1.json') 'OpenAPI moteur/source'
Require-SameHash (Join-Path $repoRoot 'schemas\api-v1.json') (Join-Path $engineDir 'schemas\api-v1.json') 'schéma moteur/source'
Require-SameHash $driverPath $driverBundle 'DLL du dossier pilote/build'

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipDriverHash = Get-ZipEntryHash $driverZip 'TimoxVasio.dll'
$builtDriverHash = Get-Hash $driverPath
if ($zipDriverHash -ne $builtDriverHash) {
    throw "DLL de l’archive pilote périmée : build=$builtDriverHash, archive=$zipDriverHash."
}
Write-Host "OK SHA-256 DLL dans l’archive pilote : $zipDriverHash"

if ($Stage -eq 'PrePackage') {
    Write-Host 'Préflight réussi : moteur, pilote, ressources API et chemins Electron sont cohérents.'
    exit 0
}

Require-SameHash $enginePath $packagedEngine 'moteur empaqueté/build'
$outputs = @(
    (Join-Path $guiRoot 'dist\Timox VASIO Control Setup 1.1.0.exe'),
    (Join-Path $guiRoot 'dist\Timox VASIO Control 1.1.0.exe')
)
foreach ($output in $outputs) {
    Require-File $output
    $item = Get-Item -LiteralPath $output
    Require-ProductVersion $output 'application Electron'
    if ($item.LastWriteTimeUtc -lt (Get-Item -LiteralPath $enginePath).LastWriteTimeUtc) {
        throw "Livrable Electron antérieur au moteur source; le paquet semble périmé : $($item.FullName)"
    }
}

$gitCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
$gitStatus = @(& git -C $repoRoot status --short)
$manifest = [ordered]@{
    version = '1.1.0'
    validatedAtUtc = [DateTime]::UtcNow.ToString('o')
    commit = $gitCommit
    workingTreeClean = ($gitStatus.Count -eq 0)
    source = [ordered]@{
        engine = [ordered]@{ path = 'build_codex_110/Release/TimoxVirtualAsioEngine.exe'; productVersion = '1.1.0'; sha256 = (Get-Hash $enginePath) }
        driver = [ordered]@{ path = 'build_driver_110/Release/TimoxVasio.dll'; productVersion = '1.1.0'; sha256 = $builtDriverHash }
    }
    artifacts = [ordered]@{
        enginePackaged = [ordered]@{ path = 'dist/win-unpacked/resources/backend/TimoxVirtualAsioEngine.exe'; sha256 = (Get-Hash $packagedEngine) }
        setup = [ordered]@{ path = 'dist/Timox VASIO Control Setup 1.1.0.exe'; sha256 = (Get-Hash $outputs[0]) }
        portable = [ordered]@{ path = 'dist/Timox VASIO Control 1.1.0.exe'; sha256 = (Get-Hash $outputs[1]) }
        driverArchive = [ordered]@{ path = 'dist/TimoxVasio Driver 1.1.0.zip'; sha256 = (Get-Hash $driverZip); driverSha256 = $zipDriverHash }
    }
}
$reportDirectory = Join-Path $guiRoot 'dist\release-validation-1.1.0'
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportDirectory 'MANIFEST.json') -Encoding utf8
Write-Host "Postflight réussi. Manifeste : $reportDirectory\MANIFEST.json"
