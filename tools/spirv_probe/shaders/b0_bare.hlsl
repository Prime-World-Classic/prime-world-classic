// b0_bare.hlsl — шейдер БЕЗ ресурсов и без вершинных входов (треугольник из
// SV_VertexID). Лестница бисекции: если пайплайн с ним не создаётся, дело не в
// ресурсах/переводе, а в самом вызове создания пайплайна.
struct VS_OUT { float4 pos : SV_POSITION; };

VS_OUT bare_vs( uint vid : SV_VertexID )
{
  VS_OUT o;
  float2 p = (vid == 0) ? float2(-1.f, -1.f) : ((vid == 1) ? float2(3.f, -1.f) : float2(-1.f, 3.f));
  o.pos = float4( p, 0.f, 1.f );
  return o;
}

float4 bare_ps( VS_OUT i ) : SV_TARGET
{
  return float4( 1.f, 0.f, 0.f, 1.f );
}
