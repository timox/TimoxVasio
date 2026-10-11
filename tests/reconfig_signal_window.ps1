function Get-ReconfigSignalWindow {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Events,
        [Parameter(Mandatory)][ValidateSet('send', 'receive', 'both')][string]$Role,
        [Parameter(Mandatory)][long]$ChangeQpc,
        [Parameter(Mandatory)][long]$EndQpc,
        [Parameter(Mandatory)][long]$GraceTicks,
        [Parameter(Mandatory)][int]$MinCallbacks
    )
    $startQpc = $ChangeQpc + $GraceTicks
    $audio = @($Events | Where-Object {
        $_.kind -eq 1 -and $_.qpc -ge $startQpc -and $_.qpc -lt $EndQpc
    })
    [long]$errors = 0
    if ($Role -eq 'receive' -or $Role -eq 'both') {
        foreach ($event in $audio) {
            $errors += [long]$event.corrupted + [long]$event.missing + [long]$event.duplicated
        }
    }
    return [ordered]@{
        callbacks = $audio.Count
        sampleErrors = $errors
        pass = $audio.Count -ge $MinCallbacks -and $errors -eq 0
    }
}
