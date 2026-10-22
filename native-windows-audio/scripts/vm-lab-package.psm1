#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Get-PackageMap {
    @(
        @('build_native_windows_audio/x64/Release/TimoxWindowsAudio/TimoxWindowsAudio.sys','driver/TimoxWindowsAudio.sys'),
        @('build_native_windows_audio/x64/Release/TimoxWindowsAudio/TimoxWindowsAudio.inf','driver/TimoxWindowsAudio.inf'),
        @('build_native_windows_audio/x64/Release/TimoxWindowsAudio/timoxwindowsaudio.cat','driver/TimoxWindowsAudio.cat'),
        @('build_native_windows_audio/x64/Release/TimoxWindowsAudio.pdb','symbols/TimoxWindowsAudio.pdb'),
        @('build_native_lab_protocol/Release/NativeAudioDriverProbe.exe','probes/NativeAudioDriverProbe.exe'),
        @('build_native_lab_transport/Release/NativeAudioLabProbe.exe','probes/NativeAudioLabProbe.exe'),
        @('build_native_lab_device/Release/TimoxNativeLabDevice.exe','tools/TimoxNativeLabDevice.exe'),
        @('native-windows-audio/driver/THIRD_PARTY_NOTICES.md','licenses/THIRD_PARTY_NOTICES.md'),
        @('native-windows-audio/driver/upstream/LICENSE','licenses/SysVAD-MS-PL.txt'),
        @('native-windows-audio/lab-device/README.md','docs/lab-device.md'),
        @('tests/native_audio_lab_probe.md','docs/lab-probe.md'),
        @('docs/native-audio-vm-lab-runbook.md','docs/VM-RUNBOOK.md')
    )
}
function Assert-SafePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith('\\')) { throw 'UNC paths are not supported for lab staging.' }
    $current = $full
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path refused: $current" }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }; $current = $parent
    }
    return $full
}
function Read-SmallJson([string]$Path) {
    $null = Assert-SafePath $Path
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer -or $file.Length -gt 1048576) { throw 'Invalid or oversized JSON file.' }
    return ([IO.File]::ReadAllText($file.FullName) | ConvertFrom-Json)
}
function Get-TreeFiles([string]$Directory) {
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse package member: $($item.FullName)" }
        if ($item.PSIsContainer) { Get-TreeFiles $item.FullName } else { $item }
    }
}
function New-TimoxLabPackage {
    param([string]$RepositoryRoot, [string]$OutputDirectory, [object]$BuildEvidence, [scriptblock]$InspectBinary)
    $repo = (Assert-SafePath $RepositoryRoot).TrimEnd('\','/')
    $out = Assert-SafePath $OutputDirectory
    if (-not $out.StartsWith($repo+'\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be inside the supplied repository workspace.' }
    if (Test-Path -LiteralPath $out) { throw 'Output already exists; never overwrite a lab package.' }
    if ($null -eq $InspectBinary -or $BuildEvidence.schemaVersion -ne 1 -or @($BuildEvidence.artifacts).Count -ne 4) { throw 'Four verified build evidence records are required.' }
    $map = @(Get-PackageMap)
    $records = @(); $evidenceUsed = @()
    foreach ($pair in $map) {
        $source = Assert-SafePath (Join-Path $repo $pair[0])
        $item = Get-Item -LiteralPath $source -Force
        if ($item.PSIsContainer) { throw "Source is not a file: $source" }
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $record = [ordered]@{ path=$pair[1]; size=[long]$item.Length; sha256=$hash; source=$pair[0]; sourceLastWriteUtc=$item.LastWriteTimeUtc.ToString('o') }
        if ([IO.Path]::GetExtension($source) -in @('.exe','.sys')) {
            $matches = @($BuildEvidence.artifacts | Where-Object { $_.source -ceq $pair[0] })
            if ($matches.Count -ne 1) { throw "Missing or duplicate evidence: $($pair[0])" }
            $proof = $matches[0]
            if ($proof.sha256 -cne $hash -or $proof.sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
                $proof.sourceDirty -isnot [bool] -or $proof.sourceDirty -or $proof.buildLogSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
                [string]::IsNullOrWhiteSpace($proof.toolchain)) { throw "Invalid build evidence: $($pair[0])" }
            $null = [DateTimeOffset]::Parse($proof.builtAtUtc, [Globalization.CultureInfo]::InvariantCulture)
            $inspection = & $InspectBinary $source
            if ($inspection.architecture -ne 'x64' -or $inspection.signature -ne 'NotSigned') { throw "Expected unsigned x64 artifact: $source" }
            if ([IO.Path]::GetExtension($source) -eq '.exe') {
                if (@($inspection.imports).Count -eq 0) { throw 'No import evidence for executable.' }
                foreach ($dll in $inspection.imports) {
                    if ($dll -match '^(vcruntime|msvcp|concrt|ucrtbase|api-ms-win-crt|msvcr)') { throw "Dynamic CRT dependency refused: $dll" }
                }
            }
            $record.inspection = $inspection
            $evidenceUsed += $proof
        }
        if ([IO.Path]::GetExtension($source) -eq '.cat') {
            $inspection = & $InspectBinary $source
            if ($inspection.signature -ne 'NotSigned') { throw 'Unsigned preparation cannot reuse a signed catalogue.' }
        }
        $records += [pscustomobject]$record
    }
    foreach ($name in @('TimoxWindowsAudio.sys','TimoxWindowsAudio.inf')) {
        $control = Assert-SafePath (Join-Path $repo ('build_native_windows_audio/x64/Release/'+$name))
        $packaged = Join-Path $repo ('build_native_windows_audio/x64/Release/TimoxWindowsAudio/'+$name)
        if ((Get-FileHash -LiteralPath $control).Hash -ne (Get-FileHash -LiteralPath $packaged).Hash) { throw "Driver control copy mismatch: $name" }
    }
    # All inputs validated before creating the fresh destination. Failures retain
    # the incomplete directory for inspection; subsequent runs refuse it.
    New-Item -ItemType Directory -Path $out | Out-Null
    foreach ($record in $records) {
        $target = Join-Path $out $record.path
        $parent = [IO.Path]::GetDirectoryName($target)
        if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
        Copy-Item -LiteralPath (Join-Path $repo $record.source) -Destination $target
        if ((Get-FileHash -LiteralPath $target).Hash -cne $record.sha256) { throw "Source changed or copy corrupted: $($record.path)" }
    }
    $manifest = [ordered]@{ schemaVersion=1; state='unsigned-laboratory'; architecture='x64'; minimumWindowsBuild=22621;
        createdAtUtc=[DateTime]::UtcNow.ToString('o'); buildEvidence=$evidenceUsed; files=$records }
    [IO.File]::WriteAllText((Join-Path $out 'manifest-unsigned.json'), ($manifest | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding($false)))
    Test-TimoxLabPackage $out
}
function Test-TimoxLabPackage {
    param([string]$PackageDirectory)
    $root = (Assert-SafePath $PackageDirectory).TrimEnd('\','/')
    $manifest = Read-SmallJson (Join-Path $root 'manifest-unsigned.json')
    if ($manifest.schemaVersion -ne 1 -or $manifest.state -cne 'unsigned-laboratory' -or $manifest.architecture -cne 'x64' -or
        $manifest.minimumWindowsBuild -ne 22621) { throw 'Unsupported package manifest; signed validation is a separate future step.' }
    $expected = @((Get-PackageMap) | ForEach-Object { $_[1] })
    if (@($manifest.files).Count -ne $expected.Count) { throw 'Incomplete or oversized file manifest.' }
    $map = @(Get-PackageMap)
    if (@($manifest.buildEvidence).Count -ne 4) { throw 'Exactly four build evidence records required.' }
    $evidenceSeen = @{}
    $seen = @{}
    foreach ($entry in $manifest.files) {
        if ($entry.path -cnotin $expected -or $seen.ContainsKey($entry.path) -or $entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or $entry.size -lt 0) { throw 'Invalid or duplicate manifest member.' }
        $seen[$entry.path] = $true
        $sourceExpected = $null
        foreach ($pair in $map) { if ($pair[1] -ceq $entry.path) { $sourceExpected = $pair[0]; break } }
        if ($null -eq $sourceExpected -or $entry.source -cne $sourceExpected) { throw 'Manifest source does not match fixed package mapping.' }
        if ([IO.Path]::GetExtension($entry.path) -in @('.sys','.exe')) {
            $proofs = @($manifest.buildEvidence | Where-Object { $_.source -ceq $entry.source })
            if ($proofs.Count -ne 1 -or $evidenceSeen.ContainsKey($entry.source)) { throw 'Missing or duplicated binary evidence.' }
            $proof = $proofs[0]; $evidenceSeen[$entry.source] = $true
            if ($proof.sha256 -cne $entry.sha256 -or $proof.sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
                $proof.sourceDirty -isnot [bool] -or $proof.sourceDirty -or $proof.buildLogSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
                $proof.toolchain -isnot [string] -or [string]::IsNullOrWhiteSpace($proof.toolchain) -or $proof.toolchain.Length -gt 512) {
                throw 'Invalid binary provenance record.'
            }
            if ($proof.builtAtUtc -isnot [string] -or $proof.builtAtUtc -notmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?Z$') { throw 'Build date must be ISO UTC.' }
            $null = [DateTimeOffset]::Parse($proof.builtAtUtc, [Globalization.CultureInfo]::InvariantCulture)
            if ($entry.inspection.architecture -cne 'x64' -or $entry.inspection.signature -cne 'NotSigned') { throw 'Binary inspection conflicts with unsigned x64 package.' }
            if ([IO.Path]::GetExtension($entry.path) -eq '.exe') {
                if (@($entry.inspection.imports).Count -eq 0 -or @($entry.inspection.imports).Count -gt 256) { throw 'Missing or excessive executable import evidence.' }
                foreach ($dll in $entry.inspection.imports) {
                    if ($dll -isnot [string] -or $dll -notmatch '^[A-Za-z0-9_.-]+\.dll$' -or
                        $dll -match '^(vcruntime|msvcp|concrt|ucrtbase|api-ms-win-crt|msvcr)') { throw 'Invalid import or dynamic CRT dependency in manifest.' }
                }
            }
        }
        $file = Get-Item -LiteralPath (Assert-SafePath (Join-Path $root $entry.path)) -Force
        if ($file.PSIsContainer -or $file.Length -ne $entry.size -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne $entry.sha256) { throw "Package content mismatch: $($entry.path)" }
    }
    if ($evidenceSeen.Count -ne 4) { throw 'Build evidence does not exactly cover packaged binaries.' }
    $actual = @(Get-TreeFiles $root)
    if ($actual.Count -ne $expected.Count+1) { throw 'Unexpected files in package (keys and unlisted files are forbidden).' }
    foreach ($file in $actual) {
        $relative = $file.FullName.Substring($root.Length+1).Replace('\','/')
        if ($relative -cne 'manifest-unsigned.json' -and $relative -cnotin $expected) { throw 'Unexpected package member.' }
    }
    [pscustomobject]@{ valid=$true; state='unsigned-laboratory'; files=$expected.Count; packageDirectory=$root }
}
Export-ModuleMember -Function New-TimoxLabPackage, Test-TimoxLabPackage
