set(LIB_NAME "media")

set(SRC_FILES
    include/media/IMediaSource.h
    
)

add_library(${LIB_NAME} SHARED ${SRC_FILES})