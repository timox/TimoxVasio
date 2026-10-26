#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/vm-lab-signing.psm1') -Force
$failures=0
$checks=0
$now=[DateTime]::UtcNow
function Fixture {
    $oids=New-Object Security.Cryptography.OidCollection
    $null=$oids.Add((New-Object Security.Cryptography.Oid('1.3.6.1.5.5.7.3.3')))
    $eku=New-Object Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension($oids,$false)
    $usage=New-Object Security.Cryptography.X509Certificates.X509KeyUsageExtension([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,$true)
    [pscustomobject]@{Thumbprint=('A'*40);HasPrivateKey=$true;NotBefore=$now.AddDays(-1);NotAfter=$now.AddDays(1);Extensions=@($eku,$usage)}
}
function Accepts($Certificate,[bool]$Expected,[string]$Label) {
    $script:checks++
    $accepted=$false
    try {Assert-TimoxSigningCertificate -Certificate $Certificate -Thumbprint ('A'*40) -Now $now;$accepted=$true}catch{if($Expected){Write-Host $_}}
    if($accepted -ne $Expected){$script:failures++;Write-Host "FAIL: $Label"}
}
Accepts (Fixture) $true 'Valid pinned certificate metadata'
Accepts $null $false 'Missing certificate'
$cert=Fixture;$cert.Thumbprint='B'*40;Accepts $cert $false 'Wrong certificate pin'
$cert=Fixture;$cert.HasPrivateKey=$false;Accepts $cert $false 'Public certificate alone'
$cert=Fixture;$cert.NotBefore=$now.AddSeconds(1);Accepts $cert $false 'Not yet valid'
$cert=Fixture;$cert.NotAfter=$now;Accepts $cert $false 'Expired at boundary'
$cert=Fixture;$cert.Extensions=@();Accepts $cert $false 'Explicit extensions required'
$cert=Fixture;$cert.Extensions=@($cert.Extensions[1]);Accepts $cert $false 'Missing code signing EKU'
$cert=Fixture;$cert.Extensions=@($cert.Extensions[0]);Accepts $cert $false 'Missing digital signature KeyUsage'
$cert=Fixture
$cert.Extensions[1]=New-Object Security.Cryptography.X509Certificates.X509KeyUsageExtension([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment,$true)
Accepts $cert $false 'Key encipherment cannot authorize signing'
$cert=Fixture;$cert.Extensions+=@($cert.Extensions[1]);Accepts $cert $false 'Duplicate KeyUsage'
$cert=Fixture;$cert.Extensions+=@($cert.Extensions[0]);Accepts $cert $false 'Duplicate EKU'
$cert=Fixture
$oids=New-Object Security.Cryptography.OidCollection
$null=$oids.Add((New-Object Security.Cryptography.Oid('1.3.6.1.5.5.7.3.1')))
$cert.Extensions[0]=New-Object Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension($oids,$false)
Accepts $cert $false 'Server authentication alone cannot authorize signing'
$cert=Fixture;$cert.Thumbprint=('a'*40);Accepts $cert $true 'Pin comparison accepts hexadecimal case'
$cert=Fixture;$cert.NotBefore=$now;Accepts $cert $true 'Validity starts at inclusive boundary'
$cert=Fixture
$cert.Extensions[1]=New-Object Security.Cryptography.X509Certificates.X509KeyUsageExtension(([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment),$true)
Accepts $cert $true 'Additional key usages do not remove signing permission'
foreach($oid in @('2.5.29.15','2.5.29.37')){
    $cert=Fixture
    $index=if($oid -eq '2.5.29.15'){1}else{0}
    $cert.Extensions[$index]=New-Object Security.Cryptography.X509Certificates.X509Extension((New-Object Security.Cryptography.Oid($oid)),([byte[]]@(255,255)),$true)
    Accepts $cert $false "Malformed extension $oid"
}
Write-Host "Certificate metadata checks=$checks failures=$failures (no certificate store or key creation)"
if($failures){exit 1}
