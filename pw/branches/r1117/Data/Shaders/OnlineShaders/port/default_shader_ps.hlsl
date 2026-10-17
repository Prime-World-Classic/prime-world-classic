// Порт Shaders/OnlineShaders/default_shader.psb (ps_3_0):
//   def c0 = 1,0,0,0
//   mov oC0.xyzw, c0.xxyx
cbuffer pwCBankPS : register(b1, space0)
{
    float4 solidColor : packoffset(c0);
};

float4 main() : SV_TARGET
{
    return float4(solidColor.x, solidColor.x, solidColor.y, solidColor.x);
}
