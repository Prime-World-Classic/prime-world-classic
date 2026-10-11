/** Attachments use real movers and fog/voxel updates, never a parallel mount implementation. */
static void AttachmentLifecycleB9(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit; Configure(*raw); NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> carrier = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	CObj<ProbeMover> passenger = new ProbeMover(fixture.world, *db, CVec3(24, 20, 0));
	CObj<ProbeMover> second = new ProbeMover(fixture.world, *db, CVec3(28, 20, 0));
	carrier->AttachUnit(carrier.GetPtr());
	checks.Check(!carrier->IsMounted(), "self attachment is rejected before creating recursive movement");
	carrier->DetachUnit();
	carrier->AttachUnit(passenger.GetPtr());
	checks.Check(passenger->IsMounted(), "live passenger enters mounted movement state");
	carrier->AttachUnit(nullptr);
	checks.Check(passenger->IsMounted(), "invalid replacement preserves the existing attachment");
	if (!passenger->IsMounted()) carrier->AttachUnit(passenger.GetPtr());
	passenger->Stop(false);
	checks.Check(passenger->IsMounted(), "ordinary stop cannot break attachment ownership");
	if (!passenger->IsMounted()) { carrier->DetachUnit(); carrier->AttachUnit(passenger.GetPtr()); }
	second->AttachUnit(passenger.GetPtr()); second->DetachUnit();
	checks.Check(passenger->IsMounted(), "another carrier cannot steal and detach a mounted passenger");
	if (!passenger->IsMounted()) { carrier->DetachUnit(); carrier->AttachUnit(passenger.GetPtr()); }
	passenger->AttachUnit(carrier.GetPtr());
	checks.Check(!carrier->IsMounted(), "attachment chain cannot create a cycle");
	passenger->DetachUnit(); carrier->DetachUnit();
	Fixture foreign(checks);
	if (foreign.loaded)
	{
		CObj<ProbeMover> outsider = new ProbeMover(foreign.world, *db, CVec3(20, 20, 0));
		carrier->AttachUnit(outsider.GetPtr());
		checks.Check(!outsider->IsMounted(), "foreign-world attachment is rejected");
		carrier->DetachUnit();
	}
	carrier->AttachUnit(passenger.GetPtr()); passenger->AttachUnit(second.GetPtr());
	carrier->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); carrier->SetUnitSpeed(4);
	carrier->MoveTo(CVec2(30, 20), 0); carrier->Step(0.5f);
	checks.Check(fabs2(passenger->GetPosition() - carrier->GetPosition()) == 0
		&& fabs2(second->GetPosition() - carrier->GetPosition()) == 0,
		"valid chain propagates actual position updates");
	carrier->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	checks.Check(!passenger->IsMounted() && second->IsMounted(), "carrier death releases its immediate passenger only");
	passenger->DetachUnit(); carrier->DetachUnit();
	carrier->AttachUnit(second.GetPtr());
	checks.Check(!second->IsMounted(), "dead carrier cannot acquire another attachment");
	carrier->DetachUnit();
	second->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	passenger->AttachUnit(second.GetPtr());
	checks.Check(!second->IsMounted(), "dead passenger cannot be mounted");
	passenger->DetachUnit();
}
