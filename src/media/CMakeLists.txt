set(LIB_NAME "Media")

set(SRC_FILES
    include/media/IMediaSource.h
    MediaSource.h
    MediaSource.cpp
    Logger.h
    Logger.cpp
)

add_library(${LIB_NAME} STATIC ${SRC_FILES})

target_include_directories(${LIB_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(${LIB_NAME} PUBLIC ffmpeg_helper spdlog)