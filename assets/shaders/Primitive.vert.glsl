#version 450

layout(location = 0) in vec3 inClipPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inViewNormal;

layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec3 viewNormal;

void main() {
    gl_Position = vec4(inClipPosition, 1.0);
    vertexColor = inColor;
    viewNormal = inViewNormal;
}
