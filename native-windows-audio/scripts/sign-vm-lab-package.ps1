#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='Medium')]
param(
    [Parameter(Mandatory=$true)][string]$UnsignedDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$SignToolPath,
    [Parameter(Mandatory=$true)][string]$Inf2CatPath
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
$inf2Cat=Check-Tool $Inf2CatPath 'inf2cat.exe'
$driver=Join-Path $destination 'driver'
$plan=New-TimoxSigningPlan -Thumbprint $CertificateThumbprint -DriverDirectory $driver
$certificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$CertificateThumbprint)
$now=Get-Date
if(-not $certificate.HasPrivateKey -or $certificate.NotBefore -gt $now -or $certificate.NotAfter -le $now){throw 'Certificate missing private key or outside validity interval.'}
$eku=@($certificate.EnhancedKeyUsageList | ForEach-Object {$_.ObjectId.Value})
if('1.3.6.1.5.5.7.3.3' -notin $eku){throw 'Explicit code-signing EKU required.'}
$rsa=[Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
try {
    if($rsa -isnot [Security.Cryptography.RSACng] -or $rsa.Key.ExportPolicy -ne [Security.Cryptography.CngExportPolicies]::None){throw 'Only non-exportable RSA CNG signing keys are supported.'}
}finally{if($rsa){$rsa.Dispose()}}
$manifestPath=Join-Path $source 'manifest-unsigned.json'
$originalManifest=[IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
$sourceFiles=@(Get-ChildItem -LiteralPath $source -Recurse -File -Force)
$initial=@{}
foreach($file in $sourceFiles){$relative=$file.FullName.Substring($source.Length+1);$initial[$relative]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
if(-not $PSCmdlet.ShouldProcess($destination,'Copy unsigned package and test-sign SYS and CAT using existing CurrentUser key')){return}
New-Item -ItemType Directory -Path $destination | Out-Null
# Never delete an incomplete candidate automatically: preserve diagnostics.
foreach($file in $sourceFiles){
    $relative=$file.FullName.Substring($source.Length+1)
    $target=Join-Path $destination $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
    if((Get-FileHash -LiteralPath $target).Hash -cne $initial[$relative]){throw 'Source changed during copy.'}
}
$logDirectory=Join-Path $destination 'signing-logs'
New-Item -ItemType Directory -Path $logDirectory | Out-Null
$cat=Join-Path $driver 'TimoxWindowsAudio.cat'
Remove-Item -LiteralPath $cat
$step=0
foreach($command in $plan.commands){
    $step++
    $tool=if($command.tool -ceq 'SignTool'){$signTool}else{$inf2Cat}
    $log=Join-Path $logDirectory ("step-$step.log")
    $arguments=$command.arguments
    $saved=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $tool @arguments 2>&1 | ForEach-Object {Add-Content -LiteralPath $log -Value ([string]$_) -Encoding UTF8;Write-Host $_}
        $code=$LASTEXITCODE
    }finally{$ErrorActionPreference=$saved}
    if($code -ne 0){throw "Signing step $step failed with exit $code. Candidate is incomplete."}
}
foreach($name in @('TimoxWindowsAudio.sys','TimoxWindowsAudio.cat')){
    $signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $driver $name)
    if($null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ine $CertificateThumbprint){throw "Unexpected signer: $name"}
    # NotTrusted is expected without host trust. This is identity evidence only.
    if($signature.Status -notin @('Valid','NotTrusted')){throw "Invalid signature structure/status for $name : $($signature.Status)"}
}
foreach($relative in $initial.Keys){
    if($relative -in @('driver\TimoxWindowsAudio.sys','driver\TimoxWindowsAudio.cat')){continue}
    if((Get-FileHash -LiteralPath (Join-Path $destination $relative)).Hash -cne $initial[$relative]){throw "Unexpected content change: $relative"}
}
$publicPath=Join-Path $destination 'signer.cer'
[IO.File]::WriteAllBytes($publicPath,$certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
$records=@(Get-ChildItem -LiteralPath $destination -Recurse -File -Force | ForEach-Object {
    [pscustomobject]@{path=$_.FullName.Substring($destination.Length+1).Replace('\','/');size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$result=[ordered]@{
    schemaVersion=1;state='signed-candidate';vmVerified=$false;targetOs='10_GE_X64';createdUtc=[DateTime]::UtcNow.ToString('o')
    unsignedManifestSha256=$initial['manifest-unsigned.json'];certificateThumbprint=$CertificateThumbprint.ToUpperInvariant()
    certificateSha256=(Get-FileHash -LiteralPath $publicPath).Hash
    tools=@(@{name='SignTool';sha256=(Get-FileHash -LiteralPath $signTool).Hash},@{name='Inf2Cat';sha256=(Get-FileHash -LiteralPath $inf2Cat).Hash})
    originalDriverHashes=@{sys=$initial['driver\TimoxWindowsAudio.sys'];cat=$initial['driver\TimoxWindowsAudio.cat']}
    files=$records;qualification='Signature trust, catalog membership, kernel policy and audio remain to be verified in VM. Manifest is an inventory, not a signed document.'
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'manifest-signed.json') -Encoding UTF8
Write-Output ([pscustomobject]@{state='signed-candidate';vmVerified=$false;directory=$destination;certificateSha256=$result.certificateSha256})
