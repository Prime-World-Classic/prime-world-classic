// Вершинный шейдер внутреннего блита seam'а (StretchRect со скейлом).
// Позиция приходит из VB уже в clip space (quad по прямоугольнику приёмника),
// UV — по прямоугольнику источника. Никаких констант: геометрию считает CPU.
struct VS_OUT
{
  float4 pos : SV_POSITION;
  float2 uv  : TEXCOORD0;
};

VS_OUT main(float2 pos : POSITION0, float2 uv : TEXCOORD0)
{
  VS_OUT o;
  o.pos = float4(pos, 0.0, 1.0);
  o.uv  = uv;
  return o;
}
