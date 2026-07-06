#pragma once

#include "PFWorldObjectBase.h"

namespace NWorld
{
class FogOfWar;
class PFLogicObject;

class TriggerMarkerHandler : public PFWorldObjectBase
{
  WORLD_OBJECT_METHODS( 0xEF94A3C0, TriggerMarkerHandler );
  
private:
  TriggerMarkerHandler()
#if defined(PW_LINUX_NULL_RENDER)
  : linuxStepCalls(0)
  , linuxTriggerScriptAreasFound(0)
  , linuxMarkerScriptAreasFound(0)
  , linuxFogFillCalls(0)
  , linuxFogClearCalls(0)
  , linuxTriggerMarkerBindings(0)
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
  {}

  // Functor
  struct UnitAccumulatorFunctor : public NonCopyable
  {
    int counterUnitFaction[3];
    CVec2         pos;
    float         range;
    NDb::ESpellTarget targetType;

    UnitAccumulatorFunctor( const CVec2 &pos_, float range_, NDb::ESpellTarget _targetType );

    void operator()( NWorld::PFLogicObject& unit );

    int operator [] (int i) { return counterUnitFaction[i]; }
  };

  struct areaState
  {
    int current;
    int previous;
  };

  typedef hash_map< string, vector< areaState > > ActiveStates;

  void ApplyMarkerVisibility(FogOfWar* fog, const SVector& tile, int radius, NDb::EFaction faction, bool unmark);

  ZDATA_(PFWorldObjectBase)
  NDb::Ptr<NDb::AdvMapSettings> advMapSettings;
  ActiveStates activeStates;
#if defined(PW_LINUX_NULL_RENDER)
  unsigned linuxStepCalls;
  unsigned linuxTriggerScriptAreasFound;
  unsigned linuxMarkerScriptAreasFound;
  unsigned linuxFogFillCalls;
  unsigned linuxFogClearCalls;
  unsigned linuxTriggerMarkerBindings;
  unsigned linuxTriggerMarkerStates;
  unsigned linuxFogVisibilityChanges;
  int linuxLastFogVisibilityBefore;
  int linuxLastFogVisibilityAfter;
  int linuxLastFogVisibilityTeam;
  int linuxLastFogVisibilityTileX;
  int linuxLastFogVisibilityTileY;
  unsigned linuxLastFogRevisionBefore;
  unsigned linuxLastFogRevisionAfter;
#endif
public:
  ZEND int operator&( IBinSaver &f ) { f.Add(1,(PFWorldObjectBase*)this); f.Add(2,&advMapSettings); f.Add(3,&activeStates); return 0; }

  TriggerMarkerHandler( PFWorld* pWorld, NDb::AdvMapSettings const * _advMapSettings );

  virtual bool Step(float dt);
#if defined(PW_LINUX_NULL_RENDER)
  unsigned GetLinuxStepCalls() const { return linuxStepCalls; }
  unsigned GetLinuxTriggerScriptAreasFound() const { return linuxTriggerScriptAreasFound; }
  unsigned GetLinuxMarkerScriptAreasFound() const { return linuxMarkerScriptAreasFound; }
  unsigned GetLinuxFogFillCalls() const { return linuxFogFillCalls; }
  unsigned GetLinuxFogClearCalls() const { return linuxFogClearCalls; }
  unsigned GetLinuxTriggerMarkerBindings() const { return linuxTriggerMarkerBindings; }
  unsigned GetLinuxTriggerMarkerStates() const { return linuxTriggerMarkerStates; }
  unsigned GetLinuxFogVisibilityChanges() const { return linuxFogVisibilityChanges; }
  int GetLinuxLastFogVisibilityBefore() const { return linuxLastFogVisibilityBefore; }
  int GetLinuxLastFogVisibilityAfter() const { return linuxLastFogVisibilityAfter; }
  int GetLinuxLastFogVisibilityTeam() const { return linuxLastFogVisibilityTeam; }
  int GetLinuxLastFogVisibilityTileX() const { return linuxLastFogVisibilityTileX; }
  int GetLinuxLastFogVisibilityTileY() const { return linuxLastFogVisibilityTileY; }
  unsigned GetLinuxLastFogRevisionBefore() const { return linuxLastFogRevisionBefore; }
  unsigned GetLinuxLastFogRevisionAfter() const { return linuxLastFogRevisionAfter; }
#endif
 };
} // namespace NWorld
