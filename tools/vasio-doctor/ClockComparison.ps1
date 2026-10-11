function Invoke-ClockPair {
    param([string]$ProbePath, [string]$PrimaryDriver, [string]$ReferenceDriver,
        [int]$Seconds, [string]$ThresholdsPath, [string]$ProfilesRoot,
        [string]$SafePrimary, [string]$SafeReference)

    $specs = @(
        [ordered]@{ driver = $PrimaryDriver; report = (Join-Path $ProfilesRoot "$SafePrimary.against-$SafeReference.primary.json") },
        [ordered]@{ driver = $ReferenceDriver; report = (Join-Path $ProfilesRoot "$SafePrimary.against-$SafeReference.reference.json") }
    )
    $running = [System.Collections.Generic.List[object]]::new()
    try {
        foreach ($spec in $specs) {
            $start = [System.Diagnostics.ProcessStartInfo]::new()
            $start.FileName = $ProbePath
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardOutput = $true
            $start.RedirectStandardError = $true
            foreach ($argument in @('cadence', '--driver', $spec.driver, '--seconds',
                $Seconds.ToString(), '--thresholds', $ThresholdsPath, '--output', $spec.report)) {
                [void]$start.ArgumentList.Add($argument)
            }
            $process = [System.Diagnostics.Process]::Start($start)
            $running.Add([ordered]@{ driver = $spec.driver; report = $spec.report;
                process = $process; stdout = $process.StandardOutput.ReadToEndAsync();
                stderr = $process.StandardError.ReadToEndAsync(); exitCode = $null })
        }
        Start-Sleep -Seconds 2
        foreach ($item in $running) {
            if ($item.process.HasExited -and $item.process.ExitCode -ne 0) {
                throw "Clock probe exited early: $($item.driver) ($($item.process.ExitCode))"
            }
        }
        foreach ($item in $running) {
            if (-not $item.process.WaitForExit(($Seconds + 120) * 1000)) {
                throw "Clock probe timed out: $($item.driver)"
            }
            $item.exitCode = $item.process.ExitCode
        }
        return [ordered]@{ primaryReport = $running[0].report;
            referenceReport = $running[1].report;
            primaryExitCode = $running[0].exitCode;
            referenceExitCode = $running[1].exitCode }
    } finally {
        foreach ($item in $running) {
            if (-not $item.process.HasExited) {
                $item.process.Kill($true)
                [void]$item.process.WaitForExit(10000)
            }
            [IO.File]::WriteAllText($item.report + '.stdout.txt',
                $item.stdout.GetAwaiter().GetResult())
            [IO.File]::WriteAllText($item.report + '.stderr.txt',
                $item.stderr.GetAwaiter().GetResult())
            $item.process.Dispose()
        }
    }
}
