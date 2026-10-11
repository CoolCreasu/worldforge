struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
VertexOutput main(uint vertexId : SV_VertexID) {
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    float2 position = positions[vertexId];
    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    return output;
}
