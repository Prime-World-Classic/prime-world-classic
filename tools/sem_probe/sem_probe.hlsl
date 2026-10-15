// sem_probe.hlsl — проба для шага 9 очереди (docs/PLAN_NVRHI.md §2.10 «шаг 2»,
// §3.z п.5): какой семантикой и каким типом в D3D9 (vs_3_0) и в DXBC SM5
// (vs_5_0) выражается индекс экземпляра, и раскрывается ли макрос в позиции
// семантики/объявления. От ответа зависит, можно ли держать ОДИН исходник
// шейдера и выбирать форму в драйвере компиляции (шаг 11).
//
// Запуск: tools/sem_probe/sem_probe.sh  (DXC из tools/spirv_probe/vendor/dxc, вендоринг этапа 1).
// vs_3_0 этим компилятором не проверяется: DXC не поддерживает SM3,
// а в D3D9-контуре индекс экземпляра и вовсе не пришел из шейдера как
// системная семантика потока (D3D9 не знает DrawInstanced) и делается потоком
// BLENDINDICES0 с дивизором. Здесь важно: один исходник на обе цели (DXBC, SPIR-V).
// Режим 9 требует от драйвера макрос объявления: -D "INSTANCE_INDEX_DECL=uint inst : SV_InstanceID".
//
// Режимы:
//   0 int  inst : BLENDINDICES0      — как сейчас в D3D9
//   1 int  inst : SV_InstanceID      — системная семантика, тип int
//   2 макрос в позиции СЕМАНТИКИ (INSTANCE_INDEX_SEM)
//   3 int  inst : V_SV_InstanceID    — D3D9-«алиас»
//   4 int  inst : TEXCOORD15         — контроль: произвольная user-семантика
//   5 uint inst : SV_InstanceID      — системная семантика, тип uint
//   6 uint inst : BLENDINDICES0      — uint на user-семантике (D3D9-поток)
//   7 макрос на ВСЁ ОБЪЯВЛЕНИЕ: uint inst : SV_InstanceID
//   8 макрос на ВСЁ ОБЪЯВЛЕНИЕ: int  inst : BLENDINDICES0
//   9 макрос на всё объявление приходит ИЗДНА (от драйвера компиляции, через
//     D3D_SHADER_MACRO / --def) — ровно то, что делает шаг 11

#ifndef PROBE_MODE
#define PROBE_MODE 0
#endif

// Макросы объявлены ДО места использования: препроцессор последовательный,
// поздний #define раннее место не раскроет.
#if PROBE_MODE == 2
#define INSTANCE_INDEX_SEM SV_InstanceID
#endif
#if PROBE_MODE == 7
#define INSTANCE_INDEX_DECL uint inst : SV_InstanceID
#endif
#if PROBE_MODE == 8
#define INSTANCE_INDEX_DECL int inst : BLENDINDICES0
#endif

float4x4 g_mvp : register(c0);
float4   g_col : register(c8);

struct VS_IN
{
  float3 pos : POSITION;
#if   PROBE_MODE == 0
  int    inst : BLENDINDICES0;
#elif PROBE_MODE == 1
  int    inst : SV_InstanceID;
#elif PROBE_MODE == 2
  int    inst : INSTANCE_INDEX_SEM;
#elif PROBE_MODE == 3
  int    inst : V_SV_InstanceID;
#elif PROBE_MODE == 4
  int    inst : TEXCOORD15;
#elif PROBE_MODE == 5
  uint   inst : SV_InstanceID;
#elif PROBE_MODE == 6
  uint   inst : BLENDINDICES0;
#elif PROBE_MODE == 7 || PROBE_MODE == 8 || PROBE_MODE == 9
  INSTANCE_INDEX_DECL;
#endif
};

struct VS_OUT
{
  float4 color : COLOR0;
  float4 posCS : POSITION;
};

VS_OUT ProbeVS(VS_IN i)
{
  VS_OUT o;
  o.posCS = mul(float4(i.pos + float3(i.inst, 0.0f, 0.0f), 1.0f), g_mvp);
  o.color = g_col;
  return o;
}

float4 ProbePS(VS_OUT i) : COLOR
{
  return i.color;
}
