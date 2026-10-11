$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\vasio-doctor\AsioFormatAcceptance.ps1')

function Assert-State($Cycle, $Report, [int]$ExitCode, [string]$Expected) {
    $result = Get-AsioFormatAcceptance -Cycle $Cycle -Report $Report `
        -ExitCode $ExitCode -RequestedRate 48000
    if ($result.state -ne $Expected -or $result.usable -ne ($Expected -eq 'pass')) {
        throw "Format assessment was $($result.state), usable=$($result.usable); expected $Expected"
    }
}

$cycle = [pscustomobject]@{
    setRateStatus = 0
    confirmedRate = 48000
    callbackCount = 3
    stages = @(
        [pscustomobject]@{ name = 'createBuffers'; success = $true },
        [pscustomobject]@{ name = 'start'; success = $true }
    )
}
$report = [pscustomobject]@{ verdict = 'pass' }
Assert-State $cycle $report 0 'pass'

$cycle.setRateStatus = 1
Assert-State $cycle $report 1 'rate-refused'
$cycle.setRateStatus = 0
$cycle.stages[0].success = $false
Assert-State $cycle $report 1 'buffers-refused'
$cycle.stages[0].success = $true
$cycle.stages[1].success = $false
Assert-State $cycle $report 1 'start-failed'
$cycle.stages[1].success = $true
$cycle.callbackCount = 0
$report.verdict = 'inconclusive'
Assert-State $cycle $report 6 'inconclusive'
$cycle.callbackCount = 3
$report.verdict = 'fail'
Assert-State $cycle $report 1 'lifecycle-failed'

'ASIO format acceptance tests passed.'
