struct VertexInput {
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 viewNormal : TEXCOORD2;
    float lightingEnabled : TEXCOORD3;
};
struct VertexOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float3 viewNormal : TEXCOORD1;
    float lightingEnabled : TEXCOORD2;
};
VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    output.viewNormal = input.viewNormal;
    output.lightingEnabled = input.lightingEnabled;
    return output;
}
