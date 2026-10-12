#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFUniTarget.h"
#include "PFTargetSelector.h"
#include "PFBaseUnit.h"
#include "PFAbilityData.h"
#include "PFApplInstant.h"
#include "PFCastLimitations.h"
#include "../LinuxBootstrap/formula_context.h"
#include "PFWorld.h"
#include <cmath>

namespace NWorld
{

namespace
{
/** Selection uses a global native candidate cache; admission retains exact world ownership. */
bool LinuxCastTargetContext(const Target& target, PFWorld* world)
{
	if (!world || !target.IsValid(true)) return false;
	const CVec3 position = target.AcquirePosition();
	if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
	if (target.IsPosition()) return true;
	if (!target.IsObject() || !IsValid(target.GetObject()) || target.GetObject()->GetWorld() != world) return false;
	return !target.IsUnit() || world->FindLinuxUnitByObjectId(target.GetUnit()->GetObjectId()) == target.GetUnit().GetPtr();
}
}

const PFAbilityData* CheckTargetLimitation(NDb::TargetCastLimitation const &dbLimit, CastLimitationsCheckParams const &cp)
{
  if (!cp.pAbility || !cp.pTarget || !dbLimit.targetSelector || !IsValid(cp.pAbility->GetOwner()))
    return 0;
	PFWorld* world = cp.pAbility->GetOwner()->GetWorld();
	if (!LinuxCastTargetContext(Target(cp.pAbility->GetOwner().GetPtr()), world)
		|| !LinuxCastTargetContext(*cp.pTarget, world)) return 0;

  CObj<PFTargetSelector> pTS = dbLimit.targetSelector->Create(0);
  if (!pTS)
    return 0;

  struct CalcTargets : public ITargetAction
  {
		explicit CalcTargets(PFWorld* world) : world(world), found(false) {}
		PFWorld* world;
		bool found;
		virtual void operator()(const Target& target) { found = found || LinuxCastTargetContext(target, world); }
  } f(world);

  PFTargetSelector::RequestParams rp(cp.pAbility->GetOwner(), cp.pAbility, *cp.pTarget);
  pTS->EnumerateTargets(f, rp);
  return f.found ? cp.pAbility : 0;
}

const PFAbilityData* CheckConditionLimitation(NDb::ConditionCastLimitation const &dbLimit, CastLimitationsCheckParams const &cp)
{
	if (!cp.pAbility || !cp.pTarget || !IsValid(cp.pAbility->GetOwner())) return 0;
	const PFBaseUnit* owner = cp.pAbility->GetOwner();
	if (!owner->GetWorld()) return 0;
	const PFLogicObject* receiver = owner;
	if (cp.pTarget->IsPosition())
	{
		const CVec3& position = cp.pTarget->GetPosition();
		if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return 0;
	}
	else
	{
		if (!cp.pTarget->IsObject() || !IsValid(cp.pTarget->GetObject())) return 0;
		receiver = cp.pTarget->GetObject();
		if (receiver->GetWorld() != owner->GetWorld()) return 0;
	}
	// Windows substitutes the owner for ground targets and defaults only an absent condition to true.
	const auto& expression = dbLimit.condition;
	const auto result = expression.sString.empty() && expression.compiledString.empty()
		? LinuxBootstrap::NumericFormulaResult{LinuxBootstrap::NumericFormulaError::None, 1.f}
		: LinuxBootstrap::EvaluateUnitNumericFormula(expression.sString.c_str(), owner, receiver, cp.pAbility);
	return result.Succeeded() && result.value != 0 ? cp.pAbility : 0;
}

const PFAbilityData* CheckDispellLimitation(NDb::DispellCastLimitation const&, CastLimitationsCheckParams const &cp)
{
  if (!cp.pAbility || !cp.pAbility->GetOwner())
    return 0;

  vector<PFBaseApplicator*> statuses;
  PFBaseUnit* pAbilityOwner = cp.pAbility->GetOwner();
  const int count = PFApplDispell::SearchStatus2Dispell(
    vector<const PFBaseUnit*>(1, pAbilityOwner),
    pAbilityOwner->GetOppositeFactionFlags(),
    Target(pAbilityOwner),
    statuses);
  return count > 0 ? cp.pAbility : 0;
}

const PFAbilityData* CheckPositionLimitation(NDb::PositionCastLimitation const& dbLimit, CastLimitationsCheckParams const& cp)
{
	if (!cp.pAbility || !cp.pTarget || !IsValid(cp.pAbility->GetOwner())) return 0;
	PFWorld* world = cp.pAbility->GetOwner()->GetWorld();
	if ((!cp.pTarget->IsPosition() && !cp.pTarget->IsUnit()) ||
		!LinuxCastTargetContext(Target(cp.pAbility->GetOwner().GetPtr()), world) ||
		!LinuxCastTargetContext(*cp.pTarget, world)) return 0;
	// Unlike ConditionCastLimitation, Windows uses a custom position receiver and defaults false.
	const PFPositionObject position(cp.pTarget->AcquirePosition());
	const auto result = LinuxBootstrap::EvaluatePositionNumericFormula(dbLimit.condition.sString.c_str(), position, *cp.pAbility);
	return result.Succeeded() && result.value != 0 ? cp.pAbility : 0;
}

}

#else

#include "PFUniTarget.h"
#include "PFApplInstant.h"
#include "PFTargetSelector.h"
#include "PFBaseUnit.h"
#include "PFAbilityData.h"
#include "PFCastLimitations.h"

namespace NWorld
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const PFAbilityData* CheckTargetLimitation(NDb::TargetCastLimitation const &dbLimit, CastLimitationsCheckParams const &cp)
{
  CObj<PFTargetSelector> pTS = dbLimit.targetSelector->Create(0);

  struct CalcTargets : public ITargetAction
  {
    CalcTargets() : count(0) {}
    int count;
    virtual void operator()(const Target &target) { count++; }
  } f;

  PFTargetSelector::RequestParams rp(cp.pAbility->GetOwner(), cp.pAbility, *cp.pTarget);
  pTS->EnumerateTargets(f, rp);

  return f.count > 0 ? cp.pAbility : 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const PFAbilityData* CheckConditionLimitation(NDb::ConditionCastLimitation const &dbLimit, CastLimitationsCheckParams const &cp)
{
	PFLogicObject *pObject = cp.pTarget->IsPosition() ? cp.pAbility->GetOwner().GetPtr() : cp.pTarget->GetObject();

	bool res = dbLimit.condition(cp.pAbility->GetOwner(), pObject, cp.pAbility, true);

	return res ? cp.pAbility : 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const PFAbilityData* CheckDispellLimitation(NDb::DispellCastLimitation const &dbLimit, CastLimitationsCheckParams const &cp)
{
  vector<PFBaseApplicator*> statuses;
  PFBaseUnit* pAbilityOwner = cp.pAbility->GetOwner();
  int count = PFApplDispell::SearchStatus2Dispell( vector<const PFBaseUnit*>( 1, pAbilityOwner ), 
                                                  cp.pAbility->GetOwner()->GetOppositeFactionFlags(), Target( pAbilityOwner ), statuses );
  return count > 0 ? cp.pAbility : 0;
}

const PFAbilityData* CheckPositionLimitation( NDb::PositionCastLimitation const& dbLimit, CastLimitationsCheckParams const& cp )
{
  const CVec3 pos = cp.pTarget->IsPosition() ? cp.pTarget->GetPosition() : cp.pTarget->GetUnit()->GetPosition() ;
  const PFPositionObject* pObject = new PFPositionObject(pos);

  bool allow = dbLimit.condition(cp.pAbility->GetOwner(), pObject, cp.pAbility, false);
  delete pObject;

  return allow ? cp.pAbility : 0;
}
}

#endif
