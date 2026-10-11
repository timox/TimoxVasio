$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\vasio-doctor\AsioBufferSizes.ps1')

function Assert-DeclaredSizes($Buffer, [long[]]$Candidates, [long[]]$Expected) {
    $actual = @($Candidates | Where-Object {
        Test-AsioDeclaredBufferSize -Frames $_ -Buffer $Buffer
    })
    if (($actual -join ',') -ne ($Expected -join ',')) {
        throw "Declared buffer sizes $($actual -join ','); expected $($Expected -join ',')"
    }
}

Assert-DeclaredSizes @{ minimum = 64; maximum = 512; preferred = 128; granularity = -1 } `
    @(63, 64, 96, 128, 256, 512, 1024) @(64, 128, 256, 512)
Assert-DeclaredSizes @{ minimum = 64; maximum = 512; preferred = 128; granularity = 0 } `
    @(64, 128, 256, 512) @(128)
Assert-DeclaredSizes @{ minimum = 80; maximum = 400; preferred = 160; granularity = 80 } `
    @(64, 80, 128, 160, 240, 256, 400) @(80, 160, 240, 400)
Assert-DeclaredSizes @{ minimum = 0; maximum = 512; preferred = 128; granularity = -1 } `
    @(64, 128) @()

'ASIO declared buffer size tests passed.'
