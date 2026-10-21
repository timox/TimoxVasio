param([string]$NativeMessages)
$ErrorActionPreference = 'Stop'
$schema = Join-Path $PSScriptRoot '../api/schemas/api-v1.json'
function Assert-Contract($Value, [bool]$Expected, [string]$Label) {
    $valid = ($Value | ConvertTo-Json -Depth 30 -Compress) | Test-Json -SchemaFile $schema -ErrorAction SilentlyContinue
    if ($valid -ne $Expected) { throw "Contract assertion failed: $Label" }
}
$bridge = @{id='speakers';kind='render';endpointId='opaque';enabled=$false;bufferDuration100ns=0}
$request = @{id='apply';command='configuration.apply';payload=@{
    clockSource=@{kind='software'};physicalDriverId=$null;sampleRate=48000;bufferFrames=256;routes=@();wasapiBridges=@($bridge)
}}
Assert-Contract $request $true 'explicit endpoint bridge'
$request.payload.wasapiBridges = @(@{id='loop';kind='endpointLoopback';defaultRole='multimedia'})
Assert-Contract $request $true 'default role with optional enabled/duration'
$request.payload.wasapiBridges = @()
Assert-Contract $request $true 'explicit removal'
$request.payload.Remove('wasapiBridges')
Assert-Contract $request $true 'legacy omission'
foreach ($invalid in @(
    @{id='bad:id';kind='capture';endpointId='a'},
    @{id='a';kind='capture';endpointId=''},
    @{id='a';kind='capture';endpointId=$null},
    @{id='a';kind='capture';endpointId="a$([char]0)b"},
    @{id='a';kind='capture';defaultRole='future'},
    @{id='a';kind='capture';endpointId='a';defaultRole='console'},
    @{id='a';kind='capture'},
    @{id='a';kind='processLoopback';endpointId='a'},
    @{id='a';kind='render';endpointId='a';enabled=1},
    @{id='a';kind='render';endpointId='a';unknown=$true},
    @{id='a';kind='render';endpointId='a';bufferDuration100ns=-1},
    @{id='a';kind='render';endpointId='a';bufferDuration100ns=10000001},
    @{id='a';kind='render';endpointId='a';bufferDuration100ns=0.5}
)) {
    $request.payload.wasapiBridges = @($invalid)
    Assert-Contract $request $false 'malformed bridge'
}
$request.payload.wasapiBridges = @(1..33 | ForEach-Object { @{id="b$_";kind='capture';endpointId='a'} })
Assert-Contract $request $false 'too many bridges'
$format = @{sampleRate=48000;channels=2;containerBits=32;validBits=32;blockAlign=8;channelMask=3;encoding='ieeeFloat'}
$health = @{error=0;mmcssError=0;mmcssActive=$true;primingTimedOut=$false;capturedFrames=0;renderedFrames=256;
    droppedCaptureFrames=0;renderSilencedInputFrames=0;renderSilencedDeviceFrames=0;discontinuities=0;timestampErrors=0;
    ratio=1.0001;nominalRatio=1;correctionPpm=100;bufferedFrames=128;rejectedFrames=0;silencedFrames=0}
$effective = @{id='speakers';state='running';generationId='18446744073709551615';retryAttempt=0;retryInMs=0;
    effectiveInfo=@{endpointId='opaque';kind='render';format=$format;bufferFrames=480;streamLatency100ns=100000;defaultPeriod100ns=100000;minimumPeriod100ns=30000};
    graphFormat=@{sampleRate=48000;channels=2;maxQuantum=256;capacityFrames=2048};
    inputEndpoints=@();outputEndpoints=@(@{id='wasapi:speakers:output:1';name='speakers 1';direction='output';channel=1});health=$health}
$state = @{apiVersion='1.1';configurationRevision=2;engine=@{state='running';clockSource=@{kind='software'};physicalDriverId=$null;sampleRate=48000;bufferFrames=256;lastError=$null};
    physicalDrivers=@();virtualDrivers=@();routes=@();configuredRoutes=@();stereoPairs=@();bufferHealth=$null;
    performanceMeasurements=@{enabled=$false;sampleEveryCallbacks=16;window=$null};
    wasapiDevices=@(@{id='opaque';name='Speakers';flow='render';state=1;mixFormat=$format;error=$null});
    wasapiInventoryError=$null;configuredWasapiBridges=@($bridge);wasapiBridges=@($effective)}
Assert-Contract $state $true 'complete 1.1 state with exact generation string'
$effective.generationId=42
Assert-Contract $state $false 'numeric generation loses identity precision'
$effective.generationId='0';$effective.state='disabled';$effective.effectiveInfo=$null;$effective.graphFormat=$null
$effective.inputEndpoints=@();$effective.outputEndpoints=@();$health.nominalRatio=$null;$health.correctionPpm=$null
Assert-Contract $state $true 'disabled bridge retained without invented capabilities'
$state.Remove('wasapiInventoryError')
Assert-Contract $state $false '1.1 requires explicit inventory failure status'
$state.apiVersion='1.0';$state.Remove('wasapiDevices');$state.Remove('configuredWasapiBridges');$state.Remove('wasapiBridges')
Assert-Contract $state $true 'complete legacy state still accepted'
Assert-Contract @{wasapiDevices=@();wasapiInventoryError='Enumeration unavailable'} $true 'inventory error is distinct from empty success'
'WASAPI API 1.1 schema fixtures passed.'
if ($NativeMessages) {
    $actual = Get-Content -LiteralPath $NativeMessages -Raw | ConvertFrom-Json
    if ($actual.Count -lt 4) { throw 'Expected initial, configured, cleared and inventory HTTP messages.' }
    foreach ($message in $actual) { Assert-Contract $message $true 'actual native HTTP response' }
    "Actual native HTTP messages validated: $($actual.Count)"
}
