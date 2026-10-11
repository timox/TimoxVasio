param([string]$StageLogPath)

@{
    phase = 'start'
    cycle = 1
    stage = 'fixture-hang'
    qpc = [System.Diagnostics.Stopwatch]::GetTimestamp()
} | ConvertTo-Json -Compress | Set-Content -LiteralPath $StageLogPath -Encoding utf8
Start-Sleep -Seconds 30
