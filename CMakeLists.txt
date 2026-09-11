cmake_minimum_required(VERSION 3.31)
project(qrhi_player VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
    add_compile_definitions(SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

if (MSVC)
    add_compile_options(/utf-8)
endif ()

add_subdirectory(src/view)