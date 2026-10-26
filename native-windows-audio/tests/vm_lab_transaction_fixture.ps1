#requires -Version 5.1
# Invoked by the package suite with its real, validated synthetic unsigned package.
function Test-SigningTransactions([string]$SourceDirectory,[string]$TestRoot) {
    $snapshot=Get-TimoxUnsignedSigningSnapshot -SourceDirectory $SourceDirectory
    $tool=Join-Path $TestRoot 'fixture-tool.bin'
    [IO.File]::WriteAllText($tool,'synthetic tool, never executed')
    $tools=@{SignTool=$tool;Inf2Cat=$tool}
    $publicBytes=[byte[]]@(1,2,3,4) # Synthetic identity bytes, not a certificate.
    foreach($case in @('success','untrusted-root','unknown-error','step1','step2','step3','runner-throws','wrong-signer','wrong-der','bad-status','changed-inf','extra-file','changed-tool','missing-log','missing-cat','reparse-member')){
        $destination=Join-Path $TestRoot ('transaction-'+$case)
        $state=@{calls=0}
        $runner={
            param($ToolPath,$Arguments,$LogPath)
            $state.calls++
            [IO.File]::WriteAllText($LogPath,'fixture step '+$state.calls)
            if($case -eq 'runner-throws'){throw 'Simulated native invocation failure'}
            if($case -eq ('step'+$state.calls)){return 7}
            if($state.calls -eq 2){
                [IO.File]::WriteAllText((Join-Path $destination 'driver/TimoxWindowsAudio.cat'),'new catalog')
            }else{[IO.File]::AppendAllText($Arguments[-1],' signed')}
            if($state.calls -eq 3){
                switch($case){
                    'changed-inf' {[IO.File]::AppendAllText((Join-Path $destination 'driver/TimoxWindowsAudio.inf'),'changed')}
                    'extra-file' {[IO.File]::WriteAllText((Join-Path $destination 'unexpected.pem'),'not a key')}
                    'changed-tool' {[IO.File]::AppendAllText($ToolPath,'changed')}
                    'missing-log' {Remove-Item -LiteralPath $LogPath}
                    'missing-cat' {Remove-Item -LiteralPath (Join-Path $destination 'driver/TimoxWindowsAudio.cat')}
                    'reparse-member' {New-Item -ItemType Junction -Path (Join-Path $destination 'junction') -Target (Join-Path $destination 'driver') | Out-Null}
                }
            }
            return 0
        }.GetNewClosure()
        $inspector={
            param($Path)
            $pin=if($case -eq 'wrong-signer'){'B'*40}else{'A'*40}
            $der=if($case -eq 'wrong-der'){[byte[]]@(9,9)}else{$publicBytes}
            $status=if($case -eq 'bad-status'){'HashMismatch'}elseif($case -in @('untrusted-root','unknown-error')){'UnknownError'}else{'NotTrusted'}
            $message=if($case -eq 'untrusted-root'){(New-Object ComponentModel.Win32Exception(-2146762487)).Message}else{'fixture diagnostic'}
            [pscustomobject]@{Status=$status;StatusMessage=$message;SignerCertificate=[pscustomobject]@{Thumbprint=$pin;RawData=$der}}
        }.GetNewClosure()
        $succeeded=$false
        try {
            $result=Invoke-TimoxSigningTransaction -Snapshot $snapshot -OutputDirectory $destination -Thumbprint ('A'*40) -PublicCertificate $publicBytes -Tools $tools -RunCommand $runner -InspectSignature $inspector
            $succeeded=$true
        }catch{if($case -eq 'success'){Write-Host "Transaction positive fixture: $_"}}
        $expectedSuccess=$case -in @('success','untrusted-root')
        Check ($succeeded -eq $expectedSuccess) "Transaction outcome: $case"
        $manifestPath=Join-Path $destination 'manifest-signed.json'
        Check ((Test-Path -LiteralPath $manifestPath) -eq $expectedSuccess) "Publish manifest only on complete success: $case"
        if($expectedSuccess -and $succeeded){
            $manifest=[IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
            Check ($manifest.state -ceq 'signed-candidate' -and $manifest.vmVerified -is [bool] -and -not $manifest.vmVerified) 'Transaction remains unqualified by VM'
            Check ($manifest.hostSignatureObservations.Count -eq 2) 'Both host signature diagnostics retained'
            foreach($observation in $manifest.hostSignatureObservations){
                Check ($observation.vmVerificationRequired -eq $true) 'Host diagnostics never qualify VM signatures'
                if($case -eq 'untrusted-root'){Check ($observation.reportedUntrustedRoot -eq $true -and $observation.status -eq 'UnknownError') 'Untrusted root diagnostic is explicit in candidate'}
            }
            Check ($manifest.files.Count -eq ($snapshot.hashes.Count+4)) 'Inventory covers original files, public certificate and three logs'
            foreach($entry in $manifest.files){
                Check ((Get-FileHash -LiteralPath (Join-Path $destination $entry.path)).Hash -ceq $entry.sha256) "Final evidence hash: $($entry.path)"
            }
        }
        if($case -match '^step([123])$'){
            Check ($state.calls -eq [int]$Matches[1]) "No following command after $case"
            Check (Test-Path -LiteralPath (Join-Path $destination 'signing-logs/step-1.log')) "Failed transaction keeps diagnostics: $case"
        }
        if($case -eq 'runner-throws'){Check ($state.calls -eq 1) 'Thrown invocation error prevents following commands'}
        if($case -eq 'reparse-member'){
            $junction=Join-Path $destination 'junction'
            if(Test-Path -LiteralPath $junction){[IO.Directory]::Delete($junction)}
        }
        [IO.File]::WriteAllText($tool,'synthetic tool, never executed')
    }
    $null=Test-TimoxLabPackage -PackageDirectory $SourceDirectory
}
