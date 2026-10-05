$ErrorActionPreference = 'Stop'
$root = 'HKCU:\Software\VASIO-Registration-Test'
$scriptPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'register_drivers.ps1'
$dllDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'build_driver_110\Release'
$expectedClsid = '{A4D39126-78CB-4D89-9E0A-54494D4F5856}'
$legacy = @{
    VASIO1 = '{7A9F4D01-4D7D-4D54-9A61-564153494F31}'
    VASIO2 = '{7A9F4D02-4D7D-4D54-9A61-564153494F32}'
    VASIO3 = '{7A9F4D03-4D7D-4D54-9A61-564153494F33}'
    VASIO4 = '{7A9F4D04-4D7D-4D54-9A61-564153494F34}'
}

function Assert-Equal($actual, $wanted, $message) {
    if ($actual -ne $wanted) { throw "$message (expected '$wanted', got '$actual')" }
}

try {
    New-Item -Path "$root\ASIO\OtherDriver" -Force | Out-Null
    New-ItemProperty -LiteralPath "$root\ASIO\OtherDriver" -Name 'Description' -Value 'OtherDriver' -Force | Out-Null
    foreach ($name in $legacy.Keys) {
        New-Item -Path "$root\ASIO\$name" -Force | Out-Null
        New-ItemProperty -LiteralPath "$root\ASIO\$name" -Name 'CLSID' -Value $legacy[$name] -Force | Out-Null
        New-Item -Path "$root\Classes\CLSID\$($legacy[$name])\InprocServer32" -Force | Out-Null
        Set-Item -LiteralPath "$root\Classes\CLSID\$($legacy[$name])\InprocServer32" -Value (Join-Path $dllDirectory "$name.dll")
    }

    & $scriptPath install -RegistryRoot $root -DllDirectory $dllDirectory
    $asioKeys = @(Get-ChildItem -LiteralPath "$root\ASIO")
    Assert-Equal $asioKeys.Count 2 'single VASIO plus unrelated ASIO entry count'
    Assert-Equal (Get-ItemProperty -LiteralPath "$root\ASIO\TimoxVasio").CLSID $expectedClsid 'TimoxVasio CLSID'
    Assert-Equal (Get-ItemProperty -LiteralPath "$root\ASIO\TimoxVasio").Description 'TimoxVasio' 'TimoxVasio Description'
    Assert-Equal (Get-Item -LiteralPath "$root\Classes\CLSID\$expectedClsid\InprocServer32").GetValue('') (Join-Path $dllDirectory 'TimoxVasio.dll') 'TimoxVasio InprocServer32'
    Assert-Equal (Get-ItemProperty -LiteralPath "$root\Classes\CLSID\$expectedClsid\InprocServer32").ThreadingModel 'Apartment' 'TimoxVasio ThreadingModel'
    foreach ($name in $legacy.Keys) {
        if (Test-Path -LiteralPath "$root\ASIO\$name") { throw "Legacy ASIO entry remains: $name" }
        if ($name -ne 'VASIO1' -and (Test-Path -LiteralPath "$root\Classes\CLSID\$($legacy[$name])")) {
            throw "Legacy COM registration remains: $($legacy[$name])"
        }
    }

    & $scriptPath install -RegistryRoot $root -DllDirectory $dllDirectory
    Assert-Equal @(Get-ChildItem -LiteralPath "$root\ASIO").Count 2 'idempotent install count'

    & $scriptPath uninstall -RegistryRoot $root -DllDirectory $dllDirectory
    & $scriptPath uninstall -RegistryRoot $root -DllDirectory $dllDirectory
    if (Test-Path -LiteralPath "$root\ASIO\TimoxVasio") { throw 'TimoxVasio ASIO key remains after uninstall' }
    if (Test-Path -LiteralPath "$root\Classes\CLSID\$expectedClsid") { throw 'TimoxVasio COM key remains after uninstall' }
    if (-not (Test-Path -LiteralPath "$root\ASIO\OtherDriver")) { throw 'Unrelated ASIO entry was removed' }
    Write-Host 'PASS: single-driver migration, unrelated registration preservation and idempotent install/uninstall.'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
