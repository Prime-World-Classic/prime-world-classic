/** Read private lifecycle state without changing production access or class layout. */
struct ApplicatorHistoryStepTagB13
{
	using Pointer = void (PFBaseUnit::*)();
	friend Pointer ApplicatorHistoryMemberB13(ApplicatorHistoryStepTagB13);
};
struct ApplicatorHistoryNamedTagB13
{
	using Pointer = PFBaseApplicator::NamedRing PFBaseUnit::*;
	friend Pointer ApplicatorHistoryMemberB13(ApplicatorHistoryNamedTagB13);
};
struct ApplicatorHistoryClockTagB13
{
	using Pointer = float PFWorld::*;
	friend Pointer ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13);
};
template<class Tag, typename Tag::Pointer member>
struct ApplicatorHistoryAccessB13
{
	friend typename Tag::Pointer ApplicatorHistoryMemberB13(Tag) { return member; }
};
template struct ApplicatorHistoryAccessB13<ApplicatorHistoryStepTagB13, &PFBaseUnit::StepApplicatorsHistory>;
template struct ApplicatorHistoryAccessB13<ApplicatorHistoryNamedTagB13, &PFBaseUnit::namedApplicators>;
template struct ApplicatorHistoryAccessB13<ApplicatorHistoryClockTagB13, &PFWorld::timeElapsed>;

static void ApplicatorHistoryStepB13(PFBaseUnit* unit)
{
	(unit->*ApplicatorHistoryMemberB13(ApplicatorHistoryStepTagB13()))();
}
static PFBaseApplicator::NamedRing& ApplicatorHistoryNamesB13(PFBaseUnit* unit)
{
	return unit->*ApplicatorHistoryMemberB13(ApplicatorHistoryNamedTagB13());
}
static int ApplicatorHistoryNamedCountB13(PFBaseUnit* unit)
{
	auto& ring = ApplicatorHistoryNamesB13(unit);
	int count = 0;
	for (auto* app = ring.first(); app != ring.last() && count < 1000; app = ring.next(app)) ++count;
	return count;
}
static std::vector<PFBaseApplicator*> ApplicatorHistoryListB13(PFBaseUnit* unit, bool sent)
{
	std::vector<PFBaseApplicator*> result;
	auto collect = [&](const CObj<PFBaseApplicator>& app) { result.push_back(app.GetPtr()); };
	if (sent) unit->ForAllSentApplicatorsInHistory(collect);
	else unit->ForAllAppliedApplicatorsInHistory(collect);
	return result;
}

/** Reuse loaded B12 descriptors/factories; explicitly drain both endpoint histories. */
struct ApplicatorHistoryFixtureB13 : ApplicatorQueryFixtureB12
{
	using ApplicatorQueryFixtureB12::ApplicatorQueryFixtureB12;
	~ApplicatorHistoryFixtureB13()
	{
		for (auto* unit : {receiver.GetPtr(), elsewhere.GetPtr(), sender.GetPtr(), other.GetPtr(),
			summon.GetPtr(), descendant.GetPtr(), otherSummon.GetPtr()})
		{
			unit->RemoveAppliedApplicators(); unit->RemoveSentApplicators();
			// Permit the regression baseline to exit cleanly after observing its stale named links.
			auto& named = ApplicatorHistoryNamesB13(unit);
			while (!named.empty()) PFBaseApplicator::NamedRing::remove(named.first());
			unit->CleanupHistoryApplicators();
		}
	}
};

/** Real duration expiry removes weak named links but retains independent history owners. */
static void ApplicatorHistoryBoundaryB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorHistoryFixtureB13 f(check, isolated.world);
	isolated.world->Step(0, 0); isolated.world->Step(10, 10);
	auto a = f.Apply(f.Stat("Short", "true", ".5"));
	auto b = f.Apply(f.Stat("Persistent"));
	check(ApplicatorHistoryNamedCountB13(f.receiver) == 2, "both actual named stat effects enter the weak ring");
	isolated.world->Step(.5f, .5f);
	check(!a->IsAlive() && b->IsAlive() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"world expiry removes the real stat contribution, not its live sibling");
	check(ApplicatorHistoryNamedCountB13(f.receiver) == 1,
		"stopped named effect is physically unlinked, not merely hidden by query filtering");
	check(a->GetMarkerTime() == 10.5f, "memorized duration completion records current world time, not creation zero");
	check(ApplicatorHistoryListB13(f.receiver, false) == std::vector<PFBaseApplicator*>{a.GetPtr()}
		&& ApplicatorHistoryListB13(f.sender, true) == std::vector<PFBaseApplicator*>{a.GetPtr()},
		"natural expiry retains one applied and one sent history link");
	a->Stop(); a->Stop();
	check(a->IsLinkedToAppliedRing() && a->IsLinkedToSentRing() && f.casts[0]->GetActiveApplicatorsCount() == 1,
		"repeated Stop preserves memorized ownership and never decrements its live sibling");
	// Exact original float comparison: equality is retained, only greater than 35 expires.
	a->SetMarkerTime(10.5f);
	isolated.world->Step(35, 35);
	ApplicatorHistoryStepB13(f.receiver); ApplicatorHistoryStepB13(f.sender);
	check(a->IsLinkedToAppliedRing() && a->IsLinkedToSentRing(), "both histories retain the exact 35-second boundary");
	isolated.world->Step(.125f, .125f);
	ApplicatorHistoryStepB13(f.receiver);
	check(!a->IsLinkedToAppliedRing(), "applied history expires at the first strictly later clock");
	// With the parent hook installed, world stepping already expires both endpoints.
	ApplicatorHistoryStepB13(f.sender);
	check(!a->IsLinkedToSentRing(), "sent history independently expires at the same original threshold");
	check(ApplicatorHistoryNamedCountB13(f.receiver) == 1 && b->IsAlive()
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23 && f.casts[0]->GetActiveApplicatorsCount() == 1,
		"history pruning neither repeats Stop nor touches active named/stat/ability ownership");
}

/** Keep chronological front-prefix semantics, including independent endpoint cleanup. */
static void ApplicatorHistoryOrderB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorHistoryFixtureB13 f(check, isolated.world);
	isolated.world->Step(0, 0); isolated.world->Step(100, 100);
	auto a = f.Apply(f.Stat("First")), b = f.Apply(f.Stat("Second")), c = f.Apply(f.Stat("Third"));
	for (auto app : {a, b, c}) { app->Stop(); MemorizeApplicator(app); }
	a->SetMarkerTime(64); b->SetMarkerTime(65); c->SetMarkerTime(66);
	ApplicatorHistoryStepB13(f.receiver);
	check(ApplicatorHistoryListB13(f.receiver, false) == std::vector<PFBaseApplicator*>{b.GetPtr(), c.GetPtr()},
		"applied history drops only the strictly expired chronological prefix in insertion order");
	check(ApplicatorHistoryListB13(f.sender, true) == std::vector<PFBaseApplicator*>{a.GetPtr(), b.GetPtr(), c.GetPtr()},
		"pruning one endpoint never steals the other endpoint history link");
	ApplicatorHistoryStepB13(f.sender);
	check(ApplicatorHistoryListB13(f.sender, true) == std::vector<PFBaseApplicator*>{b.GetPtr(), c.GetPtr()},
		"sent history applies the same independent prefix policy");
	// A malformed out-of-order list must not gain a new scan-all retention policy.
	b->SetMarkerTime(90); c->SetMarkerTime(1);
	ApplicatorHistoryStepB13(f.receiver); ApplicatorHistoryStepB13(f.sender);
	check(b->IsLinkedToAppliedRing() && c->IsLinkedToAppliedRing()
		&& b->IsLinkedToSentRing() && c->IsLinkedToSentRing(),
		"a retained first entry terminates scanning even when a later marker is older");
	f.receiver->CleanupHistoryApplicators();
	check(ApplicatorHistoryListB13(f.receiver, false).empty() && b->IsLinkedToSentRing() && c->IsLinkedToSentRing(),
		"explicit cleanup discards all local history without touching peer links");
	f.sender->CleanupHistoryApplicators(); f.sender->CleanupHistoryApplicators();
	check(!b->IsLinkedToSentRing() && !c->IsLinkedToSentRing(), "repeated cleanup leaves the sent ring empty");
}

/** Original clock selection prefers valid sender identity, then receiver, never applicator world. */
static void ApplicatorHistoryClocksB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check), foreign(check); if (!isolated.world || !foreign.world) return;
	ApplicatorHistoryFixtureB13 f(check, isolated.world);
	isolated.world->Step(0, 0); isolated.world->Step(20, 20);
	foreign.world->Step(0, 0); foreign.world->Step(70, 70);
	auto ownerless = f.Apply(f.Stat("Ownerless"), -1);
	ownerless->Stop(); MemorizeApplicator(ownerless);
	check(ownerless->GetMarkerTime() == 20 && !ownerless->IsLinkedToSentRing(),
		"ownerless effect uses receiver clock and records only applied history");
	CObj<PFBaseUnit> foreignSender = Unit(foreign.world, NDb::FACTION_FREEZE, 40);
	CObj<PFAbilityData> data = new PFAbilityData(foreignSender.GetPtr(), f.db, NDb::ABILITYTYPEID_SPECIAL, false, false);
	CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(f.receiver.GetPtr()), false);
	const Target target(f.receiver.GetPtr());
	PFApplCreatePars pars(cast, target); pars.pWorld = isolated.world; pars.pDBAppl = f.Stat("CrossWorld");
	CObj<PFBaseApplicator> app = CreateApplicator(pars);
	check(app && app->GetReceiver() == f.receiver.GetPtr(),
		"actual factory constructs the cross-world history fixture without bypassing cast eligibility");
	if (app)
	{
		app->Stop(); MemorizeApplicator(app); f.effects.push_back(app);
		check(app->GetMarkerTime() == 70, "sender clock takes precedence over receiver clock exactly as Windows");
		ApplicatorHistoryStepB13(f.receiver);
		check(app->IsLinkedToAppliedRing(), "future sender marker is not prematurely expired on receiver clock");
	}
	foreignSender->CleanupHistoryApplicators(); foreignSender->Cleanup();
	CObj<PFBaseUnit> noWorld = Unit(nullptr, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseApplicator> noEndpoints = new PFBaseApplicator;
	noEndpoints->SetMarkerTime(12); MemorizeApplicator(noEndpoints);
	check(noEndpoints->GetMarkerTime() == 12 && !noEndpoints->IsLinkedToAppliedRing() && !noEndpoints->IsLinkedToSentRing(),
		"no endpoint world leaves the marker unchanged and fabricates no history");
	ApplicatorHistoryStepB13(noWorld); noWorld->Cleanup();
	float& clock = isolated.world->*ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13());
	for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.f})
	{
		clock = bad; ownerless->SetMarkerTime(7); MemorizeApplicator(ownerless);
		check(ownerless->GetMarkerTime() == 7, "invalid world clock cannot poison an existing history marker");
		ApplicatorHistoryStepB13(f.receiver);
		check(ownerless->IsLinkedToAppliedRing(), "invalid world clock cannot trigger history release");
	}
	clock = 100;
	ownerless->SetMarkerTime(std::numeric_limits<float>::quiet_NaN());
	ApplicatorHistoryStepB13(f.receiver);
	check(ownerless->IsLinkedToAppliedRing(), "non-finite front marker does not invent an expiry time");
	clock = 20;
}

/** Removal/death differs from natural completion: discard, do not append history. */
static void ApplicatorHistoryRemovalB13(Checks& check)
{
	for (int mode = 0; mode != 4; ++mode)
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		ApplicatorHistoryFixtureB13 f(check, isolated.world);
		auto first = f.Apply(f.Stat("Removed"));
		auto surviveDb = f.Stat("Survivor");
		const_cast<NDb::StatModApplicator*>(surviveDb.GetPtr())->behaviorFlags = static_cast<NDb::EBuffBehavior>(
			NDb::BUFFBEHAVIOR_DONTSTOPONDEATH | NDb::BUFFBEHAVIOR_DONTSTOPONSENDERDEATH);
		auto second = f.Apply(surviveDb);
		if (mode == 0) f.receiver->RemoveAppliedApplicators();
		if (mode == 1) f.sender->RemoveSentApplicators();
		if (mode == 2) f.receiver->RemoveAppliedApplicatorsOnDeath();
		if (mode == 3) f.sender->RemoveSentApplicatorsOnDeath();
		const int remaining = mode >= 2 ? 1 : 0;
		check(!first->IsAlive() && second->IsAlive() == (remaining != 0)
			&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 + 3 * remaining,
			"actual bulk/death removal obeys authored receiver/sender survival flags");
		check(ApplicatorHistoryNamedCountB13(f.receiver) == remaining,
			"bulk/death removal physically unlinks stopped names, retaining only exempt live effects");
		check(ApplicatorHistoryListB13(f.receiver, false).empty() && ApplicatorHistoryListB13(f.sender, true).empty(),
			"Windows bulk/death removal discards effects rather than memorizing them");
		check(first->GetStopReason() == (mode == 2 ? APPL_STOP_REASON_ONDEATH : APPL_STOP_REASON_NONE),
			"only receiver-death removal stamps the original death stop reason");
		check(f.casts[0]->GetActiveApplicatorsCount() == remaining, "bulk/death cleanup balances only stopped ownership");
	}
}

/** Actual status child retirement unlinks every stopped name while reviving the blocked status. */
static void ApplicatorHistoryStatusesB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorHistoryFixtureB13 f(check, isolated.world);
	std::vector<CObj<PFBaseApplicator>> statuses;
	for (int power : {1, 10})
	{
		auto* db = new ApplicatorQueryDbB12<NDb::StatusApplicator>("/Mock/HistoryStatus.xdb");
		db->enabled.sString = "true"; db->startCondition.sString = "true"; db->lifeTime.sString = "-1";
		db->formulaName = "Status"; db->power = power; db->applicators.push_back(f.Stat("Child"));
		// A non-stat child selects original power conflict rather than comparable-stat merging.
		auto* flags = new ApplicatorQueryDbB12<NDb::FlagsApplicator>("/Mock/HistoryStatusFlag.xdb");
		flags->flag = NDb::UNITFLAG_FORBIDMOVE; flags->lifeTime.sString = "-1"; db->applicators.push_back(flags);
		NDb::Ptr<NDb::StatusApplicator> holder = db; statuses.push_back(f.Apply(holder));
	}
	check(statuses[0]->IsBlocked() && statuses[1]->IsEnabled() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"real status conflict blocks the weaker status and leaves one child modifier");
	check(ApplicatorHistoryNamedCountB13(f.receiver) == 3,
		"blocked status retains its own name but its stopped child is physically removed");
	statuses[1]->Stop(); MemorizeApplicator(statuses[1]);
	check(statuses[0]->IsEnabled() && !statuses[0]->IsBlocked()
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23 && ApplicatorHistoryNamedCountB13(f.receiver) == 2,
		"winner stop removes its name and child, then revives exactly one weak-status child");
	statuses[0]->Stop(); MemorizeApplicator(statuses[0]);
	check(ApplicatorHistoryNamedCountB13(f.receiver) == 0 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20
		&& f.casts[0]->GetActiveApplicatorsCount() == 0, "all status and child names retire with balanced effects");
	check(ApplicatorHistoryListB13(f.receiver, false).size() == 8,
		"natural parent/child retirement retains each distinct original status and child history entry");
	for (auto* app : ApplicatorHistoryListB13(f.receiver, false)) app->SetMarkerTime(0);
	isolated.world->Step(0, 0); isolated.world->Step(36, 36);
	ApplicatorHistoryStepB13(f.receiver); ApplicatorHistoryStepB13(f.sender);
	check(ApplicatorHistoryListB13(f.receiver, false).empty() && ApplicatorHistoryListB13(f.sender, true).empty(),
		"expired real status and child histories release both owning rings without callbacks restarting effects");
}

/** Engine parent notifications can reenter Stop, remove siblings, and release external owners. */
static void ApplicatorHistoryReentrantB13(Checks& check)
{
	for (int mode = 0; mode != 3; ++mode)
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		ApplicatorHistoryFixtureB13 f(check, isolated.world);
		const Target target(f.receiver.GetPtr());
		PFApplCreatePars pars(f.casts[0], target); pars.pWorld = isolated.world; pars.pDBAppl = f.Stat("Reentrant");
		CObj<TerminalParentB12> parent = new TerminalParentB12(pars); pars.pParent = parent;
		CObj<PFBaseApplicator> first = CreateApplicator(pars), second = CreateApplicator(pars);
		check(first && second && ActivateApplicator(first, f.casts[0]) && ActivateApplicator(second, f.casts[0]),
			"two actual stat factory effects own engine parent notifications and named entries");
		if (!first || !second) continue;
#if defined(PW_APPLICATOR_HISTORY_DIAGNOSTIC_OWNER)
		// Baseline counting keeps a diagnostic owner so stale weak links cannot corrupt teardown.
		CObj<PFBaseApplicator> observer(first);
#endif
		PFBaseApplicator* original = first.GetPtr();
		bool reentered = false;
		parent->action = [&](PFBaseApplicator& app, NDb::EParentNotification note) {
			if (note != NDb::PARENTNOTIFICATION_STOP || &app != original || reentered) return;
			reentered = true;
			app.Stop();
			if (mode == 1) f.receiver->RemoveAppliedApplicators();
			if (mode == 2) f.sender->RemoveSentApplicators();
			first = nullptr;
		};
		// Normal coverage drops every external owner during the callback.
		original->Stop();
		check(reentered && parent->stops == (mode == 0 ? 1 : 2),
			"reentrant Stop and sibling ring removal notify exactly once per actual effect");
		check(ApplicatorHistoryNamedCountB13(f.receiver) == (mode == 0 ? 1 : 0)
			&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == (mode == 0 ? 23 : 20),
			"callback removal leaves only the live sibling name and stat contribution");
		check(f.casts[0]->GetActiveApplicatorsCount() == (mode == 0 ? 1 : 0),
			"callback-released ownership does not consume or leak sibling ability counters");
		parent->action = {}; second->Stop();
#if defined(PW_APPLICATOR_HISTORY_DIAGNOSTIC_OWNER)
		PFBaseApplicator::NamedRing::safeRemove(observer.GetPtr());
		PFBaseApplicator::NamedRing::safeRemove(second.GetPtr());
#endif
	}
}

/** The final history link owns real effects; weak observations do not keep contents alive. */
static void ApplicatorHistoryLastOwnerB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorHistoryFixtureB13 f(check, isolated.world);
	isolated.world->Step(0, 0); isolated.world->Step(100, 100);
	auto app = f.Apply(f.Stat("LastOwner"));
	app->Stop(); MemorizeApplicator(app); app->SetMarkerTime(0);
	CPtr<PFBaseApplicator> observed(app.GetPtr());
	app = nullptr; f.effects.clear();
	ApplicatorHistoryStepB13(f.receiver);
	check(IsValid(observed), "sent history keeps actual effect contents alive after applied expiry");
	check(!observed->IsLinkedToAppliedRing() && observed->IsLinkedToSentRing(),
		"only one actual strong history owner remains after the first endpoint step");
	ApplicatorHistoryStepB13(f.sender);
	check(!IsValid(observed), "last history expiry releases real effect contents without a fixture CObj owner");
	check(f.casts[0]->GetActiveApplicatorsCount() == 0 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
		"last-owner disposal cannot reapply or repeat an already stopped effect");
}

/** Only destruction notification is instrumented; membership and final ownership are real rings. */
struct ApplicatorHistoryFinalizerB13 : PFBaseApplicator
{
	OBJECT_METHODS(PFBaseApplicator::typeId, ApplicatorHistoryFinalizerB13);
public:
	ApplicatorHistoryFinalizerB13() = default;
	std::function<void()> onDestroy;
	explicit ApplicatorHistoryFinalizerB13(const PFApplCreatePars& pars) : PFBaseApplicator(pars) {}
	void OnDestroyContents() override
	{
		if (onDestroy) onDestroy();
		PFBaseApplicator::OnDestroyContents();
	}
};

static void ApplicatorHistoryCleanupReentryB13(Checks& check)
{
	for (bool explicitCleanup : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		ApplicatorHistoryFixtureB13 f(check, isolated.world);
		isolated.world->Step(0, 0); isolated.world->Step(100, 100);
		const Target target(f.receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target); pars.pWorld = isolated.world; pars.pDBAppl = f.Stat("");
		CObj<ApplicatorHistoryFinalizerB13> first = new ApplicatorHistoryFinalizerB13(pars);
		CObj<PFBaseApplicator> second = CreateApplicator(pars);
		check(first->Init() && second, "reentrant finalizer and actual sibling resolve the same real receiver");
		first->Stop(); second->Stop();
		MemorizeApplicator(CObj<PFBaseApplicator>(first.GetPtr())); MemorizeApplicator(second);
		first->SetMarkerTime(0); second->SetMarkerTime(0);
		int callbacks = 0;
		first->onDestroy = [&] { ++callbacks; f.receiver->CleanupHistoryApplicators(); };
		CPtr<PFBaseApplicator> firstWeak(first.GetPtr()), secondWeak(second.GetPtr());
		first = nullptr; second = nullptr;
		if (explicitCleanup) f.receiver->CleanupHistoryApplicators();
		else ApplicatorHistoryStepB13(f.receiver);
		check(callbacks == 1 && !IsValid(firstWeak) && !IsValid(secondWeak)
			&& ApplicatorHistoryListB13(f.receiver, false).empty(),
			"final history owner callback can clear its sibling ring without invalid traversal or duplicate destruction");
		// Baseline stub leaves both owners alive; disarm references before stack teardown.
		if (IsValid(firstWeak)) static_cast<ApplicatorHistoryFinalizerB13*>(firstWeak.GetPtr())->onDestroy = {};
	}
}

/** The caller enables this only after installing the separately supplied parent Step hook. */
static void ApplicatorHistoryWorldHookB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorHistoryFixtureB13 f(check, isolated.world);
	auto app = f.Apply(f.Stat("WorldHook", "true", ".5"));
	isolated.world->Step(0, 0); isolated.world->Step(.5f, .5f);
	check(!app->IsAlive() && app->GetMarkerTime() == .5f, "production world tick memorizes the actual completion time");
	isolated.world->Step(35, 35);
	check(app->IsLinkedToAppliedRing() && app->IsLinkedToSentRing(), "production Step hook retains equality");
	isolated.world->Step(.125f, .125f);
	check(!app->IsLinkedToAppliedRing() && !app->IsLinkedToSentRing(),
		"production unit Step hook expires both endpoint histories without direct test calls");
}

/** Isolated mock worlds avoid advancing the inherited probe's accumulated simulation. */
static void ApplicatorHistoryB13(Checks& check, bool stepHookInstalled = true)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	auto* logic = new NDb::SessionLogicRoot; logic->aiLogic = new NDb::AILogicParameters;
	logic->unitLogicParameters = new NDb::UnitsLogicParameters;
	auto* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	ApplicatorHistoryBoundaryB13(check);
	ApplicatorHistoryOrderB13(check);
	ApplicatorHistoryClocksB13(check);
	ApplicatorHistoryRemovalB13(check);
	ApplicatorHistoryStatusesB13(check);
	ApplicatorHistoryReentrantB13(check);
	ApplicatorHistoryLastOwnerB13(check);
	ApplicatorHistoryCleanupReentryB13(check);
	if (stepHookInstalled) ApplicatorHistoryWorldHookB13(check);
	NDb::SessionRoot::InitRoot(saved);
}
