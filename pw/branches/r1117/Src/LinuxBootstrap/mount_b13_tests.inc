/** Original mount descriptor; the rider is the receiver and its caster is the carrier. */
static NDb::Ptr<NDb::MountApplicator> MountDescriptorB13(bool control, const char* lifetime = "-1")
{
	auto* db = new NDb::MountApplicator; db->enabled.sString = "true"; db->lifeTime.sString = lifetime;
	db->mountTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER; db->controlMount = control;
	db->formulaName = "NativeMount"; return db;
}

/** Real mounted movement and original hero bookkeeping must retire together, without consuming another root. */
static void MountLifecycleB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool control : {false, true}) for (bool rooted : {false, true}) for (bool wrapped : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = MountDescriptorB13(control);
		NDb::Ptr<NDb::BaseApplicator> outer = db.GetPtr();
		if (wrapped) outer = new WrapperFactoryDbB12("/Mock/MountWrapper.xdb", outer);
		if (rooted) f.receiver->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		check(f.sender->TeleportTo(CVec2(12, 5)), "B13 carrier starts at a separate valid world position");
		auto app = f.Create(outer); check(app && f.Activate(app), "B13 original mount descriptor and wrapper create a real native effect"); if (!app) continue;
		check(app->IsAlive() && f.receiver->GetMount() == f.sender && f.sender->GetAttachUnit() == f.receiver
			&& f.sender->IsMount() && f.receiver->IsMounted(), "B13 mounting owns matching hero, carrier and movement links");
		check(f.receiver->CheckFlag(NDb::UNITFLAG_FORBIDMOVE) && f.receiver->ControlsMount() == control
			&& f.receiver->CanControlMount() == control, "B13 original mount adds one passenger movement restriction and authored control permission");
		check(f.receiver->GetPos() == f.sender->GetPos() && f.sender->TeleportTo(CVec2(18, 8)),
			"B13 mount starts at its carrier and the carrier can teleport");
		isolated.world->Step(.125f, .125f);
		check(f.receiver->GetPos() == f.sender->GetPos(), "B13 mounted passenger follows carrier teleport on the original next movement tick");
		const CVec2 carrierBefore = f.sender->GetPos(); f.receiver->Move(CVec2(25, 8));
		for (int tick = 0; tick < 4; ++tick) isolated.world->Step(.125f, .125f);
		check((f.sender->GetPos() != carrierBefore) == control && f.receiver->GetPos() == f.sender->GetPos(),
			"B13 only a controlling rider routes actual movement to the carrier's original FSM");
		app->Stop(); app->Stop();
		check(!f.receiver->GetMount() && !f.receiver->IsMounted() && !f.receiver->ControlsMount()
			&& !f.sender->GetAttachUnit() && !f.sender->IsMount(), "B13 repeated stop releases all mount links exactly once");
		check(f.receiver->CheckFlag(NDb::UNITFLAG_FORBIDMOVE) == rooted && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 dismount balances its own root and cast count without removing an independent restriction");
		const CVec2 position = f.receiver->GetPos(); f.sender->TeleportTo(CVec2(22, 8));
		check(f.receiver->GetPos() == position, "B13 dismounted hero no longer follows its former carrier");
	}
	std::printf("B13 mount lifecycle checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Rejected duplicate mounts must not let their unconditional Stop tear down a successful sibling. */
static void MountRejectionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	NaftaFixtureB13 f(isolated.world); auto db = MountDescriptorB13(true);
	auto first = f.Create(db); check(first && f.Activate(first), "B13 duplicate fixture owns a real active mount"); if (!first) return;
	for (int i = 0; i < 3; ++i)
	{
		auto duplicate = f.Create(db); check(duplicate && f.Activate(duplicate) && !duplicate->IsAlive(), "B13 duplicate mount rejects through ordinary instant retirement");
		check(first->IsAlive() && f.receiver->GetMount() == f.sender && f.receiver->IsMounted()
			&& f.sender->GetAttachUnit() == f.receiver && f.sender->IsMount() && f.instance->GetActiveApplicatorsCount() == 1,
			"B13 failed duplicate cannot dismount or release the existing sibling's ownership");
	}
	first->Stop();
	std::printf("B13 mount rejection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Actual world ticks, not a mock timer, release riders on expiry and either endpoint's death. */
static void MountRetirementB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int cause = 0; cause < 6; ++cause)
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = MountDescriptorB13(false, cause == 0 ? "sLife/400" : "-1");
		auto app = f.Create(db); check(app && f.Activate(app), "B13 mount retirement begins through original factory and live rings"); if (!app) continue;
		if (cause == 0)
		{
			isolated.world->Step(.5f, .5f); check(app->IsAlive() && f.receiver->IsMounted(), "B13 mount retains its passenger before its real buff duration expires");
		}
		else if (cause == 1) f.sender->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		else if (cause == 2) f.receiver->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		else if (cause == 3) f.sender->DetachUnit();
		else if (cause == 4) f.receiver->DismountSpecial();
		else f.receiver->OnUnmountSpecial();
		isolated.world->Step(.5f, .5f);
		check(!app->IsAlive() && !f.receiver->GetMount() && !f.sender->GetAttachUnit() && !f.sender->IsMount()
			&& !f.receiver->IsMounted() && !f.receiver->CheckFlag(NDb::UNITFLAG_FORBIDMOVE),
			"B13 timer death or external detachment retires both mount endpoints and the owned root");
		check(f.instance->GetActiveApplicatorsCount() == 0, "B13 mount retirement balances its original ability ownership");
	}
	std::printf("B13 mount retirement checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Death notifications are broadcasts: another victim must not release these endpoints. */
static void MountEventsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool carrier : {false, true}) for (bool minigame : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = MountDescriptorB13(false);
		auto app = f.Create(db); check(app && f.Activate(app), "B13 event fixture mounts through the production factory"); if (!app) continue;
		auto other = B9MaleHero(isolated.world, NDb::FACTION_BURN);
		PFBaseUnit* endpoint = carrier ? f.sender.GetPtr() : f.receiver.GetPtr();
		endpoint->EventHappened(PFBaseUnitEventUnitDeath(other.GetPtr(), nullptr, nullptr));
		check(app->IsAlive() && f.receiver->IsMounted(), "B13 unrelated broadcast victim cannot dismount this hero");
		endpoint->EventHappened(PFBaseUnitMinigameEvent(NDb::BASEUNITEVENT_MINIGAMESTARTED, other.GetPtr()));
		check(app->IsAlive() && f.receiver->IsMounted(), "B13 unrelated minigame unit cannot release mount ownership");
		if (minigame) endpoint->EventHappened(PFBaseUnitMinigameEvent(NDb::BASEUNITEVENT_MINIGAMESTARTED, endpoint));
		else endpoint->EventHappened(PFBaseUnitEvent(NDb::BASEUNITEVENT_ISOLATE));
		check(!app->IsAlive() && !f.receiver->IsMounted() && !f.receiver->GetMount() && !f.sender->IsMount()
			&& f.instance->GetActiveApplicatorsCount() == 0, "B13 actual endpoint isolation or minigame entry releases the rider immediately");
		other->Cleanup();
	}
	std::printf("B13 mount events checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Observer callbacks may cancel start or install a replacement while the old mount stops. */
struct MountParentB13 : PFBaseApplicator
{
	OBJECT_METHODS(PFBaseApplicator::typeId, MountParentB13);
public:
	MountParentB13() = default;
	explicit MountParentB13(const PFApplCreatePars& cp) : PFBaseApplicator(cp) {}
	std::function<void(PFBaseApplicator&, NDb::EParentNotification)> action;
	int starts = 0, stops = 0;
	void OnNotification(PFBaseApplicator& app, NDb::EParentNotification note) override
	{
		starts += int(note == NDb::PARENTNOTIFICATION_START); stops += int(note == NDb::PARENTNOTIFICATION_STOP);
		if (action) action(app, note);
	}
};

/** Real parent notifications must observe released old links and balanced counts before reentrant acquisition. */
static void MountCallbacksB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool cancelStart : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = MountDescriptorB13(true);
		const Target target(f.receiver.GetPtr()); PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db;
		CObj<MountParentB13> parent = new MountParentB13(pars); CObj<PFBaseApplicator> replacement;
		parent->action = [&](PFBaseApplicator& app, NDb::EParentNotification note) {
			if (cancelStart && note == NDb::PARENTNOTIFICATION_START) app.Stop();
			if (!cancelStart && note == NDb::PARENTNOTIFICATION_STOP)
			{
				replacement = f.Create(db); if (replacement) f.Activate(replacement);
			}
		};
		auto app = f.Create(db, parent.GetPtr()); check(app && f.Activate(app), "B13 parent receives actual mount activation notifications"); if (!app) continue;
		app->Stop(); app->Stop();
		check(parent->starts == 1 && parent->stops == 1 && !app->IsAlive(), "B13 mount cancellation and repeated stop balance exactly one parent notification pair");
		check(f.receiver->IsMounted() == !cancelStart && f.sender->IsMount() == !cancelStart
			&& f.receiver->CheckFlag(NDb::UNITFLAG_FORBIDMOVE) == !cancelStart
			&& f.instance->GetActiveApplicatorsCount() == int(!cancelStart),
			"B13 start cancellation owns no links; stop callback replacement survives repeated old cleanup");
		if (replacement) replacement->Stop();
		check(!f.receiver->IsMounted() && !f.receiver->CheckFlag(NDb::UNITFLAG_FORBIDMOVE), "B13 replacement mount can release its independent root normally");
	}
	std::printf("B13 mount callbacks checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Bad endpoints, unavailable conditions and capacity cannot steal another passenger or introduce a cross-world link. */
static void MountAdmissionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int mode = 0; mode < 9; ++mode)
	{
		B8BehaviourForeignWorld isolated(check), foreign(check); if (!isolated.world || !foreign.world) continue;
		NaftaFixtureB13 f(isolated.world); auto other = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
		auto outsider = B9MaleHero(foreign.world, NDb::FACTION_FREEZE);
		auto moving = B8BehaviourMoving(isolated.world, NDb::FACTION_FREEZE);
		auto stationary = Unit(isolated.world, NDb::FACTION_FREEZE, 20);
		auto db = MountDescriptorB13(false); auto* raw = const_cast<NDb::MountApplicator*>(db.GetPtr());
		PFBaseUnit* receiver = f.receiver.GetPtr(); PFBaseUnit* caster = f.sender.GetPtr();
		if (mode == 0) raw->mountTarget = NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET;
		if (mode == 1) caster = outsider.GetPtr();
		if (mode == 2) f.sender->AttachUnit(other.GetPtr());
		if (mode == 3) receiver = moving.GetPtr();
		if (mode == 4) caster = stationary.GetPtr();
		if (mode == 5) raw->enabled.sString = "false";
		if (mode == 6) raw->enabled.sString = "cMissing";
		if (mode == 7) raw->lifeTime.sString = "1/0";
		if (mode == 8) raw->lifeTime.sString = "cMissing";
		f.ability = new PFAbilityData(caster, f.abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
		const Target target(receiver); f.instance = new PFAbilityInstance(f.ability, target, true);
		PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db; auto app = CreateApplicator(pars);
		if (app) { f.Activate(app); app->Stop(); }
		check(!app || !app->IsAlive(), "B13 invalid mount endpoint or scalar retires without an active effect");
		check(!f.receiver->GetMount() && !f.receiver->IsMounted() && !f.receiver->CheckFlag(NDb::UNITFLAG_FORBIDMOVE)
			&& !f.sender->IsMount() && !outsider->GetAttachUnit() && !stationary->IsMount()
			&& f.instance->GetActiveApplicatorsCount() == 0, "B13 rejected mount cannot acquire flags, controls, cross-world links or cast ownership");
		check(mode == 2 ? f.sender->GetAttachUnit() == other && other->IsMounted() : !f.sender->GetAttachUnit(),
			"B13 occupied carrier preserves its original passenger after rejected mount cleanup");
		f.sender->DetachUnit(); other->Cleanup(); outsider->Cleanup(); moving->Cleanup(); stationary->Cleanup();
	}
	std::printf("B13 mount admission checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** A carrier can be remounted before the detached old applicator ticks; old teardown must not touch the new rider. */
static void MountReplacementB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	NaftaFixtureB13 f(isolated.world); auto db = MountDescriptorB13(true);
	auto old = f.Create(db); check(old && f.Activate(old), "B13 remount fixture begins with the actual old passenger"); if (!old) return;
	f.receiver->DismountSpecial(); auto rider = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
	const Target target(rider.GetPtr()); CObj<PFAbilityInstance> instance = new PFAbilityInstance(f.ability, target, true);
	PFApplCreatePars pars(instance, target); pars.pDBAppl = db; auto replacement = CreateApplicator(pars);
	check(replacement && ActivateApplicator(replacement, instance), "B13 second rider acquires the released carrier before old effect retirement");
	old->Stop(); old->Stop();
	check(replacement && replacement->IsAlive() && rider->GetMount() == f.sender && f.sender->GetAttachUnit() == rider
		&& f.sender->IsMount() && rider->ControlsMount() && rider->CheckFlag(NDb::UNITFLAG_FORBIDMOVE)
		&& instance->GetActiveApplicatorsCount() == 1 && f.instance->GetActiveApplicatorsCount() == 0,
		"B13 stale old cleanup preserves the replacement's carrier marker, movement, control, root and cast");
	if (replacement) replacement->Stop();
	check(!rider->IsMounted() && !f.sender->IsMount() && !rider->GetMount() && !f.sender->GetAttachUnit()
		&& instance->GetActiveApplicatorsCount() == 0, "B13 replacement owns and releases its own attachment independently");
	rider->Cleanup();
	std::printf("B13 mount replacement checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Run shipped Fairy, Night and Frogenglut mount leaves with their unchanged target routing and control metadata. */
static void MountAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		struct Entry { const char* path; const char* id; bool reversed; bool control; };
		for (const Entry entry : {
			Entry{"/Items/Talents/Class/Fairy/status_A3_Symbiot.APLR.xdb", "C66846A2-68A6-4D2F-8ABE-88866E81ECBA", true, false},
			{"/Items/Talents/Class/Fairy/status_A3_Parasyte.APLR.xdb", "112A4D31-3724-48F0-B80D-D532B606A223", true, false},
			{"/Items/Talents/Class/Night/Ability4.TALENT.xdb", "CD4ECB66-F912-4382-932E-99441C5D7774", true, true},
			{"/Items/Talents/Class/Frogenglut/Ability4_2.TALENT.xdb", "53AC8DA9-7F20-4965-85C3-1D9B366FE3D1", false, false}})
		{
			const auto db = NDb::Get<NDb::MountApplicator>(NDb::DBID(entry.path, entry.id));
			check(db && db->controlMount == entry.control, "B13 original mount leaf loads with its authored control permission"); if (!db) continue;
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			NaftaFixtureB13 f(isolated.world); auto app = f.Create(db);
			check(app && f.Activate(app), "B13 shipped inline mount leaf activates through the actual native factory"); if (!app) continue;
			PFBaseHero* rider = entry.reversed ? f.sender.GetPtr() : f.receiver.GetPtr();
			PFBaseHero* carrier = entry.reversed ? f.receiver.GetPtr() : f.sender.GetPtr();
			check(app->IsAlive() && app->GetReceiver() == rider && rider->GetMount() == carrier
				&& carrier->GetAttachUnit() == rider && rider->ControlsMount() == entry.control,
				"B13 authored AbilityOwner AbilityTarget and ApplicatorTarget routing keep rider and carrier distinct");
			isolated.world->Step(2, 2); check(app->IsAlive(), "B13 original negative mount duration remains active until external teardown");
			app->Stop();
			check(!rider->IsMounted() && !rider->GetMount() && !rider->CheckFlag(NDb::UNITFLAG_FORBIDMOVE)
				&& !carrier->GetAttachUnit() && !carrier->IsMount() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 original mount leaf releases actual movement, flags and ability ownership");
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored mount checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
