# Produces a reviewable plan only. Never executes the listed SCM commands.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ExpectedSha256
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source = Join-Path $repoRoot 'build_native_production\Release\TimoxNativeAudioServiceProbe.exe'
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
if ($hash -ne $ExpectedSha256) { throw 'Probe hash differs from the reviewed candidate. No qualification plan produced.' }
$service = 'TimoxNativeAudioBroker'
$query = & "$env:SystemRoot\System32\sc.exe" query $service 2>&1
$queryCode = $LASTEXITCODE
if ($queryCode -ne 1060) {
    throw "Service must be absent (1060), observed $queryCode. Do not replace an existing service. $query"
}
$inspectionText = & $source --inspect
if ($LASTEXITCODE -ne 0) { throw 'Read-only probe inspection failed.' }
$inspection = $inspectionText | ConvertFrom-Json
$dryRunText = & $source --prepare-qualification-root
if ($LASTEXITCODE -ne 0) { throw 'Qualification-root dry-run failed.' }
$dryRun = $dryRunText | ConvertFrom-Json
if ($dryRun.apply -ne $false -or $dryRun.error -ne 0) { throw 'Probe did not confirm a successful read-only dry-run.' }
$destination = Join-Path $env:ProgramFiles 'TimoxNativeAudioServiceProbe\TimoxNativeAudioServiceProbe.exe'
$file = Get-Item -LiteralPath $source
[ordered]@{
    schemaVersion = 1
    mode = 'plan-only'
    observedUtc = [DateTime]::UtcNow.ToString('o')
    source = [ordered]@{ path = $source; bytes = $file.Length; modifiedUtc = $file.LastWriteTimeUtc.ToString('o'); sha256 = $hash }
    destination = $destination
    service = $service
    serviceAbsent = $true
    before = $inspection
    preparationDryRun = $dryRun
    prerequisites = @(
        'Authorize the privileged qualification separately; this script grants no authorization.'
        'Stage the verified executable in the destination directory protected against modification by ordinary users.'
        'Inspect owner, DACL and reparse points of the destination and ancestors; refuse unsafe existing paths.'
        'Recheck the destination SHA-256 and absence of the service immediately before creating it.'
        'Archive existing qualification discovery.bin, its hash and security descriptor if present.'
    )
    orderedSteps = @(
        [ordered]@{ executable = $destination; arguments = @('--prepare-qualification-root', '--apply'); require = 'Exit 0 and JSON error=0; Runtime must not be created.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('create', $service, 'binPath=', ('"' + $destination + '" --service-publication-probe'), 'type=', 'own', 'start=', 'demand', 'obj=', 'NT AUTHORITY\LocalService'); require = 'Exit 0; do not continue on any failure.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('sidtype', $service, 'unrestricted'); require = 'Exit 0.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('privs', $service, 'SeChangeNotifyPrivilege'); require = 'Exit 0.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('qc', $service); require = 'Exact executable, LocalService and demand start.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('qsidtype', $service); require = 'Unrestricted service SID.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('qprivs', $service); require = 'Only SeChangeNotifyPrivilege.' }
        [ordered]@{ executable = 'sc.exe'; arguments = @('start', $service); require = 'Record successful start, then observe STOPPED and both exit codes 0; time limit 30 seconds.' }
        [ordered]@{ executable = $destination; arguments = @('--inspect'); require = 'Run as a non-administrator: root/publication/owner errors 0; nonzero epoch changed from before.' }
    )
    rollback = @(
        'If this attempt created the service, verify its exact binary path and stopped state before sc.exe delete TimoxNativeAudioBroker.'
        'If still running, request a normal stop and observe termination; do not force-kill or delete an unverified service.'
        'Confirm service absence (1060) after deletion and record every remaining file/directory.'
        'Retain qualification metadata for evidence. Do not recursively delete the broker root or alter Runtime.'
        'Delete a staged executable only after verifying its path and hash, and only if this attempt created it.'
    )
    provesOnly = 'SCM service token and protected metadata publication; not audio transport, TLS enrollment, kernel driver loading, logon isolation or performance.'
} | ConvertTo-Json -Depth 8
