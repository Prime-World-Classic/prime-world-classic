#include "stdafx.h"

#include "TriggerMarkerHandler.h"
#include "DBAdvMap.h"
#include "PFLogicObject.h"
#include "PFAIContainer.h"
#include "PFAIWorld.h"
#include "TileMap.h"
#include "WarFog.h"

namespace NWorld
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TriggerMarkerHandler::UnitAccumulatorFunctor::UnitAccumulatorFunctor( const CVec2 &pos_, float range_, NDb::ESpellTarget _targetType )
: pos(pos_), range(range_), targetType(_targetType)
{
  counterUnitFaction[ NDb::FACTION_NEUTRAL ] = 0;
  counterUnitFaction[ NDb::FACTION_FREEZE ] = 0;
  counterUnitFaction[ NDb::FACTION_BURN ] = 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void TriggerMarkerHandler::UnitAccumulatorFunctor::operator()( NWorld::PFLogicObject& unit )
{
  if ( unit.IsInRange( pos, range ) && (targetType & (1L << unit.GetUnitType())) != 0 )
    counterUnitFaction[unit.GetFaction()]++;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TriggerMarkerHandler::TriggerMarkerHandler( PFWorld* pWorld, NDb::AdvMapSettings const * _advMapSettings )
  : PFWorldObjectBase( pWorld, 0 )
  , advMapSettings(_advMapSettings)
#if defined(PW_LINUX_NULL_RENDER)
  , linuxStepCalls(0)
  , linuxTriggerScriptAreasFound(0)
  , linuxMarkerScriptAreasFound(0)
  , linuxFogFillCalls(0)
  , linuxFogClearCalls(0)
  , linuxTriggerMarkerBindings(_advMapSettings ? _advMapSettings->triggerMarkerBinding.size() : 0)
  , linuxTriggerMarkerStates(0)
  , linuxFogVisibilityChanges(0)
  , linuxLastFogVisibilityBefore(-1)
  , linuxLastFogVisibilityAfter(-1)
  , linuxLastFogVisibilityTeam(-1)
  , linuxLastFogVisibilityTileX(-1)
  , linuxLastFogVisibilityTileY(-1)
  , linuxLastFogRevisionBefore(0)
  , linuxLastFogRevisionAfter(0)
#endif
{
  if (!advMapSettings)
    return;

  for( vector<NDb::TriggerMarkerBinding>::const_iterator it = advMapSettings->triggerMarkerBinding.begin(), itEnd = advMapSettings->triggerMarkerBinding.end(); it != itEnd; ++it )
  {
    for( int k = 0; k < it->MarkerPoints.size(); k++ )
    {
      if( activeStates.find( it->MarkerPoints[k] ) == activeStates.end() )
      {
        activeStates[it->MarkerPoints[k]].resize( NDb::KnownEnum<NDb::EFaction>::SizeOf() );

        for( int i = 0; i < activeStates[it->MarkerPoints[k]].size(); i++ )
        {
          activeStates[it->MarkerPoints[k]][i].current = 0;
          activeStates[it->MarkerPoints[k]][i].previous = 0;
        }
      }
    }
  }
#if defined(PW_LINUX_NULL_RENDER)
  linuxTriggerMarkerStates = activeStates.size();
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void TriggerMarkerHandler::ApplyMarkerVisibility(FogOfWar* fog, const SVector& tile, int radius, NDb::EFaction faction, bool unmark)
{
  if (!fog)
    return;

#if defined(PW_LINUX_NULL_RENDER)
  linuxLastFogRevisionBefore = fog->GetRevision();
  const int visTileSize = fog->GetVisTileSize() > 0 ? fog->GetVisTileSize() : 1;
  const SVector visTile(tile.x / visTileSize, tile.y / visTileSize);
  const FogOfWar::VisMap* visMap = fog->GetVisMap(static_cast<uint>(faction));
  const bool canReadCenter = visMap &&
    visTile.x >= 0 &&
    visTile.y >= 0 &&
    visTile.x < visMap->GetSizeX() &&
    visTile.y < visMap->GetSizeY();
  const int visibilityBefore = canReadCenter ? (*visMap)[visTile.x][visTile.y] : -1;
#endif

  fog->FillVisibilityMap(tile, radius, faction, unmark);

#if defined(PW_LINUX_NULL_RENDER)
  if (unmark)
    ++linuxFogClearCalls;
  else
    ++linuxFogFillCalls;
  const int visibilityAfter = canReadCenter ? (*visMap)[visTile.x][visTile.y] : -1;
  if (visibilityBefore != visibilityAfter)
    ++linuxFogVisibilityChanges;
  linuxLastFogVisibilityBefore = visibilityBefore;
  linuxLastFogVisibilityAfter = visibilityAfter;
  linuxLastFogVisibilityTeam = faction;
  linuxLastFogVisibilityTileX = visTile.x;
  linuxLastFogVisibilityTileY = visTile.y;
  linuxLastFogRevisionAfter = fog->GetRevision();
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool TriggerMarkerHandler::Step(float)
{
#if defined(PW_LINUX_NULL_RENDER)
  ++linuxStepCalls;
#endif

  if (!advMapSettings || !GetWorld())
    return true;

  PFAIContainer* aiContainer = GetWorld()->GetAIContainer();
  PFAIWorld* aiWorld = GetWorld()->GetAIWorld();
  TileMap* tileMap = GetWorld()->GetTileMap();
  FogOfWar* fog = GetWorld()->GetFogOfWar();

  if (!aiContainer || !aiWorld || !tileMap || !fog)
    return true;

  for( vector<NDb::TriggerMarkerBinding>::const_iterator it = advMapSettings->triggerMarkerBinding.begin(), itEnd = advMapSettings->triggerMarkerBinding.end(); it != itEnd; ++it )
  {
    if ( const NDb::ScriptArea* scriptAreaTrigger = aiContainer->GetScriptArea( it->TriggerPoint.c_str() ) )
    {
      UnitAccumulatorFunctor func( scriptAreaTrigger->position, scriptAreaTrigger->radius, scriptAreaTrigger->targetType );
      
      aiWorld->ForAllInRange( CVec3(scriptAreaTrigger->position, 0.0f), scriptAreaTrigger->radius + aiWorld->GetMaxObjectSize() * 0.5f, func );

#if defined(PW_LINUX_NULL_RENDER)
      ++linuxTriggerScriptAreasFound;
#endif

      for( int k = 0; k < it->MarkerPoints.size(); k++ )
      {
        activeStates[it->MarkerPoints[k]][NDb::FACTION_NEUTRAL].current += func[NDb::FACTION_NEUTRAL];
        activeStates[it->MarkerPoints[k]][NDb::FACTION_FREEZE].current += func[NDb::FACTION_FREEZE];
        activeStates[it->MarkerPoints[k]][NDb::FACTION_BURN].current += func[NDb::FACTION_BURN];
      }
    }
  }

  for( ActiveStates::iterator it = activeStates.begin(), itEnd = activeStates.end(); it != itEnd; ++it )
  {
    if ( const NDb::ScriptArea* scriptAreaMarker = aiContainer->GetScriptArea( it->first.c_str() ) )
    {
      const SVector tile = tileMap->GetTile( scriptAreaMarker->position );

#if defined(PW_LINUX_NULL_RENDER)
      ++linuxMarkerScriptAreasFound;
#endif
      
      if( it->second[NDb::FACTION_FREEZE].current && !it->second[NDb::FACTION_FREEZE].previous )
        ApplyMarkerVisibility(fog, tile, scriptAreaMarker->radius, NDb::FACTION_FREEZE, false);

      if( it->second[NDb::FACTION_BURN].current && !it->second[NDb::FACTION_BURN].previous )
        ApplyMarkerVisibility(fog, tile, scriptAreaMarker->radius, NDb::FACTION_BURN, false);

      if( it->second[NDb::FACTION_NEUTRAL].current && !it->second[NDb::FACTION_NEUTRAL].previous )
        ApplyMarkerVisibility(fog, tile, scriptAreaMarker->radius, NDb::FACTION_NEUTRAL, false);

      if( !it->second[NDb::FACTION_FREEZE].current && it->second[NDb::FACTION_FREEZE].previous )
        ApplyMarkerVisibility(fog, tile, scriptAreaMarker->radius, NDb::FACTION_FREEZE, true);

      if( !it->second[NDb::FACTION_BURN].current && it->second[NDb::FACTION_BURN].previous )
        ApplyMarkerVisibility(fog, tile, scriptAreaMarker->radius, NDb::FACTION_BURN, true);

      if( !it->second[NDb::FACTION_NEUTRAL].current && it->second[NDb::FACTION_NEUTRAL].previous )
        ApplyMarkerVisibility(fog, tile, scriptAreaMarker->radius, NDb::FACTION_NEUTRAL, true);

    }

    it->second[NDb::FACTION_FREEZE].previous = it->second[NDb::FACTION_FREEZE].current;
    it->second[NDb::FACTION_FREEZE].current = 0;

    it->second[NDb::FACTION_BURN].previous = it->second[NDb::FACTION_BURN].current;
    it->second[NDb::FACTION_BURN].current = 0;

    it->second[NDb::FACTION_NEUTRAL].previous = it->second[NDb::FACTION_NEUTRAL].current;
    it->second[NDb::FACTION_NEUTRAL].current = 0;
  }

  return true;
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(TriggerMarkerHandler,  NWorld)
