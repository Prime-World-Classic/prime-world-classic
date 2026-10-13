/** Real loaded DB identities keep same-resource and authored conflict-list tests distinct. */
struct StatusStackDbB12 : NDb::StatusApplicator
{
	explicit StatusStackDbB12(const char* id)
	{
		SetDbId(NDb::DBID(id)); ProbeResourceCache::MarkLoaded(this);
		enabled.sString = "true"; startCondition.sString = "true"; lifeTime.sString = "10";
	}
};

struct StatusStackProbeB12 : PFApplStatus
{
	explicit StatusStackProbeB12(const PFApplCreatePars& pars) : PFApplStatus(pars) {}
	bool Initialize() { return Init(); }
	bool Tick(float dt) { return Step(dt); }
	void Finish() { Stop(); }
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
};

/** Native generic wrapper Create is unsupported. This mock uses the original delegating
 * DB factory shape to test real stat children with outer metadata, without adding a factory port.
 */
struct StatusStackWrapperB12 : NDb::ApplicatorNameOverrider
{
	PFBaseApplicator* Create(const PFApplCreatePars& pars) const override { return applicator->Create(pars); }
};

/** Children use real effect factories and the normal ActivateApplicator ownership path. */
struct StatusStackFixtureB12
{
	CObj<B9CastHero> hero, other;
	NDb::Ptr<NDb::Ability> abilityDb;
	CObj<PFAbilityData> ability[2];
	CObj<PFAbilityInstance> instance[2];
	std::vector<CObj<StatusStackProbeB12>> apps;
	float initial;
	explicit StatusStackFixtureB12(PFWorld* world)
	{
		hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		other = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		other->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		abilityDb = new NDb::Ability;
		for (int i = 0; i < 2; ++i)
		{
			ability[i] = new PFAbilityData(i ? other.GetPtr() : hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
			instance[i] = new PFAbilityInstance(ability[i], Target(hero.GetPtr()), true);
		}
		initial = hero->GetStatValue(NDb::STAT_STRENGTH);
	}
	static NDb::StatModApplicator* Child(const char* add, bool dynamic = false, NDb::EStat stat = NDb::STAT_STRENGTH)
	{
		auto* db = new NDb::StatModApplicator;
		db->enabled.sString = "true"; db->lifeTime.sString = "-1";
		db->modifier.stat = stat; db->modifier.multValue.sString = "1";
		db->modifier.addValue.sString = add; db->dontUpdate = !dynamic;
		return db;
	}
	static StatusStackDbB12* Db(const char* id, const char* add, bool power = false)
	{
		auto* db = new StatusStackDbB12(id);
		db->applicators.push_back(Child(add));
		if (power)
		{
			auto* flag = new NDb::FlagsApplicator;
			flag->flag = NDb::UNITFLAG_FORBIDMOVE; flag->lifeTime.sString = "-1";
			db->applicators.push_back(flag);
		}
		return db;
	}
	CObj<StatusStackProbeB12> Begin(Checks& check, const NDb::StatusApplicator* db, int caster = 0)
	{
		const Target target(hero.GetPtr());
		PFApplCreatePars pars(instance[caster], target); pars.pDBAppl = db;
		CObj<StatusStackProbeB12> app = new StatusStackProbeB12(pars); apps.push_back(app);
		check(app->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), instance[caster]),
			"loaded status starts through actual ability activation");
		return app;
	}
	float Delta() const { return hero->GetStatValue(NDb::STAT_STRENGTH) - initial; }
	~StatusStackFixtureB12()
	{
		for (auto it = apps.rbegin(); it != apps.rend(); ++it) (*it)->Finish();
		hero->Cleanup(); other->Cleanup();
	}
};

static void StatusStackingB12(Checks& check, PFWorld* world)
{
	using F = StatusStackFixtureB12;
	for (int stack : {0, 1, 2})
	{
		F f(world);
		NDb::Ptr<NDb::StatusApplicator> db = F::Db("/Mock/StatusStack.xdb", "3");
		const_cast<NDb::StatusApplicator*>(db.GetPtr())->stackCount = stack;
		auto a = f.Begin(check, db), b = f.Begin(check, db);
		check(f.Delta() == (stack ? 6 : 3), "same caster stackCount counts prior matching statuses, not newcomer");
		check(a->IsComparableByValue() && b->IsComparableByValue(), "pure stat children select value merging");
		b->Finish(); check(f.Delta() == 3, "removing one equal merge or stack leaves one exact modifier");
		a->Finish(); check(f.Delta() == 0 && f.instance[0]->GetActiveApplicatorsCount() == 0,
			"same-caster final removal releases all child and ability ownership");
	}
	for (auto rule : {NDb::STACKINGRULE_UNSTACKABLE, NDb::STACKINGRULE_PARTIALLYSTACKABLE, NDb::STACKINGRULE_FULLYSTACKABLE})
		for (int stack : {0, 1})
		{
			F f(world); auto* raw = F::Db("/Mock/CrossStack.xdb", "3");
			raw->stackableAcrossUnits = rule; raw->stackCount = stack; NDb::Ptr<NDb::StatusApplicator> db = raw;
			auto a = f.Begin(check, db), b = f.Begin(check, db, 1);
			const bool stacks = rule == NDb::STACKINGRULE_FULLYSTACKABLE || (rule == NDb::STACKINGRULE_PARTIALLYSTACKABLE && stack);
			check(f.Delta() == (stacks ? 6 : 3), "cross-caster stacking follows full partial and unstackable policies");
			b->Finish(); check(f.Delta() == 3 && f.instance[1]->GetActiveApplicatorsCount() == 0,
				"cross-caster removal preserves the other ability ownership");
		}
	for (int flag : {int(NDb::STATUSFLAGS_STOPSAMECONFLICTED), int(NDb::STATUSFLAGS_STOPALLCONFLICTED)})
		for (int caster : {0, 1})
		{
			F f(world); auto* raw = F::Db("/Mock/StopConflict.xdb", "3");
			raw->flags = static_cast<NDb::EStatusFlags>(flag); NDb::Ptr<NDb::StatusApplicator> db = raw;
			auto old = f.Begin(check, db), fresh = f.Begin(check, db, caster);
			const bool stopped = caster ? flag == NDb::STATUSFLAGS_STOPALLCONFLICTED : flag == NDb::STATUSFLAGS_STOPSAMECONFLICTED;
			check(old->IsAlive() == !stopped && fresh->IsAlive(), "same and cross caster stop flags retain original separate policy");
			check(f.Delta() == 3, "stopping or merging conflicted stat children never doubles effect");
		}
	{
		F f(world); auto* raw = F::Db("/Mock/Refresh.xdb", "3");
		raw->stackCount = 1; raw->flags = NDb::STATUSFLAGS_UPDATELIFETIME;
		NDb::Ptr<NDb::StatusApplicator> db = raw;
		auto old = f.Begin(check, db); check(!old->Tick(3) && old->GetDuration() == 7, "status timer advances before refresh");
		auto fresh = f.Begin(check, db);
		check(old->GetLifetime() == 10 && old->GetDuration() == 10 && f.Delta() == 6,
			"allowed same-caster stack refresh copies authored lifetime and modified remaining duration");
		old->ForceUpdateLifetime(std::numeric_limits<float>::infinity(), 5);
		old->ForceUpdateLifetime(10, std::numeric_limits<float>::quiet_NaN());
		check(old->GetLifetime() == 10 && old->GetDuration() == 10, "nonfinite forced lifetime pairs do not partially mutate status timers");
	}
	for (bool longer : {false, true})
	{
		F f(world); auto* first = F::Db("/Mock/Power.xdb", "4", true);
		first->power = 7; NDb::Ptr<NDb::StatusApplicator> oldDb = first;
		auto old = f.Begin(check, oldDb);
		auto* second = F::Db("/Mock/Power.xdb", "9", true); second->power = 7;
		second->lifeTime.sString = longer ? "11" : "10"; NDb::Ptr<NDb::StatusApplicator> freshDb = second;
		auto fresh = f.Begin(check, freshDb);
		check(!old->IsComparableByValue() && !fresh->IsComparableByValue(), "nonstat gameplay children select whole-status power conflicts");
		check(old->IsBlocked() == longer && fresh->IsBlocked() == !longer && f.Delta() == (longer ? 9 : 4),
			"equal power keeps existing owner unless both finite timers give newcomer longer duration");
		auto winner = longer ? fresh : old; auto loser = longer ? old : fresh;
		winner->Finish(); winner->Finish();
		check(loser->IsAlive() && loser->IsEnabled() && !loser->IsBlocked() && f.Delta() == (longer ? 4 : 9),
			"winning status Stop restores blocked children once without stealing sibling ownership");
		check(f.instance[0]->GetActiveApplicatorsCount() == 3 && f.hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE),
			"resumed status owns its exact stat child flag child and parent ability counts");
	}
	{
		F f(world); auto* first = F::Db("/Mock/Infinite.xdb", "4", true);
		first->lifeTime.sString = "-1"; NDb::Ptr<NDb::StatusApplicator> aDb = first;
		auto a = f.Begin(check, aDb); auto* second = F::Db("/Mock/Infinite.xdb", "9", true);
		second->lifeTime.sString = "50"; NDb::Ptr<NDb::StatusApplicator> bDb = second;
		auto b = f.Begin(check, bDb);
		check(b->IsBlocked() && f.Delta() == 4, "equal power finite versus infinite timer does not invent a lifetime ordering");
	}
	{
		F f(world);
		NDb::Ptr<NDb::StatusApplicator> aDb = F::Db("/Mock/Value.xdb", "9"), bDb = F::Db("/Mock/Value.xdb", "5"), cDb = F::Db("/Mock/Value.xdb", "1");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb), c = f.Begin(check, cDb);
		check(f.Delta() == 9, "three-way merge updates winning magnitude as well as winner pointer");
		vector<const PFApplStatMod*> all, active;
		a->GetChildrenStatMods(all); b->GetChildrenStatMods(all); c->GetChildrenStatMods(all);
		a->GetChildrenStatMods(active, true); b->GetChildrenStatMods(active, true); c->GetChildrenStatMods(active, true);
		check(all.size() == 3 && active.size() == 1, "merged stat children remain owned while only the winner is unblocked");
		a->Block(); check(f.Delta() == 5, "blocking winner remerges surviving statuses immediately");
		a->GetChildrenStatMods(active); check(active.size() == 1, "disabled status does not publish child stat modifiers");
		a->Unblock(); check(f.Delta() == 9, "unblocking recreates children and restores strongest effect");
		a->Finish(); check(f.Delta() == 5, "winner removal promotes next magnitude");
		b->Finish(); check(f.Delta() == 1, "last merge peer removal unblocks the surviving child");
		c->Finish(); check(f.Delta() == 0 && f.instance[0]->GetActiveApplicatorsCount() == 0, "merge teardown leaves no orphan stat IDs or ability counts");
	}
	{
		F f(world); auto* multi = F::Db("/Mock/MultipleValues.xdb", "2");
		multi->applicators.push_back(F::Child("7")); NDb::Ptr<NDb::StatusApplicator> aDb = multi;
		NDb::Ptr<NDb::StatusApplicator> bDb = F::Db("/Mock/MultipleValues.xdb", "3");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == 7, "all same-stat children including duplicate children in one status compete for highest magnitude");
	}
	{
		F f(world); NDb::Ptr<NDb::StatusApplicator> aDb = F::Db("/Mock/SignedValue.xdb", "-8"), bDb = F::Db("/Mock/SignedValue.xdb", "5");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == -8, "value merging compares absolute effect and preserves winning debuff sign");
	}
	{
		F f(world); f.hero->SetHealth(100);
		auto* dynamic = new StatusStackDbB12("/Mock/DynamicValue.xdb");
		dynamic->applicators.push_back(F::Child("sLife / 100", true)); NDb::Ptr<NDb::StatusApplicator> aDb = dynamic;
		NDb::Ptr<NDb::StatusApplicator> bDb = F::Db("/Mock/DynamicValue.xdb", "5");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == 5, "dynamic loser starts blocked by larger static effect");
		f.hero->SetHealth(900); f.hero->Simulate(.01f);
		check(f.Delta() == 9, "actual unit stepping reevaluates blocked dynamic contenders without a winning-child notification");
		f.hero->SetHealth(200); f.hero->Simulate(.01f);
		check(f.Delta() == 5, "dynamic winner can relinquish ownership to a stronger static peer");
	}
	for (auto policy : {NDb::STOPBEHAVIOUR_STOPBYCHILD, NDb::STOPBEHAVIOUR_STOPBYALLCHILDS})
	{
		F f(world); auto* raw = F::Db("/Mock/ResumeChildren.xdb", "3"); raw->stopBehaviour = policy;
		NDb::Ptr<NDb::StatusApplicator> db = raw; auto a = f.Begin(check, db);
		a->Block(); a->Unblock();
		check(!a->Tick(0) && a->IsAlive() && f.Delta() == 3 && f.instance[0]->GetActiveApplicatorsCount() == 2,
			"re-enable owns a fresh child generation instead of completing from removed-child notifications");
	}
	{
		F f(world); auto* aRaw = F::Db("/Mock/ListA.xdb", "4", true);
		auto* bRaw = F::Db("/Mock/ListB.xdb", "9", true); bRaw->power = 10;
		NDb::Ptr<NDb::StatusApplicator> aDb = aRaw, bDb = bRaw;
		auto* outer = new NDb::ApplicatorsList; auto* inner = new NDb::ApplicatorsList;
		outer->applicatorLists.push_back(inner); inner->applicatorLists.push_back(outer);
		inner->applicators.push_back(aDb); bRaw->unstackableWith = outer;
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(a->IsBlocked() && f.Delta() == 9, "nested authored unstackable list matches loaded DB identity across different status types");
		NDb::Ptr<NDb::StatusApplicator> unrelated = F::Db("/Mock/ListUnrelated.xdb", "2");
		auto c = f.Begin(check, unrelated);
		check(c->IsEnabled() && f.Delta() == 11, "cyclic conflict lists terminate without inventing unrelated conflicts");
		b->Finish(); check(!a->IsBlocked() && f.Delta() == 6, "different-ID power conflict restores original owner on removal");
		inner->applicators.clear(); inner->applicatorLists.clear(); outer->applicatorLists.clear();
	}
	for (const char* condition : {"false", "cMissing", "1 / 0"})
	{
		F f(world); NDb::Ptr<NDb::StatusApplicator> aDb = F::Db("/Mock/RejectedStatus.xdb", "4", true);
		auto a = f.Begin(check, aDb); auto* rejected = F::Db("/Mock/RejectedStatus.xdb", "9", true);
		rejected->startCondition.sString = condition; rejected->flags = NDb::STATUSFLAGS_STOPSAMECONFLICTED;
		NDb::Ptr<NDb::StatusApplicator> bDb = rejected; auto b = f.Begin(check, bDb);
		check(!b->IsAlive() && a->IsAlive() && f.Delta() == 4 && f.instance[0]->GetActiveApplicatorsCount() == 3,
			"checked rejected start neither stops an existing conflict nor steals its ability count");
	}
	{
		F f(world); f.hero->SetHealth(100);
		NDb::Ptr<NDb::StatusApplicator> aDb = F::Db("/Mock/DisabledPower.xdb", "4", true);
		auto a = f.Begin(check, aDb); auto* higher = F::Db("/Mock/DisabledPower.xdb", "9", true);
		higher->power = 10; higher->enabled.sString = "sLife > 500";
		NDb::Ptr<NDb::StatusApplicator> bDb = higher; auto b = f.Begin(check, bDb);
		check(a->IsBlocked() && !b->IsEnabled() && f.Delta() == 0,
			"stock power ownership persists even when the stronger authored status is disabled");
		f.hero->SetHealth(900); b->Tick(0);
		check(b->IsEnabled() && a->IsBlocked() && f.Delta() == 9, "authored enable creates only stronger status children");
		f.hero->SetHealth(100); b->Tick(0);
		check(!b->IsEnabled() && a->IsBlocked() && f.Delta() == 0, "authored disable does not release stock power ownership before Stop");
		b->Finish(); check(a->IsEnabled() && f.Delta() == 4, "disabled blocker Stop releases the original status");
	}
	{
		F f(world); auto* aRaw = F::Db("/Mock/AlwaysMerge.xdb", "4", true);
		auto* bRaw = F::Db("/Mock/AlwaysMerge.xdb", "9", true);
		aRaw->flags = bRaw->flags = NDb::STATUSFLAGS_ALWAYSMERGE;
		NDb::Ptr<NDb::StatusApplicator> aDb = aRaw, bDb = bRaw;
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(a->IsComparableByValue() && b->IsComparableByValue() && !a->IsBlocked() && !b->IsBlocked() && f.Delta() == 9,
			"AlwaysMerge keeps nonstat children active but merges actual stat effects");
		b->Finish(); check(f.Delta() == 4 && f.hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE),
			"AlwaysMerge peer removal retains surviving nonstat flag ownership");
	}
	{
		F f(world); auto* aRaw = F::Db("/Mock/PowerChain.xdb", "2", true);
		auto* bRaw = F::Db("/Mock/PowerChain.xdb", "4", true);
		auto* cRaw = F::Db("/Mock/PowerChain.xdb", "8", true);
		aRaw->power = 1; bRaw->power = 2; cRaw->power = 3;
		aRaw->lifeTime.sString = "30"; bRaw->lifeTime.sString = "20"; cRaw->lifeTime.sString = "1";
		aRaw->stopBehaviour = bRaw->stopBehaviour = NDb::STOPBEHAVIOUR_STOPBYALLCHILDS;
		NDb::Ptr<NDb::StatusApplicator> aDb = aRaw, bDb = bRaw, cDb = cRaw;
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb), c = f.Begin(check, cDb);
		check(a->IsBlocked() && b->IsBlocked() && f.Delta() == 8, "successive higher-power statuses form owned suppression chain");
		check(!a->Tick(0) && !b->Tick(0), "suppressed child-driven statuses do not mistake removed children for natural completion");
		f.hero->Simulate(1);
		check(!c->IsAlive() && b->IsAlive() && !b->IsBlocked() && a->IsBlocked() && f.Delta() == 4,
			"actual unit stepping expires strongest status and resumes exactly its predecessor");
		b->Finish(); check(a->IsAlive() && !a->IsBlocked() && f.Delta() == 2, "suppression chain teardown reaches original owner");
		a->Finish(); check(f.instance[0]->GetActiveApplicatorsCount() == 0 && f.Delta() == 0,
			"expired suppression chain releases all actual ability counts");
	}
	for (const char* failure : {"cMissing", "1 / 0", "1e39", "overflow"})
	{
		F f(world); auto* raw = new StatusStackDbB12("/Mock/InvalidMerge.xdb");
		auto* child = F::Child("9", true); raw->applicators.push_back(child);
		NDb::Ptr<NDb::StatusApplicator> aDb = raw, bDb = F::Db("/Mock/InvalidMerge.xdb", "3");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == 9, "dynamic merge winner establishes a real last-good modifier");
		const int id = f.hero->GetStat(NDb::STAT_STRENGTH)->GetModifiers().back().id;
		if (strcmp(failure, "overflow") == 0) child->modifier.multValue.sString = "1e38";
		else child->modifier.addValue.sString = failure;
		f.hero->Simulate(.01f);
		const auto* retained = f.hero->GetStat(NDb::STAT_STRENGTH)->Retrieve(id);
		check(f.Delta() == 9 && retained && retained->active,
			"invalid formula or float arithmetic retains the applied last-good winner and exact modifier ID");
		child->modifier.multValue.sString = "1"; child->modifier.addValue.sString = "2";
		f.hero->Simulate(.01f); check(f.Delta() == 3, "valid recovery reranks a formerly invalid dynamic winner");
	}
	for (const char* failure : {"cMissing", "1 / 0", "1e39", "overflow"})
	{
		F f(world); NDb::Ptr<NDb::StatusApplicator> aDb = F::Db("/Mock/InvalidContender.xdb", "3");
		auto a = f.Begin(check, aDb);
		const int modifiersBefore = f.hero->GetStat(NDb::STAT_STRENGTH)->GetModifiers().size();
		auto* raw = new StatusStackDbB12("/Mock/InvalidContender.xdb");
		auto* child = F::Child(strcmp(failure, "overflow") == 0 ? "0" : failure, true);
		if (strcmp(failure, "overflow") == 0) child->modifier.multValue.sString = "1e38";
		raw->applicators.push_back(child); NDb::Ptr<NDb::StatusApplicator> bDb = raw;
		auto b = f.Begin(check, bDb);
		check(f.Delta() == 3 && f.hero->GetStat(NDb::STAT_STRENGTH)->GetModifiers().size() == modifiersBefore,
			"invalid initial contender cannot publish a neutral fallback or corrupt the valid winner");
		child->modifier.multValue.sString = "1"; child->modifier.addValue.sString = "9";
		f.hero->Simulate(.01f); check(f.Delta() == 9, "blocked initially invalid dynamic contender can recover through checked status stepping");
	}
	{
		F f(world); auto* raw = new StatusStackDbB12("/Mock/NeutralContext.xdb");
		raw->applicators.push_back(F::Child("sStrength / 2", true));
		NDb::Ptr<NDb::StatusApplicator> aDb = raw, bDb = F::Db("/Mock/NeutralContext.xdb", "6");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == f.initial / 2, "dynamic magnitude formulas observe neutral stats with all competing effects temporarily inactive");
		for (int i = 0; i < 3; ++i) f.hero->Simulate(.01f);
		check(f.Delta() == f.initial / 2, "neutral-context merging does not amplify its own dynamic coefficient on repeated steps");
	}
	{
		F f(world); f.hero->SetHealth(100);
		auto* raw = new StatusStackDbB12("/Mock/ConditionalChild.xdb");
		auto* child = F::Child("9"); child->enabled.sString = "sLife > 500"; raw->applicators.push_back(child);
		NDb::Ptr<NDb::StatusApplicator> aDb = raw, bDb = F::Db("/Mock/ConditionalChild.xdb", "3");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == 3, "merge cannot bypass a child's authored false enable condition");
		f.hero->SetHealth(900); f.hero->Simulate(.01f);
		check(f.Delta() == 9, "blocked conditional contender is admitted when its checked enable becomes true");
		f.hero->SetHealth(100); f.hero->Simulate(.01f);
		check(f.Delta() == 3, "false enable hands ownership back to the other eligible contender");
	}
	{
		F f(world); auto* stat = f.hero->GetStat(NDb::STAT_STRENGTH);
		const int foreign = stat->AddModifier(1, 11, 999);
		auto* aRaw = new StatusStackDbB12("/Mock/ForeignModifier.xdb");
		auto* top = F::Child("0"); top->modifier.multValue.sString = "2"; top->modifier.topModifier = true;
		aRaw->applicators.push_back(top);
		NDb::Ptr<NDb::StatusApplicator> aDb = aRaw, bDb = F::Db("/Mock/ForeignModifier.xdb", "35");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == 46 && stat->Retrieve(foreign) && stat->Retrieve(foreign)->active,
			"neutral previews preserve unrelated modifiers and original top-modifier magnitude arithmetic");
		b->Finish(); a->Finish(); check(f.Delta() == 11, "merge cleanup removes only owned IDs");
		stat->RemoveModifier(foreign);
	}
	{
		F f(world); auto* raw = F::Db("/Mock/ReentrantStatus.xdb", "3", true);
		raw->applicators.push_back(F::Child("7")); NDb::Ptr<NDb::StatusApplicator> db = raw;
		CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		bool stopped = false;
		listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (!stopped && event->GetType() == NDb::BASEUNITEVENT_FORBIDMOVE)
			{
				stopped = true; f.apps.back()->Finish();
			}
			return 0u;
		};
		f.hero->AddEventListener(listener.GetPtr());
		auto app = f.Begin(check, db); f.hero->RemoveEventListener(listener.GetPtr());
		check(stopped && !app->IsAlive() && f.Delta() == 0 && f.instance[0]->GetActiveApplicatorsCount() == 0
			&& !f.hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE),
			"reentrant parent Stop during child Enable cannot orphan in-flight or later-created children");
	}
	{
		F f(world); auto* raw = new StatusStackDbB12("/Mock/InvalidDelta.xdb");
		auto* child = F::Child("9", true); raw->applicators.push_back(child);
		NDb::Ptr<NDb::StatusApplicator> aDb = raw, bDb = F::Db("/Mock/InvalidDelta.xdb", "3");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb); child->modifier.addValue.sString = "1";
		for (float dt : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
			check(!a->Tick(dt) && f.Delta() == 9 && a->GetDuration() == 10,
				"invalid elapsed time cannot rerank merged values or change status lifetime");
	}
	{
		B8BehaviourForeignWorld isolated(check);
		if (isolated.world)
		{
			F f(isolated.world); auto* aRaw = F::Db("/Mock/WorldStatus.xdb", "3", true);
			auto* bRaw = F::Db("/Mock/WorldStatus.xdb", "9", true);
			aRaw->lifeTime.sString = "2"; bRaw->lifeTime.sString = ".5"; bRaw->power = 10;
			NDb::Ptr<NDb::StatusApplicator> aDb = aRaw, bDb = bRaw;
			auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
			check(a->IsBlocked() && f.Delta() == 9, "loaded-world statuses begin with one power winner");
			isolated.world->Step(0, 0); isolated.world->Step(.5f, .5f);
			check(!b->IsAlive() && a->IsAlive() && a->IsEnabled() && f.Delta() == 3,
				"real PFWorld Step expires the winner and restores the suppressed predecessor");
			isolated.world->Step(2, 2);
			check(!a->IsAlive() && f.Delta() == 0 && f.instance[0]->GetActiveApplicatorsCount() == 0,
				"real PFWorld Step retires restored status and all child ownership");
		}
	}
	for (bool innerEnabled : {false, true})
	{
		F f(world); auto* raw = new StatusStackDbB12("/Mock/WrappedMerge.xdb");
		raw->flags = NDb::STATUSFLAGS_ALWAYSMERGE;
		auto* inner = F::Child("9"); inner->enabled.sString = innerEnabled ? "true" : "false";
		auto* outer = new StatusStackWrapperB12; outer->applicator = inner;
		outer->enabled.sString = innerEnabled ? "false" : "true"; raw->applicators.push_back(outer);
		NDb::Ptr<NDb::StatusApplicator> aDb = raw, bDb = F::Db("/Mock/WrappedMerge.xdb", "3");
		auto a = f.Begin(check, aDb), b = f.Begin(check, bDb);
		check(f.Delta() == (innerEnabled ? 3 : 9), "merged wrapper eligibility belongs to outer enabled metadata, not unwrapped stat data");
		outer->enabled.sString = innerEnabled ? "true" : "false"; f.hero->Simulate(.01f);
		check(f.Delta() == (innerEnabled ? 9 : 3), "dynamic merge retains the parent's outer-wrapper enabled policy");
	}
}
