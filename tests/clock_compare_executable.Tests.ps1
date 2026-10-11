$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$comparator = Join-Path $root 'build_vasio_probe\Release\vasio-clock-compare.exe'
$thresholds = Join-Path $root 'tools\vasio-probe\thresholds.json'
$folder = Join-Path $PSScriptRoot 'results\clock-compare-fixture'
New-Item -ItemType Directory -Path $folder -Force | Out-Null

function Make-Fixture([string]$Name, [double]$Ppm, [int]$StartSeconds) {
    $pollPath = Join-Path $folder "$Name.polls.jsonl"
    $lines = [System.Collections.Generic.List[string]]::new()
    for ($index = 0; $index -le 1200; ++$index) {
        $elapsed = $index / 10.0
        $qpc = [long](($StartSeconds + $elapsed) * 10000000)
        $position = [long]([math]::Floor(48000 * (1 + $Ppm / 1000000) * $elapsed / 512) * 512)
        $lines.Add("{`"qpc`":$qpc,`"samplePosition`":$position}")
    }
    [IO.File]::WriteAllLines($pollPath, $lines)
    $reportPath = Join-Path $folder "$Name.json"
    @{ driver = $Name; qpcFrequency = 10000000; sampleRate = 48000;
        bufferFrames = 512; positionPollsSaved = $true; positionPollsPath = $pollPath } |
        ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8
    return $reportPath
}

$a = Make-Fixture 'Synthetic-A' 1000 0
$b = Make-Fixture 'Synthetic-B' -500 1
$output = Join-Path $folder 'comparison.json'
& $comparator --primary $a --reference $b --thresholds $thresholds --output $output | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Clock comparator exited $LASTEXITCODE" }
$result = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
if (-not $result.available -or
    [math]::Abs($result.relativeDriftPpm - 1500.75) -gt 50 -or
    $result.overlapSeconds -lt 118 -or
    $result.primaryRegression.samples -lt 1000 -or
    $result.referenceRegression.samples -lt 1000 -or
    $result.conclusion -ne 'dérive relative détectée') {
    throw "Common-window comparison is wrong: $($result | ConvertTo-Json -Depth 6)"
}
$far = Make-Fixture 'Synthetic-Far' -500 200
$noOverlapPath = Join-Path $folder 'no-overlap.json'
& $comparator --primary $a --reference $far --thresholds $thresholds --output $noOverlapPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Clock comparator exited $LASTEXITCODE on nonoverlap" }
$noOverlap = Get-Content -LiteralPath $noOverlapPath -Raw | ConvertFrom-Json
if ($noOverlap.available -or $noOverlap.reason -notmatch 'overlap') {
    throw 'Nonoverlapping acquisition windows were accepted.'
}
$badPolls = Join-Path $folder 'bad.polls.jsonl'
[IO.File]::WriteAllLines($badPolls, @(
    '{"qpc":100,"samplePosition":0}',
    '{"qpc":100,"samplePosition":512}',
    '{"qpc":200,"samplePosition":1024}'
))
$badReport = Join-Path $folder 'bad.json'
@{ driver = 'Bad'; qpcFrequency = 10000000; sampleRate = 48000;
    bufferFrames = 512; positionPollsSaved = $true; positionPollsPath = $badPolls } |
    ConvertTo-Json | Set-Content -LiteralPath $badReport -Encoding utf8
& $comparator --primary $badReport --reference $b --thresholds $thresholds `
    --output (Join-Path $folder 'bad-comparison.json') 2>$null | Out-Null
if ($LASTEXITCODE -ne 3) { throw 'Nonmonotonic polls were not rejected.' }
'PASS: shared-window drift, nonoverlap, and nonmonotonic poll rejection.'
