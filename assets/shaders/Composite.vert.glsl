#version 450

layout(location = 0) out vec2 outUv;

void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    // SDL GPU NDC is Y-up; textures are sampled with top-left origin.
    outUv = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
}
