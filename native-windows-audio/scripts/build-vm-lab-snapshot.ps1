#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SourceCommit = '5909035e4fbe6212a0f24a5ced94fa8ddac2270b',
    [string]$SnapshotDirectory = 'build_native_vm_source',
    [string]$VisualStudioDirectory = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Parent runs this explicitly. No elevation, installation, signing or devnode calls.
$repo = (& git -C (Join-Path $PSScriptRoot '../..') rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify repository.' }
& git -C $repo status --short
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect current repository status.' }
if ($SourceCommit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Require a complete commit hash.' }
& git -C $repo cat-file -e ($SourceCommit + '^{commit}')
if ($LASTEXITCODE -ne 0) { throw 'Unknown snapshot commit.' }
if ($SnapshotDirectory -notmatch '^build_native_vm_source(?:-[A-Za-z0-9_-]+)?$') {
    throw 'SnapshotDirectory must be a direct child directory name, without path separators.'
}
$snapshot = [IO.Path]::GetFullPath((Join-Path $repo $SnapshotDirectory))
if (-not $snapshot.StartsWith(([IO.Path]::GetFullPath($repo).TrimEnd('\')+'\'), [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($snapshot) -notmatch '^build_native_vm_source(?:-[A-Za-z0-9_-]+)?$') { throw 'Snapshot must be a dedicated build_native_vm_source directory inside this repository.' }
if (Test-Path -LiteralPath $snapshot) { throw 'Snapshot already exists; never delete, overwrite or reuse it implicitly.' }
$packages = Join-Path $repo 'build_native_audio_wdk/Windows-driver-samples/packages'
$msbuild = Join-Path $VisualStudioDirectory 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$dumpbin = Join-Path $VisualStudioDirectory 'VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/dumpbin.exe'
foreach ($tool in @($msbuild,$dumpbin)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing tool: $tool" }
    $signature = Get-AuthenticodeSignature -LiteralPath $tool
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') { throw "Untrusted tool: $tool" }
}
if (-not (Test-Path -LiteralPath (Join-Path $packages 'Microsoft.Windows.WDK.x64.10.0.28000.2526/build/native/Microsoft.Windows.WDK.x64.props'))) { throw 'Pinned WDK packages missing; no download fallback.' }
# A detached worktree gives the snapshot its own Git identity. Existing edits in
# the working repository remain untouched, including other agents' TLS changes.
& git -C $repo worktree add --detach $snapshot $SourceCommit
if ($LASTEXITCODE -ne 0) { throw 'Snapshot checkout failed; inspect it without automatic cleanup.' }
$logs = Join-Path $snapshot 'build_native_vm_evidence'
New-Item -ItemType Directory -Path $logs | Out-Null
function Assert-CleanSnapshot {
    $head = (& git -C $snapshot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $SourceCommit) { throw 'Snapshot commit changed.' }
    $changes = @(& git -C $snapshot status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $changes.Count -ne 0) { throw 'Snapshot source is not clean; refusing clean-source evidence.' }
}
function Invoke-Logged([string]$Executable, [string[]]$Arguments, [string]$LogName) {
    Assert-CleanSnapshot
    $log = Join-Path $logs $LogName
    ('Executable: ' + $Executable + "`r`nArguments: " + ($Arguments -join ' ') + "`r`nCommit: " + $SourceCommit) |
        Set-Content -LiteralPath $log -Encoding UTF8
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        # Tee-Object appends UTF-16 under Windows PowerShell 5.1, mixing encodings
        # with the UTF-8 header. Keep one explicit encoding for every log write.
        & $Executable @Arguments 2>&1 | ForEach-Object {
            $line = [string]$_
            Add-Content -LiteralPath $log -Value $line -Encoding UTF8 -ErrorAction Stop
            Write-Host $line
        }
        $commandExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($commandExit -ne 0) { throw "$LogName failed with exit code $commandExit; no package produced." }
    Assert-CleanSnapshot
}
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$records = @()
function Record-Build([string]$Relative, [string]$LogName, [string]$Toolchain) {
    $binary = Join-Path $snapshot $Relative
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing build output: $Relative" }
    $script:records += [pscustomobject]@{
        source=$Relative; sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash;
        sourceCommit=$SourceCommit; sourceDirty=$false;
        buildLogSha256=(Get-FileHash -LiteralPath (Join-Path $logs $LogName) -Algorithm SHA256).Hash;
        builtAtUtc=[DateTime]::UtcNow.ToString('o'); toolchain=$Toolchain
    }
}
Invoke-Logged $msbuild @((Join-Path $snapshot 'native-windows-audio/driver/TimoxWindowsAudio.vcxproj'),
    '/t:Rebuild','/p:Configuration=Release','/p:Platform=x64','/p:PreferredToolArchitecture=x64','/p:SignMode=Off',
    "/p:TimoxPackages=$packages",'/m:1','/v:normal') 'driver.log'
Record-Build 'build_native_windows_audio/x64/Release/TimoxWindowsAudio/TimoxWindowsAudio.sys' 'driver.log' 'VS2026 MSVC14.51; WDK/SDK NuGet10.0.28000.2526; x64 Release unsigned'
foreach ($entry in @(
    @('native-windows-audio','build_native_lab_protocol','NativeAudioDriverProbe','protocol',@()),
    @('.','build_native_lab_transport','NativeAudioLabProbe','transport',@('-DVASIO_BUILD_NATIVE_TRANSPORT_TESTS_ONLY=ON')),
    @('native-windows-audio/lab-device','build_native_lab_device','TimoxNativeLabDevice','device',@())
)) {
    $build = Join-Path $snapshot $entry[1]
    $configureArguments = @('-S',(Join-Path $snapshot $entry[0]),'-B',$build,'-G','Visual Studio 18 2026','-A','x64','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded') + $entry[4]
    Invoke-Logged $cmake $configureArguments ($entry[3]+'-configure.log')
    Invoke-Logged $cmake @('--build',$build,'--config','Release','--target',$entry[2],'--','/m:1') ($entry[3]+'.log')
    Record-Build ($entry[1]+'/Release/'+$entry[2]+'.exe') ($entry[3]+'.log') 'VS2026 MSVC14.51; x64 Release /MT; CMake Visual Studio 18 2026'
}
Invoke-Logged $dumpbin @('/nologo','/pdbpath:verbose',(Join-Path $snapshot 'build_native_windows_audio/x64/Release/TimoxWindowsAudio.sys')) 'pdb-match.log'
# Review pdb-match.log for an actual matching PDB; exit zero alone is not proof.
$evidence = [ordered]@{ schemaVersion=1; artifacts=$records;
    tools=@(@{path=$msbuild;sha256=(Get-FileHash -LiteralPath $msbuild).Hash},
        @{path=$dumpbin;sha256=(Get-FileHash -LiteralPath $dumpbin).Hash},
        @{path=$cmake;sha256=(Get-FileHash -LiteralPath $cmake).Hash});
    sourceSnapshot=$snapshot; sourceMethod='git detached worktree'; dependenciesPath=$packages;
    limitations='Shared pinned WDK package directory; build attestation, not a reproducible-build proof. PDB matching log needs review.' }
$evidencePath = Join-Path $logs 'evidence.json'
[IO.File]::WriteAllText($evidencePath, ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
Assert-CleanSnapshot
# Run the committed snapshot's packaging entry point, not the edited parent tree.
& (Join-Path $snapshot 'native-windows-audio/scripts/prepare-vm-lab-package.ps1') `
    -OutputDirectory (Join-Path $snapshot 'build_native_vm_package_unsigned') -BuildEvidencePath $evidencePath -DumpbinPath $dumpbin
& (Join-Path $snapshot 'native-windows-audio/scripts/test-vm-lab-package.ps1') -PackageDirectory (Join-Path $snapshot 'build_native_vm_package_unsigned')
Assert-CleanSnapshot
Write-Output "Unsigned package: $snapshot\build_native_vm_package_unsigned"
Write-Output "Evidence/logs: $logs"
Write-Output 'No signing or installation performed. Snapshot is retained; no automatic worktree removal.'
