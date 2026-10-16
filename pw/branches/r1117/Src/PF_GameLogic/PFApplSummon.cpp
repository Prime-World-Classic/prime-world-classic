#include "stdafx.h"

#include "PFApplSummon.h"
#include "PFAbilityData.h"
#include "PFWorld.h"
#include "PFSummoned.h"
#include "PFMaleHero.h"
#include "PFTargetSelector.h"
#include "../System/InlineProfiler.h"
#include "libdb/dbid.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "LinuxBootstrap/formula_context.h"
#include "LinuxBootstrap/summon_criterion.h"
#include <limits>
#endif

#ifndef VISUAL_CUTTED
#include "PFClientApplicators.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

namespace NWorld
{

static bool g_enableCloneAbilities = false; /// temporary

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFSummonedUnitBehaviour* PFSummonBehaviourDataCommon::CreateBehaviour(CPtr<PFBaseSummonedUnit> const& pSummonedUnit, CPtr<PFBaseUnit> const& pMaster, const NDb::SummonBehaviourBase* pBehaviourBase, float lifeTime, int behaviourFlags)
{
  return PFSummonedUnitAIBehaviour::Create(pSummonedUnit, pMaster, pBehaviourBase, dbDesc->summonType, lifeTime, behaviourFlags);
}

NDb::EUnitType PFSummonBehaviourDataCommon::GetSummonedUnitType() const
{
  return dbDesc && dbDesc->summonType == NDb::SUMMONTYPE_PET ? NDb::UNITTYPE_PET : NDb::UNITTYPE_SUMMON;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFSummonedUnitBehaviour* PFSummonBehaviourDataBase::CreateDummyBehaviour(CPtr<PFBaseSummonedUnit> const& pSummonedUnit, CPtr<PFBaseUnit> const& pMaster, const NDb::SummonBehaviourBase* pBehaviourBase, float lifeTime, int behaviourFlags)
{
  return PFSummonedUnitBehaviour::Create( pSummonedUnit, pMaster, pBehaviourBase, lifeTime, behaviourFlags );
}


#if defined(PW_LINUX_NULL_RENDER)
/** Read the new candidate, not the summoning applicator's original receiver; empty data keeps its field default. */
static LinuxBootstrap::NumericFormulaResult EvaluateSummonSetupValue(const ExecutableFloatString& source,
	const PFBaseApplicator* app, const PFBaseUnit* candidate, float fallback)
{
	if (!app || !candidate) return {LinuxBootstrap::NumericFormulaError::ResolverFailure};
	if (source.sString.empty() && source.compiledString.empty()) return {LinuxBootstrap::NumericFormulaError::None, fallback};
	return LinuxBootstrap::EvaluateSelectorConditionFormula(source.sString.c_str(), app->GetAbilityOwner(), candidate, app);
}

/** Preserve original truncation/defaults, rejecting failed or out-of-range integer reads without their random draws. */
static bool EvaluateSummonSetupInteger(PFBaseApplicator& app, const ExecutableIntString& authored, int fallback, int& value)
{
	if (authored.sString.empty() && authored.compiledString.empty()) { value = fallback; return true; }
	auto* random = app.GetWorld() ? app.GetWorld()->GetRndGen() : nullptr;
	const NRandom::RandomGenerator before(random ? *random : NRandom::RandomGenerator(0));
	const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(authored.sString.c_str(), app);
	if (!result.Succeeded() || double(result.value) < std::numeric_limits<int>::min()
		|| double(result.value) > std::numeric_limits<int>::max())
	{
		if (random) *random = before;
		return false;
	}
	value = static_cast<int>(result.value); return true;
}

/** Validate both original pre-write resource reads as one pair, including their random draws.
 * Failure leaves resources unchanged. Callers retire the new candidate and skip
 * its child effects; this does not undo earlier replacement or other setup reads.
 */
static bool SetupSummonsLifeAndEnergy(const NDb::SpawnStats& stats, const PFBaseApplicator* app, PFBaseUnit* unit)
{
	const CPtr<PFBaseUnit> owner = app ? app->GetAbilityOwner() : nullptr;
	PFWorld* world = IsValid(owner) ? owner->GetWorld() : nullptr;
	if (app && world != app->GetWorld()) world = nullptr;
	const NRandom::RandomGenerator saved = world ? *world->GetRndGen() : NRandom::RandomGenerator();
	const auto life = EvaluateSummonSetupValue(stats.life, app, unit, -1.f);
	if (!life.Succeeded()) return false;
	const auto energy = EvaluateSummonSetupValue(stats.energy, app, unit, -1.f);
	if (!energy.Succeeded())
	{
		if (world) *world->GetRndGen() = saved;
		return false;
	}
	if (life.value > 0.f) unit->ChangeHealth(life.value - unit->GetLife());
	if (energy.value >= 0.f) unit->ChangeMana(energy.value - unit->GetEnergy());
	return true;
}
#else
void SetupSummonsLifeAndEnergy( const NDb::SpawnStats& spawnStats, const PFBaseApplicator* pSummoningAppl, PFBaseUnit* pSummonedUnit )
{
  float life = spawnStats.life( pSummoningAppl->GetAbilityOwner(), pSummonedUnit, pSummoningAppl, -1.0f );
  float energy = spawnStats.energy( pSummoningAppl->GetAbilityOwner(), pSummonedUnit, pSummoningAppl, -1.0f );
  if ( life > 0.0f )
  {
    pSummonedUnit->ChangeHealth( life - pSummonedUnit->GetLife() );
  }
  if ( energy >= 0.0f )
  {
    pSummonedUnit->ChangeMana( energy - pSummonedUnit->GetEnergy() );
  }
}
#endif


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFApplSummon::Start()
{
  NI_PROFILE_FUNCTION

  if ( PFBaseApplicator::Start() )
  {
    return true;
  }

  const NDb::SummonApplicator& db = GetDB();

#if defined(PW_LINUX_NULL_RENDER)
	// Preserve original evaluation order/defaults, but never turn a failed expression into a summon.
	const auto retrieveInteger = [this](const ExecutableIntString& authored, int fallback, int& value) {
		return EvaluateSummonSetupInteger(*this, authored, fallback, value);
	};

#endif
  bool isSummonClone = ( db.summonSource == NDb::SUMMONSOURCE_TARGETCLONE );

  // Obtain unit to summon
  NDb::Ptr<NDb::Creature> pUnitDesc;
  if ( isSummonClone )
  {
    pUnitDesc = dynamic_cast<NDb::Creature*>( const_cast<NDb::Unit*>( pReceiver->DbUnitDesc() ) );

    NI_VERIFY( pUnitDesc, NStr::StrFmt( "Summon receiver should be creature, but it is %s", pReceiver->GetDebugDescription() ), return true );
  }
  else
  {
    NI_VERIFY( !db.summonedUnits.empty(), NStr::StrFmt( "No units to summon in applicator '%s'!", NDb::GetFormattedDbId(db.GetDBID()) ),
      return true; );
#if defined(PW_LINUX_NULL_RENDER)
		int summonedUnitIndex;
		if (!retrieveInteger(db.summonedUnitIndex, 0, summonedUnitIndex)) return true;
#else
    int summonedUnitIndex = RetrieveParam( db.summonedUnitIndex, 0 );
#endif
    NI_VERIFY(summonedUnitIndex >= 0 && summonedUnitIndex < db.summonedUnits.size(),NStr::StrFmt("Wrong summonedUnitIndex formula in applicator '%s' (summonedUnitIndex: %i)!", NDb::GetFormattedDbId(db.GetDBID()), summonedUnitIndex),
      summonedUnitIndex = 0; );
    pUnitDesc = db.summonedUnits[summonedUnitIndex];
    NI_VERIFY( pUnitDesc, NStr::StrFmt("Failed to get unit to summon in applicator '%s' (summonedUnitIndex: %i)!", NDb::GetFormattedDbId(db.GetDBID()), summonedUnitIndex) ,
      return true; );
  }

  CObj<PFSummonBehaviourDataBase> pBehaviourData;
  if (db.behaviour)
  {
    pBehaviourData = db.behaviour->Create();
    pBehaviourData->Initialize(this);
  }

#if defined(PW_LINUX_NULL_RENDER)
	// Validate duration before replacement, pet toggling or any summoned-unit creation.
	const auto duration = db.lifeTime.sString.empty() && db.lifeTime.compiledString.empty()
		? LinuxBootstrap::NumericFormulaResult{LinuxBootstrap::NumericFormulaError::None, 0.f}
		: LinuxBootstrap::EvaluateApplicatorNumericFormula(db.lifeTime.sString.c_str(), *this);
	if (!duration.Succeeded()) return true;
	const float lifeTime = duration.value;
#else
  float lifeTime = RetrieveParam(db.lifeTime, 0.0f);
#endif
  NDb::EUnitType unitType = pBehaviourData ? pBehaviourData->GetSummonedUnitType() : NDb::UNITTYPE_DUMMYUNIT; // unit would be an dummy by default (if have no behaviour)

#if defined(PW_LINUX_NULL_RENDER)
	int summonedUnitsCount;
	if (!retrieveInteger(db.summonedUnitsCount, 1, summonedUnitsCount)) return true;
#else
  int summonedUnitsCount = RetrieveParam( db.summonedUnitsCount, 1 );
#endif
  NI_DATA_VERIFY( summonedUnitsCount > 0, NStr::StrFmt( "Summoned units count should be positive (now %d)!", summonedUnitsCount ), summonedUnitsCount = 1 );

  const NDb::SummonBehaviourBase* const pSummonBehaviourBase = db.behaviour;
  const NDb::SummonBehaviourCommon* const pSummonBehaviourCommon = dynamic_cast<const NDb::SummonBehaviourCommon*>( pSummonBehaviourBase );

  if ( pSummonBehaviourCommon )
  {
    NDb::SummonType const& summonType = pSummonBehaviourCommon->summonType;

    // No primary summons for dead masters
    if ( summonType == NDb::SUMMONTYPE_PRIMARY && pOwner->IsDead() )
      return true;

#if defined(PW_LINUX_NULL_RENDER)
		int maxSummonCount;
		if (!retrieveInteger(pSummonBehaviourCommon->maxCount, 1, maxSummonCount)) return true;
#else
    int maxSummonCount = pSummonBehaviourCommon->maxCount( GetAbilityOwner(), GetReceiver(), this, 1 );
#endif
    int curSummonsCount = pOwner->GetSummonsCount( summonType, db.summonGroupName );

#if defined(PW_LINUX_NULL_RENDER)
		int maxThisSummonCount;
		if (!retrieveInteger(pSummonBehaviourCommon->maxThisCount, 0, maxThisSummonCount)) return true;
#else
    int maxThisSummonCount = pSummonBehaviourCommon->maxThisCount( GetAbilityOwner(), GetReceiver(), this, 0 );
#endif

    NI_DATA_ASSERT( maxThisSummonCount <= maxSummonCount, "maxThisCount should be less than maxCount" );
    
    // Pet turn off logic
    if (unitType == NDb::UNITTYPE_PET && curSummonsCount != 0)
    {
      struct UnsummonAct 
      {
        UnsummonAct( const NDb::DBID& id ) : bDontSpawn(false), dbid(id) {}
        void operator()( PFSummonBehaviour* pBehaviour )
        {
          if ( pBehaviour->GetUnit()->DbUnitDesc()->GetDBID() == dbid)
          {
            bDontSpawn = true;
            pBehaviour->Unsummon();
          }
        }

        NDb::DBID dbid;
        bool bDontSpawn;
      } unsummon(pUnitDesc->GetDBID());

      PFBehaviourGroup* pets = pOwner->GetSummonedGroup(NDb::SUMMONTYPE_PET);
      pets->ForAllBehaviour(unsummon);
      if (unsummon.bDontSpawn)
        return true;
    }
    else // ordinary summon
    {
      ExecutableFloatString const& sortingFormula = db.unsummonSortCriterion;
      UnsummonCriterion unsummonCriterion( &sortingFormula, GetAbilityOwner(), this );
      // Check for default formula value (zero)
      bool bUseCriterion = false;
      if ( !sortingFormula.sString.empty() )
      {
      float val = 0.0f;
      const int occurences = sscanf( sortingFormula.sString.c_str(), "%g%*[f]", &val ); // note that something like "0.f + someCode" will be treated as "0.f"
        bUseCriterion = occurences != 1 || val != 0.0f;
      }

#if defined(PW_LINUX_NULL_RENDER)
			if (sortingFormula.sString.empty() && !sortingFormula.compiledString.empty()) bUseCriterion = true;
			if (bUseCriterion)
			{
				// Validate the whole named group before either replacement phase can kill a member.
				struct Validate
				{
					UnsummonCriterion& criterion;
					const string& group;
					bool valid = true;
					void operator()(PFSummonBehaviour* behaviour)
					{
						if (behaviour && IsValid(behaviour->GetUnit()) && behaviour->GetGroupName() == group
							&& !LinuxBootstrap::EvaluateSummonCriterion(criterion, behaviour).Succeeded()) valid = false;
					}
				} validate{unsummonCriterion, db.summonGroupName};
				pOwner->GetSummonedGroup(summonType)->ForAllBehaviour(validate);
				if (!validate.valid) return true;
			}
#endif
      int countToUnsummon = summonedUnitsCount;
      if ( maxThisSummonCount > 0 )
      {
#if defined(PW_LINUX_NULL_RENDER)
				const int removed = pOwner->RemoveSummons(summonedUnitsCount, summonType, db.summonGroupName, pUnitDesc, maxThisSummonCount, bUseCriterion ? &unsummonCriterion : nullptr);
				if (removed < 0) return true;
				countToUnsummon -= removed;
#else
        countToUnsummon -= pOwner->RemoveSummons( summonedUnitsCount, pSummonBehaviourCommon->summonType, db.summonGroupName, pUnitDesc, maxThisSummonCount, bUseCriterion ? &unsummonCriterion : 0 );
#endif
      }
#if defined(PW_LINUX_NULL_RENDER)
			if (pOwner->RemoveSummons(summonedUnitsCount, summonType, db.summonGroupName, nullptr, maxSummonCount, bUseCriterion ? &unsummonCriterion : nullptr) < 0) return true;
#else
      pOwner->RemoveSummons( summonedUnitsCount, pSummonBehaviourCommon->summonType, db.summonGroupName, 0, maxSummonCount, bUseCriterion ? &unsummonCriterion : 0 );
#endif
    }

    pOwner->SetMaxAllowedSummons( pSummonBehaviourCommon->summonType, db.summonGroupName, maxSummonCount );
  }

  Placement placement(pOwner->GetPosition());
  if ( (db.flags & NDb::SUMMONFLAGS_TURNBYMASTERDIRECTION) != 0 )
  {
    if ( PFBaseMovingUnit* mOwner = dynamic_cast<PFBaseMovingUnit*>(pOwner.GetPtr()) )
      placement.rot.FromDirection( CVec3( -mOwner->GetMoveDirection(), 0.0f ) );
  }
  for ( int i = 0; i < summonedUnitsCount; i++ )
  {
    bool opensWarFog = unitType == NDb::UNITTYPE_PET ? false : GetDB().openWarFog;
    CPtr<PFBaseSummonedUnit> pSummonedUnit( 
      new PFBaseSummonedUnit(pReceiver->GetWorld(), pUnitDesc, unitType, pOwner.GetPtr(), placement,
        GetDB().flags & NDb::SUMMONFLAGS_NOSUMMONANIMATION , 
        GetDB().flags & NDb::SUMMONFLAGS_USEGLOWEFFECT, 
        opensWarFog) );

    if(isSummonClone && (unitType != NDb::UNITTYPE_HEROMALE || unitType != NDb::UNITTYPE_HEROFEMALE))
      pSummonedUnit->ModifyStatsByForce();

#if defined(PW_LINUX_NULL_RENDER)
		const auto damage = EvaluateSummonSetupValue(db.takeModDmg, this, pSummonedUnit, 1.f);
		if (!damage.Succeeded())
		{
			pSummonedUnit->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_UNSUMMON);
			continue;
		}
		pSummonedUnit->SetAdditionalDamageParam(damage.value, db.takeTypeUnit);
#else
    pSummonedUnit->SetAdditionalDamageParam( db.takeModDmg( pOwner, pSummonedUnit, this, 1.f ), db.takeTypeUnit );
#endif

    if ( pSummonBehaviourCommon && pSummonBehaviourCommon->targetingParams )
    {
      pSummonedUnit->OverrideTargetingParams( pSummonBehaviourCommon->targetingParams );
    }

    int behFlags = PFSummonedUnitBehaviour::BEHAVIOURFLAGS_CONTROLLEDLIFETIME;
    if ( (db.flags & NDb::SUMMONFLAGS_ESSENTIAL) != 0 )
      behFlags |= PFSummonedUnitBehaviour::BEHAVIOURFLAGS_ESSENTIALSUMMON;
    if ( (db.flags & NDb::SUMMONFLAGS_ALPHASUMMON) != 0 )
      behFlags |= PFSummonedUnitBehaviour::BEHAVIOURFLAGS_ALPHASUMMON;
    if ( (db.flags & NDb::SUMMONFLAGS_NOHEALTHPRIORITY) != 0 )
      behFlags |= PFSummonedUnitBehaviour::BEHAVIOURFLAGS_NOHEALTHPRIORITY;
    if ( (db.flags & NDb::SUMMONFLAGS_LIKEHERO) != 0 )
      behFlags |= PFSummonedUnitBehaviour::BEHAVIOURFLAGS_LIKEHERO;

    // If unit have an behaviour (it is not a dummy unit)
    CObj<PFSummonedUnitBehaviour> pBehaviour = pBehaviourData
      ? pBehaviourData->CreateBehaviour(pSummonedUnit, pOwner.GetPtr(), db.behaviour.GetPtr(), lifeTime, behFlags)
      : PFSummonBehaviourDataBase::CreateDummyBehaviour(pSummonedUnit, pOwner.GetPtr(), db.behaviour.GetPtr(), lifeTime, behFlags);

    if ( pBehaviour )
    {
      pBehaviour->SetGroupName( db.summonGroupName );

      if (pSummonBehaviourCommon)
      {
#if defined(PW_LINUX_NULL_RENDER)
				const auto lash = EvaluateSummonSetupValue(pSummonBehaviourCommon->lashRange, this, pSummonedUnit, 15.f);
				if (!lash.Succeeded())
				{
					// Install before death so original cleanup retires its owner group, alpha link and behavior together.
					pSummonedUnit->AddBehaviourOnTop(pBehaviour.GetPtr());
					pBehaviour->Unsummon();
					continue;
				}
				pBehaviour->SetLashRange(lash.value);
#else
        pBehaviour->SetLashRange( pSummonBehaviourCommon->lashRange(pOwner, pSummonedUnit, this, 15.f ) );
#endif
        pBehaviour->SetResponseRange( pSummonBehaviourCommon->responseRange );
        pBehaviour->SetResponseTime( pSummonBehaviourCommon->responseTime );
      }

      pSummonedUnit->AddBehaviourOnTop(pBehaviour.GetPtr());
    }

    // Obtain spawn position from target selector
    Target spawnTarget = GetTarget();
    if ( db.placeMode == NDb::SUMMONPLACEMODE_BYTARGETSELECTOR )
    {
      CObj<PFSingleTargetSelector> pTargetSelector = static_cast<PFSingleTargetSelector*>(db.placeSelector->Create( GetWorld() ));
      Target receiver(pReceiver);
      PFTargetSelector::RequestParams pars(*this, receiver);
      pTargetSelector->FindTarget(pars, spawnTarget);
    }
    else if ( db.placeMode == NDb::SUMMONPLACEMODE_BYBEHAVIOUR )
    {
      if (pBehaviour)
        pBehaviour->AcquireBehaviourDefinedSpawnPosition(spawnTarget);
    }
    //else if ( db.placeMode == NDb::SUMMONPLACEMODE_BYAPPLICATOR ) // default value

    // if we have not valid target to spawn to, obtain it from ability target
    if ( !spawnTarget.IsValid( true ) )
      spawnTarget = GetTarget();

    if(db.flags == NDb::SUMMONFLAGS_NOCOLLISION)
      pSummonedUnit->SetPlaceUnitIgnoreCollision(true);

    pSummonedUnit->TeleportTo( spawnTarget.AcquirePosition().AsVec2D() );

    if ( ( db.flags & NDb::SUMMONFLAGS_COPYLEVELANDSTATS ) != 0 )
    {
      pSummonedUnit->SetNaftaLevel( 1 );
      pSummonedUnit->DoLevelups( pReceiver->GetNaftaLevel() - 1 );
    }
    
#if defined(PW_LINUX_NULL_RENDER)
		if (!SetupSummonsLifeAndEnergy(db.spawnStats, this, pSummonedUnit))
		{
			pSummonedUnit->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_UNSUMMON);
			continue;
		}
#else
    SetupSummonsLifeAndEnergy( db.spawnStats, this, pSummonedUnit );
#endif

    // Activate applicators on unit
    Target createdUnit(CPtr<PFBaseUnit>(pSummonedUnit.GetPtr()));
    CreateAndActivateApplicators(db.applicators, pAbility, createdUnit, this);

    // hide primary summon if hero is playing minigame
    if ( pOwner->IsHero() && IsValid(pBehaviour) && ( pBehaviour->GetSummonTypeInt() == NDb::SUMMONTYPE_PRIMARY || pBehaviour->GetSummonTypeInt() == NDb::SUMMONTYPE_PET ) )
    {
      PFBaseHero* pHero = dynamic_cast<PFBaseHero*>( pOwner.GetPtr() );
      if ( pHero && pHero->IsIsolated() )
        pSummonedUnit->Hide( true );
    }
  }

  return true;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFApplCloneHero::Start()
{
  if ( Base::Start() )
  {
    return true;
  }

  PFBaseHero* pHero      = dynamic_cast<PFBaseHero*>( pReceiver.GetPtr() );
#if defined(PW_LINUX_NULL_RENDER)
	if (!pHero) return true;
#endif
  NI_ASSERT( pHero, "PFApplCloneHero can be applied only on a hero" );

  PFBaseHero* pOwnerHero = dynamic_cast<PFBaseHero*>( pOwner.GetPtr() );
#if defined(PW_LINUX_NULL_RENDER)
	if (!pOwnerHero) return true;
#endif
  NI_ASSERT( pOwnerHero, "PFApplCloneHero can be applied only by a hero" );

  NDb::CloneHeroApplicator const& db = GetDB();

  int flags = db.flags;

	// prepare clone parameters
	CloneParams params;
#if defined(PW_LINUX_NULL_RENDER)
	const auto duration = EvaluateSummonSetupValue(db.lifeTime, this, pHero, 0.f);
	if (!duration.Succeeded()) return true;
	params.lifeTime = duration.value;
	const auto following = EvaluateSummonSetupValue(db.followRange, this, pHero, 0.f);
	if (!following.Succeeded()) return true;
	const float followRange = following.value;
#else
	params.lifeTime = RetrieveParam(   db.lifeTime,    0.0f );
	float followRange = RetrieveParam( db.followRange, 0.0f );
#endif
	if ( followRange > 0 )
  {
    params.followRange = followRange;
  }
  params.mirrorOwnerAbilities   = ( flags & NDb::CLONEFLAGS_MIRROROWNERSPELLS ) && g_enableCloneAbilities;
  params.shouldPlaySummonEffect = ( flags & NDb::CLONEFLAGS_PLAYSUMMONEFFECT  );
  params.copyStats              = ( flags & NDb::CLONEFLAGS_COPYSTATS );
  params.isDummy                = ( flags & NDb::CLONEFLAGS_DUMMYBEHAVIOUR );
  params.initTalents            = ( flags & NDb::CLONEFLAGS_INITTALENTS );

  const bool makeSlave = ( flags & NDb::CLONEFLAGS_MAKESLAVE );

  params.pOwner = pOwnerHero;

  if ( makeSlave )
  {
    // No slave clones for dead master
    if ( pOwnerHero->IsDead() )
      return true;

    params.bSlave = true;
    params.targetingParams = db.targetingParams;

    // remove old clones
#if defined(PW_LINUX_NULL_RENDER)
		int maxSummonCount;
		if (!EvaluateSummonSetupInteger(*this, db.maxCount, 0, maxSummonCount)) return true;
		const int curSummonsCount = pOwnerHero->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, db.summonGroupName);
		const long long requested = 1LL + curSummonsCount - maxSummonCount;
		if (requested > std::numeric_limits<int>::max()) return true;
		pOwnerHero->RemoveSummons(requested > 0 ? static_cast<int>(requested) : 0, NDb::SUMMONTYPE_PRIMARY, db.summonGroupName);
#else
    int maxSummonCount = RetrieveParam( db.maxCount, 0 );
    int curSummonsCount = pOwnerHero->GetSummonsCount( NDb::SUMMONTYPE_PRIMARY, db.summonGroupName );
    pOwnerHero->RemoveSummons( 1 + curSummonsCount - maxSummonCount, NDb::SUMMONTYPE_PRIMARY, db.summonGroupName );
#endif
    pOwnerHero->SetMaxAllowedSummons( NDb::SUMMONTYPE_PRIMARY, db.summonGroupName, maxSummonCount );
  }

	// clone hero
#if defined(PW_LINUX_NULL_RENDER)
	// Preserve original post-replacement read order; earlier unsummon callbacks are not rolled back on failure.
	const auto damage = EvaluateSummonSetupValue(db.takeModDmg, this, pHero, 1.f);
	if (!damage.Succeeded()) return true;
	PFBaseHero* pClone = pHero->Clone(params, damage.value, db.takeTypeUnit, db.summonGroupName);
#else
  PFBaseHero *pClone = pHero->Clone(params, db.takeModDmg( pOwner, pHero, this, 1.f ), db.takeTypeUnit, db.summonGroupName );
#endif
  if (!pClone)
    return true;

  // slave must have unittype = summon
  if( makeSlave )
  {
    pClone->SetUnitType( NDb::UNITTYPE_SUMMON );
  }
  else if ( params.isDummy )
  {
    pClone->SetUnitType( NDb::UNITTYPE_DUMMYUNIT );
  }

	// setup non-hero properties
  pClone->SetGhostMode(flags & NDb::CLONEFLAGS_GHOSTMOVE, false);
  pClone->EnableAbilityAI(flags & NDb::CLONEFLAGS_USEABILITYAI);

#if defined(PW_LINUX_NULL_RENDER)
	if (!SetupSummonsLifeAndEnergy(db.spawnStats, this, pClone))
	{
		pClone->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_UNSUMMON);
		return true;
	}
#else
  SetupSummonsLifeAndEnergy( db.spawnStats, this, pClone );
#endif

  // Activate applicators on unit
  CPtr<PFBaseUnit> pUnit(pClone);
  Target createdUnit(pUnit);
  CreateAndActivateApplicators(db.applicators, pAbility, createdUnit, this);

  return true;
}

REGISTER_DEV_VAR("enable_clone_abilities", g_enableCloneAbilities, STORAGE_NONE); /// temporary

} // namespace

REGISTER_WORLD_OBJECT_NM(PFSummonBehaviourDataBase,   NWorld);
REGISTER_WORLD_OBJECT_NM(PFSummonBehaviourDataCommon, NWorld);
REGISTER_WORLD_OBJECT_NM(PFApplSummon,                NWorld);
REGISTER_WORLD_OBJECT_NM(PFApplCloneHero,             NWorld);

