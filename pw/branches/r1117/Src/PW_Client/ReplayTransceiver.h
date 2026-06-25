#pragma once

#include "Core/Scheduler.h"

#if defined(PW_LINUX_DB_BOOTSTRAP)

#include "Core/GameTypes.h"
#include "LoadingStatusHandler.h"

#include <string>

namespace lobby
{
  struct SGameParameters;
}

namespace NCore
{
  struct ClientSettings;

  struct ReplaySegment
  {
    typedef vector<CObj<PackedWorldCommand> > Commands;

    Commands seg;
    float deltaTime;
    unsigned long crc;

    ReplaySegment()
      : deltaTime( 0.0f )
      , crc( 0 )
    {
    }
  };

  enum ReplayBufferMode
  {
    REPLAY_BUFFER_WRITE = 0,
    REPLAY_BUFFER_READ = 1,
  };

  class IReplayStorage: public CObjectBase
  {
  public:
    virtual bool GetNextSegment( ReplaySegment & segOut ) = 0;
    virtual bool GetNextSegment( SyncSegment & segOut ) = 0;
    virtual bool GetHeader( MapStartInfo * info, int * clientId, int * stepLength, ClientSettings * clientSettings, lobby::SGameParameters* gameParams ) = 0;
    virtual bool IsOk() = 0;
  };
}

#else

#include "Core/Replay.h"

#endif


namespace Game
{
#if !defined(PW_LINUX_DB_BOOTSTRAP)
  namespace EReplayStatus { enum Enum; }
#endif
}

namespace NGameX
{
  class LoadingStatusHandler;
  _interface IAdventureScreen;
}

namespace NCore
{
  _interface IWorldBase;
}


namespace NWorld
{
  class IMapCollection;
  _interface IPointerHolder;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(PW_LINUX_DB_BOOTSTRAP)

class ReplayStorage2: public NCore::IReplayStorage
{
  OBJECT_BASIC_METHODS(ReplayStorage2)

public:
  ReplayStorage2();
  ReplayStorage2( NCore::ReplayBufferMode mode, const char * fileName, NWorld::IMapCollection * _mapCollection,
                  NGameX::LoadingStatusHandler * _loadingStatusHandler );

  virtual bool GetNextSegment( NCore::ReplaySegment& segOut );
  virtual bool GetNextSegment( NCore::SyncSegment & segOut );
  virtual bool GetHeader( NCore::MapStartInfo * info, int * clientId, int * stepLength,
                          NCore::ClientSettings * clientSettings, lobby::SGameParameters* gameParams );

  virtual bool IsOk() { return linuxOk; }

  size_t GetLinuxReplayLoadedSegments() const { return linuxSegments.size(); }
  size_t GetLinuxReplayLoadedCommands() const { return linuxLoadedCommands; }
  size_t GetLinuxReplayLoadedStatuses() const { return linuxLoadedStatuses; }
  size_t GetLinuxReplayDecodeFailures() const { return linuxDecodeFailures; }
  bool GetLinuxReplayHeaderReady() const { return linuxHeaderReady; }
  size_t GetLinuxReplayHeaderPlayers() const { return linuxHeaderMapStartInfo.playersInfo.size(); }
  int GetLinuxReplayHeaderClientId() const { return linuxHeaderClientId; }
  int GetLinuxReplayHeaderStepLength() const { return linuxHeaderStepLength; }
  const char* GetLinuxReplayHeaderMap() const { return linuxHeaderMapStartInfo.mapDescName.c_str(); }
  int GetLinuxReplayStartStep() const { return linuxStartStep; }
  int GetLinuxReplayFirstStep() const { return linuxFirstStep; }
  int GetLinuxReplayLastStep() const { return linuxLastStep; }
  const char* GetLinuxReplayError() const { return linuxError.c_str(); }

private:
  void SetLoadingStatus(Game::EReplayStatus::Enum status);
  bool LinuxFail(const char* message);
  bool LinuxLoad(const char* fileName);
  void LinuxLoadHeader(const char* fileName);

  vector<NCore::ReplaySegment> linuxSegments;
  vector<int> linuxSegmentSteps;
  vector<NCore::SyncSegment::TStatuses> linuxSegmentStatuses;
  size_t linuxReplayCursor;
  size_t linuxSyncCursor;
  bool linuxOk;
  int linuxStartStep;
  int linuxFirstStep;
  int linuxLastStep;
  size_t linuxLoadedCommands;
  size_t linuxLoadedStatuses;
  size_t linuxDecodeFailures;
  bool linuxHeaderReady;
  int linuxHeaderClientId;
  int linuxHeaderStepLength;
  NCore::MapStartInfo linuxHeaderMapStartInfo;
  NCore::ClientSettings linuxHeaderClientSettings;
  std::string linuxError;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ReplayTransceiver : public NCore::ITransceiver, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( ReplayTransceiver, NCore::ITransceiver, BaseObjectST )

public:
  ReplayTransceiver() {}
  ReplayTransceiver( NCore::IReplayStorage* _replay, int _stepLength = DEFAULT_GAME_STEP_LENGTH );

  virtual void Reinit( NCore::ICommandScheduler * scheduler ) {}
  virtual void Step( float dt );
  virtual int GetWorldStep() const;
  virtual void SendCommand( NCore::WorldCommand *pCmd, bool ) {}
  virtual void SetWorld( NCore::IWorldBase * _world );
  virtual NCore::IWorldBase * GetWorld() { return world; }
  virtual void RecordMapStart( const NCore::MapStartInfo & info ) {}

  void SetAdventureScreenInterface( NGameX::IAdventureScreen * _adventureScreen ) { adventureScreen = _adventureScreen; }

  virtual bool IsPaused() const { return false; }
  virtual bool IsAsynced() const { return false; }

  virtual int GetNextStep() const { return 0; }
  virtual void SetNextStep( int _nextStep ) {};

  virtual void SetPrecalcCrcOnce( bool _precalcCrcOnce ) {};

  virtual bool GetNoData() const { return false; }
  virtual int  GetBufferLimit() const { return 1; }

  void SetUseServerReplay(bool _useServerReplay) { useServerReplay = _useServerReplay; };

protected:
  virtual void OnDestroyContent();

private:
  CObj<NCore::IReplayStorage> replay;
  CPtr<NCore::IWorldBase> world;
  CPtr<IPointerHolder> ptrHolder;
  Weak<NGameX::IAdventureScreen> adventureScreen;
  float time;
  bool useServerReplay;
  int stepLength;
  float stepLengthInSeconds;
  float replayMsgTimer;
  bool isPaused;

  bool SetTimeScale( const char *name, const vector<wstring> &args );
  bool ReplaySpeedInc( const char *name, const vector<wstring> &args );
  bool ReplaySpeedDec( const char *name, const vector<wstring> &args );
  bool ReplaySpeedRst( const char *name, const vector<wstring> &args );
  bool ReplayPause( const char *name, const vector<wstring> &args );

  void StepReplayMessage( float dt );

public:
  void SetTimeScale( float timeScale );
};

#else

class ReplayStorage2: public NCore::IReplayStorage
{
  OBJECT_BASIC_METHODS(ReplayStorage2)

public:
  ReplayStorage2() {};
  ReplayStorage2( NCore::ReplayBufferMode mode, const char * fileName, NWorld::IMapCollection * _mapCollection, 
                  NGameX::LoadingStatusHandler * _loadingStatusHandler );

  virtual bool GetNextSegment( NCore::ReplaySegment& segOut ) { return false; }
  virtual bool GetNextSegment( NCore::SyncSegment & segOut );
  virtual bool GetHeader( NCore::MapStartInfo * info, int * clientId, int * stepLength, 
                          NCore::ClientSettings * clientSettings, lobby::SGameParameters* gameParams );

  virtual bool IsOk() { return (replayFile && replayFile->IsOk()); };

private:
  void SetLoadingStatus(Game::EReplayStatus::Enum status);

  CObj<FileStream> replayFile;
  StrongMT<NWorld::IMapCollection> mapCollection;
  Weak<NGameX::LoadingStatusHandler>  loadingStatusHandler;
  int startStep;
  int currentStep;
  int nextStep;
  bool isFinished;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ReplayTransceiver : public NCore::ITransceiver, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( ReplayTransceiver, NCore::ITransceiver, BaseObjectST )

public:
  ReplayTransceiver() {}
  ReplayTransceiver( NCore::IReplayStorage* _replay, int _stepLength = DEFAULT_GAME_STEP_LENGTH );

  virtual void Reinit( NCore::ICommandScheduler * scheduler ) {}
  virtual void Step( float dt );
  virtual int GetWorldStep() const;
  virtual void SendCommand( NCore::WorldCommand *pCmd, bool ) {}
  virtual void SetWorld( NCore::IWorldBase * _world );
  virtual NCore::IWorldBase * GetWorld() { return world; }
  virtual void RecordMapStart( const NCore::MapStartInfo & info ) {}

  void SetAdventureScreenInterface( NGameX::IAdventureScreen * _adventureScreen ) { adventureScreen = _adventureScreen; }

  virtual bool IsPaused() const { return false; }
  virtual bool IsAsynced() const { return false; }

  virtual int GetNextStep() const { return 0; }
  virtual void SetNextStep( int _nextStep ) {};

  virtual void SetPrecalcCrcOnce( bool _precalcCrcOnce ) {};

  virtual bool GetNoData() const { return false; }
  virtual int  GetBufferLimit() const { return 1; }

  void SetUseServerReplay(bool _useServerReplay) { useServerReplay = _useServerReplay; };

protected:
  virtual void OnDestroyContent();

private:
  CObj<NCore::IReplayStorage> replay;
  CPtr<NCore::IWorldBase> world;
  CPtr<IPointerHolder> ptrHolder;
  Weak<NGameX::IAdventureScreen> adventureScreen;
  float time;
  bool useServerReplay;
  //TODO: Add base class for transceivers and move there all common functionality
  int stepLength;
  float stepLengthInSeconds;
  float replayMsgTimer;
  bool isPaused;

  bool SetTimeScale( const char *name, const vector<wstring> &args );
  bool ReplaySpeedInc( const char *name, const vector<wstring> &args );
  bool ReplaySpeedDec( const char *name, const vector<wstring> &args );
  bool ReplaySpeedRst( const char *name, const vector<wstring> &args );
  bool ReplayPause( const char *name, const vector<wstring> &args );

  void StepReplayMessage( float dt );

public:
  void SetTimeScale( float timeScale );

};

#endif

} //namespace NWorld
