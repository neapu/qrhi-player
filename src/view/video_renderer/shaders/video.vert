#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 3) uniform UBuf {
    mat4 u_mvp;
};

void main() {
    gl_Position = u_mvp * vec4(position, 0.0, 1.0);
    vTexCoord = texCoord;
}