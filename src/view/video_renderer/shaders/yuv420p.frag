#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D yTexture;
layout(binding = 1) uniform sampler2D uTexture;
layout(binding = 2) uniform sampler2D vTexture;
// 色彩范围(limited/full)扩展矩阵，绑定位置4，对应YuvShaderResource::m_fsColorRangeConversionMatrix
// 作用对象是采样得到的YUV，是YUV空间里的逐分量仿射变换，必须先乘
layout(std140, binding = 4) uniform ColorRangeBlock {
    mat4 COLOR_RANGE;
};
// YUV转RGB矩阵，绑定位置5，对应YuvShaderResource::m_fsYUVtoRGBMatrix
// 必须在色彩范围扩展之后乘
layout(std140, binding = 5) uniform YuvToRGBBlock {
    mat4 YUV_TO_RGB;
};


vec3 saturate(vec3 v) { return clamp(v, 0.0, 1.0); }
void main()
{
    vec4 sampledYuv = vec4(
        texture(yTexture, vTexCoord).r,
        texture(uTexture, vTexCoord).r,
        texture(vTexture, vTexCoord).r,
        1.0
    );
    // 顺序不可颠倒：先把limited range扩展到full range(同时减去U/V的128偏移)，
    // 再做YUV→RGB。颠倒的话U/V的偏移会在RGB空间里才被减去，中灰会变成洋红色
    vec4 yuv = COLOR_RANGE * sampledYuv;

    vec4 rgb = YUV_TO_RGB * yuv;
    vec3 rgbClamped = saturate(rgb.rgb);
    fragColor = vec4(rgbClamped, 1.0);
}