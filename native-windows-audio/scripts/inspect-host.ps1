# Read-only inventory for preparing native audio qualification on this host.
# No installation, service change, certificate creation or audio endpoint open.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

function Read-ServiceState([string]$Name) {
    # sc.exe distinguishes an absent service (1060) from insufficient rights.
    $lines = & "$env:SystemRoot\System32\sc.exe" query $Name 2>&1
    $code = $LASTEXITCODE
    [ordered]@{
        name = $Name
        observation = $(if ($code -eq 0) { 'present' } elseif ($code -eq 1060) { 'absent' } else { 'unknown' })
        exitCode = $code
        detail = ($lines | Out-String).Trim()
    }
}

function Read-Artifact([string]$RelativePath) {
    $path = Join-Path $repoRoot $RelativePath
    $result = [ordered]@{ path = $path; present = $false }
    try {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $result }
        $file = Get-Item -LiteralPath $path
        $result.present = $true
        $result.bytes = $file.Length
        $result.modifiedUtc = $file.LastWriteTimeUtc.ToString('o')
        $result.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        try {
            $signature = Get-AuthenticodeSignature -LiteralPath $path
            $result.authenticodeStatus = [string]$signature.Status
            $result.signerThumbprint = $(if ($signature.SignerCertificate) { $signature.SignerCertificate.Thumbprint } else { $null })
        } catch { $result.signatureObservationError = $_.Exception.Message }
    } catch { $result.observationError = $_.Exception.Message }
    return $result
}

$report = [ordered]@{
    schemaVersion = 1
    observedUtc = [DateTime]::UtcNow.ToString('o')
    repository = $repoRoot
    osVersion = [Environment]::OSVersion.VersionString
    is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
    services = @(
        Read-ServiceState 'TimoxWindowsAudio'
        Read-ServiceState 'TimoxNativeAudioBroker'
    )
    artifacts = @(
        Read-Artifact 'build_native_windows_audio\x64\Release\TimoxWindowsAudio.sys'
        Read-Artifact 'build_native_production\Release\TimoxNativeAudioServiceProbe.exe'
        Read-Artifact 'build_native_audio_protocol\Release\NativeAudioDriverProbe.exe'
        Read-Artifact 'build_codex_110\Release\TimoxVirtualAsioEngine.exe'
        Read-Artifact 'build_driver_110\Release\TimoxVasio.dll'
    )
    limitations = @(
        'Inventory only; presence does not prove driver loading or audio operation.'
        'Authenticode inspection is not a Windows kernel signing admission test.'
        'No service provisioning, certificate enrollment, driver installation or audio test was performed.'
        'No virtual machine is assumed or required by this inventory.'
    )
}
$report | ConvertTo-Json -Depth 6
