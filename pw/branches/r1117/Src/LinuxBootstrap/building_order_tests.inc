/** A mutable region isolates state admission from navigation and authored geometry. */
class B9Region : public IPointChecking, public PFWorldObjectBase
{
	OBJECT_BASIC_METHODS(B9Region);
public:
	bool good = false;
	B9Region() : PFWorldObjectBase(nullptr, 0) {}
	bool IsGoodTile(const SVector&) const override { return good; }
};

/** Building orders obey region, sight, prohibition, restart and ordinary-motion cleanup. */
static void BuildingMoveLifecycle(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit; Configure(*raw);
	AddStat(*const_cast<NDb::StatsContainer*>(raw->stats.GetPtr()), NDb::STAT_RANGE, "4");
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> owner = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	CObj<ProbeUnit> target = new ProbeUnit(fixture.world, db);
	target->Relocate(CVec3(40, 20, 0));
	CObj<B9Region> region = new B9Region;
	owner->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); owner->SetUnitSpeed(4);
	region->good = true;
	owner->EnqueueState(new PFBaseUnitMoveToBuildingState(owner.GetPtr(), target.GetPtr(), region, false), true);
	owner->Step(0);
	checks.Check(owner->IsIdle() && !owner->IsMoving(), "good building-region tile completes without center-distance approach");
	region->good = false;
	owner->EnqueueState(new PFBaseUnitMoveToBuildingState(owner.GetPtr(), target.GetPtr(), region, false), true);
	owner->Step(0); owner->Stop(false); owner->Step(0);
	checks.Check(owner->IsMoving(), "interrupted building approach restarts");
	owner->Cleanup(); checks.Check(!owner->IsMoving(), "building cleanup stops ordinary movement");
	for (int reason = 0; reason < 3; ++reason)
	{
		if (reason == 0) owner->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		if (reason == 1) owner->OnHold(true);
		owner->EnqueueState(new PFBaseUnitMoveToBuildingState(owner.GetPtr(), target.GetPtr(), reason == 2 ? nullptr : region.GetPtr(), false), true);
		owner->Step(0);
		checks.Check(owner->IsIdle() && !owner->IsMoving(), "building approach rejects forbid, hold and missing region");
		if (reason == 0) owner->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
		if (reason == 1) owner->OnHold(false);
		owner->Cleanup(); owner->Stop(false);
	}
	owner->EnqueueState(new PFBaseUnitMoveToBuildingState(owner.GetPtr(), target.GetPtr(), region, false), true);
	owner->Step(0); owner->MoveToSpecial(CVec2(60, 20), false); owner->Cleanup();
	checks.Check(owner->IsMovingSpecial(), "building cleanup preserves special movement");
	owner->Stop(false);
	CObj<PFBaseUnitMoveToBuildingState> continuing = new PFBaseUnitMoveToBuildingState(owner.GetPtr(), target.GetPtr(), region, false);
	continuing->NeedStopOnLeave(false); owner->EnqueueState(continuing.GetPtr(), true); owner->Step(0); owner->Cleanup();
	checks.Check(owner->IsMoving(), "building opt-out retains ordinary movement");
	owner->Stop(false); region->good = true; owner->CloseWarFog(true);
	owner->EnqueueState(new PFBaseUnitMoveToBuildingState(owner.GetPtr(), target.GetPtr(), region, true), true);
	owner->Step(0);
	checks.Check(!owner->IsIdle() && owner->IsMoving(), "good building region still requires requested personal sight");
	owner->Cleanup(); owner->Stop(false);
}
