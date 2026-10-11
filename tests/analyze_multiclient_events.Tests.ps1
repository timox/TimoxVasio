$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'analyze_multiclient_events.ps1')

$path = Join-Path $env:TEMP ('vasio-multiclient-events-' + [Guid]::NewGuid().ToString('N') + '.jsonl')
try {
    @(
        '{"kind":1,"qpc":100,"samplePosition":0,"corrupted":0,"missing":0,"duplicated":0}',
        '{"kind":1,"qpc":110,"samplePosition":10,"corrupted":0,"missing":0,"duplicated":0}',
        '{"kind":1,"qpc":120,"samplePosition":20,"corrupted":1,"missing":0,"duplicated":0}',
        '{"kind":1,"qpc":130,"samplePosition":30,"corrupted":0,"missing":1,"duplicated":0}',
        '{"kind":1,"qpc":140,"samplePosition":40,"corrupted":0,"missing":0,"duplicated":0}',
        '{"kind":1,"qpc":150,"samplePosition":50,"corrupted":0,"missing":0,"duplicated":0}',
        '{"kind":2,"qpc":155,"value":3}',
        '{"kind":1,"qpc":160,"samplePosition":60,"corrupted":0,"missing":0,"duplicated":0}'
    ) | Set-Content -LiteralPath $path
    $result = Get-MulticlientEventWindow -Path $path -StartQpc 100 -KillQpc 125 `
        -StableQpc 145 -EndQpc 160 -TheoreticalMs 10 -ExpectedFrames 10 -QpcFrequency 1000
    if ($result.beforeCount -ne 3 -or $result.beforeErrors -ne 1 -or
        $result.afterCount -ne 4 -or $result.afterErrors -ne 1 -or
        $result.stableCount -ne 2 -or $result.stableErrors -ne 0 -or
        $result.cadence.intervals -ne 3 -or $result.cadence.jitterP99Ms -ne 0 -or
        $result.cadence.positionStepErrors -ne 0) {
        throw "Unexpected streamed multiclient summary: $($result | ConvertTo-Json -Compress -Depth 5)"
    }
    'Streamed multiclient event analysis passed.'
}
finally { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
