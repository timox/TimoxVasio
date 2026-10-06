param(
    [string]$ExpectedEnginePath = (Join-Path $env:LOCALAPPDATA 'Programs\vasio-control\resources\backend\TimoxVirtualAsioEngine.exe')
)

$ErrorActionPreference = 'Stop'
$issues = [System.Collections.Generic.List[string]]::new()

try {
    $registration = Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\TimoxVasio'
    $comPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$($registration.CLSID)\InprocServer32"
    $dllPath = (Get-ItemProperty -LiteralPath $comPath).'(default)'
    if (-not $dllPath -or -not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
        $issues.Add('La DLL TimoxVasio enregistrée est absente.')
    }
} catch {
    $issues.Add('Enregistrement Windows du pilote TimoxVasio incorrect.')
}

if (-not (Test-Path -LiteralPath $ExpectedEnginePath -PathType Leaf)) {
    $issues.Add("Le moteur installé est absent : $ExpectedEnginePath")
}

$engines = @(Get-CimInstance Win32_Process -Filter "name = 'TimoxVirtualAsioEngine.exe'")
if (-not $engines.Count) {
    $issues.Add('Le moteur ne tourne pas. Ouvrir Timox VASIO Control avant une application ASIO.')
} elseif (-not @($engines | Where-Object {
    $_.ExecutablePath -and [string]::Equals($_.ExecutablePath, $ExpectedEnginePath,
        [System.StringComparison]::OrdinalIgnoreCase)
}).Count) {
    $issues.Add('Le moteur actif ne provient pas de la version Timox VASIO Control attendue.')
}

try {
    $state = Invoke-RestMethod -Uri 'http://127.0.0.1:52525/api/v1/state' -TimeoutSec 3
    if ($state.apiVersion -ne '1.0' -or
        -not @($state.virtualDrivers | Where-Object id -EQ 'TimoxVasio').Count) {
        $issues.Add('Le service API local ne correspond pas à TimoxVasio.')
    } elseif (-not ($state.PSObject.Properties.Name -contains 'configuredRoutes')) {
        $issues.Add('Le moteur actif est plus ancien que la correction des routes persistantes.')
    } else {
        Write-Output "État audio : $($state.engine.state); routes actives : $(@($state.routes).Count); routes enregistrées : $(@($state.configuredRoutes).Count)."
    }
} catch {
    $issues.Add('API locale du moteur indisponible sur 127.0.0.1:52525.')
}

if ($issues.Count) {
    foreach ($issue in $issues) { Write-Error $issue -ErrorAction Continue }
    exit 1
}
Write-Output 'Pilote, moteur et API TimoxVasio cohérents.'
