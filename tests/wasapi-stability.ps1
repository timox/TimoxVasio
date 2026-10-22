# Pure snapshot evaluator. Dot-source from the orchestrator or offline tests.
# It deliberately does not infer an event-free interval from fill/ppm values.
function Get-WasapiStabilitySample {
    # BridgeIds order is trusted orchestration data: render, endpointLoopback.
    param($Snapshot, [string[]]$BridgeIds, $Baseline = $null, $Previous = $null, [double]$GapSeconds = 1)
    $issues = [Collections.Generic.List[string]]::new()
    $bridges = [Collections.Generic.List[object]]::new()
    if ($null -ne $Baseline -and $null -eq $Previous) { $Previous = $Baseline }
    if ($null -ne $Previous -and (-not [double]::IsFinite($GapSeconds) -or $GapSeconds -le 0 -or $GapSeconds -gt 2.5)) {
        $issues.Add("Snapshot gap outside (0, 2.5] seconds: $GapSeconds")
    }
    $counters = @('droppedCaptureFrames', 'rejectedFrames', 'renderSilencedInputFrames',
        'silencedFrames', 'timestampErrors', 'discontinuities', 'renderSilencedDeviceFrames')
    $available = if ($null -ne $Snapshot -and $Snapshot.PSObject.Properties['wasapiBridges']) {
        @($Snapshot.wasapiBridges)
    } else { @() }
    foreach ($bridgeId in $BridgeIds) {
        $matches = @($available | Where-Object { $_.PSObject.Properties['id'] -and $_.id -eq $bridgeId })
        if ($matches.Count -ne 1) { $issues.Add("${bridgeId}: missing or duplicate bridge"); continue }
        $bridge = $matches[0]
        $expectedKind = if ($bridgeId -eq $BridgeIds[0]) { 'render' } else { 'endpointLoopback' }
        if (-not $bridge.PSObject.Properties['effectiveInfo'] -or $null -eq $bridge.effectiveInfo -or
            -not $bridge.effectiveInfo.PSObject.Properties['kind'] -or $bridge.effectiveInfo.kind -ne $expectedKind) {
            $issues.Add("${bridgeId}: unexpected/missing effective stream kind")
        }
        $generation = if ($bridge.PSObject.Properties['generationId']) { $bridge.generationId } else { $null }
        if ($generation -isnot [string] -or [string]::IsNullOrWhiteSpace($generation) -or $generation -eq '0') {
            $issues.Add("${bridgeId}: missing/invalid generationId")
        }
        if (-not $bridge.PSObject.Properties['state'] -or $bridge.state -ne 'running') {
            $issues.Add("${bridgeId}: state is not running")
        }
        $health = if ($bridge.PSObject.Properties['health']) { $bridge.health } else { $null }
        if ($null -eq $health) { $issues.Add("${bridgeId}: missing health"); continue }
        if (-not $health.PSObject.Properties['error'] -or $null -eq $health.error -or $health.error -ne 0) {
            $issues.Add("${bridgeId}: missing/nonzero error")
        }
        if (-not $health.PSObject.Properties['primingTimedOut'] -or
            $health.primingTimedOut -isnot [bool] -or $health.primingTimedOut) {
            $issues.Add("${bridgeId}: priming timed out or diagnostic missing")
        }
        $values = [ordered]@{}
        $nativeName = if ($expectedKind -eq 'render') { 'renderedFrames' } else { 'capturedFrames' }
        $nativeFrames = $null
        try {
            if (-not $health.PSObject.Properties[$nativeName] -or $null -eq $health.$nativeName) { throw 'missing counter' }
            $nativeFrames = [decimal]$health.$nativeName
            if ($nativeFrames -lt 0 -or [decimal]::Truncate($nativeFrames) -ne $nativeFrames) { throw 'invalid counter' }
        } catch { $issues.Add("${bridgeId}: missing/invalid $nativeName"); $nativeFrames = $null }
        if ($null -ne $Previous) {
            $last = @($Previous.bridges | Where-Object id -EQ $bridgeId)
            if ($last.Count -ne 1 -or $null -eq $last[0].nativeFrames -or $null -eq $nativeFrames) {
                $issues.Add("${bridgeId}: native progression unavailable")
            } elseif ($nativeFrames -le $last[0].nativeFrames) {
                $issues.Add("${bridgeId}: $nativeName did not progress since preceding snapshot")
            }
        }
        foreach ($name in $counters) {
            if (-not $health.PSObject.Properties[$name] -or $null -eq $health.$name) {
                $issues.Add("${bridgeId}: missing $name"); continue
            }
            try {
                $value = [decimal]$health.$name
                if ($value -lt 0 -or [decimal]::Truncate($value) -ne $value) { throw 'invalid counter' }
                $values[$name] = $value
            } catch { $issues.Add("${bridgeId}: invalid $name") }
        }
        if ($null -ne $Baseline) {
            $baselineBridge = @($Baseline.bridges | Where-Object id -EQ $bridgeId)
            if ($baselineBridge.Count -ne 1) { $issues.Add("${bridgeId}: missing baseline") }
            else {
                if ($generation -ne $baselineBridge[0].generationId) { $issues.Add("${bridgeId}: generation changed") }
                foreach ($name in $counters) {
                    if ($values.Contains($name) -and $baselineBridge[0].counters.Contains($name)) {
                        $delta = $values[$name] - $baselineBridge[0].counters[$name]
                        if ($delta -ne 0) { $issues.Add("${bridgeId}: $name delta=$delta") }
                    }
                }
            }
        }
        $bridges.Add([pscustomobject]@{ id = $bridgeId; generationId = $generation; counters = $values; nativeFrames = $nativeFrames })
    }
    [pscustomobject]@{ ready = $issues.Count -eq 0 -and $BridgeIds.Count -eq 2;
        issues = @($issues.ToArray()); bridges = @($bridges.ToArray()) }
}
