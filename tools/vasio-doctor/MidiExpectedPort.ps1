function Get-MidiExpectedPortState($Requirement, $Midi) {
    $name = if ($Requirement -is [string]) { $Requirement } else { [string]$Requirement.name }
    $flow = if ($Requirement -is [string]) { $null } else { [string]$Requirement.flow }
    if ([string]::IsNullOrWhiteSpace($name)) { throw 'Expected MIDI port name is empty.' }
    $winmm = @($Midi.winmmPorts | Where-Object {
        ([string]$_.name).IndexOf($name, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -and
        (-not $flow -or $_.flow -eq $flow)
    })
    if ($winmm.Count) {
        return [pscustomobject]@{ state = 'present'; name = $name; flow = $flow; source = 'WinMM' }
    }
    if ($Midi.windowsMidiServices.enumeration -ne 'Windows MIDI Services SDK') {
        return [pscustomobject]@{ state = 'unverified'; name = $name; flow = $flow;
            source = 'Windows MIDI Services inventory unavailable' }
    }
    $services = @($Midi.windowsMidiServices.endpoints | Where-Object {
        ([string]$_.name).IndexOf($name, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
    })
    if ($services.Count) {
        return [pscustomobject]@{ state = if ($flow) { 'flow-unverified' } else { 'present' };
            name = $name; flow = $flow; source = 'Windows MIDI Services' }
    }
    return [pscustomobject]@{ state = 'missing'; name = $name; flow = $flow;
        source = 'WinMM and Windows MIDI Services' }
}
