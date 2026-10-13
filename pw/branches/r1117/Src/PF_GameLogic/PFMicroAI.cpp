#include "stdafx.h"
#include "PFAbilityData.h"
#include "PFBaseUnit.h"
#include "PFAIWorld.h"

#include "PFMicroAI.h"
#include "PFTargetSelector.h"
#include "TargetSelectorHelper.hpp"

namespace
{
  static bool g_dump_micro_ai_ts = false;

}

#if defined( PW_LINUX_DB_BOOTSTRAP )

#include "PFBehaviour.h"
#include "PFCreature.h"
#include "../LinuxBootstrap/formula_context.h"
#include <cmath>
#include <limits>

namespace NWorld
{

void GetLinuxBootstrapUnits(vector<CPtr<PFBaseUnit> >& units);

namespace
{
	/** Preserve absent=false and existing boolean spellings without losing formula errors. */
	static LinuxBootstrap::NumericFormulaResult LinuxMicroCondition(const ExecutableBoolString& expression,
		const PFBaseUnit* owner, const PFBaseUnit* target, const PFAbilityData* ability)
	{
		using namespace LinuxBootstrap;
		if (expression.sString.size() > NumericFormulaMaxLength) return {NumericFormulaError::LimitExceeded};
		if (expression.sString.empty())
			return expression.compiledString.empty() ? NumericFormulaResult{NumericFormulaError::None, 0} :
				NumericFormulaResult{NumericFormulaError::UnsupportedSyntax};
		const char* literal = SkipExecutableStringSpaces(expression.sString.c_str());
		if (!strcmp(literal, "True") || !strcmp(literal, "TRUE")) return {NumericFormulaError::None, 1};
		if (!strcmp(literal, "False") || !strcmp(literal, "FALSE")) return {NumericFormulaError::None, 0};
		return EvaluateUnitNumericFormula(expression.sString.c_str(), owner, target, ability);
	}

  static void AddUniqueUnit(vector<CPtr<PFBaseUnit> >& units, PFBaseUnit* unit)
  {
    if (!unit)
      return;
    for (vector<CPtr<PFBaseUnit> >::const_iterator it = units.begin(); it != units.end(); ++it)
      if (it->GetPtr() == unit)
        return;
    units.push_back(unit);
  }

  static void CollectCandidateUnits(vector<CPtr<PFBaseUnit> >& units, PFBaseUnit* owner)
  {
    GetLinuxBootstrapUnits(units);
    AddUniqueUnit(units, owner);
    if (owner)
      AddUniqueUnit(units, owner->GetCurrentTarget().GetPtr());
  }
}

bool PFBasicMicroAI::CheckConditions() const
{
	if (!GetDBBase() || !IsValid(pAbility) || !pAbility->GetDBDesc()) return false;
	const CPtr<PFBaseUnit>& unit = pAbility->GetOwner();
	if (!IsUnitValid(unit) || !unit->GetWorld() || pAbility->GetWorld() != unit->GetWorld()) return false;
	if (!pAbility->CanBeUsed()) return false;
	if (pAbility->GetType() == NDb::ABILITYTYPE_PASSIVE ||
		pAbility->GetType() == NDb::ABILITYTYPE_AUTOCASTABLE ||
		pAbility->GetType() == NDb::ABILITYTYPE_SIMPLE) return false;

	// Windows requires behavior even when the optional idle-only gate is disabled.
	if (!IsValid(unit->GetBehaviour()) || (GetDB().castWhenIdle && !unit->GetBehaviour()->IsInDefaultState())) return false;
	const LinuxBootstrap::NumericFormulaResult condition = LinuxMicroCondition(GetDB().condition, unit, unit, pAbility);
	if (!condition.Succeeded() || condition.value == 0) return false;

	const int abilityId = pAbility->GetAbilityTypeId() - NDb::ABILITYTYPEID_ABILITY0;
	if (abilityId >= 1 && abilityId < NDb::KnownEnum<NDb::EAbility>::sizeOf)
	{
		const PFCreature* creature = dynamic_cast<const PFCreature*>(unit.GetPtr());
		if (creature && (!creature->IsAbilityAvailable(abilityId) || !creature->CanUseAbility(abilityId) ||
			creature->IsAbilityInProgress(abilityId))) return false;
	}
	return true;
}

bool PFBasicMicroAI::GetTargetImpl(Target& target, const ITargetCondition* const condition) const
{
  if (!CheckConditions())
    return false;

  CPtr<PFBaseUnit> const& owner = pAbility->GetOwner();
  const NDb::ESpellTarget targetType = pAbility->GetTargetType();
  if (targetType == 0 || targetType == NDb::SPELLTARGET_SELF)
  {
    target = Target(owner);
    return CheckTargetCondition(target, condition, pAbility.GetPtr());
  }

  int factionFlags = 0;
  if (targetType & NDb::SPELLTARGET_ALLY)
    factionFlags |= 1 << owner->GetFaction();
  if (targetType & NDb::SPELLTARGET_ENEMY)
    factionFlags |= owner->GetOppositeFactionFlags();
  if (!factionFlags)
    factionFlags = owner->GetOppositeFactionFlags();

  float useRange = pAbility->GetUseRange();
  if (useRange <= 0.0f)
    useRange = owner->GetVisibilityRange() > 0.0f ? owner->GetVisibilityRange() : 10000.0f;

  vector<CPtr<PFBaseUnit> > units;
  CollectCandidateUnits(units, owner.GetPtr());
  UnitMaskingPredicate predicate(factionFlags, targetType);
  for (vector<CPtr<PFBaseUnit> >::iterator it = units.begin(); it != units.end(); ++it)
  {
    PFBaseUnit* unit = it->GetPtr();
    if (!IsUnitValid(unit) || unit == owner.GetPtr())
      continue;
    if (!owner->IsTargetInRange(unit, useRange))
      continue;
    if (!predicate(*unit))
      continue;

    Target candidate(unit);
    if (!CheckTargetCondition(candidate, condition, pAbility.GetPtr()))
      continue;

    target = candidate;
    return true;
  }

  return false;
}

PFTargetSelectorMicroAI::PFTargetSelectorMicroAI(CreateParams const &cp) : Base(cp)
{
  if (IsValid(GetDB().targetSelector))
    pTargetSelector = dynamic_cast<PFSingleTargetSelector*>(GetDB().targetSelector->Create(GetWorld()));
}

bool PFTargetSelectorMicroAI::GetTargetImpl(Target& target, const ITargetCondition* const condition) const
{
  if (!CheckConditions())
    return false;
  if (!IsValid(pTargetSelector))
    return PFBasicMicroAI::GetTargetImpl(target, condition);

  CPtr<PFBaseUnit> const& unit = pAbility->GetOwner();
  if (!IsValid(unit))
    return false;

	// RequestParams borrows its requester across both normal and debug selector calls.
	const Target requester(unit);
	PFTargetSelector::RequestParams rp(unit, static_cast<const IMiscFormulaPars*>(pAbility.GetPtr()), requester, condition);
  bool hasTarget = pTargetSelector->FindTarget(rp, target);

  if (hasTarget && g_dump_micro_ai_ts)
  {
    PFTargetSelector::s_DumpSelectors = true;
    Target dumpTarget;
    pTargetSelector->FindTarget(rp, dumpTarget);
    PFTargetSelector::s_DumpSelectors = false;
  }

  return hasTarget;
}

PFMultipleTargetSelectorMicroAI::PFMultipleTargetSelectorMicroAI(CreateParams const &cp) : Base(cp)
{
  if (IsValid(GetDB().targetSelector))
    pTargetSelector = dynamic_cast<PFMultipleTargetSelector*>(GetDB().targetSelector->Create(GetWorld()));
}

bool PFMultipleTargetSelectorMicroAI::GetTargetImpl(Target& target, const ITargetCondition* const condition) const
{
	if (!CheckConditions()) return false;
	if (!IsValid(pTargetSelector)) return PFBasicMicroAI::GetTargetImpl(target, condition);

	const CPtr<PFBaseUnit>& owner = pAbility->GetOwner();
	const NDb::Ptr<NDb::UnitTargetingParameters> unitPars = owner->GetTargetingParamsPtr();
	if (!unitPars || !std::isfinite(GetDB().minTargetWeight) || GetDB().minTargetCount < 0) return false;

	/** Match Windows' filter -> CanSelectTarget -> strict authored-weight comparison. */
	struct Counter : public ITargetAction, public NonCopyable
	{
		Counter(const PFBaseUnit* owner, const PFAbilityData* ability, const ExecutableBoolString& filter,
			const ITargetCondition* condition, const NDb::UnitTargetingParameters& unitPars, float minWeight)
			: owner(owner), ability(ability), filter(filter), condition(condition), unitPars(unitPars), minWeight(minWeight) {}

		void operator()(const Target& target) override
		{
			if (invalid || !target.IsUnit()) return;
			const CPtr<PFBaseUnit>& unit = target.GetUnit();
			if (!IsUnitValid(unit) || unit->GetWorld() != owner->GetWorld()) return;
			const LinuxBootstrap::NumericFormulaResult result = LinuxMicroCondition(filter, owner, unit, ability);
			if (!result.Succeeded()) { invalid = true; return; }
			if (result.value == 0 || !owner->CanSelectTarget(unit, true)) return;
			// Retain the existing native caller-supplied target condition as an extra gate.
			if (!CheckTargetCondition(target, condition, ability)) return;
			float weight = 0;
			if (!owner->TryGetTargetWeight(unit, unitPars, weight)) { invalid = true; return; }
			if (weight > minWeight && count < std::numeric_limits<int>::max()) ++count;
		}

		const PFBaseUnit* owner;
		const PFAbilityData* ability;
		const ExecutableBoolString& filter;
		const ITargetCondition* condition;
		const NDb::UnitTargetingParameters& unitPars;
		float minWeight;
		int count = 0;
		bool invalid = false;
	} counter(owner, pAbility, GetDB().unitFilter, condition, *unitPars, GetDB().minTargetWeight);

	// RequestParams borrows its requester; keep it alive through selector callbacks.
	const Target requester(owner);
	PFTargetSelector::RequestParams rp(owner, static_cast<const IMiscFormulaPars*>(pAbility.GetPtr()), requester, condition);
	pTargetSelector->EnumerateTargets(counter, rp);
	if (!counter.invalid && (GetDB().minTargetCount == 0 ? counter.count == 0 : counter.count >= GetDB().minTargetCount))
	{
		target.SetUnit(owner);
		return true;
	}
	return false;
}

} // namespace NWorld

REGISTER_DEV_VAR("dump_micro_ai_ts", g_dump_micro_ai_ts, STORAGE_NONE);

REGISTER_WORLD_OBJECT_NM(PFMicroAI, NWorld)
REGISTER_WORLD_OBJECT_NM(PFBasicMicroAI, NWorld)
REGISTER_WORLD_OBJECT_NM(PFTargetSelectorMicroAI, NWorld)
REGISTER_WORLD_OBJECT_NM(PFMultipleTargetSelectorMicroAI, NWorld)

#else

namespace NWorld
{

///////////////////////////////////////////////////////////////////////////////////////////////////
// Service functions to find units of desired type
///////////////////////////////////////////////////////////////////////////////////////////////////

template<class UnitType>
struct UnitCollector : NonCopyable
{
	UnitCollector(vector<UnitType*> &_objects)
	: objects(_objects)
	{}

	void operator()(PFLogicObject &baseUnit)
	{
		CDynamicCast<UnitType> unit = &baseUnit;
		if (unit)															// required type
			objects.push_back(unit);
	}

	vector<UnitType*> &objects;
};

static bool FindUnits(PFWorld* pWorld, const CVec3& pos, float range, int factionFlags, int targetType, vector<PFBaseUnit*>& objects)
{
	UnitCollector<PFBaseUnit> collector(objects);
	pWorld->GetAIWorld()->ForAllUnitsInRange(pos, range, collector, UnitMaskingPredicate(factionFlags, targetType));
	return objects.size() > 0;
}

static bool FindBuildings(PFWorld* pWorld, const CVec3& pos, float range, int factionFlags, int targetType, vector<PFLogicObject*>& objects)
{
	UnitCollector<PFLogicObject> collector(objects);
	pWorld->GetAIWorld()->ForAllObjectsInRange(pos, range, collector, UnitMaskingPredicate(factionFlags, targetType));
	return objects.size() > 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// PFBasicMicroAI
///////////////////////////////////////////////////////////////////////////////////////////////////

bool PFBasicMicroAI::CheckConditions() const
{
  NI_VERIFY(IsValid(pAbility), "Invalid ability", return false);

  // check usability of this ability
  if (!pAbility->CanBeUsed())
    return false;

  if (pAbility->GetType() == NDb::ABILITYTYPE_PASSIVE || pAbility->GetType() == NDb::ABILITYTYPE_AUTOCASTABLE || pAbility->GetType() == NDb::ABILITYTYPE_SIMPLE )
    return false;

  CPtr<PFBaseUnit> const & pUnit = pAbility->GetOwner();
  NI_VERIFY(IsValid(pUnit), "Invalid ability owner", return false);
  NI_VERIFY(IsValid(pUnit->GetBehaviour()), "Invalid ability owner behaviour", return false);

  if ( GetDB().castWhenIdle && !pUnit->GetBehaviour()->IsInDefaultState() )
    return false;

  // check activation condition
  if (!GetDB().condition(pUnit, pUnit, pAbility, false))
    return false;

  // check usability by unit
  int abilityId = pAbility->GetAbilityTypeId() - NDb::ABILITYTYPEID_ABILITY0;
  if (abilityId >= 1 && abilityId < NDb::KnownEnum<NDb::EAbility>::sizeOf)
  {
    PFCreature* pCreature = dynamic_cast<PFCreature*>(pUnit.GetPtr());
    if (pCreature && (!pCreature->IsAbilityAvailable(abilityId) || !pCreature->CanUseAbility(abilityId) || pCreature->IsAbilityInProgress(abilityId)))
      return false;
  }
  
  return true;
}

bool PFBasicMicroAI::GetTargetImpl(Target& target, const ITargetCondition* const condition) const
{
	if (!CheckConditions())
    return false;
  
  // get ability ownner
  NDb::ESpellTarget targetType = pAbility->GetTargetType();

  // find target for this ability

  PFBaseUnit *pUnit = pAbility->GetOwner();
  if (!pUnit)
    return false;

  // type 0
	if (targetType == 0 || targetType == NDb::SPELLTARGET_SELF)
	{
		//vector<PFBaseUnit*> objects;
		//if (!FindUnits(pUnit->GetWorld(), pUnit->GetPosition(), pUnit->GetAttackRange() * 1.5f /*pUnit->GetVisibilityRange()*/,
		//		pUnit->GetOppositeFactionFlags(), NDb::SPELLTARGET_ALL, objects))
		//{
		//	return false;		// some heuristic: cast these spells only when enemy units are available
		//}
		target = Target( pAbility->GetOwner() );
		return CheckTargetCondition(target, condition, pAbility);
	}

  if ((targetType & (NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_ALLY)) != 0)
	{
		float useRange = pAbility->GetUseRange();
		// get faction
		int factionFlags = 0;
		if (targetType & NDb::SPELLTARGET_ALLY)
			factionFlags |= 1 << pUnit->GetFaction();
		if (targetType & NDb::SPELLTARGET_ENEMY)
			factionFlags |= pUnit->GetOppositeFactionFlags();
		// allied units or enemy units
		NI_ASSERT(useRange > 0.0f, "Bad ability range");

    const unsigned unitTargetTypeMask =
      NDb::SPELLTARGET_HEROMALE |
      NDb::SPELLTARGET_HEROFEMALE |
      NDb::SPELLTARGET_CREEP |
      NDb::SPELLTARGET_SIEGECREEP |
      NDb::SPELLTARGET_ALLNEUTRALS |
      NDb::SPELLTARGET_FACTIONCHAMPION;

		if ((targetType & unitTargetTypeMask) != 0)
		{
      vector<PFBaseUnit*> objects;

			if (FindUnits(pUnit->GetWorld(), pUnit->GetPosition(), useRange, factionFlags, targetType, objects))
			{
				CPtr<PFBaseUnit> bestUnit = NULL;
				for (int i = 0; i < objects.size(); i++)
				{
					CPtr<PFBaseUnit> obj(objects[i]);
					if (!IsUnitValid(obj))
						continue;
          {
            const Target target(objects[i]);
            if (!CheckTargetCondition(target, condition, pAbility))
              continue;
          }
					bestUnit = obj;
#if USE_NEW_AI
					if (obj->IsHero() && bestUnit && !bestUnit->IsHero() && (targetType & (NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_HEROFEMALE)) )
						break;					// found a hero
#else
					break;
#endif
				}
				if (bestUnit.GetPtr())
				{
					target = Target(bestUnit);
					return true;
				}
				return false;
			}
		}
		else
		{
      const unsigned buildingTargetTypeMask =
        NDb::SPELLTARGET_TOWER |
        NDb::SPELLTARGET_MAINBUILDING |
        NDb::SPELLTARGET_BUILDING |
        NDb::SPELLTARGET_TREE;

			NI_ASSERT((targetType & buildingTargetTypeMask) != 0, "Unknown target type!");

      vector<PFLogicObject*> objects;

			if (FindBuildings(pUnit->GetWorld(), pUnit->GetPosition(), useRange, factionFlags, targetType, objects))
			{
				for (int i = 0; i < objects.size(); i++)
				{
					CPtr<PFLogicObject> obj(objects[i]);
					if (IsUnitValid(obj))
					{
						target = Target(obj);
            if (CheckTargetCondition(target, condition, pAbility))
						  return true;
					}
				}
				return false;
			}
		}
		return false; // target was not found
	}

	return false; // unknown type
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// TargetSelectorMicroAI
///////////////////////////////////////////////////////////////////////////////////////////////////
PFTargetSelectorMicroAI::PFTargetSelectorMicroAI(CreateParams const &cp) : Base(cp)
{
  if (IsValid(GetDB().targetSelector))
	{
    pTargetSelector = dynamic_cast<PFSingleTargetSelector*>( GetDB().targetSelector->Create( GetWorld() ) );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PFTargetSelectorMicroAI::GetTargetImpl(Target& target, const ITargetCondition* const condition) const
{
  if (!CheckConditions())
    return false;
  if (!IsValid(pTargetSelector))
    return PFBasicMicroAI::GetTarget(target);

  NI_VERIFY(IsValid(pAbility), "Invalid ability", return false; );

  CPtr<PFBaseUnit> const& pUnit = pAbility->GetOwner();
  NI_VERIFY( IsValid(pUnit), "Unit mast be valid!", return false; );

  PFTargetSelector::RequestParams rp(pAbility->GetOwner(), pAbility, Target(pUnit), condition );
  bool hasTarget = pTargetSelector->FindTarget(rp, target);

  if ( hasTarget && g_dump_micro_ai_ts )
  {
    PFTargetSelector::s_DumpSelectors = true;
    Target t;
    pTargetSelector->FindTarget(rp, t);
    PFTargetSelector::s_DumpSelectors = false;
  }

  return hasTarget;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// TriggerMicroAI
///////////////////////////////////////////////////////////////////////////////////////////////////
PFMultipleTargetSelectorMicroAI::PFMultipleTargetSelectorMicroAI(CreateParams const &cp) : Base(cp)
{
  if (IsValid(GetDB().targetSelector))
	{
    pTargetSelector = dynamic_cast<PFMultipleTargetSelector*>( GetDB().targetSelector->Create( GetWorld() ) );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool PFMultipleTargetSelectorMicroAI::GetTargetImpl(Target& target, const ITargetCondition* const condition) const
{
  if (!CheckConditions())
    return false;
  if (!IsValid(pTargetSelector))
    return PFBasicMicroAI::GetTarget(target);

  NI_VERIFY(IsValid(pAbility), "Invalid ability", return false; );

  CPtr<PFBaseUnit> const& pUnit = pAbility->GetOwner();
  NI_VERIFY( IsValid(pUnit), "Unit mast be valid!", return false; );

  struct Enumerator : public ITargetAction, public NonCopyable
  {
    Enumerator(PFBaseUnit const *pOwner, ConstPtr<PFAbilityData> const &pAbility, ExecutableBoolString const &cond, float targetWeight, const NDb::UnitTargetingParameters &unitPars)
      : pOwner(pOwner), pAbility(pAbility), cond(cond), count(0), targetWeight(targetWeight), unitPars(unitPars) {}

    virtual void operator()(const Target &target)
    {
      if ( !target.IsUnit() )
        return;

      CPtr<PFBaseUnit> const& pUnit = target.GetUnit();

      if ( cond(pOwner, pUnit, pAbility) )
      {
        if ( !pOwner->CanSelectTarget(pUnit, true) ) // CanSelectTarget is autoattack specific; maybe more loose check needed, also see PFWeightTargetSelector::FindTarget
          return;

        float weight = pOwner->GetTargetWeight( pUnit, unitPars, NULL );
        if ( weight > targetWeight )
          ++count;
      }
    }
    int GetCount() const { return count; }

  protected:
    PFBaseUnit const              *pOwner;
    ConstPtr<PFAbilityData> const &pAbility;
    ExecutableBoolString const    &cond;
    int   count;
    float targetWeight;
    const NDb::UnitTargetingParameters &unitPars;

  } enumerator( pUnit, pAbility, GetDB().unitFilter, GetDB().minTargetWeight, pUnit->GetTargetingParams() );

  PFTargetSelector::RequestParams rp(pUnit, pAbility, Target(pUnit), condition );
  pTargetSelector->EnumerateTargets(enumerator, rp);

  if ( GetDB().minTargetCount == 0 ? enumerator.GetCount() == 0 : enumerator.GetCount() >= GetDB().minTargetCount)
  {
    target.SetUnit( pAbility->GetOwner() );
    return true;
  }
  return false;
}


} // name space

REGISTER_DEV_VAR("dump_micro_ai_ts", g_dump_micro_ai_ts, STORAGE_NONE);


REGISTER_WORLD_OBJECT_NM(PFMicroAI, NWorld)
REGISTER_WORLD_OBJECT_NM(PFBasicMicroAI, NWorld)
REGISTER_WORLD_OBJECT_NM(PFTargetSelectorMicroAI, NWorld)
REGISTER_WORLD_OBJECT_NM(PFMultipleTargetSelectorMicroAI, NWorld)

#endif
