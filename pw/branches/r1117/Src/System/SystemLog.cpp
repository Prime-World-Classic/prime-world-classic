#include "stdafx.h"

#include "FileSystem/FileUtils.h"
#include "LogFileName.h"
#include "FileSystem/FileWriteStream.h"
#include "TimeUtils.h"

#include "SystemLog.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
NLogg::CChannelLogger &GetSystemLog()
{
  static NLogg::CChannelLogger g_systemLog( "System" );
  return g_systemLog;
}


namespace NLogg {
  StreamBuffer::StreamBuffer( CChannelLogger & _logger, const SEntryInfo & _info ) :
  logger( _logger ),
  entryInfo( _info ),
  textStart( Buffer().c_str() )
  {
    WriteHeader( logger.HeaderFormat() );
    textStart = Buffer().c_str() + Buffer().Length();
  }

  StreamBuffer::~StreamBuffer() {
    Push( "\r\n" );
    logger.Log( entryInfo, Buffer().c_str(), textStart );
  }
}
