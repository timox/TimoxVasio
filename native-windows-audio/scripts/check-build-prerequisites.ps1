[CmdletBinding()]
param(
    [string]$Workspace = (Join-Path $PSScriptRoot '../../build_native_audio_wdk'),
    [switch]$Json
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content (Join-Path $PSScriptRoot '../dependencies.json') -Raw | ConvertFrom-Json
$workspacePath = [IO.Path]::GetFullPath($Workspace)
$sample = Join-Path $workspacePath 'Windows-driver-samples'
$packages = Join-Path $sample 'packages'
$checks = [Collections.Generic.List[object]]::new()
function Add-Check([string]$Name, [bool]$Present, [string]$Detail) {
    $checks.Add([pscustomobject]@{ Name=$Name; Present=$Present; Detail=$Detail })
}
function Add-File([string]$Name, [string]$Path) {
    Add-Check $Name (Test-Path -LiteralPath $Path -PathType Leaf) $Path
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
Add-File 'vswhere' $vswhere
$visualStudio = $null
if (Test-Path -LiteralPath $vswhere) {
    $installations = @(& $vswhere -all -products '*' -format json -include packages | ConvertFrom-Json)
    $visualStudio = $installations | Where-Object {
        ([version]$_.installationVersion).Major -eq $manifest.visualStudioMajor -and $_.isComplete
    } | Sort-Object installationVersion -Descending | Select-Object -First 1
}
Add-Check 'VisualStudioVersion' ($null -ne $visualStudio) "Required major: $($manifest.visualStudioMajor)"
if ($visualStudio) {
    $vsPath = $visualStudio.installationPath
    Add-File 'MSBuildX64' (Join-Path $vsPath 'MSBuild/Current/Bin/amd64/MSBuild.exe')
    $wdkComponents = @($visualStudio.packages | Where-Object {
        $_.id -in @('Component.Microsoft.Windows.DriverKit', 'Microsoft.Windows.DriverKit',
            'Component.Microsoft.Windows.DriverKit.BuildTools')
    })
    Add-Check 'VisualStudioWdkComponent' ($wdkComponents.Count -gt 0) $vsPath
    Add-File 'KernelDriverToolset' (Join-Path $vsPath 'MSBuild/Microsoft/VC/v180/Platforms/x64/PlatformToolsets/WindowsKernelModeDriver10.0/Toolset.props')
    $defaultVersionFile = Join-Path $vsPath 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
    Add-File 'DefaultCompilerVersion' $defaultVersionFile
    if (Test-Path -LiteralPath $defaultVersionFile) {
        $compilerVersion = (Get-Content $defaultVersionFile -Raw).Trim()
        $compilerRoot = Join-Path $vsPath "VC/Tools/MSVC/$compilerVersion"
        Add-File 'CompilerX64' (Join-Path $compilerRoot 'bin/Hostx64/x64/cl.exe')
        Add-File 'SpectreRuntimeX64' (Join-Path $compilerRoot 'lib/spectre/x64/libcmt.lib')
    }
}
$kit = $manifest.kitVersion
$kitLayoutVersion = '10.0.28000.0' # directory version of the pinned 28000.2526 packages
$wdk = Join-Path $packages "Microsoft.Windows.WDK.x64.$kit"
$sdk = Join-Path $packages "Microsoft.Windows.SDK.CPP.$kit"
$sdkX64 = Join-Path $packages "Microsoft.Windows.SDK.CPP.x64.$kit"
Add-File 'WdkImport' (Join-Path $wdk 'build/native/Microsoft.Windows.WDK.x64.props')
Add-File 'KernelHeader' (Join-Path $wdk "c/Include/$kitLayoutVersion/km/ntddk.h")
Add-File 'PortClsHeader' (Join-Path $wdk "c/Include/$kitLayoutVersion/km/portcls.h")
Add-File 'PortClsLibrary' (Join-Path $wdk "c/Lib/$kitLayoutVersion/km/x64/portcls.lib")
Add-File 'InfVerif' (Join-Path $wdk "c/tools/$kitLayoutVersion/x64/infverif.exe")
Add-File 'Inf2Cat' (Join-Path $wdk "c/bin/$kitLayoutVersion/x86/Inf2Cat.exe")
Add-File 'StampInfX64' (Join-Path $wdk "c/bin/$kitLayoutVersion/x64/stampinf.exe")
Add-File 'SdkImport' (Join-Path $sdk 'build/native/Microsoft.Windows.SDK.cpp.props')
Add-File 'SdkX64Import' (Join-Path $sdkX64 'build/native/Microsoft.Windows.SDK.cpp.x64.props')
Add-File 'SysvadProject' (Join-Path $sample 'audio/sysvad/TabletAudioSample/TabletAudioSample.vcxproj')
Add-File 'SysvadLicense' (Join-Path $sample 'LICENSE')
if (Test-Path -LiteralPath (Join-Path $sample '.git')) {
    $commit = & git -C $sample rev-parse HEAD
    Add-Check 'SysvadPinnedCommit' ($LASTEXITCODE -eq 0 -and $commit -eq $manifest.sysvad.commit) "$commit"
    $changes = @(& git -C $sample status --porcelain --untracked-files=normal)
    Add-Check 'SysvadCleanCheckout' ($LASTEXITCODE -eq 0 -and $changes.Count -eq 0) 'Reference build requires unmodified upstream sources.'
} else { Add-Check 'SysvadPinnedCommit' $false 'No Git checkout' }
$present = @($checks | Where-Object { -not $_.Present }).Count -eq 0
$report = [pscustomobject]@{
    PrerequisiteFilesPresent=$present
    BuildProven=$false
    Note='Read-only inventory; only an actual driver build proves toolchain usability.'
    VisualStudioPath=if($visualStudio){$visualStudio.installationPath}else{$null}
    Workspace=$workspacePath
    Checks=$checks.ToArray()
}
if ($Json) { $report | ConvertTo-Json -Depth 5 } else { $checks | Format-Table -AutoSize; $report.Note }
if (-not $present) { exit 2 }
