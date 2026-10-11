#version 450

layout(location = 0) in vec4 vertexColor;
layout(location = 1) in vec3 viewNormal;
layout(location = 2) in float lightingEnabled;
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;

void main() {
    outAlbedo = vertexColor;
    // Normal target alpha temporarily carries the unlit-grid/lit-geometry flag.
    outNormal = vec4(normalize(viewNormal) * 0.5 + 0.5, lightingEnabled);
}
