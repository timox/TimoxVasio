#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/vm-lab-signing.psm1') -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Join-Path ([IO.Path]::GetTempPath()) ('TimoxToolTests-'+[Guid]::NewGuid().ToString('N'))
$failures=0
function Check($condition,$label){if(-not $condition){$script:failures++;Write-Host "FAIL: $label"}}
function Refuses([scriptblock]$action,$label){$refused=$false;try{& $action | Out-Null}catch{$refused=$true};Check $refused $label}
try {
    $inputDirectory=Join-Path $root 'input'
    $toolDirectory=Join-Path $root 'tools'
    New-Item -ItemType Directory -Path $inputDirectory,$toolDirectory | Out-Null
    foreach($name in @('Inf2Cat.exe','dependency.dll')){
        [IO.File]::WriteAllText((Join-Path $inputDirectory $name),('fixture '+$name))
        Copy-Item -LiteralPath (Join-Path $inputDirectory $name) -Destination (Join-Path $toolDirectory $name)
    }
    $archive=Join-Path $root 'fixture.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($inputDirectory,$archive)
    try {Assert-TimoxToolArchiveContents -ArchivePath $archive -EntryPrefix '' -ToolDirectory $toolDirectory}catch{Check $false "matching archive and files: $_"}
    Refuses {Get-TimoxPinnedInf2Cat -PackagePath $archive -ToolPath (Join-Path $toolDirectory 'Inf2Cat.exe')} 'Unapproved archive cannot authorize tool'
    [IO.File]::AppendAllText((Join-Path $toolDirectory 'dependency.dll'),'modified')
    Refuses {Assert-TimoxToolArchiveContents -ArchivePath $archive -EntryPrefix '' -ToolDirectory $toolDirectory} 'Modified sibling DLL rejected'
    Copy-Item -LiteralPath (Join-Path $inputDirectory 'dependency.dll') -Destination (Join-Path $toolDirectory 'dependency.dll') -Force
    [IO.File]::WriteAllText((Join-Path $toolDirectory 'extra.dll'),'extra')
    Refuses {Assert-TimoxToolArchiveContents -ArchivePath $archive -EntryPrefix '' -ToolDirectory $toolDirectory} 'Extra DLL rejected'
    Remove-Item -LiteralPath (Join-Path $toolDirectory 'extra.dll')
    Remove-Item -LiteralPath (Join-Path $toolDirectory 'dependency.dll')
    Refuses {Assert-TimoxToolArchiveContents -ArchivePath $archive -EntryPrefix '' -ToolDirectory $toolDirectory} 'Missing DLL rejected'
    Copy-Item -LiteralPath (Join-Path $inputDirectory 'dependency.dll') -Destination (Join-Path $toolDirectory 'dependency.dll')
    $link=Join-Path $root 'junction'
    New-Item -ItemType Junction -Path $link -Target $toolDirectory | Out-Null
    try {Refuses {Assert-TimoxToolArchiveContents -ArchivePath $archive -EntryPrefix '' -ToolDirectory $link} 'Reparse directory rejected'}finally{[IO.Directory]::Delete($link)}
}finally{
    $absolute=[IO.Path]::GetFullPath($root)
    $temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(-not $absolute.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($absolute) -notmatch '^TimoxToolTests-[0-9a-f]{32}$'){throw 'Unsafe fixture cleanup'}
    if(Test-Path -LiteralPath $absolute){Remove-Item -LiteralPath $absolute -Recurse -Force}
}
Write-Host "Tool provenance fixture failures=$failures"
if($failures){exit 1}
