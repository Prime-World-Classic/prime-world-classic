#include "stdafx.h"
#if defined( PW_LINUX_NULL_RENDER )
namespace NWorld
{
class PFBaseUnit;
}

template<> inline NWorld::PFBaseUnit* CastToUserObjectImpl<NWorld::PFBaseUnit>(CObjectBase*, NWorld::PFBaseUnit*, CObjectBase*) { return 0; }

#include "PFWorld.h"
#include "PFBaseMovingUnit.h"
#include "PFHero.h"
#include "PFResourcesCollectionClient.h"
#include "DBAdvMap.h"
#include "DBHeroesList.h"
#include "DBSessionRoots.h"
#include "TileMap.h"
#include "PFPlayer.h"
#include "WarFog.h"
#include "PFWorldProtection.h"
#include "DayNightController.h"
#include "PFWorldNatureMap.h"
#include "TriggerMarkerHandler.h"
#include "CollisionResolver.h"
#include "PFAIContainer.h"
#include "PFAIWorld.h"
#include "PFCommonCreep.h"
#include "PFBuildings.h"
#include "PFTower.h"
#include "PFGlyph.h"
#include "PFNeutralCreep.h"
#include "PFMainBuilding.h"
#include "PFMinigamePlace.h"
#include "PFRoadFlagpole.h"
#include "PFScriptedFlagpole.h"
#include "PFSimpleObject.h"
#include "PFTree.h"
#include "PF_Core/WorldObject.h"
#include "Scene/DBSceneBase.h"
#include "Core/GameCommand.h"
#include "Core/WorldCommand.h"
#include "HybridServer/PeeredTypes.h"
#include "System/Crc32Checksum.h"
#include "System/LoadingProgress.h"

NI_DEFINE_REFCOUNT( NGameX::IAdventureScreen );
REGISTER_SAVELOAD_CLASS_NM(PFWorld, NWorld);

namespace NWorld
{
int PFWorld::instanceCount = 0;

PFStatistics* CreateLinuxPFStatistics(PFWorld* pWorld);
bool StepLinuxPFStatistics(PFStatistics* pStatistics, float dtInSeconds);
void NotifyLinuxItemTransfer(PFStatistics* pStatistics, PFBaseHero* from, PFBaseHero* to, const NDb::Consumable* dbItem);

PFWorld::PFWorld() :
step( -1 ),
timeElapsed( 0.f ),
manualGameFinish(false),
humanPlayersCount(0),
worldChecker(this),
ambienceMap(NULL),
totalCreepsCount(0),
allScriptFunctionsEnabled(false),
stepLength(DEFAULT_GAME_STEP_LENGTH),
stepLengthInSeconds(DEFAULT_GAME_STEP_LENGTH * 0.001f),
defeatedFaction(NDb::FACTION_NEUTRAL),
timeScale(1.0f),
linuxLoadedWarFogUnblockObjects(0),
linuxLoadedSimpleObjects(0),
linuxLoadedMultiStateObjects(0),
linuxLoadedTreeObjects(0),
linuxLoadedGlyphSpawnerObjects(0),
linuxLoadedAdvMapObstacleObjects(0),
linuxLoadedHeroPlaceHolderObjects(0),
linuxLoadedCreepSpawnerObjects(0),
linuxLoadedNeutralCreepSpawnerObjects(0),
linuxLoadedSimpleBuildingObjects(0),
linuxLoadedUsableBuildingObjects(0),
linuxLoadedShopObjects(0),
linuxLoadedQuarterObjects(0),
linuxLoadedTowerObjects(0),
linuxLoadedControllableTowerObjects(0),
linuxLoadedFountainObjects(0),
linuxLoadedRoadFlagpoleObjects(0),
linuxLoadedScriptedFlagpoleObjects(0),
linuxLoadedMainBuildingObjects(0),
linuxLoadedMinigamePlaceObjects(0),
linuxLoadedCameraSplineObjects(0),
linuxLoadedScriptPathObjects(0),
linuxLoadedScriptPolygonAreaObjects(0),
linuxLastSteppedSpawnerObjects(0),
linuxLastSteppedCreepSpawnerObjects(0),
linuxLastSteppedNeutralCreepSpawnerObjects(0),
linuxSpawnedHeroObjects(0),
linuxPlayersWithHeroObjects(0),
linuxExecutedPackedWorldCommands(0),
linuxLastPackedWorldCommandClientId(-1),
linuxLastPackedWorldCommandTypeId(0),
linuxBootstrapRuntimeCommands(0),
linuxLastBootstrapRuntimeCommandClientId(-1),
linuxLastBootstrapRuntimeCommandToken(0),
linuxLastBootstrapRuntimeCommandValue(0.0f),
linuxStoredDeadUnits(0),
linuxCleanedDeadUnits(0),
linuxLastStoredDeadUnitObjectId(-1),
linuxLastCleanedDeadUnitObjectId(-1),
linuxPlayerStatusUpdates(0),
linuxPlayerStatusMissing(0),
linuxPlayerStatusActiveUpdates(0),
linuxPlayerStatusAwayUpdates(0),
linuxPlayerStatusPlayingUpdates(0),
linuxPlayerStatusDisconnectedUpdates(0),
linuxPlayerStatusReconnectedUpdates(0),
linuxPlayerStatusLeaverUpdates(0),
linuxLastPlayerStatusClientId(-1),
linuxLastPlayerStatusValue(-1),
linuxLastPlayerStatusStep(-1),
linuxLastPlayerStatusPlaying(-1),
linuxLastPlayerStatusActive(-1),
linuxLastPlayerStatusDisconnected(-1),
linuxLastPlayerStatusLeaver(-1),
linuxAIAutoStartAttempts(0),
linuxAIAutoStartSuccesses(0),
linuxAIAddRequests(0),
linuxAIAddSuccesses(0),
linuxAIRemoveRequests(0),
linuxAIRemoveSuccesses(0),
linuxAIStepCalls(0),
linuxAIControllerCount(0),
linuxAIBotsSettingsAvailable(0),
linuxAIBotsEnabled(-1),
linuxAILastHeroObjectId(-1),
linuxAILastPlayerId(-1),
linuxAILastUserId(-1),
linuxAILastLine(-1),
linuxAICommandAttempts(0),
linuxAICommandsSent(0),
linuxAICommandDirectFallbacks(0),
linuxAICommandMoveSent(0),
linuxAICommandCombatMoveSent(0),
linuxAICommandAttackSent(0),
linuxAICommandOtherSent(0),
linuxAILastCommandKind(0),
linuxAILastCommandHeroObjectId(-1),
linuxAILastCommandPlayerId(-1),
linuxAILastCommandUserId(-1),
linuxAILastCommandTargetObjectId(-1),
linuxAILastCommandSent(0),
linuxAutoAIEnabled(false)
{
}

PFWorld::PFWorld(const NCore::MapStartInfo&, NScene::IScene* _scene, NGameX::IAdventureScreen* _screen, PFResourcesCollection* _collection, int _stepLength, bool, int) :
step( -1 ),
timeElapsed( 0.f ),
pScene(_scene),
adventureScreen(_screen),
manualGameFinish(false),
humanPlayersCount(0),
worldChecker(this),
ambienceMap(NULL),
resourcesCollection(_collection),
totalCreepsCount(0),
allScriptFunctionsEnabled(false),
stepLength(_stepLength),
stepLengthInSeconds(_stepLength * 0.001f),
defeatedFaction(NDb::FACTION_NEUTRAL),
timeScale(1.0f),
linuxLoadedWarFogUnblockObjects(0),
linuxLoadedSimpleObjects(0),
linuxLoadedMultiStateObjects(0),
linuxLoadedTreeObjects(0),
linuxLoadedGlyphSpawnerObjects(0),
linuxLoadedAdvMapObstacleObjects(0),
linuxLoadedHeroPlaceHolderObjects(0),
linuxLoadedCreepSpawnerObjects(0),
linuxLoadedNeutralCreepSpawnerObjects(0),
linuxLoadedSimpleBuildingObjects(0),
linuxLoadedUsableBuildingObjects(0),
linuxLoadedShopObjects(0),
linuxLoadedQuarterObjects(0),
linuxLoadedTowerObjects(0),
linuxLoadedControllableTowerObjects(0),
linuxLoadedFountainObjects(0),
linuxLoadedRoadFlagpoleObjects(0),
linuxLoadedScriptedFlagpoleObjects(0),
linuxLoadedMainBuildingObjects(0),
linuxLoadedMinigamePlaceObjects(0),
linuxLoadedCameraSplineObjects(0),
linuxLoadedScriptPathObjects(0),
linuxLoadedScriptPolygonAreaObjects(0),
linuxLastSteppedSpawnerObjects(0),
linuxLastSteppedCreepSpawnerObjects(0),
linuxLastSteppedNeutralCreepSpawnerObjects(0),
linuxSpawnedHeroObjects(0),
linuxPlayersWithHeroObjects(0),
linuxExecutedPackedWorldCommands(0),
linuxLastPackedWorldCommandClientId(-1),
linuxLastPackedWorldCommandTypeId(0),
linuxBootstrapRuntimeCommands(0),
linuxLastBootstrapRuntimeCommandClientId(-1),
linuxLastBootstrapRuntimeCommandToken(0),
linuxLastBootstrapRuntimeCommandValue(0.0f),
linuxStoredDeadUnits(0),
linuxCleanedDeadUnits(0),
linuxLastStoredDeadUnitObjectId(-1),
linuxLastCleanedDeadUnitObjectId(-1),
linuxPlayerStatusUpdates(0),
linuxPlayerStatusMissing(0),
linuxPlayerStatusActiveUpdates(0),
linuxPlayerStatusAwayUpdates(0),
linuxPlayerStatusPlayingUpdates(0),
linuxPlayerStatusDisconnectedUpdates(0),
linuxPlayerStatusReconnectedUpdates(0),
linuxPlayerStatusLeaverUpdates(0),
linuxLastPlayerStatusClientId(-1),
linuxLastPlayerStatusValue(-1),
linuxLastPlayerStatusStep(-1),
linuxLastPlayerStatusPlaying(-1),
linuxLastPlayerStatusActive(-1),
linuxLastPlayerStatusDisconnected(-1),
linuxLastPlayerStatusLeaver(-1),
linuxAIAutoStartAttempts(0),
linuxAIAutoStartSuccesses(0),
linuxAIAddRequests(0),
linuxAIAddSuccesses(0),
linuxAIRemoveRequests(0),
linuxAIRemoveSuccesses(0),
linuxAIStepCalls(0),
linuxAIControllerCount(0),
linuxAIBotsSettingsAvailable(0),
linuxAIBotsEnabled(-1),
linuxAILastHeroObjectId(-1),
linuxAILastPlayerId(-1),
linuxAILastUserId(-1),
linuxAILastLine(-1),
linuxAICommandAttempts(0),
linuxAICommandsSent(0),
linuxAICommandDirectFallbacks(0),
linuxAICommandMoveSent(0),
linuxAICommandCombatMoveSent(0),
linuxAICommandAttackSent(0),
linuxAICommandOtherSent(0),
linuxAILastCommandKind(0),
linuxAILastCommandHeroObjectId(-1),
linuxAILastCommandPlayerId(-1),
linuxAILastCommandUserId(-1),
linuxAILastCommandTargetObjectId(-1),
linuxAILastCommandSent(0),
linuxAutoAIEnabled(false)
{
}

void PFWorld::OnDestroyContents() {}
void WorldChecker::Save() const {}
void WorldChecker::Load() {}

bool PFWorld::LoadMap(const NDb::AdvMapDescription* _advMapDescription, const NDb::AdventureCameraSettings*, const NCore::TPlayersStartInfo& playersInfo, LoadingProgress* progress, bool, const NWorld::PFResourcesCollection::TalentMap& talents)
{
  advMapDescription = _advMapDescription;
  humanPlayersCount = 0;
  players.clear();
  int maxPlayerId = -1;
  for (NCore::TPlayersStartInfo::const_iterator it = playersInfo.begin(); it != playersInfo.end(); ++it)
  {
    if (it->playerID > maxPlayerId)
      maxPlayerId = it->playerID;
    if (it->playerType == NCore::EPlayerType::Human)
      ++humanPlayersCount;
  }
  allScriptFunctionsEnabled = humanPlayersCount <= 1;

  if (maxPlayerId >= 0)
  {
    players.resize(maxPlayerId + 1);
    for (NCore::TPlayersStartInfo::const_iterator it = playersInfo.begin(); it != playersInfo.end(); ++it)
    {
      if (it->playerID < 0)
        continue;

      players[it->playerID] = new PFPlayer(
        this,
        it->playerID,
        it->teamID,
        it->originalTeamID,
        it->userID,
        it->zzimaSex,
        false,
        0,
        false,
        false);
    }
  }

  if (IsValid(advMapDescription) && IsValid(advMapDescription->map) && IsValid(advMapDescription->map->terrain))
  {
    const NDb::Terrain* terrain = advMapDescription->map->terrain;
    const int tilesX = terrain->elemXCount * terrain->tilesPerElement;
    const int tilesY = terrain->elemYCount * terrain->tilesPerElement;
    mapSize = CVec2(static_cast<float>(tilesX), static_cast<float>(tilesY));
    if (!pTileMap)
      pTileMap = new TileMap(this);
    pTileMap->Prepare(tilesX, tilesY, 1.0f);
    if (!pAIWorld)
    {
      pAIWorld = new PFAIWorld(this);
      pAIWorld->SetVoxelMapSizes(pTileMap);
    }
    if (!warFog)
      warFog = new FogOfWar(this, NDb::KnownEnum<NDb::EFaction>::sizeOf, tilesX, tilesY, 4, 0);
    warFog->ResetVisibility();
    if (!pNatureMap)
      pNatureMap = new PFWorldNatureMap(this);
    pNatureMap->OnLoaded(terrain);
  }

  if (progress)
  {
    progress->SetPartialProgress(EMapLoadStages::Terrain, 1.0f);
    progress->SetPartialProgress(EMapLoadStages::PathFinding, 1.0f);
    progress->SetPartialProgress(EMapLoadStages::Scene, 1.0f);
  }

  if (!LoadSceneMapObjects(advMapDescription, playersInfo, IsValid(advMapDescription) && advMapDescription->mapType == NDb::MAPTYPE_TUTORIAL, progress, talents))
    return false;

  LoadPrecachedResources(advMapDescription);

  NDb::Ptr<NDb::AdvMapSettings> advMapSettings;
  if (IsValid(advMapDescription))
  {
    advMapSettings = IsValid(advMapDescription->mapSettings) ? advMapDescription->mapSettings :
      (IsValid(advMapDescription->map) ? advMapDescription->map->mapSettings : NDb::Ptr<NDb::AdvMapSettings>());
  }
  if (!triggerMarkerHandler && IsValid(advMapSettings))
    triggerMarkerHandler = new TriggerMarkerHandler(this, advMapSettings);
  if (!pResolver)
    pResolver = new CollisionResolver(this);
  if (!pAIContainer)
    pAIContainer = new PFAIContainer(this, 0);
  if (pAIWorld)
    pAIWorld->SetMapData(advMapDescription, advMapSettings);

  const NDb::BotsSettings* botsSettings = GetBotsSettings();
  linuxAIBotsSettingsAvailable = botsSettings ? 1 : 0;
  linuxAIBotsEnabled = botsSettings ? (botsSettings->enableBotsAI ? 1 : 0) : -1;
  if (linuxAutoAIEnabled && (!botsSettings || botsSettings->enableBotsAI))
  {
    for (NCore::TPlayersStartInfo::const_iterator it = playersInfo.begin(); it != playersInfo.end(); ++it)
    {
      if (it->playerType != NCore::EPlayerType::Computer || it->playerID < 0 || it->playerID >= players.size())
        continue;

      PFPlayer* player = players[it->playerID];
      if (!player || !player->GetHero())
        continue;

      const int successesBefore = linuxAIAddSuccesses;
      ++linuxAIAutoStartAttempts;
      AddAI(player->GetHero(), it->playerID % 3);
      if (linuxAIAddSuccesses > successesBefore)
        ++linuxAIAutoStartSuccesses;
    }
  }

  if (!protection)
    protection = PFWorldProtection::Create(this);
  if (!dayNightController)
  {
    dayNightController = new DayNightController(this);
    dayNightController->Initialize();
  }
  if (!pStatistics)
    pStatistics = CreateLinuxPFStatistics(this);

  if (progress)
    progress->SetPartialProgress(EMapLoadStages::HeightMap, 1.0f);

  return true;
}

void PFWorld::Reset() {}
void PFWorld::ResetClientObjects() {}
PFPlayer* PFWorld::GetPlayer(int id) const { return 0 <= id && id < players.size() ? players[id] : 0; }
PFPlayer* PFWorld::GetPlayerByUID(int userId) const
{
  for (vector<CObj<PFPlayer> >::const_iterator it = players.begin(); it != players.end(); ++it)
  {
    PFPlayer* player = *it;
    if (player && player->GetUserID() == userId)
      return player;
  }
  return 0;
}
const int PFWorld::GetPresentPlayersCount() const { return humanPlayersCount; }
const int PFWorld::GetPresentPlayersCount(NDb::EFaction) const { return humanPlayersCount; }
void PFWorld::UpdatePlayerStatuses(const NCore::TStatuses& statuses)
{
  for (int i = 0; i < statuses.size(); ++i)
  {
    const NCore::ClientStatus& clientStatus = statuses[i];
    ++linuxPlayerStatusUpdates;
    linuxLastPlayerStatusClientId = clientStatus.clientId;
    linuxLastPlayerStatusValue = clientStatus.status;
    linuxLastPlayerStatusStep = clientStatus.step;

    PFPlayer* player = GetPlayerByUID(clientStatus.clientId);
    if (!player)
    {
      ++linuxPlayerStatusMissing;
      linuxLastPlayerStatusPlaying = -1;
      linuxLastPlayerStatusActive = -1;
      linuxLastPlayerStatusDisconnected = -1;
      linuxLastPlayerStatusLeaver = -1;
      continue;
    }

    const bool playing = Peered::IsPlayingStatus(clientStatus.status);
    const bool active = (clientStatus.status == Peered::Active);
    const bool away = (clientStatus.status == Peered::Away);
    const bool disconnected = Peered::IsDisconnectedStatus(clientStatus.status);
    const bool leaver = (clientStatus.status == Peered::RefusedToReconnect);
    const bool wasDisconnected = player->IsDisconnected();
    if (active)
      ++linuxPlayerStatusActiveUpdates;
    if (away)
      ++linuxPlayerStatusAwayUpdates;
    if (playing)
      ++linuxPlayerStatusPlayingUpdates;
    if (disconnected)
      ++linuxPlayerStatusDisconnectedUpdates;
    else if (wasDisconnected)
      ++linuxPlayerStatusReconnectedUpdates;
    if (leaver)
      ++linuxPlayerStatusLeaverUpdates;
    // Null-render bootstrap has no AdventureScreen status callback yet.
    player->SetDisconnected(disconnected, leaver);
    player->SetIsPlaying(playing);
    player->SetIsActive(active);

    linuxLastPlayerStatusPlaying = player->IsPlaying() ? 1 : 0;
    linuxLastPlayerStatusActive = player->IsActive() ? 1 : 0;
    linuxLastPlayerStatusDisconnected = player->IsDisconnected() ? 1 : 0;
    linuxLastPlayerStatusLeaver = player->IsLeaver() ? 1 : 0;
  }
}
void PFWorld::ExecuteCommands(const NCore::TPackedCommands& commands)
{
  SyncFPUStart(nfpu::AT_CMD_EXECUTE);

  for (NCore::TPackedCommands::const_iterator it = commands.begin(); it != commands.end(); ++it)
  {
    if (!IsValid(*it))
      continue;

    const DWORD commandTypeId = (*it)->GetCommandId();
    CObj<NCore::WorldCommand> wcmd = (*it)->GetWorldCommand(GetPointerSerialization());
    if (!wcmd)
      continue;

    RegisterLinuxExecutedPackedWorldCommand(wcmd->GetId(), commandTypeId);
    ExecuteCommand(wcmd.GetPtr());
  }

  SyncFPUEnd(nfpu::AT_CMD_EXECUTE);
}
bool PFWorld::Step(float dtInSeconds, float)
{
  if (dtInSeconds > 0.0f)
    ++step;
  timeElapsed += dtInSeconds;
  if (protection)
    protection->Update();
  if (dayNightController)
    dayNightController->Update(dtInSeconds);
  if (pNatureMap)
    pNatureMap->OnStep(dtInSeconds);
  if (pAIWorld)
  {
    pAIWorld->Update(dtInSeconds);
    if (dtInSeconds > 0.0f)
    {
      vector<CPtr<PFBaseSpawner> > spawners;
      linuxLastSteppedSpawnerObjects = 0;
      linuxLastSteppedCreepSpawnerObjects = 0;
      linuxLastSteppedNeutralCreepSpawnerObjects = 0;
      TObjects& objects = GetObjects();
      for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
      {
        PFBaseSpawner* spawner = dynamic_cast<PFBaseSpawner*>(it->second.GetPtr());
        if (spawner)
        {
          spawners.push_back(spawner);
          ++linuxLastSteppedSpawnerObjects;
          if (dynamic_cast<PFCreepSpawner*>(spawner))
            ++linuxLastSteppedCreepSpawnerObjects;
          if (dynamic_cast<PFNeutralCreepSpawner*>(spawner))
            ++linuxLastSteppedNeutralCreepSpawnerObjects;
        }
      }

      for (vector<CPtr<PFBaseSpawner> >::iterator it = spawners.begin(), end = spawners.end(); it != end; ++it)
      {
        if (IsValid(*it))
          (*it)->StepLinuxBootstrap(dtInSeconds);
      }

      ProcessAddRemove();
    }
    if (pResolver)
    {
      vector<PFBaseMovingUnit*> movingUnits;
      PFBaseMovingUnit::GetAllUnits(pAIWorld, movingUnits, false);
      pResolver->Resolve(movingUnits, dtInSeconds);
    }
    MovingUnit::UpdateMovements(pAIWorld, pResolver, GetTileMap(), dtInSeconds);
  }
  if (dtInSeconds > 0.0f)
  {
    vector<CPtr<PFBaseHero> > deadHeroesWithRespawn;
    TObjects& objects = GetObjects();
    for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
    {
      PFBaseHero* hero = dynamic_cast<PFBaseHero*>(it->second.GetPtr());
      if (hero && hero->IsDead() && hero->GetTimeToRespawn() >= 0.0f)
      {
        deadHeroesWithRespawn.push_back(hero);
      }
    }

    for (vector<CPtr<PFBaseHero> >::iterator it = deadHeroesWithRespawn.begin(),
         end = deadHeroesWithRespawn.end();
         it != end;
         ++it)
    {
      if (IsValid(*it) && (*it)->IsDead() && (*it)->GetTimeToRespawn() >= 0.0f)
      {
        (*it)->Step(dtInSeconds);
      }
    }
  }
  KillDeadUnits(false);
  StepLinuxPFStatistics(pStatistics, dtInSeconds);
  if (triggerMarkerHandler)
    triggerMarkerHandler->Step(dtInSeconds);
  if (pAIContainer)
  {
    pAIContainer->Step(dtInSeconds);
    ++linuxAIStepCalls;
    linuxAIControllerCount = pAIContainer->GetLinuxControllerCount();
  }
  return true;
}
void PFWorld::CalcCRC(IBinSaver&, bool) {}
void PFWorld::StoreDeadUnit(PFBaseUnit* pUnit)
{
  if (!pUnit)
    return;

  for (vector<CObj<PFBaseUnit> >::const_iterator it = deadUnits.begin(), end = deadUnits.end(); it != end; ++it)
  {
    if (it->GetPtr() == pUnit)
      return;
  }

  deadUnits.push_back(CObj<PFBaseUnit>(pUnit));
  ++linuxStoredDeadUnits;
  linuxLastStoredDeadUnitObjectId = pUnit->GetObjectId();
}
void PFWorld::UnregisterCreep(const PFCommonCreep*)
{
  if (totalCreepsCount > 0)
    --totalCreepsCount;
}
void PFWorld::OnGameFinished(NDb::EFaction failedFaction) { defeatedFaction = failedFaction; }
void PFWorld::GameFinish(NDb::EFaction failedFaction) { defeatedFaction = failedFaction; }
bool PFWorld::CanCreateClients() { return IsValid(advMapDescription); }
void PFWorld::StopMovingUnits() {}
void PFWorld::KillDeadUnits(bool fullCleanup)
{
  for (int i = 0; i < deadUnits.size(); ++i)
  {
    if (PFBaseUnit* unit = deadUnits[i])
    {
      unit->CleanupAfterDeath(fullCleanup);
      ++linuxCleanedDeadUnits;
      linuxLastCleanedDeadUnitObjectId = unit->GetObjectId();
    }
  }

  deadUnits.clear();
}
void PFWorld::SyncFPUStart(nfpu::ActionType) {}
void PFWorld::SyncFPUEnd(nfpu::ActionType) {}
void PFWorld::RegisterLinuxExecutedPackedWorldCommand(int clientId)
{
  RegisterLinuxExecutedPackedWorldCommand(clientId, 0);
}
void PFWorld::RegisterLinuxExecutedPackedWorldCommand(int clientId, DWORD commandTypeId)
{
  ++linuxExecutedPackedWorldCommands;
  linuxLastPackedWorldCommandClientId = clientId;
  linuxLastPackedWorldCommandTypeId = commandTypeId;
}
void PFWorld::RegisterLinuxBootstrapRuntimeCommand(int clientId, int token, float value)
{
  ++linuxBootstrapRuntimeCommands;
  linuxLastBootstrapRuntimeCommandClientId = clientId;
  linuxLastBootstrapRuntimeCommandToken = token;
  linuxLastBootstrapRuntimeCommandValue = value;
}
int PFWorld::GetLinuxSpawnedNeutralCreepObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFNeutralCreepSpawner* spawner = dynamic_cast<PFNeutralCreepSpawner*>(it->second.GetPtr());
    if (spawner)
      count += spawner->GetSpawnedCreepsCount();
  }
  return count;
}
int PFWorld::GetLinuxMovingCommonCreepObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCommonCreep* creep = dynamic_cast<PFCommonCreep*>(it->second.GetPtr());
    if (creep && creep->IsMoving())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxMovedCommonCreepObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCommonCreep* creep = dynamic_cast<PFCommonCreep*>(it->second.GetPtr());
    if (creep && creep->GetLinuxDistanceFromInitial() > 0.5f)
      ++count;
  }
  return count;
}
float PFWorld::GetLinuxCommonCreepMovementDistance()
{
  float distance = 0.0f;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCommonCreep* creep = dynamic_cast<PFCommonCreep*>(it->second.GetPtr());
    if (creep)
      distance += creep->GetLinuxDistanceFromInitial();
  }
  return distance;
}

void PFWorld::GetLinuxDynamicWorldMarkers(vector<LinuxDynamicWorldMarker>& markers, int maxMarkers)
{
  markers.clear();
  if (maxMarkers <= 0)
    return;

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    if (static_cast<int>(markers.size()) >= maxMarkers)
      break;

    PFBaseUnit* unit = dynamic_cast<PFBaseUnit*>(it->second.GetPtr());
    if (!unit)
      continue;

    LinuxDynamicWorldMarker marker;
    marker.x = unit->GetPosition().x;
    marker.y = unit->GetPosition().y;
    marker.objectId = unit->GetObjectId();
    marker.faction = static_cast<int>(unit->GetFaction());
    marker.healthPercent = unit->GetHealthPercent();
    marker.energyPercent = unit->GetManaPercent();
    marker.objectSize = unit->GetObjectSize();
    marker.moving = false;
    if (PFBaseMovingUnit* movingUnit = dynamic_cast<PFBaseMovingUnit*>(unit))
    {
      marker.moving = movingUnit->IsMoving();
      const CVec2 moveDir = movingUnit->GetMoveDirection();
      if (moveDir.x * moveDir.x + moveDir.y * moveDir.y > 0.0001f)
      {
        marker.moveDirX = moveDir.x;
        marker.moveDirY = moveDir.y;
        marker.hasMoveDirection = true;
      }
    }
    marker.dead = unit->IsDead();

    const NDb::Unit* unitDesc = unit->DbUnitDesc();
    if (unitDesc)
    {
      marker.unitDbid = unitDesc->GetDBID().GetFormatted();
      if (unitDesc->sceneObject)
      {
        marker.sceneObjectDbid = unitDesc->sceneObject->GetDBID().GetFormatted();
      }
      if (const NDb::AdvMapCreep* creepDesc = dynamic_cast<const NDb::AdvMapCreep*>(unitDesc))
      {
        marker.creepType = static_cast<int>(creepDesc->creepType);
      }
    }

    if (PFBaseHero* hero = dynamic_cast<PFBaseHero*>(unit))
    {
      marker.kind = LinuxDynamicWorldMarker::KIND_HERO;
      if (IsValid(hero->GetPlayer()))
      {
        marker.playerId = hero->GetPlayer()->GetPlayerID();
        marker.userId = hero->GetPlayer()->GetUserID();
      }
    }
    else if (dynamic_cast<PFCommonCreep*>(unit))
      marker.kind = LinuxDynamicWorldMarker::KIND_COMMON_CREEP;
    else if (dynamic_cast<PFNeutralCreep*>(unit))
      marker.kind = LinuxDynamicWorldMarker::KIND_NEUTRAL_CREEP;
    else
      continue;

    markers.push_back(marker);
  }
}

PFBaseUnit* PFWorld::FindLinuxUnitByObjectId(int objectId)
{
  if (objectId < 0)
    return 0;

  TObjects& objects = GetObjects();
  TObjects::iterator direct = objects.find(objectId);
  if (direct != objects.end())
  {
    PFBaseUnit* unit = dynamic_cast<PFBaseUnit*>(direct->second.GetPtr());
    if (unit)
      return unit;
  }

  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFBaseUnit* unit = dynamic_cast<PFBaseUnit*>(it->second.GetPtr());
    if (unit && unit->GetObjectId() == objectId)
      return unit;
  }

  return 0;
}
int PFWorld::GetLinuxCreepSpawnerWavesCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCreepSpawner* spawner = dynamic_cast<PFCreepSpawner*>(it->second.GetPtr());
    if (spawner)
      count += spawner->GetLastWave();
  }
  return count;
}
int PFWorld::GetLinuxNeutralCreepSpawnerWavesCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFNeutralCreepSpawner* spawner = dynamic_cast<PFNeutralCreepSpawner*>(it->second.GetPtr());
    if (spawner)
      count += spawner->GetLastWave();
  }
  return count;
}
int PFWorld::GetLinuxReadyCreepSpawnerObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCreepSpawner* spawner = dynamic_cast<PFCreepSpawner*>(it->second.GetPtr());
    if (spawner && spawner->CanSpawnLinuxBootstrapWave())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxReadyNeutralCreepSpawnerObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFNeutralCreepSpawner* spawner = dynamic_cast<PFNeutralCreepSpawner*>(it->second.GetPtr());
    if (spawner && spawner->CanSpawnLinuxBootstrapWave())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxEnabledCreepSpawnerObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCreepSpawner* spawner = dynamic_cast<PFCreepSpawner*>(it->second.GetPtr());
    if (spawner && spawner->IsLinuxBootstrapEnabled())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxEnabledNeutralCreepSpawnerObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFNeutralCreepSpawner* spawner = dynamic_cast<PFNeutralCreepSpawner*>(it->second.GetPtr());
    if (spawner && spawner->IsLinuxBootstrapEnabled())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxContentCreepSpawnerObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCreepSpawner* spawner = dynamic_cast<PFCreepSpawner*>(it->second.GetPtr());
    if (!spawner)
      continue;
    const NDb::AdvMapCreepSpawner* desc = dynamic_cast<const NDb::AdvMapCreepSpawner*>(spawner->GetDBDesc());
    if (desc && !desc->creeps.empty())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxContentNeutralCreepSpawnerObjectsCount()
{
  int count = 0;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFNeutralCreepSpawner* spawner = dynamic_cast<PFNeutralCreepSpawner*>(it->second.GetPtr());
    if (!spawner)
      continue;
    const NDb::AdvMapNeutralCreepSpawner* desc = dynamic_cast<const NDb::AdvMapNeutralCreepSpawner*>(spawner->GetDBDesc());
    if (desc && !desc->groups.empty())
      ++count;
  }
  return count;
}
int PFWorld::GetLinuxAICreepSpawnEnabled() const
{
  return pAIWorld && pAIWorld->GetSpawnCreeps() ? 1 : 0;
}
int PFWorld::GetLinuxAINeutralCreepSpawnEnabled() const
{
  return pAIWorld && pAIWorld->GetSpawnNeutralCreeps() ? 1 : 0;
}
int PFWorld::GetLinuxAIMaxCreepsCount() const
{
  return pAIWorld ? pAIWorld->GetAIParameters().maxCreepsCount : 0;
}
float PFWorld::GetLinuxMinCreepSpawnerSpawnDelay()
{
  bool found = false;
  float minDelay = 0.0f;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFCreepSpawner* spawner = dynamic_cast<PFCreepSpawner*>(it->second.GetPtr());
    if (!spawner)
      continue;
    float delay = spawner->GetLinuxSpawnDelay();
    if (!found || delay < minDelay)
      minDelay = delay;
    found = true;
  }
  return found ? minDelay : 0.0f;
}
float PFWorld::GetLinuxMinNeutralCreepSpawnerSpawnDelay()
{
  bool found = false;
  float minDelay = 0.0f;
  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFNeutralCreepSpawner* spawner = dynamic_cast<PFNeutralCreepSpawner*>(it->second.GetPtr());
    if (!spawner)
      continue;
    float delay = spawner->GetLinuxSpawnDelay();
    if (!found || delay < minDelay)
      minDelay = delay;
    found = true;
  }
  return found ? minDelay : 0.0f;
}
PFShop* PFWorld::FindLinuxFirstShopForHero(PFBaseHero const* hero, int* outConsumableIndex)
{
  if (outConsumableIndex)
  {
    *outConsumableIndex = -1;
  }

  if (!hero)
  {
    return 0;
  }

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFShop* shop = dynamic_cast<PFShop*>(it->second.GetPtr());
    if (!shop)
    {
      continue;
    }

    const int consumables = shop->GetNumConsumables();
    for (int index = 0; index < consumables; ++index)
    {
      if (shop->CanBuyConsumable(hero, index))
      {
        if (outConsumableIndex)
        {
          *outConsumableIndex = index;
        }
        return shop;
      }
    }
  }

  return 0;
}
PFBaseUnit* PFWorld::FindLinuxFirstUsableUnitForHero(PFBaseHero const* hero)
{
  if (!hero)
  {
    return 0;
  }

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFBaseUnit* unit = dynamic_cast<PFBaseUnit*>(it->second.GetPtr());
    if (unit && unit != hero && !unit->IsDead() && unit->CanBeUsedBy(hero))
    {
      return unit;
    }
  }

  return 0;
}
PFFlagpole* PFWorld::FindLinuxFirstRaisableFlagpoleForHero(PFBaseHero const* hero)
{
  if (!hero)
  {
    return 0;
  }

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFFlagpole* flagpole = dynamic_cast<PFFlagpole*>(it->second.GetPtr());
    if (flagpole && flagpole->CanRaise(hero->GetFaction()))
    {
      return flagpole;
    }
  }

  return 0;
}
PFMinigamePlace* PFWorld::FindLinuxFirstAvailableMinigamePlaceForHero(PFBaseHero const* hero)
{
  if (!hero)
  {
    return 0;
  }

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFMinigamePlace* minigamePlace = dynamic_cast<PFMinigamePlace*>(it->second.GetPtr());
    if (minigamePlace &&
        minigamePlace->IsAvailable() &&
        minigamePlace->CanBeUsedBy(hero) &&
        minigamePlace->GetFaction() == hero->GetFaction())
    {
      return minigamePlace;
    }
  }

  return 0;
}
PFMinigamePlace* PFWorld::FindLinuxFirstForeignMinigamePlaceForHero(PFBaseHero const* hero)
{
  if (!hero)
  {
    return 0;
  }

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFMinigamePlace* minigamePlace = dynamic_cast<PFMinigamePlace*>(it->second.GetPtr());
    if (minigamePlace &&
        minigamePlace->IsAvailable() &&
        minigamePlace->CanBeUsedBy(hero) &&
        minigamePlace->GetFaction() != hero->GetFaction())
    {
      return minigamePlace;
    }
  }

  return 0;
}
PFPickupableObjectBase* PFWorld::FindLinuxFirstPickupableForHero(PFBaseHero const* hero)
{
  if (!hero)
  {
    return 0;
  }

  TObjects& objects = GetObjects();
  for (TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    PFPickupableObjectBase* pickupable = dynamic_cast<PFPickupableObjectBase*>(it->second.GetPtr());
    if (pickupable && pickupable->CanBePickedUpBy(hero))
    {
      return pickupable;
    }
  }

  return 0;
}
void PFWorld::AddAI(PFBaseHero* hero, int line)
{
  ++linuxAIAddRequests;
  linuxAILastHeroObjectId = hero ? hero->GetObjectId() : -1;
  linuxAILastPlayerId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetPlayerID() : -1;
  linuxAILastUserId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetUserID() : -1;
  linuxAILastLine = line;

  const NDb::BotsSettings* botsSettings = GetBotsSettings();
  linuxAIBotsSettingsAvailable = botsSettings ? 1 : 0;
  linuxAIBotsEnabled = botsSettings ? (botsSettings->enableBotsAI ? 1 : 0) : -1;

  if (!hero)
    return;

  if (!pAIContainer)
    pAIContainer = new PFAIContainer(this, 0);

  const int controllersBefore = pAIContainer->GetLinuxControllerCount();
  IPFAIController* controller = pAIContainer->Add(hero, line);
  linuxAIControllerCount = pAIContainer->GetLinuxControllerCount();
  if (controller && linuxAIControllerCount > controllersBefore)
    ++linuxAIAddSuccesses;
}

void PFWorld::RemoveAI(PFBaseHero* hero)
{
  ++linuxAIRemoveRequests;
  linuxAILastHeroObjectId = hero ? hero->GetObjectId() : -1;
  linuxAILastPlayerId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetPlayerID() : -1;
  linuxAILastUserId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetUserID() : -1;
  if (pAIContainer && pAIContainer->Remove(hero))
    ++linuxAIRemoveSuccesses;
  linuxAIControllerCount = pAIContainer ? pAIContainer->GetLinuxControllerCount() : 0;
}

void PFWorld::RecordLinuxAICommand(LinuxAICommandKind kind, const PFBaseHero* hero, const PFLogicObject* target, bool sent)
{
  ++linuxAICommandAttempts;
  linuxAILastCommandKind = static_cast<int>(kind);
  linuxAILastCommandHeroObjectId = hero ? hero->GetObjectId() : -1;
  linuxAILastCommandPlayerId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetPlayerID() : -1;
  linuxAILastCommandUserId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetUserID() : -1;
  linuxAILastCommandTargetObjectId = target ? target->GetObjectId() : -1;
  linuxAILastCommandSent = sent ? 1 : 0;

  if (!sent)
    return;

  ++linuxAICommandsSent;
  switch (kind)
  {
  case LinuxAICommandMove:
    ++linuxAICommandMoveSent;
    break;
  case LinuxAICommandCombatMove:
    ++linuxAICommandCombatMoveSent;
    break;
  case LinuxAICommandAttack:
    ++linuxAICommandAttackSent;
    break;
  default:
    ++linuxAICommandOtherSent;
    break;
  }
}

void PFWorld::RecordLinuxAICommandDirectFallback(LinuxAICommandKind kind, const PFBaseHero* hero, const PFLogicObject* target)
{
  ++linuxAICommandDirectFallbacks;
  linuxAILastCommandKind = static_cast<int>(kind);
  linuxAILastCommandHeroObjectId = hero ? hero->GetObjectId() : -1;
  linuxAILastCommandPlayerId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetPlayerID() : -1;
  linuxAILastCommandUserId = hero && hero->GetPlayer() ? hero->GetPlayer()->GetUserID() : -1;
  linuxAILastCommandTargetObjectId = target ? target->GetObjectId() : -1;
  linuxAILastCommandSent = 0;
}

const NDb::BotsSettings* PFWorld::GetBotsSettings() const
{
  const NDb::AdvMapSettings* desc = 0;
  if (IsValid(advMapDescription))
  {
    if (IsValid(advMapDescription->mapSettings))
      desc = advMapDescription->mapSettings;
    else if (IsValid(advMapDescription->map) && IsValid(advMapDescription->map->mapSettings))
      desc = advMapDescription->map->mapSettings;
  }

  if (desc && IsValid(desc->overrideBotsSettings))
    return desc->overrideBotsSettings;

  if (pAIWorld && IsValid(pAIWorld->GetAIParameters().botsSettings))
    return pAIWorld->GetAIParameters().botsSettings;

  return 0;
}
void PFWorld::NotifyTalentCastProcessed(const NWorld::PFTalent*) {}
void PFWorld::NotifyConsumableProcessed(const NWorld::PFConsumableAbilityData*) {}
void PFWorld::NotifyCreepSpawnerCleaned(const NWorld::PFNeutralCreepSpawner*, const NWorld::PFBaseUnit*) {}
bool PFWorld::IsFactionDefeated(const NDb::EFaction& faction) { return defeatedFaction == faction; }
void PFWorld::SetDefeatedFaction(const NDb::EFaction& faction) { defeatedFaction = faction; }
bool PFWorld::PollProtectionResult(NCore::ProtectionResult& result) { return protection ? protection->PopResult(result) : false; }
void PFWorld::SetProtectionUpdateFrequency(const int offset, const int frequency) { if (protection) protection->SetUpdateFrequency(offset, frequency); }
bool PFWorld::IsDay() const { return dayNightController ? dayNightController->IsDay() : true; }
bool PFWorld::IsNight() const { return dayNightController ? dayNightController->IsNight() : false; }
void PFWorld::LockOutsideCameraArea(const NDb::AdventureCameraSettings*) {}
void PFWorld::InitMinigames() {}
void PFWorld::LoadPrecachedResources(const NDb::AdvMapDescription*) {}

namespace
{
template<class TObject>
int LoadLinuxMapObjectsOfType(
  NWorld::PFWorld* world,
  const vector<NDb::AdvMapObject>& objects,
  int typeId,
  int& totalLoaded,
  LoadingProgress* progress)
{
  int loadedCount = 0;
  for (vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    if (!IsValid(it->gameObject) || it->gameObject->GetObjectTypeID() != (DWORD)typeId)
      continue;

    ++loadedCount;
    PF_Core::WorldObjectBase* object = new TObject(world, *it);
    object->SetMapObject(true);
    if (world->GetAIContainer())
      world->GetAIContainer()->RegisterObject(object, it->scriptName, it->scriptGroupName);

    if (progress && !objects.empty())
      progress->SetPartialProgress(EMapLoadStages::MapObjects, (++totalLoaded) / (float)objects.size());
  }
  return loadedCount;
}

const NDb::Hero* FindLinuxHeroByRuntimeId(const NDb::AdvMapDescription* advMapDesc, uint heroId)
{
  NDb::Ptr<NDb::SessionRoot> root = NDb::SessionRoot::GetRoot();
  if (!IsValid(root) || !IsValid(root->logicRoot) || !IsValid(root->logicRoot->heroes))
    return 0;

  const NDb::HeroesDB* heroesDb = root->logicRoot->heroes.GetPtr();
  for (int i = 0; i < heroesDb->heroes.size(); ++i)
  {
    const NDb::Hero* hero = heroesDb->heroes[i].GetPtr();
    if (hero && Crc32Checksum().AddString(hero->id.c_str()).Get() == heroId)
      return hero;
  }

  (void)advMapDesc;
  return 0;
}
}

bool PFWorld::LoadSceneMapObjects(const NDb::AdvMapDescription* advMapDesc, const NCore::TPlayersStartInfo& playersInfo, const bool isTutorial, LoadingProgress* progress, const NWorld::PFResourcesCollection::TalentMap& talents)
{
  NI_VERIFY(advMapDesc && IsValid(advMapDesc->map), "Invalid advMap resource!", return false);

  const vector<NDb::AdvMapObject>& objects = advMapDesc->map->objects;
  int objectsLoaded = 0;

  linuxLoadedWarFogUnblockObjects = LoadLinuxMapObjectsOfType<PFWarFogUnblock>(this, objects, NDb::WarFogUnblock::typeId, objectsLoaded, progress);
  linuxLoadedSimpleObjects = LoadLinuxMapObjectsOfType<PFSimpleObject>(this, objects, NDb::SimpleObject::typeId, objectsLoaded, progress);
  linuxLoadedSimpleObjects += LoadLinuxMapObjectsOfType<PFSimpleObject>(this, objects, NDb::GameObject::typeId, objectsLoaded, progress);
  linuxLoadedSimpleObjects += LoadLinuxMapObjectsOfType<PFSimpleObject>(this, objects, NDb::Road::typeId, objectsLoaded, progress);
  linuxLoadedMultiStateObjects = LoadLinuxMapObjectsOfType<PFMultiStateObject>(this, objects, NDb::MultiStateObject::typeId, objectsLoaded, progress);
  linuxLoadedTreeObjects = LoadLinuxMapObjectsOfType<PFTree>(this, objects, NDb::TreeObject::typeId, objectsLoaded, progress);
  linuxLoadedGlyphSpawnerObjects = LoadLinuxMapObjectsOfType<PFGlyphSpawner>(this, objects, NDb::GlyphSpawner::typeId, objectsLoaded, progress);
  linuxLoadedAdvMapObstacleObjects = LoadLinuxMapObjectsOfType<PFAdvMapObstacle>(this, objects, NDb::AdvMapObstacle::typeId, objectsLoaded, progress);
  linuxLoadedHeroPlaceHolderObjects = 0;
  linuxSpawnedHeroObjects = 0;
  linuxPlayersWithHeroObjects = 0;
  vector<Placement> linuxHeroSpawns[NCore::ETeam::COUNT];
  for (vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    if (!IsValid(it->gameObject) || it->gameObject->GetObjectTypeID() != (DWORD)NDb::HeroPlaceHolder::typeId)
      continue;

    ++linuxLoadedHeroPlaceHolderObjects;
    const NDb::HeroPlaceHolder* placeholder = dynamic_cast<const NDb::HeroPlaceHolder*>(it->gameObject.GetPtr());
    const int teamIndex = placeholder ? static_cast<int>(placeholder->teamId) : -1;
    if (teamIndex >= 0 && teamIndex < NCore::ETeam::COUNT)
      linuxHeroSpawns[teamIndex].push_back(it->offset.GetPlace());

    if (progress && !objects.empty())
      progress->SetPartialProgress(EMapLoadStages::MapObjects, (++objectsLoaded) / (float)objects.size());
  }
  int nextHeroSpawn[NCore::ETeam::COUNT] = { 0, 0 };
  int inTeamHeroId[NCore::ETeam::COUNT] = { 1, 1 };
  for (NCore::TPlayersStartInfo::const_iterator it = playersInfo.begin(), end = playersInfo.end(); it != end; ++it)
  {
    if (it->playerType == NCore::EPlayerType::Invalid ||
        it->teamID == NCore::ETeam::None ||
        it->playerInfo.heroId == 0)
      continue;

    const int teamIndex = static_cast<int>(it->teamID);
    if (teamIndex < 0 || teamIndex >= NCore::ETeam::COUNT)
      continue;

    if (nextHeroSpawn[teamIndex] >= linuxHeroSpawns[teamIndex].size())
      continue;

    const NDb::Hero* hero = FindLinuxHeroByRuntimeId(advMapDesc, it->playerInfo.heroId);
    if (!hero)
      continue;

    PFBaseHero::SpawnInfo spawnInfo;
    spawnInfo.playerId = it->playerID;
    spawnInfo.inTeamId = inTeamHeroId[teamIndex]++;
    spawnInfo.placement = linuxHeroSpawns[teamIndex][nextHeroSpawn[teamIndex]++];
    spawnInfo.pHero = hero;
    spawnInfo.playerInfo = it->playerInfo;
    spawnInfo.usePlayerInfoTalentSet = it->usePlayerInfoTalentSet;
    spawnInfo.bInitInventory = !isTutorial;

    PFBaseHero* heroObject = CreateHero(this, spawnInfo);
    if (heroObject)
      ++linuxSpawnedHeroObjects;

    if (progress && playersInfo.size() > 0)
      progress->SetPartialProgress(EMapLoadStages::Heroes, linuxSpawnedHeroObjects / (float)playersInfo.size());
  }
  for (int playerIndex = 0, playerCount = GetPlayersCount(); playerIndex < playerCount; ++playerIndex)
  {
    PFPlayer* player = GetPlayer(playerIndex);
    if (player && player->GetHero())
      ++linuxPlayersWithHeroObjects;
  }
  (void)talents;
  linuxLoadedCreepSpawnerObjects = LoadLinuxMapObjectsOfType<PFCreepSpawner>(this, objects, NDb::AdvMapCreepSpawner::typeId, objectsLoaded, progress);
  linuxLoadedNeutralCreepSpawnerObjects = LoadLinuxMapObjectsOfType<PFNeutralCreepSpawner>(this, objects, NDb::AdvMapNeutralCreepSpawner::typeId, objectsLoaded, progress);
  linuxLoadedUsableBuildingObjects = LoadLinuxMapObjectsOfType<PFUsableBuilding>(this, objects, NDb::UsableBuilding::typeId, objectsLoaded, progress);
  linuxLoadedSimpleBuildingObjects = LoadLinuxMapObjectsOfType<PFSimpleBuilding>(this, objects, NDb::Building::typeId, objectsLoaded, progress);
  linuxLoadedShopObjects = LoadLinuxMapObjectsOfType<PFShop>(this, objects, NDb::Shop::typeId, objectsLoaded, progress);
  linuxLoadedQuarterObjects = LoadLinuxMapObjectsOfType<PFQuarters>(this, objects, NDb::Quarter::typeId, objectsLoaded, progress);
  linuxLoadedTowerObjects = LoadLinuxMapObjectsOfType<PFTower>(this, objects, NDb::Tower::typeId, objectsLoaded, progress);
  linuxLoadedControllableTowerObjects = LoadLinuxMapObjectsOfType<PFControllableTower>(this, objects, NDb::ControllableTower::typeId, objectsLoaded, progress);
  linuxLoadedFountainObjects = LoadLinuxMapObjectsOfType<PFFountain>(this, objects, NDb::Fountain::typeId, objectsLoaded, progress);
  linuxLoadedRoadFlagpoleObjects = LoadLinuxMapObjectsOfType<PFRoadFlagpole>(this, objects, NDb::Flagpole::typeId, objectsLoaded, progress);
  linuxLoadedScriptedFlagpoleObjects = LoadLinuxMapObjectsOfType<PFScriptedFlagpole>(this, objects, NDb::ScriptedFlagpole::typeId, objectsLoaded, progress);
  linuxLoadedMainBuildingObjects = 0;
  for (vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    if (!IsValid(it->gameObject) || it->gameObject->GetObjectTypeID() != (DWORD)NDb::MainBuilding::typeId)
      continue;

    ++linuxLoadedMainBuildingObjects;
    PFMainBuilding* const mb = new PFMainBuilding(this, *it);
    mb->SetMapObject(true);
    mainBuildings.push_back(mb);
    if (GetAIContainer())
      GetAIContainer()->RegisterObject(mb, it->scriptName, it->scriptGroupName);

    if (progress && !objects.empty())
      progress->SetPartialProgress(EMapLoadStages::MapObjects, (++objectsLoaded) / (float)objects.size());
  }

  linuxLoadedMinigamePlaceObjects = LoadLinuxMapObjectsOfType<PFMinigamePlace>(this, objects, NDb::MinigamePlace::typeId, objectsLoaded, progress);
  linuxLoadedCameraSplineObjects = 0;
  linuxLoadedScriptPathObjects = 0;
  linuxLoadedScriptPolygonAreaObjects = 0;
  for (vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    if (!IsValid(it->gameObject))
      continue;

    const DWORD typeId = it->gameObject->GetObjectTypeID();
    if (typeId == (DWORD)NDb::AdvMapCameraSpline::typeId)
    {
      ++linuxLoadedCameraSplineObjects;
      if (GetAIContainer())
        GetAIContainer()->RegisterCameraSpline(it->gameObject->GetDBID(), it->offset.GetPlace());
    }
    else if (typeId == (DWORD)NDb::ScriptPath::typeId)
    {
      ++linuxLoadedScriptPathObjects;
      if (GetAIContainer())
        GetAIContainer()->RegisterScriptPath(it->scriptName, dynamic_cast<const NDb::ScriptPath*>(it->gameObject.GetPtr()));
    }
    else if (typeId == (DWORD)NDb::ScriptPolygonArea::typeId)
    {
      ++linuxLoadedScriptPolygonAreaObjects;
      if (GetAIContainer())
        GetAIContainer()->RegisterPolygonArea(it->scriptName, dynamic_cast<const NDb::ScriptPolygonArea*>(it->gameObject.GetPtr()));
    }
    else
    {
      continue;
    }

    if (progress && !objects.empty())
      progress->SetPartialProgress(EMapLoadStages::MapObjects, (++objectsLoaded) / (float)objects.size());
  }

  if (progress)
  {
    progress->SetPartialProgress(EMapLoadStages::MapObjects, 1.0f);
    progress->SetPartialProgress(EMapLoadStages::Heroes, 1.0f);
  }
  return true;
}
bool PFWorld::CanTrackPlayersBehaviour(const NCore::MapStartInfo&) const { return false; }

void PFWorld::NotifyItemTransferForLinuxBootstrap(PFBaseHero* from, PFBaseHero* to, const NDb::Consumable* dbItem)
{
  NotifyLinuxItemTransfer(pStatistics, from, to, dbItem);
}

} // namespace NWorld
#else
#include "PFWorld.h"
#include "PFPlayer.h"
#include "PFAdvMap.h"
#include "DBAdvMap.h"
#include "TileMap.h"
#include "WarFog.h"
#include "TriggerMarkerHandler.h"
#include "PFWorldNatureMap.h"
#include "PFAIWorld.h"
#include "PFAIContainer.h"
#include "PFStatistics.h"
#include "PFDispatchStrike1.h"
#ifndef VISUAL_CUTTED
#include "PFPureClientCritter.h"
#endif
#include "../System/SyncProcessorState.h"
#include "../System/InlineProfiler.h"
#include "PFBaseMovingUnit.h"
#include "PFBuildings.h"        // for AddMapObject() - building detection
#include "PFLogicDebug.h"
#include "PFGameLogicDebugVisual.h"
#include "DBTalent.h"
#include "DBSessionRoots.h"

#include "Core/WorldCommand.h"
#include "Core/Transceiver.h"
#include "Core/CoreFSM.h"

#include "PF_Minigames/MinigameSessionInterface.h"

#include "IAdventureScreen.h"

#include "PFBuildings.h"
#include "PFChest.h" 
#include "PFCommonCreep.h"
#include "PFRoadFlagpole.h"
#include "PFScriptedFlagpole.h"
#include "PFGlyph.h"
#include "PFMainBuilding.h"
#include "PFNeutralCreep.h"
#include "PFSimpleObject.h"
#include "PFTower.h"
#include "PFTree.h"
#include "PFMinigamePlace.h"
#include "../Scene/VertexColorManager.h"
#include "PFMaleHero.h"
#include "PFTalent.h"

#include "HeroSpawn.h"
#include "../System/Win32Random.h"
#include "../System/LoadingProgress.h"

#include "PF_Core/EffectsPool.h"

#include "LuaScript.h"
#include "Scripts/FuncCallMacroses.h"
#include "Render/DxResourcesControl.h"

//pathfinding

#include "CommonPathFinder.h"
#include "RoutePathFinder.h"
#include "CollisionResolver.h"

//#include "../Render/debugrenderer.h"
//#include "../Render/renderresourcemanager.h"

#include "PFDebug.h"
#include "PFLogicConst.h"

#if defined(PW_LINUX_DB_BOOTSTRAP) || !defined(VISUAL_CUTTED)
#include "../Client/MainTimer.h"
#endif

#if defined(PW_LINUX_DB_BOOTSTRAP)
namespace Peered
{
  enum Status
  {
    Connecting                          = 0,
    Ready                               = 1,
    Active                              = 2,
    Away                                = 3,
    DisconnectedByClient                = 4,
    DisconnectedByServer                = 5,
    ConnectionTimedOut                  = 6,
    DisconnectedByCheatAttempt          = 7,
    DisconnectedByClientIntentionally   = 8,
    ConnectionTimedOutOnReconnect       = 9,
    DisconnectedByAsync                 = 10,
    RefusedToReconnect                  = 11,
  };

  inline bool IsDisconnectedStatus( int status )
  {
    switch ( status )
    {
    case DisconnectedByClient:
    case DisconnectedByClientIntentionally:
    case DisconnectedByServer:
    case DisconnectedByCheatAttempt:
    case ConnectionTimedOut:
    case ConnectionTimedOutOnReconnect:
    case DisconnectedByAsync:
    case RefusedToReconnect:
      return true;
    default:
      return false;
    }
  }

  inline bool IsPlayingStatus( int status )
  {
    return status == Active || status == Away;
  }
}
#else
#include "HybridServer/Peered.h"
#endif

#include "MapLoadingUtility.hpp"

#include "PlayerBehaviourTracking.h"
#include "PFWorldProtection.h"
#include "DayNightController.h"
#include "../Terrain/GridConstants.h"

#ifdef _SHIPPING
#include "PFClientVisibilityMap.h"
#endif
#include <curl/curl.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NI_DEFINE_REFCOUNT( NGameX::IAdventureScreen );

static NDebug::DebugVar<int> mainPerf_PFWorld_StepId              ( "StepId", "MainPerf" );

static NDebug::DebugVar<int> time_syncTime("Transciever|SyncTime", "" );
static NDebug::DebugVar<int> time_localTime("Transciever|LocalTime", "" );

namespace 
{
  static float g_noTrees = 0.0f;
  REGISTER_DEV_VAR( "no_trees", g_noTrees, STORAGE_NONE );

  DEV_VAR_STATIC bool g_enableBehaviourTracking = true;
  REGISTER_DEV_VAR("enable_behaviour_tracking", g_enableBehaviourTracking, STORAGE_NONE);
}

REGISTER_SAVELOAD_CLASS_NM(PFWorld, NWorld);

namespace NWorld
{
int PFWorld::instanceCount = 0;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<bool checkTrees> class ObjectsLoaderBase : public NonCopyable
{
  virtual PF_Core::WorldObjectBase* CreateObject(const NDb::AdvMapObject &_obj) = 0;

protected:
  PFWorld* const pWorld;
  PFAIContainer* const pAIContainer;
  NScene::IScene* const pScene;

  ObjectsLoaderBase(PFWorld* _pWorld) : pScene(_pWorld->GetScene()), pWorld(_pWorld), pAIContainer(_pWorld->GetAIContainer()) {}

public:
  bool Load(const vector<NDb::AdvMapObject> &objects, int typeId, const char* typeName, int & totalLoaded, LoadingProgress * progress)
  {
    NI_PROFILE_FUNCTION_MEM;
    NHPTimer::STime time;
    NHPTimer::GetTime( time );
    int objCount = 0;

    NScene::MeshVertexColorsManager* const pVCM = pScene->GetMeshVertexColorsManager();

    bool bLoaded = false;
    int idx = 0;
    for ( vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it, ++idx )
    {
      NI_DATA_VERIFY( IsValid( it->gameObject ), NStr::StrFmt("Invalid gameObject assigned to advmap object #%d at %2.3f � %2.3f", it - objects.begin(), it->offset.GetPlace().pos.x, it->offset.GetPlace().pos.y ), continue; );

      if(it->gameObject->GetObjectTypeID() == (DWORD)typeId )
      {
        bLoaded = true;
        if(pVCM)
          pVCM->AdvMapObjectVCBegin(idx);

        if( !checkTrees || g_noTrees <= NWin32Random::Random(0.01f, 1.0f - 0.01f) )
        {
          PF_Core::WorldObjectBase* pObject = CreateObject(*it);
          pObject->SetMapObject(true);
          pAIContainer->RegisterObject(pObject, it->scriptName, it->scriptGroupName);
          ++objCount;
        }

        if(pVCM)
          pVCM->AdvMapObjectVCEnd(idx);

        if ( progress )
          progress->SetPartialProgress( EMapLoadStages::MapObjects, ( ++totalLoaded ) / (float)objects.size() );
      }
    }

    DebugTrace( "%s: %2.3f", typeName, NHPTimer::GetTimePassedAndUpdateTime( time ) );

    return bLoaded;
  }
};

//=================================================================================================================
template<class T, bool checkTrees = false> class ObjectsLoader : public ObjectsLoaderBase<checkTrees>
{
  virtual PF_Core::WorldObjectBase* CreateObject(const NDb::AdvMapObject &_obj) { return new T(this->pWorld, _obj); }

public:
  ObjectsLoader(PFWorld* _pWorld) : ObjectsLoaderBase<checkTrees>(_pWorld) {}
};

template<class T>
bool LoadPureClientObjectsOfType( NGameX::IAdventureScreen* pScreen, NScene::IScene* pScene,
                                  const vector<NDb::AdvMapObject>& objects, int typeId, const char* typeName, int& totalLoaded,
                                  LoadingProgress* progress )
{
  NI_PROFILE_FUNCTION_MEM;
  NHPTimer::STime time;
  NHPTimer::GetTime( time );

  bool bLoaded = false;
  {
    int idx = 0;
    pScreen->ReservePureClientObjects( objects.size() );
    for ( vector<NDb::AdvMapObject>::const_iterator it = objects.begin(); it != objects.end(); it++, idx++ )
    {
      NI_DATA_VERIFY( IsValid( it->gameObject ),
                      NStr::StrFmt( "Invalid gameObject assigned to advmap object #%d at %2.3f � %2.3f",
                                    it - objects.begin(), it->offset.GetPlace().pos.x, it->offset.GetPlace().pos.y ), continue );

      if ( it->gameObject->GetObjectTypeID() != (DWORD)typeId )
      {
        continue;
      }

      if ( pScene->GetMeshVertexColorsManager() )
      {
        pScene->GetMeshVertexColorsManager()->AdvMapObjectVCBegin( idx );
      }
      bLoaded = true;

      if ( pScene->GetMeshVertexColorsManager() )
      {
        pScene->GetMeshVertexColorsManager()->AdvMapObjectVCEnd( idx );
      }

      NGameX::PFPureClientObject* pObject = new T( *it, pScene );
      pScreen->PushPureClientObject( pObject );

      if ( progress )
      {
        totalLoaded++;
        progress->SetPartialProgress( EMapLoadStages::MapObjects, 1.0f * totalLoaded / objects.size() );
      }
    }
  }

  DebugTrace( "%s: %2.3f", typeName, NHPTimer::GetTimePassedAndUpdateTime( time ) );

  return bLoaded;
}


#pragma warning( push )
#pragma warning( disable: 4355 ) //'this' : used in base member initializer list

PFWorld::PFWorld() :
timeElapsed(0.f), 
manualGameFinish(false), 
humanPlayersCount(0),
worldChecker(this),
ambienceMap(NULL), 
stepLength(DEFAULT_GAME_STEP_LENGTH),
protection(),
dayNightController(),
timeScale(NMainLoop::GetTimeScale())
{
}



PFWorld::PFWorld(const NCore::MapStartInfo & info,
                 NScene::IScene * _scene,
                 NGameX::IAdventureScreen * _screen,
                 PFResourcesCollection* _collection,
                 int _stepLength,
                 bool _aiForLeaversEnabled,
                 int _aiForLeaversThreshold) :
step( -1 ),
timeElapsed( 0.f ),
pScene( _scene ),
adventureScreen( _screen ),
manualGameFinish(false),
humanPlayersCount(0),
worldChecker(this), 
ambienceMap(NULL),
resourcesCollection(_collection),
totalCreepsCount(0),
allScriptFunctionsEnabled(false),
stepLength(_stepLength),
stepLengthInSeconds(stepLength/1000.0f),
defeatedFaction(NGameX::FACTION_UNKNOWN),
protection(),
dayNightController(),
timeScale(NMainLoop::GetTimeScale())
{
  NI_PROFILE_FUNCTION

  pTileMap        = new TileMap( this );
  pResolver       = new CollisionResolver( this );
  pAIWorld        = new PFAIWorld( this );
  pNatureMap      = new PFWorldNatureMap(this);
  pStatistics     = new PFStatistics( this );

  dayNightController = new DayNightController(this);

  minigamesMain = NULL;
  //pStatistics->ForAll(StatTrav());

  randGen.SetSeed( info.randomSeed );

  // Map description should already be loaded before< so we just get it from db cache here
  advMapDescription = NDb::Get<NDb::AdvMapDescription>( NDb::DBID( info.mapDescName ) );

  bool aiForLeaversEnabled = false;
  if (_aiForLeaversEnabled)
  {
    if (const NDb::BotsSettings * pDBBotsSettings = GetBotsSettings())
    {
      if (pDBBotsSettings->enableBotsAI)
      {
        bool newbieGame = true;
        for(int i = 0, nPlayers = info.playersInfo.size(); i < nPlayers; ++i)
        {
          const NCore::PlayerStartInfo & desc = info.playersInfo[i];
          if (desc.playerType != NCore::EPlayerType::Invalid && desc.playerInfo.basket != NCore::EBasket::Newbie)
          {
            newbieGame = false;
            break;
          }
        }
        aiForLeaversEnabled = newbieGame;
      }
    }
  }

  const bool enableBehaviourTrackingForThisGame = CanTrackPlayersBehaviour(info);

  players.resize( info.playersInfo.size() );
  for(int i = 0, nPlayers = info.playersInfo.size(); i < nPlayers; ++i)
  {
    const NCore::PlayerStartInfo & desc = info.playersInfo[i];

    // ����� �� ����������� ��������� ����� � ��������
    const bool enableBehaviourTracking =
      (enableBehaviourTrackingForThisGame) &&
      (desc.userID > 0) &&
      (desc.playerInfo.basket != NCore::EBasket::Newbie);

    players[desc.playerID] = new PFPlayer(
      this,
      desc.playerID,
      desc.teamID,
      desc.originalTeamID,
      desc.userID,
      desc.zzimaSex,
      aiForLeaversEnabled,
      _aiForLeaversThreshold,
      desc.playerInfo.chatMuted,
      enableBehaviourTracking);

    if (players[desc.playerID]->GetUserID() > 0)
    {
      ++humanPlayersCount;
    }
  }

	deadUnits.reserve(20); // 20 dead units per step

  if(instanceCount == 0)
  {
    InitLogicDebugManager();
  }
  ++instanceCount;

  smartRandGen.Init( &randGen );

  protection = PFWorldProtection::Create(this);
}



void PFWorld::OnDestroyContents()
{
  --instanceCount;
  if(instanceCount == 0)
  {
    DeinitLogicDebugManager();
  }

  // cleanup all units
  TObjects objects = GetObjects();
	for(TObjects::iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    if (IsValid(it->second))
    {
      PFBaseUnit *pUnit = dynamic_cast<PFBaseUnit*>(it->second.GetPtr());
      if (pUnit)
        pUnit->CleanupAfterDeath(true);
    }
  }

  KillDeadUnits(true);

  preResources.clear();

#ifndef _SHIPPING
  NDebug::DebugObject::ClearAll();
#endif

  World::OnDestroyContents();
}



void PFWorld::InitMinigames()
{
  NI_PROFILE_FUNCTION;

  NI_ASSERT( pScene, "" );

  NDb::Ptr<NDb::MinigamesBasic> ptr = NDb::Get<NDb::MinigamesBasic>( NDb::DBID( "MiniGames/MinigameCommon.xdb" ) );
  minigamesMain = ptr->Construct();
  if (IsValid(minigamesMain))
    minigamesMain->Set( pScene, this );
}



bool PFWorld::LoadMap( const NDb::AdvMapDescription * _advMapDescription, const NDb::AdventureCameraSettings * cameraSettings, const NCore::TPlayersStartInfo & playersInfo, LoadingProgress * progress, bool isReconnecting, const NWorld::PFResourcesCollection::TalentMap& talents )
{
  NI_PROFILE_FUNCTION_MEM;

  MAP_LOADING_SCOPE(mapLoadingController);

  advMapDescription = _advMapDescription;

  NDb::Ptr<NDb::AdvMap> advMap = advMapDescription->map;
  NI_VERIFY( IsValid(advMap), "Couldn't get map", return false );

  NDb::Ptr<NDb::AdvMapSettings> advMapSettings = IsValid(advMapDescription->mapSettings) ? (advMapDescription->mapSettings) : (advMap->mapSettings);
  NI_VERIFY( IsValid(advMapSettings), "Couldn't get map settings", return false );

  NI_ASSERT( advMap->ambienceMap.texture, "Couldn't get ambienceMap's texture" );
  if( advMap->ambienceMap.texture )
    ambienceMap = &advMap->ambienceMap;

  allScriptFunctionsEnabled = humanPlayersCount == 1 || advMapSettings->enableAllScriptFunctions;

  triggerMarkerHandler = new TriggerMarkerHandler( this, advMapSettings );

  // Training stuff
  int trainingUserId = -1;
  for ( int i = 0, playersInfoCnt = playersInfo.size(); i < playersInfoCnt; i++ )
  {
    if ( playersInfo[i].playerType == NCore::EPlayerType::Human )
    {
      trainingUserId = playersInfo[i].userID;
      break;
    }
  }
  const bool isTraining = advMapDescription->mapType == NDb::MAPTYPE_TRAINING || advMapDescription->mapType == NDb::MAPTYPE_SERIES;
  // 

  {
    MAP_LOADING_IP;

    NI_PROFILE_BLOCK_MEM( "Phase1" );

    pTileMap->Prepare(advMap->terrain->elemXCount * advMap->terrain->tilesPerElement,
      advMap->terrain->elemYCount * advMap->terrain->tilesPerElement,
      pScene->GetGridConstants().metersPerTile);
    
    LockOutsideCameraArea(cameraSettings);

    pAIWorld->SetVoxelMapSizes(pTileMap);
    pNatureMap->OnLoaded(advMap->terrain);
    progress->SetPartialProgress( EMapLoadStages::Terrain, 1.0f );

    MAP_LOADING_IP;

    pathFinder = new Pathfinding::CCommonPathFinder( pTileMap );
    routPathFinder = new Pathfinding::RoutePathFinder(pathFinder);
    int warFogObstacleVisibility = pTileMap->GetLenghtInTiles(advMap->lightEnvironment->warFogObstacleVisibility);
    warFog = new FogOfWar(this, NDb::KnownEnum<NDb::EFaction>::sizeOf, pTileMap->GetSizeX(), pTileMap->GetSizeY(), 4, warFogObstacleVisibility);

    MAP_LOADING_IP;
    
    if (pNatureMap->GetUseRoadInPathFinding())
    {
      for (int iRoute = 0; iRoute < NDb::KnownEnum<NDb::ENatureRoad>::sizeOf; ++iRoute)
      {
          vector<CVec2> const& road = pNatureMap->GetLogicRoad((NDb::ENatureRoad)iRoute);
          vector<SVector> tileRoad;
          tileRoad.reserve(road.size());

          for (int i = 0; i< road.size(); i++)
            tileRoad.push_back(pTileMap->GetTile(road[i]));

          routPathFinder->AddRoute(tileRoad, 1);
      }
    }

    progress->SetPartialProgress( EMapLoadStages::PathFinding, 1.0f );

    MAP_LOADING_IP;

    pAIWorld->SetMapData(advMapDescription, advMapSettings);

    if ( advMapDescription->scoringTable )
    {
      pStatistics->SetScoringTable( advMapDescription->scoringTable );
    }

#ifndef VISUAL_CUTTED
    MAP_LOADING_IP;

    if (IsValid(pScene))
    {
      if (advMap->bakedLighting.vertexColorsFileName != "")
      {
        pScene->CreateMeshVertexColorsManager();
        pScene->GetMeshVertexColorsManager()->Load(advMap->bakedLighting.vertexColorsFileName);
      }

      pScene->InitSHGrid(advMap->bakedLighting);

      // initialize dynamic lighting
      Render::AABB worldAABB;
      worldAABB.halfSize = 0.5f * pScene->GetGridConstants().worldSize;
      worldAABB.center = worldAABB.halfSize;
      Render::LightsManager* pLightMan = Render::GetLightsManager();
      pLightMan->SetBounds(worldAABB);
      pLightMan->CreateLights(advMap->pointLights);
    }
#endif
  }
  progress->SetPartialProgress( EMapLoadStages::Scene, 1.0f );

  MAP_LOADING_IP;

  pTileMap->ApplyHeightMap(adventureScreen->GetHeightsAsFloat());

  if( !LoadSceneMapObjects( advMapDescription, playersInfo, advMapDescription->mapType == NDb::MAPTYPE_TUTORIAL, progress, talents ) )
    return false;

  LoadPrecachedResources( advMapDescription );

  progress->SetPartialProgress( EMapLoadStages::MapObjects, 1.0f );

  if (IsValid(dayNightController))
    dayNightController->Initialize();

  {
    MAP_LOADING_IP;

    NI_PROFILE_BLOCK_MEM( "Phase2" );

    pTileMap->OnLoaded();

    if (advMap->lightEnvironment->warFogUseHeightsDelta)
    {
      warFog->ApplyHeightMap( adventureScreen->GetHeightsAsFloat(), pScene->GetHeightsController() );
      warFog->ApplyHeightSettings(advMap->lightEnvironment->warFogUseHeightsDelta, 
                                    advMap->lightEnvironment->warFogMaxHeightsDelta);
    }

    MAP_LOADING_IP;

    //
    const float goldPerTeam = advMapSettings->primeSettings.startPrimePerTeam;
    const int goldPerPlayer = static_cast<int>( goldPerTeam / (float)advMapDescription->teamSize );

    PFBaseHero* trainingHero = 0;
    for (int i = 0, count = GetPlayersCount(); i < count; ++i)
    {
      PFBaseHero* pHero = GetPlayer(i)->GetHero();
      if ( pHero )
        pHero->AddGold( goldPerPlayer, false );

      // Get training hero
      if ( isTraining && GetPlayer(i)->GetUserID() == trainingUserId )
          trainingHero = pHero;
      if ( !advMapSettings->enablePortalTalent )
      {
        if ( PFBaseMaleHero* pMaleHero = dynamic_cast<PFBaseMaleHero*>(pHero) )
          pMaleHero->GetPortal()->AddForbid();
      }
    }

    MAP_LOADING_IP;

    // Calculate stats modifier for buildings and creeps
    pAIWorld->ApplyForceModifiers(advMapSettings->force, advMapSettings->trainingForceCoeff, advMapDescription->mapType, trainingHero);

    pAIContainer->BuildIdNameMap();

    pAIContainer->SetScriptAreas( advMap->scriptAreas );

    if (!advMapSettings->scriptFileName.empty())
    {
      NI_ASSERT(pAIContainer, "AIContainer wasn't set before map load finish!");
      vector<NDb::ResourceDesc> null;
      pAIContainer->LoadScript(advMapSettings->scriptFileName, 
        advMapSettings->dictionary ? advMapSettings->dictionary->resources : null, isReconnecting );
    }

    progress->SetPartialProgress( EMapLoadStages::HeightMap, 1.0f );
  }

  return true;
}

void PFWorld::LoadPrecachedResources(const NDb::AdvMapDescription * advMapDescription)
{
  MAP_LOADING_SCOPE(mapLoadingController);

  const int precacheDepth = 20;
  NDb::Ptr<NDb::SessionRoot> pRoot = NDb::SessionRoot::GetRoot();

  const vector<NDb::Ptr<NDb::BasicEffectAttached>> &selfAuraEffects = pRoot->visualRoot->selfAuraEffects;
  const vector<NDb::Ptr<NDb::BasicEffectAttached>> &auraEffects = pRoot->visualRoot->auraEffects.auraEffects;
  const vector<NDb::Ptr<NDb::UnitLogicParameters>> &unitParameters = pRoot->logicRoot->unitLogicParameters->unitParameters;
  
  for( int i = 0; i < selfAuraEffects.size(); i++ )
  {
    CObj<PF_Core::BasicEffectAttached> pEffect = PF_Core::EffectsPool::Get()->Retrieve<PF_Core::BasicEffectAttached>( selfAuraEffects[i] );
    pEffect->DieImmediate();
    pEffect->DieImmediate();
  }
  for( int i = 0; i < auraEffects.size(); i++ )
  {
    CObj<PF_Core::BasicEffectAttached> pEffect = PF_Core::EffectsPool::Get()->Retrieve<PF_Core::BasicEffectAttached>( auraEffects[i] );
    pEffect->DieImmediate();
    pEffect->DieImmediate();
  }

  NDb::Ptr<NDb::AnimSet> pAnimSet = pRoot->visualRoot->animSets.sets[NDb::ANIMSETID_CREEP];
  preResources[ pAnimSet->GetDBID().GetFileName() ] = pAnimSet;

  for( int i = 0; i < unitParameters.size(); i++ )
  {
    MAP_LOADING_IP_VOID;

    if( unitParameters[i] )
    {
      unitParameters[i]->defaultStats.GetPtr();
      unitParameters[i]->targetingPars.GetPtr();
    }
  }

  for (nstl::map<uint, NDb::Ptr<NDb::MarketingEventRollItem>>::const_iterator i = resourcesCollection->GetMarketingItems().begin(); i!= resourcesCollection->GetMarketingItems().end(); i++)
  {
    MAP_LOADING_IP_VOID;

    NDb::Precache<NDb::MarketingEventRollItem>( (*i).second->GetDBID(), 2, true );
  }
  

  const vector<NDb::Ptr<NDb::AchievBase>> &achievementsList = GetStatistics()->DbScoring()->achievementsList;
  for( int i = 0; i < achievementsList.size(); i++ )
  {
    MAP_LOADING_IP_VOID;

    if(achievementsList[i])
      preResources[ achievementsList[i]->GetDBID().GetFileName() ] = NDb::Precache<NDb::AchievBase>( achievementsList[i]->GetDBID(), precacheDepth );
  }

  if( GetAmbienceMap() )
  {
    GetIAdventureScreen()->PreloadEffectsInResourceTree( GetAmbienceMap()->texture, BADNODENAME );
  }

  preResources[ pRoot->visualRoot->uiEvents->GetDBID().GetFileName() ] = NDb::Precache<NDb::UIEventsCustom>( pRoot->visualRoot->uiEvents->GetDBID(), 5, true ); 
  GetIAdventureScreen()->PreloadEffectsInResourceTree( pRoot->visualRoot->uiEvents, BADNODENAME );

  NDb::Ptr<NDb::ScoringTable> scoring = pRoot->logicRoot->scoringTable;

  GetIAdventureScreen()->PreloadEffectsInResourceTree( scoring, BADNODENAME );

  MAP_LOADING_IP_VOID;

  NDb::Ptr<NDb::AdvMapSettings> advMapSettings = 
    IsValid(advMapDescription->mapSettings) ? (advMapDescription->mapSettings) : (advMapDescription->map->mapSettings);
  NI_VERIFY( IsValid(advMapSettings), "Couldn't get map settings", return; );

  if(advMapSettings->dictionary) 
  {
    const vector<NDb::ResourceDesc> &resources = advMapSettings->dictionary->resources;
    NI_ASSERT(advMapSettings->dictionary->resources.size()!=0, "Resources array in dictionary is empty");

    for( int i = 0; i < resources.size(); i++ )
    {
      MAP_LOADING_IP_VOID;

      NI_VERIFY(resources[i].resource, "Bad resource in dictionary array", continue);
      DebugTrace( "resources[%d]: %s", i, resources[i].resource->GetDBID().GetFileName());
      preResources[ resources[i].resource->GetDBID().GetFileName() ] = 
        NDb::Precache<NDb::DbResource>( resources[i].resource->GetDBID(), 14, true );
      GetIAdventureScreen()->PreloadEffectsInResourceTree( resources[i].resource, BADNODENAME );
    }
  }

  MAP_LOADING_IP_VOID;

  if( advMapSettings->hintsCollection && !advMapSettings->hintsCollection->GetDBID().IsInlined() )
  {
    preResources[ advMapSettings->hintsCollection->GetDBID().GetFileName() ] = 
      NDb::Precache<NDb::HintsCollection>( advMapSettings->hintsCollection->GetDBID(), precacheDepth );
  }

  MAP_LOADING_IP_VOID;

  if( advMapSettings->dialogsCollection && !advMapSettings->dialogsCollection->GetDBID().IsInlined() )
  {
    preResources[ advMapSettings->dialogsCollection->GetDBID().GetFileName() ] = 
      NDb::Precache<NDb::DialogsCollection>( advMapSettings->dialogsCollection->GetDBID(), precacheDepth, true );
  }

  MAP_LOADING_IP_VOID;

  if ( advMapSettings->primeSettings.neutralKillExperienceModifier )
  {
    preResources[ advMapSettings->primeSettings.neutralKillExperienceModifier->GetDBID().GetFileName() ] = 
      NDb::Precache<NDb::KillExperienceModifier>( advMapSettings->primeSettings.neutralKillExperienceModifier->GetDBID(), precacheDepth );
  }

  MAP_LOADING_IP_VOID;

  if ( advMapSettings->overrideBotsSettings && !advMapSettings->overrideBotsSettings->GetDBID().IsInlined() )
  {
    preResources[ advMapSettings->overrideBotsSettings->GetDBID().GetFileName() ] = 
      NDb::Precache<NDb::HintsCollection>( advMapSettings->overrideBotsSettings->GetDBID(), precacheDepth );
  }

  MAP_LOADING_IP_VOID;

  if ( advMapSettings->customBattleStartAnnouncement.imageBurn )
    GetIAdventureScreen()->PreloadEffectsInResourceTree( advMapSettings->customBattleStartAnnouncement.imageBurn, BADNODENAME );

  MAP_LOADING_IP_VOID;

  if ( advMapSettings->customBattleStartAnnouncement.imageFreeze )
    GetIAdventureScreen()->PreloadEffectsInResourceTree( advMapSettings->customBattleStartAnnouncement.imageFreeze, BADNODENAME );

  MAP_LOADING_IP_VOID;

  GetIAdventureScreen()->PreloadEffectsInResourceTree( NDb::SessionRoot::GetRoot()->logicRoot->aiLogic->consumableGroups, BADNODENAME );
}

bool PFWorld::LoadSceneMapObjects( const NDb::AdvMapDescription* advMapDesc, const NCore::TPlayersStartInfo & players, const bool isTutorial, LoadingProgress * progress, const NWorld::PFResourcesCollection::TalentMap& talents )
{
  NI_PROFILE_FUNCTION;

  NI_VERIFY( advMapDesc,          "Invalid advMap resource!", return false );
  NI_VERIFY( IsValid(pScene),  "Invalid scene!",           return false );

  MAP_LOADING_SCOPE(mapLoadingController);
  MAP_LOADING_IP;

  PushDXPoolGuard dxPool("Game objects");

  TSpawnInfo heroesSpawnInfo;
  heroesSpawnInfo.resize( NDb::KnownEnum<NDb::ETeamID>::SizeOf() ); // do not use invalid team

  const vector<NDb::AdvMapObject> &objects = advMapDesc->map->objects;

  int objectsLoaded = 0;
  ObjectsLoader<PFWarFogUnblock>(this).Load(objects, NDb::WarFogUnblock::typeId, "WarFogUnblock", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFSimpleObject>(this).Load(objects, NDb::SimpleObject::typeId, "SimpleObject", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFSimpleObject>(this).Load(objects, NDb::GameObject::typeId, "GameObject", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFSimpleObject>(this).Load(objects, NDb::Road::typeId, "Road", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFRoadFlagpole>(this).Load(objects, NDb::Flagpole::typeId, "Flagpole", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFScriptedFlagpole>(this).Load(objects, NDb::ScriptedFlagpole::typeId, "Flagpole", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFCreepSpawner>(this).Load(objects, NDb::AdvMapCreepSpawner::typeId, "AdvMapCreepSpawner", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFNeutralCreepSpawner>(this).Load(objects, NDb::AdvMapNeutralCreepSpawner::typeId, "AdvMapNeutralCreepSpawner", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFTree, true>(this).Load(objects, NDb::TreeObject::typeId, "TreeObject", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFMultiStateObject>(this).Load(objects, NDb::MultiStateObject::typeId, "MultiStateObject", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFUsableBuilding>(this).Load(objects, NDb::UsableBuilding::typeId, "UsableBuilding", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFSimpleBuilding>(this).Load(objects, NDb::Building::typeId, "Building", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFGlyphSpawner>(this).Load(objects, NDb::GlyphSpawner::typeId, "GlyphSpawner", objectsLoaded, progress); MAP_LOADING_IP;
  ObjectsLoader<PFShop>(this).Load(objects, NDb::Shop::typeId, "Shop", objectsLoaded, progress ); MAP_LOADING_IP;
#ifndef VISUAL_CUTTED
  if( ObjectsLoader<PFMinigamePlace>(this).Load(objects, NDb::MinigamePlace::typeId, "MinigamePlace", objectsLoaded, progress) )
  {
    InitMinigames();
  }
  LoadPureClientObjectsOfType<NGameX::PFPureClientCritter>( adventureScreen, pScene, objects, NDb::Critter::typeId, "Critter", objectsLoaded, progress ); MAP_LOADING_IP;
#endif
  ObjectsLoader<PFTower>(this).Load(objects, NDb::Tower::typeId, "Tower", objectsLoaded, progress); MAP_LOADING_IP;
  progress->SetPartialProgress( EMapLoadStages::MapObjects, 0.95f );

  // ������ ����������� ���������, ��� ��� ����� �������� �����
  ObjectsLoader<PFAdvMapObstacle, false>(this).Load(objects, NDb::AdvMapObstacle::typeId, "AdvMapObstacle", objectsLoaded, progress); MAP_LOADING_IP;

  {
    NI_PROFILE_BLOCK_MEM( "OtherObjects" );

    // fill map with static objects
    int idx              = 0;
    int objCount         = 0;
    int heroSpawnerCount = 0;
    for ( vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it, ++idx )
    {
      MAP_LOADING_IP;

      NI_DATA_VERIFY( IsValid( it->gameObject ), NStr::StrFmt("Invalid gameObject assigned to advmap object #%d at %2.3f � %2.3f", it - objects.begin(), it->offset.GetPlace().pos.x, it->offset.GetPlace().pos.y ), continue; );
      if (pScene->GetMeshVertexColorsManager())
      {
        pScene->GetMeshVertexColorsManager()->AdvMapObjectVCBegin(idx);
      }

      PF_Core::WorldObjectBase* pObject = 0;
      switch ( it->gameObject->GetObjectTypeID() )
      {
      case NDb::HeroPlaceHolder::typeId:
        if(NDb::HeroPlaceHolder const* placeholder = dynamic_cast<NDb::HeroPlaceHolder const*>(it->gameObject.GetPtr()))
        {
          ++objCount;
          ++heroSpawnerCount;
          HeroSpawnInfo info;
          info.placement   = it->offset.GetPlace();
          info.placeholder = placeholder;

          heroesSpawnInfo[placeholder->teamId].push_back(info);
        }
        break;
      case NDb::Quarter::typeId:
        ++objCount;
        pObject = new PFQuarters(this, *it);
        break;
      case NDb::MainBuilding::typeId:
        {
          ++objCount;
          PFMainBuilding* const mb = new PFMainBuilding(this, *it);
          mainBuildings.push_back(mb);
          pObject = mb;
        }
        break;
      case NDb::Fountain::typeId:
        ++objCount;
        pObject = new PFFountain(this, *it);
        break;
      case NDb::AdvMapCameraSpline::typeId:
        pAIContainer->RegisterCameraSpline( it->gameObject->GetDBID(), it->offset.GetPlace() );
        break;
      case NDb::ScriptPath::typeId:
        pAIContainer->RegisterScriptPath( it->scriptName, dynamic_cast<const NDb::ScriptPath*>(it->gameObject.GetPtr()) );
      case NDb::ScriptPolygonArea::typeId:
        pAIContainer->RegisterPolygonArea( it->scriptName, dynamic_cast<const NDb::ScriptPolygonArea*>(it->gameObject.GetPtr()) );
        break;
      }

      if (pObject)
      {
        pAIContainer->RegisterObject(pObject, (*it).scriptName, (*it).scriptGroupName );
      }

      if (pScene->GetMeshVertexColorsManager())
      {
        pScene->GetMeshVertexColorsManager()->AdvMapObjectVCEnd(idx);
      }
    }

    MAP_LOADING_IP;

    // yes, it's hacky
    if ( heroSpawnerCount )
    {
      bool spawned = SpawnHeroes( this, advMapDesc, players, isTutorial, &heroesSpawnInfo, pScene, progress, talents );
      MAP_LOADING_IP;
      NI_ASSERT( spawned, "Failed to spawn players!" );
    }
  }

  return true;

}


PFPlayer* PFWorld::GetPlayer(int id) const
{
  NI_VERIFY(0<= id && id < players.size(), "Trying to get player by wrong id!", return NULL; );
  return players[id];
}

PFPlayer* PFWorld::GetPlayerByUID(int userId) const
{
  for (int i = 0; i < players.size(); i++)
  {
    NWorld::PFPlayer * player = players[i];

    if (player && player->GetUserID() == userId)
    {
      return player;
    }
  }
  return 0;
}

const int  PFWorld::GetPresentPlayersCount() const
{
  int count = 0;
  for(vector<CObj<PFPlayer> >::const_iterator it = players.begin(), end = players.end(); it != end; ++it)
    if((*it)->GetHero())
      ++count;

  return count;
}

const int  PFWorld::GetPresentPlayersCount(NDb::EFaction faction) const
{
  int count = 0;

  for (vector<CObj<PFPlayer> >::const_iterator it = players.begin(), end = players.end(); it != end; ++it)
    if (const PFBaseHero * pHero = (*it)->GetHero())
      if (pHero->GetFaction() == faction)
        ++count;

  return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::UpdatePlayerStatuses(const NCore::TStatuses & statuses)
{
  for (int i = 0; i < statuses.size(); ++i)
  {
    if (PFPlayer * player = GetPlayerByUID(statuses[i].clientId))
    {
      const NCore::ClientStatus & clientStatus = statuses[i];

      // ����� � ����������
      PFBaseHero* hero = player->GetHero();
      if ( hero )
      {
        NWorld::PFHeroStatistics* stat = hero->GetHeroStatistics();
        if ( stat )
        {
          stat->SetLeaveStatus( clientStatus.status );
          if ( clientStatus.status == Peered::Away )
            stat->AddAfk();
        }
      }

      adventureScreen->OnClientStatusChange(clientStatus.clientId, clientStatus.status, clientStatus.step);

      {
        const bool disconnected = Peered::IsDisconnectedStatus(clientStatus.status);
        const bool leaver = (clientStatus.status == Peered::RefusedToReconnect);

        player->SetDisconnected(disconnected, leaver);
      }

      bool isPlayingOld = player->IsPlaying();
      bool isPlayingNew = Peered::IsPlayingStatus(clientStatus.status);

      if (isPlayingOld != isPlayingNew)
      {
        player->SetIsPlaying(isPlayingNew);

        if (!isPlayingNew)
        {
          adventureScreen->OnPlayerDisconnected(player, clientStatus.step);
        }
      }

      bool isActiveOld = player->IsActive();
      bool isActiveNew = (clientStatus.status == Peered::Active);

      if (isActiveOld != isActiveNew)
      {
        player->SetIsActive(isActiveNew);

        const bool afk = (clientStatus.status == Peered::Away);

        if (afk)
          PlayerBehaviourTracking::DispatchEvent(player, EPlayerBehaviourEvent::Gone);
      }
    }
    else
    {
      NI_ALWAYS_ASSERT(NStr::StrFmt("UpdatePlayerStatuses: Player not found (uid=%d)", statuses[i].clientId));
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::ExecuteCommands(const NCore::TPackedCommands & commands)
{
  NI_PROFILE_FUNCTION

  SyncFPUStart( nfpu::AT_CMD_EXECUTE );

  for( NCore::TPackedCommands::const_iterator it = commands.begin(); it != commands.end(); ++it )
  {
    CObj<NCore::WorldCommand> wcmd = (*it)->GetWorldCommand( GetPointerSerialization() );

    if ( wcmd && wcmd->CanExecute() )
    {
      wcmd->Execute( this );
    }
  }

  SyncFPUEnd( nfpu::AT_CMD_EXECUTE );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFWorld::Step(float dtInSeconds, float dtLocal)
{
  NI_PROFILE_FUNCTION

  SyncFPUStart( nfpu::AT_WORLD_STEP );

  if( IsPaused() )
  {
    if ( IsValid( pAIContainer ) ) 
      pAIContainer->Step( dtInSeconds );

    SyncFPUEnd( nfpu::AT_WORLD_STEP );

    return true;
  }

  if (GetDebugRender())
  {
    NI_PROFILE_BLOCK( "Debug Render clear" );
    GetDebugRender()->ClearBuffer(Render::DRBUFFER_STEP);
  }

	NI_VERIFY(dtInSeconds > 0 || dtLocal > 0, "Stepping PFWorld with zero time deltas", return true);

  time_syncTime.AddValue( dtInSeconds * 1000.f );
  time_localTime.AddValue( dtLocal * 1000.f );

  if(dtInSeconds <= 0 && dtLocal > 0)
    return true;

  ++step;
  mainPerf_PFWorld_StepId.SetValue(step);
  {
    timeElapsed += dtInSeconds;

    pAIWorld->Update(dtInSeconds);

    {
      NI_PROFILE_BLOCK( "CoreStep" );

      LogStepBegin(step);
      PF_Core::World::Step(dtInSeconds, dtLocal); // step parent class

		  KillDeadUnits();
    }
  }

  {
    NI_PROFILE_BLOCK( "update movements and warfog" );
    MovingUnit::UpdateMovements( GetAIWorld(), pResolver, GetTileMap(), dtInSeconds );
    if ( IsValid( warFog ) )
      warFog->StepVisibility(dtInSeconds);  
    pNatureMap->OnStep(dtInSeconds);
  }

#ifdef STARFORCE_PROTECTED
  {
    NI_PROFILE_BLOCK("protection update");
    protection->Update();
  }
#endif // STARFORCE_PROTECTED

  {
    NI_PROFILE_BLOCK("day-night controller update");
    if (IsValid(dayNightController))
      dayNightController->Update(dtInSeconds);
  }

  // step statistics system
  pStatistics->OnStep(dtInSeconds);

  if( IsValid( triggerMarkerHandler ) )
    triggerMarkerHandler->Step(dtInSeconds);

  if ( minigamesMain )
    minigamesMain->Step( dtInSeconds );

  if ( IsValid( pAIContainer ) ) 
    pAIContainer->Step( dtInSeconds );

  smartRandGen.Cleanup();

  SyncFPUEnd( nfpu::AT_WORLD_STEP );

#ifndef VISUAL_CUTTED
	NMainLoop::MarkStepFrame();
#endif
  
#ifndef _SHIPPING
  NDebug::DebugObject::ProcessAll( GetDebugRender() );
#endif  

  ProtectionCheck();

  return true;
}


void PFWorld::CalcCRC( IBinSaver& f, bool fast )
{
  if ( !fast )
  {
    CPtr<PFWorld> pTmp(this); 
    f.Add( 1, &pTmp );
  }
  else
  {
    int idChunk = 0;

    for(int i = 0, size = players.size(); i < size; ++i)
    {
      PFPlayer* player = players[i];
      if ( !IsValid( player ) )
        continue;
      PFBaseHero* hero = player->GetHero();
      if ( !IsValid( hero ) )
        continue;

      int subChunkId = 0;

      f.StartChunk(idChunk, 1, IBinSaver::CHUNK_COBJECTBASE);

      CVec3 pos = hero->GetPosition();
      f.Add( ++subChunkId, &pos );
      float value;
      value = hero->GetHealth();
      f.Add( ++subChunkId, &value );
      value = hero->GetMana();
      f.Add( ++subChunkId, &value );
      value = hero->GetNafta();
      f.Add( ++subChunkId, &value );

      for( int stat = 0; stat < NDb::KnownEnum<NDb::EStat>::sizeOf; ++stat )
      {
        value = hero->GetStat( (NDb::EStat)stat )->GetValue();
        f.Add( ++subChunkId, &value );
      }

      PlayerBehaviourTracking::UpdateFastCRC(player, f, ++subChunkId);

      f.FinishChunk();

      ++idChunk;
    }

    {
      int subChunkId = 0;

      f.StartChunk(idChunk, 1, IBinSaver::CHUNK_COBJECTBASE);

      if (PFScript * pLuaScript = pAIContainer->GetLuaScript())
      {
        f.Add(++subChunkId, pLuaScript);
      }

      if (IsValid(dayNightController))
      {
        float value = dayNightController->GetNightFraction();
        f.Add(++subChunkId, &value);
      }

      // timescale
      {
        float value = GetTimeScale();
        f.Add(++subChunkId, &value);
      }

//       // double check timescale - check global var
//       {
//         float value = NMainLoop::GetTimeScale();
//         f.Add(++subChunkId, &value);
//       }

#ifdef _SHIPPING
      // visibility map mode
      {
        DI_WEAK(NGameX::VisibilityMapClient) visMap;

        bool value = true;

        if (IsValid(visMap) && NGameX::EVisMapMode::IsValid(visMap->GetMode()))
        {
          // TODO: double check?
          if (GetIAdventureScreen()->IsSpectator())
          {
            value =
              (visMap->GetMode() == NGameX::EVisMapMode::Combined) ||
              (visMap->GetMode() == NGameX::EVisMapMode::FromFaction(GetIAdventureScreen()->GetPlayerFaction()));

            // NUM_TASK hacky "fix" to overcome visMap mode changing lag after switching to another player
            {
              static unsigned failures = 0U;
              static const unsigned failuresAllowed = 2U;

              if (value)
              {
                failures = 0U;
              }
              else if (failures < failuresAllowed)
              {
                ++failures;

                value = true;
              }
            }
          }
          else
          {
            value =
              (visMap->GetMode() == NGameX::EVisMapMode::FromFaction(GetIAdventureScreen()->GetPlayerFaction()));
          }
        }

        f.Add(++subChunkId, &value);
      }

      // main buildings
      {
        for (int i = 0, count = mainBuildings.size(); i < count; ++i)
        {
          PFMainBuilding* const mb = mainBuildings[i];

          float value = mb->GetHealth();
          f.Add(++subChunkId, &value);
          value = mb->GetMaxHealth();
          f.Add(++subChunkId, &value);
        }
      }
#endif

      f.FinishChunk();

      ++idChunk;
    }
  }
}


void PFWorld::StoreDeadUnit(PFBaseUnit *pUnit)
{
  NI_ASSERT( step != -1, "Kill unit before world create! Reconnect may fail!" ); // NUM_TASK
	deadUnits.push_back(CObj<PFBaseUnit>(pUnit));
}

//////////////////////////////////////////////////////////////////////////
void PFWorld::UnregisterCreep(const PFCommonCreep*) 
{ 
  NI_VERIFY(totalCreepsCount > 0, "Nothing to unregister!", return); 
  --totalCreepsCount; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::OnGameFinished( NDb::EFaction failedFaction )
{
  if (!manualGameFinish && IsFactionDefeated(failedFaction))
  {
    GameFinish( failedFaction );
  }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::GameFinish( NDb::EFaction failedFaction )
{
  //PFAIWorld::OnGameFinished() resets voting info;
  //GetSurrenderVotes() should be called before
  int surrenderVotes = pAIWorld->GetSurrenderVotes( failedFaction );

  pAIWorld->OnGameFinished( failedFaction );
  adventureScreen->OnVictory( failedFaction, surrenderVotes );
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::KillDeadUnits(bool fullCleanup)
{
  NI_PROFILE_FUNCTION

  for (int i = 0; i < deadUnits.size(); ++i)
  {
    if (PFBaseUnit * unit = deadUnits[i])
    {
      unit->CleanupAfterDeath(fullCleanup);
    }
    else
    {
      NI_ALWAYS_ASSERT_TRACE( "Dead unit already removed" );
    }
  }

  deadUnits.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFWorld::CanCreateClients()
{
  return adventureScreen ? adventureScreen->CanCreateClients() : false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::StopMovingUnits()
{
  if (pAIWorld)
  {
    struct MovingUnitStopper : public NonCopyable
    {
      void operator()(NWorld::PFLogicObject & object)
      {
        if (PFBaseMovingUnit * unit = dynamic_cast<PFBaseMovingUnit*>(&object))
          unit->Stop();
      }
    } movingUnitStopper;

    pAIWorld->ForAllUnits(movingUnitStopper);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::SyncFPUStart( nfpu::ActionType actionType )
{
  fPUState = GetProcessorState();
  SyncProcessorState();
  fPUStatesData.SetStartState( actionType, GetProcessorState() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::SyncFPUEnd( nfpu::ActionType actionType )
{
  fPUStatesData.SetFinishState( actionType, GetProcessorState() );
  SetProcessorState( fPUState );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::AddAI( PFBaseHero* hero, int line )
{
  NI_VERIFY(hero, "Invalid hero!", return;);

  if (pAIContainer)
    pAIContainer->Add( hero, line );

  if (adventureScreen)
    adventureScreen->OnStartAiForPlayer( hero->GetPlayer() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::RemoveAI( PFBaseHero* hero )
{
  NI_VERIFY(hero, "Invalid hero!", return;);

  if (pAIContainer)
    pAIContainer->Remove( hero );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const NDb::BotsSettings * PFWorld::GetBotsSettings() const
{
  NI_VERIFY(IsValid(advMapDescription), "Invalid map description!", return 0;);

  const NDb::BotsSettings * pDBBotsSettings = 0;

  const NDb::AdvMapSettings * desc = IsValid(advMapDescription->mapSettings) ? advMapDescription->mapSettings 
                                                                             : advMapDescription->map->mapSettings;

  NI_VERIFY(desc, "Invalid map settings!", return 0;);

  pDBBotsSettings = IsValid(desc->overrideBotsSettings) ? desc->overrideBotsSettings : GetAIWorld()->GetAIParameters().botsSettings;

  return pDBBotsSettings;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::LockOutsideCameraArea(const NDb::AdventureCameraSettings * cameraSettings )
{
  NI_PROFILE_FUNCTION
  NI_VERIFY(cameraSettings, "invalid camera", return);
  NI_VERIFY(IsValid(pTileMap), "invalid tile map", return);
  
  int xrad = pTileMap->GetLenghtInTiles(cameraSettings->limitRadiusHor);
  int yrad = pTileMap->GetLenghtInTiles(cameraSettings->limitRadiusVert);

  // Apply lock multipliers, if they are not zero
  if (cameraSettings->lockMultRadiusHor)
    xrad *= cameraSettings->lockMultRadiusHor;
  if (cameraSettings->lockMultRadiusVert)
    yrad *= cameraSettings->lockMultRadiusVert;

  int width = pTileMap->GetSizeX();
  int height = pTileMap->GetSizeY();

  SVector center(width/2, height/2);

  // Apply locking circle center offsets: first - camera offset, 
  // second - lock ofset relative to the camera offset.
  center += cameraSettings->centerOffset + cameraSettings->lockCenterOffset;

  vector<SVector> lockedTiles;
  lockedTiles.reserve(300000);

  for (int i=0; i<width; i++)
  {
    for (int j=0; j<height;j++)
    {
      SVector pos(i,j);
      pos-=center;

      bool outside = (fabs2(pos.x)/fabs2(xrad) + fabs2(pos.y)/fabs2(yrad))>1;  
      if (outside)
      {
        lockedTiles.push_back(SVector(i,j));
      }
    }
  }
  pTileMap->MarkObject(lockedTiles, true, MAP_MODE_BUILDING);

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::Reset()
{
  deadUnits.reserve(20); // 20 dead units per step

  smartRandGen.Init( &randGen );

  ResetClientObjects();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorld::ResetClientObjects()
{
  adventureScreen->OnTimeScaleChanged(timeScale);

	TObjects& objs = GetObjects();

	for (TObjects::iterator it = objs.begin(); it != objs.end(); it++ )
	{
		if ( IsValid(it->second) )
			it->second->DoReset();
	}

	TWeakObjects& weakObjs = GetWeakObjects();

	for (TWeakObjects::iterator it = weakObjs.begin(); it != weakObjs.end(); it++ )
	{
		if ( IsValid(it->second) )
			it->second->DoReset();

	}
}

void PFWorld::NotifyTalentCastProcessed( const PFTalent* pTalent)
{
  if(!IsValid(pTalent)) return;
  if (PFScript * pScript = pAIContainer->GetLuaScript())
  {
    string talentId = pTalent->GetTalentDesc()->persistentId;
    const CPtr<PFBaseUnit> pOwner = pTalent->GetOwner();
    int ownerWOID = IsValid(pOwner) ? pOwner->GetWOID() : 0;

    CALL_LUA_FUNCTION_ARG2( pScript, "OnTalentCastProcessed", false, talentId, ownerWOID);
  }
}

void PFWorld::NotifyConsumableProcessed( const PFConsumableAbilityData* pConsumable)
{
  if(!IsValid(pConsumable)) return;

  if (PFScript * pScript = pAIContainer->GetLuaScript())
  {
    string talentId = IsValid(pConsumable->GetUsingConsumable()) ? pConsumable->GetUsingConsumable()->GetDBDesc()->persistentId : "";
    const PFAbilityData* pAbilityData = dynamic_cast<const PFAbilityData*>(pConsumable);
    PFBaseUnit* pOwner = IsValid(pAbilityData)? pAbilityData->GetOwner() : NULL;
    int ownerWOID = IsValid(pOwner) ? pOwner->GetWOID() : 0;
    
    CALL_LUA_FUNCTION_ARG2( pScript, "OnConsumableCastProcessed", false, talentId, ownerWOID);
  }
}

void PFWorld::NotifyCreepSpawnerCleaned( const PFNeutralCreepSpawner* pSpawner, const PFBaseUnit* pKiller )
{
  if( !IsValid(pSpawner) ) return;

  if (PFScript * pScript = pAIContainer->GetLuaScript())
  {
    CALL_LUA_FUNCTION_ARG2( pScript, "OnCreepSpawnerCleaned", false, pSpawner->GetSpawnerName(), 
      IsValid(pKiller) ? pKiller->GetWOID() : -1);
  }
}

bool PFWorld::IsFactionDefeated(const NDb::EFaction& faction)
{ 
  return defeatedFaction == static_cast<int>(faction) ? true : false;
}

void PFWorld::SetDefeatedFaction(const NDb::EFaction& faction)
{ 
  if(defeatedFaction == NGameX::FACTION_UNKNOWN)
    defeatedFaction = static_cast<int>(faction);
}

bool PFWorld::CanTrackPlayersBehaviour(const NCore::MapStartInfo& msi) const
{
  const bool replayMode = !msi.replayName.empty();

  if (replayMode)
  {
    DebugTrace("Player behaviour tracking is disabled in replay mode");
    return false;
  }

  bool canTrack = g_enableBehaviourTracking;

#ifdef _SHIPPING
  if (canTrack)
  {
    if (msi.isCustomGame)
      canTrack = false;
  }

  if (canTrack)
  {
    NI_ASSERT(IsValid(advMapDescription), "");

    if (advMapDescription->mapType != NDb::MAPTYPE_PVP)
      canTrack = false;
  }

  /*
  if (canTrack)
  {
    NCore::TPlayersStartInfo::const_iterator it = msi.playersInfo.begin();
    NCore::TPlayersStartInfo::const_iterator it_end = msi.playersInfo.end();
    for (; it != it_end; ++it)
    {
      NCore::TPlayersStartInfo::const_reference psi = *it;

      if (psi.userID > 0)
        continue;

      canTrack = false;
      break;
    }
  }
  */
#endif

  DebugTrace("Player behaviour tracking is %s for this game", (canTrack ? "enabled" : "disabled"));

  return canTrack;
}

bool PFWorld::PollProtectionResult(NCore::ProtectionResult& result)
{
  return protection ? protection->PopResult(result) : false;
}

void PFWorld::SetProtectionUpdateFrequency(const int offset, const int frequency)
{
  if (IsValid(protection))
    protection->SetUpdateFrequency(offset, frequency);
}

bool PFWorld::IsDay() const
{
  if (IsValid(dayNightController))
    return dayNightController->IsDay();
  return true;
}

bool PFWorld::IsNight() const
{
  if (IsValid(dayNightController))
    return dayNightController->IsNight();
  return false;
}

} // namespace NWorld


#endif // PW_LINUX_NULL_RENDER
