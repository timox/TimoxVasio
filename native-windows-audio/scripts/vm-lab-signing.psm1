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
function Get-TimoxUnsignedSigningSnapshot {
    param([string]$SourceDirectory)
    $ErrorActionPreference='Stop'
    Import-Module (Join-Path $PSScriptRoot 'vm-lab-package.psm1')
    $source=[IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\')
    if($source -notmatch '^[A-Za-z]:\\'){throw 'Only local drive paths are supported.'}
    $part=$source
    while($part){
        if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Reparse source refused.'}
        $part=[IO.Path]::GetDirectoryName($part)
    }
    $manifestPath=Join-Path $source 'manifest-unsigned.json'
    $manifestHash=(Get-FileHash -LiteralPath $manifestPath -ErrorAction Stop).Hash
    $manifest=[IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
    $null=Test-TimoxLabPackage -PackageDirectory $source
    if((Get-FileHash -LiteralPath $manifestPath -ErrorAction Stop).Hash -cne $manifestHash){throw 'Unsigned manifest changed during validation.'}
    $initial=@{'manifest-unsigned.json'=$manifestHash}
    foreach($entry in $manifest.files){$initial[$entry.path.Replace('/','\')]=$entry.sha256.ToUpperInvariant()}
    [pscustomobject]@{source=$source;hashes=$initial}
}
function Copy-TimoxUnsignedSigningSnapshot {
    param($Snapshot,[string]$OutputDirectory)
    $ErrorActionPreference='Stop'
    $source=$Snapshot.source
    $destination=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
    if($destination -notmatch '^[A-Za-z]:\\' -or $source -ieq $destination -or
       $destination.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $source.StartsWith($destination+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Signing copy requires disjoint local directories.'}
    $part=$destination
    while($part){
        if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Reparse destination refused.'}
        $part=[IO.Path]::GetDirectoryName($part)
    }
    if(Test-Path -LiteralPath $destination){throw 'Output already exists; refusing overwrite.'}
    $current=Get-TimoxUnsignedSigningSnapshot -SourceDirectory $source
    foreach($relative in $Snapshot.hashes.Keys){
        if($current.hashes[$relative] -cne $Snapshot.hashes[$relative]){throw "Source changed since validation: $relative"}
    }
    if($current.hashes.Count -ne $Snapshot.hashes.Count){throw 'Source inventory changed since validation.'}
    New-Item -ItemType Directory -Path $destination | Out-Null
    # Copy only the pinned manifest inventory, never a fresh recursive file list.
    foreach($relative in $Snapshot.hashes.Keys){
        $target=Join-Path $destination $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $source $relative) -Destination $target
        if((Get-FileHash -LiteralPath $target).Hash -cne $Snapshot.hashes[$relative]){throw "Source changed or copy corrupted: $relative"}
    }
    $null=Test-TimoxLabPackage -PackageDirectory $destination
}
function Assert-TimoxSigningCertificate {
    param($Certificate,[string]$Thumbprint,[DateTime]$Now=[DateTime]::UtcNow)
    if($Thumbprint -notmatch '^[0-9a-fA-F]{40}$' -or $null -eq $Certificate -or
       $Certificate.Thumbprint -ine $Thumbprint){throw 'Exact signing certificate pin required.'}
    if(-not $Certificate.HasPrivateKey -or $Certificate.NotBefore.ToUniversalTime() -gt $Now.ToUniversalTime() -or
       $Certificate.NotAfter.ToUniversalTime() -le $Now.ToUniversalTime()){throw 'Certificate missing private key or outside validity interval.'}
    $eku=@($Certificate.Extensions | Where-Object {$_.Oid.Value -eq '2.5.29.37'})
    $usage=@($Certificate.Extensions | Where-Object {$_.Oid.Value -eq '2.5.29.15'})
    if($eku.Count -ne 1 -or $usage.Count -ne 1){throw 'Exactly one explicit EKU and KeyUsage extension required.'}
    $decodedEku=New-Object Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension
    $decodedEku.CopyFrom($eku[0])
    if('1.3.6.1.5.5.7.3.3' -notin @($decodedEku.EnhancedKeyUsages | ForEach-Object {$_.Value})){throw 'Explicit code-signing EKU required.'}
    $decodedUsage=New-Object Security.Cryptography.X509Certificates.X509KeyUsageExtension
    $decodedUsage.CopyFrom($usage[0])
    if(-not ($decodedUsage.KeyUsages -band [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature)){throw 'DigitalSignature KeyUsage required.'}
    # This checks certificate metadata only; the wrapper must also open the
    # actual private key and enforce the non-exportable RSA CNG policy.
}
function Invoke-TimoxSigningCommand {
    param([string]$ToolPath,[string[]]$Arguments,[string]$LogPath)
    $saved=$ErrorActionPreference
    try {
        # Create even an empty log, and fail before invoking the tool if it cannot be written.
        Set-Content -LiteralPath $LogPath -Value '' -Encoding UTF8 -ErrorAction Stop
        $global:LASTEXITCODE=$null
        # PS5.1 represents redirected native stderr as ErrorRecord objects.
        # Continue accepts those diagnostics; filesystem errors must still terminate.
        $ErrorActionPreference='Continue'
        & $ToolPath @Arguments 2>&1 | ForEach-Object {Add-Content -LiteralPath $LogPath -Value ([string]$_) -Encoding UTF8 -ErrorAction Stop;Write-Host $_}
        $code=$global:LASTEXITCODE
    }finally{$ErrorActionPreference=$saved}
    if($null -eq $code -or $code -ne 0){throw "Signing command failed with exit '$code'. Candidate is incomplete."}
}
function Assert-TimoxToolArchiveContents {
    param([string]$ArchivePath,[string]$EntryPrefix,[string]$ToolDirectory)
    $ErrorActionPreference='Stop'
    foreach($path in @($ArchivePath,$ToolDirectory)){
        $part=[IO.Path]::GetFullPath($path)
        if($part -notmatch '^[A-Za-z]:\\'){throw 'Only local tool paths are supported.'}
        while($part){
            if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Reparse tool path refused.'}
            $part=[IO.Path]::GetDirectoryName($part)
        }
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ArchivePath))
    $expected=@{}
    try {
        foreach($entry in $archive.Entries){
            if(-not $entry.FullName.StartsWith($EntryPrefix,[StringComparison]::Ordinal) -or $entry.FullName.EndsWith('/')){continue}
            $name=$entry.FullName.Substring($EntryPrefix.Length)
            if($name -notmatch '^[A-Za-z0-9_.-]+$' -or $name -in @('.','..') -or $expected.ContainsKey($name)){throw 'Unsupported or duplicate tool archive member.'}
            $stream=$entry.Open()
            $sha=[Security.Cryptography.SHA256]::Create()
            try {$expected[$name]=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$sha.Dispose();$stream.Dispose()}
        }
    }finally{$archive.Dispose()}
    $files=@(Get-ChildItem -LiteralPath $ToolDirectory -Force)
    if($expected.Count -eq 0 -or $files.Count -ne $expected.Count){throw 'Tool directory inventory differs from signed archive.'}
    foreach($file in $files){
        if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
           -not $expected.ContainsKey($file.Name) -or (Get-FileHash -LiteralPath $file.FullName).Hash -cne $expected[$file.Name]){throw 'Tool directory content differs from signed archive.'}
    }
}
function Get-TimoxPinnedInf2Cat {
    param([string]$PackagePath,[string]$ToolPath)
    $ErrorActionPreference='Stop'
    # Exact distribution verified by NuGet with Microsoft's author identity.
    # See the signing plan for provenance. This is an artifact pin, not a
    # replacement implementation of NuGet/Authenticode signature verification.
    $packageHash='C393D03DFB640B5C92F546B32F6770EF68CD3AAF691956E7D66D8E2C28A1B55E'
    if((Get-FileHash -LiteralPath $PackagePath).Hash -cne $packageHash){throw 'Unapproved WDK package; exact verified distribution required.'}
    $tool=[IO.Path]::GetFullPath($ToolPath)
    if([IO.Path]::GetFileName($tool) -ine 'Inf2Cat.exe'){throw 'Expected pinned Inf2Cat executable.'}
    Assert-TimoxToolArchiveContents -ArchivePath $PackagePath -EntryPrefix 'c/bin/10.0.26100.0/x86/' -ToolDirectory ([IO.Path]::GetDirectoryName($tool))
    if((Get-FileHash -LiteralPath $PackagePath).Hash -cne $packageHash){throw 'WDK package changed during validation.'}
    [pscustomobject]@{path=$tool;method='pinned-microsoft-nuget-artifact';package='Microsoft.Windows.WDK.x64/10.0.26100.6584';packageSha256=$packageHash;authorCertificateSha256='566A31882BE208BE4422F7CFD66ED09F5D4524A5994F50CCC8B05EC0528C1353'}
}
function Get-TimoxSigningTreeFiles([string]$Directory) {
    foreach($item in Get-ChildItem -LiteralPath $Directory -Force -ErrorAction Stop){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse candidate member refused.'}
        if($item.PSIsContainer){Get-TimoxSigningTreeFiles $item.FullName}else{$item}
    }
}
function Invoke-TimoxSigningTransaction {
    param($Snapshot,[string]$OutputDirectory,[string]$Thumbprint,[byte[]]$PublicCertificate,
          [hashtable]$Tools,[scriptblock]$RunCommand,[scriptblock]$InspectSignature,[hashtable]$ToolProvenance=@{})
    $ErrorActionPreference='Stop'
    if(-not $RunCommand -or -not $InspectSignature -or -not $PublicCertificate.Length){throw 'Signing adapters and public identity are required.'}
    $destination=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
    $driver=Join-Path $destination 'driver'
    $plan=New-TimoxSigningPlan -Thumbprint $Thumbprint -DriverDirectory $driver
    $toolHashes=@{}
    foreach($name in @('SignTool','Inf2Cat')){$toolHashes[$name]=(Get-FileHash -LiteralPath $Tools[$name]).Hash}
    Copy-TimoxUnsignedSigningSnapshot -Snapshot $Snapshot -OutputDirectory $destination
    $initial=$Snapshot.hashes
    $logDirectory=Join-Path $destination 'signing-logs'
    New-Item -ItemType Directory -Path $logDirectory | Out-Null
    Remove-Item -LiteralPath (Join-Path $driver 'TimoxWindowsAudio.cat')
    $step=0
    foreach($command in $plan.commands){
        $step++
        $tool=$Tools[$command.tool]
        if((Get-FileHash -LiteralPath $tool).Hash -cne $toolHashes[$command.tool]){throw 'Signing tool changed before execution.'}
        $code=& $RunCommand $tool $command.arguments (Join-Path $logDirectory "step-$step.log")
        if($null -eq $code -or $code -is [array] -or $code -ne 0){throw "Signing step $step failed. Candidate is incomplete."}
    }
    $publicIdentity=[Convert]::ToBase64String($PublicCertificate)
    $signatureObservations=@()
    foreach($name in @('TimoxWindowsAudio.sys','TimoxWindowsAudio.cat')){
        $signature=& $InspectSignature (Join-Path $driver $name)
        if($null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ine $Thumbprint -or
           [Convert]::ToBase64String($signature.SignerCertificate.RawData) -cne $publicIdentity){throw "Unexpected signer: $name"}
        # PowerShell reports CERT_E_UNTRUSTEDROOT as UnknownError on this host.
        # Match only the OS-provided message for that HRESULT; do not accept
        # arbitrary UnknownError. This remains identity/diagnostic evidence,
        # never proof of signature validity, trust or catalog membership.
        $untrustedRoot=$signature.Status -eq 'UnknownError' -and
            $signature.StatusMessage.Trim() -ceq (New-Object ComponentModel.Win32Exception(-2146762487)).Message.Trim()
        if($signature.Status -notin @('Valid','NotTrusted') -and -not $untrustedRoot){throw "Invalid signature structure/status: $name"}
        $signatureObservations+=@([pscustomobject]@{file=$name;status=[string]$signature.Status;reportedUntrustedRoot=$untrustedRoot;vmVerificationRequired=$true})
    }
    foreach($relative in $initial.Keys){
        if($relative -in @('driver\TimoxWindowsAudio.sys','driver\TimoxWindowsAudio.cat')){continue}
        if((Get-FileHash -LiteralPath (Join-Path $destination $relative)).Hash -cne $initial[$relative]){throw "Unexpected content change: $relative"}
    }
    $publicPath=Join-Path $destination 'signer.cer'
    if(Test-Path -LiteralPath $publicPath){throw 'Unexpected preexisting certificate export.'}
    [IO.File]::WriteAllBytes($publicPath,$PublicCertificate)
    if([Convert]::ToBase64String([IO.File]::ReadAllBytes($publicPath)) -cne $publicIdentity){throw 'Public certificate export mismatch.'}
    $expected=@($initial.Keys)+@('signer.cer','signing-logs\step-1.log','signing-logs\step-2.log','signing-logs\step-3.log')
    $files=@(Get-TimoxSigningTreeFiles $destination)
    if($files.Count -ne $expected.Count){throw 'Unexpected or missing candidate files.'}
    $records=@(foreach($file in $files){
        $relative=$file.FullName.Substring($destination.Length+1)
        if($relative -notin $expected){throw "Unexpected candidate member: $relative"}
        [pscustomobject]@{path=$relative.Replace('\','/');size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}
    })
    $toolRecords=@(foreach($name in @('SignTool','Inf2Cat')){
        if((Get-FileHash -LiteralPath $Tools[$name]).Hash -cne $toolHashes[$name]){throw 'Signing tool changed during transaction.'}
        [pscustomobject]@{name=$name;sha256=$toolHashes[$name];provenance=$ToolProvenance[$name]}
    })
    $result=[ordered]@{
        schemaVersion=1;state='signed-candidate';vmVerified=$false;targetOs='10_GE_X64';createdUtc=[DateTime]::UtcNow.ToString('o')
        unsignedManifestSha256=$initial['manifest-unsigned.json'];certificateThumbprint=$Thumbprint.ToUpperInvariant()
        certificateSha256=(Get-FileHash -LiteralPath $publicPath).Hash;tools=$toolRecords
        hostSignatureObservations=$signatureObservations
        originalDriverHashes=@{sys=$initial['driver\TimoxWindowsAudio.sys'];cat=$initial['driver\TimoxWindowsAudio.cat']}
        files=$records;qualification='Signature trust, catalog membership, kernel policy and audio remain to be verified in VM. Manifest is an inventory, not a signed document.'
    }
    # Publish only after all validation succeeds; partial writes keep a temporary name.
    $temporary=Join-Path $destination 'manifest-signed.json.pending'
    [IO.File]::WriteAllText($temporary,($result | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
    [IO.File]::Move($temporary,(Join-Path $destination 'manifest-signed.json'))
    [pscustomobject]@{state='signed-candidate';vmVerified=$false;directory=$destination;certificateSha256=$result.certificateSha256}
}
Export-ModuleMember -Function New-TimoxSigningPlan,Invoke-TimoxSigningCommand,Assert-TimoxSigningCertificate,Get-TimoxUnsignedSigningSnapshot,Copy-TimoxUnsignedSigningSnapshot,Invoke-TimoxSigningTransaction,Assert-TimoxToolArchiveContents,Get-TimoxPinnedInf2Cat
