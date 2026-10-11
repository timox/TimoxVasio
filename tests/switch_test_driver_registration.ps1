param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Inspect', 'Stage', 'Restore')]
    [string]$Action,
    [string]$SnapshotPath,
    [string]$BaseUri = 'http://127.0.0.1:52525'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'AGENTS.md'))) {
    throw 'TimoxVasio repository root could not be confirmed.'
}
$gitStatus = @(git -C $repoRoot status --short)
$clsid = '{A4D39126-78CB-4D89-9E0A-54494D4F5856}'
$asioKey = 'HKLM:\SOFTWARE\ASIO\TimoxVasio'
$comKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
$testDll = 'C:\Program Files\Steinberg\VirtualASIO\1.1.1-test.1\TimoxVasio.dll'
$builtDll = Join-Path $repoRoot 'build_driver_110\Release\TimoxVasio.dll'
$builtHash = if (Test-Path -LiteralPath $builtDll -PathType Leaf) {
    (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
} else { $null }
$asio = Get-Item -LiteralPath $asioKey -ErrorAction Stop
$com = Get-Item -LiteralPath $comKey -ErrorAction Stop
if ($asio.GetValue('CLSID') -ine $clsid) { throw 'The registered TimoxVasio CLSID differs from the expected CLSID.' }
$registeredDll = [string]$com.GetValue('')
if (-not $registeredDll -or -not (Test-Path -LiteralPath $registeredDll -PathType Leaf)) {
    throw "The registered TimoxVasio DLL is missing: $registeredDll"
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Changing the HKLM COM registration requires an elevated PowerShell process.'
    }
}

$script:apiAvailable = $false
function Get-ActiveClients([bool]$RequireApi) {
    try {
        $state = Invoke-RestMethod -Method Get -Uri "$BaseUri/api/v1/state" -TimeoutSec 5
        $script:apiAvailable = $true
        return @($state.virtualDrivers | ForEach-Object { $_.clients } | Where-Object { $null -ne $_ })
    } catch {
        $script:apiAvailable = $false
        if ($RequireApi) { throw }
        return @()
    }
}

function Get-LoadedDrivers {
    $holders = [System.Collections.Generic.List[object]]::new()
    $unknown = [System.Collections.Generic.List[object]]::new()
    $protectedNames = @('csrss', 'Idle', 'Memory Compression', 'MpDefenderCoreService',
        'MsMpEng', 'Registry', 'services', 'smss', 'svchost', 'System', 'wininit')
    foreach ($process in @(Get-Process)) {
        try {
            foreach ($module in $process.Modules) {
                if ([IO.Path]::GetFileName($module.FileName) -ieq 'TimoxVasio.dll') {
                    $holders.Add([ordered]@{ pid = $process.Id; process = $process.ProcessName;
                        dll = $module.FileName })
                }
            }
        } catch {
            if ($process.Id -ne $PID -and $process.ProcessName -notin $protectedNames) {
                $unknown.Add([ordered]@{ pid = $process.Id; process = $process.ProcessName })
            }
        }
    }
    return [ordered]@{ holders = $holders.ToArray(); inaccessible = $unknown.ToArray() }
}

function Assert-NoClients([bool]$RequireApi) {
    $clients = @(Get-ActiveClients $RequireApi)
    $loaded = Get-LoadedDrivers
    if ($clients.Count -or @($loaded.holders).Count -or @($loaded.inaccessible).Count) {
        $names = @($clients | ForEach-Object { "$($_.processName) PID $($_.pid)" }) -join ', '
        throw "Close ASIO hosts before changing registration. API clients: $names; loaded TimoxVasio DLLs: $(@($loaded.holders).Count); uninspectable processes: $(@($loaded.inaccessible).Count)."
    }
}

if ($Action -eq 'Inspect') {
    $clients = @(Get-ActiveClients $false)
    $loaded = Get-LoadedDrivers
    [ordered]@{
        action = 'Inspect'; repository = $repoRoot; dirtyPaths = $gitStatus.Count;
        registeredDll = $registeredDll;
        registeredSha256 = (Get-FileHash -LiteralPath $registeredDll -Algorithm SHA256).Hash;
        testDll = $testDll; builtDll = $builtDll; builtSha256 = $builtHash;
        apiAvailable = $script:apiAvailable; clients = $clients; loaded = $loaded;
        ready = $script:apiAvailable -and $clients.Count -eq 0 -and @($loaded.holders).Count -eq 0 -and
            @($loaded.inaccessible).Count -eq 0
    } | ConvertTo-Json -Depth 8
    exit 0
}

Assert-Administrator
Assert-NoClients ($Action -eq 'Stage')
if ($Action -eq 'Stage') {
    if (-not $builtHash) { throw "Test DLL missing: $builtDll" }
    if ($registeredDll -ieq $testDll) { throw 'The test driver is already registered; restore the previous snapshot first.' }
    if (-not $SnapshotPath) {
        $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
        $SnapshotPath = Join-Path $repoRoot "tests\results\driver-registration-before-$stamp.json"
    }
    $snapshotFull = [IO.Path]::GetFullPath($SnapshotPath)
    $rootPrefix = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\') + '\'
    if (-not $snapshotFull.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Registration snapshot must remain inside the repository.'
    }
    if (Test-Path -LiteralPath $snapshotFull) { throw "Snapshot already exists: $snapshotFull" }
    $snapshot = [ordered]@{ createdUtc = [DateTime]::UtcNow.ToString('o');
        clsid = $clsid; asioKey = $asioKey; comKey = $comKey;
        previousDll = $registeredDll;
        previousSha256 = (Get-FileHash -LiteralPath $registeredDll -Algorithm SHA256).Hash;
        testDll = $testDll; testSha256 = $builtHash;
        repository = $repoRoot; dirtyPaths = $gitStatus.Count }
    New-Item -ItemType Directory -Path (Split-Path -Parent $snapshotFull) -Force | Out-Null
    $snapshot | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $snapshotFull -Encoding utf8
    if (Test-Path -LiteralPath $testDll -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $testDll -Algorithm SHA256).Hash -ne $builtHash) {
            throw "A different DLL already occupies the versioned test path: $testDll"
        }
    } else {
        New-Item -ItemType Directory -Path (Split-Path -Parent $testDll) -Force | Out-Null
        Copy-Item -LiteralPath $builtDll -Destination $testDll
    }
    try {
        Set-Item -LiteralPath $comKey -Value $testDll
        if ((Get-Item -LiteralPath $comKey).GetValue('') -ine $testDll) {
            throw 'The test DLL registration could not be verified.'
        }
    } catch {
        Set-Item -LiteralPath $comKey -Value $registeredDll
        throw
    }
    [ordered]@{ action = 'Stage'; snapshot = $snapshotFull; registeredDll = $testDll;
        sha256 = $builtHash } | ConvertTo-Json -Depth 4
    exit 0
}

if (-not $SnapshotPath) { throw '-SnapshotPath is required for Restore.' }
$snapshotFull = [IO.Path]::GetFullPath($SnapshotPath)
$rootPrefix = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\') + '\'
if (-not $snapshotFull.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Registration snapshot must remain inside the repository.'
}
$snapshot = Get-Content -LiteralPath $snapshotFull -Raw | ConvertFrom-Json
if ($snapshot.clsid -ine $clsid -or $snapshot.comKey -ine $comKey -or
    $snapshot.testDll -ine $testDll) {
    throw 'The snapshot does not match this test driver registration.'
}
if ($registeredDll -ine $testDll -and $registeredDll -ine $snapshot.previousDll) {
    throw "The current registration changed unexpectedly: $registeredDll"
}
if (-not (Test-Path -LiteralPath $snapshot.previousDll -PathType Leaf) -or
    (Get-FileHash -LiteralPath $snapshot.previousDll -Algorithm SHA256).Hash -ne $snapshot.previousSha256) {
    throw 'The original DLL is missing or its bytes changed; restoration needs manual review.'
}
Set-Item -LiteralPath $comKey -Value $snapshot.previousDll
if ((Get-Item -LiteralPath $comKey).GetValue('') -ine $snapshot.previousDll) {
    throw 'The previous driver registration could not be verified.'
}
[ordered]@{ action = 'Restore'; snapshot = $snapshotFull;
    registeredDll = $snapshot.previousDll; sha256 = $snapshot.previousSha256 } |
    ConvertTo-Json -Depth 4
