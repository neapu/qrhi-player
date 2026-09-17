#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D yTexture;
layout(binding = 1) uniform sampler2D uTexture;
layout(binding = 2) uniform sampler2D vTexture;
layout(std140, binding = 4) uniform ColorConversionBlock {
    mat4 COLOR_CONVERSION;
};
layout(std140, binding = 5) uniform YUVRangeBlock {
    mat4 YUV_RANGE;
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
    vec4 yuv = YUV_RANGE * sampledYuv;

    vec4 rgb = COLOR_CONVERSION * yuv;
    vec3 rgbClamped = saturate(rgb.rgb);
    fragColor = vec4(rgbClamped, 1.0);
}