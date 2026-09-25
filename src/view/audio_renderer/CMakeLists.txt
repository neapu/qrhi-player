set(LIB_NAME audio_renderer)
add_library(${LIB_NAME}
    AudioRenderer.cpp
    AudioRenderer.h
)

if (WIN32)
    target_compile_definitions(${LIB_NAME} PRIVATE NOMINMAX)
endif()

include(FetchContent)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
)
FetchContent_MakeAvailable(miniaudio)

# 公共头文件AudioRenderer.h包含了<miniaudio.h>，必须PUBLIC链接让上层获得include路径
target_link_libraries(${LIB_NAME} PUBLIC Qt6::Core miniaudio ffmpeg_helper)