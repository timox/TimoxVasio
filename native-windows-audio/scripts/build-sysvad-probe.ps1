[CmdletBinding()]
param([switch]$Acquire)
$ErrorActionPreference = 'Stop'
$repo = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify repository root.' }
$expected = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([IO.Path]::GetFullPath($repo) -ne $expected) { throw 'Unexpected repository root.' }
& git -C $repo status --short
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect repository status.' }
$manifest = Get-Content (Join-Path $PSScriptRoot '../dependencies.json') -Raw | ConvertFrom-Json
$workspacePath = Join-Path $repo 'build_native_audio_wdk'
$sample = Join-Path $workspacePath 'Windows-driver-samples'
$nuget = Join-Path $workspacePath 'nuget.exe'
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
if ($Acquire) {
    New-Item -ItemType Directory -Path $workspacePath -Force | Out-Null
    if (-not (Test-Path -LiteralPath $nuget)) {
        Invoke-WebRequest -Uri $manifest.nuget.downloadUrl -OutFile $nuget
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $nuget
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation' -or
        (Get-FileHash -LiteralPath $nuget -Algorithm SHA256).Hash -ne $manifest.nuget.sha256) {
        throw 'NuGet signature or pinned SHA-256 mismatch; no executable was run.'
    }
    if (-not (Test-Path -LiteralPath $sample)) {
        Invoke-Checked git @('init', $sample)
        Invoke-Checked git @('-C', $sample, 'remote', 'add', 'origin', $manifest.sysvad.repository)
        Invoke-Checked git @('-C', $sample, 'fetch', '--depth', '1', '--filter=blob:none', 'origin', $manifest.sysvad.commit)
        Invoke-Checked git @('-C', $sample, 'sparse-checkout', 'init', '--cone')
        Invoke-Checked git @('-C', $sample, 'sparse-checkout', 'set', 'audio/sysvad')
        Invoke-Checked git @('-C', $sample, 'checkout', '--detach', $manifest.sysvad.commit)
    }
    $actualCommit = (& git -C $sample rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $manifest.sysvad.commit) {
        throw 'Existing sample checkout differs from the pin; refusing to replace it.'
    }
    $entries = $manifest.packages | ForEach-Object {
        '  <package id="{0}" version="{1}" targetFramework="native" />' -f $_, $manifest.kitVersion
    }
    $config = Join-Path $workspacePath 'packages.x64.config'
    @('<?xml version="1.0" encoding="utf-8"?>', '<packages>') + $entries + '</packages>' |
        Set-Content -LiteralPath $config -Encoding utf8
    Invoke-Checked $nuget @('restore', $config, '-PackagesDirectory', (Join-Path $sample 'packages'),
        '-Source', 'https://api.nuget.org/v3/index.json', '-NonInteractive', '-DirectDownload')
}
# A subprocess makes the inventory's nonzero exit a checked build gate.
$pwsh = (Get-Process -Id $PID).Path
$inventoryJson = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'check-build-prerequisites.ps1') -Workspace $workspacePath -Json
if ($LASTEXITCODE -ne 0) { $inventoryJson | Write-Output; throw 'Driver build prerequisites missing.' }
$inventory = $inventoryJson | ConvertFrom-Json
$msbuild = Join-Path $inventory.VisualStudioPath 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$infToolPath = Join-Path $sample "packages/Microsoft.Windows.WDK.x64.$($manifest.kitVersion)/c/bin/10.0.28000.0/x64"
# Compile the dependency first; no solution-wide APO, package, signing or deployment target.
foreach ($project in @('EndpointsCommon/EndpointsCommon.vcxproj', 'TabletAudioSample/TabletAudioSample.vcxproj')) {
    $name = [IO.Path]::GetFileNameWithoutExtension($project)
    Invoke-Checked $msbuild @((Join-Path $sample "audio/sysvad/$project"), '/p:Configuration=Release',
        '/p:Platform=x64', '/p:PreferredToolArchitecture=x64', "/p:InfToolPath=$infToolPath",
        '/p:SignMode=Off', '/m:1', '/v:minimal', '/fl',
        "/flp:logfile=$(Join-Path $workspacePath "$name-build.log")")
}
$binary = Join-Path $sample 'audio/sysvad/TabletAudioSample/x64/Release/TabletAudioSample.sys'
if (-not (Test-Path -LiteralPath $binary)) { throw "Build returned success but expected binary missing: $binary" }
$binaryInfo = Get-Item -LiteralPath $binary
[pscustomobject]@{
    Path=$binaryInfo.FullName
    Length=$binaryInfo.Length
    LastWriteTimeUtc=$binaryInfo.LastWriteTimeUtc.ToString('o')
    Sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
    Signature=(Get-AuthenticodeSignature -LiteralPath $binary).Status.ToString()
} | ConvertTo-Json
Write-Output 'This is the unmodified Microsoft SysVAD build probe, not a TimoxWindowsAudio driver or installable release.'
