/**
 * Last-strong release destroys members, then reconstructs an invalid weak shell.
 * Each concrete fixture needs OBJECT_METHODS with its production parent's typeId
 * and an inert default constructor: inheriting the macro reconstructs the parent,
 * loses the fixture's dynamic type/size and can select the wrong sized delete.
 * Inspect only metadata and test-owned fields on shells, never gameplay hooks.
 */
static void FixtureLifetimeB13(Checks& check)
{
	const int initialCount = check.count, initialFailed = check.failed;
	{
		CObj<EnabledBuffB12> owner = new EnabledBuffB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->enabledCalls = 3; owner->disabledCalls = 2;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(EnabledBuffB12)
			&& weak->GetTypeId() == PFApplBuff::typeId,
			"fixture lifetime EnabledBuffB12: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<EnabledBuffB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime EnabledBuffB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(EnabledBuffB12) && weak->GetTypeId() == PFApplBuff::typeId,
			"fixture lifetime EnabledBuffB12: shell preserves concrete size and production typeId");
		check(shell && shell->enabledCalls == 0 && shell->disabledCalls == 0,
			"fixture lifetime EnabledBuffB12: shell resets test counters without gameplay");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<AbilityModProbeB12> owner = new AbilityModProbeB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->enables = 4;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(AbilityModProbeB12)
			&& weak->GetTypeId() == PFApplAbilityMod::typeId,
			"fixture lifetime AbilityModProbeB12: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<AbilityModProbeB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime AbilityModProbeB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(AbilityModProbeB12) && weak->GetTypeId() == PFApplAbilityMod::typeId,
			"fixture lifetime AbilityModProbeB12: shell preserves concrete size and production typeId");
		check(shell && shell->enables == 0,
			"fixture lifetime AbilityModProbeB12: shell resets test counter without gameplay");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<WeightedListB12> owner = new WeightedListB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->candidates.reserve(8);
		owner->candidates.push_back(Target(CVec3(1, 2, 3)));
		CObj<WeightedListB12> last(owner);
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(WeightedListB12)
			&& weak->GetTypeId() == static_cast<int>(PFTargetSelector::typeId),
			"fixture lifetime WeightedListB12: live concrete size and production typeId");
		owner = nullptr;
		check(IsValid(weak) && last->candidates.size() == 1 && last->candidates.capacity() >= 8,
			"fixture lifetime WeightedListB12: remaining strong owner retains vector storage");
		last = nullptr;
		auto* shell = dynamic_cast<WeightedListB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime WeightedListB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(WeightedListB12)
			&& weak->GetTypeId() == static_cast<int>(PFTargetSelector::typeId),
			"fixture lifetime WeightedListB12: shell preserves concrete size and production typeId");
		check(shell && shell->candidates.empty() && shell->candidates.capacity() == 0,
			"fixture lifetime WeightedListB12: last strong release leaves no vector storage in shell");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<TimedBuffB12> owner = new TimedBuffB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->durationScale = 7; owner->enabledCalls = 3; owner->disabledCalls = 2;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(TimedBuffB12)
			&& weak->GetTypeId() == PFApplBuff::typeId,
			"fixture lifetime TimedBuffB12: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<TimedBuffB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime TimedBuffB12: further-derived weak shell does not become EnabledBuffB12");
		check(weak->GetSizeOf() == sizeof(TimedBuffB12) && weak->GetTypeId() == PFApplBuff::typeId,
			"fixture lifetime TimedBuffB12: shell preserves concrete size and production typeId");
		check(shell && shell->durationScale == 1 && shell->enabledCalls == 0 && shell->disabledCalls == 0,
			"fixture lifetime TimedBuffB12: shell resets both fixture layers without gameplay");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<StatBuffProbeB12> owner = new StatBuffProbeB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->enabledCalls = 3;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(StatBuffProbeB12)
			&& weak->GetTypeId() == PFApplStatMod::typeId,
			"fixture lifetime StatBuffProbeB12: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<StatBuffProbeB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime StatBuffProbeB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(StatBuffProbeB12) && weak->GetTypeId() == PFApplStatMod::typeId,
			"fixture lifetime StatBuffProbeB12: shell preserves concrete size and production typeId");
		check(shell && shell->enabledCalls == 0,
			"fixture lifetime StatBuffProbeB12: shell resets test counter without gameplay");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<TerminalStatB12> owner = new TerminalStatB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->enabledCalls = 3; owner->stopOnEnable = true;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(TerminalStatB12)
			&& weak->GetTypeId() == PFApplStatMod::typeId,
			"fixture lifetime TerminalStatB12: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<TerminalStatB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime TerminalStatB12: further-derived weak shell does not become StatBuffProbeB12");
		check(weak->GetSizeOf() == sizeof(TerminalStatB12) && weak->GetTypeId() == PFApplStatMod::typeId,
			"fixture lifetime TerminalStatB12: shell preserves concrete size and production typeId");
		check(shell && shell->enabledCalls == 0 && !shell->stopOnEnable,
			"fixture lifetime TerminalStatB12: shell resets both fixture layers without gameplay");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<TerrainFormulaStatB13> owner = new TerrainFormulaStatB13;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->enabledCalls = 3;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(TerrainFormulaStatB13)
			&& weak->GetTypeId() == PFApplStatMod::typeId,
			"fixture lifetime TerrainFormulaStatB13: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<TerrainFormulaStatB13*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime TerrainFormulaStatB13: further-derived weak shell does not become StatBuffProbeB12");
		check(weak->GetSizeOf() == sizeof(TerrainFormulaStatB13) && weak->GetTypeId() == PFApplStatMod::typeId,
			"fixture lifetime TerrainFormulaStatB13: shell preserves concrete size and production typeId");
		check(shell && shell->enabledCalls == 0,
			"fixture lifetime TerrainFormulaStatB13: shell resets inherited test counter without gameplay");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		int calls = 0;
		auto lease = std::make_shared<int>(0);
		std::weak_ptr<int> released(lease);
		CObj<TerminalParentB12> owner = new TerminalParentB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->starts = 3; owner->stops = 2;
		owner->action = [lease, &calls](PFBaseApplicator&, NDb::EParentNotification) { ++*lease; ++calls; };
		lease.reset();
		CObj<TerminalParentB12> last(owner);
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(TerminalParentB12)
			&& weak->GetTypeId() == PFBaseApplicator::typeId,
			"fixture lifetime TerminalParentB12: live concrete size and production typeId");
		owner = nullptr;
		check(IsValid(weak) && !released.expired() && calls == 0 && last->action,
			"fixture lifetime TerminalParentB12: remaining strong owner retains callback capture");
		last = nullptr;
		auto* shell = dynamic_cast<TerminalParentB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime TerminalParentB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(TerminalParentB12) && weak->GetTypeId() == PFBaseApplicator::typeId,
			"fixture lifetime TerminalParentB12: shell preserves concrete size and production typeId");
		check(released.expired() && calls == 0 && shell && !shell->action && shell->starts == 0 && shell->stops == 0,
			"fixture lifetime TerminalParentB12: last strong release destroys capture without notification");
		std::fflush(stdout);
		weak = nullptr;
		check(released.expired() && calls == 0,
			"fixture lifetime TerminalParentB12: final weak release does not invoke callback");
	}
	{
		int calls = 0;
		auto lease = std::make_shared<int>(0);
		std::weak_ptr<int> released(lease);
		CObj<CancelDispatchParentB12> owner = new CancelDispatchParentB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		// These are non-owning entries; only the vector's storage belongs to the fixture.
		owner->dropped.reserve(8); owner->dropped.push_back(nullptr);
		owner->action = [lease, &calls](const PFDispatch*) { ++*lease; ++calls; };
		lease.reset();
		CObj<CancelDispatchParentB12> last(owner);
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(CancelDispatchParentB12)
			&& weak->GetTypeId() == PFBaseApplicator::typeId,
			"fixture lifetime CancelDispatchParentB12: live concrete size and production typeId");
		owner = nullptr;
		check(IsValid(weak) && !released.expired() && calls == 0 && last->action
			&& last->dropped.size() == 1 && last->dropped.capacity() >= 8,
			"fixture lifetime CancelDispatchParentB12: remaining strong owner retains callback and vector");
		last = nullptr;
		auto* shell = dynamic_cast<CancelDispatchParentB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime CancelDispatchParentB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(CancelDispatchParentB12) && weak->GetTypeId() == PFBaseApplicator::typeId,
			"fixture lifetime CancelDispatchParentB12: shell preserves concrete size and production typeId");
		check(released.expired() && calls == 0 && shell && !shell->action
			&& shell->dropped.empty() && shell->dropped.capacity() == 0,
			"fixture lifetime CancelDispatchParentB12: last strong release clears callback and vector storage");
		std::fflush(stdout);
		weak = nullptr;
		check(released.expired() && calls == 0,
			"fixture lifetime CancelDispatchParentB12: final weak release does not invoke callback");
	}
	{
		int calls = 0;
		auto lease = std::make_shared<int>(0);
		std::weak_ptr<int> released(lease);
		CObj<ApplicatorHistoryFinalizerB13> owner = new ApplicatorHistoryFinalizerB13;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->onDestroy = [lease, &calls] { ++*lease; ++calls; };
		lease.reset();
		CObj<ApplicatorHistoryFinalizerB13> last(owner);
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(ApplicatorHistoryFinalizerB13)
			&& weak->GetTypeId() == PFBaseApplicator::typeId,
			"fixture lifetime ApplicatorHistoryFinalizerB13: live concrete size and production typeId");
		owner = nullptr;
		check(IsValid(weak) && !released.expired() && calls == 0 && last->onDestroy,
			"fixture lifetime ApplicatorHistoryFinalizerB13: remaining strong owner retains finalizer capture");
		last = nullptr;
		auto* shell = dynamic_cast<ApplicatorHistoryFinalizerB13*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime ApplicatorHistoryFinalizerB13: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(ApplicatorHistoryFinalizerB13) && weak->GetTypeId() == PFBaseApplicator::typeId,
			"fixture lifetime ApplicatorHistoryFinalizerB13: shell preserves concrete size and production typeId");
		check(released.expired() && calls == 1 && shell && !shell->onDestroy,
			"fixture lifetime ApplicatorHistoryFinalizerB13: last strong release runs finalizer once then destroys capture");
		std::fflush(stdout);
		weak = nullptr;
		check(released.expired() && calls == 1,
			"fixture lifetime ApplicatorHistoryFinalizerB13: final weak release never repeats finalizer");
	}
	{
		CObj<MicroListB12> owner = new MicroListB12;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(MicroListB12)
			&& weak->GetTypeId() == static_cast<int>(PFMultipleTargetSelector::typeId),
			"fixture lifetime MicroListB12: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<MicroListB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime MicroListB12: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(MicroListB12)
			&& weak->GetTypeId() == static_cast<int>(PFMultipleTargetSelector::typeId),
			"fixture lifetime MicroListB12: shell preserves concrete size and production typeId");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<DamageBehaviour> owner = new DamageBehaviour;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->calls = 3; owner->deaths = 2; owner->stops = 1; owner->observedHealth = 42;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(DamageBehaviour)
			&& weak->GetTypeId() == static_cast<int>(PFBaseBehaviour::typeId),
			"fixture lifetime DamageBehaviour: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<DamageBehaviour*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime DamageBehaviour: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(DamageBehaviour)
			&& weak->GetTypeId() == static_cast<int>(PFBaseBehaviour::typeId),
			"fixture lifetime DamageBehaviour: shell preserves concrete size and production typeId");
		check(shell && shell->calls == 0 && shell->deaths == 0 && shell->stops == 0 && shell->observedHealth == -1,
			"fixture lifetime DamageBehaviour: shell resets test state without invoking gameplay hooks");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<SchedulingState> owner = new SchedulingState;
		CPtr<CObjectBase> weak(owner.GetPtr());
		CObjectBase* address = weak.GetPtr();
		owner->entries = 3; owner->steps = 2; owner->leaves = 1; owner->elapsed = 4; owner->complete = true;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(SchedulingState)
			&& weak->GetTypeId() == static_cast<int>(PFBaseUnitState::typeId),
			"fixture lifetime SchedulingState: live concrete size and production typeId");
		owner = nullptr;
		auto* shell = dynamic_cast<SchedulingState*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"fixture lifetime SchedulingState: invalid weak shell keeps address and concrete type");
		check(weak->GetSizeOf() == sizeof(SchedulingState)
			&& weak->GetTypeId() == static_cast<int>(PFBaseUnitState::typeId),
			"fixture lifetime SchedulingState: shell preserves concrete size and production typeId");
		check(shell && shell->entries == 0 && shell->steps == 0 && shell->leaves == 0
			&& shell->elapsed == 0 && !shell->complete,
			"fixture lifetime SchedulingState: shell resets test state without invoking gameplay hooks");
		std::fflush(stdout);
		weak = nullptr;
	}
	{
		CObj<B9UseState> owner = new B9UseState;
		CPtr<CObjectBase> weak(owner.GetPtr());
		owner->db = new NDb::Ability; owner->useRange = 19;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(B9UseState), "base-use fixture reports concrete live size");
		owner = nullptr;
		auto* shell = dynamic_cast<B9UseState*>(weak.GetPtr());
		check(!IsValid(weak) && shell && weak->GetTypeId() == static_cast<int>(PFBaseUseState::typeId),
			"base-use fixture preserves concrete shell and inherited production type ID");
		check(shell && !shell->db && shell->useRange == 4 && weak->GetSizeOf() == sizeof(B9UseState),
			"base-use fixture releases owned descriptor before final weak deletion");
		std::fflush(stdout); weak = nullptr;
	}
	{
		CObj<B9HeroUseState> owner = new B9HeroUseState;
		CPtr<CObjectBase> weak(owner.GetPtr());
		owner->db = new NDb::Ability; owner->useRange = 19;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(B9HeroUseState), "hero-use fixture reports concrete live size");
		owner = nullptr;
		auto* shell = dynamic_cast<B9HeroUseState*>(weak.GetPtr());
		check(!IsValid(weak) && shell && weak->GetTypeId() == static_cast<int>(PFBaseUseHeroState::typeId),
			"hero-use fixture preserves concrete shell and inherited production type ID");
		check(shell && !shell->db && shell->useRange == 4 && weak->GetSizeOf() == sizeof(B9HeroUseState),
			"hero-use fixture releases owned descriptor before final weak deletion");
		std::fflush(stdout); weak = nullptr;
	}
	{
		CObj<B9Interaction> owner = new B9Interaction;
		CPtr<CObjectBase> weak(owner.GetPtr());
		owner->actions = 7;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(B9Interaction), "interaction fixture reports concrete live size");
		owner = nullptr;
		auto* shell = dynamic_cast<B9Interaction*>(weak.GetPtr());
		check(!IsValid(weak) && shell && weak->GetTypeId() == static_cast<int>(PFInteractObjectState::typeId),
			"interaction fixture preserves concrete shell and inherited production type ID");
		check(shell && shell->actions == 0 && weak->GetSizeOf() == sizeof(B9Interaction),
			"interaction fixture reconstructs its shell without triggering actions");
		std::fflush(stdout); weak = nullptr;
	}
	{
		CObj<B9Pickup> owner = new B9Pickup;
		CPtr<CObjectBase> weak(owner.GetPtr());
		owner->ability = new NDb::Ability; owner->picked = 9;
		check(IsValid(weak) && weak->GetSizeOf() == sizeof(B9Pickup), "pickup fixture reports concrete live size");
		owner = nullptr;
		auto* shell = dynamic_cast<B9Pickup*>(weak.GetPtr());
		check(!IsValid(weak) && shell && weak->GetTypeId() == static_cast<int>(PFPickupableObjectBase::typeId),
			"pickup fixture preserves concrete shell and inherited production type ID");
		check(shell && !shell->ability && shell->picked == 0 && weak->GetSizeOf() == sizeof(B9Pickup),
			"pickup fixture releases owned ability without invoking pickup on its shell");
		std::fflush(stdout); weak = nullptr;
	}
	std::printf("Fixture lifetime: %d checks, %d failures\n", check.count - initialCount, check.failed - initialFailed);
	std::fflush(stdout);
}
