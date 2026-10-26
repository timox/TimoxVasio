#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='Medium')]
param(
    [Parameter(Mandatory=$true)][string]$UnsignedDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$SignToolPath,
    [Parameter(Mandatory=$true)][string]$Inf2CatPath,
    [string]$WdkPackagePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'vm-lab-signing.psm1') -Force
function Safe-LocalPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if($full -notmatch '^[A-Za-z]:\\'){throw 'Only local drive paths are supported.'}
    $part=$full
    while($part){
        if(Test-Path -LiteralPath $part){
            if((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse path refused: $part"}
        }
        $part=[IO.Path]::GetDirectoryName($part)
    }
    $full.TrimEnd('\')
}
function Check-Tool([string]$Path,[string]$Name) {
    $full=Safe-LocalPath $Path
    $item=Get-Item -LiteralPath $full
    $signature=Get-AuthenticodeSignature -LiteralPath $full
    if($item.PSIsContainer -or $item.Name -ine $Name -or $signature.Status -ne 'Valid' -or
       $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)'){throw "Expected Microsoft-signed $Name"}
    $full
}
$repo=& git -C (Join-Path $PSScriptRoot '../..') rev-parse --show-toplevel
if($LASTEXITCODE -ne 0){throw 'Cannot locate repository.'}
Write-Host "Repository: $repo"
& git -C $repo status --short
if($LASTEXITCODE -ne 0){throw 'Cannot inspect repository status.'}
$source=Safe-LocalPath $UnsignedDirectory
$destination=Safe-LocalPath $OutputDirectory
if($source -ieq $destination -or $destination.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -or
   $source.StartsWith($destination+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Source and destination must be disjoint.'}
if(Test-Path -LiteralPath $destination){throw 'Output already exists; refusing overwrite.'}
$null=& (Join-Path $PSScriptRoot 'test-vm-lab-package.ps1') -PackageDirectory $source
$signTool=Check-Tool $SignToolPath 'signtool.exe'
$toolProvenance=@{}
if($WdkPackagePath){
    $WdkPackagePath=Safe-LocalPath $WdkPackagePath
    $inf2CatEvidence=Get-TimoxPinnedInf2Cat -PackagePath $WdkPackagePath -ToolPath $Inf2CatPath
    $inf2Cat=$inf2CatEvidence.path
    $toolProvenance['Inf2Cat']=$inf2CatEvidence
}else{$inf2Cat=Check-Tool $Inf2CatPath 'inf2cat.exe'}
$driver=Join-Path $destination 'driver'
$plan=New-TimoxSigningPlan -Thumbprint $CertificateThumbprint -DriverDirectory $driver
$certificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$CertificateThumbprint)
Assert-TimoxSigningCertificate -Certificate $certificate -Thumbprint $CertificateThumbprint
$rsa=[Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
try {
    if($rsa -isnot [Security.Cryptography.RSACng] -or $rsa.Key.ExportPolicy -ne [Security.Cryptography.CngExportPolicies]::None){throw 'Only non-exportable RSA CNG signing keys are supported.'}
}finally{if($rsa){$rsa.Dispose()}}
$snapshot=Get-TimoxUnsignedSigningSnapshot -SourceDirectory $source
$publicCertificate=$certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert)
if([Convert]::ToBase64String($publicCertificate) -cne [Convert]::ToBase64String($certificate.RawData)){throw 'Certificate export differs from selected public identity.'}
if(-not $PSCmdlet.ShouldProcess($destination,'Copy unsigned package and test-sign SYS and CAT using existing CurrentUser key')){return}
$runner={
    param($ToolPath,$Arguments,$LogPath)
    if($WdkPackagePath){$null=Get-TimoxPinnedInf2Cat -PackagePath $WdkPackagePath -ToolPath $inf2Cat}
    Invoke-TimoxSigningCommand -ToolPath $ToolPath -Arguments $Arguments -LogPath $LogPath
    if($WdkPackagePath){$null=Get-TimoxPinnedInf2Cat -PackagePath $WdkPackagePath -ToolPath $inf2Cat}
    return 0
}
$inspector={param($Path) Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop}
# Incomplete transactions are retained for diagnosis; only a complete transaction publishes a manifest.
Invoke-TimoxSigningTransaction -Snapshot $snapshot -OutputDirectory $destination -Thumbprint $CertificateThumbprint -PublicCertificate $publicCertificate -Tools @{SignTool=$signTool;Inf2Cat=$inf2Cat} -RunCommand $runner -InspectSignature $inspector -ToolProvenance $toolProvenance
