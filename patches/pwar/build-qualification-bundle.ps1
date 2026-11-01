$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$root = (& git -C $repo rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($root) -ne $repo) { throw 'Repository root mismatch' }
& git -C $repo status --short
$upstream = 'ed5b07c44c7635d006422d646d56368600504b3f'
$source = Join-Path $repo "build_native_production\pwar-lifecycle-verification\PWAR-$upstream"
$driver = Join-Path $repo 'build_pwar_qualification\Release\PWARASIO.dll'
$expected = 'EAB1067A785D3AA25A241D0DD3122263558D555459879060866AA62B0D982EB2'
if ((Get-FileHash -LiteralPath $driver -Algorithm SHA256).Hash -ne $expected) {
    throw 'PWAR DLL differs from the six-patch qualified binary; rebuild/requalify before packaging'
}
$sdk = Join-Path $repo 'asiosdk'
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$inputs = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'qualification-inputs.json') -Raw | ConvertFrom-Json
$archive = Join-Path $repo 'build_native_production\pwar-ed5b07c.zip'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $inputs.upstreamArchiveSha256) { throw 'Upstream archive hash mismatch' }
$reconstruction = Join-Path $repo "build_pwar_bundle_source_$stamp"
if (Test-Path -LiteralPath $reconstruction) { throw 'Source reconstruction directory already exists' }
Expand-Archive -LiteralPath $archive -DestinationPath $reconstruction
$reconstructedSource = Join-Path $reconstruction "PWAR-$upstream"
$patches = @()
foreach ($entry in $inputs.patches) {
    $patch = Get-Item -LiteralPath (Join-Path $PSScriptRoot $entry.name)
    if ((Get-FileHash -LiteralPath $patch.FullName -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Unqualified patch: $($entry.name)" }
    & git -C $repo apply "--directory=build_pwar_bundle_source_$stamp/PWAR-$upstream" $patch.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot reconstruct reviewed source: $($entry.name)" }
    $patches += $patch
}
foreach ($relative in @('windows\asio', 'protocol', 'linux')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $reconstructedSource $relative) -Recurse -File) {
        $tail = $file.FullName.Substring($reconstructedSource.Length + 1)
        # The qualification worktree normalizes CRLF; compare source text with
        # only line endings normalized, not whitespace or compiler content.
        if ([IO.File]::ReadAllText($file.FullName).Replace("`r`n", "`n") -cne
            [IO.File]::ReadAllText((Join-Path $source $tail)).Replace("`r`n", "`n")) {
            throw "Compiled source differs from reconstructed reviewed source: $tail"
        }
    }
}
foreach ($entry in $inputs.sdkCommon) {
    if ((Get-FileHash -LiteralPath (Join-Path $sdk "common\$($entry.name)") -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Unqualified SDK input: $($entry.name)"
    }
}
$bundle = Join-Path $repo "gui\dist\PWAR-qualification-$stamp"
if (Test-Path -LiteralPath $bundle) { throw 'Output already exists' }
New-Item -ItemType Directory -Path (Join-Path $bundle 'sources\asiosdk') -Force | Out-Null
Copy-Item -LiteralPath $reconstructedSource -Destination (Join-Path $bundle 'sources\PWAR') -Recurse
Copy-Item -LiteralPath (Join-Path $sdk 'common') -Destination (Join-Path $bundle 'sources\asiosdk\common') -Recurse
Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $bundle 'sources\asiosdk\LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bundle-CMakeLists.txt') -Destination (Join-Path $bundle 'sources\CMakeLists.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bundle-README.md') -Destination (Join-Path $bundle 'README.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'GPL-3.0.txt') -Destination $bundle
New-Item -ItemType Directory -Path (Join-Path $bundle 'patches') | Out-Null
if ($patches.Count -ne 7) { throw 'Expected exactly seven reviewed patches' }
foreach ($patch in $patches) { Copy-Item -LiteralPath $patch.FullName -Destination (Join-Path $bundle 'patches') }
Copy-Item -LiteralPath $driver -Destination (Join-Path $bundle 'PWARASIO.dll')
$files = @(Get-ChildItem -LiteralPath $bundle -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($bundle.Length + 1).Replace('\', '/'); bytes = $_.Length;
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$manifest = [ordered]@{ kind = 'PWAR qualification candidate'; upstreamCommit = $upstream;
    timoxCommit = (& git -C $repo rev-parse HEAD).Trim(); builtAtUtc = [DateTime]::UtcNow.ToString('o');
    driverSha256 = $expected; files = $files }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $bundle 'MANIFEST.json') -Encoding utf8
Compress-Archive -LiteralPath $bundle -DestinationPath "$bundle.zip"
$zip = [IO.Compression.ZipFile]::OpenRead("$bundle.zip")
try {
    $prefix = (Split-Path -Leaf $bundle) + '/'
    foreach ($file in $files) {
        $entry = $zip.GetEntry($prefix + $file.path)
        if (-not $entry -or $entry.Length -ne $file.bytes) { throw "Missing or truncated ZIP entry: $($file.path)" }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($actual -ne $file.sha256) { throw "ZIP entry hash mismatch: $($file.path)" }
    }
} finally { $zip.Dispose() }
Write-Output "Bundle: $bundle"
Get-FileHash -LiteralPath "$bundle.zip" -Algorithm SHA256
