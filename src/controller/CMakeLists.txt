set(LIB_NAME Controller)

set(SRC_FILES
    ControllerImpl.h
    ControllerImpl.cpp
    FrameImpl.h
    FrameImpl.cpp
    Decoder.h
    Decoder.cpp
    Packet.h
    Packet.cpp
    PacketQueue.h
    PacketQueue.cpp
    FrameQueue.h
    FrameQueue.cpp
    Logger.h
    Logger.cpp
    Demuxer.h
    Demuxer.cpp
    DemuxerWorker.h
    DemuxerWorker.cpp
    DecodeWorker.h
    DecodeWorker.cpp
    FrameProcessor.h
    FrameProcessor.cpp
    SwsProcessor.h
    SwsProcessor.cpp
    SwrProcessor.h
    SwrProcessor.cpp
    HWTransferProcessor.h
    HWTransferProcessor.cpp
)

if (WIN32)
    list(APPEND SRC_FILES 
        DxvaDecoder.h
        DxvaDecoder.cpp
    )
endif()

add_library(${LIB_NAME} SHARED ${SRC_FILES})

target_compile_definitions(${LIB_NAME} PRIVATE CONTROLLER_LIBRARY)

target_include_directories(${LIB_NAME} 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(ffmpeg_helper)
target_link_libraries(${LIB_NAME} PRIVATE ffmpeg_helper)