#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vertexColor;

layout(set = 1, binding = 0, std140) uniform ModelTransform { mat4 model; } modelTransform;
layout(set = 1, binding = 1, std140) uniform ViewTransform { mat4 view; } viewTransform;
layout(set = 1, binding = 2, std140) uniform ProjectionTransform { mat4 projection; } projectionTransform;

void main() {
    gl_Position = projectionTransform.projection
                * viewTransform.view
                * modelTransform.model
                * vec4(inPosition, 1.0);
    vertexColor = inColor;
}
