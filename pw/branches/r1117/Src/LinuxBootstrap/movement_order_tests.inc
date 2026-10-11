/** Authored targeting distances are independent of visibility and live overrides. */
static void AuthoredTargetRanges(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit; Configure(*raw);
	NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
	params->chaseRange = 31; params->targetingRange = 17; params->aggroRange = 23;
	raw->targetingParams = params;
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeUnit> unit = new ProbeUnit(fixture.world, db);
	checks.Check(unit->GetChaseRange() == 31, "authored chase differs from visibility");
	checks.Check(unit->GetTargetingRange() == 17, "authored targeting differs from visibility");
	checks.Check(unit->GetAggroRange() == 23, "authored aggro differs from visibility");
	NDb::UnitTargetingParameters* replacement = new NDb::UnitTargetingParameters;
	replacement->chaseRange = 7; replacement->targetingRange = 9; replacement->aggroRange = 11;
	unit->OverrideTargetingParams(replacement);
	checks.Check(unit->GetChaseRange() == 7 && unit->GetTargetingRange() == 9 && unit->GetAggroRange() == 11,
		"live targeting override affects all distances");
	for (float invalid : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		replacement->chaseRange = replacement->targetingRange = replacement->aggroRange = invalid;
		checks.Check(std::isnan(unit->GetChaseRange()) && std::isnan(unit->GetTargetingRange()) && std::isnan(unit->GetAggroRange()),
			"malformed authored distances cannot expand queries");
	}
	replacement->chaseRange = replacement->targetingRange = replacement->aggroRange = 0;
	checks.Check(unit->GetChaseRange() == 0 && unit->GetTargetingRange() == 0 && unit->GetAggroRange() == 0,
		"authored zero is preserved, not replaced by vision");
	unit->OverrideTargetingParams(nullptr);
	checks.Check(unit->GetChaseRange() == 31, "clearing override restores unit DB parameters");
	raw->targetingParams = nullptr;
	CObj<ProbeUnit> absent = new ProbeUnit(fixture.world, db);
	checks.Check(absent->GetChaseRange() == 12 && absent->GetTargetingRange() == 12 && absent->GetAggroRange() == 12,
		"absent bootstrap targeting retains bounded visibility fallback");
}

/** Missing unit-specific targeting resolves through the real AI per-type table. */
static void DefaultTargetRanges(Checks& checks)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	NDb::SessionRoot* root = new NDb::SessionRoot;
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot;
	NDb::UnitsLogicParameters* units = new NDb::UnitsLogicParameters;
	units->unitParameters.resize(NDb::KnownEnum<NDb::EUnitType>::SizeOf());
	NDb::UnitLogicParameters* entry = new NDb::UnitLogicParameters;
	NDb::UnitTargetingParameters* ranges = new NDb::UnitTargetingParameters;
	ranges->chaseRange = 30; ranges->targetingRange = 18; ranges->aggroRange = 24;
	entry->targetingPars = ranges; entry->screamRange = 19;
	units->unitParameters[NDb::UNITTYPE_BUILDING] = entry;
	logic->unitLogicParameters = units; logic->aiLogic = new NDb::AILogicParameters;
	root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	Fixture fixture(checks);
	if (fixture.loaded)
	{
		NDb::Unit* raw = new NDb::Unit; Configure(*raw);
		NDb::Ptr<NDb::Unit> db = raw;
		CObj<ProbeUnit> unit = new ProbeUnit(fixture.world, db);
		checks.Check(unit->GetTargetingParamsPtr() == ranges, "missing unit DB targeting resolves AI table");
		checks.Check(unit->GetChaseRange() == 30 && unit->GetTargetingRange() == 18 && unit->GetAggroRange() == 24,
			"AI defaults retain their authored distances");
		checks.Check(unit->GetScreamRange() == 19, "scream distance resolves authored unit logic");
		entry->screamRange = std::numeric_limits<float>::infinity();
		checks.Check(std::isnan(unit->GetScreamRange()), "malformed scream distance fails closed");
	}
	NDb::SessionRoot::InitRoot(saved);
}
