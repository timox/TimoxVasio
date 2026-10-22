cmake_minimum_required(VERSION 3.16)
project(VirtualASIODrivers)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VASIO_BUILD_DRIVERS "Build the TimoxVasio ASIO driver DLL" OFF)
option(VASIO_ENABLE_CRASH_DUMP "Write a local minidump when the engine terminates on an unhandled exception" OFF)
option(VASIO_BUILD_PURE_TESTS "Build portable audio algorithm tests only" OFF)
option(VASIO_BUILD_NATIVE_TRANSPORT_TESTS_ONLY "Build isolated native Windows transport tests (no ASIO SDK)" OFF)

if(VASIO_BUILD_NATIVE_TRANSPORT_TESTS_ONLY)
    if(NOT WIN32)
        message(FATAL_ERROR "Native transport OS boundary requires Windows")
    endif()
    enable_testing()
    add_executable(NativeAudioBrokerProtocolTests tests/native_audio_broker_protocol_tests.cpp)
    target_include_directories(NativeAudioBrokerProtocolTests PRIVATE include native-windows-audio/include)
    if(MSVC)
        target_compile_options(NativeAudioBrokerProtocolTests PRIVATE /W4 /WX /EHsc)
    endif()
    add_test(NAME NativeAudioBrokerProtocolTests COMMAND NativeAudioBrokerProtocolTests)
    set_tests_properties(NativeAudioBrokerProtocolTests PROPERTIES TIMEOUT 30)
    add_executable(NativeAudioTransportTests tests/native_audio_transport_tests.cpp src/native_audio_transport_win32.cpp)
    target_include_directories(NativeAudioTransportTests PRIVATE include native-windows-audio/include)
    if(MSVC)
        target_compile_options(NativeAudioTransportTests PRIVATE /W4 /WX /EHsc)
    endif()
    add_test(NAME NativeAudioTransportTests COMMAND NativeAudioTransportTests)
    set_tests_properties(NativeAudioTransportTests PROPERTIES TIMEOUT 30)
    add_executable(NativeAudioBackendTests tests/native_audio_backend_tests.cpp
        src/native_audio_backend.cpp src/drift_resampler.cpp)
    target_include_directories(NativeAudioBackendTests PRIVATE include src native-windows-audio/include)
    if(MSVC)
        # SPSC cursor cache-line padding is intentional.
        target_compile_options(NativeAudioBackendTests PRIVATE /W4 /WX /wd4324 /EHsc)
    endif()
    add_test(NAME NativeAudioBackendTests COMMAND NativeAudioBackendTests)
    set_tests_properties(NativeAudioBackendTests PROPERTIES TIMEOUT 180)
    add_executable(NativeAudioRoutingTests tests/native_audio_routing_tests.cpp
        src/audio_routing_runtime.cpp src/routing_graph.cpp src/audio_transport.cpp)
    target_include_directories(NativeAudioRoutingTests PRIVATE include src)
    target_link_libraries(NativeAudioRoutingTests PRIVATE ole32)
    if(MSVC)
        target_compile_options(NativeAudioRoutingTests PRIVATE /EHsc)
    endif()
    add_test(NAME NativeAudioRoutingTests COMMAND NativeAudioRoutingTests)
    set_tests_properties(NativeAudioRoutingTests PROPERTIES TIMEOUT 30)
    set(NATIVE_LAB_SOURCES src/native_audio_backend.cpp src/software_clock_host.cpp
        src/drift_resampler.cpp src/audio_routing_runtime.cpp src/routing_graph.cpp
        src/audio_transport.cpp src/vasio_client_manager.cpp)
    add_executable(NativeAudioLabSessionTests tests/native_audio_lab_session_tests.cpp ${NATIVE_LAB_SOURCES})
    add_executable(NativeAudioLabProbe tests/native_audio_lab_probe.cpp
        src/native_audio_transport_win32.cpp ${NATIVE_LAB_SOURCES})
    foreach(target NativeAudioLabSessionTests NativeAudioLabProbe)
        target_include_directories(${target} PRIVATE include src native-windows-audio/include)
        target_link_libraries(${target} PRIVATE ole32 user32 avrt winmm)
        if(MSVC)
            target_compile_options(${target} PRIVATE /EHsc)
        endif()
    endforeach()
    add_test(NAME NativeAudioLabSessionTests COMMAND NativeAudioLabSessionTests)
    add_test(NAME NativeAudioLabProbeHelp COMMAND NativeAudioLabProbe --help)
    add_test(NAME NativeAudioLabProbeFake COMMAND NativeAudioLabProbe --fake --seconds 2)
    set_tests_properties(NativeAudioLabSessionTests NativeAudioLabProbeHelp NativeAudioLabProbeFake PROPERTIES TIMEOUT 30)
    return()
endif()

if(VASIO_BUILD_PURE_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(SpscAudioRingTests tests/spsc_audio_ring_tests.cpp)
    target_include_directories(SpscAudioRingTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(SpscAudioRingTests PRIVATE Threads::Threads)
    add_test(NAME SpscAudioRingTests COMMAND SpscAudioRingTests)
    set_tests_properties(SpscAudioRingTests PROPERTIES TIMEOUT 30)
    add_executable(DriftResamplerTests tests/drift_resampler_tests.cpp src/drift_resampler.cpp)
    target_include_directories(DriftResamplerTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME DriftResamplerTests COMMAND DriftResamplerTests)
    set_tests_properties(DriftResamplerTests PROPERTIES TIMEOUT 60)
    add_executable(WasapiBridgeIoTests tests/wasapi_bridge_io_tests.cpp)
    target_include_directories(WasapiBridgeIoTests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WasapiBridgeIoTests COMMAND WasapiBridgeIoTests)
    set_tests_properties(WasapiBridgeIoTests PROPERTIES TIMEOUT 30)
    add_executable(WasapiSampleConversionTests tests/wasapi_sample_conversion_tests.cpp src/wasapi_sample_conversion.cpp)
    target_include_directories(WasapiSampleConversionTests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME WasapiSampleConversionTests COMMAND WasapiSampleConversionTests)
    set_tests_properties(WasapiSampleConversionTests PROPERTIES TIMEOUT 30)
    if(MSVC)
        target_compile_options(SpscAudioRingTests PRIVATE /EHsc)
        target_compile_options(DriftResamplerTests PRIVATE /EHsc)
        target_compile_options(WasapiBridgeIoTests PRIVATE /EHsc)
        target_compile_options(WasapiSampleConversionTests PRIVATE /EHsc)
    endif()
    return()
endif()

if(VASIO_BUILD_DRIVERS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeLists_VASIO_Drivers.txt)
    return()
endif()

# Repertoires
set(ASIO_SDK_PATH "${CMAKE_SOURCE_DIR}/asiosdk" CACHE PATH "Chemin vers ASIO SDK")
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")

# Headers ASIO SDK
if(NOT EXISTS "${ASIO_SDK_PATH}/common/asio.h")
    message(WARNING "ASIO SDK non trouve a ${ASIO_SDK_PATH}")
    message(FATAL_ERROR "Telechargez ASIO SDK depuis https://www.steinberg.net/asiosdk")
endif()

include_directories(${INCLUDE_DIR})
include_directories(${ASIO_SDK_PATH}/common)
include_directories(${ASIO_SDK_PATH}/host)
include_directories(${ASIO_SDK_PATH}/host/pc)

# Sources
set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/control_api_server.cpp
    ${SRC_DIR}/audio_transport.cpp
    ${SRC_DIR}/physical_asio_host.cpp
    ${SRC_DIR}/vasio_client_manager.cpp
    ${SRC_DIR}/asio_sample_conversion.cpp
    ${SRC_DIR}/audio_routing_runtime.cpp
    ${SRC_DIR}/routing_graph.cpp
    ${SRC_DIR}/audio_controller.cpp
    ${SRC_DIR}/audio_configuration_store.cpp
    ${SRC_DIR}/application_profile_store.cpp
    ${SRC_DIR}/application_profile_store_write.cpp
    ${SRC_DIR}/engine_diagnostics.cpp
    ${SRC_DIR}/engine_version.rc
)

# Windows-specific
if(WIN32)
    set(SOURCES ${SOURCES}
        ${ASIO_SDK_PATH}/host/pc/asiolist.cpp
    )

    add_executable(TimoxVirtualAsioEngine ${SOURCES})

    add_executable(SoftwareClockHostTests
        tests/software_clock_host_tests.cpp
        src/software_clock_host.cpp)
    target_include_directories(SoftwareClockHostTests PRIVATE ${INCLUDE_DIR})
    target_link_libraries(SoftwareClockHostTests PRIVATE avrt)

    add_executable(WasapiDeviceEnumeratorTests
        tests/wasapi_device_enumerator_tests.cpp
        src/wasapi_device_enumerator.cpp)
    target_include_directories(WasapiDeviceEnumeratorTests PRIVATE ${INCLUDE_DIR})
    target_link_libraries(WasapiDeviceEnumeratorTests PRIVATE ole32 propsys uuid)

    add_executable(WasapiBackendTests tests/wasapi_backend_tests.cpp
        src/wasapi_backend.cpp src/wasapi_device_enumerator.cpp)
    target_include_directories(WasapiBackendTests PRIVATE ${INCLUDE_DIR})
    target_compile_definitions(WasapiBackendTests PRIVATE NOMINMAX)
    target_link_libraries(WasapiBackendTests PRIVATE ole32 propsys uuid)

    add_executable(WasapiBridgeTests tests/wasapi_bridge_tests.cpp
        src/wasapi_bridge.cpp src/wasapi_sample_conversion.cpp src/drift_resampler.cpp
        src/wasapi_backend.cpp src/wasapi_device_enumerator.cpp)
    target_include_directories(WasapiBridgeTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR})
    target_compile_definitions(WasapiBridgeTests PRIVATE NOMINMAX)
    target_link_libraries(WasapiBridgeTests PRIVATE avrt ole32 propsys uuid)

    add_executable(WasapiBridgeProbe tests/wasapi_bridge_probe.cpp
        src/wasapi_bridge.cpp src/wasapi_sample_conversion.cpp src/drift_resampler.cpp
        src/wasapi_backend.cpp src/wasapi_device_enumerator.cpp src/software_clock_host.cpp)
    target_include_directories(WasapiBridgeProbe PRIVATE ${INCLUDE_DIR} ${SRC_DIR})
    target_compile_definitions(WasapiBridgeProbe PRIVATE NOMINMAX)
    target_link_libraries(WasapiBridgeProbe PRIVATE avrt ole32 propsys uuid)

    # Opt-in hosts load the real virtual ASIO DLL; they never emulate its transport.
    add_executable(AsioEngineSignalProbe tests/asio_engine_signal_probe.cpp)
    target_include_directories(AsioEngineSignalProbe PRIVATE ${CMAKE_SOURCE_DIR}/asiosdk/common)
    target_link_libraries(AsioEngineSignalProbe PRIVATE ole32)

    add_executable(WasapiBridgeIoTests tests/wasapi_bridge_io_tests.cpp)
    target_include_directories(WasapiBridgeIoTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR})

    add_executable(WasapiSampleConversionTests tests/wasapi_sample_conversion_tests.cpp src/wasapi_sample_conversion.cpp)
    target_include_directories(WasapiSampleConversionTests PRIVATE ${INCLUDE_DIR})

    add_executable(SpscAudioRingTests tests/spsc_audio_ring_tests.cpp)
    target_include_directories(SpscAudioRingTests PRIVATE ${SRC_DIR})

    add_executable(DriftResamplerTests tests/drift_resampler_tests.cpp src/drift_resampler.cpp)
    target_include_directories(DriftResamplerTests PRIVATE ${SRC_DIR})

    add_executable(SoftwareClockRoutingTests
        tests/software_clock_routing_tests.cpp
        src/software_clock_host.cpp
        src/audio_routing_runtime.cpp
        src/routing_graph.cpp
        src/audio_transport.cpp)
    target_include_directories(SoftwareClockRoutingTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR})
    target_link_libraries(SoftwareClockRoutingTests PRIVATE avrt ole32)

    target_include_directories(TimoxVirtualAsioEngine PRIVATE
        ${CMAKE_SOURCE_DIR}/vendor/cpp-httplib
        ${CMAKE_SOURCE_DIR}/vendor
    )
    target_compile_definitions(TimoxVirtualAsioEngine PRIVATE CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=1048576)

    if(VASIO_ENABLE_CRASH_DUMP)
        target_compile_definitions(TimoxVirtualAsioEngine PRIVATE VASIO_ENABLE_CRASH_DUMP=1)
    endif()

    target_link_libraries(TimoxVirtualAsioEngine PRIVATE
        ole32
        oleaut32
        winmm
        user32
        ws2_32
    )
    if(VASIO_ENABLE_CRASH_DUMP)
        target_link_libraries(TimoxVirtualAsioEngine PRIVATE dbghelp)
    endif()

    add_executable(AudioTransportProbe
        ${CMAKE_SOURCE_DIR}/tests/audio_transport_probe.cpp
        ${SRC_DIR}/audio_transport.cpp
        ${SRC_DIR}/audio_transport_status_reader.cpp
    )
    target_include_directories(AudioTransportProbe PRIVATE ${INCLUDE_DIR})
    target_link_libraries(AudioTransportProbe PRIVATE ole32)

    add_executable(vasio-status
        ${CMAKE_SOURCE_DIR}/tools/vasio-status/main.cpp
        ${SRC_DIR}/audio_transport_status_reader.cpp
    )
    target_include_directories(vasio-status PRIVATE ${INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor ${CMAKE_SOURCE_DIR}/vendor/cpp-httplib)
    target_compile_options(vasio-status PRIVATE /EHsc)
    target_link_libraries(vasio-status PRIVATE ws2_32)

    add_executable(PhysicalAsioProbe
        ${CMAKE_SOURCE_DIR}/tests/physical_asio_probe.cpp
        ${SRC_DIR}/physical_asio_host.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
        ${ASIO_SDK_PATH}/host/pc/asiolist.cpp
    )
    target_include_directories(PhysicalAsioProbe PRIVATE
        ${INCLUDE_DIR}
        ${ASIO_SDK_PATH}/common
        ${ASIO_SDK_PATH}/host/pc
    )
    target_link_libraries(PhysicalAsioProbe PRIVATE ole32)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(vasio-clock-compare
            ${CMAKE_SOURCE_DIR}/tools/vasio-doctor/clock_compare.cpp)
        target_include_directories(vasio-clock-compare PRIVATE
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe)
        target_compile_options(vasio-clock-compare PRIVATE /EHsc)

        add_executable(vasio-usb-descriptors
            ${CMAKE_SOURCE_DIR}/tools/vasio-doctor/usb_audio_descriptors.cpp)
        target_include_directories(vasio-usb-descriptors PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
        target_compile_options(vasio-usb-descriptors PRIVATE /EHsc)
        target_link_libraries(vasio-usb-descriptors PRIVATE setupapi)

        set(VASIO_PROBE_ASIO_LIST "${ASIO_SDK_PATH}/host/pc/asiolist.cpp")
        if(EXISTS "${ASIO_SDK_PATH}/host/pc/patched_asiolist.cpp")
            set(VASIO_PROBE_ASIO_LIST "${ASIO_SDK_PATH}/host/pc/patched_asiolist.cpp")
        endif()
        add_executable(vasio-probe
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe/main.cpp
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe/lifecycle.cpp
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe/cadence.cpp
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe/loopback.cpp
            ${VASIO_PROBE_ASIO_LIST}
        )
        target_include_directories(vasio-probe PRIVATE
            ${ASIO_SDK_PATH}/common
            ${ASIO_SDK_PATH}/host/pc
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe
        )
        target_compile_options(vasio-probe PRIVATE /EHsc)
        target_link_libraries(vasio-probe PRIVATE ole32 user32 advapi32 psapi)
        add_executable(VasioProbeRingTests tests/vasio_probe_ring_tests.cpp)
        target_include_directories(VasioProbeRingTests PRIVATE ${CMAKE_SOURCE_DIR}/tools/vasio-probe)
        add_executable(VasioCadenceStatsTests tests/vasio_cadence_stats_tests.cpp)
        target_include_directories(VasioCadenceStatsTests PRIVATE ${CMAKE_SOURCE_DIR}/tools/vasio-probe)
        add_executable(VasioLoopbackPatternTests tests/vasio_loopback_pattern_tests.cpp)
        target_include_directories(VasioLoopbackPatternTests PRIVATE ${CMAKE_SOURCE_DIR}/tools/vasio-probe)
        add_executable(VasioLifecycleCycleSinkTests tests/vasio_lifecycle_cycle_sink_tests.cpp)
        target_include_directories(VasioLifecycleCycleSinkTests PRIVATE
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/tools/vasio-probe)
        target_compile_options(VasioLifecycleCycleSinkTests PRIVATE /EHsc)
    endif()

    add_executable(AsioBufferHealthTests tests/asio_buffer_health_tests.cpp)
    target_include_directories(AsioBufferHealthTests PRIVATE ${INCLUDE_DIR})

    add_executable(AsioSampleConversionTests
        ${CMAKE_SOURCE_DIR}/tests/asio_sample_conversion_tests.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
    )
    target_include_directories(AsioSampleConversionTests PRIVATE
        ${INCLUDE_DIR}
        ${ASIO_SDK_PATH}/common
    )

    add_executable(AsioSampleConversionBlockTests
        ${CMAKE_SOURCE_DIR}/tests/asio_sample_conversion_block_tests.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
    )
    target_include_directories(AsioSampleConversionBlockTests PRIVATE
        ${INCLUDE_DIR}
        ${ASIO_SDK_PATH}/common
    )

    add_executable(RoutingGraphTests
        ${CMAKE_SOURCE_DIR}/tests/routing_graph_tests.cpp
        ${SRC_DIR}/routing_graph.cpp
    )
    target_include_directories(RoutingGraphTests PRIVATE ${INCLUDE_DIR})

    add_executable(RoutingGraphBenchmark
        ${CMAKE_SOURCE_DIR}/tests/routing_graph_benchmark.cpp
        ${SRC_DIR}/routing_graph.cpp
    )
    target_include_directories(RoutingGraphBenchmark PRIVATE ${INCLUDE_DIR})

    add_executable(AudioRoutingRuntimeTests
        ${CMAKE_SOURCE_DIR}/tests/audio_routing_runtime_tests.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp
    )
    target_include_directories(AudioRoutingRuntimeTests PRIVATE ${INCLUDE_DIR})
    target_link_libraries(AudioRoutingRuntimeTests PRIVATE ole32)

    add_executable(WasapiRoutingRuntimeTests tests/wasapi_routing_runtime_tests.cpp
        src/audio_routing_runtime.cpp src/routing_graph.cpp src/audio_transport.cpp)
    target_include_directories(WasapiRoutingRuntimeTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR})
    target_link_libraries(WasapiRoutingRuntimeTests PRIVATE ole32)

    add_executable(AudioPathBenchmark
        tests/audio_path_benchmark.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp)
    target_include_directories(AudioPathBenchmark PRIVATE ${INCLUDE_DIR} ${ASIO_SDK_PATH}/common)
    target_link_libraries(AudioPathBenchmark PRIVATE ole32)

    add_executable(PerformanceStageMeasurementsTests
        tests/performance_stage_measurements_tests.cpp)
    target_include_directories(PerformanceStageMeasurementsTests PRIVATE ${INCLUDE_DIR})

    add_executable(RealtimeCallbackSlotTests tests/realtime_callback_slot_tests.cpp)
    target_include_directories(RealtimeCallbackSlotTests PRIVATE ${INCLUDE_DIR})

    add_executable(AudioRoutingHotSwapTests
        tests/audio_routing_hot_swap_tests.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp)
    target_include_directories(AudioRoutingHotSwapTests PRIVATE ${INCLUDE_DIR})
    target_link_libraries(AudioRoutingHotSwapTests PRIVATE ole32)

    add_executable(AudioConfigurationStoreTests
        ${CMAKE_SOURCE_DIR}/tests/audio_configuration_store_tests.cpp
        ${SRC_DIR}/audio_configuration_store.cpp
    )
    target_include_directories(AudioConfigurationStoreTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${CMAKE_SOURCE_DIR}/vendor
    )

    add_executable(PersistentRoutesTests
        ${CMAKE_SOURCE_DIR}/tests/persistent_routes_tests.cpp
        ${SRC_DIR}/audio_transport.cpp
    )
    target_include_directories(PersistentRoutesTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR})
    target_link_libraries(PersistentRoutesTests PRIVATE ole32)

    add_executable(EngineDiagnosticsTests
        ${CMAKE_SOURCE_DIR}/tests/engine_diagnostics_tests.cpp
        ${SRC_DIR}/engine_diagnostics.cpp
    )
    target_include_directories(EngineDiagnosticsTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${CMAKE_SOURCE_DIR}/vendor
    )

    add_executable(ApplicationProfileStoreTests
        ${CMAKE_SOURCE_DIR}/tests/application_profile_store_tests.cpp
        ${SRC_DIR}/application_profile_store.cpp
        ${SRC_DIR}/application_profile_store_write.cpp
    )
    target_include_directories(ApplicationProfileStoreTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${CMAKE_SOURCE_DIR}/vendor
    )

    add_executable(AudioControllerPersistenceTests
        ${CMAKE_SOURCE_DIR}/tests/audio_controller_persistence_tests.cpp
        ${SRC_DIR}/audio_controller.cpp
        ${SRC_DIR}/audio_configuration_store.cpp
        ${SRC_DIR}/application_profile_store.cpp
        ${SRC_DIR}/application_profile_store_write.cpp
        ${SRC_DIR}/physical_asio_host.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp
        ${SRC_DIR}/vasio_client_manager.cpp
        ${ASIO_SDK_PATH}/host/pc/asiolist.cpp
    )
    target_include_directories(AudioControllerPersistenceTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${ASIO_SDK_PATH}/common
        ${ASIO_SDK_PATH}/host
        ${ASIO_SDK_PATH}/host/pc
        ${CMAKE_SOURCE_DIR}/vendor
    )
    target_link_libraries(AudioControllerPersistenceTests PRIVATE
        ole32 oleaut32 winmm user32
    )

    add_executable(AudioControllerClockTests
        ${CMAKE_SOURCE_DIR}/tests/audio_controller_clock_tests.cpp
        ${SRC_DIR}/audio_controller.cpp
        ${SRC_DIR}/audio_configuration_store.cpp
        ${SRC_DIR}/application_profile_store.cpp
        ${SRC_DIR}/application_profile_store_write.cpp
        ${SRC_DIR}/physical_asio_host.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp
        ${SRC_DIR}/vasio_client_manager.cpp
        ${ASIO_SDK_PATH}/host/pc/asiolist.cpp
    )
    target_include_directories(AudioControllerClockTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${ASIO_SDK_PATH}/common
        ${ASIO_SDK_PATH}/host
        ${ASIO_SDK_PATH}/host/pc
        ${CMAKE_SOURCE_DIR}/vendor
    )
    target_link_libraries(AudioControllerClockTests PRIVATE
        ole32 oleaut32 winmm user32
    )

    add_executable(ApplicationProfilesApiTests
        ${CMAKE_SOURCE_DIR}/tests/application_profiles_api_tests.cpp
        ${SRC_DIR}/control_api_server.cpp
        ${SRC_DIR}/engine_diagnostics.cpp
        ${SRC_DIR}/audio_controller.cpp
        ${SRC_DIR}/audio_configuration_store.cpp
        ${SRC_DIR}/application_profile_store.cpp
        ${SRC_DIR}/application_profile_store_write.cpp
        ${SRC_DIR}/physical_asio_host.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp
        ${SRC_DIR}/vasio_client_manager.cpp
        ${ASIO_SDK_PATH}/host/pc/asiolist.cpp
    )
    target_include_directories(ApplicationProfilesApiTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${ASIO_SDK_PATH}/common
        ${ASIO_SDK_PATH}/host
        ${ASIO_SDK_PATH}/host/pc
        ${CMAKE_SOURCE_DIR}/vendor
        ${CMAKE_SOURCE_DIR}/vendor/cpp-httplib
    )
    target_compile_definitions(ApplicationProfilesApiTests PRIVATE CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=1048576)
    target_link_libraries(ApplicationProfilesApiTests PRIVATE
        ole32 oleaut32 winmm user32 ws2_32
    )

    get_target_property(controller_test_sources AudioControllerClockTests SOURCES)
    list(REMOVE_ITEM controller_test_sources "${CMAKE_SOURCE_DIR}/tests/audio_controller_clock_tests.cpp")
    add_executable(AudioControllerWasapiTests tests/audio_controller_wasapi_tests.cpp ${controller_test_sources})
    target_include_directories(AudioControllerWasapiTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR} ${CMAKE_SOURCE_DIR}/vendor)
    target_link_libraries(AudioControllerWasapiTests PRIVATE ole32 oleaut32 winmm user32)

    get_target_property(api_test_sources ApplicationProfilesApiTests SOURCES)
    list(REMOVE_ITEM api_test_sources "${CMAKE_SOURCE_DIR}/tests/application_profiles_api_tests.cpp")
    add_executable(WasapiBridgeApiTests tests/wasapi_bridge_api_tests.cpp ${api_test_sources})
    target_include_directories(WasapiBridgeApiTests PRIVATE ${INCLUDE_DIR} ${SRC_DIR}
        ${CMAKE_SOURCE_DIR}/vendor ${CMAKE_SOURCE_DIR}/vendor/cpp-httplib)
    target_compile_definitions(WasapiBridgeApiTests PRIVATE CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=1048576)
    target_link_libraries(WasapiBridgeApiTests PRIVATE ole32 oleaut32 winmm user32 ws2_32)


    add_executable(VasioClientManagerProbe
        ${CMAKE_SOURCE_DIR}/tests/vasio_client_manager_probe.cpp
        ${SRC_DIR}/vasio_client_manager.cpp
        ${SRC_DIR}/audio_transport.cpp
    )
    target_include_directories(VasioClientManagerProbe PRIVATE ${INCLUDE_DIR})
    target_include_directories(VasioClientManagerProbe PRIVATE
        ${SRC_DIR}
        ${ASIO_SDK_PATH}/common
    )
    target_link_libraries(VasioClientManagerProbe PRIVATE ole32 oleaut32 winmm user32)

    add_executable(VasioExternalHostProbe
        ${CMAKE_SOURCE_DIR}/tests/vasio_external_host_probe.cpp
        ${ASIO_SDK_PATH}/host/pc/patched_asiolist.cpp
    )
    target_include_directories(VasioExternalHostProbe PRIVATE
        ${INCLUDE_DIR}
        ${ASIO_SDK_PATH}/common
        ${ASIO_SDK_PATH}/host/pc
    )
    target_link_libraries(VasioExternalHostProbe PRIVATE ole32)

    add_executable(JsonRuntimeProbe ${CMAKE_SOURCE_DIR}/tests/json_runtime_probe.cpp)
    target_include_directories(JsonRuntimeProbe PRIVATE
        "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}cppwinrt"
    )
    target_compile_definitions(JsonRuntimeProbe PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
    target_link_libraries(JsonRuntimeProbe PRIVATE windowsapp)
    foreach(clock_consumer TimoxVirtualAsioEngine AudioControllerClockTests
            AudioControllerPersistenceTests ApplicationProfilesApiTests AudioControllerWasapiTests WasapiBridgeApiTests)
        target_sources(${clock_consumer} PRIVATE src/software_clock_host.cpp
            src/wasapi_bridge_manager.cpp src/wasapi_bridge.cpp src/wasapi_backend.cpp
            src/wasapi_device_enumerator.cpp src/wasapi_sample_conversion.cpp src/drift_resampler.cpp)
        target_compile_definitions(${clock_consumer} PRIVATE NOMINMAX)
        target_link_libraries(${clock_consumer} PRIVATE avrt propsys uuid)
    endforeach()
    foreach(store_consumer TimoxVirtualAsioEngine AudioConfigurationStoreTests
            AudioControllerClockTests AudioControllerPersistenceTests ApplicationProfilesApiTests
            AudioControllerWasapiTests WasapiBridgeApiTests)
        target_sources(${store_consumer} PRIVATE src/wasapi_bridge_configuration.cpp)
        target_include_directories(${store_consumer} PRIVATE ${SRC_DIR})
    endforeach()
    foreach(runtime_consumer TimoxVirtualAsioEngine AudioControllerClockTests
            AudioControllerPersistenceTests ApplicationProfilesApiTests
            AudioRoutingRuntimeTests AudioRoutingHotSwapTests AudioPathBenchmark
            SoftwareClockRoutingTests)
        target_include_directories(${runtime_consumer} PRIVATE ${SRC_DIR})
    endforeach()
else()
    message(FATAL_ERROR "Ce projet necessite Windows et Visual Studio")
endif()

add_custom_command(TARGET TimoxVirtualAsioEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/api/openapi-v1.json
    $<TARGET_FILE_DIR:TimoxVirtualAsioEngine>/openapi-v1.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/api/schemas
    $<TARGET_FILE_DIR:TimoxVirtualAsioEngine>/schemas
    COMMENT "Copie du contrat OpenAPI et des schémas..."
)
add_custom_command(TARGET ApplicationProfilesApiTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/api/openapi-v1.json
    $<TARGET_FILE_DIR:ApplicationProfilesApiTests>/openapi-v1.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/api/schemas
    $<TARGET_FILE_DIR:ApplicationProfilesApiTests>/schemas
    COMMENT "Copie du contrat OpenAPI pour le test API..."
)
