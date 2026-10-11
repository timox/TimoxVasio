param(
    [Parameter(Mandatory)][string]$SendEvents,
    [Parameter(Mandatory)][string]$ReceiveEvents,
    [string]$ThresholdsPath = (Join-Path $PSScriptRoot '..\tools\vasio-probe\thresholds.json')
)

$ErrorActionPreference = 'Stop'
$maxPendingSenderBlocks = [int]((Get-Content -LiteralPath $ThresholdsPath -Raw |
    ConvertFrom-Json).loopback.maxAnalysisPendingBlocks)
if ($maxPendingSenderBlocks -lt 1) { throw 'loopback.maxAnalysisPendingBlocks must be positive.' }
$sender = [System.IO.StreamReader]::new((Resolve-Path -LiteralPath $SendEvents).Path)
$receiver = [System.IO.StreamReader]::new((Resolve-Path -LiteralPath $ReceiveEvents).Path)
function Next-AudioEvent([System.IO.StreamReader]$Reader) {
    while (($line = $Reader.ReadLine()) -ne $null) {
        $event = $line | ConvertFrom-Json
        if ($event.kind -eq 1) { return $event }
    }
    return $null
}
try {
    $senderByCounter = @{}
    $pendingSender = [System.Collections.Generic.Queue[object]]::new()
    $nextSender = Next-AudioEvent $sender
    [long]$matched = 0
    [long]$unmatched = 0
    [long]$latencySum = 0
    [long]$peakPendingSenderBlocks = 0
    [long]$minimum = [long]::MaxValue
    [long]$maximum = [long]::MinValue
    while (($received = Next-AudioEvent $receiver) -ne $null) {
        while ($null -ne $nextSender -and [long]$nextSender.qpc -le [long]$received.qpc) {
            $senderByCounter[[string]([long]$nextSender.counter % 1048576)] = $nextSender
            $pendingSender.Enqueue($nextSender)
            if ($pendingSender.Count -gt $peakPendingSenderBlocks) {
                $peakPendingSenderBlocks = $pendingSender.Count
            }
            if ($pendingSender.Count -gt $maxPendingSenderBlocks) {
                throw "Loopback latency analysis exceeded $maxPendingSenderBlocks pending sender blocks."
            }
            $nextSender = Next-AudioEvent $sender
        }
        if (-not $received.positionValid -or $received.preSync -gt 0) { continue }
        $key = [string]([long]$received.counter % 1048576)
        $emitted = $senderByCounter[$key]
        if ($null -eq $emitted -or -not $emitted.positionValid) {
            ++$unmatched
            continue
        }
        $latency = [long]$received.samplePosition - [long]$emitted.samplePosition
        ++$matched
        $latencySum += $latency
        if ($latency -lt $minimum) { $minimum = $latency }
        if ($latency -gt $maximum) { $maximum = $latency }
        # Earlier sender blocks cannot match a later, ordered receiver event.
        while ($pendingSender.Count -gt 0 -and
            [long]$pendingSender.Peek().qpc -le [long]$emitted.qpc) {
            $old = $pendingSender.Dequeue()
            $oldKey = [string]([long]$old.counter % 1048576)
            if ([object]::ReferenceEquals($senderByCounter[$oldKey], $old)) {
                $senderByCounter.Remove($oldKey)
            }
        }
    }
    if ($matched -eq 0) { throw 'No emitted block could be matched to a received block.' }
    [pscustomobject]@{
        matchedBlocks = $matched
        unmatchedBlocks = $unmatched
        latencyMinSamples = $minimum
        latencyMaxSamples = $maximum
        latencyMeanSamples = [double]$latencySum / [double]$matched
        latencyStable = $minimum -eq $maximum
        peakPendingSenderBlocks = $peakPendingSenderBlocks
    }
}
finally { $sender.Dispose(); $receiver.Dispose() }
