#ifndef REPLAYRUNNER_H_EFE52CC1_F5F7_466
#define REPLAYRUNNER_H_EFE52CC1_F5F7_466

#include "Client/DefaultScreenBase.h"
#include "PF_GameLogic/GameControl.h"
#include "Core/GameTypes.h"
#include "PF_GameLogic/GameMaps.h"
#include "System/JobThread.h"
#include "LoadingScreen.h"
#include "LocalGameContext.h"

namespace NWorld
{
  class ReplayTransceiver;
  class PFResourcesCollection;
  class MapLoadingJob;
}


namespace NGameX
{
  class AdventureScreen;
}


namespace Game
{

class LoadingScreen;
class LocalCmdScheduler;

class ReplayRunnerLoader : public NGameX::IGameControl, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(ReplayRunnerLoader, NGameX::IGameControl, BaseObjectMT);
public:
  ReplayRunnerLoader(NWorld::ReplayTransceiver* _transceiver, 
    LocalCmdScheduler* scheduler, 
    NCore::MapStartInfo& _mapInfo,
    ReplayRunner* runner);
  //NGameX::IGameControl

  void LaunchReplay();
  int Poll(float dt);

  virtual void OnCombatScreenStarted( NCore::IWorldBase * world, const NGameX::ReplayInfo & _replayInfo );
  virtual void OnVictory( const StatisticService::RPC::SessionClientResults & _results, const NGameX::ReplayInfo & _replayInfo );
  virtual void LeaveGame();
  virtual void SetTimeScale( float timescale );
  virtual bool IsGameReady() { return state == S_RUNNING || state == S_FINISHED; }
  virtual bool IsWaitingLeaveAck() { return false; }
  virtual void DispatchPlaySameTeamDecision(const bool) {}
  virtual void DispatchBadBehaviourComplaint(const int, const int) {}
  virtual void OnProtectionError() {}

  void SetAdventureScreen( NGameX::AdventureScreen* _screen ) { advScreen = _screen; }

  enum EState
  {
    S_CONNECTING,
    S_STARTING_SCREEN,
    S_RUNNING,
    S_FINISHED,
    S_LOADING
  };

  EState GetState() const { return state; }
  void Stop() { state = S_STARTING_SCREEN; }
  bool IsLoading() const { return state == S_LOADING; }
  bool IsRunning() const { return state == S_RUNNING; }

  void StopAsyncMapLoadingJob();
  // The loading job holds the world strongly (MapLoadingJob::world): let it go when the replay closes
  void ReleaseLoadingJob();

  ~ReplayRunnerLoader();

private:
  EState state;
  Strong<NWorld::ReplayTransceiver> transceiver;
  NCore::MapStartInfo mapInfo;

  Weak<NGameX::AdventureScreen>     advScreen;

  StrongMT<LocalCmdScheduler>       scheduler;
  StrongMT<NWorld::MapLoadingJob>   mapLoadingJob;
  StrongMT<threading::JobThread>    loadingThread;
  Weak<Game::LoadingScreen>         loadingScreeen;
  Weak<ReplayRunner>                replayRunner;
};

struct ReplayUserParam
{
  ReplayUserParam(): playerIdx(0), teamIdx(0), playerInTeam(0) { }
  explicit ReplayUserParam(const wstring& userParam ): playerInTeam(0)
  {
    wchar_t tIdx;
    wchar_t pIdx;

    switch(userParam.length())
    {
    case 1:
      tIdx = userParam.c_str()[0];
      teamIdx = NStr::ToInt( &tIdx );
      break;
    case 2:
      pIdx = userParam.c_str()[1];
      tIdx = userParam.c_str()[0];

      playerIdx =  NStr::ToInt( &pIdx );
      teamIdx = NStr::ToInt( &tIdx );
      break;
    default:
      NI_ALWAYS_ASSERT("userParam mast be in format X1X2, where X1 - teamid and X2 - userid ");
    }
  };

  void clear()
  {
    playerInTeam = 0;
  }

  bool findPredicate(const NCore::PlayerStartInfo &playerInfo){
    if((int)playerInfo.teamID == teamIdx)
    {
      if(playerInTeam == playerIdx)
      {
        clear();
        return true;
      }

      playerInTeam++;
    }
    return  false ;
  };

  bool findAnyValidUserWithHeroIdPredicate(const NCore::PlayerStartInfo &playerInfo){
    if(playerInfo.playerInfo.heroId > 0 && playerInfo.userID > 0 && playerInfo.teamID != -1 )
    {
        return true;
    }

    return  false ;
  };

  bool isMapCompatible(int maxTeamCount, int maxTeamSize)
  {
    if( (teamIdx < 0 || playerIdx < 0) 
      || (teamIdx >= 0 && (teamIdx + 1) > maxTeamCount)
      || (playerIdx >= 0 && (playerIdx + 1) > maxTeamSize))
    {
      return false;
    }
    return true;
  }

  int playerIdx;
  int teamIdx;

private:
  int playerInTeam;
};

class ReplayRunner : public LoadingScreen
{
  NI_DECLARE_REFCOUNT_CLASS_1( ReplayRunner, Game::LoadingScreen );

public:
  ReplayRunner( const string& replayFileName, 
                NWorld::IMapCollection * _mapCollection);
  ~ReplayRunner();

  bool Init( UI::User * uiUser ) ;
  void Step( bool bAppActive );

  virtual bool IsInited() const { return loader ? loader->IsLoading() : true; } // Used to remove from draw list (temp. hack)

  bool IsModal() const { return false; }
  bool IsTransparent() const { return false; }
  bool IsLoading() const { return loader && loader->IsLoading(); }
  bool IsRunning() const { return loader && loader->IsRunning(); }
  bool IsRestartRequested() const;

  // A seek back reopens the replay (see NWorld::ReplayTransceiver). Called from the Poll of the
  // context that runs replays; true while it is busy with that. The old combat screen must be
  // destroyed before the new one is created: its destructor resets AdventureScreen::Instance()
  // and the input section.
  static bool PollRestart( Weak<ReplayRunner> & runner, NWorld::IMapCollection * maps, string & restartFile, int & waitFrames );

  int Poll(float dt) { return loader ? loader->Poll(dt) : -1; }

  bool SkipTime( const char *name, const vector<wstring> &args );

  void StopAsyncMapLoadingJob();

  // Leaves the replay: stops the map loading, pops the combat screen and the runner
  void Close();

private:
  NCore::MapStartInfo mapInfo;
  string replayFileName;
  Weak<NGameX::AdventureScreen>  advScreeen;
  StrongMT<ReplayRunnerLoader> loader;
  Strong<NWorld::ReplayTransceiver> transceiver;
  StrongMT<NWorld::IMapCollection> mapCollection;
  Strong<NWorld::MapDescriptionLoader> mapDescription;

  CObj<NWorld::PFResourcesCollection> resourceCollection;


  int GetPlayer(int clientId);
  int PickAnyPlayer();
  bool StartReplay();
  void CreateAdvScreeen( int playerId, int _stepLength, const NCore::ClientSettings & clientSettings );
  void RunningState();

  ReplayRunner() : LoadingScreen( "", "", true, false ) {}
};

} //namespace Game

#endif //#define REPLAYRUNNER_H_EFE52CC1_F5F7_466
