$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'TimoxDllIdentity.ps1')
. (Join-Path $PSScriptRoot 'ClockComparison.ps1')
. (Join-Path $PSScriptRoot 'MidiExpectedPort.ps1')

$options = [ordered]@{ json = $false; output = $null; expected = $null;
    probe = $null; against = $null; probePath = $null; listen = $null; listenSeconds = 0;
    statusPath = $null; usbPath = $null; comparePath = $null;
    apiPort = 52525; clockSeconds = 600 }
for ($index = 0; $index -lt $args.Count; $index++) {
    $arg = [string]$args[$index]
    switch ($arg) {
        '--json' { $options.json = $true }
        '--output' { if (++$index -ge $args.Count) { throw '--output requires a path' }; $options.output = $args[$index] }
        '--expected' { if (++$index -ge $args.Count) { throw '--expected requires a path' }; $options.expected = $args[$index] }
        '--probe' { if (++$index -ge $args.Count) { throw '--probe requires a driver name' }; $options.probe = $args[$index] }
        '--against' { if (++$index -ge $args.Count) { throw '--against requires a driver name' }; $options.against = $args[$index] }
        '--probe-path' { if (++$index -ge $args.Count) { throw '--probe-path requires a path' }; $options.probePath = $args[$index] }
        '--usb-path' { if (++$index -ge $args.Count) { throw '--usb-path requires a path' }; $options.usbPath = $args[$index] }
        '--compare-path' { if (++$index -ge $args.Count) { throw '--compare-path requires a path' }; $options.comparePath = $args[$index] }
        '--clock-seconds' { if (++$index -ge $args.Count) { throw '--clock-seconds requires a duration' }; $options.clockSeconds = [int]$args[$index] }
        '--status-path' { if (++$index -ge $args.Count) { throw '--status-path requires a path' }; $options.statusPath = $args[$index] }
        '--listen' {
            if (($index + 2) -ge $args.Count) { throw '--listen requires a port and seconds' }
            $options.listen = $args[++$index]
            $options.listenSeconds = [int]$args[++$index]
        }
        '--api-port' { if (++$index -ge $args.Count) { throw '--api-port requires a value' }; $options.apiPort = [int]$args[$index] }
        default { throw "Unknown option: $arg" }
    }
}
if ($options.listen -and ($options.listenSeconds -lt 1 -or $options.listenSeconds -gt 86400)) {
    throw '--listen seconds must be between 1 and 86400'
}
if ($options.apiPort -lt 1 -or $options.apiPort -gt 65535) { throw 'Invalid API port' }
if ($options.clockSeconds -lt 60 -or $options.clockSeconds -gt 86400) {
    throw '--clock-seconds must be between 60 and 86400'
}
if ($options.against -and (-not $options.probe -or $options.against -eq $options.probe)) {
    throw '--against requires --probe with a different driver name'
}

function Get-PeArchitecture([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    $stream = [System.IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 64) { return 'invalid' }
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { return 'invalid' }
        $stream.Position = 0x3C
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or ($offset + 6) -gt $stream.Length) { return 'invalid' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550) { return 'invalid' }
        switch ($reader.ReadUInt16()) {
            0x014c { return 'x86' }
            0x8664 { return 'x64' }
            0xaa64 { return 'arm64' }
            default { return 'unknown' }
        }
    } finally { $stream.Dispose() }
}

function Get-AsioDrivers {
    $drivers = [System.Collections.Generic.List[object]]::new()
    foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64,
                        [Microsoft.Win32.RegistryView]::Registry32)) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::LocalMachine, $view)
        try {
            $asio = $base.OpenSubKey('SOFTWARE\ASIO')
            if (-not $asio) { continue }
            foreach ($name in $asio.GetSubKeyNames()) {
                $key = $asio.OpenSubKey($name)
                $clsid = [string]$key.GetValue('CLSID', '')
                $description = [string]$key.GetValue('Description', '')
                $key.Dispose()
                $rawPath = $null
                if ($clsid) {
                    $com = $base.OpenSubKey("SOFTWARE\Classes\CLSID\$clsid\InprocServer32")
                    if ($com) { $rawPath = [string]$com.GetValue('', ''); $com.Dispose() }
                }
                $path = if ($rawPath) { [Environment]::ExpandEnvironmentVariables($rawPath.Trim('"')) } else { $null }
                $present = [bool]($path -and (Test-Path -LiteralPath $path -PathType Leaf))
                $drivers.Add([ordered]@{ name = $name; registryView = $view.ToString();
                    description = $description; clsid = $clsid; dll = $path;
                    filePresent = $present; architecture = if ($present) { Get-PeArchitecture $path } else { $null } })
            }
            $asio.Dispose()
        } finally { $base.Dispose() }
    }
    return $drivers.ToArray()
}

function Get-AsioUsers($drivers) {
    $paths = @($drivers | Where-Object filePresent | ForEach-Object { $_.dll.ToLowerInvariant() })
    $users = [System.Collections.Generic.List[object]]::new()
    $denied = 0
    $inaccessible = [System.Collections.Generic.List[object]]::new()
    foreach ($process in @(Get-Process)) {
        try {
            $moduleCount = 0
            foreach ($module in $process.Modules) {
                $moduleCount++
                $path = [string]$module.FileName
                if ($paths -contains $path.ToLowerInvariant() -or
                    [IO.Path]::GetFileName($path) -ieq 'TimoxVasio.dll') {
                    $users.Add([ordered]@{ pid = $process.Id; process = $process.ProcessName;
                        dll = $path })
                }
            }
            if ($moduleCount -eq 0) {
                $denied++
                $inaccessible.Add([ordered]@{ pid = $process.Id; process = $process.ProcessName })
            }
        } catch {
            $denied++
            $inaccessible.Add([ordered]@{ pid = $process.Id; process = $process.ProcessName })
        }
    }
    $protectedNames = @('csrss', 'Idle', 'Memory Compression', 'MpDefenderCoreService',
        'MsMpEng', 'Registry', 'services', 'smss', 'svchost', 'System', 'wininit')
    $potential = @($inaccessible | Where-Object {
        $_.pid -ne $PID -and $_.process -notin $protectedNames })
    return [ordered]@{ users = $users.ToArray(); inaccessibleProcesses = $denied;
        inaccessible = $inaccessible.ToArray(); inaccessiblePotential = $potential }
}

function Get-AudioEndpoints {
    $result = [System.Collections.Generic.List[object]]::new()
    $keyName = '{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0'
    $friendlyNameKey = '{a45c254e-df1c-4efd-8020-67d146a850e0},2'
    $deviceNameKey = '{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
    $pnpKey = '{a8b865dd-2e3d-4094-ad97-e593a70c75d6},8'
    foreach ($flow in @('Render', 'Capture')) {
        $root = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$flow"
        foreach ($endpoint in @(Get-ChildItem -LiteralPath $root -ErrorAction SilentlyContinue)) {
            $properties = Get-ItemProperty -LiteralPath (Join-Path $endpoint.PSPath 'Properties') -ErrorAction SilentlyContinue
            $value = $null
            if ($properties) { $value = $properties.PSObject.Properties[$keyName].Value }
            $rate = $null
            $channels = $null
            $bitsPerSample = $null
            $formatTag = $null
            $validBitsPerSample = $null
            $sampleEncoding = $null
            if ($value -is [byte[]] -and $value.Length -ge 24 -and
                [BitConverter]::ToUInt32($value, 0) -eq 0x41) {
                $candidate = [BitConverter]::ToUInt32($value, 12)
                if ($candidate -ge 8000 -and $candidate -le 768000) { $rate = $candidate }
                $formatTag = [BitConverter]::ToUInt16($value, 8)
                $channels = [BitConverter]::ToUInt16($value, 10)
                $bitsPerSample = [BitConverter]::ToUInt16($value, 22)
                if ($formatTag -eq 1) { $sampleEncoding = 'pcm-integer' }
                elseif ($formatTag -eq 3) { $sampleEncoding = 'ieee-float' }
                if ($formatTag -eq 0xFFFE -and $value.Length -ge 28) {
                    $validBitsPerSample = [BitConverter]::ToUInt16($value, 26)
                    if ($value.Length -ge 48) {
                        $subtype = [guid]::new([byte[]]$value[32..47]).ToString()
                        if ($subtype -eq '00000001-0000-0010-8000-00aa00389b71') {
                            $sampleEncoding = 'pcm-integer'
                        } elseif ($subtype -eq '00000003-0000-0010-8000-00aa00389b71') {
                            $sampleEncoding = 'ieee-float'
                        } else { $sampleEncoding = "subformat:$subtype" }
                    }
                }
            }
            $result.Add([ordered]@{ id = $endpoint.PSChildName; flow = $flow.ToLowerInvariant();
                name = if ($properties) { $properties.PSObject.Properties[$friendlyNameKey].Value } else { $null };
                deviceName = if ($properties) { $properties.PSObject.Properties[$deviceNameKey].Value } else { $null };
                pnpId = if ($properties) { $properties.PSObject.Properties[$pnpKey].Value } else { $null };
                state = (Get-ItemProperty -LiteralPath $endpoint.PSPath).DeviceState;
                sharedModeSampleRate = $rate; channels = $channels;
                bitsPerSample = $bitsPerSample; validBitsPerSample = $validBitsPerSample;
                formatTag = $formatTag; sampleEncoding = $sampleEncoding })
        }
    }
    return $result.ToArray()
}

function Get-AudioDefaults($endpoints) {
    try {
        Add-Type -Path (Join-Path $PSScriptRoot 'AudioDefaults.cs') -ErrorAction Stop
        $defaults = @([AudioDefaults]::Read() | ForEach-Object {
            $entry = $_
            $match = @($endpoints | Where-Object {
                $_.flow -eq $entry.Flow -and $entry.Id -and
                $entry.Id.EndsWith($_.id, [StringComparison]::OrdinalIgnoreCase)
            } | Select-Object -First 1)
            [ordered]@{ flow = $entry.Flow; role = $entry.Role; id = $entry.Id;
                name = if ($match.Count) { $match[0].name } else { $null };
                deviceName = if ($match.Count) { $match[0].deviceName } else { $null };
                sharedModeSampleRate = if ($match.Count) { $match[0].sharedModeSampleRate } else { $null };
                error = $entry.Error }
        })
        return [ordered]@{ defaults = $defaults; error = $null }
    } catch {
        return [ordered]@{ defaults = @(); error = $_.Exception.Message }
    }
}

function Get-AsioWindowsEndpointMatch($capsReport, $endpoints) {
    $matches = [System.Collections.Generic.List[object]]::new()
    foreach ($group in @(@{ flow = 'capture'; channels = @($capsReport.inputs) },
                         @{ flow = 'render'; channels = @($capsReport.outputs) })) {
        foreach ($channel in $group.channels) {
            $baseName = ([string]$channel.name) -replace ' [0-9]+$', ''
            foreach ($endpoint in @($endpoints | Where-Object {
                $_.state -eq 1 -and $_.flow -eq $group.flow -and $_.name -eq $baseName
            })) {
                $matches.Add([ordered]@{ asioChannel = $channel.name;
                    flow = $group.flow; endpointId = $endpoint.id;
                    endpointName = $endpoint.name; deviceName = $endpoint.deviceName;
                    pnpId = $endpoint.pnpId })
            }
        }
    }
    $devices = @($matches | ForEach-Object deviceName | Where-Object { $_ } | Select-Object -Unique)
    return [ordered]@{ method = 'ASIO channel name matched to active Windows endpoint name';
        matches = $matches.ToArray(); candidateDevices = $devices;
        conclusion = if ($devices.Count -eq 1) { $devices[0] } else { $null };
        certainty = 'heuristic; verify in the ASIO driver control panel' }
}

function Get-AudioHardware {
    $signed = @{}
    $devices = [System.Collections.Generic.List[object]]::new()
    try {
        foreach ($item in @(Get-CimInstance Win32_PnPSignedDriver -ErrorAction Stop)) {
            if ($item.DeviceID) { $signed[$item.DeviceID] = $item }
        }
        $soundDevices = @(Get-CimInstance Win32_SoundDevice -ErrorAction Stop)
    } catch {
        return [ordered]@{ devices = @(); error = $_.Exception.Message }
    }
    foreach ($device in $soundDevices) {
        $driver = $signed[$device.PNPDeviceID]
        $devices.Add([ordered]@{ name = $device.Name; pnpId = $device.PNPDeviceID;
            usb = [bool]($device.PNPDeviceID -match '^(USB|TUSBAUDIO_ENUM)\\'); status = $device.Status;
            driverVersion = if ($driver) { $driver.DriverVersion } else { $null };
            driverProvider = if ($driver) { $driver.DriverProviderName } else { $null } })
    }
    return [ordered]@{ devices = $devices.ToArray(); error = $null }
}

function Get-MidiInventory {
    Add-Type -Path (Join-Path $PSScriptRoot 'MidiWinmm.cs') -ErrorAction Stop
    $ports = @([MidiWinmm]::Enumerate() | ForEach-Object {
        $check = [MidiWinmm]::CheckOpen([uint32]$_.Id, $_.Flow)
        [ordered]@{ id = $_.Id; flow = $_.Flow; name = $_.Name;
            driverVersion = $_.DriverVersion; stack = 'WinMM (underlying stack unknown)';
            openCheck = $check.State; openStatus = $check.Status }
    })
    $service = Get-Service -Name MidiSrv -ErrorAction SilentlyContinue
    $midiServices = [ordered]@{ service = if ($service) { $service.Status.ToString() } else { 'not-installed' };
        endpoints = @(); sessions = @(); enumeration = 'unavailable' }
    if ($service -and $service.Status -eq 'Running') {
        try {
            Import-Module WindowsMidiServices -ErrorAction Stop
            $type = [Microsoft.Windows.Devices.Midi2.MidiEndpointDeviceInformation]
            $midiServices.endpoints = @($type::FindAll() | ForEach-Object {
                $transport = $_.GetTransportSuppliedInfo()
                [ordered]@{ name = $_.Name; endpointDeviceId = $_.EndpointDeviceId;
                    transportCode = $transport.TransportCode; transportId = $transport.TransportId }
            })
            $midiServices.enumeration = 'Windows MIDI Services SDK'
            foreach ($port in $ports) {
                $flow = if ($port.flow -eq 'input') {
                    [Microsoft.Windows.Devices.Midi2.Midi1PortFlow]::MidiMessageSource
                } else {
                    [Microsoft.Windows.Devices.Midi2.Midi1PortFlow]::MidiMessageDestination
                }
                $endpoint = $type::CreateFromAssociatedMidi1PortNumber([uint32]$port.id, $flow)
                if ($endpoint) {
                    $transport = $endpoint.GetTransportSuppliedInfo()
                    $port.stack = "Windows MIDI Services: $($transport.TransportCode)"
                    $port.endpointDeviceId = $endpoint.EndpointDeviceId
                }
            }
            if (Get-Command Get-MidiSession -ErrorAction SilentlyContinue) {
                $midiServices.sessions = @(Get-MidiSession | Select-Object Name, ProcessId, EndpointDeviceId)
            }
        } catch { $midiServices.enumeration = "SDK unavailable: $($_.Exception.Message)" }
    }
    return [ordered]@{ winmmPorts = $ports; windowsMidiServices = $midiServices }
}

function Get-VasioStatus([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return [ordered]@{ report = $null; error = 'vasio-status is not built' }
    }
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Resolve-Path -LiteralPath $path).Path
    $start.ArgumentList.Add('--json')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        if (-not $process.WaitForExit(5000)) {
            $process.Kill($true)
            [void]$process.WaitForExit(5000)
            return [ordered]@{ report = $null; error = 'vasio-status timed out' }
        }
        if ($process.ExitCode -ne 0) {
            return [ordered]@{ report = $null; error = $process.StandardError.ReadToEnd() }
        }
        return [ordered]@{ report = ($process.StandardOutput.ReadToEnd() | ConvertFrom-Json);
            error = $null }
    } finally { $process.Dispose() }
}

function Add-Finding($findings, [string]$severity, [string]$code, [string]$detail, [string]$action) {
    $findings.Add([ordered]@{ severity = $severity; code = $code;
        detail = $detail; action = $action })
}

$drivers = @(Get-AsioDrivers)
$usage = Get-AsioUsers $drivers
$endpoints = @(Get-AudioEndpoints)
$audioDefaults = Get-AudioDefaults $endpoints
$hardwareInventory = Get-AudioHardware
$hardware = @($hardwareInventory.devices)
$midi = Get-MidiInventory
$statusPath = if ($options.statusPath) { $options.statusPath } else {
    Join-Path $PSScriptRoot '..\..\build_vasio_probe\Release\vasio-status.exe' }
$virtualStatus = Get-VasioStatus $statusPath
$engine = $null
try { $engine = Invoke-RestMethod -Method Get -Uri "http://127.0.0.1:$($options.apiPort)/api/v1/state" -TimeoutSec 1 } catch {}
$expected = $null
if ($options.expected) { $expected = Get-Content -LiteralPath $options.expected -Raw | ConvertFrom-Json }
$findings = [System.Collections.Generic.List[object]]::new()

foreach ($driver in $drivers) {
    if (-not $driver.filePresent) {
        Add-Finding $findings red 'ASIO_DLL_MISSING' "$($driver.name) $($driver.registryView): $($driver.dll)" 'Reinstall or correct the ASIO registration.'
    }
}
$timox = @($drivers | Where-Object name -eq 'TimoxVasio')
if ($timox.Count -eq 0) {
    Add-Finding $findings red 'TIMOX_MISSING' 'TimoxVasio is not registered.' 'Install and register the TimoxVasio DLL.'
}
$timoxPaths = @($timox | Where-Object dll | ForEach-Object { $_.dll }) +
    @($usage.users | Where-Object { [IO.Path]::GetFileName($_.dll) -ieq 'TimoxVasio.dll' } |
        ForEach-Object { $_.dll }) +
    @($virtualStatus.report.clients | Where-Object { $_.statusAvailable -and $_.dllPath } |
        ForEach-Object { $_.dllPath })
$timoxDlls = @(Get-TimoxDllIdentity -Paths $timoxPaths)
$timoxDllFinding = Get-TimoxDllIdentityFinding -Identities $timoxDlls
if ($timoxDllFinding) {
    Add-Finding $findings $timoxDllFinding.severity $timoxDllFinding.code `
        $timoxDllFinding.detail $timoxDllFinding.action
}
if ($usage.inaccessibleProcesses -gt 0) {
    Add-Finding $findings orange 'MODULE_SCAN_INCOMPLETE' "$($usage.inaccessibleProcesses) processes could not be inspected." 'Run the doctor with rights sufficient to inspect those processes.'
}
if ($hardwareInventory.error) {
    Add-Finding $findings orange 'AUDIO_HARDWARE_INCOMPLETE' $hardwareInventory.error 'Rerun with permission to read Windows device inventory.'
}
if ($audioDefaults.error) {
    Add-Finding $findings orange 'AUDIO_DEFAULTS_INCOMPLETE' $audioDefaults.error 'Inspect Windows sound settings or rerun the doctor with access to Core Audio.'
}
if ($virtualStatus.error) {
    Add-Finding $findings orange 'VIRTUAL_STATUS_UNAVAILABLE' $virtualStatus.error 'Build vasio-status and rerun the doctor.'
} elseif ($virtualStatus.report) {
    foreach ($client in @($virtualStatus.report.clients)) {
        if (-not $client.statusAvailable) {
            Add-Finding $findings orange 'LEGACY_STATUS' "$($client.process) PID $($client.pid) uses shared layout v$($client.layoutVersion)." 'Restart the client after installing a v7 status-capable DLL and matching engine.'
        }
    }
    foreach ($issue in @($virtualStatus.report.alerts)) {
        Add-Finding $findings red $issue.code $issue.message 'Inspect the client, its routes and the recent status history.'
    }
    if ($expected -and $expected.maxExchangeAgeMs) {
        foreach ($client in @($virtualStatus.report.clients | Where-Object {
            $_.state -eq 'running' -and $_.statusAvailable -and
            $_.lastExchangeAgeMs -gt [double]$expected.maxExchangeAgeMs
        })) {
            Add-Finding $findings red 'STALE_CLIENT' "$($client.process) PID $($client.pid) has not exchanged audio for $([math]::Round($client.lastExchangeAgeMs)) ms." 'Inspect the host process and engine attachment; restart the affected client if needed.'
        }
    }
}
if ($expected) {
    foreach ($name in @($expected.audioDevices)) {
        if ($hardwareInventory.error) {
            Add-Finding $findings orange 'AUDIO_DEVICE_UNVERIFIED' "$name could not be checked because hardware inventory is unavailable." 'Rerun with permission to read Windows device inventory.'
        } elseif (-not @($hardware | Where-Object { $_.name -like "*$name*" }).Count) {
            Add-Finding $findings red 'AUDIO_DEVICE_MISSING' "$name is not present." 'Connect or enable the expected audio device.'
        }
    }
    foreach ($requirement in @($expected.midiPorts)) {
        $match = Get-MidiExpectedPortState -Requirement $requirement -Midi $midi
        $description = if ($match.flow) { "$($match.flow) $($match.name)" } else { $match.name }
        switch ($match.state) {
            'missing' {
                Add-Finding $findings red 'MIDI_PORT_MISSING' "$description is not present in WinMM or Windows MIDI Services." 'Connect or enable the expected MIDI port and inspect its driver.'
            }
            'unverified' {
                Add-Finding $findings orange 'MIDI_PORT_UNVERIFIED' "$description is absent from WinMM; Windows MIDI Services inventory is unavailable." 'Inspect this port in MIDI Settings or make Windows MIDI Services enumeration available.'
            }
            'flow-unverified' {
                Add-Finding $findings orange 'MIDI_PORT_FLOW_UNVERIFIED' "$description is present in Windows MIDI Services, but its input/output direction could not be verified." 'Inspect the endpoint direction in MIDI Settings.'
            }
        }
    }
}
$targetRate = if ($expected -and $expected.sampleRate) { [int]$expected.sampleRate } elseif ($engine -and $engine.engine.sampleRate) { [int]$engine.engine.sampleRate } else { 0 }
if ($targetRate) {
    foreach ($endpoint in @($endpoints | Where-Object { $_.state -eq 1 -and $_.sharedModeSampleRate -and $_.sharedModeSampleRate -ne $targetRate })) {
        Add-Finding $findings orange 'AUDIO_RATE_DIVERGENCE' "$($endpoint.flow) $($endpoint.name) ($($endpoint.deviceName), $($endpoint.id)): $($endpoint.sharedModeSampleRate) Hz vs $targetRate Hz." 'Review this Windows endpoint default format; change it only if this endpoint participates in the route.'
    }
}
if ($midi.windowsMidiServices.enumeration -ne 'Windows MIDI Services SDK') {
    Add-Finding $findings orange 'MIDI_SERVICES_INCOMPLETE' $midi.windowsMidiServices.enumeration 'Install a MIDI Services module with endpoint enumeration, or inspect endpoints in MIDI Settings.'
}
foreach ($port in @($midi.winmmPorts | Where-Object { $_.openCheck -eq 'exclusive-occupied' })) {
    Add-Finding $findings red 'MIDI_PORT_OCCUPIED' "$($port.flow) $($port.name) (WinMM ID $($port.id)) is already allocated." 'Close the application using this exclusive MIDI port, or choose another port.'
}
foreach ($port in @($midi.winmmPorts | Where-Object { $_.openCheck -eq 'open-failed' })) {
    Add-Finding $findings orange 'MIDI_PORT_OPEN_FAILED' "$($port.flow) $($port.name) (WinMM status $($port.openStatus)) could not be opened for a non-playing check." 'Inspect the MIDI driver and port state; the error does not prove another application owns it.'
}

$listenResult = $null
if ($options.listen) {
    $port = @($midi.winmmPorts | Where-Object {
        $_.flow -eq 'input' -and ($_.name -eq $options.listen -or [string]$_.id -eq $options.listen)
    })
    if ($port.Count -ne 1) { throw "MIDI input port not uniquely found: $($options.listen)" }
    $clock = [MidiWinmm]::Listen([uint32]$port[0].id, $options.listenSeconds)
    $listenResult = [ordered]@{ port = $port[0]; seconds = $options.listenSeconds;
        state = $clock.State; openError = $clock.OpenError; startError = $clock.StartError;
        clockCount = $clock.ClockCount; startCount = $clock.StartCount; stopCount = $clock.StopCount;
        tempoBpm = $clock.TempoBpm; jitterMs = $clock.JitterMs }
    if ($clock.OpenError -ne 0) {
        Add-Finding $findings red 'MIDI_PORT_OCCUPIED' "$($port[0].name) could not be opened (WinMM $($clock.OpenError))." 'Close the application holding this exclusive input, then retry --listen.'
    }
}

$profile = $null
if ($options.probe) {
    $primaryDriverRegistrations = @($drivers | Where-Object name -eq $options.probe)
    if ($primaryDriverRegistrations.Count -eq 0) { throw "ASIO driver not found: $($options.probe)" }
    $inUse = @($usage.users | Where-Object {
        $primaryDriverRegistrations.dll -contains $_.dll -or
        ($options.probe -eq 'TimoxVasio' -and
            [IO.Path]::GetFileName($_.dll) -ieq 'TimoxVasio.dll')
    })
    $engineClients = if ($options.probe -eq 'TimoxVasio' -and $engine) {
        @($engine.virtualDrivers | Where-Object id -eq 'TimoxVasio' |
            ForEach-Object { $_.clients } | Where-Object { $null -ne $_ })
    } else { @() }
    if ($engineClients.Count -or $inUse.Count -or @($usage.inaccessiblePotential).Count -gt 0) {
        $detail = if ($engineClients.Count) {
            "TimoxVasio has active client(s): $(@($engineClients | ForEach-Object { '{0} PID {1}' -f $_.processName, $_.pid }) -join ', ')."
        } elseif ($inUse.Count) { "$($options.probe) is loaded in $($inUse.Count) process(es)." }
            else { "Cannot prove that $($options.probe) is unused: $(@($usage.inaccessiblePotential | ForEach-Object process) -join ', ') could not be inspected." }
        Add-Finding $findings red 'PROBE_REFUSED_IN_USE' $detail 'Close the applications using this driver and rerun with enough rights to inspect process modules.'
    } else {
        $probePath = if ($options.probePath) { $options.probePath } else {
            Join-Path $PSScriptRoot '..\..\build_vasio_probe\Release\vasio-probe.exe' }
        if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) { throw "vasio-probe missing: $probePath" }
        . (Join-Path $PSScriptRoot '..\..\tests\probe_process.ps1')
        $profilesRoot = Join-Path $PSScriptRoot '..\..\tests\results\profiles'
        $thresholds = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\vasio-probe\thresholds.json')).Path
        New-Item -ItemType Directory -Path $profilesRoot -Force | Out-Null
        $safeName = $options.probe -replace '[^a-zA-Z0-9._-]', '_'
        $capsPath = Join-Path $profilesRoot "$safeName.caps.json"
        $capsExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
            'caps', '--driver', $options.probe, '--output', $capsPath
        ) -TimeoutSeconds 120
        $profile = [ordered]@{ driver = $options.probe; caps = $capsPath;
            capsExitCode = $capsExit; lifecycle = $null; cadence = $null;
            clockIdentification = $null;
            declaredBuffer = $null; sampleRates = @();
            rateAcceptance = @(); bufferAcceptance = @();
            liveReconfiguration = $null;
            simultaneousOpen = $null }
        if ($capsExit -ne 0 -and (Test-Path -LiteralPath $capsPath)) {
            $capsFailure = Get-Content -LiteralPath $capsPath -Raw | ConvertFrom-Json
            $detail = if ($capsFailure.driverError) { $capsFailure.driverError } else { $capsFailure.error }
            Add-Finding $findings orange 'PROBE_INIT_FAILED' "$($options.probe): $detail" 'Check the pilot configuration and the selected device before retrying.'
        }
        if ($capsExit -eq 0) {
            $capsReport = Get-Content -LiteralPath $capsPath -Raw | ConvertFrom-Json
            $profile.declaredBuffer = $capsReport.buffer
            $profile.sampleRates = @($capsReport.sampleRates)
            $lifePath = Join-Path $profilesRoot "$safeName.lifecycle.json"
            $lifeExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                'lifecycle', '--driver', $options.probe, '--cycles', '5',
                '--thresholds', $thresholds, '--output', $lifePath
            ) -TimeoutSeconds 120
            $profile.lifecycle = [ordered]@{ report = $lifePath; exitCode = $lifeExit }
        }
        if ($profile.lifecycle -and $profile.lifecycle.exitCode -eq 0) {
            $cadencePath = Join-Path $profilesRoot "$safeName.cadence.json"
            $cadenceExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                'cadence', '--driver', $options.probe, '--seconds', '60',
                '--thresholds', $thresholds, '--output', $cadencePath
            ) -TimeoutSeconds 180
            $profile.cadence = [ordered]@{ report = $cadencePath; exitCode = $cadenceExit }
            if ($cadenceExit -ne 0) {
                $cadenceReport = Get-Content -LiteralPath $cadencePath -Raw | ConvertFrom-Json
                $codes = @($cadenceReport.findings | ForEach-Object code) -join ', '
                if ($cadenceReport.verdict -eq 'fail') {
                    Add-Finding $findings red 'PROBE_CADENCE_FAILED' "$($options.probe): $codes" 'Review the measured callback timing and position data in the cadence report.'
                } else {
                    Add-Finding $findings orange 'PROBE_CADENCE_INCONCLUSIVE' "$($options.probe): $($cadenceReport.precondition)" 'Review callback timing and TimeInfo availability in the cadence report.'
                }
            }
            $clockPath = Join-Path $profilesRoot "$safeName.clock-$($options.clockSeconds)s.json"
            $clockReport = $null
            $clockExit = -1
            try {
                if ($options.clockSeconds -eq 60) {
                    $clockPath = $cadencePath
                    $clockExit = $cadenceExit
                } else {
                    $clockExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                        'cadence', '--driver', $options.probe, '--seconds',
                        $options.clockSeconds.ToString(), '--thresholds', $thresholds,
                        '--output', $clockPath
                    ) -TimeoutSeconds ($options.clockSeconds + 120)
                }
                $clockReport = Get-Content -LiteralPath $clockPath -Raw | ConvertFrom-Json
            } catch {
                Add-Finding $findings orange 'CLOCK_MEASUREMENT_FAILED' "$($options.probe): $($_.Exception.Message)" 'Inspect the clock run and retry when the driver is free.'
            }
            . (Join-Path $PSScriptRoot 'ClockMeasurementAcceptance.ps1')
            $clockAcceptance = Get-ClockMeasurementAcceptance -ExitCode $clockExit -Report $clockReport
            $endpointMatch = Get-AsioWindowsEndpointMatch $capsReport $endpoints
            $usbInfo = [ordered]@{ available = $false;
                reason = 'No uniquely matched active USB audio endpoint.'; report = $null }
            $matchedPnps = @($endpointMatch.matches | ForEach-Object pnpId |
                Where-Object { $_ } | Select-Object -Unique)
            if ($endpointMatch.conclusion -and $matchedPnps.Count -eq 1 -and
                $matchedPnps[0] -match 'VID_([0-9A-Fa-f]{4}).*PID_([0-9A-Fa-f]{4})') {
                $vid = $Matches[1]
                $devicePid = $Matches[2]
                $usbPath = if ($options.usbPath) { $options.usbPath } else {
                    Join-Path $PSScriptRoot '..\..\build_vasio_probe\Release\vasio-usb-descriptors.exe' }
                if (Test-Path -LiteralPath $usbPath -PathType Leaf) {
                    $usbReportPath = Join-Path $profilesRoot "$safeName.usb.json"
                    try {
                        $usbExit = Invoke-ProbeProcess -FilePath $usbPath -Arguments @(
                            '--vid', $vid, '--pid', $devicePid, '--output', $usbReportPath
                        ) -TimeoutSeconds 30
                        $usbReport = Get-Content -LiteralPath $usbReportPath -Raw | ConvertFrom-Json
                        $usbInfo = [ordered]@{ available = [bool]$usbReport.available;
                            report = $usbReportPath; exitCode = $usbExit;
                            devices = @($usbReport.devices);
                            hubErrors = @($usbReport.hubErrors);
                            reason = if ($usbReport.available) { $null } else {
                                'No matching USB device descriptor could be read.' } }
                    } catch {
                        $usbInfo.reason = $_.Exception.Message
                    }
                } else { $usbInfo.reason = "USB descriptor reader is not built: $usbPath" }
            }
            $cadenceSource = $null
            if ($options.probe -eq 'TimoxVasio' -and $engine) {
                $selectedId = $engine.engine.physicalDriverId
                $selectedDriver = @($engine.physicalDrivers | Where-Object id -eq $selectedId |
                    Select-Object -First 1)
                $cadenceSource = [ordered]@{
                    kind = 'selected-physical-asio-callback'
                    physicalDriverId = $selectedId
                    physicalDriverName = if ($selectedDriver.Count) { $selectedDriver[0].name } else { $null }
                    engineState = $engine.engine.state
                    callbackActive = $engine.engine.state -eq 'running'
                }
            }
            $profile.clockIdentification = [ordered]@{
                declared = $capsReport.clockSources;
                windowsEndpointMatch = $endpointMatch;
                usbDescriptor = $usbInfo;
                cadenceSource = $cadenceSource;
                measured = $clockAcceptance.regression;
                measurementUsable = $clockAcceptance.usable;
                measurementReason = $clockAcceptance.reason;
                measurementReport = $clockPath; measurementExitCode = $clockExit;
                against = $null;
                conclusion = 'indéterminée';
                reason = if ($cadenceSource) {
                    'TimoxVasio is paced by the selected physical ASIO callback; the selected device oscillator or external lock state is not yet proven.'
                } elseif ($usbInfo.available) {
                    'USB synchronization type is known, but the ASIO clock lock state and an independent simultaneous clock comparison are unavailable.'
                } else {
                    'Driver clock declaration and measured rate alone cannot identify the physical clock source without a matched USB descriptor or an independent simultaneous reference.'
                }
            }
            if (-not $usbInfo.available -and $matchedPnps.Count -eq 1 -and
                $matchedPnps[0] -match 'VID_[0-9A-Fa-f]{4}.*PID_[0-9A-Fa-f]{4}') {
                Add-Finding $findings orange 'USB_DESCRIPTOR_INCOMPLETE' "$($options.probe): $($usbInfo.reason)" 'Check the USB descriptor reader and access to the connected USB hubs.'
            }
            if (-not $clockAcceptance.usable) {
                Add-Finding $findings orange 'CLOCK_MEASUREMENT_INCOMPLETE' "$($options.probe): $($clockAcceptance.reason)" 'Review the clock measurement report and retry with a longer stable run.'
            }
        }
        $probeUnsafe = $false
        if ($capsExit -eq 0 -and $profile.lifecycle -and $profile.lifecycle.exitCode -eq 0) {
            . (Join-Path $PSScriptRoot 'AsioFormatAcceptance.ps1')
            $currentRate = [int]$capsReport.currentSampleRate.rate
            $preferredBuffer = [int]$capsReport.buffer.preferred
            foreach ($item in @($capsReport.sampleRates | Where-Object accepted)) {
                $rate = [int]$item.rate
                $ratePath = Join-Path $profilesRoot "$safeName.rate-$rate.json"
                try {
                    $rateExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                        'lifecycle', '--driver', $options.probe, '--cycles', '1',
                        '--rate', $rate.ToString(), '--buffer', $preferredBuffer.ToString(),
                        '--thresholds', $thresholds, '--output', $ratePath
                    ) -TimeoutSeconds 30
                    $rateReport = Get-Content -LiteralPath $ratePath -Raw | ConvertFrom-Json
                    $cycle = @($rateReport.cycles)[0]
                    $acceptance = Get-AsioFormatAcceptance -Cycle $cycle -Report $rateReport `
                        -ExitCode $rateExit -RequestedRate $rate
                    $profile.rateAcceptance += [ordered]@{ rate = $rate; declared = $true;
                        setStatus = $cycle.setRateStatus; confirmedRate = $cycle.confirmedRate;
                        restored = $cycle.rateRestored; bufferCreated = [bool](@($cycle.stages | Where-Object name -eq createBuffers)[0].success);
                        exitCode = $rateExit; report = $ratePath; acceptance = $acceptance }
                    if ($acceptance.state -eq 'inconclusive') {
                        Add-Finding $findings orange 'ASIO_DECLARED_RATE_UNVERIFIED' "$($options.probe) declared $rate Hz, but no callback proved playback at that rate." 'Check the physical device and active routing, then repeat the probe.'
                    } elseif (-not $acceptance.usable) {
                        Add-Finding $findings red 'ASIO_DECLARED_RATE_REFUSED' "$($options.probe) declared $rate Hz, but the format was not usable ($($acceptance.state))." 'Use a rate that starts with buffers and callbacks, or update its driver.'
                    }
                    if ($cycle.rateRestored -eq $false) {
                        Add-Finding $findings red 'PROBE_RATE_NOT_RESTORED' "$($options.probe): $rate Hz test could not restore the original rate." 'Inspect the driver control panel before further tests.'
                        $probeUnsafe = $true
                        break
                    }
                } catch {
                    $profile.rateAcceptance += [ordered]@{ rate = $rate; declared = $true;
                        error = $_.Exception.Message; report = $ratePath }
                    Add-Finding $findings red 'PROBE_RATE_CHECK_FAILED' "$($options.probe): $rate Hz check failed or timed out." 'Inspect the driver and retry only after verifying its current rate.'
                    $probeUnsafe = $true
                    break
                }
            }
            . (Join-Path $PSScriptRoot 'AsioBufferSizes.ps1')
            $buffer = $capsReport.buffer
            $bufferCandidates = @($buffer.minimum, $buffer.preferred, $buffer.maximum, 64, 128, 256, 512) |
                Where-Object { Test-AsioDeclaredBufferSize -Frames $_ -Buffer $buffer } |
                Sort-Object -Unique
            foreach ($frames in $(if ($probeUnsafe) { @() } else { $bufferCandidates })) {
                $frameCount = [int]$frames
                $bufferPath = Join-Path $profilesRoot "$safeName.buffer-$frameCount.json"
                try {
                    $bufferExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                        'lifecycle', '--driver', $options.probe, '--cycles', '1',
                        '--rate', $currentRate.ToString(), '--buffer', $frameCount.ToString(),
                        '--thresholds', $thresholds, '--output', $bufferPath
                    ) -TimeoutSeconds 30
                    $bufferReport = Get-Content -LiteralPath $bufferPath -Raw | ConvertFrom-Json
                    $cycle = @($bufferReport.cycles)[0]
                    $acceptance = Get-AsioFormatAcceptance -Cycle $cycle -Report $bufferReport `
                        -ExitCode $bufferExit -RequestedRate $currentRate
                    $profile.bufferAcceptance += [ordered]@{ frames = $frameCount;
                        declared = $true; created = $acceptance.buffersCreated;
                        restored = $cycle.rateRestored; exitCode = $bufferExit;
                        report = $bufferPath; acceptance = $acceptance }
                    if ($acceptance.state -eq 'inconclusive') {
                        Add-Finding $findings orange 'ASIO_DECLARED_BUFFER_UNVERIFIED' "$($options.probe) declared $frameCount frames, but no callback proved playback at that size." 'Check the physical device and active routing, then repeat the probe.'
                    } elseif (-not $acceptance.usable) {
                        Add-Finding $findings red 'ASIO_DECLARED_BUFFER_REFUSED' "$($options.probe) declared $frameCount frames, but the format was not usable ($($acceptance.state))." 'Use a size that starts with buffers and callbacks, or update its driver.'
                    }
                    if ($cycle.rateRestored -eq $false) {
                        Add-Finding $findings red 'PROBE_RATE_NOT_RESTORED' "$($options.probe): $frameCount-frame test could not restore the original rate." 'Inspect the driver control panel before further tests.'
                        $probeUnsafe = $true
                        break
                    }
                } catch {
                    $profile.bufferAcceptance += [ordered]@{ frames = $frameCount;
                        declared = $true; error = $_.Exception.Message; report = $bufferPath }
                    Add-Finding $findings red 'PROBE_BUFFER_CHECK_FAILED' "$($options.probe): $frameCount-frame check failed or timed out." 'Inspect the driver before retrying this size.'
                    $probeUnsafe = $true
                    break
                }
            }
            $profile.liveReconfiguration = [ordered]@{
                rate = [ordered]@{ attempted = $false;
                    reason = 'No accepted alternative sample rate is available.' };
                buffer = [ordered]@{ attempted = $false;
                    reason = 'ASIO has no host-side setBufferSize call. A driver-panel or device event is required to test a driver-initiated buffer reset.' }
            }
            $alternativeRate = @($capsReport.sampleRates | Where-Object {
                $_.accepted -and [int]$_.rate -ne $currentRate
            } | Select-Object -First 1)
            if (-not $probeUnsafe -and $alternativeRate.Count -eq 1) {
                $targetLiveRate = [int]$alternativeRate[0].rate
                $livePath = Join-Path $profilesRoot "$safeName.live-rate-$targetLiveRate.json"
                try {
                    $liveExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                        'lifecycle', '--driver', $options.probe, '--cycles', '1',
                        '--live-rate', $targetLiveRate.ToString(),
                        '--thresholds', $thresholds, '--output', $livePath
                    ) -TimeoutSeconds 45
                    $liveReport = Get-Content -LiteralPath $livePath -Raw | ConvertFrom-Json
                    $liveCycle = @($liveReport.cycles)[0]
                    $profile.liveReconfiguration.rate = [ordered]@{
                        attempted = $true; targetRate = $targetLiveRate;
                        exitCode = $liveExit; report = $livePath;
                        result = $liveCycle.liveRateChange;
                        originalRate = $liveCycle.originalRate;
                        restored = $liveCycle.rateRestored;
                        precondition = $liveReport.precondition }
                    if ($liveCycle.rateRestored -eq $false) {
                        Add-Finding $findings red 'LIVE_RATE_NOT_RESTORED' "$($options.probe): the live rate test could not restore $currentRate Hz." 'Inspect the driver control panel before another probe.'
                        $probeUnsafe = $true
                    }
                    if (-not $liveCycle.liveRateChange) {
                        Add-Finding $findings orange 'LIVE_RATE_INCONCLUSIVE' "$($options.probe): no live rate change was attempted after start." 'Review the callback precondition and the live-rate report.'
                    }
                } catch {
                    $profile.liveReconfiguration.rate = [ordered]@{
                        attempted = $true; targetRate = $targetLiveRate;
                        error = $_.Exception.Message; report = $livePath }
                    Add-Finding $findings red 'LIVE_RATE_CHECK_FAILED' "$($options.probe): the live rate trial failed or timed out." 'Inspect and restore the driver rate before further probing.'
                    $probeUnsafe = $true
                }
            }
        }
        if (-not $probeUnsafe -and $capsExit -eq 0 -and $profile.lifecycle -and $profile.lifecycle.exitCode -eq 0) {
            $primaryPath = Join-Path $profilesRoot "$safeName.simultaneous-primary.json"
            $secondaryPath = Join-Path $profilesRoot "$safeName.simultaneous-secondary.json"
            $start = [System.Diagnostics.ProcessStartInfo]::new()
            $start.FileName = $probePath
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $true
            $start.RedirectStandardOutput = $true
            $start.RedirectStandardError = $true
            foreach ($argument in @('cadence', '--driver', $options.probe, '--seconds',
                '20', '--thresholds', $thresholds, '--output', $primaryPath)) {
                [void]$start.ArgumentList.Add($argument)
            }
            $primary = [System.Diagnostics.Process]::Start($start)
            try {
                Start-Sleep -Seconds 2
                if ($primary.HasExited) {
                    $profile.simultaneousOpen = [ordered]@{ primaryExitedEarly = $true;
                        primaryExitCode = $primary.ExitCode; secondaryAttempted = $false }
                } else {
                    try {
                        $secondaryExit = Invoke-ProbeProcess -FilePath $probePath -Arguments @(
                            'caps', '--driver', $options.probe, '--output', $secondaryPath
                        ) -TimeoutSeconds 8
                        $secondary = Get-Content -LiteralPath $secondaryPath -Raw | ConvertFrom-Json
                        $profile.simultaneousOpen = [ordered]@{ primaryExitedEarly = $false;
                            secondaryAttempted = $true; secondaryExitCode = $secondaryExit;
                            secondaryInitialized = $secondary.initialized;
                            secondaryOpenStatus = $secondary.openStatus; report = $secondaryPath }
                    } catch {
                        $profile.simultaneousOpen = [ordered]@{ primaryExitedEarly = $false;
                            secondaryAttempted = $true; secondaryError = $_.Exception.Message }
                    }
                }
                if (-not $primary.WaitForExit(30000)) {
                    $primary.Kill($true)
                    [void]$primary.WaitForExit(10000)
                    $profile.simultaneousOpen.primaryTimedOut = $true
                }
                $profile.simultaneousOpen.primaryExitCode = $primary.ExitCode
                $profile.simultaneousOpen.primaryReport = $primaryPath
            } finally {
                if (-not $primary.HasExited) { $primary.Kill($true); [void]$primary.WaitForExit(10000) }
                [IO.File]::WriteAllText((Join-Path $profilesRoot "$safeName.simultaneous-primary.stdout.txt"),
                    $primary.StandardOutput.ReadToEnd())
                [IO.File]::WriteAllText((Join-Path $profilesRoot "$safeName.simultaneous-primary.stderr.txt"),
                    $primary.StandardError.ReadToEnd())
                $primary.Dispose()
            }
        }
        if ($options.against) {
            $referenceMatches = @($drivers | Where-Object name -eq $options.against)
            if (-not $referenceMatches.Count) {
                Add-Finding $findings red 'AGAINST_DRIVER_MISSING' "$($options.against) is not registered." 'Select a registered independent ASIO driver.'
            } elseif ($probeUnsafe -or -not $profile.clockIdentification) {
                Add-Finding $findings orange 'AGAINST_NOT_RUN' 'The primary profile did not reach a safe clock-comparison state.' 'Resolve the primary probe failure before comparing clocks.'
            } elseif (@($primaryDriverRegistrations | ForEach-Object dll | Where-Object {
                $_ -in @($referenceMatches | ForEach-Object dll)
            }).Count) {
                Add-Finding $findings red 'AGAINST_SAME_DLL' 'Both selected ASIO names resolve to the same DLL.' 'Choose a separate ASIO driver and hardware clock.'
            } else {
                $latestUsage = Get-AsioUsers $drivers
                $selectedDlls = @($primaryDriverRegistrations | ForEach-Object dll) +
                    @($referenceMatches | ForEach-Object dll)
                $busy = @($latestUsage.users | Where-Object { $_.dll -in $selectedDlls })
                $virtualBusy = ($options.probe -eq 'TimoxVasio' -or
                    $options.against -eq 'TimoxVasio') -and
                    @($virtualStatus.report.clients).Count -gt 0
                if ($busy.Count -or $virtualBusy -or
                    @($latestUsage.inaccessiblePotential).Count) {
                    Add-Finding $findings red 'AGAINST_REFUSED_IN_USE' 'A selected driver is in use, or process-module access cannot prove both are free.' 'Close clients of both drivers and rerun with sufficient process inspection rights.'
                } else {
                    $safeReference = $options.against -replace '[^a-zA-Z0-9._-]', '_'
                    try {
                        $comparePath = if ($options.comparePath) { $options.comparePath } else {
                            Join-Path $PSScriptRoot '..\..\build_vasio_probe\Release\vasio-clock-compare.exe' }
                        if (-not (Test-Path -LiteralPath $comparePath -PathType Leaf)) {
                            throw "vasio-clock-compare missing: $comparePath"
                        }
                        $pair = Invoke-ClockPair -ProbePath $probePath -PrimaryDriver $options.probe `
                            -ReferenceDriver $options.against -Seconds $options.clockSeconds `
                            -ThresholdsPath $thresholds -ProfilesRoot $profilesRoot `
                            -SafePrimary $safeName -SafeReference $safeReference
                        $comparisonPath = Join-Path $profilesRoot "$safeName.against-$safeReference.comparison.json"
                        $comparisonExit = Invoke-ProbeProcess -FilePath $comparePath -Arguments @(
                            '--primary', $pair.primaryReport, '--reference', $pair.referenceReport,
                            '--thresholds', $thresholds, '--output', $comparisonPath
                        ) -TimeoutSeconds 120
                        if ($comparisonExit -ne 0) {
                            throw "vasio-clock-compare exited $comparisonExit"
                        }
                        $pair['comparisonReport'] = $comparisonPath
                        $pair['comparison'] = Get-Content -LiteralPath $comparisonPath -Raw | ConvertFrom-Json
                        $profile.clockIdentification.against = $pair
                        if (-not $pair.comparison.available) {
                            Add-Finding $findings orange 'AGAINST_INCONCLUSIVE' $pair.comparison.reason 'Review both cadence reports and repeat with stable simultaneous acquisition.'
                        }
                    } catch {
                        $profile.clockIdentification.against = [ordered]@{
                            available = $false; error = $_.Exception.Message }
                        Add-Finding $findings red 'AGAINST_FAILED' $_.Exception.Message 'Check both ASIO drivers, their device selection and the paired probe logs.'
                    }
                }
            }
        }
        $profilePath = Join-Path $profilesRoot "$safeName.json"
        $profile | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $profilePath -Encoding utf8
        $profile.profilePath = $profilePath
        if (@($profile.rateAcceptance).Count -eq 0 -or @($profile.bufferAcceptance).Count -eq 0 -or
            -not $profile.simultaneousOpen -or $profile.simultaneousOpen.primaryExitedEarly -or
            -not $profile.simultaneousOpen.secondaryAttempted) {
            Add-Finding $findings orange 'PROBE_PROFILE_PARTIAL' "$($options.probe) profile could not complete its rate, buffer or simultaneous-open checks." 'Review the profile and rerun after resolving the recorded prerequisite.'
        }
        if ($profile.liveReconfiguration -and -not $profile.liveReconfiguration.buffer.attempted) {
            Add-Finding $findings orange 'LIVE_BUFFER_CHANGE_UNTESTED' "$($options.probe): a driver-initiated buffer-size change was not triggered." 'Use the driver control panel during a dedicated live probe, then inspect kAsioResetRequest and restart behavior.'
        }
    }
}

$severity = if (@($findings | Where-Object severity -eq 'red').Count) { 'red' }
    elseif (@($findings | Where-Object severity -eq 'orange').Count) { 'orange' }
    else { 'green' }
$report = [ordered]@{ schemaVersion = 1; createdUtc = [DateTime]::UtcNow.ToString('o');
    verdict = $severity; expected = $expected; expectedSampleRate = $targetRate;
    asioDrivers = $drivers; asioUsers = $usage; timoxDlls = $timoxDlls;
    audio = [ordered]@{ endpoints = $endpoints; defaults = $audioDefaults.defaults;
        defaultsError = $audioDefaults.error; hardware = $hardware;
        hardwareInventoryError = $hardwareInventory.error };
    midi = $midi; virtualStatus = $virtualStatus; listen = $listenResult;
    probe = $profile; findings = $findings.ToArray() }
$reportJson = $report | ConvertTo-Json -Depth 16
if ($options.output) { $reportJson | Set-Content -LiteralPath $options.output -Encoding utf8 }
if ($options.json) { $reportJson }
else {
    Write-Host "vasio-doctor: $($report.verdict)" -ForegroundColor $(@{green='Green';orange='Yellow';red='Red'}[$severity])
    foreach ($finding in $findings) {
        Write-Host "[$($finding.severity)] $($finding.code): $($finding.detail)" -ForegroundColor $(@{green='Green';orange='Yellow';red='Red'}[$finding.severity])
        Write-Host "  Action: $($finding.action)"
    }
    Write-Host "ASIO drivers: $($drivers.Count); Windows endpoints: $($endpoints.Count); WinMM ports: $($midi.winmmPorts.Count)"
}
if ($severity -eq 'red') { exit 2 }
