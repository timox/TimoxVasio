#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/vm-lab-signing.psm1') -Force
$failures=0
function Check([bool]$Condition,[string]$Message) {
    if(-not $Condition){$script:failures++;Write-Host "FAIL: $Message"}
}
function Refuses([scriptblock]$Action,[string]$Message) {
    $rejected=$false;try {& $Action | Out-Null}catch{$rejected=$true}
    Check $rejected $Message
}
try {
    $plan=New-TimoxSigningPlan -Thumbprint ('A'*40) -DriverDirectory 'C:\fixture\driver'
    Check ($plan.state -ceq 'signed-candidate') 'Output must remain a candidate'
    Check ($plan.vmVerified -is [bool] -and -not $plan.vmVerified) 'No host trust qualification'
    Check ($plan.commands.Count -eq 3) 'Exactly SYS, Inf2Cat, CAT commands'
    Check ($plan.commands[0].arguments[-1] -ceq 'C:\fixture\driver\TimoxWindowsAudio.sys') 'SYS signed first'
    Check ($plan.commands[1].tool -ceq 'Inf2Cat') 'Catalog regenerated after SYS'
    Check ($plan.commands[1].arguments -contains '/os:10_GE_X64') 'Exact Windows 26100 target'
    Check ($plan.commands[2].arguments[-1] -ceq 'C:\fixture\driver\TimoxWindowsAudio.cat') 'New CAT signed last'
    Check ($plan.commands[0].arguments -contains '/sha1') 'Exact certificate selection'
    Check (-not ($plan.commands[0].arguments -contains '/sm')) 'CurrentUser only'
}catch{$failures++;Write-Host "FAIL: positive plan: $_"}
Refuses {New-TimoxSigningPlan -Thumbprint 'bad' -DriverDirectory 'C:\fixture\driver'} 'Reject malformed certificate pin'
Refuses {New-TimoxSigningPlan -Thumbprint ('A'*40) -DriverDirectory 'C:\fixture\driver' -TargetOs '10_NI_X64'} 'Reject unsupported target'
Refuses {New-TimoxSigningPlan -Thumbprint ('A'*40) -DriverDirectory '\\server\driver'} 'Reject network signing directory'
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('timox-signing-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    $log=Join-Path $scratch 'success.log'
    try {
        Invoke-TimoxSigningCommand -ToolPath $env:ComSpec -Arguments @('/d','/c','echo fixture-output & echo fixture-error 1>&2 & exit /b 0') -LogPath $log
        $content=[IO.File]::ReadAllText($log)
        Check ($content.Contains('fixture-output') -and $content.Contains('fixture-error')) 'Capture stdout and stderr under Windows PowerShell 5.1'
    }catch{$failures++;Write-Host "FAIL: successful command logging: $_"}
    Refuses {Invoke-TimoxSigningCommand -ToolPath $env:ComSpec -Arguments @('/d','/c','exit /b 7') -LogPath (Join-Path $scratch 'failure.log')} 'Nonzero native exit must stop signing'
    Refuses {Invoke-TimoxSigningCommand -ToolPath $env:ComSpec -Arguments @('/d','/c','echo output') -LogPath $scratch} 'Log write failure must stop signing even with native exit zero'
    $appendLog=Join-Path $scratch 'append-failure.log'
    # The command makes the already-created log read-only before emitting output.
    # This exercises Add-Content, not the initial Set-Content preflight.
    Refuses {Invoke-TimoxSigningCommand -ToolPath $env:ComSpec -Arguments @('/d','/c',('attrib +R "'+$appendLog+'" & echo append-must-fail & exit /b 0')) -LogPath $appendLog} 'Append failure after launch must terminate despite Continue preference'
    Refuses {Invoke-TimoxSigningCommand -ToolPath (Join-Path $scratch 'missing.exe') -Arguments @() -LogPath (Join-Path $scratch 'missing.log')} 'Missing executable must not inherit stale successful exit'
    foreach($failureStep in 1..3){
        $visited=0
        $rejected=$false
        try {
            foreach($step in 1..3){
                $visited++
                $exitCode=if($step -eq $failureStep){7}else{0}
                Invoke-TimoxSigningCommand -ToolPath $env:ComSpec -Arguments @('/d','/c',("exit /b $exitCode")) -LogPath (Join-Path $scratch "sequence-$failureStep-$step.log")
            }
        }catch{$rejected=$true}
        Check ($rejected -and $visited -eq $failureStep) "Stop at failed command $failureStep without running following commands"
    }
    # An empty native output must still leave a journal for the evidence inventory.
    $emptyLog=Join-Path $scratch 'empty.log'
    Invoke-TimoxSigningCommand -ToolPath $env:ComSpec -Arguments @('/d','/c','exit /b 0') -LogPath $emptyLog
    Check (Test-Path -LiteralPath $emptyLog -PathType Leaf) 'Silent successful command leaves a log'
}finally{
    # Only remove the exact unique fixture directory created above.
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
Write-Host "Signing fixtures failures: $failures"
if($failures){exit 1}
