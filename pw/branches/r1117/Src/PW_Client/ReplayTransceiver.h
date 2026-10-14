#pragma once

#include "Core/Scheduler.h"
#include "Core/Replay.h"


namespace Game
{
  namespace EReplayStatus { enum Enum; }
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

namespace threading
{
  class JobThread;
}


namespace NWorld
{
  class IMapCollection;
  class SaveCompressThreadJob;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ReplayStorage2: public NCore::IReplayStorage
{
  OBJECT_BASIC_METHODS(ReplayStorage2)

public:
  // Where the reader stands in the file: saved with a world snapshot and restored on rewind
  struct ReadPosition
  {
    int filePosition;
    int startStep;
    int currentStep;
    int nextStep;
    bool isFinished;

    ReadPosition() : filePosition(0), startStep(NCore::INVALID_STEP), currentStep(NCore::INVALID_STEP), nextStep(NCore::INVALID_STEP), isFinished(false) {}
  };

  ReplayStorage2() {};
  ReplayStorage2( NCore::ReplayBufferMode mode, const char * fileName, NWorld::IMapCollection * _mapCollection,
                  NGameX::LoadingStatusHandler * _loadingStatusHandler );

  virtual bool GetNextSegment( NCore::ReplaySegment& segOut ) { return false; }
  virtual bool GetNextSegment( NCore::SyncSegment & segOut );
  virtual bool GetHeader( NCore::MapStartInfo * info, int * clientId, int * stepLength,
                          NCore::ClientSettings * clientSettings, lobby::SGameParameters* gameParams );

  virtual bool IsOk() { return (replayFile && replayFile->IsOk()); };

  // Number of world steps in the whole replay (scans the step records once, the file is memory mapped)
  int ScanLength();
  // World steps already handed out by GetNextSegment
  int GetPlayedSteps() const { return startStep == NCore::INVALID_STEP ? 0 : currentStep - startStep + 1; }

  ReadPosition GetReadPosition() const;
  void SetReadPosition( const ReadPosition & position );

private:
  void SetLoadingStatus(Game::EReplayStatus::Enum status);

  CObj<FileStream> replayFile;
  StrongMT<NWorld::IMapCollection> mapCollection;
  Weak<NGameX::LoadingStatusHandler>  loadingStatusHandler;
  int dataStart;
  int startStep;
  int currentStep;
  int nextStep;
  bool isFinished;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plays a replay step by step. Seeking: forward runs the steps as fast as the frame budget
// allows; backward restores the nearest earlier world snapshot (taken while playing, the same
// world save/load the reconnect uses) and runs forward from it. Controls: console commands
// replay_* (keys in replay.cfg) and the replay panel of the combat UI (ReplayControlsController).
class ReplayTransceiver : public NCore::ITransceiver, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( ReplayTransceiver, NCore::ITransceiver, BaseObjectST )

public:
  ReplayTransceiver();
  ReplayTransceiver( NCore::IReplayStorage* _replay, int _stepLength = DEFAULT_GAME_STEP_LENGTH );
  ~ReplayTransceiver();

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

  // Seek by a number of seconds (negative - back) / to a part of the replay (0..1)
  void SeekBy( float seconds );
  void SeekTo( float fraction );

protected:
  virtual void OnDestroyContent();

private:
  struct Snapshot
  {
    int step;                                // world steps played when the snapshot was taken
    ReplayStorage2::ReadPosition position;
    CObj<Stream> data;                       // compressed world (WorldChecker format)
  };

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

  int totalSteps;
  int seekTarget;                            // world step to reach, -1 - not seeking
  bool replayEnded;
  nstl::vector<Snapshot> snapshots;          // sorted by step
  StrongMT<SaveCompressThreadJob> snapshotJob;
  StrongMT<threading::JobThread> snapshotThread;
  Snapshot pendingSnapshot;

  bool SetTimeScale( const char *name, const vector<wstring> &args );
  bool ReplaySpeedInc( const char *name, const vector<wstring> &args );
  bool ReplaySpeedDec( const char *name, const vector<wstring> &args );
  bool ReplaySpeedRst( const char *name, const vector<wstring> &args );
  bool ReplayPause( const char *name, const vector<wstring> &args );
  bool ReplaySeek( const char *name, const vector<wstring> &args );
  bool ReplaySeekTo( const char *name, const vector<wstring> &args );

  void StepReplayMessage( float dt );

  ReplayStorage2 * Storage() const;
  int GetPlayedSteps() const;
  bool StepOnce();
  void StepSeek();
  void SeekToStep( int step );
  void StartSnapshot( bool force );
  void PollSnapshot( bool wait );
  void TrimSnapshots();
  bool RestoreSnapshot( const Snapshot & snapshot );
  void UpdateControls();

public:
  void SetTimeScale( float timeScale );

};

} //namespace NWorld
