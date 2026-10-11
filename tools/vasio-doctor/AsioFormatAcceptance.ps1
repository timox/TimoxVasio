function Get-AsioFormatAcceptance {
    param(
        [Parameter(Mandatory)]$Cycle,
        [Parameter(Mandatory)]$Report,
        [Parameter(Mandatory)][int]$ExitCode,
        [Parameter(Mandatory)][long]$RequestedRate
    )

    $buffersCreated = [bool]@($Cycle.stages | Where-Object name -eq 'createBuffers' | Select-Object -First 1)[0].success
    $started = [bool]@($Cycle.stages | Where-Object name -eq 'start' | Select-Object -First 1)[0].success
    $rateConfirmed = $Cycle.setRateStatus -eq 0 -and [long]$Cycle.confirmedRate -eq $RequestedRate
    $callbacksObserved = [long]$Cycle.callbackCount -gt 0
    $usable = $rateConfirmed -and $buffersCreated -and $started -and $callbacksObserved -and
        $ExitCode -eq 0 -and $Report.verdict -eq 'pass'

    $state = if (-not $rateConfirmed) { 'rate-refused' }
        elseif (-not $buffersCreated) { 'buffers-refused' }
        elseif (-not $started) { 'start-failed' }
        elseif (-not $callbacksObserved -or $Report.verdict -eq 'inconclusive') { 'inconclusive' }
        elseif ($usable) { 'pass' }
        else { 'lifecycle-failed' }

    [ordered]@{
        state = $state
        usable = $usable
        rateConfirmed = $rateConfirmed
        buffersCreated = $buffersCreated
        started = $started
        callbacksObserved = $callbacksObserved
    }
}
