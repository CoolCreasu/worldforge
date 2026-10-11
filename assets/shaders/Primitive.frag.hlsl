struct PixelInput { float4 color : TEXCOORD0; };
float4 main(PixelInput input) : SV_Target0 { return input.color; }
