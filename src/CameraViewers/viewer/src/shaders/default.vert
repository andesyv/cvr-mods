#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 tex_coord;

layout(push_constant) uniform FrameData {
    float time;
    mat4 mvp;
} frame_data;

layout(location = 0) out vec2 uv;

void main() {
    uv = tex_coord;
    gl_Position = frame_data.mvp * vec4(position, 1.0);
}
