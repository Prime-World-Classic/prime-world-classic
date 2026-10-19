// Пиксельный шейдер внутреннего блита seam'а: одна выборка из источника.
// Набор 0 — текстура (binding 0), набор 1 — сэмплер (binding 0).
Texture2D    srcTex : register(t0, space0);
SamplerState srcSmp : register(s0, space1);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
  return srcTex.Sample(srcSmp, uv);
}
