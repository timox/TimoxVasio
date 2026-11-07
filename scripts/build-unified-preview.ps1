param(
    [Parameter(Mandatory = $true)][string]$MidiWinmdPath,
    [Parameter(Mandatory = $true)][string]$CppWinrtPath,
    [Parameter(Mandatory = $true)][string]$CppWinrtHeadersPath,
    [string]$BuildRoot = 'build/unified-preview'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$gui = Join-Path $repo 'gui'
$build = if ([IO.Path]::IsPathRooted($BuildRoot)) { [IO.Path]::GetFullPath($BuildRoot) }
    else { [IO.Path]::GetFullPath((Join-Path $repo $BuildRoot)) }
$lockPath = Join-Path $gui 'artifacts.lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$winmd = (Resolve-Path -LiteralPath $MidiWinmdPath).Path
$winmdHash = (Get-FileHash -LiteralPath $winmd -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedWinmdHash = $lock.buildInputs.windowsMidiMetadata.sha256
if ($winmdHash -ne $expectedWinmdHash) { throw "Windows MIDI metadata hash mismatch: $winmdHash" }
$cppwinrt = (Resolve-Path -LiteralPath $CppWinrtPath).Path
$cppwinrtHash = (Get-FileHash -LiteralPath $cppwinrt -Algorithm SHA256).Hash.ToLowerInvariant()
if ($cppwinrtHash -ne $lock.buildInputs.cppwinrt.sha256) { throw "cppwinrt.exe hash mismatch: $cppwinrtHash" }
$cppwinrtHeaders = (Resolve-Path -LiteralPath $CppWinrtHeadersPath).Path

$revision = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the monorepo revision.' }
if (& git -C $repo status --porcelain --untracked-files=normal) { throw 'Commit source changes before building the preview.' }

function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

$pwarBuild = Join-Path $build 'pwar'
$engineBuild = Join-Path $build 'engine'
$driverBuild = Join-Path $build 'driver'
 $toolchain = @('-G', 'Visual Studio 18 2026', '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0')
Invoke-Checked 'cmake' (@('-S', (Join-Path $repo 'pwar'), '-B', $pwarBuild) + $toolchain + @(
    '-DPWAR_BUILD_LINUX=OFF', '-DPWAR_BUILD_WINDOWS_CLIENT=ON', '-DPWAR_BUILD_LEGACY_DRIVER=OFF')
)
Invoke-Checked 'cmake' @('--build', $pwarBuild, '--config', 'Release', '--target', 'PwarAsioClient', 'PwarBuildInfo')
Invoke-Checked 'cmake' (@('-S', (Join-Path $repo 'engine-next'), '-B', $engineBuild) + $toolchain + @(
    '-DTIMOX_ENABLE_WINDOWS_MIDI_SERVICES=ON', "-DTIMOX_MIDI2_WINMD=$winmd",
    "-DTIMOX_CPPWINRT=$cppwinrt", "-DTIMOX_CPPWINRT_HEADERS=$cppwinrtHeaders",
    "-DTIMOX_PWAR_CLIENT_REVISION=$revision", "-DTIMOX_PWAR_CLIENT_SHA256=$((Get-FileHash (Join-Path $pwarBuild 'Release\PwarAsioClient.exe') -Algorithm SHA256).Hash)"))
Invoke-Checked 'cmake' @('--build', $engineBuild, '--config', 'Release', '--target', 'TimoxVirtualAsioEngine', 'EngineBuildInfo')
Invoke-Checked 'cmake' (@('-S', $repo, '-B', $driverBuild) + $toolchain + @('-DVASIO_BUILD_DRIVERS=ON'))
Invoke-Checked 'cmake' @('--build', $driverBuild, '--config', 'Release', '--target', 'TimoxVasio')

$backend = Join-Path $gui 'artifacts\backend'
$driverArtifacts = Join-Path $gui 'artifacts\driver'
New-Item -ItemType Directory -Force -Path $backend, $driverArtifacts | Out-Null
$outputs = @(
    @{ Source = (Join-Path $pwarBuild 'Release\PwarAsioClient.exe'); Target = (Join-Path $backend 'PwarAsioClient.exe') },
    @{ Source = (Join-Path $pwarBuild 'Release\pwar-build.json'); Target = (Join-Path $backend 'pwar-build.json') },
    @{ Source = (Join-Path $engineBuild 'Release\TimoxVirtualAsioEngine.exe'); Target = (Join-Path $backend 'TimoxVirtualAsioEngine.exe') },
    @{ Source = (Join-Path $engineBuild 'Release\engine-build.json'); Target = (Join-Path $backend 'engine-build.json') },
    @{ Source = (Join-Path $driverBuild 'Release\TimoxVasio.dll'); Target = (Join-Path $driverArtifacts 'TimoxVasio.dll') }
)
foreach ($output in $outputs) {
    if (-not (Test-Path -LiteralPath $output.Source -PathType Leaf)) { throw "Missing build output: $($output.Source)" }
    Copy-Item -LiteralPath $output.Source -Destination $output.Target -Force
}
foreach ($name in @('openapi-v1.json', 'schemas\api-v1.json')) {
    Copy-Item -LiteralPath (Join-Path $repo "engine-next\api\$name") -Destination (Join-Path $gui "api\$name") -Force
}
foreach ($name in @('Windows.Devices.Midi2.dll', 'Windows.Devices.Midi2.pri')) {
    $input = $lock.artifacts | Where-Object path -eq "artifacts/backend/$name" | Select-Object -First 1
    if (-not $input) { continue }
    $source = Join-Path $gui $input.path
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $input.sha256) { throw "Pinned Windows MIDI runtime changed: $name" }
}

$files = @(
    @{ Path = 'artifacts/driver/TimoxVasio.dll'; Component = 'driver' },
    @{ Path = 'artifacts/backend/TimoxVirtualAsioEngine.exe'; Component = 'engine' },
    @{ Path = 'artifacts/backend/PwarAsioClient.exe'; Component = 'pwar' }
)
foreach ($file in $files) {
    $item = $lock.artifacts | Where-Object component -eq $file.Component | Select-Object -First 1
    $path = Join-Path $gui $file.Path
    $item.repository = 'timox/TimoxVasio'
    $item.revision = $revision
    $item.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $item.sizeBytes = (Get-Item -LiteralPath $path).Length
    $item.sourcePath = switch ($file.Component) {
        'driver' { 'build/unified-preview/driver/Release/TimoxVasio.dll' }
        'engine' { 'build/unified-preview/engine/Release/TimoxVirtualAsioEngine.exe' }
        'pwar' { 'build/unified-preview/pwar/Release/PwarAsioClient.exe' }
    }
}
$lock.release.driver.repository = 'timox/TimoxVasio'
$lock.release.driver.revision = $revision
$lock.release.driver.sha256 = ($lock.artifacts | Where-Object component -eq 'driver').sha256
$lock.release.engine.repository = 'timox/TimoxVasio'
$lock.release.engine.revision = $revision
$lock.release.pwar.repository = 'timox/TimoxVasio'
$lock.release.pwar.revision = $revision
foreach ($schema in $lock.schemas) {
    $schema.sha256 = (Get-FileHash -LiteralPath (Join-Path $gui $schema.path) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$lock | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $lockPath -Encoding utf8
Write-Output "Preview components built from $revision. Artifacts and API schemas are staged in gui; commit them before packaging."
