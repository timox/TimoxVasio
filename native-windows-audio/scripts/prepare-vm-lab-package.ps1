#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$BuildEvidencePath,
    [Parameter(Mandatory=$true)][string]$DumpbinPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = (& git -C (Join-Path $PSScriptRoot '../..') rev-parse --show-toplevel)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repo)) { throw 'Cannot identify repository root.' }
$repo = [IO.Path]::GetFullPath($repo.Trim())
$gitStatus = @(& git -C $repo status --short)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read repository status.' }
Write-Host "Repository: $repo"
$gitStatus | ForEach-Object { Write-Host $_ }
$tool = Get-Item -LiteralPath $DumpbinPath -Force
if ($tool.PSIsContainer -or $tool.Name -ine 'dumpbin.exe' -or ($tool.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Expected the official dumpbin.exe file.' }
$toolSignature = Get-AuthenticodeSignature -LiteralPath $tool.FullName
if ($toolSignature.Status -ne 'Valid' -or $toolSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') { throw 'dumpbin must have a valid Microsoft signature.' }
$evidenceFile = Get-Item -LiteralPath $BuildEvidencePath -Force
if ($evidenceFile.PSIsContainer -or $evidenceFile.Length -gt 1048576) { throw 'Invalid build evidence file.' }
$evidence = [IO.File]::ReadAllText($evidenceFile.FullName) | ConvertFrom-Json
# Evidence is an operator-supplied build attestation. Each source commit must
# actually exist in this repository; no automatic assignment to current HEAD.
foreach ($artifact in $evidence.artifacts) {
    if ($artifact.sourceCommit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Invalid source commit.' }
    & git -C $repo cat-file -e ($artifact.sourceCommit + '^{commit}')
    if ($LASTEXITCODE -ne 0) { throw "Unknown evidence commit: $($artifact.sourceCommit)" }
}
$inspect = {
    param([string]$Path)
    $signature = (Get-AuthenticodeSignature -LiteralPath $Path).Status.ToString()
    if ([IO.Path]::GetExtension($Path) -ieq '.cat') { return [pscustomobject]@{signature=$signature;architecture='catalogue';imports=@()} }
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 128 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'Invalid PE DOS header.' }
        $stream.Position = 0x3c; $offset = $reader.ReadUInt32()
        if ($offset -gt $stream.Length-24) { throw 'PE header outside file.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Expected PE x64 machine.' }
    } finally { $reader.Dispose(); $stream.Dispose() }
    $lines = @(& $tool.FullName /nologo /dependents $Path 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed for $Path" }
    $imports = @($lines | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] } } | Sort-Object -Unique)
    [pscustomobject]@{ signature=$signature;architecture='x64';imports=$imports;inspectorSha256=(Get-FileHash -LiteralPath $tool.FullName).Hash }
}.GetNewClosure()
Import-Module (Join-Path $PSScriptRoot 'vm-lab-package.psm1') -Force
New-TimoxLabPackage -RepositoryRoot $repo -OutputDirectory $OutputDirectory -BuildEvidence $evidence -InspectBinary $inspect
