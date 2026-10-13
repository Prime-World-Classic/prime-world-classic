// b0_trivial.hlsl — чистый SM6 (без прослойки), тот же набор ресурсов, что у
// b0_probe.hlsl. Нужен, чтобы отделить «наш перевод диалекта» от «нашего же
// вызова NVRHI»: если с этим шейдером пайплайн создаётся, а с b0_probe — нет,
// дело в переводе.
// Регистры подобраны под слоты пробы: сэмплер 0, константы 1, текстура 2.
cbuffer Consts : register( b1 ) { float4 tint; };
Texture2D    tex : register( t2 );
SamplerState smp : register( s0 );

struct VS_IN  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VS_OUT trivial_vs( VS_IN i )
{
  VS_OUT o;
  o.pos = float4( i.pos, 1.f );
  o.uv  = i.uv;
  return o;
}

float4 trivial_ps( VS_OUT i ) : SV_TARGET
{
  return tex.Sample( smp, i.uv ) * tint;
}
