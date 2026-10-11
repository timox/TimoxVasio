function Test-AsioDeclaredBufferSize {
    param(
        [Parameter(Mandatory)][long]$Frames,
        [Parameter(Mandatory)]$Buffer
    )

    $minimum = [long]$Buffer.minimum
    $maximum = [long]$Buffer.maximum
    $preferred = [long]$Buffer.preferred
    $granularity = [long]$Buffer.granularity
    if ($minimum -le 0 -or $maximum -lt $minimum -or
        $Frames -lt $minimum -or $Frames -gt $maximum) { return $false }
    if ($granularity -eq -1) { return ($Frames -band ($Frames - 1)) -eq 0 }
    if ($granularity -eq 0) { return $Frames -eq $preferred }
    if ($granularity -gt 0) { return (($Frames - $minimum) % $granularity) -eq 0 }
    return $false
}
