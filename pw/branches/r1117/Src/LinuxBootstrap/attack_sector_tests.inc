/** Authored sector count, angular preference and both collision radii determine formation. */
static void AttackSectorGeometry(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit; Configure(*raw); NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> target = new ProbeMover(fixture.world, *db, CVec3(40, 40, 0));
	CObj<ProbeMover> attacker = new ProbeMover(fixture.world, *db, CVec3(20, 40, 0));
	target->SetObjectSize(6); attacker->SetObjectSize(2);
	for (int count : {3, 4, 8})
	{
		target->InitAttackSectors(count);
		for (int sector = 0; sector < count; ++sector)
		{
			target->ClearAttackSectors();
			const CVec2 direction = target->GetAttackSectorDir(sector);
			attacker->TeleportTo(CVec2(40, 40) + direction * 12);
			const int allocated = target->OccupyAttackSector(attacker.GetPtr());
			checks.Check(allocated == sector, "first allocation prefers the attacker's angular sector");
			checks.Check(target->GetAttackSectorOccupies(allocated) == 1, "sector allocation increments exactly once");
			const CVec2 offset = target->GetAttackSectorOffset(attacker.GetPtr(), allocated);
			checks.Check(fabs(offset - direction * 4) < 0.001f, "sector offset includes attacker and target half sizes");
			const int other = target->OccupyAttackSector(attacker.GetPtr());
			checks.Check(other != allocated && target->GetAttackSectorOccupies(other) == 1,
				"occupied preferred sector searches a nearby free sector");
			target->ReleaseAttackSector(allocated); target->ReleaseAttackSector(other);
			checks.Check(target->GetAttackSectorOccupies(allocated) == 0, "released sectors return to zero");
		}
	}
	target->ClearAttackSectors();
	checks.Check(target->OccupyAttackSector(nullptr) == -1, "missing attacker cannot consume a sector");
	checks.Check(target->GetAttackSectorOffset(nullptr, 0) == VNULL2, "missing attacker has no formation offset");
	target->InitAttackSectors(0);
	checks.Check(target->OccupyAttackSector(attacker.GetPtr()) == -1 && target->GetAttackSectorsCount() == 0,
		"explicitly empty formation is not silently replaced by three sectors");
}
