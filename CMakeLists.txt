cmake_minimum_required(VERSION 3.31)
project(qrhi_player VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMAKE_BUILD_TYPE 只对单配置生成器（Ninja / Makefiles）有意义，不能在多配置生成器（VS / Ninja Multi-Config）上强行设置
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# SOURCE_DIR 中的反斜杠在 C++ 字符串字面量里是非法转义序列，统一转成正斜杠
string(REPLACE "\\" "/" QRHI_SOURCE_DIR "${CMAKE_SOURCE_DIR}")

# 用生成器表达式而非 CMAKE_BUILD_TYPE 判断配置，否则多配置生成器（VS / Ninja Multi-Config）下永远不生效
add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:SOURCE_DIR="${QRHI_SOURCE_DIR}">
)

if (MSVC)
    add_compile_options(/utf-8)
endif ()

add_subdirectory(src/view)
add_subdirectory(src/controller)