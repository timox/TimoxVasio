$ErrorActionPreference = 'Stop'
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$bundle = Join-Path $guiRoot 'dist\driver_bundle_1_1_0'
$archive = Join-Path $guiRoot 'dist\TimoxVasio Driver 1.1.0.zip'
$driver = Join-Path $repoRoot 'build_driver_110\Release\TimoxVasio.dll'
$readme = Join-Path $bundle 'README.md'

foreach ($source in @($driver, $readme, (Join-Path $repoRoot 'Installer TimoxVasio.bat'), (Join-Path $repoRoot 'register_drivers.ps1'), (Join-Path $repoRoot 'LICENSE'))) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Ressource de l’archive pilote introuvable : $source"
    }
}

Copy-Item -LiteralPath $driver -Destination (Join-Path $bundle 'TimoxVasio.dll') -Force
foreach ($name in @('Installer TimoxVasio.bat', 'register_drivers.ps1', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination (Join-Path $bundle $name) -Force
}
Compress-Archive -LiteralPath @(
    (Join-Path $bundle 'Installer TimoxVasio.bat'),
    (Join-Path $bundle 'LICENSE'),
    $readme,
    (Join-Path $bundle 'register_drivers.ps1'),
    (Join-Path $bundle 'TimoxVasio.dll')
) -DestinationPath $archive -Force
Write-Host "Archive pilote reconstruite depuis le build canonique : $archive"
