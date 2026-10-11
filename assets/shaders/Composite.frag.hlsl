Texture2D<float4> albedoBuffer : register(t0, space2);
Texture2D<float4> normalBuffer : register(t1, space2);
Texture2D<float> depthBuffer : register(t2, space2);
SamplerState albedoSampler : register(s0, space2);
SamplerState normalSampler : register(s1, space2);
SamplerState depthSampler : register(s2, space2);
cbuffer LightingParameters : register(b0, space3) {
    float4 lightDirection;
};

struct PixelInput {
    float2 uv : TEXCOORD0;
};
float4 main(PixelInput input) : SV_Target0 {
    float4 albedo = albedoBuffer.Sample(albedoSampler, input.uv);
    float depth = depthBuffer.Sample(depthSampler, input.uv);
    if (depth >= 0.999999) return albedo;

    float4 encodedNormal = normalBuffer.Sample(normalSampler, input.uv);
    // Editor grid colors are visual guides, not game-world material colors.
    if (encodedNormal.a < 0.5) return albedo;

    float3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    float diffuse = max(dot(normal, normalize(lightDirection.xyz)), 0.0);
    float lightingAmount = 0.28 + 0.72 * diffuse;
    return float4(albedo.rgb * lightingAmount, albedo.a);
}
