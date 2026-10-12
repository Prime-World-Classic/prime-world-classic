/** Loaded mock descriptors retain real engine factories and authored effect behavior. */
template<class T>
struct ApplicatorQueryDbB12 : T
{
	explicit ApplicatorQueryDbB12(const char* id)
	{
		this->SetDbId(NDb::DBID(id));
		ProbeResourceCache::MarkLoaded(this);
	}
};

/** Each query group owns its endpoints; only the outer entry point owns/steps its world. */
struct ApplicatorQueryFixtureB12
{
	Checks& check;
	PFWorld* world;
	CObj<PFBaseUnit> sender, other, receiver, elsewhere, summon, descendant, otherSummon;
	NDb::Ptr<NDb::Ability> db;
	std::vector<CObj<PFAbilityData>> data;
	std::vector<CObj<PFAbilityInstance>> casts;
	std::vector<CObj<PFDispatch>> dispatches;
	std::vector<CObj<PFBaseApplicator>> effects;

	ApplicatorQueryFixtureB12(Checks& check, PFWorld* world) : check(check), world(world)
	{
		sender = Unit(world, NDb::FACTION_FREEZE, 40);
		other = Unit(world, NDb::FACTION_FREEZE, 40);
		receiver = Unit(world, NDb::FACTION_FREEZE, 20);
		elsewhere = Unit(world, NDb::FACTION_FREEZE, 20);
		summon = Unit(world, NDb::FACTION_FREEZE, 10);
		descendant = Unit(world, NDb::FACTION_FREEZE, 10);
		otherSummon = Unit(world, NDb::FACTION_FREEZE, 10);
		for (auto* unit : {sender.GetPtr(), other.GetPtr(), receiver.GetPtr(), elsewhere.GetPtr(),
			summon.GetPtr(), descendant.GetPtr(), otherSummon.GetPtr()})
			unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		summon->SetMaster(sender.GetPtr()); descendant->SetMaster(summon.GetPtr()); otherSummon->SetMaster(other.GetPtr());
		auto* raw = new ApplicatorQueryDbB12<NDb::Ability>("/Mock/QueryAbility.xdb");
		raw->manaCost.sString = "0"; db = raw;
		for (auto* owner : {sender.GetPtr(), other.GetPtr(), summon.GetPtr(), descendant.GetPtr(), otherSummon.GetPtr()})
			data.push_back(new PFAbilityData(owner, db, NDb::ABILITYTYPEID_SPECIAL, false, false));
		for (int index : {0, 0, 1, 2, 3, 4})
			casts.push_back(new PFAbilityInstance(data[index], Target(receiver.GetPtr()), false));
	}
	static NDb::Ptr<NDb::StatModApplicator> Stat(const char* name, const char* enabled = "true", const char* lifetime = "-1")
	{
		auto* raw = new ApplicatorQueryDbB12<NDb::StatModApplicator>("/Mock/QueryStat.xdb");
		raw->formulaName = name; raw->enabled.sString = enabled; raw->startCondition.sString = "true";
		raw->lifeTime.sString = lifetime; raw->modifier.stat = NDb::STAT_STRENGTH;
		raw->modifier.multValue.sString = "1"; raw->modifier.addValue.sString = "3";
		raw->dontUpdate = false;
		return raw;
	}
	CObj<PFBaseApplicator> Apply(const NDb::BaseApplicator* descriptor, int cast = 0,
		PFDispatch* dispatch = nullptr, PFBaseUnit* targetUnit = nullptr)
	{
		const Target target(targetUnit ? targetUnit : receiver.GetPtr());
		const CObj<PFAbilityInstance> instance = cast < 0 ? CObj<PFAbilityInstance>() : casts[cast];
		PFApplCreatePars pars(instance, target, CPtr<PFBaseApplicator>(), dispatch);
		pars.pWorld = world; pars.pDBAppl = descriptor;
		CObj<PFBaseApplicator> effect = CreateApplicator(pars);
		check(effect && ActivateApplicator(effect, instance), "query fixture activates an actual loaded factory effect");
		if (effect) effects.push_back(effect);
		return effect;
	}
	CObj<PFDispatch> Dispatch(int cast)
	{
		auto* spellRaw = new ApplicatorQueryDbB12<NDb::Spell>("/Mock/QuerySpell.xdb");
		auto* dispatchRaw = new ApplicatorQueryDbB12<NDb::DispatchByTime>("/Mock/QueryDispatch.xdb");
		dispatchRaw->lifeTime.sString = "100"; spellRaw->dispatch = dispatchRaw;
		NDb::Ptr<NDb::Spell> spell = spellRaw;
		CObj<PFDispatch> dispatch = CreateDispatch(casts[cast], nullptr,
			Target(casts[cast]->GetOwner().GetPtr()), Target(receiver.GetPtr()), spell);
		check(dispatch && !dispatch->IsObjectDead(), "query fixture creates a live real dispatch identity");
		if (dispatch) dispatches.push_back(dispatch);
		return dispatch;
	}
	~ApplicatorQueryFixtureB12()
	{
		for (auto it = effects.rbegin(); it != effects.rend(); ++it) (*it)->Stop();
		for (const auto& dispatch : dispatches) dispatch->Cancel();
		for (auto* unit : {receiver.GetPtr(), elsewhere.GetPtr(), sender.GetPtr(), other.GetPtr(),
			summon.GetPtr(), descendant.GetPtr(), otherSummon.GetPtr()}) unit->Cleanup();
	}
};

/** Assert ordered matching independently of Find's enabled policy and Count's enabled-only policy. */
static void ApplicatorQueryOrderB12(Checks& check, PFBaseUnit* receiver, const IMiscFormulaPars* context,
	ApplicatorSearchType type, const std::vector<CObj<PFBaseApplicator>>& expected)
{
	for (int index = -1; index <= static_cast<int>(expected.size()) + 1; ++index)
	{
		const int position = index < 0 ? 0 : index;
		const IMiscFormulaPars* match = position < static_cast<int>(expected.size()) ? expected[position].GetPtr() : context;
		check(receiver->FindApplicator("Shared", context, type, index) == match,
			"indexed lookup preserves matching ring insertion order, negative-first policy and fallback identity");
	}
	check(receiver->FindApplicator("Shared", context, type) == (expected.empty() ? context : expected[0].GetPtr()),
		"unindexed query is exactly index zero");
	check(receiver->CountApplicators("Shared", context, type) == static_cast<int>(expected.size()),
		"count uses the same exact owner instance dispatch or direct-master filter");
}

/** Same DB, owner, cast, dispatch and direct master are intentionally different search scopes. */
static void ApplicatorQuerySelectionB12(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	auto d0 = f.Dispatch(0), d1 = f.Dispatch(0);
	if (!d0 || !d1) return;
	auto db = f.Stat("Shared");
	auto a = f.Apply(db, 0, d0.GetPtr()), b = f.Apply(db, 0, d1.GetPtr());
	auto c = f.Apply(db, 1), d = f.Apply(db, 2), e = f.Apply(db, 3), g = f.Apply(db, 4), h = f.Apply(db, 5);
	auto extra = f.Apply(db, 0, d0.GetPtr(), f.elsewhere.GetPtr());
	if (!a || !b || !c || !d || !e || !g || !h || !extra) return;
	check(f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 41 && f.elsewhere->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"named ring fixtures own real stat modifiers on their actual separate receivers");
	ApplicatorQueryOrderB12(check, f.receiver, a, GLOBAL, {a, b, c, d, e, g, h});
	ApplicatorQueryOrderB12(check, f.receiver, a, UNIT, {a, b, c});
	ApplicatorQueryOrderB12(check, f.receiver, a, ABILITY, {a, b});
	ApplicatorQueryOrderB12(check, f.receiver, a, DISPATCH, {a});
	ApplicatorQueryOrderB12(check, f.receiver, a, UNIT_AND_SUMMONS, {a, b, c, e});
	ApplicatorQueryOrderB12(check, f.receiver, d, UNIT_AND_SUMMONS, {d, h});
	ApplicatorQueryOrderB12(check, f.receiver, c, DISPATCH, {c, d, e, g, h});
	ApplicatorQueryOrderB12(check, f.receiver, f.data[0], UNIT, {a, b, c});
	ApplicatorQueryOrderB12(check, f.receiver, f.data[0], UNIT_AND_SUMMONS, {a, b, c, e});
	// Original ability contexts cannot name one cast or dispatch and fall back to GLOBAL.
	for (auto type : {ABILITY, DISPATCH, static_cast<ApplicatorSearchType>(99)})
		ApplicatorQueryOrderB12(check, f.receiver, f.data[0], type, {a, b, c, d, e, g, h});
	TargetSelectorFormulaPars wrapped(a.GetPtr());
	ApplicatorQueryOrderB12(check, f.receiver, &wrapped, ABILITY, {a, b});
	TargetSelectorFormulaPars wrappedAbility(f.data[0].GetPtr());
	ApplicatorQueryOrderB12(check, f.receiver, &wrappedAbility, UNIT_AND_SUMMONS, {a, b, c, e});
	check(f.sender->CountApplicators("Shared", a, GLOBAL) == 0 && f.sender->FindApplicator("Shared", a, GLOBAL) == a.GetPtr(),
		"receiver queries do not search its sent ring or the global world");
	check(f.elsewhere->CountApplicators("Shared", a, GLOBAL) == 1 && f.elsewhere->FindApplicator("Shared", a, GLOBAL) == extra.GetPtr(),
		"same dispatch on another receiver remains confined to that receiver named ring");
	f.summon->SetMaster(f.other.GetPtr());
	check(f.receiver->CountApplicators("Shared", a, UNIT_AND_SUMMONS) == 3
		&& f.receiver->CountApplicators("Shared", d, UNIT_AND_SUMMONS) == 3,
		"direct master changes are read live without recursive descendant inclusion or cached ownership");
	f.sender->ChangeFaction(NDb::FACTION_BURN);
	check(f.receiver->CountApplicators("Shared", a, UNIT) == 3, "ownership queries compare identity rather than current faction");
	check(f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 41 && f.casts[0]->GetActiveApplicatorsCount() == 3,
		"repeated query traversal neither mutates effects nor consumes active ability ownership");
	f.casts[0]->RemoveApplicatorsFrom(f.receiver.GetPtr());
	check(f.receiver->CountApplicators("Shared", f.data[0], GLOBAL) == 5
		&& f.receiver->FindApplicator("Shared", f.data[0], GLOBAL) == c.GetPtr()
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 35,
		"real ability removal retires only matching receiver effects and their named entries");
	check(f.elsewhere->CountApplicators("Shared", f.data[0], GLOBAL) == 1 && f.casts[0]->GetActiveApplicatorsCount() == 1,
		"ability removal on one receiver retains same-cast ownership on another receiver");
}

/** A disabled buff remains findable, but enabled counts track actual stat ownership. */
static void ApplicatorQueryNamesB12(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	f.sender->GetVariableVWM("QueryGate")->SetBaseValue(0);
	auto disabledDb = f.Stat("Shared", "sender.vQueryGate != 0");
	auto a = f.Apply(disabledDb), b = f.Apply(f.Stat("Shared")), lower = f.Apply(f.Stat("shared"));
	if (!a || !b || !lower) return;
	check(a->IsAlive() && !a->IsEnabled() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 26,
		"initially disabled real stat buff stays alive and owns no modifier");
	check(f.receiver->FindApplicator("Shared", f.data[0], GLOBAL) == a.GetPtr()
		&& f.receiver->FindApplicator("Shared", f.data[0], GLOBAL, 1) == b.GetPtr()
		&& f.receiver->CountApplicators("Shared", f.data[0], GLOBAL) == 1,
		"Find includes disabled buffs in ordinal order while Count excludes them");
	check(f.receiver->FindApplicator("shared", a, GLOBAL) == lower.GetPtr()
		&& f.receiver->CountApplicators("SHARED", a, GLOBAL) == 0,
		"formula names compare exactly and case sensitively");
	f.sender->GetVariableVWM("QueryGate")->SetBaseValue(1); a->Step(0);
	check(a->IsEnabled() && f.receiver->CountApplicators("Shared", a, GLOBAL) == 2
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 29,
		"live authored enable transition updates count and real modifier ownership");
	f.sender->GetVariableVWM("QueryGate")->SetBaseValue(0); a->Step(0);
	check(!a->IsEnabled() && f.receiver->CountApplicators("Shared", a, GLOBAL) == 1
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 26,
		"live authored disable transition removes only its own stat contribution");
	a->Stop(); MemorizeApplicator(a);
	check(f.receiver->FindApplicator("Shared", f.data[0], GLOBAL) == b.GetPtr()
		&& f.receiver->CountApplicators("Shared", f.data[0], GLOBAL) == 1,
		"stopped and memorized disabled effect is absent from named lookup even while retained in history");
	auto leaf = f.Stat("InnerName");
	auto* middleRaw = new ApplicatorQueryDbB12<NDb::ApplicatorNameOverrider>("/Mock/QueryMiddle.xdb");
	middleRaw->applicator = leaf; middleRaw->formulaName = "MiddleName";
	NDb::Ptr<NDb::ApplicatorNameOverrider> middle = middleRaw;
	auto* outerRaw = new ApplicatorQueryDbB12<NDb::ApplicatorNameOverrider>("/Mock/QueryOuter.xdb");
	outerRaw->applicator = middle; outerRaw->formulaName = "OuterName"; outerRaw->enabled.sString = "true";
	NDb::Ptr<NDb::ApplicatorNameOverrider> outer = outerRaw;
	auto wrapped = f.Apply(outer);
	check(wrapped && dynamic_cast<PFApplStatMod*>(wrapped.GetPtr()) && wrapped->GetDBBase() == leaf,
		"nested loaded wrapper resolves to a real typed stat effect through c41 factory");
	check(wrapped && f.receiver->FindApplicator("OuterName", f.data[0], GLOBAL) == wrapped.GetPtr()
		&& f.receiver->CountApplicators("OuterName", f.data[0], GLOBAL) == 1
		&& f.receiver->CountApplicators("InnerName", f.data[0], GLOBAL) == 0
		&& f.receiver->CountApplicators("MiddleName", f.data[0], GLOBAL) == 0,
		"only outermost authored wrapper name is searchable, never resolved inner effect names");
	auto* emptyRaw = new ApplicatorQueryDbB12<NDb::ApplicatorNameOverrider>("/Mock/QueryUnnamed.xdb");
	emptyRaw->applicator = leaf; emptyRaw->formulaName = ""; emptyRaw->enabled.sString = "true";
	NDb::Ptr<NDb::ApplicatorNameOverrider> empty = emptyRaw;
	auto unnamed = f.Apply(empty);
	check(unnamed && unnamed->IsEnabled() && f.receiver->CountApplicators("", f.data[0], GLOBAL) == 0
		&& f.receiver->FindApplicator("", f.data[0], GLOBAL) == f.data[0].GetPtr()
		&& f.receiver->CountApplicators("InnerName", f.data[0], GLOBAL) == 0,
		"empty outer name deliberately prevents named registration despite an active real inner modifier");
}

/** Real status power conflicts block and revive a named status without replacing query ownership. */
static void ApplicatorQueryBlockedB12(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	std::vector<CObj<PFBaseApplicator>> statuses;
	for (int power : {1, 10})
	{
		auto* raw = new ApplicatorQueryDbB12<NDb::StatusApplicator>("/Mock/QueryConflict.xdb");
		raw->enabled.sString = "true"; raw->startCondition.sString = "true"; raw->lifeTime.sString = "-1";
		raw->formulaName = "Contested"; raw->power = power; raw->applicators.push_back(f.Stat("Child"));
		auto* flag = new ApplicatorQueryDbB12<NDb::FlagsApplicator>("/Mock/QueryConflictFlag.xdb");
		flag->flag = NDb::UNITFLAG_FORBIDMOVE; flag->lifeTime.sString = "-1"; raw->applicators.push_back(flag);
		NDb::Ptr<NDb::StatusApplicator> db = raw; statuses.push_back(f.Apply(db));
	}
	if (!statuses[0] || !statuses[1]) return;
	check(statuses[0]->IsBlocked() && !statuses[0]->IsEnabled() && statuses[1]->IsEnabled()
		&& f.receiver->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE) && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"actual competing status power blocks the lower effect and retains one real child contribution");
	check(f.receiver->FindApplicator("Contested", f.data[0], GLOBAL) == statuses[0].GetPtr()
		&& f.receiver->FindApplicator("Contested", f.data[0], GLOBAL, 1) == statuses[1].GetPtr()
		&& f.receiver->CountApplicators("Contested", f.data[0], GLOBAL) == 1,
		"blocked status remains in Find insertion order while Count sees only the enabled winner");
	statuses[1]->Stop();
	check(!statuses[0]->IsBlocked() && statuses[0]->IsEnabled()
		&& f.receiver->CountApplicators("Contested", f.data[0], GLOBAL) == 1
		&& f.receiver->FindApplicator("Contested", f.data[0], GLOBAL) == statuses[0].GetPtr()
		&& f.receiver->CountApplicators("Child", f.data[0], GLOBAL) == 1,
		"real winner removal revives the retained status and only its new live child named entry");
}

/** Context fallback, null dispatch equivalence and absent owner guards preserve the public contract. */
static void ApplicatorQueryContextsB12(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	auto a = f.Apply(f.Stat("Shared")), orphan = f.Apply(f.Stat("Orphan"), -1);
	if (!a || !orphan) return;
	check(!IsValid(orphan->GetAbilityOwner()) && orphan->IsEnabled(), "real ownerless named stat effect is valid engine input");
	check(f.receiver->CountApplicators("Orphan", a, UNIT_AND_SUMMONS) == 0
		&& f.receiver->FindApplicator("Orphan", a, UNIT_AND_SUMMONS) == a.GetPtr(),
		"ownerless candidate cannot dereference a master or match a real sender scope");
	check(f.receiver->CountApplicators("Orphan", nullptr, GLOBAL) == 1
		&& f.receiver->FindApplicator("Orphan", nullptr, GLOBAL) == orphan.GetPtr(),
		"global named queries require no formula context or sender ownership");
	check(f.receiver->CountApplicators("Orphan", a, DISPATCH) == 1
		&& f.receiver->FindApplicator("Orphan", a, DISPATCH) == orphan.GetPtr(),
		"null dispatch equals null dispatch as in the original, independent of sender or cast");
	TargetSelectorFormulaPars wrapped(a.GetPtr()), empty(nullptr), nested(&wrapped);
	for (auto* context : {static_cast<const IMiscFormulaPars*>(a.GetPtr()), static_cast<const IMiscFormulaPars*>(f.data[0].GetPtr()),
		static_cast<const IMiscFormulaPars*>(&wrapped), static_cast<const IMiscFormulaPars*>(&empty), static_cast<const IMiscFormulaPars*>(nullptr)})
	{
		check(f.receiver->FindApplicator("Absent", context, GLOBAL) == context
			&& f.receiver->CountApplicators("Absent", context, GLOBAL) == 0,
			"unmatched global lookup returns exact incoming formula wrapper or null identity");
		check(f.receiver->FindApplicator(nullptr, context, GLOBAL) == context
			&& f.receiver->CountApplicators(nullptr, context, GLOBAL) == 0,
			"null query name is rejected without reading it or substituting a context");
	}
	for (auto type : {UNIT, ABILITY, DISPATCH, UNIT_AND_SUMMONS})
	{
		check(f.receiver->FindApplicator("Shared", nullptr, type) == nullptr
			&& f.receiver->CountApplicators("Shared", nullptr, type) == 0,
			"non-global lookup rejects a missing applicator or ability context");
		check(f.receiver->FindApplicator("Shared", &empty, type) == nullptr
			&& f.receiver->CountApplicators("Shared", &empty, type) == 0,
			"invalid selector context returns its unwrapped misc pointer as Windows does");
		check(f.receiver->FindApplicator("Shared", &nested, type) == &wrapped
			&& f.receiver->CountApplicators("Shared", &nested, type) == 0,
			"selector context unwrap is one level only, not an invented recursive query context");
	}
	check(f.receiver->FindApplicator("Shared", &empty, GLOBAL) == a.GetPtr()
		&& f.receiver->CountApplicators("Shared", &empty, GLOBAL) == 1,
		"global search ignores an absent inner selector context");
	check(f.receiver->FindApplicator("Orphan", a, static_cast<ApplicatorSearchType>(99)) == orphan.GetPtr()
		&& f.receiver->CountApplicators("Orphan", a, static_cast<ApplicatorSearchType>(99)) == 0,
		"original invalid-enum asymmetry is retained: Find defaults global, Count has no matching case");
	check(f.receiver->FindApplicator("Shared", &wrapped, UNIT, 2147483647) == &wrapped,
		"out-of-range index returns the original wrapper without clamping to a matching effect");
}

/** Only live named ring entries qualify; instantaneous, rejected, expired and removed effects do not. */
static void ApplicatorQueryLifetimeB12(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	auto shortBuff = f.Apply(f.Stat("Short", "true", ".5"));
	auto longBuff = f.Apply(f.Stat("Long", "true", "2"));
	auto rejectedDb = f.Stat("Rejected");
	const_cast<NDb::StatModApplicator*>(rejectedDb.GetPtr())->startCondition.sString = "false";
	auto rejected = f.Apply(rejectedDb);
	auto* damageRaw = new ApplicatorQueryDbB12<NDb::DamageApplicator>("/Mock/QueryInstant.xdb");
	damageRaw->formulaName = "Instant"; damageRaw->enabled.sString = "true";
	damageRaw->damage.sString = "7"; damageRaw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	NDb::Ptr<NDb::DamageApplicator> damageDb = damageRaw;
	auto instant = f.Apply(damageDb);
	check(shortBuff && longBuff && rejected && instant && !rejected->IsAlive() && !instant->IsAlive()
		&& f.receiver->GetHealth() == 993 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 26,
		"real instant damage and rejected buff complete without persistent named or modifier ownership");
	for (const char* name : {"Instant", "Rejected"})
		check(f.receiver->FindApplicator(name, f.data[0], GLOBAL) == f.data[0].GetPtr()
			&& f.receiver->CountApplicators(name, f.data[0], GLOBAL) == 0,
			"completed instant and rejected effects are not searched in applied or sent history");
	check(f.receiver->CountApplicators("Short", nullptr, GLOBAL) == 1
		&& f.receiver->CountApplicators("Long", nullptr, GLOBAL) == 1,
		"normal activation registers each live named buff before world stepping");
	isolated.world->Step(0, 0); isolated.world->Step(.25f, .25f);
	check(f.receiver->CountApplicators("Short", nullptr, GLOBAL) == 1
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 26,
		"real world keeps a named effect searchable before its authored lifetime boundary");
	isolated.world->Step(.25f, .25f);
	check(f.receiver->CountApplicators("Short", nullptr, GLOBAL) == 0
		&& f.receiver->FindApplicator("Short", f.data[0], GLOBAL) == f.data[0].GetPtr()
		&& f.receiver->CountApplicators("Long", nullptr, GLOBAL) == 1
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"exact expiry removes the named entry and stat effect while retaining the later sibling");
	isolated.world->Step(1.5f, 1.5f);
	check(f.receiver->CountApplicators("Long", nullptr, GLOBAL) == 0
		&& f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 && f.casts[0]->GetActiveApplicatorsCount() == 0,
		"isolated world expires the last named effect with balanced actual ability ownership");
	check(shortBuff && !shortBuff->IsAlive() && longBuff && !longBuff->IsAlive(),
		"fixture and history references do not make retired named effects searchable again");
}

/** Entry point requires existing B8 loaded-world fixtures and no accumulated-world stepping. */
static void ApplicatorQueriesB12(Checks& check)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	auto* logic = new NDb::SessionLogicRoot;
	logic->aiLogic = new NDb::AILogicParameters;
	logic->unitLogicParameters = new NDb::UnitsLogicParameters;
	auto* root = new NDb::SessionRoot; root->logicRoot = logic;
	NDb::SessionRoot::InitRoot(root);
	ApplicatorQuerySelectionB12(check);
	ApplicatorQueryNamesB12(check);
	ApplicatorQueryBlockedB12(check);
	ApplicatorQueryContextsB12(check);
	ApplicatorQueryLifetimeB12(check);
	NDb::SessionRoot::InitRoot(saved);
}
