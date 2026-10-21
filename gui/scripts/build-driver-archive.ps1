$ErrorActionPreference = 'Stop'
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$bundle = Join-Path $guiRoot 'dist\driver_bundle_1_2_7'
$archive = Join-Path $guiRoot 'dist\TimoxVasio Driver 1.2.7.zip'
$driver = Join-Path $repoRoot 'build_driver_110\Release\TimoxVasio.dll'
$readmeSource = Join-Path $repoRoot 'install\driver-archive-README.md'
$englishReadmeSource = Join-Path $repoRoot 'install\driver-archive-README.en.md'
$readme = Join-Path $bundle 'README.md'
$englishReadme = Join-Path $bundle 'README.en.md'

foreach ($source in @($driver, $readmeSource, $englishReadmeSource, (Join-Path $repoRoot 'install\Installer TimoxVasio.bat'), (Join-Path $repoRoot 'install\register_drivers.ps1'), (Join-Path $repoRoot 'LICENSE'))) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Ressource de l’archive pilote introuvable : $source"
    }
}

New-Item -ItemType Directory -Path $bundle -Force | Out-Null
Copy-Item -LiteralPath $driver -Destination (Join-Path $bundle 'TimoxVasio.dll') -Force
Copy-Item -LiteralPath $readmeSource -Destination $readme -Force
Copy-Item -LiteralPath $englishReadmeSource -Destination $englishReadme -Force
foreach ($name in @('Installer TimoxVasio.bat', 'register_drivers.ps1', 'LICENSE')) {
    $source = if ($name -eq 'LICENSE') { Join-Path $repoRoot $name } else { Join-Path $repoRoot "install\$name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $bundle $name) -Force
}
Compress-Archive -LiteralPath @(
    (Join-Path $bundle 'Installer TimoxVasio.bat'),
    (Join-Path $bundle 'LICENSE'),
    $readme,
    $englishReadme,
    (Join-Path $bundle 'register_drivers.ps1'),
    (Join-Path $bundle 'TimoxVasio.dll')
) -DestinationPath $archive -Force
Write-Host "Archive pilote reconstruite depuis le build canonique : $archive"
