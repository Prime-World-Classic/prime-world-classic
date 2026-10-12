/** Fixture-only metadata placement; scoring, targeting, history and rings stay engine-owned. */
struct WeightedUnitB12 : ProbeUnit
{
	bool aggressive = false;
	WeightedUnitB12(PFWorld* world, const NDb::Unit* db, NDb::EFaction faction, NDb::EUnitType type)
		: ProbeUnit(world, db, faction) { SetUnitType(type); }
	void Place(const CVec3& p)
	{
		position = p;
		if (GetWorld() && std::isfinite(p.x) && std::isfinite(p.y)) GetWorld()->GetAIWorld()->OnUnitMove(*this);
	}
	bool IsAggressiveNeutralCreep() const override { return aggressive; }
	void HeroClock(float value) { lastHeroAttack.timePassed = value; }
	void AttackClock(float value) { lastAttack.timePassed = value; }
};

/** Zero only optional weights so each fixture specifies its exact arithmetic. */
static NDb::Ptr<NDb::UnitTargetingParameters> WeightedParamsB12()
{
	NDb::UnitTargetingParameters* p = new NDb::UnitTargetingParameters;
	p->attackerMeWeight = p->attackerTowerWeight = p->attackerMasterWeight = p->attackerAllyWeight = 0;
	p->healthWeight = 0; p->targetingRange = 20; p->chaseRange = 20; p->maxTimeFromAttack = 2;
	return p;
}

static CObj<WeightedUnitB12> WeightedUnitCreateB12(PFWorld* world, const NDb::UnitTargetingParameters* params,
	NDb::EFaction faction = NDb::FACTION_FREEZE, NDb::EUnitType type = NDb::UNITTYPE_CREEP, bool attack = false)
{
	CObj<PFBaseUnit> seed = Unit(nullptr, faction, 40);
	NDb::Unit* raw = new NDb::Unit; raw->stats = seed->DbUnitDesc()->stats; raw->targetingParams = params;
	if (attack) raw->attackAbility = new NDb::BaseAttack;
	NDb::Ptr<NDb::Unit> db = raw;
	return new WeightedUnitB12(world, db, faction, type);
}

static bool WeightedNearB12(float actual, float expected)
{
	return std::isfinite(actual) && std::fabs(actual - expected) < .0001f;
}

/** Supply original virtual policy inputs without replacing target weight calculations. */
struct WeightedPolicyB12 : PFSummonBehaviour
{
	bool unleashed = false;
	bool allied = true;
	WeightedPolicyB12() { faction = NDb::FACTION_FREEZE; unitType = NDb::UNITTYPE_CREEP; }
	bool IsUnleashed() const override { return unleashed; }
	void BindMaster(PFBaseUnit* master) { pMaster = master; }
	void HeroLike(bool value) { behaviourFlags = value ? BEHAVIOURFLAGS_LIKEHERO : BEHAVIOURFLAGS_NONE; }
	bool IsAttackedUnitAlly(const CPtr<PFBaseUnit>&, NDb::EFaction) const override { return allied; }
};

/** Exact original arithmetic, branch switches, 3D center distance and signed tables. */
static void WeightedTermsB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto target = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	const auto score = [&]() { return owner->GetTargetWeight(target.GetPtr(), *p); };
	check(score() == 0, "zero optional authored weights give zero, not an invented default");
	p->unitTypeTableWeight = 2; p->unitTargetWeight[NDb::UNITTYPE_CREEP] = 3;
	check(score() == 6, "unit-type table multiplies its authored coefficient");
	target->SetUnitType(NDb::UNITTYPE_BUILDING); p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -100;
	check(score() == -200, "authored negative building weights are not clamped away");
	target->SetUnitType(NDb::UNITTYPE_CREEP); p->unitTypeTableWeight = 0;
	target->Place(CVec3(11, 5, 8)); p->rangeWeight = 10;
	check(WeightedNearB12(score(), 5), "range weight uses three-dimensional center distance, not planar or surface distance");
	p->weightMaximumRangePersent = .5f;
	check(WeightedNearB12(score(), 10), "maximum-range percentage scales and saturates the original range term");
	p->weightMaximumRangePersent = 1;
	check(WeightedNearB12(score(), 5), "percentage at one follows the original alternate denominator");
	p->weightMaximumRangePersent = 2;
	check(WeightedNearB12(score(), 5), "percentage above one retains the original alternate branch");
	p->weightMaximumRangePersent = -.5f;
	check(WeightedNearB12(score(), 10.0f / 3), "finite negative percentage retains authored arithmetic");
	target->Place(CVec3(35, 5, 0));
	check(score() == 0, "distance beyond targeting range clamps its term to zero");
	p->rangeWeight = 0; p->healthWeight = 8; target->SetHealth(250);
	check(score() == 6, "health term uses remaining life fraction");
	p->healthWeight = 0; p->OutOfAggroWeight = -7; p->aggroRange = -1;
	check(score() == -7, "negative aggro sentinel falls back to targetingRange");
	target->Place(CVec3(25, 5, 0));
	check(score() == 0, "aggro boundary is strict greater-than");
	p->aggroRange = 5;
	check(score() == -7, "explicit smaller aggro radius supplies its penalty");
	owner->AssignTarget(target.GetPtr(), false);
	check(score() == 0, "current target skips out-of-aggro penalty");
	p->currentTargetWeight[NDb::UNITTYPE_CREEP] = 4; p->currentTargetTableWeight = 3;
	check(score() == 12, "current-target table is multiplied only for exact resolved identity");
	owner->DropTarget();
	check(score() == -7, "dropping current target removes its table bonus");
	p->OutOfAggroWeight = 0; p->aggressiveNeutralWeight = 9; target->aggressive = true;
	check(score() == 9, "aggressive-neutral type bonus is additive when the type table is used");
}

/** Attack history separates aggressive true-hero hits and keeps strict timeout semantics. */
static void WeightedHistoryB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto attacker = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto ally = WeightedUnitCreateB12(f.world, p);
	auto hero = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE);
	const auto score = [&]() { return owner->GetTargetWeight(attacker.GetPtr(), *p); };
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_CREEP] = 2;
	p->attackerMeWeight = 3; p->attackerAllyWeight = 5; p->attackerMasterWeight = 7; p->attackerTowerWeight = 11;
	p->attackerHeroWeight[NDb::UNITTYPE_CREEP] = 13; p->attackerHeroTableWeight = 2;
	p->autoAttackWeight = 17; p->talentAttackWeight = 19; p->attackerNearbyHeroWeight = 23;
	attacker->SetLastAttackData(owner.GetPtr(), true, NDb::FACTION_FREEZE);
	check(score() == 22, "recent auto attack on self adds type, auto and attackerMe weights");
	attacker->SetLastAttackData(ally.GetPtr(), false, NDb::FACTION_FREEZE);
	check(score() == 26, "talent attack on ordinary ally uses attackerAlly weight");
	ally->SetUnitType(NDb::UNITTYPE_TOWER);
	check(score() == 32, "attacked allied tower uses attackerTower weight");
	owner->SetMaster(ally.GetPtr());
	check(score() == 28, "attacked master takes precedence over tower classification");
	owner->SetMaster(nullptr);
	attacker->SetLastAttackDataEx(hero.GetPtr(), true, true, NDb::FACTION_FREEZE);
	check(attacker->GetLastHeroAttackData()->pVictim == hero.GetPtr() && attacker->GetLastAttackData()->pVictim == ally.GetPtr(),
		"aggressive true-hero dispatch writes hero history without overwriting ordinary attack history");
	check(score() == 66, "recent hero aggression replaces type weight with hero table and adds nearby and auto weights");
	owner->SetUnitType(NDb::UNITTYPE_TOWER);
	check(score() == 68, "tower owner retains its unit-type weight in addition to attacker-hero table");
	owner->SetUnitType(NDb::UNITTYPE_CREEP); attacker->aggressive = true; p->aggressiveNeutralAttackingHeroWeight = 29;
	check(score() == 95, "aggressive-neutral hero attacker uses the dedicated extra weight");
	attacker->aggressive = false;
	attacker->HeroClock(2);
	check(score() == 32, "hero history expires at exact maxTimeFromAttack and falls back to ordinary history");
	attacker->AttackClock(2);
	check(score() == 2, "ordinary history also expires at equality");
	p->maxTimeFromAttack = -1;
	check(score() == 66, "negative authored attack-history timeout is unlimited, unlike priority retention");
	p->maxTimeFromAttack = 2;
	attacker->RemoveLastHeroAttackData(); attacker->RemoveLastAttackData();
	attacker->SetLastAttackDataEx(hero.GetPtr(), false, false, NDb::FACTION_FREEZE);
	check(!IsValid(attacker->GetLastHeroAttackData()->pVictim) && attacker->GetLastAttackData()->pVictim == hero.GetPtr(),
		"nonaggressive true-hero dispatch remains ordinary history");
	check(score() == 45, "ordinary history of a true hero uses hero table without nearby hero bonus");
	hero->ChangeFaction(NDb::FACTION_BURN);
	check(score() == 45, "original victim faction keeps the original ally classification after faction change");
	attacker->SetLastAttackData(hero.GetPtr(), false, NDb::FACTION_BURN);
	check(score() == 2, "enemy victim and enemy original faction give no ally attack bonus");
	auto clone = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE, true);
	attacker->SetLastAttackDataEx(clone.GetPtr(), true, true, NDb::FACTION_FREEZE);
	check(!IsValid(attacker->GetLastHeroAttackData()->pVictim) && attacker->GetLastAttackData()->pVictim == clone.GetPtr(),
		"hero clone does not populate aggressive true-hero history");
	check(!clone->IsLikeHero() && score() == 24, "clone without alpha behavior uses ordinary ally weight, not hero-like weight");
	auto summon = WeightedUnitCreateB12(f.world, p, NDb::FACTION_FREEZE, NDb::UNITTYPE_SUMMON);
	CObj<WeightedPolicyB12> policy = new WeightedPolicyB12;
	policy->SetUnitType(NDb::UNITTYPE_SUMMON); policy->BindMaster(hero); policy->HeroLike(true);
	summon->AddBehaviourOnTop(policy); hero->SetAlphaSummon(summon.GetPtr());
	check(summon->IsLikeHero(), "real alpha master binding and authored summon behavior supply IsLikeHero");
	attacker->SetLastAttackDataEx(summon.GetPtr(), true, true, NDb::FACTION_FREEZE);
	check(attacker->GetLastHeroAttackData()->pVictim == summon.GetPtr() && score() == 66,
		"aggressive hero-like alpha hit enters hero history and original nearby weighting");
	hero->SetAlphaSummon(nullptr);
	check(!summon->IsLikeHero(), "ordinary summon with the same behavior flag is not hero-like without alpha ownership");
	summon->RemoveBehaviour(policy);
}

/** Master/current alpha resolution and summon-specific type suppression use existing bindings. */
static void WeightedMastersB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto target = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN, NDb::UNITTYPE_SUMMON);
	auto master = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN, NDb::UNITTYPE_HEROMALE);
	target->SetMaster(master.GetPtr()); master->SetAlphaSummon(target.GetPtr());
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_SUMMON] = 3; p->unitTargetWeight[NDb::UNITTYPE_HEROMALE] = 7;
	const auto score = [&]() { return owner->GetTargetWeight(target.GetPtr(), *p); };
	check(score() == 7, "alpha target uses its real master's type table");
	p->isStationarySummon = true;
	check(score() == 3, "stationary-summon switch preserves the alpha target instead of substituting its master");
	p->isStationarySummon = false;
	owner->AssignTarget(master.GetPtr(), false);
	p->currentTargetWeight[NDb::UNITTYPE_HEROMALE] = 5; p->currentTargetTableWeight = 2;
	check(score() == 17, "current master and alpha candidate resolve to the same identity");
	owner->DropTarget();
	auto allyMaster = WeightedUnitCreateB12(f.world, p);
	owner->SetMaster(allyMaster.GetPtr()); allyMaster->AssignTarget(master.GetPtr(), false); p->masterTargetWeight = 11;
	check(score() == 18, "master current target contributes original master target weight");
	owner->GetStat(NDb::STAT_RANGE)->SetBaseValue(2);
	allyMaster->Place(CVec3(27, 5, 0));
	check(score() == 18, "master leash accepts exact chase plus attack range");
	allyMaster->Place(CVec3(28, 5, 0));
	check(score() == 0, "original master leash returns zero beyond the boundary");
	CObj<WeightedPolicyB12> policy = new WeightedPolicyB12;
	policy->unleashed = true; policy->BindMaster(allyMaster); owner->AddBehaviourOnTop(policy);
	check(score() == 18, "existing unleashed-summon virtual policy bypasses the master leash");
	owner->RemoveBehaviour(policy);
	owner->SetMaster(nullptr); master->SetAlphaSummon(nullptr);
	auto hero = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE);
	master->SetLastAttackDataEx(hero.GetPtr(), true, true, NDb::FACTION_FREEZE);
	p->attackerHeroSummonWeight = 31;
	check(score() == 31, "ordinary summon whose master attacks an ally hero replaces its type-table term");
	p->attackerHeroSummonWeight = 0;
	check(score() == 3, "disabled summon attacker weight preserves original unit-type table");
}

/** Ally convergence uses scream radius; sibling exclusion uses faction and DB identity. */
static void WeightedAlliesB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto target = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	CObj<WeightedUnitB12> ally = new WeightedUnitB12(f.world, owner->DbUnitDesc(), NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP);
	p->allyTargetWeight = 9; ally->AssignTarget(target.GetPtr(), false);
	const auto score = [&]() { return owner->GetTargetWeight(target.GetPtr(), *p); };
	check(score() == 9, "nearby allied current target contributes one allyTargetWeight");
	owner->AssignTarget(target.GetPtr(), false);
	check(score() == 0, "current target skips ally convergence weight");
	owner->DropTarget(); ally->Place(CVec3(50, 5, 0));
	check(score() == 0, "ally outside authored scream range gives no convergence bonus");
	p->ignoreSiblingTarget = true;
	check(score() == -1, "same-DB same-faction attacker ring suppresses a sibling target regardless of distance");
	ally->ChangeFaction(NDb::FACTION_BURN);
	check(score() == 0, "same DB in another faction is not a sibling");
	ally->ChangeFaction(NDb::FACTION_FREEZE); ally->DropTarget();
	check(score() == 0, "dropping sibling target removes it from the original attackers ring");
	NDb::Ptr<NDb::Unit> named = static_cast<NDb::Unit*>(NDb::GetDbTypesRegistrator().Create(NDb::DBID("/Mock/WeightedSibling.xdb"), "Unit", true));
	check(bool(named), "real DB factory provides a distinct loaded sibling identity");
	if (named)
	{
		auto* db = const_cast<NDb::Unit*>(named.GetPtr()); db->stats = owner->DbUnitDesc()->stats; db->targetingParams = p;
		CObj<WeightedUnitB12> different = new WeightedUnitB12(f.world, db, NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP);
		different->AssignTarget(target.GetPtr(), false);
		check(score() == 9, "same faction but different DB does not trigger sibling exclusion");
		different->DropTarget();
	}
	p->ignoreSiblingTarget = false; p->attackerAllyWeight = 7;
	target->SetLastAttackData(ally.GetPtr(), true, NDb::FACTION_FREEZE);
	CObj<WeightedPolicyB12> policy = new WeightedPolicyB12; policy->allied = false; owner->AddBehaviourOnTop(policy);
	check(score() == 0, "active behavior can reject default attacked-ally classification");
	policy->allied = true;
	check(score() == 7, "active behavior acceptance feeds the original attacker-ally term");
	owner->RemoveBehaviour(policy);
}

/** Custom formulas use live attack ability context and the actual alpha, not its master. */
static void WeightedCustomB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p, NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP, true);
	auto alpha = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN, NDb::UNITTYPE_SUMMON);
	auto master = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	alpha->SetMaster(master.GetPtr()); master->SetAlphaSummon(alpha.GetPtr());
	alpha->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(17); master->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(93);
	p->customWeight.sString = "sStrength+rStrength";
	const auto score = [&]() { return owner->GetTargetWeight(alpha.GetPtr(), *p); };
	check(IsValid(owner->GetAttackAbilityData()) && score() == 57, "custom formula receives actual alpha target while owner attack ability is live");
	alpha->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(23);
	check(score() == 63, "custom score rereads live target stats without caching a previous result");
	for (const char* text : {"1/0", "1e39", "unknown(rStrength)"})
	{
		p->customWeight.sString = text;
		check(score() <= -1, "invalid or unsupported selected custom formula rejects the candidate");
	}
	p->customWeight.sString = ""; p->customWeight.compiledString = "unsupported-bytecode";
	check(score() <= -1, "compiled-only selected formula is not silently treated as zero");
	p->customWeight.compiledString = "";
	check(score() == 0, "absent custom formula retains the original zero default");
	auto noAttack = WeightedUnitCreateB12(f.world, p);
	p->customWeight.sString = "unknown(1)";
	check(noAttack->GetTargetWeight(alpha.GetPtr(), *p) == 0, "custom formula is not evaluated without an attack ability");
}

/** Mock candidate enumeration only; the selector's production scoring and acceptance remain real. */
struct WeightedListB12 : PFTargetSelector
{
	std::vector<Target> candidates;
	WeightedListB12(const NDb::TargetSelector& db, PFWorld* world) : PFTargetSelector(db, world) {}
	void EnumerateTargets(ITargetAction& action, const RequestParams&) override
	{
		for (const Target& target : candidates) action(target);
	}
};

struct WeightedSelectorB12 : PFWeightTargetSelector
{
	WeightedSelectorB12(const NDb::WeightTargetSelector& db, PFWorld* world, PFTargetSelector* list)
		: PFWeightTargetSelector(db, world) { pTargetSelector = list; }
};

static void WeightedSelectorContractB12(Checks& check)
{
	B8BehaviourForeignWorld f(check), other(check); if (!f.world || !other.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto a = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN, NDb::UNITTYPE_BUILDING);
	auto b = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto foreign = WeightedUnitCreateB12(other.world, p, NDb::FACTION_BURN);
	B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE), foreignReveal(other.world, NDb::FACTION_FREEZE);
	NDb::Ptr<NDb::AreaTargetSelector> listDb = new NDb::AreaTargetSelector;
	NDb::Ptr<NDb::WeightTargetSelector> selectorDb = new NDb::WeightTargetSelector;
	CObj<WeightedListB12> list = new WeightedListB12(*listDb, f.world);
	CObj<WeightedSelectorB12> selector = new WeightedSelectorB12(*selectorDb, f.world, list);
	const Target requester(owner.GetPtr());
	PFTargetSelector::RequestParams request(owner.GetPtr(), static_cast<const IMiscFormulaPars*>(nullptr), requester);
	Target selected;
	list->candidates = {Target(a.GetPtr())}; p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -100;
	check(!selector->FindTarget(request, selected), "weight selector does not accept its first excluded building");
	p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -1;
	check(!selector->FindTarget(request, selected), "weight selector shares the strict minus-one baseline");
	p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -.5f;
	check(selector->FindTarget(request, selected) && selected.GetUnit() == a.GetPtr(), "weight selector accepts finite scores above minus one");
	p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = 0; list->candidates = {Target(b.GetPtr()), Target(a.GetPtr())};
	check(selector->FindTarget(request, selected) && selected.GetUnit() == b.GetPtr(), "weight selector equal scores retain enumerator order");
	p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = 4;
	check(selector->FindTarget(request, selected) && selected.GetUnit() == a.GetPtr(), "weight selector consumes authored score rather than list position");
	list->candidates = {Target(foreign.GetPtr())};
	check(!selector->FindTarget(request, selected), "weight selector rejects a cross-world candidate even if its own world reveals it");
}

/** Manual priority retention now affects production selection without bypassing exclusion gates. */
static void WeightedSelectionB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto first = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto second = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	// Same voxel keeps the engine's ID ordering independent of distance.
	first->Place(CVec3(9, 5, 0)); second->Place(CVec3(6, 5, 0));
	B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
	std::vector<PFBaseUnit*> order;
	const auto collect = [&](PFBaseUnit& unit) { if (owner->CanSelectTarget(&unit, true)) order.push_back(&unit); };
	f.world->GetAIWorld()->ForAllUnitsInRange(owner->GetPosition(), 20, collect,
		UnitMaskingPredicate(owner->GetOppositeFactionFlags(), NDb::SPELLTARGET_ALL, owner));
	check(order.size() == 2, "selection fixture contains exactly two eligible indexed enemy targets");
	if (order.size() != 2) return;
	check(!order.empty() && owner->FindTarget(20, false) == order.front(), "equal scores retain original engine enumeration order, not nearest distance");
	p->rangeWeight = 10;
	check(owner->FindTarget(20, false) == second.GetPtr(), "authored range weighting chooses the nearer target");
	p->attackerNearbyHeroWeight = 100;
	owner->AddToPriorityTargets(first.GetPtr());
	check(owner->FindTarget(20, false) == first.GetPtr(), "manual priority API now supplies authored nearby-hero weight to acquisition");
	owner->UpdatePriorityTargets(2);
	check(owner->FindTarget(20, false) == second.GetPtr(), "priority expiry restores authored distance preference");
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -100;
	first->SetUnitType(NDb::UNITTYPE_BUILDING); second->SetUnitType(NDb::UNITTYPE_BUILDING);
	check(!owner->FindTarget(20, false), "all negatively weighted buildings yield no target, never a nearest fallback");
	p->rangeWeight = 0; p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -1;
	check(!owner->FindTarget(20, false), "weight equal to the original minus-one sentinel is excluded");
	p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -.5f;
	check(owner->FindTarget(20, false) == order.front(), "finite negative weight above minus one is selectable and preserves ties");
	first->SetUnitType(NDb::UNITTYPE_CREEP); second->SetUnitType(NDb::UNITTYPE_CREEP);
	p->rangeWeight = 10; owner->AddToPriorityTargets(first.GetPtr());
	for (auto flag : {NDb::UNITFLAG_FORBIDAUTOTARGETME, NDb::UNITFLAG_FORBIDTAKEDAMAGE, NDb::UNITFLAG_FLYING})
	{
		first->AddFlag(flag);
		check(owner->FindTarget(20, false) == second.GetPtr(), "priority cannot bypass existing target eligibility");
		first->RemoveFlag(flag);
	}
	f.world->GetAIWorld()->UnregisterObjectOrUnit(first);
	check(owner->FindTarget(20, false) == second.GetPtr(), "priority cannot resurrect an unindexed target");
	f.world->GetAIWorld()->RegisterUnit(first);
	first->Place(CVec3(50, 5, 0));
	check(owner->FindTarget(20, false) == second.GetPtr(), "priority cannot bypass acquisition range or live faction fog");
	owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	check(!owner->FindTarget(20, false) && owner->FindTarget(20, false, 0, false) == second.GetPtr(), "existing native forbid-select and explicit bypass policy remain unchanged");
}

/** Reject selected malformed terms and foreign context without poisoning otherwise disabled terms. */
static void WeightedGuardsB12(Checks& check)
{
	B8BehaviourForeignWorld f(check), other(check); if (!f.world || !other.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto target = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto foreign = WeightedUnitCreateB12(other.world, p, NDb::FACTION_BURN);
	const auto score = [&]() { return owner->GetTargetWeight(target.GetPtr(), *p); };
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	check(owner->GetTargetWeight(foreign.GetPtr(), *p) <= -1, "foreign target context cannot acquire a valid score");
	check(owner->GetTargetWeight(CPtr<PFBaseUnit>(), *p) <= -1, "missing target is rejected rather than becoming a selectable zero");
	target->SetMaster(foreign.GetPtr()); foreign->SetAlphaSummon(target.GetPtr());
	check(score() <= -1, "alpha master from another world cannot supply type or priority context");
	foreign->SetAlphaSummon(nullptr); target->SetMaster(nullptr);
	owner->SetMaster(foreign.GetPtr());
	check(score() <= -1, "owner master from another world cannot influence weighted targeting");
	owner->SetMaster(nullptr);
	for (float value : {nan, inf, -inf})
	{
		p->rangeWeight = value;
		check(score() <= -1, "nonfinite selected range coefficient fails closed");
		p->rangeWeight = 0; p->targetingRange = value;
		check(score() <= -1, "nonfinite targeting range fails closed");
		p->targetingRange = 20;
		target->Place(CVec3(8, 5, value));
		check(score() <= -1, "nonfinite height is rejected before original three-dimensional weight arithmetic");
		target->Place(CVec3(8, 5, 0));
	}
	p->targetingRange = 0;
	check(score() <= -1, "zero authored targeting range is not divided by or silently substituted");
	p->targetingRange = 20; p->unitTypeTableWeight = 1; p->unitTargetWeight.clear();
	check(score() <= -1, "truncated active unit-type table fails closed");
	p->unitTypeTableWeight = 0;
	check(score() == 0, "zero coefficient can disable a missing optional table");
	p->currentTargetWeight.clear();
	check(score() == 0, "unused current-target table does not poison unrelated scoring");
	owner->AssignTarget(target.GetPtr(), false);
	check(score() <= -1, "selected current-target branch requires a bounded table index");
	p->currentTargetTableWeight = 0;
	check(score() == 0, "zero current-target coefficient disables its missing table");
	owner->DropTarget();
	p->attackerHeroWeight.clear();
	check(score() == 0, "unused attacker-hero table does not reject an unrelated target");
	auto hero = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE);
	target->SetLastAttackDataEx(hero.GetPtr(), true, true, NDb::FACTION_FREEZE);
	check(score() <= -1, "active attacker-hero branch rejects a truncated table");
	p->attackerHeroTableWeight = 0; p->attackerNearbyHeroWeight = 9;
	check(score() == 9, "zero table coefficient disables the missing hero table without disabling independent nearby weight");
	target->HeroClock(nan);
	check(score() == 0, "nonfinite history clock cannot qualify for a finite nearby weight");
	target->HeroClock(-1);
	check(score() == 0, "negative history clock is not a recent attack");
	target->RemoveLastHeroAttackData();
	target->SetUnitType(static_cast<NDb::EUnitType>(-1));
	check(score() <= -1, "invalid target type cannot index authored tables");
	target->SetUnitType(NDb::UNITTYPE_CREEP);
	p->rangeWeight = std::numeric_limits<float>::max(); p->healthWeight = std::numeric_limits<float>::max(); target->SetHealth(1);
	check(score() <= -1, "finite terms overflowing their final float score fail closed");
	p->rangeWeight = p->healthWeight = 0; p->weightMaximumRangePersent = nan;
	check(score() <= -1, "nonfinite percentage cannot select a denominator branch");
	p->weightMaximumRangePersent = 0;
	// The unsupported path map must be rejected before any dereference.
	check(owner->GetTargetWeight(target.GetPtr(), *p, reinterpret_cast<PathMap*>(owner.GetPtr())) <= -1,
		"explicit path-map requests are rejected instead of approximated by center distance");
}

/** Missing bootstrap DB keeps its old nearest route; authored rejection never uses that route. */
static void WeightedUnconfiguredB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto owner = WeightedUnitCreateB12(f.world, nullptr);
	auto first = WeightedUnitCreateB12(f.world, nullptr, NDb::FACTION_BURN);
	auto second = WeightedUnitCreateB12(f.world, nullptr, NDb::FACTION_BURN);
	first->Place(CVec3(9, 5, 0)); second->Place(CVec3(6, 5, 0));
	B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
	check(!owner->GetTargetingParamsPtr() && owner->FindTarget(20, false) == second.GetPtr(),
		"unconfigured bootstrap units retain existing nearest-distance selection");
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_CREEP] = -100;
	owner->OverrideTargetingParams(p);
	check(!owner->FindTarget(20, false), "live authored exclusions never fall back to the unconfigured nearest route");
}

/** Real cast/dispatch creation populates the history consumed by the original hero-attacker branch. */
static void WeightedDispatchB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto attacker = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto hero = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE);
	hero->TeleportTo(CVec2(15, 5), true, false);
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_CREEP] = 3;
	p->attackerHeroWeight[NDb::UNITTYPE_CREEP] = 11; p->talentAttackWeight = 7; p->attackerNearbyHeroWeight = 5;
	NDb::Ability* raw = new NDb::Ability;
	raw->manaCost.sString = "7";
	NDb::DispatchLinear* linear = new NDb::DispatchLinear;
	linear->speed = 10; linear->aiming = NDb::DISPATCHAIMING_HOMING; linear->flags = NDb::DISPATCHFLAGS_AGGRESSIVE;
	linear->dieAfterApply = true;
	raw->dispatch = linear;
	NDb::Ptr<NDb::Ability> db = raw;
	CObj<PFAbilityData> ability = new PFAbilityData(attacker.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = attacker->CreateAbilityInstance(ability, Target(hero.GetPtr()));
	CObj<PFDispatch> dispatch = cast ? B11FindDispatch(f.world, cast) : CObj<PFDispatch>();
	check(cast && dispatch && attacker->GetMana() == 193, "real paid ability creates an aggressive linear dispatch");
	if (!dispatch) return;
	attacker->Step(1.1f);
	check(dispatch->IsObjectDead() && attacker->GetLastHeroAttackData()->pVictim == hero.GetPtr(),
		"real dispatch arrival and OnDie aggression hook record the true hero in dedicated history");
	check(owner->GetTargetWeight(attacker.GetPtr(), *p) == 23, "weighting consumes the real dispatch-produced hero history");
	owner->AddToPriorityTargets(attacker.GetPtr());
	check(owner->GetTargetWeight(attacker.GetPtr(), *p) == 28, "manual priority adds independently to dispatch-produced hero help");
	cast->Cancel();
	owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDATTACK);
	attacker->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDATTACK);
	const float ageBeforeWorld = attacker->GetLastHeroAttackData()->timePassed;
	f.world->Step(0, 0); f.world->Step(2, 2);
	check(!owner->IsPriorityTarget(attacker.GetPtr()) && attacker->GetLastHeroAttackData()->timePassed == ageBeforeWorld + 2,
		"isolated real-world stepping ages stationary history and priority exactly once");
	check(owner->GetTargetWeight(attacker.GetPtr(), *p) == 3, "exact history and priority expiry restores the ordinary type table");
}

/** Original history maintenance drops attacks when the victim loses vision or chase eligibility. */
static void WeightedHistoryRetentionB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr()); p->chaseRange = 5;
	auto attacker = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto victim = WeightedUnitCreateB12(f.world, p);
	auto hero = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE); hero->OverrideTargetingParams(p);
	victim->Place(CVec3(9, 5, 0)); hero->TeleportTo(CVec2(9, 5), true, false);
	{
		B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
		attacker->SetLastAttackData(victim.GetPtr(), true, NDb::FACTION_FREEZE);
		attacker->SetLastAttackDataEx(hero.GetPtr(), true, true, NDb::FACTION_FREEZE);
		attacker->StepLastAttackData(.5f);
		check(attacker->GetLastAttackData()->timePassed == .5f && attacker->GetLastHeroAttackData()->timePassed == .5f,
			"both histories advance while their victims can see and chase the attacker");
		for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
			attacker->StepLastAttackData(dt);
		check(attacker->GetLastAttackData()->timePassed == .5f && attacker->GetLastHeroAttackData()->timePassed == .5f,
			"invalid or nonforward simulation time neither extends nor clears valid attack memories");
		attacker->Place(CVec3(20, 5, 0)); attacker->StepLastAttackData(.25f);
		check(!IsValid(attacker->GetLastAttackData()->pVictim) && !IsValid(attacker->GetLastHeroAttackData()->pVictim),
			"both original history channels clear when the victim cannot chase the attacker");
		p->chaseRange = 0;
		attacker->SetLastAttackData(victim.GetPtr(), true, NDb::FACTION_FREEZE);
		attacker->SetLastAttackDataEx(hero.GetPtr(), true, true, NDb::FACTION_FREEZE);
		attacker->StepLastAttackData(.25f);
		check(IsValid(attacker->GetLastAttackData()->pVictim) && IsValid(attacker->GetLastHeroAttackData()->pVictim),
			"zero chase range retains the original unlimited-chase meaning");
	}
	f.world->GetFogOfWar()->StepVisibility(0);
	attacker->StepLastAttackData(.25f);
	check(!IsValid(attacker->GetLastAttackData()->pVictim) && !IsValid(attacker->GetLastHeroAttackData()->pVictim),
		"both histories clear when victim faction loses actual fog visibility of the attacker");
}

/** Alpha preference applies only to candidates admitted by native range/visibility and valid scoring. */
static void WeightedAlphaSelectionB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p, NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP, true);
	auto master = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto alpha = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN, NDb::UNITTYPE_SUMMON);
	alpha->Place(CVec3(12, 5, 0)); master->Place(CVec3(8, 5, 0));
	alpha->SetMaster(master.GetPtr()); master->SetAlphaSummon(alpha.GetPtr());
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_CREEP] = 3;
	B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
	check(owner->FindTarget(20, false) == alpha.GetPtr(), "visible eligible alpha takes precedence when its master wins original scoring");
	alpha->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	check(owner->FindTarget(20, false) == master.GetPtr(), "alpha preference cannot replace a master with a forbidden candidate");
	alpha->RemoveFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	alpha->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(0);
	p->customWeight.sString = "1/rStrength";
	check(owner->FindTarget(20, false) == master.GetPtr(), "alpha preference cannot revive a candidate rejected by native numeric guards");
	p->customWeight.sString = "0"; alpha->Place(CVec3(50, 5, 0));
	check(owner->FindTarget(20, false) == master.GetPtr(), "out-of-range or unseen alpha is not added by master substitution");
}

/** Isolated session and world fixtures keep target enumeration independent of older tests. */
static void WeightedTargetingB12(Checks& check)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot; logic->aiLogic = new NDb::AILogicParameters;
	NDb::UnitsLogicParameters* units = new NDb::UnitsLogicParameters;
	for (int i = 0; i < units->unitParameters.size(); ++i)
	{
		NDb::UnitLogicParameters* unit = new NDb::UnitLogicParameters; unit->screamRange = 12;
		units->unitParameters[i] = unit;
	}
	logic->unitLogicParameters = units;
	NDb::SessionRoot* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	WeightedTermsB12(check);
	WeightedHistoryB12(check);
	WeightedMastersB12(check);
	WeightedAlliesB12(check);
	WeightedCustomB12(check);
	WeightedSelectionB12(check);
	WeightedSelectorContractB12(check);
	WeightedGuardsB12(check);
	WeightedUnconfiguredB12(check);
	WeightedDispatchB12(check);
	WeightedHistoryRetentionB12(check);
	WeightedAlphaSelectionB12(check);
	NDb::SessionRoot::InitRoot(saved);
}
