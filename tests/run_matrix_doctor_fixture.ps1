$ErrorActionPreference = 'Stop'
$outputPath = $null
for ($index = 0; $index -lt $args.Count; $index++) {
    if ($args[$index] -eq '--output' -and $index + 1 -lt $args.Count) {
        $outputPath = $args[$index + 1]
        break
    }
}
if (-not $outputPath) { throw 'Fixture needs --output.' }
@{ verdict = 'orange'; findings = @() } | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $outputPath -Encoding utf8
