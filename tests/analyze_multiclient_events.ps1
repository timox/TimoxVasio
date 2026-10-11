function Get-MulticlientEventWindow {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][long]$StartQpc,
        [Parameter(Mandatory)][long]$KillQpc,
        [Parameter(Mandatory)][long]$StableQpc,
        [Parameter(Mandatory)][long]$EndQpc,
        [Parameter(Mandatory)][double]$TheoreticalMs,
        [Parameter(Mandatory)][long]$ExpectedFrames,
        [Parameter(Mandatory)][long]$QpcFrequency
    )
    $reader = [IO.StreamReader]::new((Resolve-Path -LiteralPath $Path).Path)
    $jitters = [System.Collections.Generic.List[double]]::new()
    [long]$beforeCount = 0
    [long]$afterCount = 0
    [long]$stableCount = 0
    [long]$beforeErrors = 0
    [long]$afterErrors = 0
    [long]$stableErrors = 0
    [long]$positionErrors = 0
    $previousAfter = $null
    try {
        while (($line = $reader.ReadLine()) -ne $null) {
            $event = $line | ConvertFrom-Json
            if ($event.kind -ne 1 -or $event.qpc -lt $StartQpc -or
                $event.qpc -gt $EndQpc) { continue }
            $errors = [long]$event.corrupted + [long]$event.missing + [long]$event.duplicated
            if ($event.qpc -lt $KillQpc) {
                ++$beforeCount
                $beforeErrors += $errors
                continue
            }
            if ($event.qpc -eq $KillQpc) { continue }
            ++$afterCount
            $afterErrors += $errors
            if ($event.qpc -ge $StableQpc) {
                ++$stableCount
                $stableErrors += $errors
            }
            if ($null -ne $previousAfter) {
                $intervalMs = ([long]$event.qpc - [long]$previousAfter.qpc) * 1000.0 /
                    $QpcFrequency
                $jitters.Add([Math]::Abs($intervalMs - $TheoreticalMs))
                if ([long]$event.samplePosition - [long]$previousAfter.samplePosition -ne $ExpectedFrames) {
                    ++$positionErrors
                }
            }
            $previousAfter = $event
        }
    }
    finally { $reader.Dispose() }
    $sorted = $jitters.ToArray()
    [Array]::Sort($sorted)
    $p99 = if ($sorted.Length) {
        $sorted[[Math]::Max(0, [Math]::Ceiling($sorted.Length * 0.99) - 1)]
    } else { 0.0 }
    return [ordered]@{
        beforeCount = $beforeCount; afterCount = $afterCount; stableCount = $stableCount;
        beforeErrors = $beforeErrors; afterErrors = $afterErrors; stableErrors = $stableErrors;
        cadence = [ordered]@{ intervals = $jitters.Count; jitterP99Ms = $p99;
            positionStepErrors = $positionErrors }
    }
}
