#pragma once

#include "PFWorldObjectBase.h"

namespace NWorld
{
class PFLogicObject;

class TriggerMarkerHandler : public PFWorldObjectBase
{
  WORLD_OBJECT_METHODS( 0xEF94A3C0, TriggerMarkerHandler );
  
private:
  TriggerMarkerHandler() {}

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

  ZDATA_(PFWorldObjectBase)
  NDb::Ptr<NDb::AdvMapSettings> advMapSettings;
  ActiveStates activeStates;
public:
  ZEND int operator&( IBinSaver &f ) { f.Add(1,(PFWorldObjectBase*)this); f.Add(2,&advMapSettings); f.Add(3,&activeStates); return 0; }

  TriggerMarkerHandler( PFWorld* pWorld, NDb::AdvMapSettings const * _advMapSettings );

  virtual bool Step(float dt);
 };
} // namespace NWorld
