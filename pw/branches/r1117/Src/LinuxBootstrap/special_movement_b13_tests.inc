// Include after pathfinding_b13_tests.inc: reuse its real loaded-world fixture.

/** Both original setters break ordinary follow immediately, before a world tick. */
static void SpecialFollowB13(Checks& check)
{
	for (bool unitTarget : {false, true})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingMoverB13> leader = f.Mover(CVec2(21.5f, 60.5f));
		CObj<RoutingMoverB13> target = f.Mover(CVec2(20.5f, 20.5f));
		mover->FollowTo(CPtr<PFBaseMovingUnit>(leader.GetPtr()));
		check(mover->GetFollowedBy().GetPtr() == leader.GetPtr(), "special: ordinary follow established by the original setter");
		leader->TeleportTo(CVec2(150.5f, 60.5f)); f.Wall(NWorld::MAP_MODE_STATIC);
		const bool accepted = unitTarget ? mover->MoveToSpecial(CPtr<PFBaseUnit>(target.GetPtr()), false)
			: mover->MoveToSpecial(CVec2(20.5f, 20.5f), false);
		check(accepted && mover->IsMovingSpecial(), "special: both setters admit direct special state before movement");
		check(!IsValid(mover->GetFollowedBy()), "special: setter clears ordinary follow before movement");
		check(mover->GetPosition().AsVec2D() == CVec2(20.5f, 60.5f), "special: setter does not move the unit");
		check(mover->GetDestination() == CVec2(20.5f, 20.5f), "special: setter retains requested direct destination");
		f.Tick(.25f);
		check(mover->IsMovingSpecial() && fabs(mover->GetPosition().x - 20.5f) < .001f
			&& fabs(mover->GetPosition().y - 57.5f) < .001f, "special: old leader cannot replace special movement with an ordinary route");
		mover->Stop(false); const CVec2 stopped = mover->GetPosition().AsVec2D();
		leader->TeleportTo(CVec2(160.5f, 60.5f)); f.Tick(.25f);
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == stopped,
			"special: cancellation cannot revive the previously cleared follow");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingMoverB13> leader = f.Mover(CVec2(21.5f, 60.5f));
		mover->FollowTo(CPtr<PFBaseMovingUnit>(leader.GetPtr()), true);
		check(mover->MoveToSpecial(CVec2(20.5f, 20.5f), false), "special: forced-follow control admits original special setter");
		check(mover->GetFollowedBy().GetPtr() == leader.GetPtr(), "special: original nonforced FollowTo(NULL) preserves forced follow");
		mover->FollowTo(CPtr<PFBaseMovingUnit>(), true);
		check(!IsValid(mover->GetFollowedBy()) && !mover->IsMoving(), "special: explicit forced cancellation retains original policy");
		mover->FollowTo(CPtr<PFBaseMovingUnit>(leader.GetPtr()));
		leader->TeleportTo(CVec2(35.5f, 60.5f)); f.Tick(.25f);
		check(mover->GetPosition().x > 20.5f && mover->GetFollowedBy().GetPtr() == leader.GetPtr(),
			"special: ordinary follow remains functional without a special command");
	}
}

/** The original special tick bounds every sample independently of collision masks. */
static void SpecialEdgesB13(Checks& check)
{
	const CVec2 starts[] = {CVec2(198.5f, 60.5f), CVec2(1.5f, 60.5f),
		CVec2(100.5f, 118.5f), CVec2(100.5f, 1.5f)};
	const CVec2 goals[] = {CVec2(200.5f, 60.5f), CVec2(-.5f, 60.5f),
		CVec2(100.5f, 120.5f), CVec2(100.5f, -.5f)};
	for (int mode = 0; mode < 3; ++mode)
		for (int edge = 0; edge < 4; ++edge)
		{
			RoutingFixtureB13 f(check); if (!f.world) continue;
			CObj<RoutingMoverB13> mover = f.Mover(starts[edge]);
			if (mode == 1) mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
			if (mode == 2) mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNORESTATIC
				| NDb::GHOSTMOVEMODE_IGNOREBUILDINGS | NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
			check(mover->MoveToSpecial(goals[edge], false), "special: finite off-map goal is admitted for original boundary-stop behavior");
			check(mover->GetPosition().AsVec2D() == starts[edge] && mover->IsMovingSpecial(),
				"special: off-map setter neither clamps the goal nor mutates position");
			f.Tick(.25f);
			const CVec2 at = mover->GetPosition().AsVec2D();
			check(std::isfinite(at.x) && std::isfinite(at.y) && at.x >= 0 && at.y >= 0 && at.x < 200 && at.y < 120,
				"special: all four world edges remain bounded independently of collision masks");
			check(!mover->IsMoving() && at != goals[edge], "special: edge collision stops without claiming the outside endpoint");
			f.Tick(.25f);
			check(mover->GetPosition().AsVec2D() == at, "special: boundary stop has no residual displacement");
		}
	for (const CVec2& goal : {CVec2(0, 60.5f), CVec2(100.5f, 0),
		CVec2(199.75f, 60.5f), CVec2(100.5f, 119.75f)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		const CVec2 start(Clamp(goal.x, 1.5f, 198.5f), Clamp(goal.y, 1.5f, 118.5f));
		CObj<RoutingMoverB13> mover = f.Mover(start);
		mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		check(mover->MoveToSpecial(goal, false), "special: valid boundary/subtile endpoint admitted"); f.Tick(.25f);
		check(!mover->IsMoving() && fabs(mover->GetPosition().AsVec2D() - goal) < .001f,
			"special: zero edge is inclusive and uppermost in-map subtile is reachable without collision");
	}
	for (const CVec2& goal : {CVec2(200, 60.5f), CVec2(100.5f, 120)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(Min(goal.x, 198.5f), Min(goal.y, 118.5f)));
		mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		check(mover->MoveToSpecial(goal, false), "special: exact exclusive upper boundary remains a finite admitted goal"); f.Tick(.25f);
		check(!mover->IsMoving() && mover->GetPosition().x < 200 && mover->GetPosition().y < 120,
			"special: exact upper boundary is outside even with no collision");
	}
	for (const CVec2& goal : {CVec2(-1.e-8f, 60.5f), CVec2(100.5f, -1.e-8f)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(Max(goal.x, .25f), Max(goal.y, .25f)));
		mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		check(mover->MoveToSpecial(goal, false), "special: tiny finite outside endpoint remains admitted"); f.Tick(.25f);
		check(!mover->IsMoving() && mover->GetPosition().x >= 0 && mover->GetPosition().y >= 0,
			"special: rounded trace endpoint cannot authorize an exact negative coordinate commit");
	}
}

/** Guard normalization and placement without narrowing finite off-map destinations. */
static void SpecialNumericB13(Checks& check)
{
	const float inf = std::numeric_limits<float>::infinity();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float huge = std::numeric_limits<float>::max();
	for (const CVec2& bad : {CVec2(nan, 20), CVec2(20, nan), CVec2(inf, 20),
		CVec2(20, -inf), CVec2(huge, 20), CVec2(-huge, huge), CVec2(1.e20f, 1.e20f)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingMoverB13> leader = f.Mover(CVec2(21.5f, 60.5f));
		mover->FollowTo(CPtr<PFBaseMovingUnit>(leader.GetPtr()));
		const CVec2 oldGoal = mover->GetDestination();
		const bool accepted = mover->MoveToSpecial(bad, false);
		check(!accepted, "special: nonfinite or overflow-length destination rejected before normalization");
		check(mover->GetFollowedBy().GetPtr() == leader.GetPtr() && mover->GetDestination() == oldGoal
			&& !mover->IsMovingSpecial(), "special: invalid setter does not partially cancel a valid follow command");
		check(mover->GetPosition().AsVec2D() == CVec2(20.5f, 60.5f), "special: invalid setter leaves position unchanged");
		// Do not step an invalid baseline admission merely to exercise undefined downstream conversion.
		mover->FollowTo(CPtr<PFBaseMovingUnit>(), true); mover->Stop(false);
	}
	for (const CVec2& badOrigin : {CVec2(-1, 60.5f), CVec2(200, 60.5f), CVec2(huge, 60.5f)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		check(mover->TeleportTo(badOrigin), "special: existing explicit finite teleport policy remains unchanged");
		check(!mover->MoveToSpecial(CVec2(20.5f, 60.5f), false), "special: invalid origin cannot start bounded special movement");
		check(mover->GetPosition().AsVec2D() == badOrigin, "special: failed admission never relocates an invalid origin");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(198.5f, 60.5f));
		mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		check(mover->MoveToSpecial(CVec2(1.e18f, 60.5f), false), "special: representable finite off-map length is not narrowed to a tile");
		mover->SetUnitSpeed(huge); const CVec2 before = mover->GetPosition().AsVec2D();
		f.Tick(2);
		check(mover->GetPosition().AsVec2D() == before, "special: overflow speed-times-delta cannot mutate coordinates");
		// A small budget makes this regression safe even on the unbounded baseline.
		mover->SetUnitSpeed(12); f.Tick(.25f);
		check(!mover->IsMoving() && mover->GetPosition().x < 200, "special: huge finite goal stops at first world edge without an unbounded trace");
	}
}

/** Special movement stays direct; masking obstacles must not mask the world extent. */
static void SpecialDirectB13(Checks& check)
{
	for (bool noCollide : {false, true})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue; f.Wall(NWorld::MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(68.5f, 60.5f));
		CObj<RoutingMoverB13> blocker = f.Mover(CVec2(75.5f, 60.5f));
		if (noCollide) mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		check(mover->MoveToSpecial(CVec2(78.5f, 60.5f), false), "special: direct wall test admitted");
		for (int i = 0; i < 6 && mover->IsMoving(); ++i) f.Tick(.25f);
		check(fabs(mover->GetPosition().y - 60.5f) < .001f, "special: direct movement never acquires an ordinary wall detour");
		check(!mover->IsMoving() && (noCollide ? fabs(mover->GetPosition().x - 78.5f) < .001f : mover->GetPosition().x < 70),
			"special: no-collide crosses static and dynamic blockers while ordinary special stops at the wall");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(NWorld::MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		mover->MoveTo(CVec2(150.5f, 60.5f), 0); f.Tick();
		check(mover->IsMoving() && !mover->IsMovingSpecial(), "special: ordinary routed command started before replacement");
		const CVec2 at = mover->GetPosition().AsVec2D();
		check(mover->MoveToSpecial(at + CVec2(0, -10), false), "special: setter replaces an executable ordinary route");
		f.Tick(.25f);
		check(fabs(mover->GetPosition().x - at.x) < .001f && fabs(mover->GetPosition().y - (at.y - 3)) < .001f,
			"special: replacing an ordinary route consumes only the new direct segment");
		mover->TeleportTo(CVec2(30.5f, 20.5f)); f.Tick(.25f);
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == CVec2(30.5f, 20.5f),
			"special: explicit teleport cancels the active special command");
		mover->MoveToSpecial(CVec2(30.5f, 40.5f), false);
		mover->MoveTo(CVec2(40.5f, 20.5f), 0); f.Tick(.25f);
		check(!mover->IsMovingSpecial() && mover->GetPosition().x > 30.5f && fabs(mover->GetPosition().y - 20.5f) < .001f,
			"special: new ordinary command cancels special state and uses its own destination");
		mover->Stop(false); const CVec2 stopped = mover->GetPosition().AsVec2D();
		check(mover->MoveToSpecial(stopped, false), "special: zero-distance setter is admitted"); f.Tick(.25f);
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == stopped, "special: zero-distance command completes without normalization or movement");
		check(!mover->MoveToSpecial(CPtr<PFBaseUnit>(), false), "special: absent unit target remains rejected");
		CObj<RoutingMoverB13> leader = f.Mover(stopped + CVec2(2, 0));
		mover->MoveToSpecial(stopped + CVec2(0, 10), false);
		mover->FollowTo(CPtr<PFBaseMovingUnit>(leader.GetPtr()));
		check(!mover->IsMovingSpecial() && mover->GetFollowedBy().GetPtr() == leader.GetPtr(),
			"special: a new ordinary follow command cancels special movement before stepping");
		leader->TeleportTo(stopped + CVec2(20, 0)); f.Tick(.25f);
		check(mover->GetPosition().x > stopped.x && fabs(mover->GetPosition().y - stopped.y) < .001f,
			"special: replacement follow advances toward its leader without residual special displacement");
	}
}

/** Keep special-movement fixtures independent of the outer probe's session settings. */
static void SpecialMovementB13(Checks& check)
{
	struct RootScope
	{
		NDb::Ptr<NDb::SessionRoot> saved;
		RootScope() : saved(NDb::SessionRoot::GetRoot())
		{
			auto* logic = new NDb::SessionLogicRoot; logic->aiLogic = new NDb::AILogicParameters;
			auto* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
		}
		~RootScope() { NDb::SessionRoot::InitRoot(saved); }
	} rootScope;
	SpecialFollowB13(check);
	SpecialEdgesB13(check);
	SpecialNumericB13(check);
	SpecialDirectB13(check);
}
