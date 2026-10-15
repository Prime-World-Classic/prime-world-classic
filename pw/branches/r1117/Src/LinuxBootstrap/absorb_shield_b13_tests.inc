/** Real factory, event routing and owning buff lifecycle; descriptors are explicit mock data. */
struct AbsorbShieldFixtureB13 : WrapperFactoryFixtureB12
{
	explicit AbsorbShieldFixtureB13(PFWorld* world) : WrapperFactoryFixtureB12(world) {}
	void Tick() { static_cast<PFBaseUnit*>(receiver.GetPtr())->Step(0); }
	static NDb::AbsorbShieldApplicator* Descriptor(const char* capacity = "100", const char* lifetime = "10")
	{
		auto* db = new NDb::AbsorbShieldApplicator;
		db->enabled.sString = "true"; db->startCondition.sString = "true";
		db->damageToAbsorb.sString = capacity; db->lifeTime.sString = lifetime;
		db->formulaName = "ShieldB13";
		return db;
	}
	CObj<PFBaseApplicator> Apply(Checks& check, const NDb::BaseApplicator* db)
	{
		auto app = Create(db);
		check(app && Activate(app), "B13 shield factory activates the original engine applicator");
		return app;
	}
	float Hit(float amount, bool bypass = false)
	{
		auto desc = DirectDamage(); desc.amount = amount; desc.ignoreDefences = bypass;
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE; desc.pSender = sender;
		return receiver->OnDamage(desc);
	}
};

/** Allow an original parent callback to stop the shield during its start notification. */
struct AbsorbShieldParentB13 : PFBaseApplicator
{
	OBJECT_METHODS(PFBaseApplicator::typeId, AbsorbShieldParentB13);
public:
	AbsorbShieldParentB13() = default;
	explicit AbsorbShieldParentB13(const PFApplCreatePars& cp) : PFBaseApplicator(cp) {}
	int starts = 0, stops = 0;
	void OnNotification(PFBaseApplicator& app, NDb::EParentNotification note) override
	{
		if (note == NDb::PARENTNOTIFICATION_START) { ++starts; app.Stop(); }
		if (note == NDb::PARENTNOTIFICATION_STOP) ++stops;
	}
};

/** Capacity changes immediately, while the aggregate follows the original deferred tick update. */
static void AbsorbShieldB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = AbsorbShieldFixtureB13;
	for (float amount : {0.f, 40.f, 100.f, 140.f}) for (bool bypass : {false, true})
	{
		F f(world); NDb::Ptr<NDb::AbsorbShieldApplicator> db = F::Descriptor();
		auto app = f.Apply(check, db); if (!app) continue;
		check(f.receiver->GetShield() == 100 && app->GetVariable("damageToAbsorb") == 100,
			"B13 shield start publishes authored capacity and named remaining capacity");
		const float absorbed = bypass ? 0.f : std::min(100.f, amount);
		check(f.Hit(amount, bypass) == amount - absorbed && f.receiver->GetHealth() == 800 - amount + absorbed,
			"B13 actual damage consumes capacity before health and ignoreDefences bypasses shields");
		check(app->GetVariable("damageToAbsorb") == 100 - absorbed && f.receiver->GetShield() == 100,
			"B13 shield events update capacity without prematurely changing the displayed aggregate");
		const bool over = app->Step(0);
		check(over == (absorbed == 100) && f.receiver->GetShield() == 100 - absorbed,
			"B13 next shield tick publishes consumed capacity and reports exhaustion");
		if (over) app->Stop();
		app->Stop(); app->Stop();
		check(f.receiver->GetShield() == 0 && f.instance->GetActiveApplicatorsCount() == 0 && f.Hit(5) == 5,
			"B13 repeated Stop removes aggregate listener and ability ownership exactly once");
	}
	for (const char* condition : {"false", "cMissing", "compiled-only"})
	{
		F f(world); auto* raw = F::Descriptor(); NDb::Ptr<NDb::AbsorbShieldApplicator> db = raw, peerDb = F::Descriptor("20");
		raw->startCondition.sString = condition;
		if (strcmp(condition, "compiled-only") == 0) { raw->startCondition.sString = ""; raw->startCondition.compiledString = "unsupported"; }
		auto peer = f.Apply(check, peerDb), app = f.Apply(check, db); if (!peer || !app) continue;
		check(!app->IsAlive() && f.receiver->GetShield() == 20 && f.instance->GetActiveApplicatorsCount() == 1,
			"B13 rejected shield start condition leaves peer aggregate and ownership intact");
	}
	{
		F f(world); auto* raw = F::Descriptor(); NDb::Ptr<NDb::AbsorbShieldApplicator> db = raw;
		raw->enabled.sString = "false"; auto app = f.Apply(check, db); if (app)
		{
			check(app->IsAlive() && !app->IsEnabled() && f.receiver->GetShield() == 0 && f.Hit(25) == 25,
				"B13 initially disabled shield owns no listener or aggregate");
			raw->enabled.sString = "true"; app->Step(0);
			check(f.receiver->GetShield() == 100 && f.Hit(25) == 0 && app->GetVariable("damageToAbsorb") == 75,
				"B13 first enable attaches the unconsumed authored shield");
		}
	}
	{
		F f(world); NDb::Ptr<NDb::AbsorbShieldApplicator> db = F::Descriptor();
		auto app = f.Apply(check, db); if (app)
		{
			CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener; int calls = 0;
			observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				if (const auto* hit = dynamic_cast<const PFBaseUnitDamageEvent*>(event))
				{
					++calls; check(hit->damage2Deal == 0 && app->GetVariable("damageToAbsorb") == 70,
						"B13 real shield precedes ordinary damage observers"); app->Stop();
				}
				return 0u;
			};
			f.receiver->AddEventListener(observer.GetPtr());
			check(f.Hit(30) == 0 && calls == 1 && !app->IsAlive() && f.receiver->GetShield() == 0,
				"B13 callback shield removal retains absorbed damage while releasing the pending aggregate");
			f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
		}
	}
	for (bool reverse : {false, true}) for (bool equal : {false, true})
	{
		F f(world); NDb::Ptr<NDb::AbsorbShieldApplicator> aDb = F::Descriptor("40", "5");
		NDb::Ptr<NDb::AbsorbShieldApplicator> bDb = F::Descriptor("60", equal ? "5" : "10");
		CObj<PFBaseApplicator> a, b;
		if (reverse) { b = f.Apply(check, bDb); a = f.Apply(check, aDb); }
		else { a = f.Apply(check, aDb); b = f.Apply(check, bDb); }
		if (!a || !b) continue;
		check(f.receiver->GetShield() == 100 && f.Hit(50) == 0, "B13 stacked shield capacities jointly absorb real damage");
		const bool firstA = equal && !reverse;
		check(a->GetVariable("damageToAbsorb") == (firstA ? 0 : 40)
			&& b->GetVariable("damageToAbsorb") == (firstA ? 50 : 10),
			"B13 shields preserve original descending duration priority and equal-duration insertion order");
		f.Tick();
		check(f.receiver->GetShield() == 50 && f.Hit(70) == 20,
			"B13 owning tick removes exhausted shields without removing surviving peers");
		f.Tick();
		check(f.receiver->GetShield() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 all exhausted stacked shields retire through owning rings");
	}
	for (int transition = 0; transition < 4; ++transition)
	{
		F f(world); auto* raw = F::Descriptor(); NDb::Ptr<NDb::AbsorbShieldApplicator> db = raw;
		NDb::Ptr<NDb::AbsorbShieldApplicator> peerDb = F::Descriptor("20", "1");
		auto app = f.Apply(check, db), peer = f.Apply(check, peerDb); if (!app || !peer) continue;
		check(f.Hit(30) == 0 && f.receiver->GetShield() == 120, "B13 pending shield damage retains deferred accounting");
		if (transition == 0) { raw->enabled.sString = "false"; app->Step(0); }
		if (transition == 1) { raw->enabled.sString = "false"; app->Step(0); app->Step(0); }
		if (transition == 2) { check(app->Step(11), "B13 expired shield reports terminal state"); app->Stop(); }
		if (transition == 3) { app->Stop(); app->Stop(); }
		check(f.receiver->GetShield() == 20 && f.Hit(25) == 5,
			"B13 disable repeated disable expiry and Stop cannot subtract a surviving peer's shield");
		peer->Stop();
		if (transition < 2)
		{
			raw->enabled.sString = "true"; app->Step(0); app->Step(0);
			check(f.receiver->GetShield() == 70 && app->GetVariable("damageToAbsorb") == 70,
				"B13 reenable restores only unconsumed shield and never duplicates aggregate contribution");
			check(f.Hit(80) == 10, "B13 reenabled shield has no phantom consumed capacity");
			f.Tick();
			check(!app->IsAlive() && f.receiver->GetShield() == 0, "B13 reenabled exhausted shield retires normally");
		}
	}
	for (const char* capacity : {"", "-10", "0", "sLife/4 + cWrapperBonus", "cMissing", "1/0", "1e39", "compiled-only"})
	{
		F f(world); auto* raw = F::Descriptor(capacity); NDb::Ptr<NDb::AbsorbShieldApplicator> db = raw;
		if (strcmp(capacity, "compiled-only") == 0) { raw->damageToAbsorb.sString = ""; raw->damageToAbsorb.compiledString = "unsupported"; }
		auto app = f.Create(db); check(app != nullptr, "B13 checked shield formulas use the actual factory"); if (!app) continue;
		const bool valid = !*capacity || strcmp(capacity, "-10") == 0 || strcmp(capacity, "0") == 0 || capacity[0] == 's';
		check(f.Activate(app) && app->IsAlive() == valid,
			"B13 malformed capacity terminates on admission while finite nonpositive capacity retains original clamp");
		const float expected = capacity[0] == 's' ? 102.f : 0.f;
		check(f.receiver->GetShield() == expected && app->GetVariable("damageToAbsorb") == expected,
			"B13 capacity evaluates real sender constants or rejects before shield mutation");
		f.Tick();
		check(app->IsAlive() == (expected > 0), "B13 zero or rejected capacity leaves no active shield after owning tick");
	}
	{
		F f(world); NDb::Ptr<NDb::AbsorbShieldApplicator> db = F::Descriptor("3e38");
		auto a = f.Apply(check, db), b = f.Apply(check, db); if (a && b)
		{
			check(a->IsEnabled() && !b->IsEnabled() && std::isfinite(f.receiver->GetShield())
				&& f.receiver->GetShield() == a->GetVariable("damageToAbsorb"),
				"B13 overflowing aggregate rejects only the new shield contribution");
			a->Stop(); b->Step(0);
			check(b->IsEnabled() && f.receiver->GetShield() == b->GetVariable("damageToAbsorb"),
				"B13 rejected aggregate can recover through the ordinary enable transition");
			auto desc = DirectDamage(); PFBaseUnitDamageEvent hit(desc, std::numeric_limits<float>::max());
			f.receiver->EventHappened(hit);
			check(b->GetVariable("damageToAbsorb") == 0 && std::isfinite(hit.damage2Deal) && hit.damage2Deal > 0,
				"B13 finite overkill cannot overflow pending shield absorption");
			f.Tick(); check(f.receiver->GetShield() == 0 && !b->IsAlive(), "B13 large exhausted shield leaves no aggregate residue");
		}
	}
	for (float invalid : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		F f(world); NDb::Ptr<NDb::AbsorbShieldApplicator> db = F::Descriptor();
		auto app = f.Apply(check, db); if (!app) continue;
		auto desc = DirectDamage(); PFBaseUnitDamageEvent event(desc, invalid);
		f.receiver->EventHappened(event);
		check(app->GetVariable("damageToAbsorb") == 100 && f.receiver->GetShield() == 100,
			"B13 malformed event damage cannot poison or increase shield capacity");
		f.Hit(10);
		check(!app->Step(invalid) && f.receiver->GetShield() == 100 && app->GetVariable("damageToAbsorb") == 90,
			"B13 invalid elapsed time cannot consume pending presentation accounting");
		check(!app->Step(0) && f.receiver->GetShield() == 90, "B13 valid tick still applies pending shield damage once");
	}
	for (bool receiverDies : {false, true}) for (bool keep : {false, true})
	{
		B8BehaviourForeignWorld owning(check); if (!owning.world) continue;
		F f(owning.world); auto* raw = F::Descriptor(); NDb::Ptr<NDb::AbsorbShieldApplicator> db = raw;
		if (keep) raw->behaviorFlags = static_cast<NDb::EBuffBehavior>(NDb::BUFFBEHAVIOR_ENABLEDONSENDERDEATH
			| (receiverDies ? NDb::BUFFBEHAVIOR_DONTSTOPONDEATH : NDb::BUFFBEHAVIOR_DONTSTOPONSENDERDEATH));
		auto app = f.Apply(check, db); if (!app) continue;
		f.Hit(10);
		(receiverDies ? f.receiver : f.sender)->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		owning.world->Step(.1f, .1f);
		check(app->IsAlive() == keep && f.receiver->GetShield() == (keep ? 90 : 0)
			&& f.instance->GetActiveApplicatorsCount() == int(keep),
			"B13 original world death cleanup honors independent shield endpoint survival flags");
		f.receiver->CleanupAfterDeath(true);
		check(!app->IsAlive() && f.receiver->GetShield() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 full cleanup also releases death-surviving shield listener and aggregate");
	}
	for (const char* capacity : {"100", "cMissing"})
	{
		F f(world); NDb::Ptr<NDb::AbsorbShieldApplicator> db = F::Descriptor(capacity), peerDb = F::Descriptor("20");
		auto peer = f.Apply(check, peerDb); if (!peer) continue;
		const Target target(f.receiver.GetPtr()); PFApplCreatePars cp(f.instance, target); cp.pDBAppl = db;
		CObj<AbsorbShieldParentB13> parent = new AbsorbShieldParentB13(cp);
		auto app = f.Create(db, parent.GetPtr()); check(app && f.Activate(app), "B13 reentrant shield parent admits a real factory-created child");
		if (!app) continue;
		check(!app->IsAlive() && parent->starts == 1 && parent->stops == 1 && f.receiver->GetShield() == 20
			&& f.instance->GetActiveApplicatorsCount() == 1 && f.Hit(25) == 5,
			"B13 start callback cancellation cannot unregister unowned shield or decrement the peer's ability count");
	}
	std::printf("B13 absorb shields checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Read only the shipped shield and its inline constants through the original XML serializer. */
static void AbsorbShieldAuthoredB13(Checks& check, PFWorld* world, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	RootFileSystem::RegisterFileSystem(files);
	NDb::Ptr<NDb::AbsorbShieldApplicator> db = new NDb::AbsorbShieldApplicator;
	auto* constants = new NDb::UnitConstantsContainer;
	NDb::Ptr<NDb::UnitConstantsContainer> ownedConstants = constants;
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Plane/status_A3_Shield.APLR.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 shipped Plane shield XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			const bool found = saver->StartChunk("StatusApplicator", 0) && saver->StartChunk("applicators", 0)
				&& saver->StartChunk("Item", 1);
			check(found, "B13 original XML reader locates Plane's inline shield child");
			if (found) saver->Add("AbsorbShieldApplicator", const_cast<NDb::AbsorbShieldApplicator*>(db.GetPtr()));
		}
	}
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Plane/Ability_A3.TALENT.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 shipped Plane talent XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			const bool found = saver->StartChunk("Talent", 0) && saver->StartChunk("constants", 0)
				&& saver->StartChunk("UnitConstantsContainer", 0) && saver->StartChunk("vars", 0);
			check(found, "B13 original XML reader locates Plane's inline ability constants");
			if (found) for (int i = 1, count = saver->CountChunks(); i <= count && i <= 128; ++i)
			{
				if (!saver->StartChunk("Item", i)) continue;
				if (saver->StartChunk("UnitConstant", 0))
				{
					auto* constant = new NDb::UnitConstant; NDb::Ptr<NDb::UnitConstant> owned = constant;
					saver->Add("name", &constant->name); saver->Add("var", &constant->var);
					if (constant->name == "A3_DamageCapacity" || constant->name == "A3_Duration") constants->vars.push_back(owned);
					saver->FinishChunk();
				}
				saver->FinishChunk();
			}
		}
	}
	check(db->damageToAbsorb.sString == "cA3_DamageCapacity" && db->lifeTime.sString == "cA3_Duration"
		&& db->formulaName == "AbsorbDamageAppl" && constants->vars.size() == 2,
		"B13 shipped descriptor and both actual capacity and duration constants load without replacement literals");
	for (bool purchased : {false, true}) for (bool intellect : {false, true})
	{
		AbsorbShieldFixtureB13 f(world); auto hero = CommandHeroB10(world);
		auto* talent = new NDb::Talent; talent->persistentId = "Plane_A3_SUP";
		talent->naftaCost = 0; talent->manaCost.sString = "0"; hero->InstallTalent(talent, purchased);
		hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(intellect ? 50 : 160);
		hero->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(intellect ? 160 : 50);
		const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->constants = ownedConstants;
		f.ability = new PFAbilityData(hero.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		f.instance = new PFAbilityInstance(f.ability, Target(f.receiver.GetPtr()), true);
		auto app = f.Apply(check, db);
		if (app)
		{
			float remaining = 0; const float capacity = purchased ? 787.f : 525.f;
			check(app->IsAlive() && app->GetRemainingLifeTime(remaining) && remaining == 5
				&& app->GetVariable("damageToAbsorb") == capacity && f.receiver->GetShield() == capacity,
				"B13 authored Plane shield uses actual purchased talent max stat damageScale and five-second duration");
			check(f.Hit(100) == 0 && app->GetVariable("damageToAbsorb") == capacity - 100,
				"B13 real shipped Plane shield absorbs through the original damage event");
			check(app->Step(5), "B13 actual Plane lifetime expires at its authored deadline"); app->Stop();
			check(f.receiver->GetShield() == 0 && f.instance->GetActiveApplicatorsCount() == 0 && f.Hit(10) == 10,
				"B13 authored shield expiry releases aggregate listener and ability ownership");
		}
		hero->Cleanup();
	}
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored Plane shields checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
