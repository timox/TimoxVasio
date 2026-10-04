$ErrorActionPreference = 'Stop'
$asioRoot = Split-Path -Parent $PSScriptRoot
$apiPath = Join-Path $asioRoot 'API.md'
$schemaPath = Join-Path $asioRoot 'schemas\api-v1.json'
$openApiPath = Join-Path $asioRoot 'openapi-v1.json'
$engineSourcePath = Join-Path $asioRoot 'src\main.cpp'
$api = Get-Content -LiteralPath $apiPath -Raw
$engineSource = Get-Content -LiteralPath $engineSourcePath -Raw

foreach ($obsolete in 'routes.add', 'std::getline(std::cin', 'routing.ini', 'RegisterDrivers') {
    if ($engineSource.Contains($obsolete)) { throw "Legacy control path remains active in src/main.cpp: $obsolete" }
}

$required = @(
    'GET /api/v1/state',
    'GET /api/v1/drivers',
    'GET /api/v1/application-profiles',
    'PUT /api/v1/application-profiles',
    'restartRequiredClients',
    '255',
    'ws://127.0.0.1:<port>/api/v1/ws',
    'configuration.apply',
    'engine.status',
    'devices.changed',
    'routes.changed',
    'audio.meter',
    'engine.error',
    '127.0.0.1'
)
foreach ($term in $required) {
    if (-not $api.Contains($term)) { throw "API.md lacks required contract item: $term" }
}
foreach ($obsolete in 'routes.add', 'stdin/stdout') {
    if ($api.Contains($obsolete)) { throw "Obsolete API contract remains: $obsolete" }
}
if (-not (Test-Path -LiteralPath $schemaPath -PathType Leaf)) { throw "Missing schema: $schemaPath" }
if (-not (Test-Path -LiteralPath $openApiPath -PathType Leaf)) { throw "Missing Swagger/OpenAPI document: $openApiPath" }

$openApi = Get-Content -LiteralPath $openApiPath -Raw | ConvertFrom-Json -AsHashtable
if ($openApi.openapi -ne '3.1.0') { throw 'OpenAPI document must use OpenAPI 3.1.0' }
if ($openApi.info.title -ne 'TimoxVasio Local Control API' -or $openApi.info.version -ne '1.0.0') { throw 'OpenAPI info metadata is missing or inconsistent' }
if (-not $api.Contains('openapi-v1.json')) { throw 'API.md does not link to the Swagger/OpenAPI document' }
foreach ($path in '/api/v1/state', '/api/v1/drivers', '/api/v1/application-profiles', '/api/v1/ws') {
    if (-not $openApi.paths.ContainsKey($path)) { throw "OpenAPI document lacks path $path" }
}
foreach ($method in 'get', 'put') {
    if (-not $openApi.paths['/api/v1/application-profiles'].ContainsKey($method)) {
        throw "OpenAPI application profile path lacks $method operation"
    }
}
if (-not $openApi.paths['/api/v1/ws']['get'].ContainsKey('x-websocket')) { throw 'WebSocket operation is not declared in OpenAPI' }
foreach ($schema in 'State', 'Drivers', 'ApplicationProfiles', 'ReplaceApplicationProfiles', 'ApplicationProfilesUpdate', 'ApplyCommand', 'Response', 'Event') {
    if (-not $openApi.components.schemas.ContainsKey($schema)) { throw "OpenAPI document lacks schema $schema" }
    if (-not $openApi.components.schemas[$schema]['$ref'].StartsWith('./schemas/api-v1.json#')) { throw "OpenAPI schema $schema does not reuse the normative JSON schema" }
}

$fixtures = @(
    @{ name = 'state'; value = @{ apiVersion = '1.0'; engine = @{ state = 'stopped'; physicalDriverId = $null; sampleRate = $null; bufferFrames = $null; lastError = $null }; physicalDrivers = @(); virtualDrivers = @(); routes = @() } },
    @{ name = 'drivers'; value = @{ physicalDrivers = @(); virtualDrivers = @() } },
    @{ name = 'application profiles'; value = @{ profiles = @(@{ processName = 'mixxx.exe'; inputChannels = 255; outputChannels = 255 }) } },
    @{ name = 'replace application profiles'; value = @{ profiles = @(@{ processName = 'mixxx.exe'; inputChannels = 255; outputChannels = 255 }) } },
    @{ name = 'application profile update'; value = @{ profiles = @(@{ processName = 'mixxx.exe'; inputChannels = 255; outputChannels = 255 }); restartRequiredClients = @(@{ pid = 42; processName = 'mixxx.exe' }) } },
    @{ name = 'apply command'; value = @{ id = 'request-1'; command = 'configuration.apply'; payload = @{ physicalDriverId = 'driver-physical-1'; sampleRate = 48000; bufferFrames = 256; routes = @() } } },
    @{ name = 'success response'; value = @{ id = 'request-1'; success = $true; result = @{ accepted = $true } } },
    @{ name = 'error response'; value = @{ id = 'request-1'; success = $false; error = @{ code = 'INVALID_CONFIGURATION'; message = 'Invalid route' } } },
    @{ name = 'engine status event'; value = @{ event = 'engine.status'; payload = @{ state = 'reconfiguring'; physicalDriverId = $null; sampleRate = $null; bufferFrames = $null; lastError = $null } } },
    @{ name = 'devices event'; value = @{ event = 'devices.changed'; payload = @{ physicalDrivers = @(); virtualDrivers = @() } } },
    @{ name = 'routes event'; value = @{ event = 'routes.changed'; payload = @{ routes = @() } } },
    @{ name = 'meter event'; value = @{ event = 'audio.meter'; payload = @{ endpointId = 'virtual:TimoxVasio:1234:output:1'; peakDbfs = -12.0; underruns = 0; overruns = 0 } } },
    @{ name = 'error event'; value = @{ event = 'engine.error'; payload = @{ code = 'PHYSICAL_DRIVER_OPEN_FAILED'; message = 'Device unavailable' } } }
)
foreach ($fixture in $fixtures) {
    $json = ConvertTo-Json -InputObject $fixture.value -Depth 20 -Compress
    if (-not ($json | Test-Json -SchemaFile $schemaPath)) { throw "Schema validation failed for $($fixture.name): $json" }
}

$invalid = @{ id = 'request-2'; command = 'routes.add'; payload = @{} } | ConvertTo-Json -Compress
$invalidAccepted = $false
try { $invalidAccepted = $invalid | Test-Json -SchemaFile $schemaPath } catch { $invalidAccepted = $false }
if ($invalidAccepted) { throw 'Schema accepts the obsolete routes.add command' }
$invalidBuffer = @{
    id = 'request-invalid-buffer'; command = 'configuration.apply'; payload = @{
        physicalDriverId = $null
        sampleRate = $null
        bufferFrames = 0
        routes = @()
    }
} | ConvertTo-Json -Depth 8 -Compress
$invalidBufferAccepted = $false
try { $invalidBufferAccepted = $invalidBuffer | Test-Json -SchemaFile $schemaPath } catch { $invalidBufferAccepted = $false }
if ($invalidBufferAccepted) { throw 'Schema accepts a zero physical buffer size' }
$invalidProfile = @{ profiles = @(@{ processName = 'mixxx.exe'; inputChannels = 256; outputChannels = 257 }) } | ConvertTo-Json -Depth 8 -Compress
$invalidProfileAccepted = $false
try { $invalidProfileAccepted = $invalidProfile | Test-Json -SchemaFile $schemaPath } catch { $invalidProfileAccepted = $false }
if ($invalidProfileAccepted) { throw 'Schema accepts an application profile above 256 channels' }
$invalidProfilePath = @{ profiles = @(@{ processName = '..\Mixxx.exe'; inputChannels = 255; outputChannels = 255 }) } | ConvertTo-Json -Depth 8 -Compress
$invalidProfilePathAccepted = $false
try { $invalidProfilePathAccepted = $invalidProfilePath | Test-Json -SchemaFile $schemaPath } catch { $invalidProfilePathAccepted = $false }
if ($invalidProfilePathAccepted) { throw 'Schema accepts an application profile with a path instead of an executable basename' }
Write-Output 'PASS: documented HTTP/WebSocket contract and all schema fixtures.'
