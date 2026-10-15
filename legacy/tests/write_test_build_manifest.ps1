# ARCHIVE — INCOMPATIBLE SAUF 1.1.1 : ce script cible la DLL et le moteur 1.1.1 (chemins et versions codés en dur). Il n'est plus maintenu et échoue sur une installation 1.2.1. Conservé pour référence.

param(
    [string]$OutputPath = (Join-Path $PSScriptRoot 'results\test-build-manifest.json')
)

$ErrorActionPreference = 'Stop'
$repoRoot = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'AGENTS.md'))) {
    throw 'TimoxVasio repository root could not be confirmed.'
}

function Get-Artifact([string]$RelativePath) {
    $path = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required test artifact is missing: $path"
    }
    $file = Get-Item -LiteralPath $path
    return [ordered]@{
        path = $RelativePath.Replace('\', '/')
        lastWriteUtc = $file.LastWriteTimeUtc.ToString('o')
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        productVersion = $file.VersionInfo.ProductVersion
    }
}

$guiMain = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'gui\build\static\js') `
    -Filter 'main.*.js' -File | Where-Object { $_.Name -notlike '*.map' })
if ($guiMain.Count -ne 1) { throw 'Expected one built GUI main JavaScript file.' }
$guiMainRelative = 'gui\build\static\js\' + $guiMain[0].Name
$artifacts = [ordered]@{
    engine = Get-Artifact 'build_status_v7\Release\TimoxVirtualAsioEngine.exe'
    driver = Get-Artifact 'build_driver_110\Release\TimoxVasio.dll'
    probe = Get-Artifact 'build_vasio_probe\Release\vasio-probe.exe'
    status = Get-Artifact 'build_vasio_probe\Release\vasio-status.exe'
    gui = Get-Artifact 'gui\build\index.html'
    guiJavaScript = Get-Artifact $guiMainRelative
    engineOpenApi = Get-Artifact 'build_status_v7\Release\openapi-v1.json'
    sourceOpenApi = Get-Artifact 'api\openapi-v1.json'
    engineSchema = Get-Artifact 'build_status_v7\Release\schemas\api-v1.json'
    sourceSchema = Get-Artifact 'api\schemas\api-v1.json'
    portableGui = Get-Artifact 'tests\results\test-electron\win-unpacked\Timox VASIO Control Test.exe'
    portableAsar = Get-Artifact 'tests\results\test-electron\win-unpacked\resources\app.asar'
    portableEngine = Get-Artifact 'tests\results\test-electron\win-unpacked\resources\backend\TimoxVirtualAsioEngine.exe'
    portableOpenApi = Get-Artifact 'tests\results\test-electron\win-unpacked\resources\backend\openapi-v1.json'
    portableSchema = Get-Artifact 'tests\results\test-electron\win-unpacked\resources\backend\schemas\api-v1.json'
    testDriverCopy = Get-Artifact 'tests\results\test-driver-1.1.1-test.1\TimoxVasio.dll'
    testDriverArchive = Get-Artifact 'tests\results\test-driver-1.1.1-test.1.zip'
}
$driverBoundary = & (Join-Path $PSScriptRoot 'verify_driver_boundary.ps1') `
    -DllPath (Join-Path $repoRoot $artifacts.driver.path) `
    -CMakeCachePath (Join-Path $repoRoot 'build_driver_110\CMakeCache.txt') |
    ConvertFrom-Json
if ($driverBoundary.verdict -ne 'pass' -or
    $driverBoundary.sha256 -ne $artifacts.driver.sha256) {
    throw 'The built driver failed the disk-write boundary check.'
}
if ($artifacts.engineOpenApi.sha256 -ne $artifacts.sourceOpenApi.sha256 -or
    $artifacts.engineSchema.sha256 -ne $artifacts.sourceSchema.sha256 -or
    $artifacts.portableOpenApi.sha256 -ne $artifacts.sourceOpenApi.sha256 -or
    $artifacts.portableSchema.sha256 -ne $artifacts.sourceSchema.sha256) {
    throw 'The test engine or portable app API resources differ from the current source contracts.'
}
if ($artifacts.portableEngine.sha256 -ne $artifacts.engine.sha256 -or
    $artifacts.testDriverCopy.sha256 -ne $artifacts.driver.sha256) {
    throw 'A packaged test binary differs from its source build.'
}
$guiBuildTime = (Get-Item -LiteralPath (Join-Path $repoRoot 'gui\build\index.html')).LastWriteTimeUtc
foreach ($relativePath in @('gui\src\App.js', 'gui\src\RoutingPatchbay.js')) {
    if ((Get-Item -LiteralPath (Join-Path $repoRoot $relativePath)).LastWriteTimeUtc -gt $guiBuildTime) {
        throw "The GUI build predates $relativePath; rebuild React before writing a manifest."
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $repoRoot $artifacts.testDriverArchive.path))
try {
    $driverEntry = @($archive.Entries | Where-Object FullName -eq 'TimoxVasio.dll')
    if ($driverEntry.Count -ne 1) { throw 'The test driver archive must contain one TimoxVasio.dll.' }
    $stream = $driverEntry[0].Open()
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $archivedDriverHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $sha.Dispose() }
} finally { $archive.Dispose() }
if ($archivedDriverHash -ne $artifacts.driver.sha256) {
    throw 'The test driver archive DLL differs from the built driver.'
}

$asarModule = Join-Path $repoRoot 'gui\node_modules\@electron\asar'
$asarPath = Join-Path $repoRoot $artifacts.portableAsar.path
$asarHashCode = @'
const asar = require(process.argv[1]);
const crypto = require('crypto');
const archive = process.argv[2];
const name = process.argv[3];
const entries = asar.listPackage(archive).filter(entry => entry.endsWith(name));
if (entries.length !== 1) process.exit(2);
const bytes = asar.extractFile(archive, entries[0].slice(1));
process.stdout.write(crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase());
'@
$packagedGuiHash = (& node -e $asarHashCode $asarModule $asarPath $guiMain[0].Name).Trim()
if ($LASTEXITCODE -ne 0 -or $packagedGuiHash -ne $artifacts.guiJavaScript.sha256) {
    throw 'The JavaScript in the portable app differs from the built GUI.'
}

$manifest = [ordered]@{
    kind = 'TimoxVasio local test build'
    createdUtc = [DateTime]::UtcNow.ToString('o')
    commit = (git -C $repoRoot rev-parse HEAD).Trim()
    installed = $false
    liveMode6Validated = $false
    release111Preflight = 'not run by this script; this manifest does not validate the 1.1.1 distribution'
    archivedDriverSha256 = $archivedDriverHash
    driverBoundary = $driverBoundary
    packagedGuiJavaScriptSha256 = $packagedGuiHash
    artifacts = $artifacts
}
$fullOutput = [IO.Path]::GetFullPath($OutputPath)
$resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\') + '\'
if (-not $fullOutput.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Manifest output must stay inside the repository: $fullOutput"
}
New-Item -ItemType Directory -Path (Split-Path -Parent $fullOutput) -Force | Out-Null
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $fullOutput -Encoding utf8
Write-Output $fullOutput
