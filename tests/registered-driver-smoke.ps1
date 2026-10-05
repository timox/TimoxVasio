$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $repoRoot 'build_driver_110\Release\DriverProbe.exe'
$asioKey = 'HKLM:\SOFTWARE\ASIO\TimoxVasio'
$expectedDll = 'C:\Program Files\Steinberg\VirtualASIO\1.1.0\TimoxVasio.dll'

if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw "Sonde absente : $probe" }
if (-not (Test-Path -LiteralPath $asioKey)) { throw 'TimoxVasio absent du registre ASIO' }
$clsid = (Get-ItemProperty -LiteralPath $asioKey).CLSID
$inprocKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
$registeredDll = (Get-Item -LiteralPath $inprocKey).GetValue('')
if ($registeredDll -ne $expectedDll) { throw "DLL enregistrée inattendue : $registeredDll" }
if (-not (Test-Path -LiteralPath $registeredDll -PathType Leaf)) { throw "DLL enregistrée absente : $registeredDll" }
if ((Get-Item -LiteralPath $registeredDll).VersionInfo.ProductVersion -ne '1.1.0') {
    throw "Version du pilote enregistré inattendue : $registeredDll"
}

& $probe --registered TimoxVasio
if ($LASTEXITCODE -ne 0) { throw "Sonde ASIO enregistrée en échec : $LASTEXITCODE" }
Write-Output "PASS: TimoxVasio 1.1.0 enregistré et activé depuis $registeredDll"
