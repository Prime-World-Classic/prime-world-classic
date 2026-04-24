#include "stdafx.h"
#include "Render/AABB.h"
#include "BinStatsCollector.h"
#include "System/BSUtil.h"
#include "MemoryLib/newdelete.h"
#include "System/BinChunkSerializer.h"
#include "System/Crc32ChecksumFast.h"
#include "System/LogFileName.h"
#include "WorldCommand.h"
#include "Replay.h"

namespace NCore
{

namespace
{
  const static int INVALID_WORLD_ID = -1;
  const static int NULL_TYPE_ID = 0;
  const static int NULL_WORLD_ID = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class BinStatsCollector::DebugPrintData
{
public:
  explicit DebugPrintData( FILE *file ) : file( file ) {}
  
  void ProcessRecord( const Record<Step> &rec ) { fprintf( file, "Step: %d\n", rec.step ); }
  void ProcessRecord( const Record<Data> &rec ) { fprintf( file, "  Data: id=%d, crc=%08X\n", rec.idChunk, rec.crc ); }
  void ProcessRecord( const Record<Object> & ) { fprintf( file, "  Object\n" ); }
  void ProcessRecord( const Record<StartChunk> &rec ) { fprintf( file, "  StartChunk: id=%d, type=%d\n", rec.idChunk, rec.chunkType ); }
  void ProcessRecord( const Record<FinishChunk> & ) { fprintf( file, "  FinishChunk\n" ); }
  void ProcessRecord( const Record<Pointer> &rec ) { fprintf( file, "  Pointer: id=%d, ptr=%p\n", rec.idChunk, rec.pointer ); }
  void ProcessRecord( const Record<StartObject> & ) { fprintf( file, "  StartObject\n" ); }
  void ProcessRecord( const Record<FinishObject> &rec ) { fprintf( file, "  FinishObject: type=%d, ptr=%p\n", rec.typeId, rec.pointer ); }
  void ProcessRecord( const Record<CurrentCrcObject> &rec ) { fprintf( file, "  CurrentCrcObject: crc=%08X\n", rec.crc ); }

private:
  FILE *file;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class BinStatsCollector::CollectPointerData
{
public:
  explicit CollectPointerData( PointersData &ptrsData ) : ptrsData( ptrsData ) 
  {
    ptrsData.clear();
    PointerData data;
    data.typeId = NULL_TYPE_ID;
    data.worldId = NULL_WORLD_ID;
    ptrsData.insert( PointersData::value_type(0, data) );
  }
  
  void ProcessRecord( const Record<Step> & ) {}
  void ProcessRecord( const Record<Data> & ) {}
  void ProcessRecord( const Record<Object> & ) {}
  void ProcessRecord( const Record<StartChunk> & ) {}
  void ProcessRecord( const Record<FinishChunk> & ) {}
  void ProcessRecord( const Record<Pointer> &rec ) 
  {
    PointerData data;
    data.typeId = NULL_TYPE_ID;
    data.worldId = (int)( (size_t)rec.pointer );
    ptrsData.insert( PointersData::value_type(rec.pointer, data) );
  }
  void ProcessRecord( const Record<StartObject> & ) {}
  void ProcessRecord( const Record<FinishObject> &rec ) 
  {
    PointerData data;
    data.typeId = rec.typeId;
    data.worldId = (int)( (size_t)rec.pointer );
    ptrsData.insert( PointersData::value_type(rec.pointer, data) );
  }
  void ProcessRecord( const Record<CurrentCrcObject> & ) {}

private:
  PointersData &ptrsData;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class BinStatsCollector::WriteObjectsToFile
{
public:
  WriteObjectsToFile( const PointersData &ptrsData, Stream &stream ) : ptrsData( ptrsData ), stream( stream ) {}
  
  struct SObject { int typeId; int worldId; void Set(int t, int w) { typeId = t; worldId = w; } 
    int operator&(IBinSaver &f) { f.Add(2, &typeId); f.Add(3, &worldId); return 0; }
  };
  struct FileFormat { enum Enum { Step = 1, Object = 2 }; };

  void ProcessRecord( const Record<Step> &rec ) { stream << (int)FileFormat::Step << rec.step; }
  void ProcessRecord( const Record<Data> & ) {}
  void ProcessRecord( const Record<Object> & ) {}
  void ProcessRecord( const Record<StartChunk> & ) {}
  void ProcessRecord( const Record<FinishChunk> & ) {}
  void ProcessRecord( const Record<Pointer> & ) {}
  void ProcessRecord( const Record<StartObject> & ) {}
  void ProcessRecord( const Record<FinishObject> &rec ) 
  {
    SObject out;
    PointersData::const_iterator it = ptrsData.find( rec.pointer );
    if ( it != ptrsData.end() )
    {
      out.Set( rec.typeId, it->second.worldId );
    }
    else
    {
      out.Set( rec.typeId, INVALID_WORLD_ID );
      NI_ALWAYS_ASSERT( "BinStatsCollector format error" );
    }
    stream << (int)FileFormat::Object << out;
  }
  void ProcessRecord( const Record<CurrentCrcObject> & ) {}

private:
  const PointersData &ptrsData;
  Stream &stream;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class BinStatsCollector::LinearizeData
{
public:
  explicit LinearizeData( BuffersStack &buffStack ) : buffStack( buffStack ), curBuffNum( 0 ) 
  {
    buffStack.clear();
    buffStack.reserve( 30 );
    buffStack.push_back( Buffer() );  
  }
  
  void ProcessRecord( const Record<Step> &rec ) { ProcessRecordImpl( rec ); }
  void ProcessRecord( const Record<Data> &rec ) { ProcessRecordImpl( rec ); }
  void ProcessRecord( const Record<Object> &rec ) { ProcessRecordImpl( rec ); }
  void ProcessRecord( const Record<StartChunk> &rec ) { ProcessRecordImpl( rec ); }
  void ProcessRecord( const Record<FinishChunk> &rec ) { ProcessRecordImpl( rec ); }
  void ProcessRecord( const Record<Pointer> &rec ) { ProcessRecordImpl( rec ); }
  void ProcessRecord( const Record<CurrentCrcObject> &rec ) { ProcessRecordImpl( rec ); }

  void ProcessRecord( const Record<StartObject> &rec )
  {
    ++curBuffNum;
    if ( curBuffNum == (int)buffStack.size() )
      buffStack.push_back( Buffer() );  
    ProcessRecordImpl( rec );  
  }
  
  void ProcessRecord( const Record<FinishObject> &rec )
  {
    ProcessRecordImpl( rec );
    if ( curBuffNum > 0 )
      --curBuffNum;
    else
      NI_ALWAYS_ASSERT( "Format error" );   
  }
  
private:
  template< char num >
  void ProcessRecordImpl( const Record<num> &rec )
  {
    NI_ASSERT( curBuffNum < (int)buffStack.size(), "" );
    const char* pBeg = reinterpret_cast<const char*>( &rec );
    const char* pEnd = pBeg + sizeof( rec );
    buffStack[curBuffNum].insert( buffStack[curBuffNum].end(), pBeg, pEnd );
  }

private:
  BuffersStack &buffStack;
  int curBuffNum;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<class Strategy>
class BinStatsCollector::ObjectsParser : public Strategy
{
public:
  explicit ObjectsParser( Strategy st = Strategy() ) : Strategy( st ) {}
 
  template< class T >
  explicit ObjectsParser( const T &t ) : Strategy( t ) {}

  const char *Process( const char *pCur, const char * const pEnd )
  { 
    InfoType type = (InfoType)*pCur;
    switch ( type )
    {
      case Step:             return ParseRecord<Step>( pCur, pEnd );
      case Data:             return ParseRecord<Data>( pCur, pEnd );
      case Object:           return ParseRecord<Object>( pCur, pEnd );
      case StartChunk:       return ParseRecord<StartChunk>( pCur, pEnd );
      case FinishChunk:      return ParseRecord<FinishChunk>( pCur, pEnd );
      case Pointer:          return ParseRecord<Pointer>( pCur, pEnd );
      case StartObject:      return ParseRecord<StartObject>( pCur, pEnd );
      case FinishObject:     return ParseRecord<FinishObject>( pCur, pEnd );
      case CurrentCrcObject: return ParseRecord<CurrentCrcObject>( pCur, pEnd );
      default:
        NI_ALWAYS_ASSERT( "BinStatsCollector format error" );
        return pEnd;
    }
  }

private:  
  template<char Type>
  const char *ParseRecord( const char *pCur, const char * const pEnd )
  { 
    NI_ASSERT( pEnd - pCur >= (int)sizeof( Record<Type> ), "BinStatsCollector format error" );
    const Record<Type> * const pResult = reinterpret_cast<const Record<Type> *>( pCur );
    Strategy::ProcessRecord( *pResult );
    return pCur + sizeof( Record<Type> );
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void BinStatsCollector::Init( unsigned char* _buffer, size_t size, int step )
{
  buffer = _buffer;
  buffCurr = buffer;
  buffEnd = buffer + size;
  isOverrun = false;
  AppendObject<Step>().Set( step );
}

bool BinStatsCollector::OnStartChunk( const IBinSaver::chunk_id idChunk, int chunkType )
{
  AppendObject<StartChunk>().Set( idChunk, chunkType );
  return true;
}

void BinStatsCollector::OnFinishChunk()
{
  AppendObject<FinishChunk>().Set();
}

void BinStatsCollector::OnStorePointer( const IBinSaver::chunk_id idChunk, CObjectBase* object )
{
  AppendObject<Pointer>().Set(idChunk, object);
}

void BinStatsCollector::OnStartObject()
{
  AppendObject<StartObject>().Set();
}

void BinStatsCollector::OnFinishObject( CObjectBase* object )
{
  AppendObject<FinishObject>().Set( object, object != 0 ? object->GetTypeId() : -1 );
}

void BinStatsCollector::OnDataChunk( const IBinSaver::chunk_id idChunk, const void* pData, int nSize )
{
  Record<Data> &dr = AppendObject<Data>();

  dr.Init( idChunk );

  switch ( nSize )
  {
  case 0: case 1:
    dr.SetCrc( *static_cast<const BYTE*>( pData ) );
    break;

  case 2: case 3:
    dr.SetCrc( *static_cast<const WORD*>( pData ) );
    break;

  case 4:
    dr.SetCrc( *static_cast<const DWORD*>( pData ) );
    break;

  default:
    dr.SetCrc( Crc32ChecksumFast::CalcForSmallLength((const unsigned char*)pData, nSize) );
  }
}

void BinStatsCollector::Dump( const char *buffer, size_t size )
{
  if ( buffer == 0 || size == 0 )
    return;
  FILE *f = fopen( "raw_async.txt", "w" );
  if ( f )
  {
    DebugPrintData strategy( f );
    ObjectsParser<DebugPrintData> parser( strategy );
    ParseBuffInternal( buffer, buffer + size, parser );
    fclose( f );
  }
}

void BinStatsCollector::Linearize( BuffersStack &buffStack, const char *buffer, size_t size )
{
  LinearizeData strategy( buffStack );
  ObjectsParser<LinearizeData> parser( strategy );
  ParseBuffInternal( buffer, buffer + size, parser );
}

void BinStatsCollector::Write( Stream &stream, const BuffersStack &buffStack )
{
  PointersData ptrsData;
  CollectPointerData collectStrategy( ptrsData );
  ObjectsParser<CollectPointerData> collector( collectStrategy );
  ParseBuffExternal( buffStack, collector );

  WriteObjectsToFile writeStrategy( ptrsData, stream );
  ObjectsParser<WriteObjectsToFile> writer( writeStrategy );
  ParseBuffExternal( buffStack, writer );
}

template<class T>
void BinStatsCollector::ParseBuffInternal( const char *pBeg, const char *pEnd, T &obj )
{
  while ( pBeg < pEnd )
  {
    pBeg = obj.Process( pBeg, pEnd );
  }
}

template<class T>
void BinStatsCollector::ParseBuffExternal( const BuffersStack &buffStack, T &obj )
{
  for ( size_t i = 0; i < buffStack.size(); ++i )
  {
    if ( buffStack[i].empty() )
      continue;
    const char *pBeg = (const char *)&buffStack[i][0];
    const char *pEnd = pBeg + buffStack[i].size();
    ParseBuffInternal( pBeg, pEnd, obj );
  }
}

#if defined(NV_LINUX_PLATFORM)
  int BinStatsCollector::getStepFromBuffer( const void * ) { return 0; }
  unsigned int BinStatsCollector::getCrcFromBuffer( const void * ) { return 0; }
  void BinStatsCollector::setStepToBuffer( void *, int ) {}
  void BinStatsCollector::setCrcToBuffer( void *, unsigned int ) {}
  void BinStatsCollector::writeToFile( const void *, size_t, Stream & ) {}
  template<> void BinStatsCollector::OnCurrentCrc<Crc32ChecksumBaseFast<Crc32ChecksumDummyStrategyTmp> >( const Crc32ChecksumBaseFast<Crc32ChecksumDummyStrategyTmp> & ) {}
#endif

} // namespace NCore
