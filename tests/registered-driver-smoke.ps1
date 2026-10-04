$ErrorActionPreference = 'Stop'
$asioRoot = Split-Path -Parent $PSScriptRoot
$dllDirectory = Join-Path $asioRoot 'build_drivers_vs2026_ninja'
$installer = Join-Path $asioRoot 'register_drivers.ps1'
$probe = Join-Path $dllDirectory 'DriverProbe.exe'

try {
    & $installer install -RegistryRoot 'HKLM:\SOFTWARE' -DllDirectory $dllDirectory
    $asioKey = 'HKLM:\SOFTWARE\ASIO\TimoxVasio'
    $clsid = (Get-ItemProperty -LiteralPath $asioKey).CLSID
    $inprocKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
    $dll = (Get-Item -LiteralPath $inprocKey).GetValue('')
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw "TimoxVasio DLL is missing: $dll" }
    foreach ($name in 'VASIO1', 'VASIO2', 'VASIO3', 'VASIO4') {
        if (Test-Path -LiteralPath "HKLM:\SOFTWARE\ASIO\$name") { throw "Legacy ASIO entry remains: $name" }
    }
    Write-Output "TimoxVasio registry: $clsid -> $dll"

    & $probe --registered TimoxVasio
    if ($LASTEXITCODE -ne 0) { throw "Steinberg AsioDriverList probe failed with exit code $LASTEXITCODE" }
    Write-Output 'PASS: Steinberg AsioDriverList discovered and instantiated the unique TimoxVasio driver.'
} finally {
    & $installer uninstall -RegistryRoot 'HKLM:\SOFTWARE' -DllDirectory $dllDirectory
}
