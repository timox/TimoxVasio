param(
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$DriverPath,
    [switch]$StopAllTimox
)

$root = [IO.Path]::GetFullPath($InstallRoot).TrimEnd([char]92) + [char]92
# Release ASIO clients first; otherwise the engine or DLL cannot be replaced.
Get-Process |
    ForEach-Object {
        try {
            if ($_.Modules.FileName -contains $DriverPath) {
                Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
            }
        } catch { }
    }
Get-CimInstance Win32_Process -Filter "Name = 'TimoxVSSC.exe' OR Name = 'Timox VASIO Control.exe' OR Name = 'TimoxVirtualAsioEngine.exe' OR Name = 'PwarAsioClient.exe'" |
    Where-Object { $StopAllTimox -or ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
