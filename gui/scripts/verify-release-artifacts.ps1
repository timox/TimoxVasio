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
$installerInclude = Join-Path $guiRoot 'assets\installer.nsh'
$installerHelper = Join-Path $repoRoot 'install\setup-driver.ps1'
$registrationScript = Join-Path $repoRoot 'install\register_drivers.ps1'
$driverBundle = Join-Path $guiRoot 'dist\driver_bundle_1_2_6\TimoxVasio.dll'
$driverZip = Join-Path $guiRoot 'dist\TimoxVasio Driver 1.2.6.zip'
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
    if ($version -ne '1.2.6') {
        throw "$Description : ProductVersion attendu 1.2.6, obtenu '$version' dans '$Path'."
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
if ($package.version -ne '1.2.6') { throw "Version GUI attendue 1.2.6, trouvée $($package.version)." }
$mainSource = Get-Content -LiteralPath (Join-Path $guiRoot 'electron\main.js') -Raw
if ($mainSource -notmatch [regex]::Escape('../../build_codex_110/Release/TimoxVirtualAsioEngine.exe')) {
    throw 'Le chemin du moteur de développement dans electron/main.js ne correspond pas au build canonique.'
}
$engineResource = $package.build.extraResources | Where-Object { $_.to -eq 'backend/TimoxVirtualAsioEngine.exe' } | Select-Object -First 1
$configResource = $package.build.extraResources | Where-Object { $_.to -eq 'backend/config' } | Select-Object -First 1
if (-not $engineResource -or $engineResource.from -ne '../build_codex_110/Release/TimoxVirtualAsioEngine.exe') {
    throw 'Electron Builder ne référence pas le moteur canonique.'
}
if ($configResource) {
    throw 'Electron Builder ne doit plus embarquer la configuration routing.ini du prototype.'
}
if (-not $package.build.nsis.perMachine -or $package.build.nsis.include -ne 'assets/installer.nsh') {
    throw 'Le Setup doit demander les droits administrateur et inclure le pilote ASIO.'
}
Require-File $installerInclude
Require-File $installerHelper
Require-File $registrationScript
$installerSource = Get-Content -LiteralPath $installerInclude -Raw
if ($installerSource -notmatch [regex]::Escape('${PROJECT_DIR}\..\build_driver_110\Release\TimoxVasio.dll') -or
    $installerSource -notmatch [regex]::Escape('SetRegView 64') -or
    $installerSource -notmatch [regex]::Escape('WriteRegStr HKLM "SOFTWARE\ASIO\TimoxVasio"') -or
    $installerSource -notmatch [regex]::Escape('InprocServer32')) {
    throw 'Le Setup ne référence pas les ressources canoniques du pilote.'
}
if ($installerSource -notmatch [regex]::Escape('$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.6\TimoxVasio.dll')) {
    throw 'Le Setup doit enregistrer la DLL dans son dossier versionné 64 bits.'
}

Require-File $enginePath
Require-File $driverPath
Require-ProductVersion $enginePath 'moteur'
Require-ProductVersion $driverPath 'pilote ASIO'
Require-File (Join-Path $engineDir 'openapi-v1.json')
Require-File (Join-Path $engineDir 'schemas\api-v1.json')
Require-SameHash (Join-Path $repoRoot 'api\openapi-v1.json') (Join-Path $engineDir 'openapi-v1.json') 'OpenAPI moteur/source'
Require-SameHash (Join-Path $repoRoot 'api\schemas\api-v1.json') (Join-Path $engineDir 'schemas\api-v1.json') 'schéma moteur/source'
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
    (Join-Path $guiRoot 'dist\Timox VASIO Control Setup 1.2.6.exe'),
    (Join-Path $guiRoot 'dist\Timox VASIO Control 1.2.6.exe')
)
foreach ($output in $outputs) {
    Require-File $output
    $item = Get-Item -LiteralPath $output
    Require-ProductVersion $output 'application Electron'
    if ($item.LastWriteTimeUtc -lt (Get-Item -LiteralPath $enginePath).LastWriteTimeUtc) {
        throw "Livrable Electron antérieur au moteur source; le paquet semble périmé : $($item.FullName)"
    }
}

$sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
if (-not $sevenZip) { throw '7z.exe est requis pour vérifier la DLL embarquée dans le Setup.' }
$payloadDirectory = Join-Path $guiRoot 'dist\release-validation-1.2.6\setup-payload'
New-Item -ItemType Directory -Path $payloadDirectory -Force | Out-Null
& $sevenZip.Source e -tNsis $outputs[0] "-o$payloadDirectory" '-y' '*TimoxVasio.dll' | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Extraction des ressources du Setup impossible : code $LASTEXITCODE" }
$setupDriver = Join-Path $payloadDirectory 'TimoxVasio.dll'
Require-SameHash $driverPath $setupDriver 'DLL du Setup/build'

$gitCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
$gitStatus = @(& git -C $repoRoot status --short)
$manifest = [ordered]@{
    version = '1.2.6'
    validatedAtUtc = [DateTime]::UtcNow.ToString('o')
    commit = $gitCommit
    workingTreeClean = ($gitStatus.Count -eq 0)
    source = [ordered]@{
        engine = [ordered]@{ path = 'build_codex_110/Release/TimoxVirtualAsioEngine.exe'; productVersion = '1.2.6'; sha256 = (Get-Hash $enginePath) }
        driver = [ordered]@{ path = 'build_driver_110/Release/TimoxVasio.dll'; productVersion = '1.2.6'; sha256 = $builtDriverHash }
    }
    artifacts = [ordered]@{
        enginePackaged = [ordered]@{ path = 'dist/win-unpacked/resources/backend/TimoxVirtualAsioEngine.exe'; sha256 = (Get-Hash $packagedEngine) }
        setup = [ordered]@{ path = 'dist/Timox VASIO Control Setup 1.2.6.exe'; sha256 = (Get-Hash $outputs[0]) }
        driverInSetup = [ordered]@{ path = 'dist/release-validation-1.2.6/setup-payload/TimoxVasio.dll'; sha256 = (Get-Hash $setupDriver) }
        portable = [ordered]@{ path = 'dist/Timox VASIO Control 1.2.6.exe'; sha256 = (Get-Hash $outputs[1]) }
        driverArchive = [ordered]@{ path = 'dist/TimoxVasio Driver 1.2.6.zip'; sha256 = (Get-Hash $driverZip); driverSha256 = $zipDriverHash }
    }
}
$reportDirectory = Join-Path $guiRoot 'dist\release-validation-1.2.6'
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportDirectory 'MANIFEST.json') -Encoding utf8
Write-Host "Postflight réussi. Manifeste : $reportDirectory\MANIFEST.json"
