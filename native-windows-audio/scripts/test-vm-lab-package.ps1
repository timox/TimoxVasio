#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'vm-lab-package.psm1') -Force
$result = Test-TimoxLabPackage -PackageDirectory $PackageDirectory
foreach ($relative in @('driver/TimoxWindowsAudio.sys','driver/TimoxWindowsAudio.cat','probes/NativeAudioDriverProbe.exe','probes/NativeAudioLabProbe.exe','tools/TimoxNativeLabDevice.exe')) {
    if ((Get-AuthenticodeSignature -LiteralPath (Join-Path $PackageDirectory $relative)).Status -ne 'NotSigned') {
        throw "Unsigned verifier refuses a signed or unverifiable artifact: $relative"
    }
}
$result
