#version 450

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D albedoBuffer;
layout(set = 2, binding = 1) uniform sampler2D normalBuffer;
layout(set = 2, binding = 2) uniform sampler2D depthBuffer;

void main() {
    vec4 albedo = texture(albedoBuffer, inUv);
    float depth = texture(depthBuffer, inUv).r;
    if (depth >= 0.999999) {
        outColor = albedo;
        return;
    }

    vec3 normal = normalize(texture(normalBuffer, inUv).xyz * 2.0 - 1.0);
    vec3 lightDirection = normalize(vec3(-0.35, 0.75, 0.55));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float lighting = 0.28 + 0.72 * diffuse;
    outColor = vec4(albedo.rgb * lighting, albedo.a);
}
