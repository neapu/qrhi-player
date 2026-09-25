set(LIB_NAME "Media")

set(SRC_FILES
    include/media/IMediaSource.h
    MediaSource.h
    MediaSource.cpp
    Logger.h
    Logger.cpp
    Demuxer.h
    Demuxer.cpp
    FrameProcessor.h
    FrameProcessor.cpp
    DemuxWorker.h
    DemuxWorker.cpp
    PacketQueue.h
    PacketQueue.cpp
    Decoder.h
    Decoder.cpp
    DecodeWorker.h
    DecodeWorker.cpp
    SwsProcessor.h
    SwsProcessor.cpp
    SwrProcessor.h
    SwrProcessor.cpp
)

add_library(${LIB_NAME} STATIC ${SRC_FILES})

target_include_directories(${LIB_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(${LIB_NAME} PUBLIC ffmpeg_helper spdlog)