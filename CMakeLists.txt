cmake_minimum_required(VERSION 3.26)

project(PubgExt LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_VENDOR_JSON "Use the vendored PubgExt/json/json.hpp header" ON)
option(USE_VMMDLL "Build the legacy VMMDLL keyboard/registry sources" OFF)
option(BUILD_TESTS "Build the GoogleTest unit tests" OFF)

# Keep this list aligned with the sources enabled by PubgExt/PubgExt.vcxproj.
# The repository also contains legacy DMA/VMMDLL sources which are not part of
# that project and require an external VMMDLL SDK.
set(PUBGEXT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Config/ConfigUtilities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/ESP/PlayerEsp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/ESP/Radar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Memory/Memory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc/CheatFunction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc/Pch/Pch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc/Vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/OS-ImGui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/OS-ImGui_Base.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/OS-ImGui_External.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui_demo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui_impl_dx11.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui_impl_win32.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/SDK/ActorEntity.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/SDK/Aimbot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/SDK/Camera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/SDK/Engine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/driver/driver_interface_v3.cpp"
)

if(USE_VMMDLL)
    list(APPEND PUBGEXT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Memory/InputManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Memory/Registry.cpp"
    )
endif()

file(GLOB_RECURSE PUBGEXT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/*.hpp"
)

set(PUBGEXT_ASM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/driver/syscall.asm"
)

set(PUBGEXT_RESOURCE_SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/resource.rc")
    list(APPEND PUBGEXT_RESOURCE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/resource.rc"
    )
endif()

add_executable(RuntimeBroker WIN32
    ${PUBGEXT_SOURCES}
    ${PUBGEXT_HEADERS}
    ${PUBGEXT_ASM_SOURCES}
    ${PUBGEXT_RESOURCE_SOURCES}
)

target_include_directories(RuntimeBroker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Config"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/ESP"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Security"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/SDK"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc/Pch"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Memory"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/driver"
)

if(USE_VENDOR_JSON)
    target_compile_definitions(RuntimeBroker PRIVATE USE_VENDOR_JSON=1)
    target_include_directories(RuntimeBroker PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/json"
    )
else()
    find_package(nlohmann_json CONFIG REQUIRED)
    find_path(NLOHMANN_JSON_COMPAT_INCLUDE_DIR json.hpp
        PATH_SUFFIXES nlohmann
        REQUIRED
    )
    target_compile_definitions(RuntimeBroker PRIVATE USE_VENDOR_JSON=0)
    target_include_directories(RuntimeBroker SYSTEM PRIVATE
        "${NLOHMANN_JSON_COMPAT_INCLUDE_DIR}"
    )
    target_link_libraries(RuntimeBroker PRIVATE nlohmann_json::nlohmann_json)
endif()

target_compile_definitions(RuntimeBroker PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _CONSOLE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(MSVC)
    target_compile_options(RuntimeBroker PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W3>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    )
endif()

target_link_libraries(RuntimeBroker PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    winmm
    ws2_32
    d2d1
    dwrite
    windowscodecs
    user32
    kernel32
    imm32
    gdi32
    shell32
    advapi32
    ntdll
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt" FILES
    ${PUBGEXT_SOURCES}
    ${PUBGEXT_HEADERS}
    ${PUBGEXT_ASM_SOURCES}
    ${PUBGEXT_RESOURCE_SOURCES}
)

if(BUILD_TESTS)
    include(CTest)
    include(FetchContent)

    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(pubg_tests
        tests/test_vector.cpp
        tests/test_camera.cpp
        tests/test_config.cpp
        PubgExt/Misc/Vector.cpp
        PubgExt/SDK/Camera.cpp
        PubgExt/Config/ConfigUtilities.cpp
    )

    target_include_directories(pubg_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Config"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Misc/Pch"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Security"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/Memory"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/driver"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/json"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui"
        "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/OS-ImGui/imgui"
    )

    target_compile_definitions(pubg_tests PRIVATE
        USE_VENDOR_JSON=1
    )

    if(MSVC)
        target_compile_options(pubg_tests PRIVATE /W3 /permissive- /EHsc)
    endif()

    target_link_libraries(pubg_tests PRIVATE GTest::gtest_main)

    add_test(NAME pubg_tests COMMAND pubg_tests)
endif()
