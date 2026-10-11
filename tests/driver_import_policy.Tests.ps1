$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'driver_import_policy.ps1')

$safe = @'
    E6 CreateFileW
   4A9 ReadFile
'@
if (@(Get-ForbiddenDriverDiskWriteImports -DumpbinText $safe).Count) {
    throw 'Read-only imports were rejected.'
}
$unsafe = @'
    E6 CreateFileW
   4A9 ReadFile
   541 WriteFile
   542 MoveFileExW
'@
$found = @(Get-ForbiddenDriverDiskWriteImports -DumpbinText $unsafe)
if (($found -join ',') -ne 'MoveFileExW,WriteFile') {
    throw "Disk-write imports were not detected: $($found -join ',')"
}
'Driver import policy tests passed.'
