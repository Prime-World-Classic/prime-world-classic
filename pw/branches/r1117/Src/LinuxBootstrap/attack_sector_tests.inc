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

/** Real queued attacks own chase sectors and release them on completion or interruption. */
static void ChaseSectorOwnership(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit; Configure(*raw);
	AddStat(*const_cast<NDb::StatsContainer*>(raw->stats.GetPtr()), NDb::STAT_RANGE, "1");
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> target = new ProbeMover(fixture.world, *db, CVec3(28, 20, 0));
	target->ChangeFaction(Enemy); target->SetObjectSize(2); target->InitAttackSectors(4);
	CObj<ProbeMover> first = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	CObj<ProbeMover> second = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	first->SetObjectSize(2); second->SetObjectSize(2); fixture.Tick();
	for (ProbeMover* mover : {first.GetPtr(), second.GetPtr()})
	{
		mover->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); mover->SetUnitSpeed(4);
		mover->EnqueueState(new PFBaseUnitAttackState(fixture.world, mover, target.GetPtr(), true), true);
		mover->Step(0);
	}
	int occupied = 0; for (int i = 0; i < 4; ++i) occupied += target->GetAttackSectorOccupies(i);
	checks.Check(occupied == 2 && target->GetAttackSectorOccupies(2) == 1,
		"queued production attacks allocate distinct chase sectors");
	for (int i = 0; i < 100; ++i) { first->Step(0.1f); second->Step(0.1f); }
	checks.Check(first->IsTargetInAttackRange(target.GetPtr(), true) && second->IsTargetInAttackRange(target.GetPtr(), true),
		"formation movement cannot stop outside actual attack range");
	checks.Check(!first->IsMoving() && !second->IsMoving(), "formation arrival stops both attackers");
	std::printf("B9 formation positions: first=(%.5f,%.5f) second=(%.5f,%.5f)\n",
		first->GetPosition().x, first->GetPosition().y, second->GetPosition().x, second->GetPosition().y);
	checks.Check(fabs(first->GetPosition().AsVec2D() - second->GetPosition().AsVec2D()) > 0.2f,
		"competing attackers approach different formation directions");
	first->Cleanup(); second->Cleanup();
	occupied = 0; for (int i = 0; i < 4; ++i) occupied += target->GetAttackSectorOccupies(i);
	checks.Check(occupied == 0, "finished and cleaned attack orders leave no occupied sectors");
	first->TeleportTo(CVec2(20, 20));
	CObj<PFBaseUnitChaseState> retained = new PFBaseUnitChaseState(first.GetPtr(), false, 1, 20, target.GetPtr(), true);
	first->EnqueueState(retained.GetPtr(), true); first->Step(0); first->Cleanup();
	occupied = 0; for (int i = 0; i < 4; ++i) occupied += target->GetAttackSectorOccupies(i);
	checks.Check(occupied == 0, "leaving a still-owned chase releases occupancy before object destruction");
	retained = nullptr;
	occupied = 0; for (int i = 0; i < 4; ++i) occupied += target->GetAttackSectorOccupies(i);
	checks.Check(occupied == 0, "later chase destruction cannot release a slot twice");
}
