#pragma once
#include "Scheduler.h"
#include "../System/Crc32Calculator.h"
#include "GameCommand.h"
#include "BinStatsCollector.h"
#include "System/AsyncInvoker.h"
#include "Core/WorldBase.h"
#include "GameTypes.h"

_interface IPointerHolder;

namespace NCore
{
class ReplayStorage;

class CrcStatsCollector : public BinStatsCollector
{
public:
  struct TypeStats
  {
    int size;
    int objects;

    TypeStats() : size( 0 ), objects( 0 ) { }

    void Add( int _size, bool increaseObjectsCounter = false )
    {
      size += _size;
      if ( increaseObjectsCounter )
        ++objects;
    }
  };
  typedef hash_map<string, vector<TypeStats> > TTypeStatsMap;
private:
  TTypeStatsMap stats;
  vector<CObjectBase*> curObjects;
  CObjectBase* curObject;
  CObjectBase* newObject;
  int slices;

  TypeStats& GetSliceData( const char* key );

public:
  CrcStatsCollector() : BinStatsCollector(), curObject(0), newObject(0), slices(0) {}

  int GetSliceCount() const { return slices; }

  void OnReset() { slices++; }

  void OnStartObject();

  void OnFinishObject( CObjectBase* object );

  void OnStorePointer( const IBinSaver::chunk_id idChunk, CObjectBase* object );

  void OnDataChunk( const IBinSaver::chunk_id idChunk, const void* pData, int nSize );

  void OnDataChunk( const string& data ) {}

  void OnDataChunk( const wstring& data ) {}

  bool OnStartChunk( const IBinSaver::chunk_id idChunk, int chunkType );

  void OnFinishChunk();

  const TTypeStatsMap& GetStatsData() const;

  virtual void DumpStats() const {}
};


//TODO: move it to a separate file
class TransceiverCrcCalculator
{
public:
  struct CRCResult
  {
    CRCResult() : step(INVALID_STEP), crc(0) {}

    bool IsValid() const { return step != INVALID_STEP; }

    int step;
    unsigned long crc;
  };

  struct Buffer
  {
    unsigned char* buffer;
    size_t size;
    size_t length;

    Buffer() : buffer( 0 ), size( 0 ), length( 0 ) { }
    ~Buffer() { if ( buffer ) { delete buffer; buffer = 0; } }

    void Allocate( size_t _size )
    {
      if ( buffer )
        delete buffer;

      buffer = new unsigned char[_size];
      size = _size;
      length = 0;
    }
  };

  TransceiverCrcCalculator();
  ~TransceiverCrcCalculator();

  void CreateCalculator(bool _crcDataEnabled);
  void StartCalcCRCAsync( int step, CObj<IWorldBase> world );
  void StartCalcCRCSync( int step, CObj<IWorldBase> world );
  CRCResult WaitForCrcResult() { return threadWithTask.EndInvoke(); }
  void DumpCrc( int step );
  void UpdateBuffers(int lastConfirmedStep);
  Buffer * GetBuffer(int step, Stream & stream);

  bool IsBusy() const { return threadWithTask.IsBusy(); }
  void Sync() { threadWithTask.Sync(); }

private:
  struct CalcCRCTask
  {
    TransceiverCrcCalculator *pThis;
    int step;
    CObj<IWorldBase> world; 
    
    CRCResult operator()() const 
    {
      CRCResult res;
      
      res.crc = pThis->CalcCRCImpl( step, world );
      res.step = step;
    
      return res; 
    };
  };
  
  unsigned long CalcCRCImpl( int step, CObj<IWorldBase> world );
  
  nstl::list<Buffer*> crcBuffers;
  nstl::list<Buffer*> crcBuffersCache;
  int bufferSize;
  bool crcDataEnabled;
  
  CObj<ICrcCalculator> crcCalculator;
#ifndef _SHIPPING
  threading::AsyncInvoker<CalcCRCTask, CRCResult> threadWithTask;
#else
  threading::FakeAsyncInvoker<CalcCRCTask, CRCResult> threadWithTask;
#endif // _SHIPPING
};

// Adaptive steps buffer (jitter buffer). Remembers when the recent steps arrived, replays these arrivals with
// every possible buffer limit and picks the smallest one, which would have kept the world running without
// noticeable freezes for all but a small budget of steps. A rare lost packet doesn't raise the buffer,
// a constantly jittering connection does.
// It changes only WHEN the client executes received steps, never WHAT it executes, so it can't cause desync.
class StepsBufferLimit
{
public:
  StepsBufferLimit();

  void Init( const StepsDelaySettings& settings, int _stepLength );
  void OnStepsArrived( int lastArrivedStep, double timeMs );
  int GetValue() const { return currentBufferLimit; }

private:
  void ResetHistory();
  void PushArrival( double timeMs );
  double GetArrival( int index ) const { return arrivals[( arrivalsHead + index ) % arrivalsCapacity]; }
  int CountFreezes( int bufferLimit ) const;
  void Recalc();

  StepsDelaySettings bufferLimitSettings;
  int stepLength;
  int currentBufferLimit;
  int lastArrivedStep;
  int windowSteps;

  // ring buffer of the recent steps' arrival times (ms), the oldest first
  vector<double> arrivals;
  int arrivalsCapacity;
  int arrivalsHead;
  int arrivalsCount;
};

namespace
{
  struct ProtectionMagicStepCalculator : NonCopyable
  {
    ProtectionMagicStepCalculator()
      : serverStep(NCore::INVALID_STEP)
      , worldStep(NCore::INVALID_STEP)
    {

    }

    bool NeedUpdate() const
    {
      if (serverStep < 0)
        return true;
      if (worldStep < 0)
        return true;

      return false;
    }

    bool Update(const int _serverStep, const int _worldStep)
    {
      if (_serverStep < 0)
        return false;
      if (_worldStep < 0)
        return false;

      bool updated = false;

      if (serverStep < 0)
      {
        serverStep = _serverStep;
        updated = true;
      }

      if (worldStep < 0)
      {
        worldStep = _worldStep;
        updated = true;
      }

      return updated;
    }

    int Calculate(const int step)
    {
      if (serverStep < 0)
        return NCore::INVALID_STEP;
      if (worldStep < 0)
        return NCore::INVALID_STEP;

      if (step < 0)
        return NCore::INVALID_STEP;

      return (step + serverStep - worldStep);
    }

    int serverStep;
    int worldStep;
  };
}

class Transceiver : public ITransceiver, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( Transceiver, ITransceiver, BaseObjectST )
public:
  Transceiver( ICommandScheduler * scheduler, int _stepLength = DEFAULT_GAME_STEP_LENGTH, bool writeReplay = true );

  ~Transceiver();
  //ITransceiver
  virtual void Reinit( ICommandScheduler * scheduler );
  virtual void Step( float dt );
  virtual int GetWorldStep() const;
  virtual void SendCommand( WorldCommand *command, bool isPlayerCommand );
  virtual void SetWorld( IWorldBase * world );
  virtual IWorldBase * GetWorld() { return world; }
  virtual void RecordMapStart( const MapStartInfo & info );
  virtual bool IsPaused() const;
  virtual bool IsAsynced() const { return asyncState; }
  virtual int GetNextStep() const { return nextStep; }
  virtual void SetNextStep( int _nextStep ) { nextStep = _nextStep; }
  virtual void SetPrecalcCrcOnce( bool _precalcCrcOnce ) { precalcCrcOnce = _precalcCrcOnce; }
  virtual bool GetNoData() const { return noData; }
  virtual int GetBufferLimit() const;

private:
  StrongMT<ICommandScheduler> scheduler;

#ifndef _SHIPPING
  Crc32Calculator<CrcStatsCollector> crcStatCalc;
#endif

  CObj<IWorldBase> world;
  CPtr<IPointerHolder> ptrHolder;

  CObj<ReplayStorage> replay;

  bool asyncState;

  float localTimeElapsed;
  float worldTimeElapsed;
  float checkTime;
  int nextStep;
  bool delaySteps;
  TransceiverCrcCalculator crcCalc;
  bool precalcCrcOnce;
  float slowDownFactor;
  bool noData;
  double clockMs;
  //TODO: Add base class for transceivers and move there all common functionality
  int stepLength;
  float stepLengthInSeconds;
  
  StepsBufferLimit stepsBufferLimit;

  ProtectionMagicStepCalculator pmsc;

  int lastProtectionMagicAsyncStep;

  void ProcessSegment( float localTime );
  bool CanProcessStep();
  void SkipDelayedStepd();
  void ProcessProtectionMagic();
};
}
