set(LIB_NAME video_renderer)

set(SRC_FILES
    QRhiVideoRenderer.cpp
    QRhiVideoRenderer.h
    ShaderResource.h
    ShaderResource.cpp
    YuvShaderResource.h
    YuvShaderResource.cpp
)

add_library(${LIB_NAME} ${SRC_FILES})

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

target_link_libraries(${LIB_NAME} PRIVATE Controller)