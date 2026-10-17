// Порт Shaders/OnlineShaders/DiffuseMap.psb (ps_3_0):
//   dcl_texcoord0 v0 | dcl_2d s0
//   texld oC0.xyzw, v0.xyzw, s0.xyzw
// CTAB: единственный ресурс — сэмплер `diffuseMap` в слоте 0. Слот D3D9 =
// binding в наборе 2 (текстура) и в наборе 3 (сэмплер) — см. shim.py.
Texture2D    diffuseMap        : register(t0, space2);
SamplerState diffuseMapSampler : register(s0, space3);

float4 main(float2 inUV : TEXCOORD0) : SV_TARGET
{
    return diffuseMap.Sample(diffuseMapSampler, inUV);
}
