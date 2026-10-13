/** Invalid score arithmetic must not become the valid authored score -1 in MicroAI. */
static void TargetWeightErrorsB13(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
	const NDb::UnitTargetingParameters saved = f.Weights();
	for (int malformed = 0; malformed < 9; ++malformed)
	{
		f.Weights() = saved;
		auto& p = f.Weights();
		const float infinity = std::numeric_limits<float>::infinity();
		const float nan = std::numeric_limits<float>::quiet_NaN();
		if (malformed == 0) p.targetingRange = 0;
		if (malformed == 1) p.rangeWeight = nan;
		if (malformed == 2) p.weightMaximumRangePersent = infinity;
		if (malformed == 3) p.aggroRange = nan;
		if (malformed == 4) p.healthWeight = infinity;
		if (malformed == 5) p.unitTypeTableWeight = infinity;
		if (malformed == 6) p.unitTargetWeight[NDb::UNITTYPE_BUILDING] = nan;
		if (malformed == 7) p.unitTargetWeight.clear();
		if (malformed == 8)
		{
			p.unitTypeTableWeight = std::numeric_limits<float>::max();
			p.unitTargetWeight[NDb::UNITTYPE_BUILDING] = 2;
		}
		check(f.owner->GetTargetWeight(f.enemy.GetPtr(), p) == -1, "legacy selector API keeps its failure sentinel");
		for (float threshold : {-100.f, -1.f, 0.f}) for (int count : {0, 1})
		{
			f.AI().minTargetWeight = threshold; f.AI().minTargetCount = count;
			Target result(f.enemy);
			check(!f.micro->GetTarget(result) && result.GetUnit() == f.enemy,
				"invalid selected weight rejects MicroAI without counting a sentinel or satisfying exact-zero policy");
		}
	}
	f.Weights() = saved;
	f.AI().minTargetCount = 1;
	for (float score : {-100.f, -1.f, -.5f, 0.f, 3.f})
	{
		f.Weights().unitTargetWeight[NDb::UNITTYPE_BUILDING] = score;
		f.AI().minTargetWeight = score - .25f;
		check(f.Query(), "valid signed scores, including minus one, still qualify after rejected arithmetic");
	}
	f.Weights().unitTypeTableWeight = 0;
	f.Weights().unitTargetWeight.clear();
	f.AI().minTargetWeight = -1;
	check(f.Query(), "an unused malformed table is not evaluated when its coefficient is zero");
	f.Weights().unitTypeTableWeight = 1;
	f.AI().unitFilter.sString = "false"; f.AI().minTargetCount = 0;
	check(f.Query(), "filtered-out candidates do not evaluate unused invalid weights");
	f.AI().unitFilter.sString = "true"; f.enemy->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	check(f.Query(), "ineligible candidates retain the original exact-zero policy without scoring");
	f.enemy->RemoveFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	f.List().targets.clear();
	check(f.Query(), "empty candidate sets do not evaluate unrelated invalid weight data");
	check(f.owner->GetMana() == 200 && f.ability->GetActiveInstancesCount() == 0,
		"failed and successful MicroAI queries do not pay or cast");
}

/** Exercise selected custom formulas through an actual owned base-attack context. */
static void TargetWeightCustomErrorsB13(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	B8BehaviourReveal reveal(f.fixture.world, NDb::FACTION_FREEZE);
	f.owner = WeightedUnitCreateB12(f.fixture.world, f.targeting, NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP, true);
	f.behaviour = new MicroBehaviourB12; f.owner->AddBehaviourOnTop(f.behaviour);
	f.ability = new PFAbilityData(f.owner.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL);
	PFMicroAICreateParams cp(f.microDb.GetPtr(), f.ability); f.micro = f.microDb->Create(cp);
	f.Weights().unitTypeTableWeight = 0;
	check(IsValid(f.owner->GetAttackAbilityData()), "custom MicroAI fixture has a real base-attack formula context");
	for (const char* expression : {"1/0", "1e39", "unknown(rStrength)", "cMissing", ""})
	{
		f.Weights().customWeight.sString = expression;
		f.Weights().customWeight.compiledString = *expression ? "" : "compiled-only";
		for (float threshold : {-100.f, -1.f, 0.f}) for (int count : {0, 1})
		{
			f.AI().minTargetWeight = threshold; f.AI().minTargetCount = count;
			Target result(f.enemy);
			check(!f.ability->FindMicroAITarget(result) && result.GetUnit() == f.enemy,
				"actual ability MicroAI rejects invalid or compiled-only custom scores at any threshold/count");
		}
	}
	f.Weights().customWeight.compiledString = "";
	f.AI().minTargetCount = 1; f.AI().minTargetWeight = -2;
	for (const char* expression : {"-1", "sStrength-rStrength-21", "true ? -1 : cMissing"})
	{
		f.Weights().customWeight.sString = expression;
		Target result;
		check(f.ability->FindMicroAITarget(result) && result.GetUnit() == f.owner,
			"valid custom minus-one scores remain eligible through the actual ability entry point");
	}
	check(f.owner->GetMana() == 200 && f.ability->GetActiveInstancesCount() == 0,
		"checked custom target selection does not pay or create a cast");
	auto second = Unit(f.fixture.world, NDb::FACTION_BURN, 40);
	f.Weights().customWeight.sString = "rStrength < 30 ? 3 : 1/0";
	for (bool reverse : {false, true})
	{
		f.List().targets = reverse ? std::vector<Target>{Target(second), Target(f.enemy)}
			: std::vector<Target>{Target(f.enemy), Target(second)};
		for (int count : {0, 1})
		{
			f.AI().minTargetCount = count;
			check(!f.Query(), "one invalid selected weight rejects the whole query regardless of candidate order");
		}
	}
	f.List().targets = {Target(f.enemy)}; f.AI().minTargetCount = 1;
	check(f.Query(), "removing the invalid candidate recovers without a sticky cached error");
}

/** Checked output preserves the original finite sentinel branches and never publishes partial arithmetic. */
static void TargetWeightResultB13(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	auto owner = WeightedUnitCreateB12(f.world, p), enemy = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	p->unitTypeTableWeight = 1;
	for (float value : {-100.f, -1.f, 0.f, 3.f})
	{
		float result = 12345;
		p->unitTargetWeight[NDb::UNITTYPE_CREEP] = value;
		check(owner->TryGetTargetWeight(enemy.GetPtr(), *p, result) && result == value,
			"checked native score returns every valid signed value separately from success");
	}
	float result = 12345;
	p->unitTypeTableWeight = std::numeric_limits<float>::infinity();
	check(!owner->TryGetTargetWeight(enemy.GetPtr(), *p, result) && result == 12345,
		"failed checked arithmetic leaves the caller output unchanged");
	check(!owner->TryGetTargetWeight(CPtr<PFBaseUnit>(), *p, result) && result == 12345,
		"missing target fails without publishing a partial score");
	p->unitTypeTableWeight = 1;
	CObj<WeightedUnitB12> sibling = new WeightedUnitB12(f.world, owner->DbUnitDesc(), NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP);
	sibling->AssignTarget(enemy.GetPtr(), false); p->ignoreSiblingTarget = true;
	check(owner->TryGetTargetWeight(enemy.GetPtr(), *p, result) && result == -1,
		"original sibling-policy minus one is a valid result, not an arithmetic error");
	sibling->DropTarget(); p->ignoreSiblingTarget = false;
	owner->SetMaster(sibling.GetPtr()); sibling->Place(CVec3(100, 5, 0));
	check(owner->TryGetTargetWeight(enemy.GetPtr(), *p, result) && result == 0,
		"original out-of-leash zero is also a successful score");
	owner->SetMaster(nullptr);
}
