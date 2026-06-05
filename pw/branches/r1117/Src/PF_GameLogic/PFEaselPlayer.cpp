#include "stdafx.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "PFEaselPlayer.h"
#include "PFMinigamePlace.h"
#include "PFAbilityData.h"
#include "SessionEventType.h"
#include "../Core/GameCommand.h"
#include "../Core/Scheduler.h"

namespace NWorld
{

class LinuxNullMinigames;

class LinuxNullMinigamesMain : public PF_Minigames::IMinigamesMain, public CObjectBase
{
  OBJECT_METHODS( 0x9D62D444, LinuxNullMinigamesMain );

  CPtr<NScene::IScene> scene;
  CPtr<NCore::IWorldBase> world;
  Weak<NCore::ITransceiver> transceiver;
  const NDb::DBMinigamesCommon* commonDBData;

  typedef nstl::hash_map<int, PF_Minigames::MinigameCreepDesc> PersonalCreeps;
  PersonalCreeps personalCreepStack;
  int nextCreepID;

  typedef nstl::map<NDb::ERoute, int> RouteSpawners;
  typedef nstl::map<NDb::EFaction, RouteSpawners> Spawners;
  Spawners spawners;

  float sessionTime;
  int producedObjects;
  int sentCommands;

public:
  LinuxNullMinigamesMain()
    : commonDBData(0)
    , nextCreepID(1)
    , sessionTime(0.0f)
    , producedObjects(0)
    , sentCommands(0)
  {
  }

  explicit LinuxNullMinigamesMain(PFWorld* pWorld)
    : commonDBData(0)
    , nextCreepID(1)
    , sessionTime(0.0f)
    , producedObjects(0)
    , sentCommands(0)
  {
    Set(pWorld ? pWorld->GetScene() : 0, pWorld);
  }

  virtual void Set(NScene::IScene* newScene, NCore::IWorldBase* worldBase)
  {
    scene = newScene;
    world = worldBase;
  }

  virtual void SetTransceiver(NCore::ITransceiver* newTransceiver)
  {
    transceiver = newTransceiver;
  }

  virtual NScene::IScene* GetScene() { return scene; }
  virtual NCore::IWorldBase* GetWorld() { return world; }

  virtual PF_Minigames::IMinigames* ProduceMinigamesObject(
    PFWorld* pWorld,
    PF_Minigames::IWorldSessionInterface* worldInterf,
    bool isLocal);

  virtual void SendWorldCommand(NCore::WorldCommand* worldCommand)
  {
    if (!worldCommand)
      return;

    CObj<NCore::WorldCommand> command = worldCommand;
    if (IsValid(transceiver))
    {
      transceiver->SendCommand(command, true);
      ++sentCommands;
    }
  }

  virtual void Step(float deltaTime)
  {
    sessionTime += deltaTime;
  }

  virtual bool GetCreepDesc(int creepID, PF_Minigames::MinigameCreepDesc& creepDesc)
  {
    PersonalCreeps::iterator it = personalCreepStack.find(creepID);
    if (it == personalCreepStack.end())
      return false;

    creepDesc = it->second;
    return true;
  }

  virtual void GetFreeCreep(
    int playerID,
    PF_Minigames::ECreepType::Enum creepType,
    PF_Minigames::MinigameCreepDesc& creepDesc)
  {
    for (PersonalCreeps::iterator it = personalCreepStack.begin(); it != personalCreepStack.end(); ++it)
    {
      if (!it->second.isOut &&
          it->second.type == creepType &&
          (it->second.ownerPlayerID == -1 || it->second.ownerPlayerID == playerID))
      {
        it->second.isOut = true;
        it->second.ownerPlayerID = playerID;
        creepDesc = it->second;
        return;
      }
    }

    creepDesc = PF_Minigames::MinigameCreepDesc();
    creepDesc.type = creepType;
    creepDesc.creepID = nextCreepID++;
    creepDesc.instant = true;
    creepDesc.name = L"Instant creep";
    creepDesc.isOut = true;
    creepDesc.ownerPlayerID = playerID;
    personalCreepStack.insert(nstl::make_pair(creepDesc.creepID, creepDesc));
  }

  virtual void ReturnCreep(int creepID)
  {
    PersonalCreeps::iterator it = personalCreepStack.find(creepID);
    if (it == personalCreepStack.end())
      return;

    if (it->second.instant)
      personalCreepStack.erase(creepID);
    else
      it->second.isOut = false;
  }

  virtual void RegisterCreepSpawner(NDb::EFaction faction, NDb::ERoute routeID, int spawnerObjectID)
  {
    spawners[faction][routeID] = spawnerObjectID;
  }

  virtual int GetSpawnerID(NDb::EFaction faction, NDb::ERoute routeID) const
  {
    Spawners::const_iterator it = spawners.find(faction);
    if (it == spawners.end())
      return -1;

    RouteSpawners::const_iterator routeIt = it->second.find(routeID);
    return routeIt == it->second.end() ? -1 : routeIt->second;
  }

  virtual const NDb::DBMinigamesCommon* GetCommonDBData() const { return commonDBData; }
};

class LinuxNullSingleMinigame
 : public PF_Minigames::ISingleMinigame, public CObjectBase
{
  OBJECT_METHODS( 0x9D62D443, LinuxNullSingleMinigame );

public:
  ZDATA
  ZNOPARENT(PF_Minigames::ISingleMinigame)
  ZNOPARENT(CObjectBase)
  CPtr<PFMinigamePlace> place;
  NDb::DBID dbid;
  nstl::string id;
  bool running;
  bool clientStarted;
  bool paused;
  bool underFogOfWar;
  bool sessionFinished;
  bool sessionVictory;
  int startCalls;
  int startClientCalls;
  int leaveCalls;
  int leaveCommandCalls;
  int pauseCommandCalls;
  int stepCalls;
  int updateCalls;
  int pauseCalls;
  int cheatDropCooldownCalls;
  int cheatWinCalls;
  int sessionFinishedCalls;
  int mapLoadedCalls;
  int ejectCalls;
  ZEND int operator&(IBinSaver& f)
  {
    f.Add(2, &place);
    f.Add(3, &dbid);
    f.Add(4, &id);
    f.Add(5, &running);
    f.Add(6, &clientStarted);
    f.Add(7, &paused);
    f.Add(8, &underFogOfWar);
    f.Add(9, &sessionFinished);
    f.Add(10, &sessionVictory);
    f.Add(11, &startCalls);
    f.Add(12, &startClientCalls);
    f.Add(13, &leaveCalls);
    f.Add(14, &leaveCommandCalls);
    f.Add(15, &pauseCommandCalls);
    f.Add(16, &stepCalls);
    f.Add(17, &updateCalls);
    f.Add(18, &pauseCalls);
    f.Add(19, &cheatDropCooldownCalls);
    f.Add(20, &cheatWinCalls);
    f.Add(21, &sessionFinishedCalls);
    f.Add(22, &mapLoadedCalls);
    f.Add(23, &ejectCalls);
    return 0;
  }

  LinuxNullSingleMinigame()
    : running(false)
    , clientStarted(false)
    , paused(false)
    , underFogOfWar(false)
    , sessionFinished(false)
    , sessionVictory(false)
    , startCalls(0)
    , startClientCalls(0)
    , leaveCalls(0)
    , leaveCommandCalls(0)
    , pauseCommandCalls(0)
    , stepCalls(0)
    , updateCalls(0)
    , pauseCalls(0)
    , cheatDropCooldownCalls(0)
    , cheatWinCalls(0)
    , sessionFinishedCalls(0)
    , mapLoadedCalls(0)
    , ejectCalls(0)
  {
  }

  void SetId(const char* newId)
  {
    id = newId && *newId ? newId : "Easel";
  }

  const char* GetId() const
  {
    return id.empty() ? "Easel" : id.c_str();
  }

  virtual bool Start(PFMinigamePlace* newPlace)
  {
    ++startCalls;
    if (!newPlace)
      return false;

    place = newPlace;
    SetId(newPlace->MinigameId().c_str());
    if (IsValid(newPlace->GetMinigamePlaceDB()))
      dbid = newPlace->GetMinigamePlaceDB()->GetDBID();
    running = true;
    sessionFinished = false;
    sessionVictory = false;
    return true;
  }

  virtual bool StartClient()
  {
    ++startClientCalls;
    clientStarted = true;
    return true;
  }

  virtual void Leave()
  {
    ++leaveCalls;
    running = false;
    place = 0;
  }

  virtual void SendLeaveMinigameCommand(PF_Minigames::IWorldSessionInterface* worldInterface)
  {
    ++leaveCommandCalls;
    if (worldInterface)
      worldInterface->OnLeaveMinigameCmd();
    else
      Leave();
  }

  virtual void SendPauseMinigameCommand(PF_Minigames::IWorldSessionInterface* worldInterface, bool enablePause)
  {
    (void)worldInterface;
    ++pauseCommandCalls;
    OnPause(enablePause);
  }

  virtual void OnStep(float deltaTime)
  {
    (void)deltaTime;
    if (running)
      ++stepCalls;
  }

  virtual void Update(float deltaTime, bool gameOnPause)
  {
    (void)deltaTime;
    paused = gameOnPause;
    if (running)
      ++updateCalls;
  }

  virtual void OnPause(bool newPaused)
  {
    ++pauseCalls;
    paused = newPaused;
  }

  virtual const NDb::DBID& GetDBID() const { return dbid; }

  virtual void PlaceUnderFogOfWar(bool newUnderFogOfWar) { underFogOfWar = newUnderFogOfWar; }
  virtual void CheatDropCooldowns() { ++cheatDropCooldownCalls; }
  virtual void CheatWinGame() { ++cheatWinCalls; sessionFinished = true; sessionVictory = true; }

  virtual void SessionFinished(bool victory)
  {
    ++sessionFinishedCalls;
    sessionFinished = true;
    sessionVictory = victory;
  }

  virtual void OnMapLoaded() { ++mapLoadedCalls; }

  virtual void Eject()
  {
    ++ejectCalls;
    Leave();
  }
};

class LinuxNullMinigames : public PF_Minigames::IMinigames, public CObjectBase
{
  OBJECT_METHODS( 0x9D62D442, LinuxNullMinigames );

  CPtr<PFEaselPlayer> owner;
  CObj<PF_Minigames::IMinigamesMain> main;

public:
  ZDATA
  ZNOPARENT(PF_Minigames::IMinigames)
  ZNOPARENT(CObjectBase)
  CPtr<PFMinigamePlace> currentPlace;
  CObj<LinuxNullSingleMinigame> singleMinigame;
  nstl::string singleMinigameId;
  Placement placement;
  bool isLocal;
  int startCalls;
  int leaveCalls;
  int forceLeaveCalls;
  int stepCalls;
  int updateCalls;
  int mapLoadedCalls;
  int initCalls;
  int reinitCalls;
  ZEND int operator&(IBinSaver& f)
  {
    f.Add(2, &currentPlace);
    f.Add(3, &singleMinigame);
    f.Add(4, &singleMinigameId);
    f.Add(5, &placement);
    f.Add(6, &isLocal);
    f.Add(7, &startCalls);
    f.Add(8, &leaveCalls);
    f.Add(9, &forceLeaveCalls);
    f.Add(10, &stepCalls);
    f.Add(11, &updateCalls);
    f.Add(12, &mapLoadedCalls);
    f.Add(13, &initCalls);
    f.Add(14, &reinitCalls);
    return 0;
  }

  LinuxNullMinigames()
    : isLocal(false)
    , startCalls(0)
    , leaveCalls(0)
    , forceLeaveCalls(0)
    , stepCalls(0)
    , updateCalls(0)
    , mapLoadedCalls(0)
    , initCalls(0)
    , reinitCalls(0)
  {
    InitGames();
  }

  LinuxNullMinigames(PFEaselPlayer* player, bool local)
    : owner(player)
    , isLocal(local)
    , startCalls(0)
    , leaveCalls(0)
    , forceLeaveCalls(0)
    , stepCalls(0)
    , updateCalls(0)
    , mapLoadedCalls(0)
    , initCalls(0)
    , reinitCalls(0)
  {
    InitGames();
  }

  void SetMain(PF_Minigames::IMinigamesMain* newMain)
  {
    main = newMain;
  }

  LinuxNullSingleMinigame* EnsureSingleMinigame(const char* id)
  {
    if (!IsValid(singleMinigame))
      singleMinigame = new LinuxNullSingleMinigame();

    if (id && *id)
      singleMinigameId = id;
    else if (singleMinigameId.empty())
      singleMinigameId = "Easel";

    singleMinigame->SetId(singleMinigameId.c_str());
    return singleMinigame.GetPtr();
  }

  virtual bool StartMinigame(PFMinigamePlace* place)
  {
    ++startCalls;
    if (!place)
      return false;

    LinuxNullSingleMinigame* minigame = EnsureSingleMinigame(place->MinigameId().c_str());
    if (!minigame->Start(place))
      return false;

    currentPlace = place;
    placement = place->GetMinigamePlacement();
    if (isLocal)
      minigame->StartClient();
    return true;
  }

  virtual void LeaveMinigame()
  {
    ++leaveCalls;
    if (IsValid(singleMinigame))
      singleMinigame->Leave();
    currentPlace = 0;
  }

  virtual void SetLocal(bool local) { isLocal = local; }
  virtual bool IsLocal() const { return isLocal; }

  virtual void OnStep(float deltaTime)
  {
    ++stepCalls;
    if (IsValid(main))
      main->Step(deltaTime);
    if (IsValid(currentPlace) && IsValid(singleMinigame))
      singleMinigame->OnStep(deltaTime);
  }

  virtual PF_Minigames::IMinigamesMain* GetMain() const { return main; }
  virtual PF_Minigames::IWorldSessionInterface* GetWorldSessionInterface() const { return owner; }

  virtual int MinigamesCount() { return IsValid(singleMinigame) ? 1 : 0; }
  virtual PF_Minigames::ISingleMinigame* GetMinigame(const char* id)
  {
    if (!IsValid(singleMinigame))
      return 0;
    if (!id || !*id)
      return singleMinigame.GetPtr();
    return strcmp(singleMinigame->GetId(), id) == 0 ? singleMinigame.GetPtr() : 0;
  }
  virtual PF_Minigames::ISingleMinigame* GetMinigame(int index)
  {
    return index == 0 && IsValid(singleMinigame) ? singleMinigame.GetPtr() : 0;
  }
  virtual const char* GetMinigameId(int index)
  {
    return index == 0 && IsValid(singleMinigame) ? singleMinigame->GetId() : 0;
  }
  virtual PF_Minigames::ISingleMinigame* GetCurrentMinigame()
  {
    return IsValid(currentPlace) && IsValid(singleMinigame) ? singleMinigame.GetPtr() : 0;
  }

  virtual void UpdateM(float deltaTime)
  {
    ++updateCalls;
    if (IsValid(currentPlace) && IsValid(singleMinigame))
      singleMinigame->Update(deltaTime, false);
  }

  virtual void SetPlacement(const Placement& newPlacement) { placement = newPlacement; }
  virtual float GetMinigamePlaceOpacity() const { return IsValid(currentPlace) ? 1.0f : 0.0f; }
  virtual bool DoFade() const { return IsValid(currentPlace); }

  virtual void ForceLeaveMinigame()
  {
    ++forceLeaveCalls;
    if (IsValid(singleMinigame))
      singleMinigame->SendLeaveMinigameCommand(owner);
    else if (IsValid(owner))
      owner->OnLeaveMinigameCmd();
  }

  virtual void OnMapLoaded()
  {
    ++mapLoadedCalls;
    if (IsValid(singleMinigame))
      singleMinigame->OnMapLoaded();
  }
  virtual void InitGames()
  {
    ++initCalls;
    EnsureSingleMinigame(singleMinigameId.empty() ? "Easel" : singleMinigameId.c_str());
  }
  virtual void ReinitGames()
  {
    ++reinitCalls;
    currentPlace = 0;
    InitGames();
  }
};
PF_Minigames::IMinigames* LinuxNullMinigamesMain::ProduceMinigamesObject(
  PFWorld* pWorld,
  PF_Minigames::IWorldSessionInterface* worldInterf,
  bool isLocal)
{
  if (pWorld && !IsValid(world))
    Set(pWorld->GetScene(), pWorld);

  ++producedObjects;
  PFEaselPlayer* player = dynamic_cast<PFEaselPlayer*>(worldInterf);
  LinuxNullMinigames* minigames = new LinuxNullMinigames(player, isLocal);
  minigames->SetMain(this);
  return minigames;
}


PFEaselPlayer::PFEaselPlayer(PFWorld* pWorld, const SpawnInfo &info, NDb::EUnitType unitType, NDb::EFaction faction, NDb::EFaction _originalFaction)
  : PFBaseHero(pWorld, info, unitType, faction, _originalFaction)
  , bidon(NDb::BIDONTYPE_NONE)
{
  const bool isLocal = info.playerId >= 0 && IsValid(GetPlayer()) && GetPlayer()->IsLocal();
  CObj<PF_Minigames::IMinigamesMain> main = new LinuxNullMinigamesMain(pWorld);
  minigames = main->ProduceMinigamesObject(pWorld, this, isLocal);
}

void PFEaselPlayer::OnDestroyContents() { minigames = 0; PFBaseHero::OnDestroyContents(); }
void PFEaselPlayer::ForceLeaveMinigame()
{
  if (IsValid(minigames))
    minigames->ForceLeaveMinigame();
  else
    OnLeaveMinigameCmd();
}
bool PFEaselPlayer::Step(float dtInSeconds)
{
  if (IsValid(GetPlayer()) && !GetPlayer()->IsPlaying() && IsIsolated())
    OnLeaveMinigameCmd();

  if (IsValid(minigames))
    minigames->OnStep(dtInSeconds);

  return PFBaseHero::Step(dtInSeconds);
}
void PFEaselPlayer::OnGameFinished( const NDb::EFaction failedFaction ) { PFBaseHero::OnGameFinished(failedFaction); }
void PFEaselPlayer::OnBeforeClose()
{
  if (IsValid(minigamePlace) || IsIsolated() || CheckFlag(NDb::UNITFLAG_INMINIGAME))
    OnLeaveMinigameCmd();
}
void PFEaselPlayer::DropCooldowns( DropCooldownParams const& dropCooldownParams ) { PFBaseHero::DropCooldowns(dropCooldownParams); }
void PFEaselPlayer::Isolate( bool isolate ) { isolated = isolate; }
bool PFEaselPlayer::StartMinigame( PFMinigamePlace * pPlace )
{
  if (!pPlace || pPlace->CurrentEaselPlayer())
    return false;

  if (IsValid(minigamePlace) && minigamePlace != pPlace)
    minigamePlace->SetEaselPlayer(0);

  minigamePlace = pPlace;
  minigamePlace->SetEaselPlayer(this);
  Isolate(true);
  FlushStateQueue();
  if (!CheckFlag(NDb::UNITFLAG_INMINIGAME))
    AddFlag(NDb::UNITFLAG_INMINIGAME);
  if (IsValid(minigames))
  {
    minigames->SetPlacement(pPlace->GetMinigamePlacement());
    minigames->StartMinigame(pPlace);
  }
  pPlace->OnPlayerEnter();
  LogMinigameEvent(SessionEventType::MG2Started, 0, 0);
  MinigameEvent(NDb::BASEUNITEVENT_MINIGAMESTARTED);
  return true;
}
bool PFEaselPlayer::OnLeaveMinigameCmd()
{
  const bool hadMinigame = IsValid(minigamePlace) || IsIsolated() || CheckFlag(NDb::UNITFLAG_INMINIGAME);
  if (CheckFlag(NDb::UNITFLAG_INMINIGAME))
    RemoveFlag(NDb::UNITFLAG_INMINIGAME);
  Isolate(false);
  if (IsValid(minigames))
    minigames->LeaveMinigame();
  if (IsValid(minigamePlace))
  {
    minigamePlace->OnPlayerLeave();
    minigamePlace->SetEaselPlayer(0);
  }
  minigamePlace = 0;
  if (hadMinigame)
  {
    LogMinigameEvent(SessionEventType::MG2Exit, 0, 0);
    MinigameEvent(NDb::BASEUNITEVENT_MINIGAMEEXIT);
  }
  return true;
}
void PFEaselPlayer::GetItemTransferTargets( vector<CPtr<PFBaseHero> > & targets ) { targets.clear(); }
bool PFEaselPlayer::CanGetScrollDuplicate( PFBaseHero * target ) { (void)target; return false; }
bool PFEaselPlayer::AddItemToHero( PFBaseHero * target, const NDb::Consumable * pDBDesc, int quantity ) { return target ? target->TakeConsumable(pDBDesc, quantity, NDb::CONSUMABLEORIGIN_MINIGAME) : false; }
void PFEaselPlayer::SetCurrentBidon( NDb::EBidonType _bidon ) { bidon = _bidon; }
void PFEaselPlayer::SetNaftaInfoProvider( NGameX::INaftaInfoProvider * naftaInfoProvider ) { (void)naftaInfoProvider; }
bool PFEaselPlayer::CanBuyZZBoost() { return false; }
void PFEaselPlayer::BuyZZBoost() {}
void PFEaselPlayer::LogMinigameEvent( SessionEventType::EventType eventType, int param1, int param2 ) { LogSessionEvent(eventType, param1); (void)param2; }
void PFEaselPlayer::OnMapLoaded() { if (IsValid(minigames)) minigames->OnMapLoaded(); }
void PFEaselPlayer::MinigameEvent( NDb::EBaseUnitEvent eventType) { (void)eventType; }
void PFEaselPlayer::OnUnitDie(CPtr<PFBaseUnit> pKiller, int flags, PFBaseUnitDamageDesc const* pDamageDesc) { PFBaseHero::OnUnitDie(pKiller, flags, pDamageDesc); }

} //namespace NWorld

REGISTER_WORLD_OBJECT_NM( PFEaselPlayer, NWorld )
REGISTER_SAVELOAD_CLASS_NM( LinuxNullMinigamesMain, NWorld )
REGISTER_SAVELOAD_CLASS_NM( LinuxNullSingleMinigame, NWorld )
REGISTER_SAVELOAD_CLASS_NM( LinuxNullMinigames, NWorld )
BASIC_REGISTER_CLASS( PF_Minigames::IMinigames );

#else
#include "PFEaselPlayer.h"

#include "PFAbilityData.h"

#include "PFMinigamePlace.h"
#include "PFChest.h"
#include "PFStatistics.h"
#include "PFImpulsiveBuffs.h"
#include "PFPredefinedUnitVariables.h"

#ifndef VISUAL_CUTTED
#include "PFClientMinigamePlace.h"
#include "AdventureScreen.h"
#else //VISUAL_CUTTED
#include "../Game/PF/Audit/ClientStubs.h"
#endif //VISUAL_CUTTED

#include "SessionEventType.h"

#include "PlayerBehaviourTracking.h"

namespace 
{
  void HideUnit(NWorld::PFBaseUnit * unit, bool hide)
  {
    if (!IsValid(unit))
      return;

    unit->Hide( hide );
    unit->StopAttackingMe();

    if( hide )
      unit->EventHappened(NWorld::PFBaseUnitEvent(NDb::BASEUNITEVENT_ISOLATE));

  }
}

namespace NWorld
{


PFEaselPlayer::PFEaselPlayer(PFWorld* pWorld, const SpawnInfo &info, NDb::EUnitType unitType, NDb::EFaction faction, NDb::EFaction _originalFaction)
  : PFBaseHero(pWorld, info, unitType , faction, _originalFaction)
  , bidon(NDb::BIDONTYPE_NONE)
{  
  if( info.playerId >= 0 && IsValid(GetWorld()->GetMinigamesMain()))
  {
    bool allowLocalMinigame;
    if ( NGameX::AdventureScreen::Instance()->IsSpectator() )
    {
      allowLocalMinigame = false;
    }
    else
    {
      bool isReplay = NGameX::AdventureScreen::Instance()->IsInReplayMode();
      allowLocalMinigame = !isReplay && pWorld->GetPlayer(info.playerId)->IsLocal();
    }

    minigames = GetWorld()->GetMinigamesMain()->ProduceMinigamesObject( pWorld, this, allowLocalMinigame );
  }
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFEaselPlayer::OnDestroyContents()
{
  minigames = 0;
 
  PFBaseHero::OnDestroyContents();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFEaselPlayer::ForceLeaveMinigame()
{
  if( IsValid( minigames ) )
    minigames->ForceLeaveMinigame();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFEaselPlayer::Step(float dtInSeconds)
{
  if ( IsValid( GetPlayer() ) )
  {
    if ( !GetPlayer()->IsPlaying() && IsIsolated() )
      OnLeaveMinigameCmd();
  }

  if( IsValid( minigames ) )
    minigames->OnStep( dtInSeconds );

  return PFBaseHero::Step(dtInSeconds);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFEaselPlayer::OnGameFinished( const NDb::EFaction failedFaction )
{
  PFBaseHero::OnGameFinished( failedFaction );

  if ( minigames )
    if ( IsValid( minigames->GetCurrentMinigame() ) )
      minigames->GetCurrentMinigame()->SessionFinished( failedFaction != GetFaction() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFEaselPlayer::OnBeforeClose()
{
  // If we've got here (exit via menu, Alt+F4, etc.) and still have minigame - log exit
  if ( minigames )
    if ( IsValid( minigames->GetCurrentMinigame() ) )
    {
      LogMinigameEvent(SessionEventType::MG2Exit, 0, 0);
      MinigameEvent(NDb::BASEUNITEVENT_MINIGAMEEXIT);
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFEaselPlayer::DropCooldowns( DropCooldownParams const& dropCooldownParams )
{
  PFBaseHero::DropCooldowns( dropCooldownParams );

  if ( minigames )
    if ( IsValid( minigames->GetCurrentMinigame() ) )
      minigames->GetCurrentMinigame()->CheatDropCooldowns();
}



//////////////////////////////////////////////////////////////////////////
void PFEaselPlayer::Isolate( bool isolate )
{
  if( isolated == isolate )
    return;

  isolated = isolate;

  HideUnit(this, isolate);

  struct Func : public ISummonAction, public NonCopyable
  {
    bool hide;
    Func(bool _hide): hide(_hide) {}
    virtual void operator()(PFBaseUnit * pUnit)
    {
      HideUnit(pUnit, hide);
    }
  } hider(isolate);

  ForAllSummons(hider, NDb::SUMMONTYPE_PRIMARY);
  ForAllSummons(hider, NDb::SUMMONTYPE_PET);
}


//////////////////////////////////////////////////////////////////////////
bool PFEaselPlayer::StartMinigame( PFMinigamePlace * pPlace )
{
  if ( !pPlace->CanBeUsedBy( this ) )
    return false;

  if( pPlace->GetFaction() != GetFaction() )
    return false;

  if( pPlace->CurrentEaselPlayer() )
    return false;

  bool isMount = IsMount();
  if ( isMount )
  {
    if ( PFBaseHero* hero = dynamic_cast<PFBaseHero*>(GetAttachUnit().GetPtr()) )
    {
      hero->EventHappened(NWorld::PFBaseUnitMinigameEvent(NDb::BASEUNITEVENT_MINIGAMESTARTED, this));
    }
  }

  NI_ASSERT( pPlace, "Null minigame place. NUM_TASK?" );
  minigamePlace = pPlace;
  minigamePlace->SetEaselPlayer( this );

  Isolate( true );

  // Избавляемся от стейтов в очереди. 
  FlushStateQueue();

  if (!CheckFlag(NDb::UNITFLAG_INMINIGAME))
    AddFlag(NDb::UNITFLAG_INMINIGAME);

#ifndef VISUAL_CUTTED
  NGameX::AdventureScreen::Instance()->OnEnterLeaveMinigame( this, true );
#endif //VISUAL_CUTTED

  Placement placement = minigamePlace->GetMinigamePlacement();
  minigames->SetPlacement( placement);

  minigames->StartMinigame( pPlace );

  pPlace->OnPlayerEnter();

  //just log mg2 started without params
  LogMinigameEvent(SessionEventType::MG2Started, 0, 0);
  MinigameEvent(NDb::BASEUNITEVENT_MINIGAMESTARTED);

  GetWorld()->SetIgnoreSlowdownHint( true );

  return true;
}


//////////////////////////////////////////////////////////////////////////
bool PFEaselPlayer::OnLeaveMinigameCmd()
{
  DebugTrace( "OnLeaveMinigameCmd..." ); //NUM_TASK debugging

  SetNaftaInfoProvider(0);

  if (CheckFlag(NDb::UNITFLAG_INMINIGAME))
    RemoveFlag(NDb::UNITFLAG_INMINIGAME);

  Isolate( false );

#ifndef VISUAL_CUTTED
  NGameX::AdventureScreen::Instance()->OnEnterLeaveMinigame( this, false );
#endif //VISUAL_CUTTED

  if ( minigames )
    minigames->LeaveMinigame();

  GetWorld()->SetIgnoreSlowdownHint( false );

  NI_VERIFY( IsValid( minigamePlace ), "minigame place somehow is not valid. may be NUM_TASK?",  minigamePlace = 0; return true; );
  minigamePlace->OnPlayerLeave();
  minigamePlace->SetEaselPlayer( 0 );
  minigamePlace = 0;

  LogMinigameEvent(SessionEventType::MG2Exit, 0, 0);
  MinigameEvent(NDb::BASEUNITEVENT_MINIGAMEEXIT);

  return true;
}



void PFEaselPlayer::GetItemTransferTargets( vector<CPtr<PFBaseHero>> & targets )
{
  targets.clear();

  NDb::EFaction we = GetFaction();

  const PFWorld * world = GetWorld();
  for ( int i = 0; i < world->GetPlayersCount(); ++i )
  {
    PFPlayer * player = world->GetPlayer( i );
    PFBaseHero * hero = player->GetHero();
    if ( !hero )
      continue;

    hero->IsLocal();

    if ( hero->GetFaction() != we )
      continue;

    targets.push_back( hero );
  }
}

bool PFEaselPlayer::CanGetScrollDuplicate( PFBaseHero * target )
{
  if ( this == target )
    return false;

  const float probability = GetVariableValue( UnitVariables::szScrollDuplicationProc );

  return GetWorld()->GetRndGen()->Roll( probability );
}

bool PFEaselPlayer::AddItemToHero( PFBaseHero * target, const NDb::Consumable * pDBDesc, int quantity )
{
  if ( target )
  {
    const EPlayerBehaviourEvent::Enum behaviourEvent = (this == target)
      ? EPlayerBehaviourEvent::TookScroll
      : EPlayerBehaviourEvent::GaveScroll;

    PlayerBehaviourTracking::DispatchEvent(this, behaviourEvent);

    // Gain the same scroll if possible
    if ( CanGetScrollDuplicate( target ) )
    {
      if ( this->TakeConsumable( pDBDesc, quantity, NDb::CONSUMABLEORIGIN_MINIGAME ) )
      {
        GetWorld()->GetStatistics()->NotifyItemTransfer( this, this, pDBDesc );
      }
    }

    if ( target->TakeConsumable( pDBDesc, quantity, NDb::CONSUMABLEORIGIN_MINIGAME ) )
    {
      GetWorld()->GetStatistics()->NotifyItemTransfer( this, target, pDBDesc );
      // Play FX on a local hero who gained scroll
      if ( this != target && target->IsLocal() )
      {
        target->OnScrollReceived();
      }
      return true;
    }
  }
  return false;
}



void PFEaselPlayer::SetCurrentBidon( NDb::EBidonType _bidon ) 
{ 
  //TODO refactor this

  if ( NDb::BIDONTYPE_NONE <= _bidon && _bidon <= NDb::BIDONTYPE_PALETTE )
  {
    bidon = _bidon; 

    if ( !IsValid( minigames ) )
      return;

    const NDb::Bidon& bidonDesc = minigames->GetMain()->GetCommonDBData()->sessionBidonAbilities[bidon];

    if ( !bidonDesc.ability )
      return;

    //CALL_CLIENT_1ARGS(OnChangeBidon, bidonDesc );

    PFAbilityData *pA = new PFAbilityData(this, bidonDesc.ability, NDb::ABILITYTYPEID_ABILITY2 );
    SetAbility(NDb::ABILITY_ID_2, pA);
  }
}

void PFEaselPlayer::SetNaftaInfoProvider( NGameX::INaftaInfoProvider * naftaInfoProvider )
{
  NGameX::IAdventureScreen * advScreen = GetWorld()->GetIAdventureScreen();
   if (IsValid(advScreen))
     advScreen->SetNaftaInfoProvider(naftaInfoProvider);

}

bool PFEaselPlayer::CanBuyZZBoost()
{
  NGameX::IAdventureScreen * advScreen = GetWorld()->GetIAdventureScreen();
  NI_VERIFY(IsValid(advScreen), "", return false;)
  ImpulsiveBuffsManager * buffsManager = advScreen->GetImpulseBuffsManager();
  NI_VERIFY(IsValid(buffsManager), "impulse buff manager is not setted", return false;)

  return buffsManager->CanBuyService(NDb::GENERALSERVICES_ZZBOOST);
}

void PFEaselPlayer::BuyZZBoost()
{
  if (!IsLocal())
    return;
  NGameX::IAdventureScreen * advScreen = GetWorld()->GetIAdventureScreen();
  NI_VERIFY(IsValid(advScreen), "", return ;);
  ImpulsiveBuffsManager * buffsManager = advScreen->GetImpulseBuffsManager();
  NI_VERIFY(IsValid(buffsManager), "impulse buff manager is not setted", return;);
  buffsManager->BuyService(NDb::GENERALSERVICES_ZZBOOST);
}

void PFEaselPlayer::LogMinigameEvent( SessionEventType::EventType eventType, int param1, int param2 )
{
  StatisticService::RPC::SessionEventInfo params;
  params.intParam1 = param1;
  params.intParam2 = param2;
  LogSessionEvent(eventType,params);
}

void PFEaselPlayer::OnMapLoaded()
{
  if( IsValid( minigames ) )
    minigames->OnMapLoaded();
}

void PFEaselPlayer::MinigameEvent( NDb::EBaseUnitEvent eventType)
{
  PFBaseUnitMinigameEvent evt(eventType, this);
  EventHappened(evt);
}

void PFEaselPlayer::OnUnitDie(CPtr<PFBaseUnit> pKiller, int flags, PFBaseUnitDamageDesc const* pDamageDesc /*= 0*/)
{
  if (CheckFlagType(NDb::UNITFLAGTYPE_INMINIGAME))
  {
    minigames->GetCurrentMinigame()->Eject();
  }

  PFBaseHero::OnUnitDie(pKiller, flags, pDamageDesc);
}

} //namespace NWorld

REGISTER_WORLD_OBJECT_NM( PFEaselPlayer, NWorld )
BASIC_REGISTER_CLASS( PF_Minigames::IMinigames );

#endif
