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

/** Actual attack orders use checked target-dependent range and chase expressions. */
static void AttackFormulaPolicy(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	struct Case { const char* expression; bool chase; };
	for (const Case policy : {Case{"", true}, Case{"0", false}, Case{"1", true},
		Case{"-1", true}, Case{"sLife == tLife", true}, Case{"sLife > tLife", false},
		Case{"unknownPolicy", false}, Case{"1/0", false}})
	{
		NDb::Unit* raw = new NDb::Unit; Configure(*raw);
		AddStat(*const_cast<NDb::StatsContainer*>(raw->stats.GetPtr()), NDb::STAT_RANGE, "4");
		NDb::BaseAttack* attack = new NDb::BaseAttack;
		attack->useRange.sString = "sRange + tRange";
		attack->allowChase.sString = policy.expression;
		raw->attackAbility = attack;
		NDb::Ptr<NDb::Unit> db = raw;
		CObj<ProbeMover> owner = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
		CObj<ProbeMover> target = new ProbeMover(fixture.world, *db, CVec3(28, 20, 0));
		target->ChangeFaction(Enemy); fixture.Tick();
		checks.Check(owner->IsTargetInAttackRange(target.GetPtr(), true),
			"attack range uses both live sender and target properties");
		attack->useRange.sString = "unknownRange";
		target->TeleportTo(CVec2(22, 20));
		checks.Check(!owner->IsTargetInAttackRange(target.GetPtr(), true),
			"invalid attack range never falls back to an unrelated stat");
		target->TeleportTo(CVec2(28, 20));
		attack->useRange.sString = "4";
		owner->SetUnitSpeed(4); owner->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		owner->EnqueueState(new PFBaseUnitAttackState(fixture.world, owner.GetPtr(), target.GetPtr(), true), true);
		owner->Step(0.1f);
		checks.Check(owner->IsMoving() == policy.chase, "authored chase expression controls approach");
		checks.Check(owner->IsIdle() != policy.chase, "denied chase ends the order without damage");
		checks.Check(target->GetHealth() == 100, "chase policy cannot synthesize damage");
		owner->Cleanup(); owner->Stop(false);
		target->TeleportTo(CVec2(22, 20));
		owner->EnqueueState(new PFBaseUnitAttackState(fixture.world, owner.GetPtr(), target.GetPtr(), true), true);
		owner->Step(0.0f);
		checks.Check(!owner->IsIdle() && !owner->IsMoving(), "chase permission never forbids an already-in-range attack");
		owner->Cleanup(); owner->Stop(false);
	}
}

/** Point orders stop at range, recover from interruption and leave no ordinary motion. */
static void PointMoveLifecycle(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit; Configure(*raw); NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> owner = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	owner->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); owner->SetUnitSpeed(4);
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(), CVec2(24, 20), 5), true);
	owner->Step(0.0f);
	checks.Check(owner->IsIdle() && !owner->IsMoving(), "already-in-range point order does not start motion");
	owner->Cleanup(); owner->Stop(false);
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(), CVec2(40, 20), 2), true);
	owner->Step(0.0f); owner->Stop(false); owner->Step(0.0f);
	checks.Check(owner->IsMoving(), "point order resumes ordinary interrupted movement");
	for (int i = 0; i < 100 && !owner->IsIdle(); ++i) owner->Step(0.1f);
	checks.Check(owner->IsIdle() && !owner->IsMoving(), "finished point order stops motion");
	checks.Check(owner->IsPositionInRange(CVec2(40, 20), 2) && owner->GetPosition().x <= 38.01f,
		"point order retains authored stopping distance");
	owner->TeleportTo(CVec2(20, 20));
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(), CVec2(40, 20), 2), true);
	owner->Step(0.0f); owner->Cleanup();
	checks.Check(!owner->IsMoving(), "point state cleanup stops ordinary movement");
	owner->Stop(false); owner->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(), CVec2(40, 20), 2), true);
	owner->Step(0.0f);
	checks.Check(owner->IsIdle() && !owner->IsMoving(), "forbidden point movement terminates its state");
	owner->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE); owner->Cleanup(); owner->Stop(false);
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(),
		CVec2(std::numeric_limits<float>::quiet_NaN(), 20), 2), true);
	owner->Step(0.0f);
	checks.Check(owner->IsIdle() && !owner->IsMoving() && std::isfinite(owner->GetPosition().x),
		"invalid point goal ends before movement or coordinate mutation");
	owner->Cleanup(); owner->Stop(false);
	owner->CloseWarFog(true);
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(), CVec2(24, 20), 5, true), true);
	owner->Step(0.0f);
	checks.Check(!owner->IsIdle() && owner->IsMoving(), "point arrival also requires requested personal sight");
	owner->Cleanup(); owner->Stop(false);
	owner->EnqueueState(new PFBaseUnitMoveToState(owner.GetPtr(), CVec2(40, 20), 2), true);
	owner->Step(0.0f); owner->MoveToSpecial(CVec2(60, 20), false); owner->Cleanup();
	checks.Check(owner->IsMovingSpecial(), "point cleanup does not interrupt special movement");
	owner->Stop(false);
}
