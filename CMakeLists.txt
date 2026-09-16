cmake_minimum_required(VERSION 3.26)

project(PubgExt LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USE_VENDOR_JSON "Use the vendored PubgExt/json/json.hpp header" ON)

# Keep the CMake target in sync with the source tree, including SDK, ESP,
# memory, misc, OS-ImGui, configuration, and driver implementation sources.
file(GLOB_RECURSE PUBGEXT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/PubgExt/*.cpp"
)

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

add_executable(RuntimeBroker
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
    target_compile_options(RuntimeBroker PRIVATE /W3 /permissive- /EHsc)
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
