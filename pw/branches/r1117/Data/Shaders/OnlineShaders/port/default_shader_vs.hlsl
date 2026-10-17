// Порт Shaders/OnlineShaders/default_shader.vsb (vs_3_0):
//   def c0 = 1,0,0,0
//   dcl_position0 v0 | dcl_position0 o0
//   mad r0.xyzw, v0.xyzx, c0.xxxy, c0.yyyx   -> r0 = (x, 0, 0, 1)
//   dp4 r1.xyzw, r0, c4..c7                  -> r1 = mul(r0, M1)
//   dp4 o0.xyzw, r1, c12..c15                -> o  = mul(r1, M2)
cbuffer pwCBankVS : register(b0, space0)
{
    float4x4 xformM1 : packoffset(c4);
    float4x4 xformM2 : packoffset(c12);
};

float4 main(float4 inPos : POSITION) : SV_POSITION
{
    float4 r0 = float4(inPos.x, 0.0, 0.0, 1.0);
    return mul(mul(r0, xformM1), xformM2);
}
