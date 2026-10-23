#requires -Version 5.1
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/vm-lab-package.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../scripts/vm-lab-signing.psm1') -Force
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('TimoxPackageTests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$failures = 0
function Check([bool]$Value, [string]$Message) { if (-not $Value) { $script:failures++; Write-Host "FAIL: $Message" } }
function Refuses([scriptblock]$Operation, [string]$Message) {
    $refused = $false; try { & $Operation | Out-Null } catch { $refused = $true }; Check $refused $Message
}
$paths = @(
    'build_native_windows_audio/x64/Release/TimoxWindowsAudio/TimoxWindowsAudio.sys',
    'build_native_windows_audio/x64/Release/TimoxWindowsAudio/TimoxWindowsAudio.inf',
    'build_native_windows_audio/x64/Release/TimoxWindowsAudio/timoxwindowsaudio.cat',
    'build_native_windows_audio/x64/Release/TimoxWindowsAudio.pdb',
    'build_native_windows_audio/x64/Release/TimoxWindowsAudio.sys',
    'build_native_windows_audio/x64/Release/TimoxWindowsAudio.inf',
    'build_native_lab_protocol/Release/NativeAudioDriverProbe.exe',
    'build_native_lab_transport/Release/NativeAudioLabProbe.exe',
    'build_native_lab_device/Release/TimoxNativeLabDevice.exe',
    'native-windows-audio/driver/THIRD_PARTY_NOTICES.md',
    'native-windows-audio/driver/upstream/LICENSE',
    'native-windows-audio/lab-device/README.md',
    'tests/native_audio_lab_probe.md',
    'docs/native-audio-vm-lab-runbook.md')
try {
    foreach ($relative in $paths) {
        $full = Join-Path $testRoot $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($full)) -Force | Out-Null
        [IO.File]::WriteAllText($full, 'fixture-' + [IO.Path]::GetFileName($full))
    }
    $evidence = [pscustomobject]@{ schemaVersion=1; artifacts=@() }
    foreach ($relative in @($paths[0], $paths[6], $paths[7], $paths[8])) {
        $evidence.artifacts += [pscustomobject]@{ source=$relative; sha256=(Get-FileHash (Join-Path $testRoot $relative)).Hash;
            sourceCommit=('a'*40); sourceDirty=$false; buildLogSha256=('b'*64); builtAtUtc='2026-10-22T19:30:00Z'; toolchain='MSVC test fixture' }
    }
    $inspector = { param($Path) [pscustomobject]@{ architecture='x64'; signature='NotSigned'; imports=@('KERNEL32.dll') } }
    $out = Join-Path $testRoot 'package'
    $built = $false
    try { New-TimoxLabPackage $testRoot $out $evidence $inspector | Out-Null; $built=$true } catch { Write-Host "Positive fixture: $_" }
    Check $built 'complete fixture assembles an unsigned package'
    if ($built) {
        Check (Test-Path (Join-Path $out 'manifest-unsigned.json')) 'manifest created'
        try { Test-TimoxLabPackage $out | Out-Null } catch { Check $false 'fresh package passes exact file/hash validation' }
        Refuses { New-TimoxLabPackage $testRoot $out $evidence $inspector } 'existing destination never overwritten'
        $manifestPath = Join-Path $out 'manifest-unsigned.json'
        $manifestText = [IO.File]::ReadAllText($manifestPath)
        $badManifest = $manifestText | ConvertFrom-Json
        $badManifest.files = @($badManifest.files | Select-Object -Skip 1)
        [IO.File]::WriteAllText($manifestPath, ($badManifest | ConvertTo-Json -Depth 10))
        Refuses { Test-TimoxLabPackage $out } 'incomplete manifest refused'
        [IO.File]::WriteAllText($manifestPath, $manifestText)
        foreach ($case in @('missing-evidence','wrong-source','wrong-hash','bad-commit','dirty','bad-date','no-toolchain','bad-architecture','bad-signature','dynamic-import')) {
            $badManifest = $manifestText | ConvertFrom-Json
            $binary = @($badManifest.files | Where-Object { $_.path -eq 'probes/NativeAudioDriverProbe.exe' })[0]
            switch ($case) {
                'missing-evidence' { $badManifest.PSObject.Properties.Remove('buildEvidence') }
                'wrong-source' { $badManifest.buildEvidence[0].source = 'build_other/alien.sys' }
                'wrong-hash' { $badManifest.buildEvidence[0].sha256 = ('0'*64) }
                'bad-commit' { $badManifest.buildEvidence[0].sourceCommit = 'not-a-commit' }
                'dirty' { $badManifest.buildEvidence[0].sourceDirty = $true }
                'bad-date' { $badManifest.buildEvidence[0].builtAtUtc = 'never' }
                'no-toolchain' { $badManifest.buildEvidence[0].toolchain = '' }
                'bad-architecture' { $binary.inspection.architecture = 'arm64' }
                'bad-signature' { $binary.inspection.signature = 'Valid' }
                'dynamic-import' { $binary.inspection.imports = @('VCRUNTIME140.dll') }
            }
            [IO.File]::WriteAllText($manifestPath, ($badManifest | ConvertTo-Json -Depth 10))
            Refuses { Test-TimoxLabPackage $out } "manifest provenance/inspection contradiction refused: $case"
        }
        [IO.File]::WriteAllText($manifestPath, $manifestText)
        try {
            $snapshot=Get-TimoxUnsignedSigningSnapshot -SourceDirectory $out
            $copy=Join-Path $testRoot 'signing-copy'
            Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory $copy
            Check ((Test-TimoxLabPackage $copy).valid) 'signing copy preserves verified unsigned package'
            $lowerManifest=$manifestText | ConvertFrom-Json
            foreach($entry in $lowerManifest.files){$entry.sha256=$entry.sha256.ToLowerInvariant()}
            foreach($entry in $lowerManifest.buildEvidence){$entry.sha256=$entry.sha256.ToLowerInvariant()}
            [IO.File]::WriteAllText($manifestPath,($lowerManifest | ConvertTo-Json -Depth 10))
            try {
                $lowerSnapshot=Get-TimoxUnsignedSigningSnapshot -SourceDirectory $out
                Copy-TimoxUnsignedSigningSnapshot -Snapshot $lowerSnapshot -OutputDirectory (Join-Path $testRoot 'lowercase-copy')
            }finally{[IO.File]::WriteAllText($manifestPath,$manifestText)}
            Refuses {Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory $copy} 'signing copy refuses existing destination'
            Refuses {Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory (Join-Path $out 'nested')} 'signing copy refuses nested destination'
            $junction=Join-Path $testRoot 'signing-junction'
            New-Item -ItemType Junction -Path $junction -Target $copy | Out-Null
            try {
                Refuses {Get-TimoxUnsignedSigningSnapshot -SourceDirectory $junction} 'snapshot refuses reparse source'
                Refuses {Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory (Join-Path $junction 'child')} 'copy refuses reparse destination ancestor'
            }finally{[IO.Directory]::Delete($junction)}
            $mutated=Join-Path $out 'docs/lab-device.md'
            $savedBytes=[IO.File]::ReadAllBytes($mutated)
            try {
                [IO.File]::AppendAllText($mutated,'changed after snapshot')
                Refuses {Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory (Join-Path $testRoot 'changed-copy')} 'copy refuses source changed since snapshot'
            }finally{[IO.File]::WriteAllBytes($mutated,$savedBytes)}
            [IO.File]::WriteAllText($manifestPath,($manifestText+"`n"))
            try {
                Refuses {Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory (Join-Path $testRoot 'manifest-copy')} 'copy refuses even a valid manifest changed since snapshot'
            }finally{[IO.File]::WriteAllText($manifestPath,$manifestText)}
        }catch{Check $false "signing snapshot/copy fixture: $_"}
        $extra = Join-Path $out 'unlisted-private-key.pem'
        . (Join-Path $PSScriptRoot 'vm_lab_transaction_fixture.ps1')
        Test-SigningTransactions -SourceDirectory $out -TestRoot $testRoot
        [IO.File]::WriteAllText($extra, 'NOT A KEY: fake extra file')
        Refuses { Test-TimoxLabPackage $out } 'extra unlisted file refused'
        if($null -ne (Get-Variable snapshot -ErrorAction SilentlyContinue)){
            Refuses {Copy-TimoxUnsignedSigningSnapshot -Snapshot $snapshot -OutputDirectory (Join-Path $testRoot 'extra-copy')} 'copy refuses files added after snapshot'
        }
        Remove-Item -LiteralPath $extra
        [IO.File]::AppendAllText((Join-Path $out 'probes/NativeAudioDriverProbe.exe'), 'corrupt')
        Refuses { Test-TimoxLabPackage $out } 'one byte mutation invalidates package'
    }
    $missing = Join-Path $testRoot $paths[7]; Move-Item -LiteralPath $missing -Destination ($missing+'.saved')
    Refuses { New-TimoxLabPackage $testRoot (Join-Path $testRoot 'missing') $evidence $inspector } 'missing MT source refused'
    Move-Item -LiteralPath ($missing+'.saved') -Destination $missing
    [IO.File]::AppendAllText((Join-Path $testRoot $paths[4]), 'mismatch')
    Refuses { New-TimoxLabPackage $testRoot (Join-Path $testRoot 'mismatch') $evidence $inspector } 'driver control copy mismatch refused'
    [IO.File]::WriteAllText((Join-Path $testRoot $paths[4]), 'fixture-TimoxWindowsAudio.sys')
    Refuses { New-TimoxLabPackage $testRoot (Join-Path $testRoot 'noevidence') ([pscustomobject]@{schemaVersion=1;artifacts=@()}) $inspector } 'missing build evidence refused'
    $dynamic = { param($Path) [pscustomobject]@{ architecture='x64';signature='NotSigned';imports=@('VCRUNTIME140.dll') } }
    Refuses { New-TimoxLabPackage $testRoot (Join-Path $testRoot 'dynamic') $evidence $dynamic } 'dynamic MSVC runtime refused'
    $arm = { param($Path) [pscustomobject]@{ architecture='arm64';signature='NotSigned';imports=@('KERNEL32.dll') } }
    Refuses { New-TimoxLabPackage $testRoot (Join-Path $testRoot 'arm') $evidence $arm } 'non x64 binary refused'
    $link = Join-Path $testRoot 'linked-root'
    New-Item -ItemType Junction -Path $link -Target (Join-Path $testRoot 'docs') | Out-Null
    try { Refuses { New-TimoxLabPackage $link (Join-Path $link 'package') $evidence $inspector } 'reparse source root refused' }
    finally { [IO.Directory]::Delete($link) }
} finally {
    # Only the independently generated GUID directory is removed; verify containment first.
    $absolute = [IO.Path]::GetFullPath($testRoot)
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $absolute.StartsWith($temp, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($absolute) -notmatch '^TimoxPackageTests-[0-9a-f]{32}$') { throw 'Unsafe test cleanup path' }
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
Write-Host "Package fixture failures=$failures (no driver execution, signature or installation)"
if ($failures) { exit 1 }
