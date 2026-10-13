#include <functional>

/** A real endpoint checker whose controlled callbacks exercise movement ownership boundaries. */
class RoutingCallbackB13 : public IPointChecking, public PFWorldObjectBase
{
	OBJECT_BASIC_METHODS(RoutingCallbackB13);
public:
	SVector start;
	bool atOrigin;
	bool result;
	bool enabled = true;
	mutable bool fired = false;
	mutable int calls = 0;
	std::function<void()> action;
	bool* destroyed = nullptr;
	RoutingCallbackB13(const SVector& start = SVector(), bool atOrigin = false, bool result = false)
		: PFWorldObjectBase(nullptr, 0), start(start), atOrigin(atOrigin), result(result) {}
	~RoutingCallbackB13() { if (destroyed) *destroyed = true; }
	bool IsGoodTile(const SVector& tile) const override
	{
		++calls;
		if (enabled && !fired && (atOrigin || tile != start))
		{
			fired = true;
			action();
		}
		return fired && result;
	}
};

/** Callback state changes must take effect before a route or old arrival result is committed. */
static void RoutingCallbacksB13(Checks& check)
{
	for (bool atOrigin : {false, true}) for (bool result : {false, true}) for (int action = 0; action < 7; ++action)
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(MAP_MODE_STATIC);
		const CVec2 start(20.5f, 60.5f), goal(150.25f, 60.75f), replacement(20.5f, 15.5f), relocated(15.5f, 20.5f);
		CObj<RoutingMoverB13> mover = f.Mover(start);
		CObj<RoutingCallbackB13> checker = new RoutingCallbackB13(f.world->GetTileMap()->GetTile(start), atOrigin, result);
		CVec2 callbackPosition;
		checker->action = [&] {
			if (action == 0) mover->Stop(false);
			if (action == 1) mover->MoveTo(replacement, 0);
			if (action == 2) { mover->Stop(false); mover->MoveTo(goal, 0, checker.GetPtr()); }
			if (action == 3) mover->TeleportTo(relocated);
			if (action == 4) mover->KillUnit();
			if (action == 5) f.world->UnregisterObject(mover.GetPtr(), false);
			if (action == 6) mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNORESTATIC, false);
			callbackPosition = mover->GetPosition().AsVec2D();
		};
		mover->MoveTo(goal, 0, checker.GetPtr());
		f.Tick();
		check(checker->fired, "routing callback runs at its intended origin/search boundary");
		check(mover->GetPosition().AsVec2D() == callbackPosition,
			"cancelled or replaced routing operation cannot commit displacement after its callback");
		if (action == 0 || action == 3 || action == 4)
			check(!mover->IsMoving(), "stop, teleport and death callback states survive staged route completion");
		if (action == 1 || action == 2 || action == 6)
		{
			check(mover->IsMoving(), "old predicate success cannot stop a replacement movement command");
			check(mover->GetDestination() == (action == 1 ? replacement : goal),
				"old path construction cannot overwrite the callback's replacement destination");
			checker->result = false;
			f.Tick();
			check(mover->GetPosition().AsVec2D() != callbackPosition,
				"replacement movement remains eligible on the following world tick");
		}
		else
		{
			f.Tick();
			check(mover->GetPosition().AsVec2D() == callbackPosition,
				"cancelled route cannot resume during the following world tick");
		}
		mover->Stop(false);
	}
	for (int change = 0; change < 3; ++change)
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingCallbackB13> checker = new RoutingCallbackB13(SVector(20, 60), true, false);
		checker->action = [&] {
			if (change == 0) mover->Step(.1f);
			if (change == 1) mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
			if (change == 2) mover->SetUnitSpeed(6);
		};
		mover->MoveTo(CVec2(150.25f, 60.75f), 0, checker.GetPtr()); f.Tick();
		check(checker->fired && mover->GetPosition().AsVec2D() == CVec2(20.5f, 60.5f),
			"reentrant stepping and changed movement flags/speed invalidate the old distance budget");
		f.Tick();
		check(mover->GetPosition().AsVec2D() != CVec2(20.5f, 60.5f),
			"invalidated budget does not poison the following movement attempt");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(MAP_MODE_STATIC);
		CObj<RoutingMoverB13> a = f.Mover(CVec2(20.5f, 60.5f)), b = f.Mover(CVec2(20.5f, 65.5f));
		CObj<RoutingCallbackB13> aCheck = new RoutingCallbackB13(SVector(20, 60), false, false);
		CObj<RoutingCallbackB13> bCheck = new RoutingCallbackB13(SVector(20, 65), false, false);
		bCheck->enabled = false;
		b->MoveTo(CVec2(150.5f, 65.5f), 0, bCheck.GetPtr()); b->Step(.1f);
		check(b->IsMoving() && bCheck->calls > 0, "cross-unit callback fixture installs B's original cursor first");
		int callsAtNestedExit = -1;
		aCheck->action = [&] { b->Step(20); callsAtNestedExit = bCheck->calls; };
		a->MoveTo(CVec2(150.25f, 60.75f), 0, aCheck.GetPtr()); a->Step(.1f);
		check(aCheck->fired && callsAtNestedExit >= 0, "A's original search callback steps B through cursor refill");
		check(bCheck->calls == callsAtNestedExit,
			"rejected nested cursor search cannot replace A's active shared-finder predicate with B's");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingCallbackB13> checker = new RoutingCallbackB13(SVector(20, 60), false, false);
		checker->enabled = false;
		CVec2 cancelledAt;
		checker->action = [&] { cancelledAt = mover->GetPosition().AsVec2D(); mover->Stop(false); };
		mover->MoveTo(CVec2(150.25f, 60.75f), 0, checker.GetPtr()); f.Tick();
		check(mover->IsMoving() && !checker->fired, "late-callback fixture first installs a real original route");
		checker->enabled = true;
		for (int tick = 0; tick < 600 && mover->IsMoving() && !checker->fired; ++tick)
		{
			checker->start = f.world->GetTileMap()->GetTile(mover->GetPosition().AsVec2D());
			f.Tick();
		}
		check(checker->fired && !mover->IsMoving() && mover->GetPosition().AsVec2D() == cancelledAt,
			"cursor-refill callback can release its last live path owner without a stale position commit");
		f.Tick();
		check(mover->GetPosition().AsVec2D() == cancelledAt, "cancelled refilled cursor stays retired next tick");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingCallbackB13> checker = new RoutingCallbackB13(SVector(20, 60), false, false);
		bool destroyed = false, destroyedInside = false;
		checker->destroyed = &destroyed;
		checker->action = [&] {
			mover->Stop(false);
			destroyedInside = destroyed;
		};
		mover->MoveTo(CVec2(150.25f, 60.75f), 0, checker.GetPtr());
		checker = nullptr;
		// The baseline may destroy the active callback here; the live snapshot must retain it.
		f.Tick();
		check(!destroyedInside && destroyed, "last-owner checker release is deferred until the active callback returns");
		check(mover->GetPosition().AsVec2D() == CVec2(20.5f, 60.5f), "checker retirement cannot commit a stale route");
	}
}
