#pragma once
//////////////////////////////////////////////////////////////////////////
// Тень константных регистров шейдеров (c0..c255 на стадию) — этап 2 плана
// NVRHI, docs/PLAN_NVRHI.md §2.7.
//
// Зачем это нужно. D3D9 применяет каждую запись сразу (SetVertexShaderConstantF
// по индексу регистра), а Vulkan и D3D11 — один uniform-буфер на стадию,
// который надо собрать из записей. Мест записи констант в клиенте больше
// сотни (FlashRenderer, PFRenderUtils, PFRenderInterface, ShadowMap,
// FullScreenFX, Minimap, Terrain, WaterMesh, ...), все они идут через две
// функции Renderer::Set{Vertex,Pixel}ShaderConstants. Поэтому полное
// состояние регистров держится здесь: вызовы идут «сквозь» в устройство как
// раньше (поведение и порядок не меняются), а тень обновляется рядом.
// NvrhiBackend на биндинге берёт из тени один блок — переписывать 100+ мест
// не придётся.
//
// Второй смысл — измеримая раскладка, а не догадка по GlobalMasks.h: какие
// диапазоны регистров реально пишутся за бой и сколько регистров живые. Это
// вход для выбора размера и числа uniform-буферов при бюджете
// maxBoundDescriptorSets = 8 (§2.6).

namespace Render
{

// «Виртуальных регистров» у D3D9-шейдера 256 (c0..c255), см. также
// GlobalMasks.h и SHADER_REGISTERS_COUNT в renderer.cpp.
const int SHADER_CONST_REGISTERS = 256;

enum EShaderConstStage
{
  csVertex = 0,
  csPixel  = 1,
  csConstStageCount
};

class ShaderConstStore
{
public:
  ShaderConstStore();

  // Те же start/count, что у Set*ShaderConstantF: копирование vec4-регистров
  // в тень. Ничего не применяет и ничего не проверяет — это зеркало.
  void Write( unsigned startRegister, unsigned registerCount, const float* pData );

  const float* Data() const { return m_values; }

  // Диапазон затронутых регистров (LastUsed — exclusive). Нужен, чтобы
  // uniform-буфер был узким, а не 4 КБ на стадию.
  unsigned FirstUsed() const { return m_first; }
  unsigned LastUsed()  const { return m_last; }
  // Сколько регистров менялось с прошлого Turn() — чтобы не перезаливать
  // буфер, когда константы не менялись.
  unsigned ChangedSinceTurn() const { return m_changed; }
  void Turn() { m_changed = 0; }

  // Всего обращений (записей) — по нему видно, насколько «шумной» является
  // установка констант.
  unsigned Writes() const { return m_writes; }

private:
  float    m_values[SHADER_CONST_REGISTERS * 4];
  unsigned m_first, m_last, m_changed, m_writes;
};

class ConstStore
{
public:
  ShaderConstStore& Stage( EShaderConstStage s ) { return m_stages[s]; }
  const ShaderConstStore& Stage( EShaderConstStage s ) const { return m_stages[s]; }

  // Одна строка для DebugTrace/замера: диапазоны и число живых регистров по
  // обеим стадиям. Возвращает указатель на статический буфер.
  const char* StatsLine() const;

private:
  ShaderConstStore m_stages[csConstStageCount];
};

// Единственная тень на процесс. Специально свободная функция, а не член
// Renderer: чтобы не трогать renderer.h (он включается почти везде и любое
// касание стоит полной пересборки графа).
ConstStore& GetConstStore();

} // namespace Render
