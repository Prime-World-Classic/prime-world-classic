#pragma once

#include "renderbackend.h"    // EShaderConstStage, SHADER_CONST_REGISTERS

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
// GlobalMasks.h и SHADER_REGISTERS_COUNT в renderer.cpp. Значение и enum
// EShaderConstStage переехали в renderbackend.h: seam владеет лексиконом
// констант (он же отвечает за их чтение), а тень — только хранилище.

class ShaderConstStore
{
public:
  ShaderConstStore();

  // Те же start/count, что у Set*ShaderConstantF: копирование vec4-регистров
  // в тень. Ничего не применяет и ничего не проверяет — это зеркало.
  // Значение сравнивается побитово: регистр считается изменённым только если
  // байты правда другие (см. комментарий к m_changed — на этом живёт
  // единственная разумная стратегия загрузки uniform-буфера на Vulkan).
  void Write( unsigned startRegister, unsigned registerCount, const float* pData );

  const float* Data() const { return m_values; }

  // Диапазон затронутых регистров (LastUsed — exclusive). Нужен, чтобы
  // uniform-буфер был узким, а не 4 КБ на стадию.
  unsigned FirstUsed() const { return m_first; }
  unsigned LastUsed()  const { return m_last; }
  // Сколько регистров ЗАПИСАНО с прошлого Turn() — «шумность» установки
  // констант (одни и те же числа пишутся по нескольку раз за кадр).
  unsigned TouchedSinceTurn() const { return m_touched; }
  // Из них — сколько значений реально изменилось. Именно это, а не записи,
  // решает, надо ли трогать буфер (§2.9: «ConstBlock обязан строиться на
  // ChangedSinceTurn»). Раньше этот счётчик врал: он считал записи.
  unsigned ChangedSinceTurn() const { return m_changed; }
  void Turn() { m_touched = 0; m_changed = 0; }

  // Всего обращений (записей) — по нему видно, насколько «шумной» является
  // установка констант.
  unsigned Writes() const { return m_writes; }

  // --- ConstBlock: то, что бэкенд забирает одним куском -------------------
  // Блок = Data()+BlockFirst() .. Data()+BlockLast(). Пустой магазин даёт
  // пустой блок, а не «весь диапазон».
  unsigned BlockFirst() const { return m_last ? m_first : 0; }
  unsigned BlockCount() const { return m_last - ( m_last ? m_first : 0 ); }
  unsigned BlockBytes() const { return BlockCount() * 4 * (unsigned)sizeof( float ); }
  // Растёт только при смене значения: если revision не двигается, буфер
  // перекладывать нельзя (на D3D9 это дёшево, на Vulkan — запись в
  // constant buffer на каждый bind).
  unsigned Revision() const { return m_revision; }
  // Диапазон изменённого с прошлого вызова (exclusive в конце) и сброс
  // грязного флага. Возвращает false, если с прошлого вызова не менялось
  // ничего.
  bool TakeDirty( unsigned* pFirst, unsigned* pCount );

private:
  float    m_values[SHADER_CONST_REGISTERS * 4];
  unsigned m_first, m_last, m_touched, m_changed, m_writes, m_revision;
  unsigned m_dirtyFirst, m_dirtyLast;
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

// Сверка тени с тем, что реально стоит в устройстве (шаг 7, §2.9).
// Возвращает число расхождений; в деталях — первый несовпавший регистр и
// худший по разнице. Нужно это затем, чтобы доказать: тень не врёт, и на
// этапе 3 её можно делать единственным источником констант (на
// uniform-буферах Get*ShaderConstantF ответить нечем — отвечает блок).
struct ConstVerifyResult
{
  unsigned regsChecked, mismatches, firstMismatch, worstRegister;
  unsigned worstShadowBits, worstDeviceBits;  // биты того самого компонента
  HRESULT  hr;          // первая ошибка бэкенда (если чтение не удалось)
  ConstVerifyResult() : regsChecked(0), mismatches(0), firstMismatch(0),
                        worstRegister(0), worstShadowBits(0), worstDeviceBits(0), hr(0) {}
};

// Читает [BlockFirst, BlockFirst+BlockCount) стадии из бэкенда и сравнивает
// с тенью. Сравнивать имеет смысл только пока устройство не пересоздавалось:
// Reset() обнуляет константы, а тень — накопительная за весь прогон.
ConstVerifyResult VerifyConstStoreAgainstBackend( IRenderBackend* pBackend, EShaderConstStage stage );

} // namespace Render
