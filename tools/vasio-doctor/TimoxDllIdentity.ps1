function Get-TimoxDllIdentity {
    param([string[]]$Paths)

    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $result = [System.Collections.Generic.List[object]]::new()
    foreach ($path in $Paths) {
        if (-not $path -or -not $seen.Add($path)) { continue }
        $entry = [ordered]@{ path = $path; filePresent = $false;
            fileVersion = $null; sha256 = $null; error = $null }
        try {
            $file = Get-Item -LiteralPath $path -ErrorAction Stop
            if ($file -isnot [System.IO.FileInfo]) { throw 'Path is not a file' }
            $entry.path = $file.FullName
            $entry.filePresent = $true
            $entry.fileVersion = $file.VersionInfo.FileVersion
            $entry.sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256 -ErrorAction Stop).Hash
        } catch {
            $entry.error = $_.Exception.Message
        }
        $result.Add($entry)
    }
    return $result.ToArray()
}

function Get-TimoxDllIdentityFinding {
    param([object[]]$Identities)

    $hashes = @($Identities | Where-Object sha256 | ForEach-Object sha256 | Select-Object -Unique)
    if ($hashes.Count -gt 1) {
        $detail = @($Identities | Where-Object sha256 | ForEach-Object {
            "$($_.path) [$($_.fileVersion); $($_.sha256)]"
        }) -join ', '
        return [ordered]@{ severity = 'red'; code = 'MULTIPLE_TIMOX_DLLS';
            detail = $detail;
            action = 'Point both registry views to the intended build; restart clients holding other DLLs.' }
    }
    if (@($Identities | Where-Object { -not $_.sha256 }).Count -gt 0) {
        $detail = @($Identities | Where-Object { -not $_.sha256 } | ForEach-Object {
            "$($_.path): $($_.error)"
        }) -join ', '
        return [ordered]@{ severity = 'orange'; code = 'TIMOX_DLL_IDENTITY_UNVERIFIED';
            detail = $detail; action = 'Make the listed DLL files readable and rerun the doctor.' }
    }
    return $null
}
