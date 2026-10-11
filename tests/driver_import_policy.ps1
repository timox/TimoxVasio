function Get-ForbiddenDriverDiskWriteImports {
    param([Parameter(Mandatory)][string]$DumpbinText)

    $forbidden = @(
        'WriteFile', 'WriteFileEx', 'WriteFileGather', 'FlushFileBuffers',
        'SetEndOfFile', 'SetFileValidData', 'MoveFileExA', 'MoveFileExW',
        'ReplaceFileA', 'ReplaceFileW', 'CopyFileA', 'CopyFileW',
        'CreateDirectoryA', 'CreateDirectoryW', 'DeleteFileA', 'DeleteFileW'
    )
    @([regex]::Matches($DumpbinText,
        '(?m)^\s*[0-9A-Fa-f]+\s+([A-Za-z_][A-Za-z0-9_@]*)\s*$') |
        ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ -in $forbidden } |
        Sort-Object -Unique)
}
