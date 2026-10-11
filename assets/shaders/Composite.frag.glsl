#version 450

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D albedoBuffer;
layout(set = 2, binding = 1) uniform sampler2D normalBuffer;
layout(set = 2, binding = 2) uniform sampler2D depthBuffer;
layout(set = 3, binding = 0, std140) uniform LightingParameters {
    vec4 lightDirection;
} lighting;

void main() {
    vec4 albedo = texture(albedoBuffer, inUv);
    float depth = texture(depthBuffer, inUv).r;
    if (depth >= 0.999999) {
        outColor = albedo;
        return;
    }

    vec4 encodedNormal = texture(normalBuffer, inUv);
    // Editor grid colors are visual guides, not game-world material colors.
    if (encodedNormal.a < 0.5) {
        outColor = albedo;
        return;
    }

    vec3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    // The light direction is expressed in view space but remains fixed in world space.
    float diffuse = max(dot(normal, normalize(lighting.lightDirection.xyz)), 0.0);
    float lightingAmount = 0.28 + 0.72 * diffuse;
    outColor = vec4(albedo.rgb * lightingAmount, albedo.a);
}
