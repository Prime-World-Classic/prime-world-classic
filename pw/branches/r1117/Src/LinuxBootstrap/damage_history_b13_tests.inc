/** Loaded actual damage factories and the integrated C3 history lifecycle are the fixture. */
struct DamageHistoryFixtureB13 : ApplicatorHistoryFixtureB13
{
	using ApplicatorHistoryFixtureB13::ApplicatorHistoryFixtureB13;
	static NDb::Ptr<NDb::DamageApplicator> DamageDb(const char* amount,
		NDb::EApplicatorDamageType kind = NDb::APPLICATORDAMAGETYPE_PURE)
	{
		auto* db = new ApplicatorQueryDbB12<NDb::DamageApplicator>("/Mock/DamageHistory.xdb");
		db->damage.sString = amount; db->enabled.sString = "true"; db->damageType = kind;
		db->damageMode = NDb::DAMAGEMODE_DONTATTACKBACK; db->cancelInvisibility = false;
		// Allow distinct real hits at the same timestamp, rather than the independent DB limiter.
		db->timeLimiter = -1;
		return db;
	}
	CObj<PFApplDamage> Hit(const char* amount, int cast = 0, PFBaseUnit* target = nullptr,
		NDb::EApplicatorDamageType kind = NDb::APPLICATORDAMAGETYPE_PURE)
	{
		auto app = Apply(DamageDb(amount, kind), cast, nullptr, target);
		auto* damage = dynamic_cast<PFApplDamage*>(app.GetPtr());
		check(damage && !damage->IsAlive() && damage->IsLinkedToAppliedRing(),
			"actual instantaneous damage factory completes into C3 applied history");
		return damage;
	}
	void Tick(float seconds) { world->Step(seconds, seconds); }
	void Clear()
	{
		for (auto* unit : {receiver.GetPtr(), elsewhere.GetPtr(), sender.GetPtr(), other.GetPtr(),
			summon.GetPtr(), descendant.GetPtr(), otherSummon.GetPtr()}) unit->CleanupHistoryApplicators();
	}
};

/** Corrupt only serialized numeric fields; the instances and all owning links remain real. */
struct DamageHistoryAmountTagB13
{
	using Pointer = float PFApplDamage::*;
	friend Pointer DamageHistoryAmountMemberB13(DamageHistoryAmountTagB13);
};
template<DamageHistoryAmountTagB13::Pointer member>
struct DamageHistoryAmountAccessB13
{
	friend DamageHistoryAmountTagB13::Pointer DamageHistoryAmountMemberB13(DamageHistoryAmountTagB13) { return member; }
};
template struct DamageHistoryAmountAccessB13<&PFApplDamage::damageDealed>;

/** Query both endpoint APIs without assumptions about a formula-parser binding. */
static void DamageHistoryPairB13(Checks& check, PFBaseUnit* receiver, PFBaseUnit* sender,
	float interval, float taken, float dealt)
{
	const IUnitFormulaPars* r = receiver;
	const IUnitFormulaPars* s = sender;
	check(r->GetDamageTaken(interval) == taken, "simple Taken uses the receiver applied history through the existing virtual API");
	check(s->GetDamageDealt(interval) == dealt, "simple Dealt uses the sender sent history through the existing virtual API");
}

/** Stored completion times, strict interval edges and the original 35-second history limit. */
static void DamageHistoryTimesB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageHistoryFixtureB13 f(check, isolated.world); f.Tick(0); f.Tick(10);
	auto a = f.Hit("7.75"); f.Tick(.5f); auto b = f.Hit("2.75");
	check(a->GetMarkerTime() == 10 && b->GetMarkerTime() == 10.5f, "real damage captures distinct completion times, not creation zero");
	check(f.receiver->GetHealth() == 989.5f, "actual health reflects fractional hits independently of query integer accumulation");
	DamageHistoryPairB13(check, f.receiver, f.sender, .5f, 2, 2);
	DamageHistoryPairB13(check, f.receiver, f.sender, std::nextafter(.5f, 1.f), 9, 9);
	DamageHistoryPairB13(check, f.receiver, f.sender, .125f, 2, 2);
	DamageHistoryPairB13(check, f.receiver, f.sender, 0, 0, 0);
	f.Tick(.125f);
	DamageHistoryPairB13(check, f.receiver, f.sender, .125f, 0, 0);
	DamageHistoryPairB13(check, f.receiver, f.sender, std::nextafter(.125f, 1.f), 2, 2);
	f.Tick(34.375f);
	check(a->IsLinkedToAppliedRing() && a->IsLinkedToSentRing(), "C3 keeps the old hit at exactly 35 seconds of age");
	DamageHistoryPairB13(check, f.receiver, f.sender, 35, 2, 2);
	DamageHistoryPairB13(check, f.receiver, f.sender, 36, 9, 9);
	f.Tick(.125f);
	check(!a->IsLinkedToAppliedRing() && !a->IsLinkedToSentRing(), "production Step hook prunes the old hit strictly after 35 seconds");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1000, 2, 2);
	f.Tick(.5f);
	DamageHistoryPairB13(check, f.receiver, f.sender, 1000, 0, 0);
}

/** Endpoint identity, non-damage exclusion, ownerless records and mana damage follow Windows. */
static void DamageHistoryKindsB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageHistoryFixtureB13 f(check, isolated.world); f.Tick(0); f.Tick(4);
	auto a = f.Hit("10.75"), b = f.Hit("4.75", 2), c = f.Hit("6.75", 0, f.elsewhere);
	auto ownerless = f.Hit("3.75", -1);
	auto mana = f.Hit("8.75", 0, nullptr, NDb::APPLICATORDAMAGETYPE_MANA);
	check(mana->GetDamageDealed() == 8.75f && f.receiver->GetMana() == 191.25f,
		"real mana burn reports its actual energy amount through the same PFApplDamage class");
	auto* healRaw = new ApplicatorQueryDbB12<NDb::HealApplicator>("/Mock/DamageHistoryHeal.xdb");
	healRaw->amount.sString = "5"; healRaw->enabled.sString = "true";
	NDb::Ptr<NDb::HealApplicator> healDb = healRaw; f.Apply(healDb);
	auto stat = f.Apply(f.Stat("NotDamage")); stat->Stop(); MemorizeApplicator(stat);
	const auto applied = ApplicatorHistoryListB13(f.receiver, false);
	const auto sent = ApplicatorHistoryListB13(f.sender, true);
	check(applied.size() == 6 && sent.size() == 5, "mixed real damage, mana, heal and stat histories retain original endpoints");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 25, 24);
	DamageHistoryPairB13(check, f.elsewhere, f.other, 1, 6, 4);
	check(f.sender->GetDamageTaken(1) == 0 && f.receiver->GetDamageDealt(1) == 0,
		"opposite endpoint rings and units without matching history remain empty");
	check(!ownerless->GetAbilityOwner() && !ownerless->IsLinkedToSentRing(),
		"ownerless damage contributes to received history without fabricating sender ownership");
	const float life = f.receiver->GetHealth(), energy = f.receiver->GetMana();
	for (int i = 0; i != 4; ++i) DamageHistoryPairB13(check, f.receiver, f.sender, 1, 25, 24);
	check(ApplicatorHistoryListB13(f.receiver, false) == applied && ApplicatorHistoryListB13(f.sender, true) == sent,
		"repeated reverse traversal preserves both chronological ring orders and ownership");
	check(f.receiver->GetHealth() == life && f.receiver->GetMana() == energy && f.casts[0]->GetActiveApplicatorsCount() == 0,
		"history queries never pay, repeat damage/healing or mutate resources and active ability counts");
	f.sender->ChangeFaction(NDb::FACTION_BURN); f.receiver->SetVulnerable(false);
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 25, 24);
	f.receiver->CleanupHistoryApplicators();
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 24);
	f.sender->CleanupHistoryApplicators();
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
}

/** Reverse suffix scanning is not a scan-all filter, and non-damage timestamps also stop it. */
static void DamageHistoryOrderB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageHistoryFixtureB13 f(check, isolated.world); f.Tick(0); f.Tick(10);
	auto a = f.Hit("5"), b = f.Hit("7"), c = f.Hit("11");
	a->SetMarkerTime(9.75f); b->SetMarkerTime(5); c->SetMarkerTime(9.5f);
	const auto before = ApplicatorHistoryListB13(f.receiver, false);
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 11, 11);
	check(ApplicatorHistoryListB13(f.receiver, false) == before, "an out-of-order marker does not reorder or consume the ring");
	auto stat = f.Apply(f.Stat("OldNonDamage")); stat->Stop(); MemorizeApplicator(stat); stat->SetMarkerTime(0);
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
	stat->SetMarkerTime(10);
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 11, 11);
	DamageHistoryPairB13(check, f.receiver, f.sender, 11, 23, 23);
}

/** C++ int += float is rounded in float and truncated on EVERY visited hit. */
static void DamageHistoryIntegersB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageHistoryFixtureB13 f(check, isolated.world); f.Tick(0); f.Tick(2);
	f.Hit("1.75"); f.Hit("1.75");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 2, 2);
	f.Clear(); f.Hit(".75"); f.Hit(".75");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
	f.receiver->GetStat(NDb::STAT_LIFE)->SetCoreValue(4294967296.f);
	f.Tick(.125f);
	check(f.receiver->GetMaxHealth() == 4294967296.f, "real resource reconciliation supports the large finite damage fixture");
	for (bool largeNewest : {false, true})
	{
		f.Clear(); f.receiver->SetHealth(f.receiver->GetMaxHealth());
		if (!largeNewest) f.Hit("16777216");
		f.Hit("1"); f.Hit("1");
		if (largeNewest) f.Hit("16777216");
		const float expected = largeNewest ? 16777216.f : 16777218.f;
		DamageHistoryPairB13(check, f.receiver, f.sender, 1, expected, expected);
	}
	f.Clear(); f.receiver->SetHealth(f.receiver->GetMaxHealth());
	auto maxSafe = f.Hit("2147483520");
	check(maxSafe->GetDamageDealed() == 2147483520.f, "actual damage reaches the largest float safely convertible to positive int");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 2147483520.f, 2147483520.f);
	f.Hit("64");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
	f.Clear(); f.receiver->SetHealth(f.receiver->GetMaxHealth()); f.Hit("2147483648");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
}

/** Query while a real damage callback is active: the current hit is not yet memorized. */
struct DamageHistoryListenerB13 : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(DamageHistoryListenerB13);
public:
	PFBaseUnit* sender = nullptr;
	PFBaseUnit* receiver = nullptr;
	int calls = 0;
	float beforeTaken = -1, beforeDealt = -1;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (const auto* damage = dynamic_cast<const PFBaseUnitDamageEvent*>(event))
		{
			++calls;
			beforeTaken = receiver->GetDamageTaken(1); beforeDealt = sender->GetDamageDealt(1);
			damage->damage2Deal = 3.75f; damage->pDesc->delegatedDamage = 9;
		}
		return 0;
	}
};

static void DamageHistoryCallbacksB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageHistoryFixtureB13 f(check, isolated.world); f.Tick(0); f.Tick(3);
	f.Hit("7.75");
	CObj<DamageHistoryListenerB13> listener = new DamageHistoryListenerB13;
	listener->sender = f.sender; listener->receiver = f.receiver;
	f.receiver->AddEventListener(listener.GetPtr());
	auto app = f.Hit("100");
	check(listener->calls == 1 && listener->beforeTaken == 7 && listener->beforeDealt == 7,
		"query inside incoming callback sees preceding history but excludes the in-flight damage applicator");
	check(app->GetDamageDealed() == 3.75f && app->GetVariable("damageDealed") == 12.75f
		&& f.receiver->GetHealth() == 988.5f, "actual dealt amount differs from authored damage and delegated variable total");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 10, 10);
	check(listener->calls == 1, "queries never replay the damage callback");
	f.receiver->RemoveEventListener(listener.GetPtr());
	auto rejected = f.Hit("1/0");
	check(rejected->GetDamageDealed() == 0 && !rejected->IsAlive(), "invalid damage formula completes with zero real damage");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 10, 10);
	// A genuine health cap is reported by the damage applicator, not the original formula.
	f.receiver->AddFlag(NDb::UNITFLAG_FORBIDDEATH); f.receiver->SetHealth(5);
	auto capped = f.Hit("100");
	check(capped->GetDamageDealed() == 5 && f.receiver->GetHealth() == 1, "nonlethal guarded fixture records health-capped actual damage");
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 15, 15);
}

/** Invalid query clocks/intervals and serialized records fail without unsafe float-to-int conversion. */
static void DamageHistoryGuardsB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageHistoryFixtureB13 f(check, isolated.world); f.Tick(0); f.Tick(10);
	auto a = f.Hit("7");
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	for (float interval : {0.f, -1.f, nan, inf, -inf}) DamageHistoryPairB13(check, f.receiver, f.sender, interval, 0, 0);
	DamageHistoryPairB13(check, f.receiver, f.sender, std::numeric_limits<float>::denorm_min(), 7, 7);
	DamageHistoryPairB13(check, f.receiver, f.sender, std::numeric_limits<float>::max(), 7, 7);
	float& clock = isolated.world->*ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13());
	for (float bad : {nan, inf, -inf, -1.f})
	{
		clock = bad; DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
	}
	clock = 10;
	for (float bad : {nan, inf, -inf, 11.f, -1.f})
	{
		a->SetMarkerTime(bad); DamageHistoryPairB13(check, f.receiver, f.sender, 20, 0, 0);
	}
	a->SetMarkerTime(10);
	float& value = a.GetPtr()->*DamageHistoryAmountMemberB13(DamageHistoryAmountTagB13());
	for (float bad : {nan, inf, -inf, 2147483648.f, -4294967296.f})
	{
		value = bad; DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
	}
	value = 7;
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 7, 7);
	value = -2.75f;
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, -2, -2);
	value = -2147483648.f;
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, -2147483648.f, -2147483648.f);
	value = 7;
	const auto applied = ApplicatorHistoryListB13(f.receiver, false);
	auto b = f.Hit("3"); value = nan;
	DamageHistoryPairB13(check, f.receiver, f.sender, 1, 0, 0);
	value = 7; DamageHistoryPairB13(check, f.receiver, f.sender, 1, 10, 10);
	check(ApplicatorHistoryListB13(f.receiver, false).size() == applied.size() + 1 && a->IsLinkedToAppliedRing()
		&& a->IsLinkedToSentRing(), "failed numeric queries restore ring ownership and permit later valid recovery");
	CObj<PFBaseUnit> noWorld = Unit(nullptr, NDb::FACTION_FREEZE, 10);
	check(noWorld->GetDamageTaken(1) == 0 && noWorld->GetDamageDealt(1) == 0,
		"missing world is an empty guarded query, not a clock dereference");
	noWorld->Cleanup();
}

/** Keep all simulation stepping and corrupted numeric fixtures inside their own loaded worlds. */
static void DamageHistoryB13(Checks& check)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	auto* logic = new NDb::SessionLogicRoot; logic->aiLogic = new NDb::AILogicParameters;
	logic->unitLogicParameters = new NDb::UnitsLogicParameters;
	auto* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	DamageHistoryTimesB13(check);
	DamageHistoryKindsB13(check);
	DamageHistoryOrderB13(check);
	DamageHistoryIntegersB13(check);
	DamageHistoryCallbacksB13(check);
	DamageHistoryGuardsB13(check);
	NDb::SessionRoot::InitRoot(saved);
}
