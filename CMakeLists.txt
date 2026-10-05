cmake_minimum_required(VERSION 3.16)
project(VirtualASIODrivers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VASIO_BUILD_DRIVERS "Build the TimoxVasio ASIO driver DLL" OFF)
option(VASIO_ENABLE_CRASH_DUMP "Write a local minidump when the engine terminates on an unhandled exception" OFF)

if(VASIO_BUILD_DRIVERS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists_VASIO_Drivers.txt)
    return()
endif()

# Repertoires
set(ASIO_SDK_PATH "${CMAKE_SOURCE_DIR}/asiosdk" CACHE PATH "Chemin vers ASIO SDK")
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(CONFIG_DIR "${CMAKE_SOURCE_DIR}/config")

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
    ${SRC_DIR}/engine_diagnostics.cpp
)

# Windows-specific
if(WIN32)
    set(SOURCES ${SOURCES}
        ${ASIO_SDK_PATH}/host/pc/asiolist.cpp
    )

    add_executable(TimoxVirtualAsioEngine ${SOURCES})

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
    )
    target_include_directories(AudioTransportProbe PRIVATE ${INCLUDE_DIR})
    target_link_libraries(AudioTransportProbe PRIVATE ole32)

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

    add_executable(AsioSampleConversionTests
        ${CMAKE_SOURCE_DIR}/tests/asio_sample_conversion_tests.cpp
        ${SRC_DIR}/asio_sample_conversion.cpp
    )
    target_include_directories(AsioSampleConversionTests PRIVATE
        ${INCLUDE_DIR}
        ${ASIO_SDK_PATH}/common
    )

    add_executable(RoutingGraphTests
        ${CMAKE_SOURCE_DIR}/tests/routing_graph_tests.cpp
        ${SRC_DIR}/routing_graph.cpp
    )
    target_include_directories(RoutingGraphTests PRIVATE ${INCLUDE_DIR})

    add_executable(AudioRoutingRuntimeTests
        ${CMAKE_SOURCE_DIR}/tests/audio_routing_runtime_tests.cpp
        ${SRC_DIR}/audio_routing_runtime.cpp
        ${SRC_DIR}/routing_graph.cpp
        ${SRC_DIR}/audio_transport.cpp
    )
    target_include_directories(AudioRoutingRuntimeTests PRIVATE ${INCLUDE_DIR})
    target_link_libraries(AudioRoutingRuntimeTests PRIVATE ole32)

    add_executable(AudioConfigurationStoreTests
        ${CMAKE_SOURCE_DIR}/tests/audio_configuration_store_tests.cpp
        ${SRC_DIR}/audio_configuration_store.cpp
    )
    target_include_directories(AudioConfigurationStoreTests PRIVATE
        ${INCLUDE_DIR}
        ${SRC_DIR}
        ${CMAKE_SOURCE_DIR}/vendor
    )

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

    add_executable(ApplicationProfilesApiTests
        ${CMAKE_SOURCE_DIR}/tests/application_profiles_api_tests.cpp
        ${SRC_DIR}/control_api_server.cpp
        ${SRC_DIR}/engine_diagnostics.cpp
        ${SRC_DIR}/audio_controller.cpp
        ${SRC_DIR}/audio_configuration_store.cpp
        ${SRC_DIR}/application_profile_store.cpp
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
else()
    message(FATAL_ERROR "Ce projet necessite Windows et Visual Studio")
endif()

# Configuration
add_custom_command(TARGET TimoxVirtualAsioEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CONFIG_DIR}
    $<TARGET_FILE_DIR:TimoxVirtualAsioEngine>/config
    COMMENT "Copie de la configuration..."
)
