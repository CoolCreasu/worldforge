#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inLocalNormal;
layout(location = 0) out vec4 vertexColor;
layout(location = 1) out vec3 viewNormal;

layout(set = 1, binding = 0, std140) uniform ModelTransform { mat4 model; } modelTransform;
layout(set = 1, binding = 1, std140) uniform ViewTransform { mat4 view; } viewTransform;
layout(set = 1, binding = 2, std140) uniform ProjectionTransform { mat4 projection; } projectionTransform;

void main() {
    mat3 worldToViewNormal = transpose(inverse(mat3(viewTransform.view * modelTransform.model)));
    viewNormal = normalize(worldToViewNormal * inLocalNormal);
    gl_Position = projectionTransform.projection
                * viewTransform.view
                * modelTransform.model
                * vec4(inPosition, 1.0);
    vertexColor = inColor;
}
