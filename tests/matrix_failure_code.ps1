function Test-MatrixEntryPass {
    param([Parameter(Mandatory)][object]$Entry)
    return [bool]($Entry.verdict -eq 'pass' -and
        $null -ne $Entry.exitCode -and [int]$Entry.exitCode -eq 0)
}

function Get-MatrixFailureCode {
    param(
        [Parameter(Mandatory)][object[]]$Results,
        [Parameter(Mandatory)][string]$DoctorVerdict
    )
    if ($DoctorVerdict -eq 'red') { return 7 }
    foreach ($result in $Results) {
        if ($result.mode -eq 'doctor') {
            if ($null -eq $result.exitCode -or [int]$result.exitCode -ne 0) { return 6 }
        } elseif (-not (Test-MatrixEntryPass -Entry $result)) { return 6 }
    }
    return 0
}
