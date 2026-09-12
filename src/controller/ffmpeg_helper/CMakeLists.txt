set(LIB_NAME "ffmpeg_helper")

add_library(${LIB_NAME} STATIC
    FFmpegError.cpp
    FFmpegError.h
    InputContext.cpp
    InputContext.h
    Packet.h
    Packet.cpp
    CodecContext.h
    CodecContext.cpp
    Frame.h
    Frame.cpp
    SwsContext.h
    SwsContext.cpp
    SwrContext.h
    SwrContext.cpp
)

if (WIN32)
    if (NOT DEFINED FFMPEG_DIR OR FFMPEG_DIR STREQUAL "")
        message(FATAL_ERROR "FFMPEG_DIR is not defined. Please set the FFMPEG_DIR environment variable to the path of your FFmpeg installation.")
    endif()
    find_library(AVFORMAT_LIBRARY NAMES avformat PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    find_library(AVCODEC_LIBRARY NAMES avcodec PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    find_library(AVUTIL_LIBRARY NAMES avutil PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    find_library(SWSCALE_LIBRARY NAMES swscale PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    find_library(AVFILTER_LIBRARY NAMES avfilter PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    find_library(AVDEVICE_LIBRARY NAMES avdevice PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    find_library(SWRESAMPLE_LIBRARY NAMES swresample PATHS "${FFMPEG_DIR}/lib" REQUIRED)
    target_link_libraries(${LIB_NAME} PUBLIC
        ${AVFORMAT_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${AVFILTER_LIBRARY}
        ${AVDEVICE_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
    )
    target_include_directories(${LIB_NAME} PUBLIC "${FFMPEG_DIR}/include")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libavfilter libavdevice libswresample)

    target_link_libraries(${LIB_NAME} PUBLIC
        PkgConfig::LIBAV
    )
endif()