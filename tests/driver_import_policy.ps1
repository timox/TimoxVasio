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

function Get-DriverBackendBoundaryViolations {
    param([Parameter(Mandatory)][string]$CMakeText)

    # Deliberately conservative: new DLL inputs require review, regardless of their
    # names. ole32 is legal for ASIO COM activation and is not evidence of WASAPI.
    # This audits the simple declarations in the driver include, not arbitrary
    # CMake programs or runtime LoadLibrary/COM behavior.
    $allowedSources = @(
        '${SRC_DIR}/vasio_driver.cpp', '${SRC_DIR}/vasio_driver_factory.cpp',
        '${SRC_DIR}/vasio_compatibility_profile.cpp', '${SRC_DIR}/application_profile_store.cpp',
        '${SRC_DIR}/audio_transport.cpp', '${SRC_DIR}/vasio_com_driver.h',
        '${SRC_DIR}/vasio_compatibility_profile.h', '${SRC_DIR}/vasio_driver.def',
        '${SRC_DIR}/driver_version.rc', '${ASIO_SDK_PATH}/common/combase.cpp',
        '${ASIO_SDK_PATH}/common/dllentry.cpp', '${ASIO_SDK_PATH}/common/register.cpp'
    )
    $allowedLibraries = @('ole32', 'oleaut32', 'winmm', 'user32')
    $variables = [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::Ordinal)
    $violations = [System.Collections.Generic.List[string]]::new()
    $driverFound = $false
    $text = [regex]::Replace($CMakeText, '(?m)#.*$', '')
    $commands = [regex]::Matches($text, '(?is)\b(set|list|add_library|target_sources|target_link_libraries)\s*\(([^()]*)\)')
    # Never silently skip a relevant declaration outside this audit's supported
    # syntax (for example parentheses inside quoted paths).
    $commandStarts = [regex]::Matches($text, '(?is)\b(set|list|add_library|target_sources|target_link_libraries)\s*\(')
    foreach ($start in $commandStarts) {
        if ($start.Index -notin $commands.Index) {
            $violations.Add("Driver declaration could not be audited near offset $($start.Index).")
        }
    }

    function Expand-DriverInput([string]$Token, [string[]]$Stack = @()) {
        if ($Token -match '^\$\{([^}]+)\}$') {
            $name = $Matches[1]
            if ($name -in $Stack -or -not $variables.ContainsKey($name)) {
                $violations.Add("Unresolved driver input: $Token")
                return
            }
            foreach ($item in $variables[$name]) {
                Expand-DriverInput $item ($Stack + $name)
            }
        } else {
            $Token
        }
    }

    foreach ($command in $commands) {
        $kind = $command.Groups[1].Value.ToLowerInvariant()
        $tokens = @([regex]::Matches($command.Groups[2].Value, '"[^"]*"|[^\s;]+') |
            ForEach-Object { $_.Value.Trim('"') })
        if (-not $tokens.Count) { continue }
        if ($kind -eq 'set') {
            $variables[$tokens[0]] = @($tokens | Select-Object -Skip 1)
            continue
        }
        if ($kind -eq 'list') {
            if ($tokens.Count -ge 3 -and $tokens[0] -eq 'APPEND') {
                $variables[$tokens[1]] = @($variables[$tokens[1]]) + @($tokens | Select-Object -Skip 2)
            }
            continue
        }
        $targets = @(Expand-DriverInput $tokens[0])
        if ($targets.Count -ne 1) {
            $violations.Add('Driver target expression could not be audited.')
            continue
        }
        if ($targets[0] -cne 'TimoxVasio') { continue }
        if ($kind -eq 'add_library') { $driverFound = $true }
        $allowed = if ($kind -eq 'target_link_libraries') { $allowedLibraries } else { $allowedSources }
        foreach ($token in @($tokens | Select-Object -Skip 1)) {
            if ($token -in @('PRIVATE', 'PUBLIC', 'INTERFACE', 'SHARED')) { continue }
            foreach ($input in @(Expand-DriverInput $token)) {
                if ($input -cnotin $allowed) {
                    $violations.Add("Unapproved driver $kind input: $input")
                }
            }
        }
    }
    if (-not $driverFound) { $violations.Add('TimoxVasio target declaration could not be audited.') }
    $violations | Sort-Object -Unique
}
