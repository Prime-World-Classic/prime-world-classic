#pragma once

#include "Core/GameTypes.h"
#include "System/RandomGenerator.h"
#include "libdb/Db.h"
#include "PF_Core/World.hpp"
#include "PF_Core/WorldObject.h"
#include "Scene/Scene.h"
#include "System/BitData.h"
#include "System/Singleton.h"
#include "Render/debugrenderer.h"
#include "IAdventureScreen.h"
#include "WorldChecker.h"
#include "PFResourcesCollectionClient.h"
#include "System/StarForce/StarForce.h"
#include "SmartRandom.h"
#include "FPUState.h"
#include "Core/Scheduler.h"



namespace PF_Minigames
{
  class IMinigamesMain;
}

namespace NGameX
{
_interface IAdventureScreen;
}

namespace NDb
{
  struct AdvMap;
  struct AdvMapDescription;
  struct AdvMapSettings;
  struct Talent;
  struct Consumable;
  struct BotsSettings;
}

namespace Pathfinding
{
  class CCommonPathFinder;
  class RoutePathFinder;
}

namespace NCore
{
  class PackedWorldCommand;
}

class LoadingProgress;



namespace NWorld
{

class TileMap;
class CollisionResolver;
class PFPlayer;
class PFVoxelMap;
class PFAIWorld;
class PFWorldNatureMap;
class PFStatistics;
class TriggerMarkerHandler;
class PFAIContainer;
class PFBaseUnit;
class PFBaseHero;
class PFLogicObject;
class FogOfWar;
class PFCommonCreep;
class PFTalent;
class PFConsumableAbilityData;
class PFNeutralCreepSpawner;
class PFMainBuilding;
class PFShop;
class PFFlagpole;
class PFPickupableObjectBase;
class PFMinigamePlace;

#if defined( PW_LINUX_NULL_RENDER )
struct LinuxDynamicWorldMarker
{
  enum EKind
  {
    KIND_HERO = 1,
    KIND_COMMON_CREEP = 2,
    KIND_NEUTRAL_CREEP = 3
  };

  float x;
  float y;
  int objectId;
  int kind;
  int faction;
  int playerId;
  int userId;
  string unitDbid;
  string sceneObjectDbid;
  int creepType;
  float healthPercent;
  float energyPercent;
  float objectSize;
  float moveDirX;
  float moveDirY;
  bool hasMoveDirection;
  bool moving;
  bool dead;

  LinuxDynamicWorldMarker()
    : x(0.0f),
      y(0.0f),
      objectId(-1),
      kind(0),
      faction(0),
      playerId(-1),
      userId(-1),
      creepType(-1),
      healthPercent(1.0f),
      energyPercent(0.0f),
      objectSize(0.0f),
      moveDirX(0.0f),
      moveDirY(0.0f),
      hasMoveDirection(false),
      moving(false),
      dead(false)
  {
  }
};
#endif

class MapLoadingController;
class DayNightController;

typedef StrongMT<MapLoadingController> MapLoadingControllerPtr;

namespace EMapLoadStages
{
  enum Enum
  {
    Environment,
    Terrain,
    PathFinding,
    Scene,
    MapObjects,
    Heroes,
    HeightMap
  };
};


typedef map<int, int> TPersistentScores;

class PFWorldProtection;

class PFWorld : public PF_Core::World
{
  OBJECT_METHODS( 0x2C5BDC80, PFWorld );

  PFWorld();

  CPtr<NScene::IScene>    pScene;
  CObj<PF_Minigames::IMinigamesMain> minigamesMain;

  CPtr<PFResourcesCollection> resourcesCollection;

  typedef hash_map< string, NDb::Ptr< NDb::DbResource > > PrecachedResources;
  PrecachedResources preResources;

  static int              instanceCount;
  Render::IDebugRender   *debugRender;

  CObj<Pathfinding::CCommonPathFinder> pathFinder;
  CObj<Pathfinding::RoutePathFinder> routPathFinder;

  Weak<NGameX::IAdventureScreen> adventureScreen;
  NDb::Ptr<NDb::AdvMapDescription> advMapDescription;

	WorldChecker worldChecker;

  const NDb::SoundAmbienceMap* ambienceMap;

  unsigned int            fPUState;       // Intentionally not serialized

  MapLoadingControllerPtr mapLoadingController;

  ZDATA_(PF_Core::World)
  CObj<TileMap>           pTileMap;
  CObj<FogOfWar>          warFog;

  CObj<CollisionResolver> pResolver;
  CObj<PFAIWorld>         pAIWorld;
  CObj<PFAIContainer>     pAIContainer;
  CObj<PFWorldNatureMap>  pNatureMap;
  CObj<PFStatistics>      pStatistics;

  CVec2                   mapSize;    
  vector<CObj<PFPlayer> > players;
	vector<CObj<PFBaseUnit> > deadUnits;
  vector<CObj<PFMainBuilding> > mainBuildings;

  int                     step;
  float                   timeElapsed;

  NRandom::RandomGenerator randGen;
  SmartRandomGenerator    smartRandGen;
  bool                    manualGameFinish;
  int                     humanPlayersCount;
  CObj<TriggerMarkerHandler> triggerMarkerHandler;
  int                     totalCreepsCount;
  bool                    allScriptFunctionsEnabled;
  nfpu::FPUStatesData     fPUStatesData;
  int                     stepLength;
  float                   stepLengthInSeconds;
  
  int                     defeatedFaction; //faction who first lost main building

  CObj<PFWorldProtection> protection;

  CObj<DayNightController>  dayNightController;
  float                   timeScale;
#if defined( PW_LINUX_NULL_RENDER )
  int linuxLoadedWarFogUnblockObjects;
  int linuxLoadedSimpleObjects;
  int linuxLoadedMultiStateObjects;
  int linuxLoadedTreeObjects;
  int linuxLoadedGlyphSpawnerObjects;
  int linuxLoadedAdvMapObstacleObjects;
  int linuxLoadedHeroPlaceHolderObjects;
  int linuxLoadedCreepSpawnerObjects;
  int linuxLoadedNeutralCreepSpawnerObjects;
  int linuxLoadedSimpleBuildingObjects;
  int linuxLoadedUsableBuildingObjects;
  int linuxLoadedShopObjects;
  int linuxLoadedQuarterObjects;
  int linuxLoadedTowerObjects;
  int linuxLoadedControllableTowerObjects;
  int linuxLoadedFountainObjects;
  int linuxLoadedRoadFlagpoleObjects;
  int linuxLoadedScriptedFlagpoleObjects;
  int linuxLoadedMainBuildingObjects;
  int linuxLoadedMinigamePlaceObjects;
  int linuxLoadedCameraSplineObjects;
  int linuxLoadedScriptPathObjects;
  int linuxLoadedScriptPolygonAreaObjects;
  int linuxLastSteppedSpawnerObjects;
  int linuxLastSteppedCreepSpawnerObjects;
  int linuxLastSteppedNeutralCreepSpawnerObjects;
  int linuxSpawnedHeroObjects;
  int linuxPlayersWithHeroObjects;
  int linuxExecutedPackedWorldCommands;
  int linuxLastPackedWorldCommandClientId;
  DWORD linuxLastPackedWorldCommandTypeId;
  int linuxBootstrapRuntimeCommands;
  int linuxLastBootstrapRuntimeCommandClientId;
  int linuxLastBootstrapRuntimeCommandToken;
  float linuxLastBootstrapRuntimeCommandValue;
  int linuxStoredDeadUnits;
  int linuxCleanedDeadUnits;
  int linuxLastStoredDeadUnitObjectId;
  int linuxLastCleanedDeadUnitObjectId;
  int linuxPlayerStatusUpdates;
  int linuxPlayerStatusMissing;
  int linuxPlayerStatusActiveUpdates;
  int linuxPlayerStatusAwayUpdates;
  int linuxPlayerStatusPlayingUpdates;
  int linuxPlayerStatusDisconnectedUpdates;
  int linuxPlayerStatusReconnectedUpdates;
  int linuxPlayerStatusLeaverUpdates;
  int linuxLastPlayerStatusClientId;
  int linuxLastPlayerStatusValue;
  int linuxLastPlayerStatusStep;
  int linuxLastPlayerStatusPlaying;
  int linuxLastPlayerStatusActive;
  int linuxLastPlayerStatusDisconnected;
  int linuxLastPlayerStatusLeaver;
  int linuxAIAutoStartAttempts;
  int linuxAIAutoStartSuccesses;
  int linuxAIAddRequests;
  int linuxAIAddSuccesses;
  int linuxAIRemoveRequests;
  int linuxAIRemoveSuccesses;
  int linuxAIStepCalls;
  int linuxAIControllerCount;
  int linuxAIBotsSettingsAvailable;
  int linuxAIBotsEnabled;
  int linuxAILastHeroObjectId;
  int linuxAILastPlayerId;
  int linuxAILastUserId;
  int linuxAILastLine;
  int linuxAICommandAttempts;
  int linuxAICommandsSent;
  int linuxAICommandDirectFallbacks;
  int linuxAICommandMoveSent;
  int linuxAICommandCombatMoveSent;
  int linuxAICommandStopSent;
  int linuxAICommandFollowSent;
  int linuxAICommandAttackSent;
  int linuxAICommandActivateTalentSent;
  int linuxAICommandUseTalentSent;
  int linuxAICommandBuyConsumableSent;
  int linuxAICommandUseConsumableSent;
  int linuxAICommandUsePortalSent;
  int linuxAICommandPickupObjectSent;
  int linuxAICommandRaiseFlagSent;
  int linuxAICommandOtherSent;
  int linuxAILastCommandKind;
  int linuxAILastCommandHeroObjectId;
  int linuxAILastCommandPlayerId;
  int linuxAILastCommandUserId;
  int linuxAILastCommandTargetObjectId;
  int linuxAILastCommandSent;
  bool linuxAutoAIEnabled;
#endif

public:
  ZEND int operator&( IBinSaver &f ) { f.Add(1,(PF_Core::World*)this); f.Add(2,&pTileMap); f.Add(3,&warFog); f.Add(4,&pResolver); f.Add(5,&pAIWorld); f.Add(6,&pAIContainer); f.Add(7,&pNatureMap); f.Add(8,&pStatistics); f.Add(9,&mapSize); f.Add(10,&players); f.Add(11,&deadUnits); f.Add(12,&mainBuildings); f.Add(13,&step); f.Add(14,&timeElapsed); f.Add(15,&randGen); f.Add(16,&smartRandGen); f.Add(17,&manualGameFinish); f.Add(18,&humanPlayersCount); f.Add(19,&triggerMarkerHandler); f.Add(20,&totalCreepsCount); f.Add(21,&allScriptFunctionsEnabled); f.Add(22,&fPUStatesData); f.Add(23,&stepLength); f.Add(24,&stepLengthInSeconds); f.Add(25,&defeatedFaction); f.Add(26,&protection); f.Add(27,&dayNightController); f.Add(28,&timeScale); return 0; }
  
  PFWorld( const NCore::MapStartInfo & info, NScene::IScene * _scene, NGameX::IAdventureScreen * _screen,
            PFResourcesCollection * _collection, int _stepLength, bool _aiForLeaversEnabled, int _aiForLeaversThreshold );
  virtual void OnDestroyContents();

  void SetDebugRender( Render::IDebugRender * ptr ) { debugRender = ptr; }

  MapLoadingControllerPtr GetMapLoadingController() const { return mapLoadingController; }
  void SetMapLoadingController(const MapLoadingControllerPtr& ptr) { mapLoadingController = ptr; }

  bool LoadMap(const NDb::AdvMapDescription * advMapDescription, const NDb::AdventureCameraSettings * cameraSettings, const NCore::TPlayersStartInfo & playersInfo, LoadingProgress * progress, bool isReconnecting, const NWorld::PFResourcesCollection::TalentMap & talents );

  PFResourcesCollection* GetResourcesCollection() 
  { 
    return (IsValid(resourcesCollection) ? resourcesCollection : NULL); 
  }
	WorldChecker* GetWorldChecker()
	{
		return &worldChecker;
	}
  void Reset();
  void ResetClientObjects();

  float GetTimeScale() const { return timeScale; }
  void SetTimeScale(float scale) { timeScale = scale; }

  const NDb::AdvMapDescription *GetMapDescription() { return advMapDescription; }
  const NDb::AdvMapDescription *GetMapDescription() const { return advMapDescription; }
  TileMap           *GetTileMap() const { return pTileMap; }
  PFAIWorld         *GetAIWorld() const { return pAIWorld; }
  PFStatistics      *GetStatistics() { return pStatistics; }
  PFStatistics      *GetStatistics() const { return pStatistics; }
#if defined(PW_LINUX_NULL_RENDER)
  void NotifyItemTransferForLinuxBootstrap(PFBaseHero* from, PFBaseHero* to, const NDb::Consumable* dbItem);
#endif
  FogOfWar          *GetFogOfWar() const {return warFog;}
  PFWorldNatureMap  *GetNatureMap()     { return pNatureMap; }
  PFAIContainer     *GetAIContainer() const { return pAIContainer; }
  CollisionResolver *GetCollisionResolver() const { return pResolver; }
  const NDb::SoundAmbienceMap* GetAmbienceMap() { return ambienceMap; }
  void SetAIContainer( PFAIContainer* AIC ) { pAIContainer = AIC; }
  NGameX::IAdventureScreen * GetIAdventureScreen() {return adventureScreen;}
  NGameX::IAdventureScreen const* GetIAdventureScreen() const {return adventureScreen;}

  void RegisterCreep(const PFCommonCreep*) { ++totalCreepsCount; }
  void UnregisterCreep(const PFCommonCreep*);
  const int GetRegisteredCreepsCount() const { return totalCreepsCount; }  


  //pathfinding
  Pathfinding::CCommonPathFinder * GetPathFinder() {return pathFinder;}
  Pathfinding::RoutePathFinder * GetRoutPathfinder() {return routPathFinder;}

  void SetPathFinder( Pathfinding::CCommonPathFinder* _pathFinder ) { pathFinder = _pathFinder; }
  void SetRoutPathfinder( Pathfinding::RoutePathFinder* _pathFinder ) { routPathFinder = _pathFinder; }
  void SetFogOfWar( FogOfWar* _warFog ) { warFog = _warFog; }

  PF_Minigames::IMinigamesMain * GetMinigamesMain() { return minigamesMain; }

  PFWorldNatureMap const* GetNatureMap() const    { return pNatureMap; }

  NRandom::RandomGenerator * GetRndGen() { return &randGen; }
  SmartRandomGenerator* GetSmartRndGen() { return &smartRandGen; }

  PFPlayer*  GetPlayer(int id) const;
  PFPlayer*  GetPlayerByUID(int userId) const;
  const int  GetPlayersCount() const { return players.size(); }
  bool IsAllScriptFunctionsEnabled() const { return allScriptFunctionsEnabled; }
  const int  GetPresentPlayersCount() const;
  const int  GetPresentPlayersCount(NDb::EFaction faction) const;
#if defined( PW_LINUX_NULL_RENDER )
  int GetLinuxLoadedMapObjectsCount() const
  {
    return linuxLoadedWarFogUnblockObjects + linuxLoadedSimpleObjects + linuxLoadedMultiStateObjects + linuxLoadedTreeObjects + linuxLoadedGlyphSpawnerObjects + linuxLoadedAdvMapObstacleObjects + linuxLoadedHeroPlaceHolderObjects + linuxLoadedCreepSpawnerObjects + linuxLoadedNeutralCreepSpawnerObjects + linuxLoadedSimpleBuildingObjects + linuxLoadedUsableBuildingObjects + linuxLoadedShopObjects + linuxLoadedQuarterObjects + linuxLoadedTowerObjects + linuxLoadedControllableTowerObjects + linuxLoadedFountainObjects + linuxLoadedRoadFlagpoleObjects + linuxLoadedScriptedFlagpoleObjects + linuxLoadedMainBuildingObjects + linuxLoadedMinigamePlaceObjects + linuxLoadedCameraSplineObjects + linuxLoadedScriptPathObjects + linuxLoadedScriptPolygonAreaObjects;
  }
  int GetLinuxLoadedWarFogUnblockObjectsCount() const { return linuxLoadedWarFogUnblockObjects; }
  int GetLinuxLoadedSimpleObjectsCount() const { return linuxLoadedSimpleObjects; }
  int GetLinuxLoadedMultiStateObjectsCount() const { return linuxLoadedMultiStateObjects; }
  int GetLinuxLoadedTreeObjectsCount() const { return linuxLoadedTreeObjects; }
  int GetLinuxLoadedGlyphSpawnerObjectsCount() const { return linuxLoadedGlyphSpawnerObjects; }
  int GetLinuxLoadedAdvMapObstacleObjectsCount() const { return linuxLoadedAdvMapObstacleObjects; }
  int GetLinuxLoadedHeroPlaceHolderObjectsCount() const { return linuxLoadedHeroPlaceHolderObjects; }
  int GetLinuxLoadedCreepSpawnerObjectsCount() const { return linuxLoadedCreepSpawnerObjects; }
  int GetLinuxLoadedNeutralCreepSpawnerObjectsCount() const { return linuxLoadedNeutralCreepSpawnerObjects; }
  int GetLinuxLoadedSimpleBuildingObjectsCount() const { return linuxLoadedSimpleBuildingObjects; }
  int GetLinuxLoadedUsableBuildingObjectsCount() const { return linuxLoadedUsableBuildingObjects; }
  int GetLinuxLoadedShopObjectsCount() const { return linuxLoadedShopObjects; }
  int GetLinuxLoadedQuarterObjectsCount() const { return linuxLoadedQuarterObjects; }
  int GetLinuxLoadedTowerObjectsCount() const { return linuxLoadedTowerObjects; }
  int GetLinuxLoadedControllableTowerObjectsCount() const { return linuxLoadedControllableTowerObjects; }
  int GetLinuxLoadedFountainObjectsCount() const { return linuxLoadedFountainObjects; }
  int GetLinuxLoadedRoadFlagpoleObjectsCount() const { return linuxLoadedRoadFlagpoleObjects; }
  int GetLinuxLoadedScriptedFlagpoleObjectsCount() const { return linuxLoadedScriptedFlagpoleObjects; }
  int GetLinuxLoadedMainBuildingObjectsCount() const { return linuxLoadedMainBuildingObjects; }
  int GetLinuxLoadedMinigamePlaceObjectsCount() const { return linuxLoadedMinigamePlaceObjects; }
  int GetLinuxLoadedCameraSplineObjectsCount() const { return linuxLoadedCameraSplineObjects; }
  int GetLinuxLoadedScriptPathObjectsCount() const { return linuxLoadedScriptPathObjects; }
  int GetLinuxLoadedScriptPolygonAreaObjectsCount() const { return linuxLoadedScriptPolygonAreaObjects; }
  int GetLinuxLastSteppedSpawnerObjectsCount() const { return linuxLastSteppedSpawnerObjects; }
  int GetLinuxLastSteppedCreepSpawnerObjectsCount() const { return linuxLastSteppedCreepSpawnerObjects; }
  int GetLinuxLastSteppedNeutralCreepSpawnerObjectsCount() const { return linuxLastSteppedNeutralCreepSpawnerObjects; }
  int GetLinuxSpawnedHeroObjectsCount() const { return linuxSpawnedHeroObjects; }
  int GetLinuxPlayersWithHeroObjectsCount() const { return linuxPlayersWithHeroObjects; }
  void RegisterLinuxExecutedPackedWorldCommand(int clientId);
  void RegisterLinuxExecutedPackedWorldCommand(int clientId, DWORD commandTypeId);
  void RegisterLinuxBootstrapRuntimeCommand(int clientId, int token, float value);
  int GetLinuxExecutedPackedWorldCommandsCount() const { return linuxExecutedPackedWorldCommands; }
  int GetLinuxLastPackedWorldCommandClientId() const { return linuxLastPackedWorldCommandClientId; }
  DWORD GetLinuxLastPackedWorldCommandTypeId() const { return linuxLastPackedWorldCommandTypeId; }
  int GetLinuxBootstrapRuntimeCommandsCount() const { return linuxBootstrapRuntimeCommands; }
  int GetLinuxLastBootstrapRuntimeCommandClientId() const { return linuxLastBootstrapRuntimeCommandClientId; }
  int GetLinuxLastBootstrapRuntimeCommandToken() const { return linuxLastBootstrapRuntimeCommandToken; }
  float GetLinuxLastBootstrapRuntimeCommandValue() const { return linuxLastBootstrapRuntimeCommandValue; }
  int GetLinuxStoredDeadUnitsCount() const { return linuxStoredDeadUnits; }
  int GetLinuxCleanedDeadUnitsCount() const { return linuxCleanedDeadUnits; }
  int GetLinuxPendingDeadUnitsCount() const { return deadUnits.size(); }
  int GetLinuxLastStoredDeadUnitObjectId() const { return linuxLastStoredDeadUnitObjectId; }
  int GetLinuxLastCleanedDeadUnitObjectId() const { return linuxLastCleanedDeadUnitObjectId; }
  int GetLinuxPlayerStatusUpdatesCount() const { return linuxPlayerStatusUpdates; }
  int GetLinuxPlayerStatusMissingCount() const { return linuxPlayerStatusMissing; }
  int GetLinuxPlayerStatusActiveUpdatesCount() const { return linuxPlayerStatusActiveUpdates; }
  int GetLinuxPlayerStatusAwayUpdatesCount() const { return linuxPlayerStatusAwayUpdates; }
  int GetLinuxPlayerStatusPlayingUpdatesCount() const { return linuxPlayerStatusPlayingUpdates; }
  int GetLinuxPlayerStatusDisconnectedUpdatesCount() const { return linuxPlayerStatusDisconnectedUpdates; }
  int GetLinuxPlayerStatusReconnectedUpdatesCount() const { return linuxPlayerStatusReconnectedUpdates; }
  int GetLinuxPlayerStatusLeaverUpdatesCount() const { return linuxPlayerStatusLeaverUpdates; }
  int GetLinuxLastPlayerStatusClientId() const { return linuxLastPlayerStatusClientId; }
  int GetLinuxLastPlayerStatusValue() const { return linuxLastPlayerStatusValue; }
  int GetLinuxLastPlayerStatusStep() const { return linuxLastPlayerStatusStep; }
  int GetLinuxLastPlayerStatusPlaying() const { return linuxLastPlayerStatusPlaying; }
  int GetLinuxLastPlayerStatusActive() const { return linuxLastPlayerStatusActive; }
  int GetLinuxLastPlayerStatusDisconnected() const { return linuxLastPlayerStatusDisconnected; }
  int GetLinuxLastPlayerStatusLeaver() const { return linuxLastPlayerStatusLeaver; }
  int GetLinuxAIAutoStartAttempts() const { return linuxAIAutoStartAttempts; }
  int GetLinuxAIAutoStartSuccesses() const { return linuxAIAutoStartSuccesses; }
  int GetLinuxAIAddRequests() const { return linuxAIAddRequests; }
  int GetLinuxAIAddSuccesses() const { return linuxAIAddSuccesses; }
  int GetLinuxAIRemoveRequests() const { return linuxAIRemoveRequests; }
  int GetLinuxAIRemoveSuccesses() const { return linuxAIRemoveSuccesses; }
  int GetLinuxAIStepCalls() const { return linuxAIStepCalls; }
  int GetLinuxAIControllerCount() const { return linuxAIControllerCount; }
  int GetLinuxAIBotsSettingsAvailable() const { return linuxAIBotsSettingsAvailable; }
  int GetLinuxAIBotsEnabled() const { return linuxAIBotsEnabled; }
  int GetLinuxAILastHeroObjectId() const { return linuxAILastHeroObjectId; }
  int GetLinuxAILastPlayerId() const { return linuxAILastPlayerId; }
  int GetLinuxAILastUserId() const { return linuxAILastUserId; }
  int GetLinuxAILastLine() const { return linuxAILastLine; }
  enum LinuxAICommandKind
  {
    LinuxAICommandUnknown = 0,
    LinuxAICommandMove = 1,
    LinuxAICommandCombatMove = 2,
    LinuxAICommandStop = 3,
    LinuxAICommandFollow = 4,
    LinuxAICommandAttack = 5,
    LinuxAICommandActivateTalent = 6,
    LinuxAICommandUseTalent = 7,
    LinuxAICommandBuyConsumable = 8,
    LinuxAICommandUseConsumable = 9,
    LinuxAICommandUsePortal = 10,
    LinuxAICommandPickupObject = 11,
    LinuxAICommandRaiseFlag = 12
  };
  void RecordLinuxAICommand(LinuxAICommandKind kind, const PFBaseHero* hero, const PFLogicObject* target, bool sent);
  void RecordLinuxAICommandDirectFallback(LinuxAICommandKind kind, const PFBaseHero* hero, const PFLogicObject* target);
  int GetLinuxAICommandAttempts() const { return linuxAICommandAttempts; }
  int GetLinuxAICommandsSent() const { return linuxAICommandsSent; }
  int GetLinuxAICommandDirectFallbacks() const { return linuxAICommandDirectFallbacks; }
  int GetLinuxAICommandMoveSent() const { return linuxAICommandMoveSent; }
  int GetLinuxAICommandCombatMoveSent() const { return linuxAICommandCombatMoveSent; }
  int GetLinuxAICommandStopSent() const { return linuxAICommandStopSent; }
  int GetLinuxAICommandFollowSent() const { return linuxAICommandFollowSent; }
  int GetLinuxAICommandAttackSent() const { return linuxAICommandAttackSent; }
  int GetLinuxAICommandActivateTalentSent() const { return linuxAICommandActivateTalentSent; }
  int GetLinuxAICommandUseTalentSent() const { return linuxAICommandUseTalentSent; }
  int GetLinuxAICommandBuyConsumableSent() const { return linuxAICommandBuyConsumableSent; }
  int GetLinuxAICommandUseConsumableSent() const { return linuxAICommandUseConsumableSent; }
  int GetLinuxAICommandUsePortalSent() const { return linuxAICommandUsePortalSent; }
  int GetLinuxAICommandPickupObjectSent() const { return linuxAICommandPickupObjectSent; }
  int GetLinuxAICommandRaiseFlagSent() const { return linuxAICommandRaiseFlagSent; }
  int GetLinuxAICommandOtherSent() const { return linuxAICommandOtherSent; }
  int GetLinuxAILastCommandKind() const { return linuxAILastCommandKind; }
  int GetLinuxAILastCommandHeroObjectId() const { return linuxAILastCommandHeroObjectId; }
  int GetLinuxAILastCommandPlayerId() const { return linuxAILastCommandPlayerId; }
  int GetLinuxAILastCommandUserId() const { return linuxAILastCommandUserId; }
  int GetLinuxAILastCommandTargetObjectId() const { return linuxAILastCommandTargetObjectId; }
  int GetLinuxAILastCommandSent() const { return linuxAILastCommandSent; }
  void SetLinuxAutoAIEnabled(bool enabled) { linuxAutoAIEnabled = enabled; }
  bool IsLinuxAutoAIEnabled() const { return linuxAutoAIEnabled; }
  int GetLinuxRegisteredCreepObjectsCount() const { return GetRegisteredCreepsCount(); }
  int GetLinuxSpawnedNeutralCreepObjectsCount();
  int GetLinuxMovingCommonCreepObjectsCount();
  int GetLinuxMovedCommonCreepObjectsCount();
  float GetLinuxCommonCreepMovementDistance();
  void GetLinuxDynamicWorldMarkers(vector<LinuxDynamicWorldMarker>& markers, int maxMarkers);
  PFBaseUnit* FindLinuxUnitByObjectId(int objectId);
  int GetLinuxCreepSpawnerWavesCount();
  int GetLinuxNeutralCreepSpawnerWavesCount();
  int GetLinuxReadyCreepSpawnerObjectsCount();
  int GetLinuxReadyNeutralCreepSpawnerObjectsCount();
  int GetLinuxEnabledCreepSpawnerObjectsCount();
  int GetLinuxEnabledNeutralCreepSpawnerObjectsCount();
  int GetLinuxContentCreepSpawnerObjectsCount();
  int GetLinuxContentNeutralCreepSpawnerObjectsCount();
  int GetLinuxAICreepSpawnEnabled() const;
  int GetLinuxAINeutralCreepSpawnEnabled() const;
  int GetLinuxAIMaxCreepsCount() const;
  float GetLinuxMinCreepSpawnerSpawnDelay();
  float GetLinuxMinNeutralCreepSpawnerSpawnDelay();
  PFShop* FindLinuxFirstShopForHero(PFBaseHero const* hero, int* outConsumableIndex);
  PFBaseUnit* FindLinuxFirstUsableUnitForHero(PFBaseHero const* hero);
  PFFlagpole* FindLinuxFirstRaisableFlagpoleForHero(PFBaseHero const* hero);
  PFFlagpole* FindLinuxNearestRaisableFlagpoleForHero(PFBaseHero const* hero, float maxDistance);
  PFMinigamePlace* FindLinuxFirstAvailableMinigamePlaceForHero(PFBaseHero const* hero);
  PFMinigamePlace* FindLinuxFirstForeignMinigamePlaceForHero(PFBaseHero const* hero);
  PFPickupableObjectBase* FindLinuxFirstPickupableForHero(PFBaseHero const* hero);
  PFPickupableObjectBase* FindLinuxNearestPickupableForHero(PFBaseHero const* hero, float maxDistance);
#endif
  virtual int GetStepLength() const { return stepLength; }
  virtual float GetStepLengthInSeconds() const { return stepLengthInSeconds; }

  virtual void UpdatePlayerStatuses(const NCore::TStatuses & statuses);

  virtual void ExecuteCommands(const NCore::TPackedCommands & commands);

  virtual bool Step(float dtInSeconds, float dtLocal);
  virtual void CalcCRC( IBinSaver& f, bool fast );

  virtual int GetStepNumber() const { return step; }

  void SetMapSize(CVec2 const &_mapSize) { mapSize = _mapSize; }
  CVec2 const& GetMapSize() const { return mapSize; }

  NScene::IScene *GetScene() const { return pScene; }
  float GetTimeElapsed() const {return timeElapsed;}

	void StoreDeadUnit(PFBaseUnit *pUnit);
  
  static NCore::IWorldBase* CreatePFWorld()
  {
    return new NWorld::PFWorld();
  }

  void OnGameFinished( NDb::EFaction failedFaction );
  void GameFinish( NDb::EFaction failedFaction );

  void SetManualGameFinish(bool _manualGameFinish) { manualGameFinish = _manualGameFinish; }
  bool GetManualGameFinish() const { return manualGameFinish; }

  Render::IDebugRender *GetDebugRender() { return debugRender; }

  virtual bool CanCreateClients();

  virtual void Save() const { worldChecker.Save(); }
  virtual void Load() { worldChecker.Load(); }

  virtual void StopMovingUnits();

  void SyncFPUStart( nfpu::ActionType actionType );
  void SyncFPUEnd( nfpu::ActionType actionType );

  void AddAI( PFBaseHero* hero, int lineNumber );
  void RemoveAI( PFBaseHero* hero );

  const NDb::BotsSettings * GetBotsSettings() const;

  void NotifyTalentCastProcessed(const NWorld::PFTalent*);
  void NotifyConsumableProcessed(const NWorld::PFConsumableAbilityData*);
  void NotifyCreepSpawnerCleaned( const NWorld::PFNeutralCreepSpawner* pSpawner, const NWorld::PFBaseUnit* pKiller );

  bool IsFactionDefeated(const NDb::EFaction& faction);
  void SetDefeatedFaction(const NDb::EFaction& faction);

  virtual bool HasProtection() const
  {
#ifndef STARFORCE_PROTECTED
    return false;
#else
    return true;
#endif
  }

  virtual bool PollProtectionResult(NCore::ProtectionResult& result);
  virtual void SetProtectionUpdateFrequency(const int offset, const int frequency);

  const CObj<DayNightController>& GetDayNightController() const { return dayNightController; }

  bool IsDay() const;
  bool IsNight() const;
private:
  void LockOutsideCameraArea(const NDb::AdventureCameraSettings * cameraSettings);
  void InitMinigames();
  void KillDeadUnits( bool fullCleanup = false );
  void LoadPrecachedResources(const NDb::AdvMapDescription * advMapDescription);
  bool LoadSceneMapObjects( const NDb::AdvMapDescription* advMapDesc, const NCore::TPlayersStartInfo & players, const bool isTutorial, LoadingProgress * progress, const NWorld::PFResourcesCollection::TalentMap& talents );

  bool CanTrackPlayersBehaviour(const NCore::MapStartInfo& msi) const;

  friend WorldChecker;

  STARFORCE_EXPORT void ProtectionCheck();
};
  
} //namespace NWorld
