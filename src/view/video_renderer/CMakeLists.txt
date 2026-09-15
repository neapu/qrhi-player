set(LIB_NAME "video_renderer")

set(SRC_FILES
    VideoRendererUtils.cpp
    VideoRendererUtils.h
    QRhiVideoRenderer.cpp
    QRhiVideoRenderer.h
    YuvShaderResource.cpp
    YuvShaderResource.h
)

add_library(${LIB_NAME} STATIC
    ${SRC_FILES}
)

find_package(Qt6 6.8 REQUIRED COMPONENTS ShaderTools)

qt_add_shaders(${LIB_NAME} "shaders"
    FILES
        shaders/video.vert
        shaders/yuv420p.frag
)

if(Qt6_VERSION VERSION_GREATER_EQUAL "6.9")
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
# QRhiVideoRenderer.h（公开头文件）包含了Qt私有头文件<rhi/qrhi.h>，
# 所以GuiPrivate必须以PUBLIC方式链接，让上层目标也能获得私有头文件的include路径
target_link_libraries(${LIB_NAME} PUBLIC Qt6::GuiPrivate)
target_link_libraries(${LIB_NAME} PRIVATE Qt6::Gui Qt6::Widgets)

# 公开头文件以"QRhiVideoRenderer.h"方式被上层引用
target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${LIB_NAME} PUBLIC Controller)
