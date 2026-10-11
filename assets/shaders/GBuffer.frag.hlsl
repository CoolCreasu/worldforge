struct PixelInput {
    float4 color : TEXCOORD0;
    float3 viewNormal : TEXCOORD1;
};
struct PixelOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
};
PixelOutput main(PixelInput input) {
    PixelOutput output;
    output.albedo = input.color;
    output.normal = float4(normalize(input.viewNormal) * 0.5 + 0.5, 1.0);
    return output;
}
