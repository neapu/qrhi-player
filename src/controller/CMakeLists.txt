set(LIB_NAME Controller)

add_library(${LIB_NAME} SHARED
    ControllerImpl.cpp
    FrameImpl.cpp
    Decoder.cpp
    Packet.cpp
    Logger.cpp
)

target_compile_definitions(${LIB_NAME} PRIVATE CONTROLLER_LIBRARY)

target_include_directories(${LIB_NAME} 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(ffmpeg_helper)
target_link_libraries(${LIB_NAME} PRIVATE ffmpeg_helper)