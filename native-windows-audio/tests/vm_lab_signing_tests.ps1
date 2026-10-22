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
Write-Host "Signing fixtures failures: $failures"
if($failures){exit 1}
