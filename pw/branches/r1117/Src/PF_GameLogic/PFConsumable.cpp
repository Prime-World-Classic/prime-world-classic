#include "stdafx.h"


#if defined( PW_LINUX_NULL_RENDER )

#include "PFConsumable.h"
#include "DBConsumable.h"

namespace NWorld
{

NAMEMAP_BEGIN(PFConsumable)
  NAMEMAP_FUNC_RO(moneyCost, &PFConsumable::GetMoneyCost )
NAMEMAP_END

PFConsumable::PFConsumable(CObj<PFConsumableAbilityData> const& abilityData, int quantity_)
: PFWorldObjectBase(0, 0)
, moneyCost(0)
, actionBarIndex(-1)
, isPet(false)
, pAbilityData(abilityData)
, quantity(quantity_)
{
}

PFConsumable::PFConsumable(CPtr<PFWorld> const& pWorld, CPtr<PFBaseUnit> const& pOwner, NDb::Ptr<NDb::Consumable> const& dbDesc)
: PFWorldObjectBase(pWorld, 0)
, moneyCost(0)
, actionBarIndex(-1)
, isPet(false)
, pDBDesc(dbDesc)
, quantity(1)
{
  (void)pOwner;
}

PFConsumable::PFConsumable()
: moneyCost(0)
, quantity(-1)
, actionBarIndex(-1)
, isPet(false)
{
}

int PFConsumable::GetMaxQuantity() const
{
  return pDBDesc ? pDBDesc->maxQuantity : 0;
}

int const& PFConsumable::GetMoneyCost() const
{
  moneyCost = pDBDesc ? pDBDesc->naftaCost : 0;
  return moneyCost;
}

void PFConsumable::AddQuantity(int adds)
{
  if (!IsSpendable())
    return;
  quantity = min(adds + quantity, GetMaxQuantity());
}

bool PFConsumable::CanBeUsed() const
{
  return false;
}

bool PFConsumable::IsSpendable() const
{
  return pDBDesc && pDBDesc->maxQuantity > 0;
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFConsumable, NWorld);

#else
#include "PFConsumable.h"

#include "DBConsumable.h"

#include "PFAbilityData.h"

namespace NWorld
{

NAMEMAP_BEGIN(PFConsumable)
  NAMEMAP_INCLUDE(pAbilityData)
  NAMEMAP_FUNC_RO(moneyCost, &PFConsumable::GetMoneyCost )
NAMEMAP_END

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PFConsumable::PFConsumable(CObj<PFConsumableAbilityData> const& pAbilityData, int quantity) 
: PFWorldObjectBase( pAbilityData->GetWorld(), 0 )
, moneyCost( 0 )
, quantity(quantity)
, pAbilityData(pAbilityData)
, pDBDesc( dynamic_cast<NDb::Consumable const*>(pAbilityData->GetDBDesc()) )
, actionBarIndex(-1)
, isPet(false)
{
  NI_VERIFY( pDBDesc, "Ability must be an consumable!", return; );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PFConsumable::PFConsumable( CPtr<PFWorld> const& pWorld, CPtr<PFBaseUnit> const& pOwner, NDb::Ptr<NDb::Consumable> const& pDBDesc) 
: PFWorldObjectBase( pWorld, 0 )
, moneyCost( 0 )
, pDBDesc(pDBDesc)
, pAbilityData( new PFConsumableAbilityData( pOwner, pDBDesc.GetPtr(), NDb::ABILITYTYPEID_CONSUMABLE, pWorld ? true : false ) )
, quantity( 1 )
, actionBarIndex(-1)
, isPet(false)
{
}

PFConsumable::PFConsumable()
: moneyCost(0)
, quantity(-1)
, actionBarIndex(-1)
, isPet(false)
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int PFConsumable::GetMaxQuantity() const
{
  return GetDBDesc()->maxQuantity;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int const &PFConsumable::GetMoneyCost() const
{ 
  moneyCost = (int)GetAbility()->GetModifiedValue( GetDBDesc()->naftaCost, NDb::ABILITYMODMODE_NAFTACOST );
  return moneyCost;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFConsumable::AddQuantity(int adds)
{
  if ( !IsSpendable() )
    return;

  int maxQuantity = GetMaxQuantity();
  quantity = min(adds+quantity, maxQuantity);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFConsumable::CanBeUsed() const
{
  return pAbilityData->CanBeUsed();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFConsumable::IsSpendable() const
{
  return pDBDesc->maxQuantity > 0;
}

} //namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFConsumable, NWorld);

#endif
