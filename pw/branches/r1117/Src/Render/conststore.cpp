#include "stdafx.h"
#include "conststore.h"

namespace Render
{

ShaderConstStore::ShaderConstStore()
  : m_first( SHADER_CONST_REGISTERS ), m_last( 0 ), m_touched( 0 ), m_changed( 0 )
  , m_writes( 0 ), m_revision( 0 ), m_dirtyFirst( SHADER_CONST_REGISTERS ), m_dirtyLast( 0 )
{
  memset( m_values, 0, sizeof( m_values ) );
}

void ShaderConstStore::Write( unsigned startRegister, unsigned registerCount, const float* pData )
{
  if( !pData || startRegister >= SHADER_CONST_REGISTERS )
    return;

  unsigned count = registerCount;
  if( startRegister + count > SHADER_CONST_REGISTERS )
    count = SHADER_CONST_REGISTERS - startRegister;   // хвост за границу — пишем, что влезло

  // По регистру, а не одним memcpy: uniform-буфер на Vulkan перекладывается
  // только по изменённым регистрам, и чтобы это знать, значение надо сравнить.
  // Сравнение побитовое (memcmp), а не по значению: NaN разного payload'а
  // считается изменением — консервативно, лишняя перекладка дешевле ошибки.
  for( unsigned i = 0; i < count; ++i )
  {
    const unsigned reg = startRegister + i;
    const float*   pSrc = pData + i * 4;
    float*         pDst = &m_values[ reg * 4 ];
    if( memcmp( pDst, pSrc, 4 * sizeof( float ) ) != 0 )
    {
      memcpy( pDst, pSrc, 4 * sizeof( float ) );
      ++m_changed;
      ++m_revision;
      if( reg < m_dirtyFirst ) m_dirtyFirst = reg;
      if( reg + 1 > m_dirtyLast ) m_dirtyLast = reg + 1;
    }
    ++m_touched;
  }

  if( startRegister < m_first )
    m_first = startRegister;
  if( startRegister + count > m_last )
    m_last = startRegister + count;
  ++m_writes;
}

bool ShaderConstStore::TakeDirty( unsigned* pFirst, unsigned* pCount )
{
  if( m_dirtyLast <= m_dirtyFirst )
    return false;
  if( pFirst ) *pFirst = m_dirtyFirst;
  if( pCount ) *pCount = m_dirtyLast - m_dirtyFirst;
  m_dirtyFirst = SHADER_CONST_REGISTERS;
  m_dirtyLast  = 0;
  return true;
}

const char* ConstStore::StatsLine() const
{
  const ShaderConstStore& vs = Stage( csVertex );
  const ShaderConstStore& ps = Stage( csPixel );
  // touched — записанные регистры, changed — реально изменившиеся значения,
  // rev — суммарно изменений за прогон. Разрыв между touched и changed и есть
  // ответ на вопрос «сколько стоит наивная перекладка блока целиком» (§2.9).
  return NStr::StrFmt(
    "ConstStore: VS c%u..c%u (%u reg, %u B, %u writes, %u touched, %u changed, rev=%u)"
    " | PS c%u..c%u (%u reg, %u B, %u writes, %u touched, %u changed, rev=%u)",
    vs.LastUsed() ? vs.FirstUsed() : 0, vs.LastUsed() ? vs.LastUsed() - 1 : 0,
    vs.BlockCount(), vs.BlockBytes(), vs.Writes(), vs.TouchedSinceTurn(),
    vs.ChangedSinceTurn(), vs.Revision(),
    ps.LastUsed() ? ps.FirstUsed() : 0, ps.LastUsed() ? ps.LastUsed() - 1 : 0,
    ps.BlockCount(), ps.BlockBytes(), ps.Writes(), ps.TouchedSinceTurn(),
    ps.ChangedSinceTurn(), ps.Revision() );
}

ConstVerifyResult VerifyConstStoreAgainstBackend( IRenderBackend* pBackend, EShaderConstStage stage )
{
  ConstVerifyResult r;
  const ShaderConstStore& store = GetConstStore().Stage( stage );
  const unsigned first = store.BlockFirst();
  const unsigned count = store.BlockCount();
  if( !pBackend || !count )
    return r;

  // Читаем из устройства сразу весь используемый диапазон: D3D9 умеет
  // Get*ShaderConstantF блоком, Vulkan-бэкенд ответит из того же блока.
  static float s_readback[ SHADER_CONST_REGISTERS * 4 ];
  const HRESULT hr = pBackend->GetShaderConstants( stage, first, count, s_readback );
  if( FAILED( hr ) )
  {
    r.hr = hr;
    return r;
  }

  const float* pShadow = store.Data() + first * 4;
  for( unsigned i = 0; i < count; ++i )
  {
    for( int c = 0; c < 4; ++c )
    {
      const float a = pShadow[ i * 4 + c ], b = s_readback[ i * 4 + c ];
      if( a == b )
        continue;
      // Биты, а не «разница»: сверка побитовая, и 1 ULP — уже расхождение
      // (для FP-детерминизма важен именно бит, см. инвариант в AGENTS.md).
      unsigned bitsA = 0, bitsB = 0;
      memcpy( &bitsA, &a, sizeof( bitsA ) );
      memcpy( &bitsB, &b, sizeof( bitsB ) );
      if( !r.mismatches )
      {
        r.firstMismatch = first + i;
        r.worstRegister = first + i;
        r.worstShadowBits = bitsA;
        r.worstDeviceBits = bitsB;
      }
      ++r.mismatches;
      break;    // один регистр — не больше одного расхождения
    }
    ++r.regsChecked;
  }
  return r;
}

ConstStore& GetConstStore()
{
  static ConstStore s_store;
  return s_store;
}

} // namespace Render
