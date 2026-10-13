#include "stdafx.h"
#include "conststore.h"

namespace Render
{

ShaderConstStore::ShaderConstStore()
  : m_first( SHADER_CONST_REGISTERS ), m_last( 0 ), m_changed( 0 ), m_writes( 0 )
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

  memcpy( &m_values[startRegister * 4], pData, count * 4 * sizeof( float ) );

  if( startRegister < m_first )
    m_first = startRegister;
  if( startRegister + count > m_last )
    m_last = startRegister + count;
  m_changed += count;
  ++m_writes;
}

const char* ConstStore::StatsLine() const
{
  const ShaderConstStore& vs = Stage( csVertex );
  const ShaderConstStore& ps = Stage( csPixel );
  return NStr::StrFmt(
    "ConstStore: VS c%u..c%u (%u reg, %u writes, %u changed) | PS c%u..c%u (%u reg, %u writes, %u changed)",
    vs.LastUsed() ? vs.FirstUsed() : 0, vs.LastUsed() ? vs.LastUsed() - 1 : 0,
    vs.LastUsed() - ( vs.LastUsed() ? vs.FirstUsed() : 0 ), vs.Writes(), vs.ChangedSinceTurn(),
    ps.LastUsed() ? ps.FirstUsed() : 0, ps.LastUsed() ? ps.LastUsed() - 1 : 0,
    ps.LastUsed() - ( ps.LastUsed() ? ps.FirstUsed() : 0 ), ps.Writes(), ps.ChangedSinceTurn() );
}

ConstStore& GetConstStore()
{
  static ConstStore s_store;
  return s_store;
}

} // namespace Render
