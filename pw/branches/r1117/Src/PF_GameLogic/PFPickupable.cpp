#include "stdafx.h"
#include "PFPickupable.h"
#include "PFClientLogicObject.h"
#include "PFBaseUnit.h"


namespace NWorld
{

PFPickupableObjectBase::PFPickupableObjectBase( const CPtr<PFWorld>& pWorld, const CVec3& pos, const NDb::GameObject* objectDesc )
  : PFLogicObject( pWorld, pos, objectDesc ), isBeingPickuped(false)
{
  InitData data;
  data.faction = NDb::FACTION_NEUTRAL;
  data.playerId = 0xffffffff;
  data.type = NDb::UNITTYPE_PICKUPABLE;
  PFLogicObject::Initialize(data);
}

bool PFPickupableObjectBase::PickUp( PFBaseUnit* pPicker )
{
  isBeingPickuped = false;
  const bool canBePickedUp = CanBePickedUpBy(pPicker);
  if ( canBePickedUp )
  {
    OnPickedUp(pPicker);
    Die();
  }

  return canBePickedUp;
}

bool PFPickupableObjectBase::CanBePickedUpBy( const PFBaseUnit* pPicker ) const
{
#if defined( PW_LINUX_NULL_RENDER )
  return isBeingPickuped == false && pPicker && pPicker->CheckFlag( NDb::UNITFLAG_FORBIDINTERACT ) == false;
#else
  return isBeingPickuped == false && pPicker->CheckFlag( NDb::UNITFLAG_FORBIDINTERACT ) == false;
#endif
}

bool PFPickupableObjectBase::Step(float dt)
{
  NI_PROFILE_FUNCTION
#if !defined( PW_LINUX_NULL_RENDER )
  CALL_CLIENT(UpdateVisibility);
#endif
  return PFLogicObject::Step(dt);
}

} //namespace NWorld
