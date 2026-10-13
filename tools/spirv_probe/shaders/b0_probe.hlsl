// b0_probe.hlsl — тестовый «материал» в диалекте клиента (SM3: register(cN),
// sampler2D, tex2D, : POSITION/: COLOR, clip) для спайка B0 (PLAN_NVRHI §3.x).
//
// Смысл набора конструкций — проверить на одном маленьком кейсе те семантики,
// которые спайк A не проверял:
//   * конвенция mul(vector, matrix) (строковые векторы D3D9);
//   * константы из register(cN) -> uniform-буфер;
//   * выборка tex2D с адресным режимом и фильтром (ориентация UV/V);
//   * clip() (alpha-test) — на Vulkan это discard;
//   * : COLOR в выходной структуре VS -> пользовательская семантика;
//   * : COLOR у PS-точки входа -> SV_TARGET.
//
// Генерация: ../gen_b0_shader.py (прослойка shim.py + DXC -> SPIR-V).

float4x4 WorldXf : register( c4 );
float4   Tint    : register( c20 );

struct VS_IN
{
  float3 Position : POSITION;
  float2 UV : TEXCOORD0;
};

struct VS_OUT
{
  float4 Position : POSITION;
  float2 UV : TEXCOORD0;
  float4 Tint : COLOR;
};

VS_OUT MainPass_VSMain( VS_IN In )
{
  VS_OUT Out;
  Out.Position = mul( float4( In.Position, 1.f ), WorldXf );
  Out.UV = In.UV;
  Out.Tint = Tint;
  return Out;
}

sampler2D probeTex : register( s0 );

float4 MainPass_PSMain( VS_OUT In ) : COLOR
{
  float4 t = tex2D( probeTex, In.UV );
  float4 c = t * In.Tint;
  clip( c.a - 0.25f );
  return c;
}
