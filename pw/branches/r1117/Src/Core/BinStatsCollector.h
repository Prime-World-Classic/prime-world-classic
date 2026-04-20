#pragma once
#include "System/Crc32Calculator.h"

_interface IPointerHolder;
namespace NCore
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class BinStatsCollector : public IStatsCollector
{
public:
  enum InfoType
  {
    Step,
    Data,
    Object,
    StartChunk,
    FinishChunk,
    Pointer,
    StartObject,
    FinishObject,
    CurrentCrcObject,
    InfoType_Count
  };

  struct PointerData
  {
    int typeId;
    int worldId;
  };
  
  typedef map<CObjectBase *, PointerData> PointersData;
  typedef vector< char > Buffer;
  typedef vector< Buffer > BuffersStack;

  template<char Type>
  struct Record;

public:
  BinStatsCollector() : buffer( 0 ), buffCurr( 0 ), buffEnd( 0 ), isOverrun(false) { }
  
  virtual void Init( unsigned char* buffer, size_t size, int step );
  void* GetBuffer() const { return buffer; }
  size_t GetLength() const { return buffCurr - buffer; }
  bool IsOverrun() const { return isOverrun; }

  virtual bool IsChecksum() { return true; }

  virtual void OnReset() {}

  virtual bool OnStartChunk( const IBinSaver::chunk_id idChunk, int chunkType );
  virtual void OnFinishChunk();
  virtual void OnStorePointer( const IBinSaver::chunk_id idChunk, CObjectBase* object );
  virtual void OnStartObject();
  virtual void OnFinishObject( CObjectBase* object );

  virtual void OnDataChunk( const IBinSaver::chunk_id idChunk, const void* data, int size );
  virtual void OnDataChunk( const string & ) {}
  virtual void OnDataChunk( const wstring & ) {}
  virtual void OnDataChunk( const nstl::fixed_string_base<char> & ) {}
  virtual void OnDataChunk( const nstl::fixed_string_base<wchar_t> & ) {}
  
  template<class TCrc>
  void OnCurrentCrc( const TCrc &crc );
  
  static void writeToFile( const void *buffer, size_t size, Stream &stream );
  static int getStepFromBuffer( const void *buffer );
  static unsigned int getCrcFromBuffer( const void *buffer );
  static void setStepToBuffer( void *buffer, int _step );
  static void setCrcToBuffer( void *buffer, unsigned int _crc );
  
  void Dump( const char *buffer, size_t size );
  void Linearize( BuffersStack &buffStack, const char *buffer, size_t size );
  void Write( Stream &stream, const BuffersStack &buffStack );

private:
  template<char Type> Record<Type>& AppendObject();
  
  template<class Strategy>
  class ObjectsParser;
  
  class WriteObjectsToFile;
  class CollectPointerData;
  class DebugPrintData;
  class LinearizeData;
  
private:
  template<class T>
  static void ParseBuffInternal( const char *pBeg, const char *pEnd, T &obj );
  
  template<class T>
  static void ParseBuffExternal( const BuffersStack &buffStack, T &obj );
  
private:
  unsigned char safeBuffer[64];
  unsigned char* buffer;
  unsigned char* buffCurr;
  unsigned char* buffEnd;
  bool isOverrun;
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<>
struct BinStatsCollector::Record<BinStatsCollector::Step>
{
  char type;
  int step;
  unsigned int crc;

  void Set( int _step )
  {
    step = _step;
    crc = 0;
  }
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::Data>
{
  char type;
  IBinSaver::chunk_id idChunk;
  unsigned int crc;

  void Init( const IBinSaver::chunk_id _idChunk )
  {
    idChunk = _idChunk;
  }

  void SetCrc( unsigned int _crc )
  {
    crc = _crc;
  }
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::Object>
{
  char type;
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::StartChunk>
{
  char type;
  IBinSaver::chunk_id idChunk;
  int chunkType;

  void Set( const IBinSaver::chunk_id _idChunk, int _chunkType )
  {
    idChunk = _idChunk;
    chunkType = _chunkType;
  }
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::FinishChunk>
{
  char type;
  char padding[3];

  void Set()
  {
    //Nothing
  }
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::Pointer>
{
  char type;
  IBinSaver::chunk_id idChunk;
  CObjectBase *pointer;

  void Set( const IBinSaver::chunk_id _idChunk, CObjectBase *_pointer )
  {
    idChunk = _idChunk;
    pointer = _pointer;
  }
};


template<>
struct BinStatsCollector::Record<BinStatsCollector::StartObject>
{
  char type;
  char padding[3];

  void Set()
  {
    //Nothing
  }
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::FinishObject>
{
  char type; 
  int typeId;
  CObjectBase *pointer; 

  void Set( CObjectBase *_pointer, int _typeId )
  {
    typeId = _typeId;
    pointer = _pointer;
  }
};

template<>
struct BinStatsCollector::Record<BinStatsCollector::CurrentCrcObject>
{
  char type; 
  unsigned int crc;

  void SetCrc( unsigned int _crc )
  {
    crc = _crc;
  }
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<char Type>
BinStatsCollector::Record<Type>& BinStatsCollector::AppendObject()
{
  typedef Record<Type> CurT;

  NI_STATIC_ASSERT( sizeof(CurT) % 4 == 0, AlignmentFailure );
  NI_STATIC_ASSERT( sizeof(CurT) <= sizeof(safeBuffer), IncreaseSizeOfSafeBuffer );
  
  CurT *pResult;
  unsigned char * const nextCurr = buffCurr + sizeof( CurT );

  if( nextCurr <= buffEnd )
  {
    pResult = static_cast<CurT *>( static_cast<void *>(buffCurr) );
    buffCurr = nextCurr;
  }
  else
  {
    NI_VERIFY( isOverrun, "Crc buffer overrun!", isOverrun = true; );
    pResult = static_cast<CurT *>( static_cast<void *>(safeBuffer) );
    buffCurr = buffEnd;
  }
  
  pResult->type = Type;
  return *pResult;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace NCore
