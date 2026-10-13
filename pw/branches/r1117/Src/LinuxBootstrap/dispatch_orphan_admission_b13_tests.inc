/** Mock absent animation timing only; real construction, admission and ownership remain active. */
struct OrphanDelayTimerB13 : B10ActivationTimerRestore
{
	OBJECT_BASIC_METHODS(OrphanDelayTimerB13);
public:
	bool attack;
	explicit OrphanDelayTimerB13(bool attack = false) : B10ActivationTimerRestore(1), attack(attack) {}
	void DataChunk(chunk_id id, void* data, int size, int) override
	{
		if (size == sizeof(float) && (attack ? (id == 3 || id == 4) : (id == 10 || id == 11)))
			std::memcpy(data, &delay, sizeof(float));
	}
};

/** Observe the existing weak holder without giving its dispatch an extra scheduling owner. */
struct OrphanHolderSnapshotB13 : B10ActivationTimerRestore
{
	OBJECT_BASIC_METHODS(OrphanHolderSnapshotB13);
public:
	int depth = 0;
	bool attack;
	CPtr<PFDispatch> dispatch;
	explicit OrphanHolderSnapshotB13(bool attack = false) : attack(attack) {}
	bool IsReading() override { return false; }
	bool StartChunk(chunk_id id, int, int) override
	{
		if ((!depth && id == (attack ? 2 : 4)) || (depth == 1 && id == 2)) { ++depth; return true; }
		return false;
	}
	void FinishChunk() override { --depth; }
	void DataChunk(chunk_id, void*, int, int) override {}
	void StoreObject(CObjectBase* object) override { dispatch = dynamic_cast<PFDispatch*>(object); }
};

/** The void fallback makes the pre-fix runtime red reproducible; the fixed contract is bool. */
template<class Instance>
static bool OrphanApplyAttackB13(Instance* instance)
{
	if constexpr (std::is_same<decltype(instance->ApplyAttack()), bool>::value)
		return instance->ApplyAttack();
	else
	{
		instance->ApplyAttack();
		return true;
	}
}

/** Reject only delayed dispatches without another owner; do not promise a worldless scheduler.
 * Empty worldless deferred dispatches already dropped after Start in 037b. These tests cover
 * preactivation eligibility, cancellation and supported owners, not that older limitation.
 */
static void DispatchOrphanAdmissionB13(Checks& check, PFWorld* world)
{
	const int initialCount = check.count, initialFailed = check.failed;
	check((std::is_same<decltype(std::declval<PFBaseAttackInstance&>().ApplyAttack()), bool>::value),
		"native ApplyAttack reports admission, independently of synchronous completion");
	for (bool registered : {false, true}) for (bool payload : {false, true})
	for (bool attack : {false, true}) for (bool authored : {false, true})
	for (bool abandon : {false, true})
	{
		const bool orphan = !registered && !payload;
		const int caseFailed = check.failed;
		CObj<PFBaseUnit> sender = Unit(registered ? world : nullptr, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(registered ? world : nullptr, NDb::FACTION_BURN, 20);
		NDb::Ability* raw = attack ? static_cast<NDb::Ability*>(new NDb::BaseAttack) : new NDb::Ability;
		raw->manaCost.sString = "70"; raw->cooldownTime.sString = "5";
		if (authored) { auto* db = new NDb::DispatchImmediate; db->instantApply = false; raw->dispatch = db; }
		if (payload)
		{
			auto* damage = new NDb::DamageApplicator; damage->damage.sString = "25";
			damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; raw->applicators.push_back(damage);
		}
		NDb::Ptr<NDb::Ability> db = raw;
		CObj<PFAbilityData> data = attack
			? static_cast<PFAbilityData*>(new PFBaseAttackData(sender.GetPtr(), static_cast<NDb::BaseAttack*>(raw)))
			: new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1);
		CObj<B10AbilityEvents> events = new B10AbilityEvents; sender->AddEventListener(events.GetPtr());
		CObj<PFAbilityInstance> cast = attack
			? static_cast<PFAbilityInstance*>(new PFBaseAttackInstance(static_cast<PFBaseAttackData*>(data.GetPtr()), Target(receiver.GetPtr())))
			: new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		OrphanDelayTimerB13 timer(attack); cast->operator&(timer);
		const auto apply = [&]() { return attack
			? OrphanApplyAttackB13(static_cast<PFBaseAttackInstance*>(cast.GetPtr())) : cast->ApplyToTarget(); };
		const auto cancel = [&]() {
			if (attack) static_cast<PFBaseAttackInstance*>(cast.GetPtr())->Cancel(); else cast->Cancel();
		};
		const auto finished = [&]() { return attack
			? static_cast<PFBaseAttackInstance*>(cast.GetPtr())->IsReadyToDie() : cast->IsFinished(); };
		const auto unpaid = [&]() { return sender->GetMana() == 200 && receiver->GetHealth() == 1000
			&& data->GetCurrentCooldown() == 0 && events->casts == 0; };
		check(apply() == !orphan, "delayed admission requires a registration or payload owner");
		OrphanHolderSnapshotB13 snapshot(attack); cast->operator&(snapshot);
		check(IsValid(snapshot.dispatch) == !orphan, "eligibility agrees with actual weak instance holder after return");
		check(unpaid(), "construction or rejection does not charge, emit a cast event, or apply damage");
		if (orphan)
		{
			check(finished(), "rejected delayed orphan is terminal before any further update");
			if (!abandon)
			{
				check(!apply(), "failed orphan admission is terminal for repeated Apply calls");
				cast->Update(.25f); cast->Update(2); cast->Update(2);
				check(finished() && unpaid(), "later updates cannot activate or charge rejected orphan work");
				cancel(); cancel();
				check(!apply() && cast->Update(2) && unpaid(), "repeated cancellation cannot revive rejected work");
			}
			CPtr<PFAbilityInstance> weak(cast.GetPtr());
			CObj<PFAbilityInstance> other(cast); cast = nullptr;
			check(IsValid(weak), "an explicit second owner keeps the rejected instance alive");
			other = nullptr;
			check(!IsValid(weak), "last owner releases rejected pending cast without requiring caller cancellation");
		}
		else
		{
			check(IsValid(snapshot.dispatch) && !snapshot.dispatch->Started(), "supported delayed dispatch remains unstarted");
			if (IsValid(snapshot.dispatch))
				check(snapshot.dispatch->GetAbility().GetPtr() == cast.GetPtr()
					&& snapshot.dispatch->GetSender().GetPtr() == sender.GetPtr(), "supported prestart accessors remain unchanged");
			cast->Update(.25f);
			check(IsValid(snapshot.dispatch) && !snapshot.dispatch->Started() && unpaid(), "supported predeadline update preserves unpaid dispatch");
			if (abandon)
			{
				cancel(); cancel(); cast->Update(2);
				check(finished() && unpaid(), "explicit cancellation releases supported pending work without charge");
			}
			else
			{
				cast->Update(.75f);
				if (authored)
				{
					check(IsValid(snapshot.dispatch) && snapshot.dispatch->Started() && !snapshot.dispatch->HasApplied(),
						"registered or payload-owned deferred dispatch survives launch until the next tick");
					cast->Update(.1f);
				}
				cast->Update(2);
				check(finished() && receiver->GetHealth() == (payload ? 975 : 1000), "supported dispatch completes its payload exactly once");
				check(sender->GetMana() == (attack ? 200 : 130) && events->casts == (attack ? 0 : 1)
					&& data->GetCurrentCooldown() == (attack ? 0 : 5), "ability payment, event and cooldown occur only once at activation");
				cast->Update(2);
				check(receiver->GetHealth() == (payload ? 975 : 1000) && events->casts == (attack ? 0 : 1), "completed dispatch cannot replay on later updates");
				cancel();
			}
			// Completed payloads can retain their cast through engine history; only rejected
			// orphans above promise destruction on the last external instance owner release.
			cast = nullptr;
		}
		sender->RemoveEventListener(events.GetPtr());
		sender->Cleanup(); receiver->Cleanup();
		if (check.failed != caseFailed)
			std::printf("Orphan case: registered=%d payload=%d attack=%d deferred=%d abandon=%d failures=%d\n",
				registered, payload, attack, authored, abandon, check.failed - caseFailed);
	}

	// Native animation time offsets are currently zero, so use real factory rejection to
	// test DoAttack propagation, and real synchronous completion to rule out IsReadyToDie proxies.
	for (bool registered : {false, true}) for (bool unsupported : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(registered ? world : nullptr, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(registered ? world : nullptr, NDb::FACTION_BURN, 20);
		auto* raw = new NDb::BaseAttack;
		if (unsupported) { auto* invalid = new NDb::DispatchRockmanMace; invalid->speed = -1; raw->dispatch = invalid; }
		CObj<PFBaseAttackData> data = new PFBaseAttackData(sender.GetPtr(), raw);
		check(data->DoAttack(Target(receiver.GetPtr())) == !unsupported,
			"outer DoAttack propagates rejection but accepts successful synchronous empty completion");
		check(unsupported ? data->GetCurrentCooldown() == 0 : data->GetCurrentCooldown() > 0,
			"outer DoAttack starts cooldown only for admitted work");
		data->Update(2, true);
		check(receiver->GetHealth() == 1000 && sender->GetMana() == 200,
			"outer attack processing cannot revive rejected empty work or charge mana");
		data->Cancel(); sender->Cleanup(); receiver->Cleanup();
	}
	std::printf("Orphan admission: %d checks, %d failures\n", check.count - initialCount, check.failed - initialFailed);
}
