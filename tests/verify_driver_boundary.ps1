param(
    [string]$DllPath = (Join-Path $PSScriptRoot '..\build_driver_110\Release\TimoxVasio.dll'),
    [string]$CMakeCachePath = (Join-Path $PSScriptRoot '..\build_driver_110\CMakeCache.txt')
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$dll = (Resolve-Path -LiteralPath $DllPath -ErrorAction Stop).Path
$cache = (Resolve-Path -LiteralPath $CMakeCachePath -ErrorAction Stop).Path
$linkerLine = @(Get-Content -LiteralPath $cache | Where-Object {
    $_ -like 'CMAKE_LINKER:FILEPATH=*'
})
if ($linkerLine.Count -ne 1) { throw 'CMAKE_LINKER is missing from the build cache.' }
$linker = $linkerLine[0].Substring('CMAKE_LINKER:FILEPATH='.Length)
$dumpbin = Join-Path (Split-Path -Parent $linker) 'dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) {
    throw "dumpbin.exe is missing beside the configured linker: $dumpbin"
}

. (Join-Path $PSScriptRoot 'driver_import_policy.ps1')
$importLines = & $dumpbin /imports $dll 2>&1
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed to inspect $dll" }
$forbidden = @(Get-ForbiddenDriverDiskWriteImports -DumpbinText ($importLines -join "`n"))
if ($forbidden.Count) {
    throw "TimoxVasio.dll imports disk-write APIs: $($forbidden -join ', ')"
}

$cmake = Get-Content -LiteralPath (Join-Path $repoRoot 'CMakeLists_VASIO_Drivers.txt') -Raw
$sources = [regex]::Match($cmake, '(?s)set\(COMMON_SOURCES(?<body>.*?)\r?\n\)')
if (-not $sources.Success) { throw 'The driver source list could not be audited.' }
if ($sources.Groups['body'].Value -match 'application_profile_store_write') {
    throw 'The driver target includes the profile disk-write implementation.'
}
$readSource = Get-Content -LiteralPath (Join-Path $repoRoot 'src\application_profile_store.cpp') -Raw
if ($readSource -match 'ApplicationProfileStore::Save|GENERIC_WRITE|WriteFile\s*\(') {
    throw 'The driver-linked application profile source contains a disk-write implementation.'
}

[pscustomobject]@{
    verdict = 'pass'
    dll = $dll
    sha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    diskWriteImports = @()
} | ConvertTo-Json -Compress
