$ErrorActionPreference = 'Stop'
$schema = Join-Path $PSScriptRoot '../api/schemas/api-v1.json'
$request = @{ id='clock-test'; command='configuration.apply'; payload=@{
    physicalDriverId=$null; sampleRate=48000; bufferFrames=256; routes=@(); clockSource=@{kind='software'}
} }
if (-not (($request | ConvertTo-Json -Depth 10) | Test-Json -SchemaFile $schema -ErrorAction SilentlyContinue)) {
    throw 'Software clock command must satisfy schema.'
}
$request.payload.clockSource.kind = 'invalid'
if (($request | ConvertTo-Json -Depth 10) | Test-Json -SchemaFile $schema -ErrorAction SilentlyContinue) {
    throw 'Unknown clock kind was accepted.'
}
$request.payload.clockSource = @{kind='software'; extra=$true}
if (($request | ConvertTo-Json -Depth 10) | Test-Json -SchemaFile $schema -ErrorAction SilentlyContinue) {
    throw 'Unexpected clock field was accepted.'
}
'Software clock schema fixtures passed.'
