set(LIB_NAME audio_renderer)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
add_library(${LIB_NAME}
    AudioRenderer.cpp
    AudioRenderer.h
)
include(FetchContent)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
)
FetchContent_MakeAvailable(miniaudio)

# 公共头文件AudioRenderer.h包含了<miniaudio.h>，必须PUBLIC链接让上层获得include路径
target_link_libraries(${LIB_NAME} PUBLIC miniaudio)
# 公共头文件AudioRenderer.h继承QObject，PUBLIC链接让上层获得QObject定义与include路径
target_link_libraries(${LIB_NAME} PUBLIC Qt6::Core)
# 公开头文件以"AudioRenderer.h"方式被上层引用
target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIB_NAME} PUBLIC Controller)