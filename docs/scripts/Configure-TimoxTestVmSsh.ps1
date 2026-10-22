#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$UserName = 'timo',
    [string]$PublicKeyPath = (Join-Path $PSScriptRoot 'timox-vm.pub'),
    [Parameter(Mandatory = $true)][string[]]$AllowedRemoteAddress,
    [ValidateRange(1, 65535)][int]$Port = 22
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Run ONLY from the console of the dedicated test VM. No automatic elevation.
if ($UserName -cnotmatch '^[A-Za-z0-9_][A-Za-z0-9_.-]{0,19}$') {
    throw 'Use a simple existing local account name (no domain, spaces or SSH patterns).'
}
foreach ($address in $AllowedRemoteAddress) {
    $parts = $address.Split('/')
    $ip = $null
    if ($parts.Count -gt 2 -or $parts[0] -notmatch '^[0-9a-fA-F:.]+$' -or
        -not [Net.IPAddress]::TryParse($parts[0], [ref]$ip)) { throw "Invalid literal IP/CIDR: $address" }
    $bits = if ($ip.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork) { 32 } else { 128 }
    if ($parts.Count -eq 2 -and ($parts[1] -notmatch '^[0-9]+$' -or [int]$parts[1] -lt 1 -or [int]$parts[1] -gt $bits)) {
        throw "Invalid CIDR prefix (zero is forbidden): $address"
    }
    if ($ip.Equals([Net.IPAddress]::Any) -or $ip.Equals([Net.IPAddress]::IPv6Any)) { throw 'Wildcard addresses are forbidden.' }
}
if ($AllowedRemoteAddress.Count -eq 0) { throw 'At least one source IP/CIDR is required.' }
$keyFile = Get-Item -LiteralPath $PublicKeyPath
if ($keyFile.PSIsContainer -or $keyFile.Length -gt 16384 -or $keyFile.Extension -ne '.pub') { throw 'Expected a small .pub public key file.' }
$key = [IO.File]::ReadAllText($keyFile.FullName).Trim()
if ($key -notmatch '^ssh-ed25519 ([A-Za-z0-9+/]+={0,2})(?: [^\r\n]*)?$') { throw 'Expected exactly one Ed25519 public key, without authorized_keys options.' }
$blob = [Convert]::FromBase64String($Matches[1])
# SSH wire encoding: string("ssh-ed25519"), string(32-byte public key).
if ($blob.Length -ne 51 -or [BitConverter]::ToString($blob[0..3]) -ne '00-00-00-0B' -or
    [Text.Encoding]::ASCII.GetString($blob, 4, 11) -ne 'ssh-ed25519' -or
    [BitConverter]::ToString($blob[15..18]) -ne '00-00-00-20') { throw 'Malformed Ed25519 public key blob.' }
$user = Get-LocalUser -Name $UserName
if (-not $user.Enabled) { throw 'The selected local account is disabled.' }
$adminSid = New-Object Security.Principal.SecurityIdentifier('S-1-5-32-544')
$members = @(Get-LocalGroupMember -SID $adminSid)
if (-not ($members | Where-Object { $_.SID.Value -eq $user.SID.Value })) { throw 'The account must be a direct member of local Administrators.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run in an elevated Windows PowerShell console INSIDE the dedicated VM.' }

function Assert-NoReparse([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path refused: $current" }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Set-AdminAcl([string]$Path, [bool]$Directory = $false) {
    if ($Directory) { $acl = New-Object Security.AccessControl.DirectorySecurity }
    else { $acl = New-Object Security.AccessControl.FileSecurity }
    $acl.SetAccessRuleProtection($true, $false)
    $acl.SetOwner($adminSid)
    foreach ($sidText in @('S-1-5-18', 'S-1-5-32-544')) {
        $sid = New-Object Security.Principal.SecurityIdentifier($sidText)
        if ($Directory) {
            $rule = New-Object Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        } else { $rule = New-Object Security.AccessControl.FileSystemAccessRule($sid, 'FullControl', 'Allow') }
        $acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}
$sshDir = Join-Path $env:ProgramData 'ssh'
$configPath = Join-Path $sshDir 'sshd_config'
$keysPath = Join-Path $sshDir 'administrators_authorized_keys'
Assert-NoReparse $configPath
Assert-NoReparse $keysPath
$ruleName = 'Timox-TestVM-SSH'
$existingRule = Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue
if ($existingRule) { throw 'Project firewall rule already exists. Inspect/remove it explicitly from the VM console before reconfiguration.' }
if (-not $PSCmdlet.ShouldProcess($env:COMPUTERNAME, "Configure VM-only SSH for $UserName on port $Port from $($AllowedRemoteAddress -join ', ')")) { return }

$backup = $null
$saved = @()
try {
    $restartNeeded = $false
    $capability = Get-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0'
    if ($capability.State -ne 'Installed') {
        $installed = Add-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0'
        $restartNeeded = [bool]$installed.RestartNeeded
    }
    # Disable only the standard capability rule, never arbitrary firewall rules.
    $defaultRule = Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue
    if ($defaultRule) { $defaultRule | Disable-NetFirewallRule | Out-Null }
    Stop-Service sshd -ErrorAction Stop
    if ($restartNeeded) { throw 'Capability requires a VM restart. Restart manually, then rerun from the VM console.' }
    if (-not (Test-Path -LiteralPath $sshDir)) { New-Item -ItemType Directory -Path $sshDir | Out-Null }
    Assert-NoReparse $configPath
    Assert-NoReparse $keysPath
    $backup = Join-Path $sshDir ('Timox-backup-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    Set-AdminAcl $backup $true
    foreach ($path in @($configPath, $keysPath)) {
        $exists = Test-Path -LiteralPath $path
        $entry = @{ Path = $path; Existed = $exists; Backup = (Join-Path $backup ([IO.Path]::GetFileName($path))); Acl = $null }
        if ($exists) {
            $entry.Acl = Get-Acl -LiteralPath $path
            Copy-Item -LiteralPath $path -Destination $entry.Backup
            $entry.Acl.Sddl | Set-Content -LiteralPath ($entry.Backup + '.sddl') -Encoding ASCII
        }
        $saved += $entry
    }
    $config = @"
# Dedicated Timox test VM; replaces previous configuration (backed up).
Port $Port
PubkeyAuthentication yes
AuthenticationMethods publickey
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitEmptyPasswords no
AllowUsers $($UserName.ToLowerInvariant())
AuthorizedKeysFile __PROGRAMDATA__/ssh/administrators_authorized_keys
AllowTcpForwarding no
AllowAgentForwarding no
PermitTunnel no
Subsystem sftp sftp-server.exe
"@
    [IO.File]::WriteAllText($configPath, $config + "`r`n", (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($keysPath, $key + "`r`n", (New-Object Text.UTF8Encoding($false)))
    Set-AdminAcl $configPath
    Set-AdminAcl $keysPath
    $sshd = Join-Path $env:WINDIR 'System32/OpenSSH/sshd.exe'
    $keygen = Join-Path $env:WINDIR 'System32/OpenSSH/ssh-keygen.exe'
    & $keygen -A
    if ($LASTEXITCODE -ne 0) { throw 'Host key generation failed.' }
    & $sshd -t -f $configPath
    if ($LASTEXITCODE -ne 0) { throw 'sshd -t rejected the configuration; service remains stopped.' }
    New-NetFirewallRule -Name $ruleName -DisplayName 'Timox test VM SSH (restricted sources)' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $Port -RemoteAddress $AllowedRemoteAddress -Program $sshd -Profile Any | Out-Null
    Set-Service sshd -StartupType Automatic
    Start-Service sshd
    Write-Output "SSH configured. Backup: $backup"
    Write-Output "Account: $UserName ; port: $Port ; permitted sources: $($AllowedRemoteAddress -join ', ')"
    & $keygen -lf (Join-Path $sshDir 'ssh_host_ed25519_key.pub')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot display host fingerprint.' }
} catch {
    $original = $_
    Stop-Service sshd -ErrorAction SilentlyContinue
    Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue | Disable-NetFirewallRule -ErrorAction SilentlyContinue | Out-Null
    foreach ($entry in $saved) {
        try {
            if ($entry.Existed) {
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path -Force
                Set-Acl -LiteralPath $entry.Path -AclObject $entry.Acl
            } elseif (Test-Path -LiteralPath $entry.Path) { Remove-Item -LiteralPath $entry.Path }
        } catch { Write-Warning "Rollback failed for $($entry.Path): $_" }
    }
    Write-Warning "Setup failed. sshd left stopped; capability/host keys may remain, default firewall rule stays disabled. Backup: $backup. Recover from the VM console."
    throw $original
}
