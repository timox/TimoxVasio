function Invoke-ProbeProcess(
    [string]$FilePath,
    [string[]]$Arguments,
    [int]$TimeoutSeconds,
    [string]$StageLogPath = '',
    [double]$MaxStageMs = 0,
    [string]$OutputLogPath = ''
) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    if ($OutputLogPath) {
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
    }
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $deadline = [System.Diagnostics.Stopwatch]::GetTimestamp() +
            [int64]($TimeoutSeconds * [System.Diagnostics.Stopwatch]::Frequency)
        while (-not $process.WaitForExit(100)) {
            $now = [System.Diagnostics.Stopwatch]::GetTimestamp()
            if ($now -ge $deadline) {
                $process.Kill($true)
                [void]$process.WaitForExit(10000)
                throw "vasio-probe timed out after $TimeoutSeconds seconds: $($Arguments -join ' ')"
            }
            if ($StageLogPath -and $MaxStageMs -gt 0 -and (Test-Path -LiteralPath $StageLogPath)) {
                try {
                    $last = Get-Content -LiteralPath $StageLogPath -Tail 1 -ErrorAction Stop | ConvertFrom-Json
                    if ($last.phase -eq 'start') {
                        $elapsedMs = ($now - [int64]$last.qpc) * 1000.0 /
                            [System.Diagnostics.Stopwatch]::Frequency
                        if ($elapsedMs -gt $MaxStageMs + 500) {
                            $process.Kill($true)
                            [void]$process.WaitForExit(10000)
                            throw "ASIO stage $($last.stage) exceeded $MaxStageMs ms in cycle $($last.cycle)"
                        }
                    }
                }
                catch {
                    if ($_.Exception.Message -like 'ASIO stage *') { throw }
                    # The producer may be writing the last JSONL line; retry on the next poll.
                }
            }
        }
        return $process.ExitCode
    }
    finally {
        if ($OutputLogPath -and $process.HasExited) {
            [IO.File]::WriteAllText("$OutputLogPath.stdout.txt", $process.StandardOutput.ReadToEnd())
            [IO.File]::WriteAllText("$OutputLogPath.stderr.txt", $process.StandardError.ReadToEnd())
        }
        $process.Dispose()
    }
}
