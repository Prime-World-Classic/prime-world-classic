#include "stdafx.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "DBConsumable.h"
#include "DBGameLogic.h"
#include "DBStats.h"
#include "PFMaleHero.h"
#include "PFTalent.h"
#include "PFConsumable.h"
#include "PFAIWorld.h"
#include "../Game/PF/Audit/ClientStubs.h"

namespace
{
static int g_current_pet = 0;

struct LinuxNullTalentCalculator : public NWorld::ITalentCalculator
{
  virtual void operator()( NDb::Ptr<NDb::Talent> const&, int, const NWorld::PFTalentsSet::SlotInfo& ) {}
};
}

namespace NWorld
{

NAMEMAP_BEGIN( PFBaseMaleHero )
  NAMEMAP_PARENT( PFBaseHero )
  NAMEMAP_FUNC_RO( devPoints, &PFBaseMaleHero::GetDevPointsRef )
NAMEMAP_END

PFBaseMaleHero::PFBaseMaleHero(PFWorld* pWorld, const PFBaseHero::SpawnInfo &info, NDb::EFaction faction, NDb::EFaction _originalFaction)
  : PFEaselPlayer(pWorld, info, NDb::UNITTYPE_HEROMALE, faction, _originalFaction)
  , manaCostForceModifier(1.0f), lifeCostForceModifier(1.0f), lastTalentUseStep(-1), cheatBudget(0.0f)
{
  for (unsigned int i = 0; i < persistentStats.capacity(); ++i) persistentStats[i] = 0.0f;
}

inline CObj<PFTalent> CreatePortalTalent( CPtr<PFBaseMaleHero> const& pOwner )
{
  if (!IsValid(pOwner) || !pOwner->GetWorld() || !pOwner->GetWorld()->GetAIWorld())
    return 0;

  CObj<PFTalent> pPortal = new PFTalent( pOwner, pOwner->GetWorld()->GetAIWorld()->GetPortalTalent(), 0, -1, -2, false );
  pPortal->abilityType = NDb::ABILITYTYPEID_PORTAL;
  return pPortal;
}

void PFBaseMaleHero::OnDie() { if (pTalents) pTalents->CleanSet(); pTalents = 0; portal = 0; PFBaseHero::OnDie(); }
bool PFBaseMaleHero::Step(float dtInSeconds) { if (pTalents) pTalents->Update(dtInSeconds); if (portal) portal->Update(dtInSeconds, true); return PFEaselPlayer::Step(dtInSeconds); }
void PFBaseMaleHero::InitHero( NDb::BaseHero const* pDesc, bool initTalents, bool usePlayerInfoTalentSet, const NCore::PlayerInfo& playerInfo, bool initInventory )
{
  (void)initInventory;
  PFBaseHero::InitHero(pDesc);

  if (!isClone)
  {
    FillPersistentStats(playerInfo, persistentStats);
    FillPersistentBuffs(playerInfo, persistentBuffs);
    portal = CreatePortalTalent(this);
    if (portal)
    {
      portal->Activate();
      portal->ApplyPassivePart(true);
    }
  }

  if (initTalents)
  {
    pTalents = new PFTalentsSet(GetWorld());
    PFTalentsSet::SetInfo info;
    LinuxNullTalentCalculator calculator;
    const float talentsForce = PFTalentsSet::PreloadTalentsSetAndCalcForce(
      GetWorld() && GetWorld()->GetAIWorld() ? &GetWorld()->GetAIWorld()->GetAIParameters() : 0,
      GetDbHero(),
      usePlayerInfoTalentSet,
      GetWorld() ? GetWorld()->GetResourcesCollection() : 0,
      playerInfo,
      info);
    if (!isClone)
      force += talentsForce;
    pTalents->LoadSet(this, info);
    pTalents->CalculateForce(calculator);
    pTalents->ActivateTakenOnStart();
  }
}
void PFBaseMaleHero::FillPersistentStats( const NCore::PlayerInfo playerInfo, TPersistentStats& stats )
{
  for (unsigned int i = 0; i < stats.capacity(); ++i)
    stats[i] = 0.0f;

  stats[NDb::STAT_LIFE] = playerInfo.hsHealth;
  stats[NDb::STAT_ENERGY] = playerInfo.hsMana;
  stats[NDb::STAT_STRENGTH] = playerInfo.hsStrength;
  stats[NDb::STAT_INTELLECT] = playerInfo.hsIntellect;
  stats[NDb::STAT_ATTACKSPEED] = playerInfo.hsAgility;
  stats[NDb::STAT_DEXTERITY] = playerInfo.hsCunning;
  stats[NDb::STAT_STAMINA] = playerInfo.hsFortitude;
  stats[NDb::STAT_WILL] = playerInfo.hsWill;
  stats[NDb::STAT_LIFEREGENERATIONABSOLUTE] = playerInfo.hsLifeRegen;
  stats[NDb::STAT_ENERGYREGENERATIONABSOLUTE] = playerInfo.hsManaRegen;
}
void PFBaseMaleHero::DoLevelups(int count, float statsBonusBudget)
{
  if (count < 1)
    return;

  float maxLevel = 1.0f;
  if (GetWorld() && GetWorld()->GetAIWorld())
    maxLevel = static_cast<float>(Max(1, GetWorld()->GetAIWorld()->GetMaxHeroLevel()));

  for (unsigned int i = 0; i < persistentStats.capacity(); ++i)
  {
    NDb::EStat stat = static_cast<NDb::EStat>(i);
    float statValue = persistentStats[stat] * count / maxLevel;

    PersistentBuff* buff = &persistentBuffs[stat];
    if (buff && !buff->applied)
    {
      statValue = statValue * buff->buffVal.mul + buff->buffVal.add;
      buff->applied = true;
    }

    AddStat(stat, statValue);
  }

  PFEaselPlayer::DoLevelups(count, statsBonusBudget);
}
void PFBaseMaleHero::DoHeroLevelups(const int count, const float statsBonusBudget, const float fraction)
{
  if (count < 1)
    return;

  const float mod = static_cast<float>(count) * fraction;

  for (unsigned int i = 0; i < persistentStats.capacity(); ++i)
  {
    const NDb::EStat stat = static_cast<NDb::EStat>(i);
    const PersistentBuff& buff = persistentBuffs[stat];
    float statValue = persistentStats[stat] * buff.buffVal.mul + buff.buffVal.add;
    statValue *= mod;
    AddStat(stat, statValue);
  }

  UpgradeHeroStats(count, statsBonusBudget, fraction);
  naftaLevel += count;
  timeSinceLevelUp = 0.0f;
  OnLevelUp();

  struct LevelSynchronizer: CloneFunc, ISummonAction
  {
    int level;

    LevelSynchronizer(int _level) : level(_level) {}
    void operator()(PFBaseUnit* pUnit) { if (pUnit) pUnit->SetNaftaLevel(level); }
    void operator()(PFBaseHero* pClone) { operator()(static_cast<PFBaseUnit*>(pClone)); }
  } f(naftaLevel);

  ForAllSummons(f, NDb::SUMMONTYPE_PRIMARY);
  ForAllSummons(f, NDb::SUMMONTYPE_SECONDARY);
  ForAllClones(f);
}
void PFBaseMaleHero::FillPersistentBuffs( const NCore::PlayerInfo playerInfo, TPersistentBuffs& buffs )
{
  for (unsigned int i = 0; i < buffs.capacity(); ++i)
    buffs[i] = PersistentBuff();

  for (NCore::TPlayerBuffs::const_iterator it = playerInfo.hBuffs.begin(); it != playerInfo.hBuffs.end(); ++it)
  {
    if (it->first < 0 || it->first >= static_cast<int>(buffs.capacity()))
      continue;
    buffs[static_cast<NDb::EStat>(it->first)] = PersistentBuff(it->second);
  }
}
void PFBaseMaleHero::DropCooldowns( DropCooldownParams const& dropCooldownParams ) { PFBaseHero::DropCooldowns(dropCooldownParams); if ((dropCooldownParams.flags & NDb::ABILITYIDFLAGS_TALENTS) != 0) DropTalentsCooldowns(dropCooldownParams); }
void PFBaseMaleHero::LoadTalents(string const& strTalentsSetPath)
{
  if (!pTalents)
    pTalents = new PFTalentsSet(GetWorld());

  LinuxNullTalentCalculator calculator;
  pTalents->LoadPredefinedSet(this, strTalentsSetPath, calculator);
  pTalents->ActivateTakenOnStart();
}
PFTalent* PFBaseMaleHero::GetTalent(int level, int slot) const { return pTalents ? pTalents->GetTalent(level, slot).GetPtr() : 0; }
ETalentActivation::Enum PFBaseMaleHero::CanActivateTalent(int level, int slot) const { return pTalents ? pTalents->CanActivateTalent(level, slot) : ETalentActivation::Denied; }
bool PFBaseMaleHero::HasFreshTalentsToBuy() const { return pTalents && pTalents->HasFreshTalentsToBuy(); }
bool PFBaseMaleHero::ActivateTalent(int level, int slot) { return pTalents && pTalents->ActivateTalent(level, slot); }
void PFBaseMaleHero::OnTalentActivated( const int level, const int slot, const float statsIncrementFraction )
{
  (void)level;
  (void)slot;

  const int levelUps = CountLevelups();
  if (levelUps < 1)
  {
    RecalculateManaCostModifier();
    return;
  }

  if (!isClone)
    DoHeroLevelups(levelUps, 0.0f, statsIncrementFraction);
  else
    DoLevelups(levelUps);

  RecalculateManaCostModifier();
}
bool PFBaseMaleHero::CanUseTalent(int level, int slot) const { return CanUseTalent(GetTalent(level, slot)); }
bool PFBaseMaleHero::CanUseTalent( PFTalent * talent ) const { return IsValid(talent) && talent->CanBeUsed(); }
CObj<PFAbilityInstance> PFBaseMaleHero::UseTalent( int level, int slot, Target const& target ) { return UseTalent(GetTalent(level, slot), target); }
CObj<PFAbilityInstance> PFBaseMaleHero::UseTalent( PFTalent* talent, Target const& target )
{
  if (!CanUseTalent(talent))
    return 0;
  CObj<PFAbilityInstance> instance = talent->ApplyToTarget(target);
  if (instance)
    talent->SetLastUseStep(GetWorld() ? GetWorld()->GetStepNumber() : -1);
  return instance;
}
int PFBaseMaleHero::GetDevPoints() const { return IsValid(pTalents) ? pTalents->GetDevPoints() : 0; }
int const &PFBaseMaleHero::GetDevPointsRef() const { static int zero = 0; return IsValid(pTalents) ? pTalents->GetDevPoints() : zero; }
void PFBaseMaleHero::ModifyForceByStats( const PFBaseMaleHero::TPersistentStats& stats, const NDb::AILogicParameters* pAIParams, float& force )
{
  if (!pAIParams || pAIParams->forceParameters.IsEmpty())
    return;

  float statsPoints = 0.0f;
  for (unsigned int i = 0; i < stats.capacity(); ++i)
  {
    NDb::StatBudget const* statBudget = PFStatContainer::GetStatBudget(pAIParams, static_cast<NDb::EStat>(i));
    if (statBudget && statBudget->budget > EPS_VALUE)
      statsPoints += stats[i] / statBudget->budget;
  }

  force += statsPoints * pAIParams->forceParameters->masteryPointForce;
}
float PFBaseMaleHero::GetForce( bool countPersistentStats ) const
{
  float result = PFBaseHero::GetForce(countPersistentStats);
  if (countPersistentStats && GetWorld() && GetWorld()->GetAIWorld())
    ModifyForceByStats(persistentStats, &GetWorld()->GetAIWorld()->GetAIParameters(), result);
  return result;
}
float PFBaseMaleHero::GetManaCostModifier( bool altCost ) const { return altCost ? lifeCostForceModifier : manaCostForceModifier; }
void PFBaseMaleHero::RecalculateManaCostModifier() { manaCostForceModifier = 1.0f; lifeCostForceModifier = 1.0f; }
float PFBaseMaleHero::GetTalentsAcquiredBudgetPercent() const { return pTalents ? pTalents->GetAcquiredBudgetPercent() : 0.0f; }
void PFBaseMaleHero::DropTalentsCooldowns( DropCooldownParams const& dropCooldownParams ) const { (void)dropCooldownParams; }
void PFBaseMaleHero::RestartGroupCooldowns( PFTalent const* pTalent ) { (void)pTalent; }
const IUnitFormulaPars* PFBaseMaleHero::GetObjectFavorite() const { return 0; }
void PFBaseMaleHero::ExecuteCommandUseConsumable( int slot, const Target& target, bool isPlayerCommand )
{
  (void)isPlayerCommand;

  if ( !CanUseConsumable(slot) )
    return;

  UseConsumable(slot, target, IsLocal());
}
void PFBaseMaleHero::DoPetLogic() {}
void PFBaseMaleHero::LoadInventory( const NWorld::PFResourcesCollection* collection ) { (void)collection; }
int PFBaseMaleHero::GetEffectiveClassMask() const { return 0; }

} // namespace NWorld

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFBaseMaleHero, NWorld)
REGISTER_VAR( "current_pet", g_current_pet, STORAGE_USER );

#else

#include "DBConsumable.h"
#include "DBGameLogic.h"
#include "DBStats.h"
#include "PFBaseUnitStates.h"
#include "PFChest.h"
#include "PFHeroStates.h"
#include "PFPet.h" // for pet debugging
#include "PFStatistics.h"
#include "PFMaleHero.h"
#include "PFLogicDebug.h"
#include "PFGameLogicLog.h"
#include "PFWorldNatureMap.h"

#include "PFBaseAttackData.h"
#include "PFConsumable.h"
#include "PFTalent.h"
#include "ForceCalc.h"

#include "../System/InlineProfiler.h"

#ifndef VISUAL_CUTTED
#include "PFClientHero.h"
#include "PFClientApplicators.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

#include "PFImpulsiveBuffs.h"
#include "PFAbilityData.h"

#include "PFPredefinedUnitVariables.h"

#include "IAdventureScreen.h"
#include "SessionEventType.h"


#include "PFResourcesCollectionClient.h"
#include "AdventureScreen.h"
#include "HeroActions.h"

namespace
{
static int g_current_pet = 0;
}

namespace NWorld
{

  NAMEMAP_BEGIN( PFBaseMaleHero )
    NAMEMAP_PARENT( PFBaseHero )
    NAMEMAP_FUNC_RO( devPoints, &PFBaseMaleHero::GetDevPointsRef )
  NAMEMAP_END

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // PFBaseMaleHero
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  PFBaseMaleHero::PFBaseMaleHero(PFWorld* pWorld, const PFBaseHero::SpawnInfo &info, NDb::EFaction faction, NDb::EFaction _originalFaction)
    : PFEaselPlayer(pWorld, info, NDb::UNITTYPE_HEROMALE, faction, _originalFaction)
    , manaCostForceModifier(1.0f)
    , lifeCostForceModifier(1.0f)
    , lastTalentUseStep(-1)
  {
    for (unsigned int i = 0; i < persistentStats.capacity(); ++i)
    {
      persistentStats[i] = 0.0f;
    }
    cheatBudget = 0.0f;
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::OnDie()
  {
    if (pTalents)
    {
      pTalents->CleanSet();
      pTalents = NULL;
    }

    if ( portal )
    {
      portal->CancelAbility();
      portal->ApplyPassivePart(false);
      portal = 0;
    }

    PFBaseHero::OnDie();
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  bool PFBaseMaleHero::Step(float dtInSeconds)
  {
    NI_PROFILE_FUNCTION
    
    if ( pTalents )
      pTalents->Update(dtInSeconds);

    if (portal)
      portal->Update(dtInSeconds, true);

    return PFEaselPlayer::Step(dtInSeconds);
  }
  

  inline CObj<PFTalent> CreatePortalTalent( CPtr<PFBaseMaleHero> const& pOwner )
  {
    CObj<PFTalent> pPortal = new PFTalent( pOwner, pOwner->GetWorld()->GetAIWorld()->GetPortalTalent(), 0, -1, -2, false );
    pPortal->abilityType = NDb::ABILITYTYPEID_PORTAL;
    return pPortal;
  }


  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::InitHero( NDb::BaseHero const* pDesc, bool initTalents, bool usePlayerInfoTalentSet, const NCore::PlayerInfo& playerInfo, bool initInventory )
  {
		PFBaseHero::InitHero(pDesc);

    if ( !isClone )
    {
      FillPersistentStats( playerInfo, persistentStats );
      FillPersistentBuffs (playerInfo, persistentBuffs );
      portal = CreatePortalTalent( this );
      portal->Activate(); 
      portal->ApplyPassivePart( true );
    }

    if ( initTalents )
    {
      pTalents = new PFTalentsSet( GetWorld() );

      const NDb::AILogicParameters& aiParams = GetWorld()->GetAIWorld()->GetAIParameters();
      
      NI_DATA_ASSERT( !aiParams.forceParameters.IsEmpty(), "forceParameters is empty" );

      PFResourcesCollection* pCollection = GetWorld()->GetResourcesCollection();
      NI_ASSERT( pCollection, "Resource Collection not created" );
      PFTalentsSet::SetInfo info;
      float _force = PFTalentsSet::PreloadTalentsSetAndCalcForce(&aiParams, GetDbHero(), usePlayerInfoTalentSet, pCollection, playerInfo, info);
      if ( !isClone )
        force += _force;
      pTalents->LoadSet(this, info);

      // Calculate average stat bonus budget per level and set average stat bonus percent
      stats->SetAverageIncPercent( Force::CalcHeroStatsBonusBudget( this, 0 ) );
      pTalents->ActivateTakenOnStart();
    }

    if ( !isClone && initInventory )
    {
      inventory = playerInfo.inventory;
      LoadInventory( GetWorld()->GetResourcesCollection() );
    }

    NDb::Hero const* pHeroDesc = dynamic_cast<NDb::Hero const*>(pDesc);
    NI_ASSERT(pHeroDesc, "Invalid hero desc!");
  }

  void PFBaseMaleHero::FillPersistentStats( const NCore::PlayerInfo playerInfo, TPersistentStats& stats )
  {
    //“еперь статы даютс€ не сразу, а "размазываютс€" до 36го уровн€ ( NUM_TASK )
    // —охран€ем статы, чтобы не сохран€ть всю PlayerInfo в геро€, используютс€ дл€ "размазанных" статов, также используетс€ в чите hero_force
    for (unsigned int i = 0; i < stats.capacity(); ++i)
    {
      stats[i] = 0.0f;
    }

    stats[NDb::STAT_LIFE] = playerInfo.hsHealth;
    stats[NDb::STAT_ENERGY] = playerInfo.hsMana;
    stats[NDb::STAT_STRENGTH] = playerInfo.hsStrength;
    stats[NDb::STAT_INTELLECT] = playerInfo.hsIntellect;
    stats[NDb::STAT_ATTACKSPEED] = playerInfo.hsAgility;
    stats[NDb::STAT_DEXTERITY] = playerInfo.hsCunning;
    stats[NDb::STAT_STAMINA] = playerInfo.hsFortitude;
    stats[NDb::STAT_WILL] = playerInfo.hsWill;
    stats[NDb::STAT_LIFEREGENERATIONABSOLUTE] = playerInfo.hsLifeRegen;
    stats[NDb::STAT_ENERGYREGENERATIONABSOLUTE] = playerInfo.hsManaRegen;
  }

  void PFBaseMaleHero::DoLevelups(int count, float statsBonusBudget)
  {
    float maxLevel = GetWorld()->GetAIWorld()->GetMaxHeroLevel();

    if (count < 1)
		  return;

    //¬ыдаем "размазанные" статы ( NUM_TASK )
    for (unsigned int i = 0; i < persistentStats.capacity(); ++i)
    {
      NDb::EStat stat = static_cast<NDb::EStat>(i);
      float statValue = persistentStats[stat]*count/maxLevel;

      PersistentBuff* buff = &persistentBuffs[stat];
      if(buff && !buff->applied)
      {
        statValue = statValue * buff->buffVal.mul + buff->buffVal.add;
        buff->applied = true;
      }

      AddStat( stat, statValue);
    }

    PFEaselPlayer::DoLevelups( count, statsBonusBudget );
  }

  void PFBaseMaleHero::DoHeroLevelups(const int count, const float statsBonusBudget, const float fraction)
  {
    if (count < 1)
      return;

    const float mod = static_cast<float>(count) * fraction;

    for (unsigned int i = 0; i < persistentStats.capacity(); ++i)
    {
      const NDb::EStat stat = static_cast<NDb::EStat>(i);

      float statValue = persistentStats[stat];

      {
        const PersistentBuff& buff = persistentBuffs[stat];

        statValue *= buff.buffVal.mul;
        statValue += buff.buffVal.add;
      }

      statValue *= mod;

      AddStat( stat, statValue);
    }

    // NOTE: NUM_TASK код ниже скопирован из методов базовых классов, внесены соответствующие правки

    if (count > 0)
    {
      UpgradeHeroStats(count, statsBonusBudget, fraction);

      naftaLevel += count;
      timeSinceLevelUp = 0.0f;

      OnLevelUp();
    }

    struct LevelSynchronizer: CloneFunc, ISummonAction
    {
      int level;

      LevelSynchronizer( int _level ) : level ( _level ) { }
      void operator()( PFBaseUnit* pUnit ) { pUnit->SetNaftaLevel( level ); }
      void operator()( PFBaseHero* pClone ) { operator()( (PFBaseUnit*)pClone ); }
    } f( naftaLevel );

    ForAllSummons( f, NDb::SUMMONTYPE_PRIMARY );
    ForAllSummons( f, NDb::SUMMONTYPE_SECONDARY );
    ForAllClones( f );
  }

  void PFBaseMaleHero::FillPersistentBuffs( const NCore::PlayerInfo playerInfo, TPersistentBuffs& buffs )
  {
    NCore::TPlayerBuffs::const_iterator it;
    for(it = playerInfo.hBuffs.begin();it != playerInfo.hBuffs.end();++it)
    {
      NDb::EStat stat = static_cast<NDb::EStat>(it->first);
      buffs[stat] = PersistentBuff(it->second);
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::DropCooldowns( DropCooldownParams const& dropCooldownParams )
  {
    PFBaseHero::DropCooldowns( dropCooldownParams );

    if ( ( dropCooldownParams.flags & NDb::ABILITYIDFLAGS_TALENTS ) != 0 )
    {
      DropTalentsCooldowns( dropCooldownParams );
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::LoadTalents(string const& strTalentsSetPath)
  {
    if (pTalents)
    {
      force = 0.0f; // reset force

      const NDb::AILogicParameters* aiParams = &GetWorld()->GetAIWorld()->GetAIParameters();
      NI_ASSERT( aiParams, "aiParams is empty" );
      Force::TalentsForceCalculator talentsForceCalculator( aiParams );

      pTalents->LoadPredefinedSet(this, strTalentsSetPath, talentsForceCalculator);
      force += talentsForceCalculator.GetResult();
      force += aiParams->forceParameters->baseHeroForce;

      pTalents->ActivateTakenOnStart();
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  PFTalent* PFBaseMaleHero::GetTalent(int level, int slot) const
  {
    NI_VERIFY( pTalents || isClone, "Male hero have no talents!", return 0 );

    return pTalents ? pTalents->GetTalent(level, slot) : 0;
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  ETalentActivation::Enum PFBaseMaleHero::CanActivateTalent(int level, int slot) const
  {
    return pTalents->CanActivateTalent( level, slot );
  }



bool PFBaseMaleHero::HasFreshTalentsToBuy() const
{
  return pTalents->HasFreshTalentsToBuy();
}


  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  bool PFBaseMaleHero::ActivateTalent(int level, int slot)
  {
    bool isActivated = pTalents->ActivateTalent( level, slot );

    if ( isActivated )
    {
      PFTalent * talent = pTalents->GetTalent(level, slot);

      if (talent->GetTalentDesc())
      {
        StatisticService::RPC::SessionEventInfo params;
        params.intParam1 = Crc32Checksum().AddString( talent->GetTalentDesc()->persistentId.c_str() ).Get();
        params.intParam2 = talent->GetNaftaCost();
        LogSessionEvent(SessionEventType::TalentUnlocked, params);
      }
    }

    return isActivated;
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::OnTalentActivated( const int level, const int slot, const float statsIncrementFraction )
  {
    int nLevelUps = CountLevelups();
    NI_ASSERT( nLevelUps >= 0 && nLevelUps <= 20, "Insane levelups" );

    if ( !isClone )
    {
      const float statsBonusBudget = Force::CalcHeroStatsBonusBudget( this, level );

      DoHeroLevelups( nLevelUps, statsBonusBudget, statsIncrementFraction );
      //DoLevelups(nLevelUps, statsBonusBudget);
      RecalculateManaCostModifier();
    }
    else
    {
      DoLevelups( nLevelUps );
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  bool PFBaseMaleHero::CanUseTalent(int level, int slot) const
  {
    return CanUseTalent(pTalents->GetTalent(level, slot));
  }

  bool PFBaseMaleHero::CanUseTalent( PFTalent * talent ) const
  {
    return IsValid(talent) && talent->CanBeUsed();
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  CObj<PFAbilityInstance> PFBaseMaleHero::UseTalent( int level, int slot, Target const& target )
  {
    return UseTalent( pTalents->GetTalent( level, slot ), target );
  }

  CObj<PFAbilityInstance> PFBaseMaleHero::UseTalent( PFTalent* talent, Target const& target )
  {
    NI_VERIFY( talent && talent->IsActivated(), "There is no talent in given slot!", return NULL; );
    NI_VERIFY( talent->CanBeUsed(), "Can not use talent!", return NULL; );

    CObj<PFAbilityInstance> pInstance = NULL;

    if ( !talent->IsOn() )
    {
      if ( !talent->IsMultiState() )
      {
        pInstance = CreateAbilityInstance( talent, target );
      }
      else
      {
        pInstance = talent->Toggle( target );
      }

      if ( pInstance )
      {
        EventHappened( PFBaseUnitAbilityStartEvent( pInstance.GetPtr() ) );

        ELookKind lookAtTarget = pInstance->IsCasterShouldLookAtTarget();
        if ( lookAtTarget != DontLook )
        {
          SetMoveDirection( target.AcquirePosition().AsVec2D() );
        }
        CALL_CLIENT_6ARGS( OnUseAbility, talent->GetAbilityNode(), talent->GetAbilityMarker(), target, talent->GetTimeOffset( true ), lookAtTarget, talent->IsAbilitySupposedToSyncVisual() );

        // Save last talent usage step for scripting needs
        lastTalentUseStep = pWorld->GetStepNumber();
        talent->SetLastUseStep( lastTalentUseStep );
      }
    }
    else // switchable talent assumed
    {
      // It is supposed that switchable abilities have single instance at a time
      if ( talent->GetActiveInstancesCount() > 0 )
      {
        EventHappened( PFBaseUnitAbilityStartEvent( talent->GetActiveInstances()[0] ) );
      }

      talent->Toggle( target );

      if (talent->GetTalentDesc())
      {
        LogSessionEvent( SessionEventType::TalentSwitchedOff, 
          Crc32Checksum().AddString( talent->GetTalentDesc()->persistentId.c_str() ).Get());
      }
    }

    return pInstance;
  }


  int PFBaseMaleHero::GetDevPoints() const { return IsValid(pTalents) ? pTalents->GetDevPoints() : 0; };

  int const &PFBaseMaleHero::GetDevPointsRef() const
  {
    return pTalents->GetDevPoints();
  }

  void PFBaseMaleHero::ModifyForceByStats( const PFBaseMaleHero::TPersistentStats& stats, const NDb::AILogicParameters* pAIParams, float& force )
  {
    float statsPoints = 0;
    for (unsigned int i = 0; i < stats.capacity(); ++i)
    {
      NDb::StatBudget const* statBudget = PFStatContainer::GetStatBudget( pAIParams, (NDb::EStat)i );
      if ( statBudget )
      {
        statsPoints += stats[i] / statBudget->budget;
      }
    }
    statsPoints *= pAIParams->forceParameters->masteryPointForce;
    force += statsPoints;
  }

  float PFBaseMaleHero::GetForce( bool countPersistentStats /*= false*/) const
  {
    float force = PFBaseHero::GetForce( countPersistentStats );
    if ( countPersistentStats )
      ModifyForceByStats(persistentStats, &GetWorld()->GetAIWorld()->GetAIParameters(), force);
    
    return force;
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float PFBaseMaleHero::GetManaCostModifier( bool altCost /*= false*/ ) const
  {
    return altCost ? lifeCostForceModifier : manaCostForceModifier;
  }

  void PFBaseMaleHero::RecalculateManaCostModifier()
  {
    Force::CalcHeroAbilityCostFactor( this, manaCostForceModifier, lifeCostForceModifier );
  }

  float PFBaseMaleHero::GetTalentsAcquiredBudgetPercent() const
  {
    return pTalents->GetAcquiredBudgetPercent();
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::DropTalentsCooldowns( DropCooldownParams const& dropCooldownParams ) const
  {
    struct DropCooldownFunc : public NonCopyable
    {
      DropCooldownFunc( DropCooldownParams const& dropCooldownParams ) : dropCooldownParams(dropCooldownParams) {};

      virtual void operator()(PFTalent *pTalent, int level, int slot)
      {
        if ( pTalent->IsReady() )
          return;

        if ( dropCooldownParams.talentsList.empty() )
        {
          if ( pTalent != dropCooldownParams.exceptAbility )
            pTalent->DropCooldown(true, dropCooldownParams.cooldownReduction, dropCooldownParams.reduceByPercent);
        }
        else
        {
          if ( PFAbilityData::IsAbilitySuitable( pTalent->GetTalentDesc(), dropCooldownParams.talentsList, dropCooldownParams.mode ) )
          {
            pTalent->DropCooldown(true, dropCooldownParams.cooldownReduction, dropCooldownParams.reduceByPercent);
          }
        }
      }

      DropCooldownParams const& dropCooldownParams;
    } dropCooldownFunc( dropCooldownParams );

    //drop cooldown for portal
    if ( portal )
      dropCooldownFunc(portal, 0, 0);

    ForAllTalents( dropCooldownFunc );
  }

  void PFBaseMaleHero::RestartGroupCooldowns( PFTalent const* pTalent )
  {
    NDb::TalentGroup const* group = pTalent->GetGroup();
    vector<PFTalent*> const& vecTalents = pTalents->GetTalentsFromGroup( group );

    for ( vector<PFTalent*>::const_iterator it = vecTalents.begin(); it != vecTalents.end(); ++it )
    {
      if ( *it != pTalent && (*it)->GetCurrentCooldown() < group->cooldown )
      {
        (*it)->PFAbilityData::RestartCooldown( group->cooldown );
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  const IUnitFormulaPars* PFBaseMaleHero::GetObjectFavorite() const
  {
    return 0;
  }

  void PFBaseMaleHero::ExecuteCommandUseConsumable( int slot, const Target& target, bool isPlayerCommand )
  {
    CObj<NCore::WorldCommand> pCommand = NWorld::CreateCmdUseConsumable( this, slot, target );
    
    if( IsValid( pCommand ) )
    {
      if ( NGameX::AdventureScreen::Instance() )
      {
        NGameX::AdventureScreen::Instance()->SendGameCommand( pCommand, isPlayerCommand );
      }
    }
  }

  void PFBaseMaleHero::DoPetLogic()
  {
    if ( IsLocal() )
    {
      // 1. "default"
      if ( g_current_pet == 0 )
      {
        // a. - ничего не делаем

        // b. - вызываем случайного и прописываем pet X
        for ( int i = 0; i < consumables.size(); i++ )
        {
          if ( CanUseConsumable( i ) )
          {
            ExecuteCommandUseConsumable( i, Target( this ), false );
            break;
          }
        }
      }
      // 2. "no pet"
      else if ( g_current_pet == -1 )
      {
        for ( int i = 0; i < consumables.size(); i++ )
        {
          PFConsumable* cons = consumables[i];
          if ( cons )
          {
            cons->SetActionBarIndex( -2 );
          }
        }
      }
      // 3. "pet X"
      else
      {
        // a. также как "default"
        if ( inventory.find( g_current_pet ) == inventory.end() )
        {
          g_current_pet = 0;
          for ( int i = 0; i < consumables.size(); i++ )
          {
            if ( CanUseConsumable( i ) )
            {
              ExecuteCommandUseConsumable( i, Target( this ), false );
              break;
            }
          }
        }
        // b. помещаем одного на actionBar, вызываем
        else
        {
          for ( int i = 0; i < consumables.size(); i++ )
          {
            PFConsumable* cons = consumables[i];
            if ( cons )
            {
              int persId = Crc32Checksum().AddString( cons->GetDBDesc()->persistentId.c_str() ).Get();
              if ( persId != g_current_pet )
                cons->SetActionBarIndex( -2 );
            }
          }
          for ( int i = 0; i < consumables.size(); i++ )
          {
            PFConsumable* cons = consumables[i];
            if ( cons && CanUseConsumable( i ) )
            {
              int persId = Crc32Checksum().AddString( cons->GetDBDesc()->persistentId.c_str() ).Get();
              if ( persId == g_current_pet )
              {
                g_current_pet = 0;
                ExecuteCommandUseConsumable( i, Target( this ), false );
                break;
              }
            }
          }
        }
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  void PFBaseMaleHero::LoadInventory( const NWorld::PFResourcesCollection* collection )
  {
    for ( nstl::vector<int>::const_iterator it = inventory.begin(); it != inventory.end(); ++it )
    {
      NDb::Ptr<NDb::Consumable> consumable = collection->FindConsumableById( *it );
      NDb::Precache<NDb::Consumable>(consumable->GetDBID(), 10);
      TakeConsumable( consumable, 1 );
    }
    for ( int i = 0; i < consumables.size(); i++ )
    {
      if ( consumables[i] )
      {
        consumables[i]->SetPet( true );
      }
    }
  }

  int PFBaseMaleHero::GetEffectiveClassMask() const
  {
    const vector<float>& efficiency = static_cast<const NDb::Hero*>(GetDbHero())->classEfficiency;

    int res = 0;
    for ( int i = 1; i < efficiency.size(); ++i )
    {
      if ( efficiency[i] > 0 )
        res |= 1 << (i-1);
    }
    return res;
  }

} // namespace NWorld;

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFBaseMaleHero, NWorld)

REGISTER_VAR( "current_pet", g_current_pet, STORAGE_USER );

#endif
