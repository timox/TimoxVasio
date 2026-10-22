#requires -Version 5.1
Set-StrictMode -Version Latest
function New-TimoxSigningPlan {
    param([string]$Thumbprint,[string]$DriverDirectory,[string]$TargetOs='10_GE_X64')
    if($Thumbprint -notmatch '^[0-9a-fA-F]{40}$'){throw 'Expected exact certificate SHA-1 thumbprint.'}
    if($TargetOs -cne '10_GE_X64'){throw 'Only Windows 11 24H2 x64 is supported.'}
    if($DriverDirectory -notmatch '^[A-Za-z]:\\' -or $DriverDirectory.Contains('..')){throw 'Expected absolute local driver directory.'}
    $sys=Join-Path $DriverDirectory 'TimoxWindowsAudio.sys'
    $cat=Join-Path $DriverDirectory 'TimoxWindowsAudio.cat'
    [pscustomobject]@{state='signed-candidate';vmVerified=$false;commands=@(
        [pscustomobject]@{tool='SignTool';arguments=@('sign','/v','/s','My','/sha1',$Thumbprint,'/fd','SHA256',$sys)},
        [pscustomobject]@{tool='Inf2Cat';arguments=@(('/driver:'+$DriverDirectory),'/os:10_GE_X64','/verbose')},
        [pscustomobject]@{tool='SignTool';arguments=@('sign','/v','/s','My','/sha1',$Thumbprint,'/fd','SHA256',$cat)}
    )}
}
Export-ModuleMember -Function New-TimoxSigningPlan
