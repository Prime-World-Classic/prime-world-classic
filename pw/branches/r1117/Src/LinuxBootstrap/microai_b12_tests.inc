/** Supply policy inputs only; MicroAI gates, scoring and ability state remain production code. */
struct MicroBehaviourB12 : PFBaseBehaviour
{
	bool idle = true, select = true;
	mutable bool sawVisibilityRequirement = false;
	explicit MicroBehaviourB12(PFBaseMovingUnit* owner = nullptr) : PFBaseBehaviour(owner)
	{
		if (!owner) { SetFaction(NDb::FACTION_FREEZE); SetUnitType(NDb::UNITTYPE_CREEP); }
	}
	bool IsInDefaultState() const override { return idle; }
	bool CanSelectTarget(const PFBaseUnit*, bool mustSee) const override
	{
		sawVisibilityRequirement = mustSee; return select;
	}
};

/** Mock just candidate enumeration, including foreign/nonunit inputs for the consumer guard. */
struct MicroListDbB12 : NDb::MultipleTargetSelector
{
	std::vector<Target> targets;
	NWorld::PFTargetSelector* Create(PFWorld* world) const override;
};
struct MicroListB12 : PFMultipleTargetSelector
{
	const MicroListDbB12& db;
	MicroListB12(const MicroListDbB12& db, PFWorld* world) : PFMultipleTargetSelector(db, world), db(db) {}
	void EnumerateTargets(ITargetAction& action, const RequestParams&) override
	{
		for (const Target& target : db.targets) action(target);
	}
};
NWorld::PFTargetSelector* MicroListDbB12::Create(PFWorld* world) const { return new MicroListB12(*this, world); }

/** An isolated loaded world plus real unit/ability/MicroAI objects from authored DB factories. */
struct MicroFixtureB12
{
	B8BehaviourForeignWorld fixture;
	NDb::Ptr<NDb::UnitTargetingParameters> targeting;
	NDb::Ptr<NDb::MultipleTargetSelectorMicroAI> microDb;
	NDb::Ptr<MicroListDbB12> listDb;
	NDb::Ptr<NDb::Ability> abilityDb;
	CObj<PFBaseUnit> owner, enemy;
	CObj<MicroBehaviourB12> behaviour;
	CObj<PFAbilityData> ability;
	CObj<PFMicroAI> micro;
	explicit MicroFixtureB12(Checks& check) : fixture(check)
	{
		if (!fixture.world) return;
		NDb::UnitTargetingParameters* p = new NDb::UnitTargetingParameters;
		p->unitTypeTableWeight = 1; p->healthWeight = 0; p->targetingRange = 20;
		targeting = p;
		owner = Unit(fixture.world, NDb::FACTION_FREEZE, 40);
		enemy = Unit(fixture.world, NDb::FACTION_BURN, 20);
		owner->OverrideTargetingParams(p);
		behaviour = new MicroBehaviourB12; owner->AddBehaviourOnTop(behaviour);
		MicroListDbB12* list = new MicroListDbB12; list->targets = {Target(enemy)}; listDb = list;
		NDb::MultipleTargetSelectorMicroAI* m = new NDb::MultipleTargetSelectorMicroAI;
		m->condition.sString = "true"; m->unitFilter.sString = "true"; m->targetSelector = list;
		microDb = m;
		NDb::Ability* a = new NDb::Ability; a->type = NDb::ABILITYTYPE_ACTIVE;
		a->targetType = NDb::SPELLTARGET_SELF; a->microAI = m; a->manaCost.sString = "7";
		abilityDb = a;
		ability = new PFAbilityData(owner.GetPtr(), a, NDb::ABILITYTYPEID_SPECIAL);
		PFMicroAICreateParams cp(microDb.GetPtr(), ability);
		micro = m->Create(cp);
	}
	NDb::UnitTargetingParameters& Weights() { return *const_cast<NDb::UnitTargetingParameters*>(targeting.GetPtr()); }
	NDb::MultipleTargetSelectorMicroAI& AI() { return *const_cast<NDb::MultipleTargetSelectorMicroAI*>(microDb.GetPtr()); }
	MicroListDbB12& List() { return *const_cast<MicroListDbB12*>(listDb.GetPtr()); }
	bool Query() { Target result; return micro && micro->GetTarget(result) && result.IsUnit() && result.GetUnit() == owner; }
};

/** Windows compares every finite score strictly against the authored threshold, not just positive ones. */
static void MicroThresholdsB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
	float& score = f.Weights().unitTargetWeight[NDb::UNITTYPE_BUILDING];
	for (float value : {-100.0f, -1.0f, -.5f, 0.0f, 3.0f})
	{
		score = value;
		check(f.owner->GetTargetWeight(f.enemy.GetPtr(), f.Weights()) == value, "MicroAI fixture uses the verified real weighted scorer");
		f.AI().minTargetWeight = value;
		check(!f.Query(), "equal weight never exceeds the authored MicroAI threshold");
		f.AI().minTargetWeight = value - .25f;
		check(f.Query(), "negative and positive scores above the authored threshold count without an invented minus-one floor");
		f.AI().minTargetWeight = value + .25f;
		check(!f.Query(), "weight below an authored threshold does not count even when the threshold is nonpositive");
	}
	score = 3; f.AI().minTargetWeight = 0; f.AI().minTargetCount = 2;
	check(!f.Query(), "one qualifying target cannot satisfy a count of two");
	auto second = Unit(f.fixture.world, NDb::FACTION_BURN, 10);
	f.List().targets.push_back(Target(second));
	check(f.Query(), "two qualifying real targets satisfy the original count boundary");
	f.List().targets = {Target(f.enemy), Target(f.enemy)};
	check(f.Query(), "MicroAI preserves selector enumeration counts rather than inventing a distinct-unit policy");
	f.AI().minTargetCount = 0;
	check(!f.Query(), "zero count means exactly no qualifying targets, not any count");
	f.AI().minTargetWeight = 3;
	check(f.Query(), "strict threshold exclusion can satisfy the original exact-zero count policy");
	f.List().targets.clear();
	check(f.Query(), "empty enumeration satisfies exact-zero count");
	f.AI().minTargetCount = 1;
	check(!f.Query(), "empty enumeration cannot satisfy a positive count");
	f.AI().minTargetCount = -1;
	check(!f.Query(), "malformed negative target count is rejected rather than auto-casting");
	f.AI().minTargetCount = 0;
	for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		f.AI().minTargetWeight = value;
		check(!f.Query(), "nonfinite threshold fails even for an empty zero-count enumeration");
	}
}

/** Selection must still obey actual fog, flags, behavior and exact-world context. */
static void MicroEligibilityB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	f.Weights().unitTargetWeight[NDb::UNITTYPE_BUILDING] = 3;
	{
		B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
		check(f.Query() && f.behaviour->sawVisibilityRequirement, "multiple consumer invokes real CanSelectTarget with mustSee true");
		for (auto flag : {NDb::UNITFLAG_FORBIDAUTOTARGETME, NDb::UNITFLAG_FORBIDTAKEDAMAGE, NDb::UNITFLAG_FLYING})
		{
			f.enemy->AddFlag(flag); f.AI().minTargetWeight = -100;
			check(!f.Query(), "negative thresholds never bypass auto-target, vulnerability or flight eligibility");
			f.enemy->RemoveFlag(flag);
		}
		f.behaviour->select = false;
		check(!f.Query(), "actual owner behavior can reject the enumerated target");
		f.behaviour->select = true;
		f.List().targets = {Target(f.owner)};
		check(!f.Query(), "owner cannot count as its own automatic attack target");
		f.List().targets = {Target(CVec3(5, 5, 0))};
		check(!f.Query(), "position targets do not enter the unit counter");
		B8BehaviourForeignWorld foreignWorld(check);
		auto foreign = Unit(foreignWorld.world, NDb::FACTION_BURN, 20);
		B8BehaviourReveal foreignReveal(foreignWorld.world, NDb::FACTION_FREEZE);
		f.List().targets = {Target(foreign)};
		check(!f.Query(), "foreign-world units are rejected before a negative threshold can admit a sentinel score");
		f.List().targets = {Target(f.enemy)};
		f.enemy->ChangeHealth(-f.enemy->GetHealth());
		check(!f.Query(), "dead units do not count as selectable targets");
	}
	f.enemy = Unit(f.fixture.world, NDb::FACTION_BURN, 20); f.List().targets = {Target(f.enemy)};
	f.fixture.world->GetFogOfWar()->StepVisibility(0);
	check(!f.Query(), "losing actual owner-faction fog visibility removes an otherwise eligible target");
}

/** Both authored boolean sites use the same checked engine formula context and finite truth conversion. */
static void MicroFormulasB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
	f.Weights().unitTargetWeight[NDb::UNITTYPE_BUILDING] = 3;
	f.AI().condition.sString = "sStrength == rStrength ? sStrength > 30 : false";
	check(f.Query(), "activation condition receives owner as both sender and receiver");
	f.owner->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(25);
	check(!f.Query(), "activation condition rereads live owner stats");
	f.owner->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(40);
	f.AI().unitFilter.sString = "rStrength < sStrength";
	check(f.Query(), "unit filter receives actual candidate rather than the owner as receiver");
	f.enemy->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(50);
	check(!f.Query(), "live candidate changes affect the next filter query");
	f.enemy->GetStat(NDb::STAT_STRENGTH)->SetBaseValue(20);
	NDb::UnitConstant* constant = new NDb::UnitConstant; constant->name = "MicroLimit"; constant->var.sString = "sStrength-10";
	NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer; constants->vars.push_back(constant);
	const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->constants = constants;
	// Ability construction builds the original constant-name map; authored values remain live.
	f.ability = new PFAbilityData(f.owner.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL);
	PFMicroAICreateParams cp(f.microDb.GetPtr(), f.ability);
	f.micro = f.microDb->Create(cp);
	f.AI().unitFilter.sString = "rStrength < cMicroLimit";
	check(f.Query(), "unit filter resolves constants through the actual ability context");
	constant->var.sString = "10";
	check(!f.Query(), "ability constant changes are not cached across MicroAI queries");
	f.AI().condition.sString = "true";
	for (const char* expression : {".25", "-2.5", "true", "1e-3", "True", "TRUE"})
	{
		f.AI().unitFilter.sString = expression;
		check(f.Query(), "finite nonzero boolean results use C++ truth semantics without integer truncation");
	}
	f.AI().unitFilter.sString = "0.0"; f.AI().minTargetCount = 0;
	check(f.Query(), "valid false filter can satisfy an authored zero-count condition");
	f.AI().unitFilter.sString = "true && false";
	check(f.Query(), "supported logical false remains a valid zero-count filter");
	f.AI().unitFilter.sString = "false && cMissing";
	check(f.Query(), "short-circuited missing constants do not poison valid zero-count filtering");
	f.AI().condition.sString = "sLife > 0 && true";
	check(f.Query(), "common checked logical syntax reaches live MicroAI activation");
	f.AI().condition.sString = "true";
	for (const char* expression : {"unknown(1)", "1/0", "1e39", "rMissing", "true && unknown(1)"})
	{
		f.AI().unitFilter.sString = expression;
		check(!f.Query(), "unsupported or nonfinite selected filter cannot masquerade as zero eligible targets");
		f.AI().unitFilter.sString = "true"; f.AI().condition.sString = expression;
		check(!f.Query(), "invalid activation condition fails closed");
		f.AI().condition.sString = "true";
	}
	f.AI().unitFilter.sString = ""; f.AI().unitFilter.compiledString = "bytecode";
	check(!f.Query(), "compiled-only selected unit filter is not a valid false result");
	f.AI().unitFilter.compiledString = "";
	check(f.Query(), "absent filter keeps the original false default and zero-count meaning");
	f.AI().unitFilter.sString = "true"; f.AI().minTargetCount = 1;
	f.AI().condition.sString = "";
	check(!f.Query(), "absent activation condition retains the original false default");
	f.AI().condition.compiledString = "bytecode";
	check(!f.Query(), "compiled-only activation condition remains unsupported");
	f.AI().condition.compiledString = ""; f.AI().condition.sString = "true ? 1 : sMissing";
	check(f.Query(), "unselected unknown property retains the existing evaluator's lazy conditional semantics");
	const std::string padding(LinuxBootstrap::NumericFormulaMaxLength + 1, ' ');
	f.AI().condition.sString = (padding + "TRUE").c_str();
	check(!f.Query(), "legacy boolean spellings cannot bypass the native expression-length limit");
	f.AI().condition.sString = "true"; f.AI().unitFilter.sString = (padding + "FALSE").c_str(); f.AI().minTargetCount = 0;
	check(!f.Query(), "oversized selected false filter is an error, not a zero-count success");
	check(f.owner->GetMana() == 200 && f.owner->GetHealth() == 1000 && f.ability->GetActiveInstancesCount() == 0,
		"MicroAI queries neither pay resources nor create ability instances");
}

/** Exercise the original CheckConditions directly without replacing its implementation. */
struct MicroBasicB12 : PFBasicMicroAI
{
	explicit MicroBasicB12(const CreateParams& cp) : PFBasicMicroAI(cp) {}
	bool Conditions() const { return CheckConditions(); }
	void ClearAbility() { pAbility = nullptr; }
};

static void MicroConditionsB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	PFMicroAICreateParams cp(f.microDb.GetPtr(), f.ability);
	CObj<MicroBasicB12> basic = new MicroBasicB12(cp);
	check(basic->Conditions(), "ready active ability with behavior and true condition is usable");
	f.behaviour->idle = false; f.AI().castWhenIdle = true;
	check(!basic->Conditions(), "authored castWhenIdle rejects a nondefault behavior state");
	f.AI().castWhenIdle = false;
	check(basic->Conditions(), "disabled castWhenIdle allows an otherwise usable busy unit");
	f.owner->RemoveBehaviour(f.behaviour);
	check(!basic->Conditions(), "Windows requires an owner behavior even when castWhenIdle is false");
	f.owner->AddBehaviourOnTop(f.behaviour);
	f.behaviour->idle = true; f.AI().castWhenIdle = true;
	check(basic->Conditions(), "returning to default state satisfies the authored idle gate");
	for (auto type : {NDb::ABILITYTYPE_PASSIVE, NDb::ABILITYTYPE_AUTOCASTABLE, NDb::ABILITYTYPE_SIMPLE})
	{
		const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->type = type;
		check(!basic->Conditions(), "original passive/autocastable/simple types remain excluded");
	}
	const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->type = NDb::ABILITYTYPE_ACTIVE;
	f.ability->AddForbid(); check(!basic->Conditions(), "existing ability forbid gate remains active"); f.ability->RemoveForbid();
	f.ability->RestartCooldown(2); check(!basic->Conditions(), "existing ability cooldown gate remains active"); f.ability->DropCooldown(true);
	f.owner->ChangeMana(-200); check(!basic->Conditions(), "existing resource eligibility gate remains active"); f.owner->ChangeMana(200);
	check(basic->Conditions(), "live readiness restoration permits the next query");
	f.owner->ChangeHealth(-f.owner->GetHealth());
	check(!basic->Conditions(), "dead owner cannot pass native MicroAI context checks");
	basic->ClearAbility(); check(!basic->Conditions(), "missing ability fails without dereferencing its DB");
}

/** Seed the original ability-container initializer, which native base construction currently omits. */
struct MicroHeroB12 : B8BehaviourHero
{
	MicroHeroB12(PFWorld* world, const PFBaseHero::SpawnInfo& spawn)
		: B8BehaviourHero(world, spawn, NDb::FACTION_FREEZE) { CreateAbilities(this, *spawn.pHero); }
};

/** Actual creature slot APIs supply availability, in-progress and selective forbid policy. */
static void MicroCreatureSlotsB12(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto seed = B8BehaviourHeroUnit(f.world, NDb::FACTION_FREEZE);
	PFBaseHero::SpawnInfo spawn; spawn.pHero = dynamic_cast<const NDb::Hero*>(seed->DbUnitDesc());
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	CObj<MicroHeroB12> hero = new MicroHeroB12(f.world, spawn);
	check(hero->GetGlobalCooldown() == 2, "slot fixture initializes real ability-container global cooldown from authored AI data");
	NDb::BasicMicroAI* m = new NDb::BasicMicroAI; m->condition.sString = "true";
	NDb::Ptr<NDb::BasicMicroAI> microDb = m;
	NDb::Ability* a = new NDb::Ability; a->type = NDb::ABILITYTYPE_ACTIVE; a->targetType = NDb::SPELLTARGET_SELF;
	NDb::Ptr<NDb::Ability> db = a;
	CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1);
	PFMicroAICreateParams cp(microDb.GetPtr(), ability);
	CObj<MicroBasicB12> micro = new MicroBasicB12(cp);
	hero->SetAbility(1, nullptr);
	check(!micro->Conditions(), "creature missing the corresponding ability slot cannot pass Windows usability policy");
	hero->SetAbility(1, ability);
	check(micro->Conditions(), "actual bound and available creature slot is admitted");
	hero->SetAbilityInProgress(1, true);
	check(!micro->Conditions(), "in-progress creature slot blocks another MicroAI activation");
	hero->SetAbilityInProgress(1, false); hero->AddFlag(NDb::UNITFLAG_FORBID_ABILITY1);
	check(!micro->Conditions(), "selective creature ability flag is checked through CanUseAbility");
	hero->RemoveFlag(NDb::UNITFLAG_FORBID_ABILITY1);
	check(micro->Conditions(), "removing the slot restriction restores eligibility");
	hero->StartGlobalCooldown();
	check(!micro->Conditions(), "real global creature cooldown blocks an individually ready slot");
	hero->Step(2.1f);
	check(micro->Conditions(), "isolated creature stepping restores eligibility after global cooldown");
	CObj<PFAbilityData> special = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	PFMicroAICreateParams specialCp(microDb.GetPtr(), special); CObj<MicroBasicB12> specialMicro = new MicroBasicB12(specialCp);
	hero->AddFlag(NDb::UNITFLAG_FORBID_ABILITY1);
	check(specialMicro->Conditions(), "special abilities are not incorrectly mapped to ordinary creature slots");
	hero->RemoveFlag(NDb::UNITFLAG_FORBID_ABILITY1);
	CObj<PFAbilityData> zero = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY0);
	PFMicroAICreateParams zeroCp(microDb.GetPtr(), zero); CObj<MicroBasicB12> zeroMicro = new MicroBasicB12(zeroCp);
	hero->SetAbility(0, nullptr);
	check(zeroMicro->Conditions(), "original slot check starts at ability one, not the stats slot zero");
}

/** A caller predicate observes actual candidate/ability context and remains an additional native gate. */
struct MicroCallerConditionB12 : ITargetCondition
{
	bool allow = false;
	mutable const PFAbilityData* observedAbility = nullptr;
	mutable CPtr<PFBaseUnit> observedUnit;
	bool operator()(const Target& target, const PFAbilityData* const ability) const override
	{
		observedAbility = ability;
		observedUnit = target.IsUnit() ? target.GetUnit() : CPtr<PFBaseUnit>();
		return allow;
	}
};

static void MicroCallerAndFallbackB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
	f.Weights().unitTargetWeight[NDb::UNITTYPE_BUILDING] = 3;
	MicroCallerConditionB12 condition;
	Target result(f.enemy);
	check(!f.micro->GetTarget(result, condition) && result.GetUnit() == f.enemy,
		"rejected multiple query preserves the caller's previous output target");
	check(condition.observedUnit == f.enemy && condition.observedAbility == f.ability,
		"consumer retains the caller's actual candidate and ability context");
	condition.allow = true;
	check(f.micro->GetTarget(result, condition) && result.GetUnit() == f.owner,
		"successful multiple query returns owner, not the counted candidate");
	f.AI().targetSelector = nullptr;
	PFMicroAICreateParams cp(f.microDb.GetPtr(), f.ability); f.micro = f.microDb->Create(cp);
	condition.allow = false;
	check(!f.micro->GetTarget(result, condition), "missing-selector basic fallback retains the existing native caller condition");
	condition.allow = true;
	check(f.micro->GetTarget(result, condition) && result.GetUnit() == f.owner,
		"missing-selector fallback still supports the original basic self-target route");
	f.AI().castWhenIdle = true; f.behaviour->idle = false;
	check(!f.micro->GetTarget(result, condition), "restored idle gate applies to fallback selection too");
}

/** An actual area selector and normal/temporary ability entry points exercise the composed route. */
static void MicroAreaB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
	f.Weights().unitTargetWeight[NDb::UNITTYPE_BUILDING] = 4;
	NDb::AreaTargetSelector* area = new NDb::AreaTargetSelector;
	area->range.sString = "20"; area->targetFilter = NDb::ESpellTarget(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_ALL);
	f.AI().targetSelector = area;
	CObj<PFAbilityData> ability = new PFAbilityData(f.owner.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL);
	Target target;
	check(ability->FindMicroAITarget(target) && target.GetUnit() == f.owner,
		"actual authored area selector feeds weighted multiple MicroAI through the ability entry point");
	f.AI().minTargetWeight = 4;
	check(!ability->FindMicroAITarget(target), "actual area route applies strict threshold equality");
	f.AI().minTargetWeight = 0; f.enemy->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	check(!ability->FindMicroAITarget(target), "area selector output still passes consumer-specific CanSelectTarget");
	f.enemy->RemoveFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	check(ability->FindMicroAITargetTemp(target) && target.GetUnit() == f.owner,
		"temporary MicroAI with no own world still uses the real ability owner's world");
	check(f.owner->GetMana() == 200 && ability->GetActiveInstancesCount() == 0,
		"composed target queries remain side-effect-free for payment and active casts");
	NDb::TargetSelectorMicroAI* single = new NDb::TargetSelectorMicroAI;
	NDb::FirstTargetSelector* first = new NDb::FirstTargetSelector; first->targetSelector = f.listDb;
	single->targetSelector = first; single->condition.sString = "sStrength > 30"; single->castWhenIdle = true;
	const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->microAI = single;
	CObj<PFAbilityData> singleAbility = new PFAbilityData(f.owner.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL);
	check(singleAbility->FindMicroAITarget(target) && target.GetUnit() == f.enemy,
		"shared checked activation also reaches the original single-selector MicroAI route");
	f.behaviour->idle = false;
	check(!singleAbility->FindMicroAITarget(target), "single-selector MicroAI shares the restored idle gate");
	f.behaviour->idle = true; single->condition.sString = "1/0";
	check(!singleAbility->FindMicroAITarget(target), "single-selector MicroAI also rejects invalid authored activation");
}

/** Every group owns its loaded world; session context and shared fixtures are never stepped. */
static void MicroAIB12(Checks& check)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot;
	NDb::AILogicParameters* aiLogic = new NDb::AILogicParameters; aiLogic->globalCooldownTime = 2; logic->aiLogic = aiLogic;
	logic->unitLogicParameters = new NDb::UnitsLogicParameters;
	NDb::SessionRoot* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	MicroThresholdsB12(check);
	MicroEligibilityB12(check);
	MicroFormulasB12(check);
	MicroConditionsB12(check);
	MicroCreatureSlotsB12(check);
	MicroCallerAndFallbackB12(check);
	MicroAreaB12(check);
	NDb::SessionRoot::InitRoot(saved);
}
