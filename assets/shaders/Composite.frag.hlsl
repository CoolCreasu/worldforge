Texture2D<float4> albedoBuffer : register(t0, space2);
Texture2D<float4> normalBuffer : register(t1, space2);
Texture2D<float> depthBuffer : register(t2, space2);
SamplerState albedoSampler : register(s0, space2);
SamplerState normalSampler : register(s1, space2);
SamplerState depthSampler : register(s2, space2);

struct PixelInput {
    float2 uv : TEXCOORD0;
};
float4 main(PixelInput input) : SV_Target0 {
    float4 albedo = albedoBuffer.Sample(albedoSampler, input.uv);
    float depth = depthBuffer.Sample(depthSampler, input.uv);
    if (depth >= 0.999999) return albedo;

    float3 normal = normalize(normalBuffer.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0);
    float3 lightDirection = normalize(float3(-0.35, 0.75, 0.55));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float lighting = 0.28 + 0.72 * diffuse;
    return float4(albedo.rgb * lighting, albedo.a);
}
