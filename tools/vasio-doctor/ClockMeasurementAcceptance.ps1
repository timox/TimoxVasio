function Get-ClockMeasurementAcceptance {
    param(
        [Parameter(Mandatory)][int]$ExitCode,
        [AllowNull()][object]$Report
    )
    if ($null -eq $Report) {
        return [ordered]@{ usable = $false; regression = $null;
            reason = 'No clock cadence report was produced.' }
    }
    if ($ExitCode -ne 0 -or $Report.verdict -ne 'pass') {
        return [ordered]@{ usable = $false; regression = $null;
            reason = "Clock cadence verdict is $($Report.verdict), exit code $ExitCode." }
    }
    if (-not $Report.clockRegression -or -not $Report.clockRegression.available) {
        return [ordered]@{ usable = $false; regression = $null;
            reason = 'Clock cadence passed, but the QPC regression is unavailable.' }
    }
    return [ordered]@{ usable = $true; regression = $Report.clockRegression;
        reason = $null }
}
