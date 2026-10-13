/** Expose only the production status lifecycle, without alternate child execution. */
struct StatusLifecycleProbeB12 : PFApplStatus
{
	explicit StatusLifecycleProbeB12(const PFApplCreatePars& pars) : PFApplStatus(pars) {}
	bool Initialize() { return Init(); }
	bool Tick(float dt) { return Step(dt); }
	void Finish() { Stop(); }
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
};

/** Owned ability and children let normal engine accounting reveal premature completion. */
struct StatusLifecycleFixtureB12
{
	CObj<B9CastHero> hero;
	NDb::Ptr<NDb::Ability> abilityDb;
	CObj<PFAbilityData> ability;
	CObj<PFAbilityInstance> instance;
	NDb::Ptr<NDb::StatusApplicator> db;
	CObj<StatusLifecycleProbeB12> app;
	StatusLifecycleFixtureB12(PFWorld* world, NDb::EStopBehaviour policy, bool instant, bool empty = false)
	{
		hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		abilityDb = new NDb::Ability;
		ability = new PFAbilityData(hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		instance = new PFAbilityInstance(ability, Target(hero.GetPtr()), true);
		NDb::StatusApplicator* raw = new NDb::StatusApplicator;
		raw->stopBehaviour = policy; raw->lifeTime.sString = "10";
		if (!empty)
		{
			if (instant)
			{
				NDb::PermanentStatModApplicator* child = new NDb::PermanentStatModApplicator;
				child->modifier.stat = NDb::STAT_STRENGTH;
				child->modifier.multValue.sString = "1"; child->modifier.addValue.sString = "1";
				raw->applicators.push_back(child);
			}
			else
			{
				NDb::StatModApplicator* child = new NDb::StatModApplicator;
				child->modifier.stat = NDb::STAT_STRENGTH;
				child->modifier.multValue.sString = "1"; child->modifier.addValue.sString = "2";
				child->lifeTime.sString = ".25"; raw->applicators.push_back(child);
			}
			NDb::StatModApplicator* child = new NDb::StatModApplicator;
			child->modifier.stat = NDb::STAT_STRENGTH;
			child->modifier.multValue.sString = "1"; child->modifier.addValue.sString = "3";
			child->lifeTime.sString = "1"; raw->applicators.push_back(child);
		}
		db = raw;
		const Target target(hero.GetPtr());
		PFApplCreatePars pars(instance, target); pars.pDBAppl = db;
		app = new StatusLifecycleProbeB12(pars);
	}
	bool Begin() { return app->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), instance); }
	~StatusLifecycleFixtureB12() { app->Finish(); hero->Cleanup(); }
};

static void StatusLifecycleB12(Checks& check, PFWorld* world)
{
	for (auto policy : {NDb::STOPBEHAVIOUR_STOPBYCHILD, NDb::STOPBEHAVIOUR_STOPBYALLCHILDS, NDb::STOPBEHAVIOUR_STOPBYTIME})
	{
		StatusLifecycleFixtureB12 f(world, policy, true);
		const float initial = f.hero->GetStatValue(NDb::STAT_STRENGTH);
		check(f.Begin(), "status creates real instant and persistent child applicators");
		check(f.app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 2 &&
			f.hero->GetStatValue(NDb::STAT_STRENGTH) == initial + 4,
			"instant child completion does not terminate its parent during sibling creation");
		check(f.app->Tick(0) == (policy == NDb::STOPBEHAVIOUR_STOPBYCHILD),
			"child-driven parent completion is reported by Step rather than a synchronous notification");
		f.app->Finish(); f.app->Finish();
		check(f.instance->GetActiveApplicatorsCount() == 0 && f.hero->GetStatValue(NDb::STAT_STRENGTH) == initial + 1,
			"status Stop removes its persistent children once while preserving an instant permanent change");
	}
	for (auto policy : {NDb::STOPBEHAVIOUR_STOPBYCHILD, NDb::STOPBEHAVIOUR_STOPBYALLCHILDS, NDb::STOPBEHAVIOUR_STOPBYTIME})
	{
		StatusLifecycleFixtureB12 f(world, policy, false, true);
		check(f.Begin() && f.app->Tick(0) == (policy == NDb::STOPBEHAVIOUR_STOPBYALLCHILDS),
			"empty status follows the original any-child all-children and time completion rules");
	}
	for (auto policy : {NDb::STOPBEHAVIOUR_STOPBYCHILD, NDb::STOPBEHAVIOUR_STOPBYALLCHILDS, NDb::STOPBEHAVIOUR_STOPBYTIME})
	{
		StatusLifecycleFixtureB12 f(world, policy, false);
		const float initial = f.hero->GetStatValue(NDb::STAT_STRENGTH);
		check(f.Begin() && f.instance->GetActiveApplicatorsCount() == 3,
			"timed status owns two actual persistent child buffs");
		f.hero->Simulate(.25f);
		check(f.app->IsAlive() == (policy != NDb::STOPBEHAVIOUR_STOPBYCHILD) &&
			f.hero->GetStatValue(NDb::STAT_STRENGTH) == initial + (policy == NDb::STOPBEHAVIOUR_STOPBYCHILD ? 0 : 3),
			"real unit stepping completes any-child status but retains all-child and timed parents");
		f.hero->Simulate(.75f);
		check(f.app->IsAlive() == (policy == NDb::STOPBEHAVIOUR_STOPBYTIME) &&
			f.hero->GetStatValue(NDb::STAT_STRENGTH) == initial,
			"last timed child completes all-child status without shortening a time-owned parent");
		f.hero->Simulate(9);
		check(!f.app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"authored status deadline leaves no owned children or active ability count");
	}
	{
		AbilityModFixtureB12 modifier(world, NDb::ABILITYMODMODE_DURATION, ".5", "1");
		modifier.Edit().targetAbilities = NDb::ABILITYIDFLAGS_ABILITY1;
		check(modifier.app->Initialize() && !modifier.app->Run(), "duration modifier uses a real hero ability ring");
		NDb::Ptr<NDb::Ability> abilityDb = new NDb::Ability;
		CObj<PFAbilityData> ability = new PFAbilityData(modifier.hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_ABILITY1, false, false);
		CObj<PFAbilityInstance> instance = new PFAbilityInstance(ability, Target(modifier.hero.GetPtr()), true);
		for (const char* lifetime : {"10", "0", "-1"})
		{
			NDb::StatusApplicator* raw = new NDb::StatusApplicator; raw->lifeTime.sString = lifetime;
			NDb::Ptr<NDb::StatusApplicator> db = raw;
			const Target target(modifier.hero.GetPtr());
			PFApplCreatePars pars(instance, target); pars.pDBAppl = db;
			CObj<StatusLifecycleProbeB12> app = new StatusLifecycleProbeB12(pars);
			check(app->Initialize() && app->GetDuration() == (lifetime[0] == '1' ? 6.f : lifetime[0] == '0' ? 0.f : -1.f),
				"positive status duration uses source ability modifiers while zero and infinite sentinels remain unchanged");
		}
	}
}
