/** Observe notifications while leaving activation, resource effects and ownership in the engine. */
struct TerminalParentB12 : PFBaseApplicator
{
	int starts = 0, stops = 0;
	std::function<void(PFBaseApplicator&, NDb::EParentNotification)> action;
	explicit TerminalParentB12(const PFApplCreatePars& pars) : PFBaseApplicator(pars) {}
	void OnNotification(PFBaseApplicator& app, NDb::EParentNotification note) override
	{
		if (note == NDb::PARENTNOTIFICATION_START) ++starts;
		if (note == NDb::PARENTNOTIFICATION_STOP) ++stops;
		if (action) action(app, note);
	}
};

/** Only expose existing lifecycle hooks; stat insertion/removal remains production code. */
struct TerminalStatB12 : StatBuffProbeB12
{
	using StatBuffProbeB12::StatBuffProbeB12;
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
	bool stopOnEnable = false;
	void Enable() override
	{
		StatBuffProbeB12::Enable();
		if (stopOnEnable) Finish();
	}
};

/** Repeated and reentrant cleanup must not consume a sibling's live ability ownership. */
static void ApplicatorTerminalB12(Checks& check, PFWorld* world)
{
	for (int callback = 0; callback < 3; ++callback)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_FREEZE, 20);
		NDb::Ptr<NDb::Ability> dbAbility = new NDb::Ability;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), dbAbility, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), true);
		NDb::StatModApplicator* db = new NDb::StatModApplicator;
		db->modifier.stat = NDb::STAT_STRENGTH;
		db->modifier.multValue.sString = "1"; db->modifier.addValue.sString = "3"; db->lifeTime.sString = "-1";
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(cast, target); pars.pDBAppl = db;
		CObj<TerminalParentB12> parent = new TerminalParentB12(pars);
		pars.pParent = parent;
		CObj<TerminalStatB12> first = new TerminalStatB12(pars), second = new TerminalStatB12(pars);
		check(first->Initialize() && second->Initialize() &&
			ActivateApplicator(CObj<PFBaseApplicator>(first.GetPtr()), cast) &&
			ActivateApplicator(CObj<PFBaseApplicator>(second.GetPtr()), cast),
			"two real persistent stat effects activate on one ability");
		check(cast->GetActiveApplicatorsCount() == 2 && parent->starts == 2 &&
			receiver->GetStatValue(NDb::STAT_STRENGTH) == 26, "each active effect owns one counter and one stat modifier");
		bool reentered = false;
		parent->action = [&](PFBaseApplicator& app, NDb::EParentNotification note) {
			if (note != NDb::PARENTNOTIFICATION_STOP || &app != first.GetPtr() || reentered) return;
			reentered = true;
			if (callback == 1) first->Finish();
			if (callback == 2) second->Finish();
		};
		first->Finish();
		const int remaining = callback == 2 ? 0 : 1;
		check(cast->GetActiveApplicatorsCount() == remaining && parent->stops == 2 - remaining,
			"stop notification may stop itself or its sibling without duplicate ownership decrement");
		check(!first->IsAlive() && !first->IsEnabled() &&
			receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 + 3 * remaining,
			"terminal effect removes only its owned stat contribution");
		MemorizeApplicator(CObj<PFBaseApplicator>(first.GetPtr()));
		check(first->IsLinkedToAppliedRing(), "stopped effect is retained in the engine history ring");
		first->Finish(); first->Finish();
		check(cast->GetActiveApplicatorsCount() == remaining && parent->stops == 2 - remaining,
			"repeated stop cannot consume sibling ownership or repeat parent notifications");
		check(first->IsLinkedToAppliedRing(), "repeated cleanup must not unlink the already memorized history entry");
		const int enabled = first->enabledCalls;
		first->Block(); first->Unblock();
		check(!first->IsEnabled() && first->enabledCalls == enabled &&
			receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 + 3 * remaining,
			"stale block/unblock cannot reinsert a stopped buff modifier");
		check(first->Run() && !first->IsEnabled() && parent->starts == 2 &&
			cast->GetActiveApplicatorsCount() == remaining, "stopped applicator cannot restart its ownership or enabled effect");
		check(first->Tick(.1f), "stopped buff remains terminal when stepped again");
		parent->action = {};
		second->Finish(); first->Finish();
		check(cast->GetActiveApplicatorsCount() == 0 && receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
			"final sibling cleanup releases all effects without leftover modifiers");
	}
	for (bool stopOnEnable : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_FREEZE, 20);
		NDb::Ptr<NDb::Ability> dbAbility = new NDb::Ability;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), dbAbility, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), true);
		NDb::StatModApplicator* db = new NDb::StatModApplicator;
		db->modifier.stat = NDb::STAT_STRENGTH; db->modifier.multValue.sString = "1"; db->modifier.addValue.sString = "3";
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(cast, target); pars.pDBAppl = db;
		CObj<TerminalParentB12> parent = new TerminalParentB12(pars); pars.pParent = parent;
		CObj<TerminalStatB12> child = new TerminalStatB12(pars);
		child->stopOnEnable = stopOnEnable;
		parent->action = [&](PFBaseApplicator&, NDb::EParentNotification note) {
			if (!stopOnEnable && note == NDb::PARENTNOTIFICATION_START) child->Finish();
		};
		check(child->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(child.GetPtr()), cast),
			"parent may terminate an effect from its actual START notification");
		check(!child->IsAlive() && !child->IsEnabled() && child->enabledCalls == (stopOnEnable ? 1 : 0) &&
			cast->GetActiveApplicatorsCount() == 0 && parent->starts == 1 && parent->stops == 1,
			"start callback termination prevents subsequent Enable and duplicate Stop notification");
		check(receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
			"cancelled-in-start stat effect leaves no modifier behind");
		int active = 0;
		auto count = [&](const CObj<PFBaseApplicator>&) { ++active; };
		receiver->ForAllAppliedApplicators(count);
		check(active == 0, "termination during start or enable never registers a stopped effect as active");
		parent->action = {}; child->Finish();
	}
}
