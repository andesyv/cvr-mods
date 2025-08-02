#version 460

layout(location = 0) in vec2 uv;

layout(binding = 0) uniform sampler2D s;

layout(location = 0) out vec4 frag_colour;

void main() {
    frag_colour = vec4(texture(s, uv).rgb, 1.0);
}
