// b0_vb.hlsl — вершинные данные есть, ресурсов нет. PS возвращает
// (0, 1, uv.x, 1): по каналу B видно, какое UV дошло до пиксельного шейдера.
struct VS_IN  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VS_OUT vb_vs( VS_IN i )
{
  VS_OUT o;
  o.pos = float4( i.pos, 1.f );
  o.uv  = i.uv;
  return o;
}

float4 vb_ps( VS_OUT i ) : SV_TARGET
{
  return float4( 0.f, 1.f, i.uv.x, 1.f );
}
