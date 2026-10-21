$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'driver_import_policy.ps1')
$cmake = Get-Content (Join-Path $PSScriptRoot '../cmake/CMakeLists_VASIO_Drivers.txt') -Raw

function Assert-RejectedBoundary([string]$Text, [string]$Expected) {
    $violations = @(Get-DriverBackendBoundaryViolations -CMakeText $Text)
    if (-not ($violations -match [regex]::Escape($Expected))) {
        throw "Boundary did not reject '$Expected': $($violations -join ', ')"
    }
}

if (-not (Get-Command Get-DriverBackendBoundaryViolations -ErrorAction SilentlyContinue)) {
    throw 'Missing reusable driver backend boundary audit.'
}
if (@(Get-DriverBackendBoundaryViolations -CMakeText $cmake).Count) {
    throw 'Current driver sources and legal COM libraries must pass.'
}
Assert-RejectedBoundary ($cmake.Replace('set(COMMON_SOURCES', 'set(COMMON_SOURCES ${SRC_DIR}/wasapi_bridge.cpp')) 'wasapi_bridge.cpp'
Assert-RejectedBoundary ($cmake + '`ntarget_sources(TimoxVasio PRIVATE ${SRC_DIR}/wasapi_clock_backend.cpp)'.Replace('`n', "`n")) 'wasapi_clock_backend.cpp'
Assert-RejectedBoundary ($cmake + "`ntarget_link_libraries(TimoxVasio PRIVATE WasapiBackend)") 'WasapiBackend'
Assert-RejectedBoundary ($cmake + "`ntarget_link_libraries(TimoxVasio PRIVATE mmdevapi)") 'mmdevapi'
Assert-RejectedBoundary ($cmake + '`nset(BACKEND ${SRC_DIR}/wasapi_bridge.cpp)`ntarget_sources(TimoxVasio PRIVATE ${BACKEND})'.Replace('`n', "`n")) 'wasapi_bridge.cpp'
Assert-RejectedBoundary ($cmake + '`ntarget_link_libraries(TimoxVasio PRIVATE ${UNRESOLVED_BACKEND})'.Replace('`n', "`n")) 'UNRESOLVED_BACKEND'
Assert-RejectedBoundary ($cmake.Replace('add_library(TimoxVasio', 'list(APPEND COMMON_SOURCES ${SRC_DIR}/wasapi_bridge.cpp)' + "`n" + 'add_library(TimoxVasio')) 'wasapi_bridge.cpp'
Assert-RejectedBoundary 'set(COMMON_SOURCES driver.cpp)' 'could not be audited'
Assert-RejectedBoundary ($cmake + '`nset(BACKEND WasapiBackend)`nset(backend ole32)`ntarget_link_libraries(TimoxVasio PRIVATE ${BACKEND})'.Replace('`n', "`n")) 'WasapiBackend'
Assert-RejectedBoundary ($cmake + '`ntarget_sources(TimoxVasio PRIVATE "${SRC_DIR}/backend(wasapi).cpp")'.Replace('`n', "`n")) 'could not be audited'
Assert-RejectedBoundary ($cmake + '`nset(DRIVER_TARGET TimoxVasio)`ntarget_link_libraries(${DRIVER_TARGET} PRIVATE WasapiBackend)'.Replace('`n', "`n")) 'WasapiBackend'
if (@(Get-DriverBackendBoundaryViolations -CMakeText ($cmake + "`n# target_link_libraries(TimoxVasio PRIVATE WasapiBackend)`ntarget_link_libraries(DriverProbe PRIVATE WasapiBackend)")).Count) {
    throw 'Comments and other target dependencies must not contaminate the DLL audit.'
}
'Driver WASAPI boundary tests passed (13 cases).'
