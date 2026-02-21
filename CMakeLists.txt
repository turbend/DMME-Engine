cmake_minimum_required(VERSION 3.20)

# Include vcpkg toolchain if not already included
if(NOT CMAKE_TOOLCHAIN_FILE)
    # Try to find vcpkg toolchain
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()
endif()

project(DMME-Engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows 10 target
add_compile_definitions(
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

# Warning level
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus)
    # /W4  = high warning level
    # /WX  = treat warnings as errors
    # /permissive- = strict conformance
endif()

find_package(spdlog CONFIG REQUIRED)

add_subdirectory(src)