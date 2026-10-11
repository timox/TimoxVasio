$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\vasio-doctor\MidiExpectedPort.ps1')

$midi = [pscustomobject]@{
    winmmPorts = @([pscustomobject]@{ name = 'XONE:K3 MIDI IN'; flow = 'input' })
    windowsMidiServices = [pscustomobject]@{
        enumeration = 'SDK unavailable'; endpoints = @()
    }
}
function Assert-State($requirement, $expected) {
    $actual = (Get-MidiExpectedPortState $requirement $midi).state
    if ($actual -ne $expected) { throw "Expected $expected, got $actual" }
}
Assert-State ([pscustomobject]@{ name = 'XONE:K3'; flow = 'input' }) 'present'
Assert-State ([pscustomobject]@{ name = 'XONE:K3'; flow = 'output' }) 'unverified'
Assert-State 'SC follower' 'unverified'
$midi.windowsMidiServices.enumeration = 'Windows MIDI Services SDK'
$midi.windowsMidiServices.endpoints = @(
    [pscustomobject]@{ name = 'SC follower' },
    [pscustomobject]@{ name = 'Port A*B' }
)
Assert-State 'SC follower' 'present'
Assert-State ([pscustomobject]@{ name = 'SC follower'; flow = 'output' }) 'flow-unverified'
Assert-State 'A*B' 'present'
Assert-State 'A?B' 'missing'
Assert-State ([pscustomobject]@{ name = 'missing'; flow = 'input' }) 'missing'
'Expected MIDI port tests passed.'
