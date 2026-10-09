cmake_minimum_required(VERSION 3.20)

project(railmaster
    VERSION 0.1.0
    DESCRIPTION "Open-source reimplementation of a classic 3D railroad tycoon game"
    LANGUAGES CXX)

# Default to an optimised build with debug info: the economy simulates a
# year of history when a map is created, which is slow unoptimised.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RAILMASTER_BUILD_CLIENT "Build the SDL2/OpenGL game client" ON)
option(RAILMASTER_BUILD_TESTS "Build unit tests" ON)
option(RAILMASTER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(railmaster_warnings INTERFACE)
if(MSVC)
    target_compile_options(railmaster_warnings INTERFACE /W4 /permissive-)
    if(RAILMASTER_WARNINGS_AS_ERRORS)
        target_compile_options(railmaster_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(railmaster_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    if(RAILMASTER_WARNINGS_AS_ERRORS)
        target_compile_options(railmaster_warnings INTERFACE -Werror)
    endif()
endif()

add_subdirectory(src/sim)

if(RAILMASTER_BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

if(RAILMASTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
