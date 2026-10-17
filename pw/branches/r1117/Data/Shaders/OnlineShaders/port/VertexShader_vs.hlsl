// Порт Shaders/OnlineShaders/VertexShader.vsb (vs_3_0, DXIL) в SPIR-V (§3.z п.14).
// Оригинал (D3DXDisassembleShader, 2026-10-16):
//   def c0 = 1,0,0,0
//   dcl_position0 v0 | dcl_texcoord0 v1
//   dcl_texcoord0 o0 | dcl_position0 o1
//   mad o1.xy, v0.xyzw, c21.xyzw, c21.zwzw
//   mad o0.xy, c20.xyzw, v1.xyzw, c20.zwzw
//   mov o1.zw, c0.x
// Константы c20/c21 — VSHADER_LOCALCONST0/1, их выставляет
// ImmRenderer::BasicParams::BindMaterial (madValueUV / madValuePos).
// Раскладка — канонический c-банк (§3.z п.2): байт UBO = 16*номер регистра.
cbuffer pwCBankVS : register(b0, space0)
{
    float4 madValueUV  : packoffset(c20);
    float4 madValuePos : packoffset(c21);
};

struct VS_OUT
{
    float2 outUV  : TEXCOORD0;
    float4 outPos : SV_POSITION;   // в SM3 это был out-параметр с POSITION
};

VS_OUT main(float4 inPos : POSITION, float2 inUV : TEXCOORD0)
{
    VS_OUT o;
    o.outPos.xy = inPos.xy * madValuePos.xy + madValuePos.zw;
    o.outPos.zw = float2(1.0, 1.0);          // mov o1.zw, c0.x -> обе 1.0
    o.outUV     = inUV * madValueUV.xy + madValueUV.zw;
    return o;
}
