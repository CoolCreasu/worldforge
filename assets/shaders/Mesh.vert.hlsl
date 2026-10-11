cbuffer ModelTransform : register(b0, space1) {
    column_major float4x4 model;
};
cbuffer ViewTransform : register(b1, space1) {
    column_major float4x4 view;
};
cbuffer ProjectionTransform : register(b2, space1) {
    column_major float4x4 projection;
};
struct VertexInput {
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 localNormal : TEXCOORD2;
};
struct VertexOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float3 viewNormal : TEXCOORD1;
};
VertexOutput main(VertexInput input) {
    VertexOutput output;
    float3x3 viewModelLinear = (float3x3)mul(view, model);
    float3x3 normalTransform = transpose(inverse(viewModelLinear));
    output.position = mul(projection, mul(view, mul(model, float4(input.position, 1.0))));
    output.color = input.color;
    output.viewNormal = normalize(mul(normalTransform, input.localNormal));
    return output;
}
