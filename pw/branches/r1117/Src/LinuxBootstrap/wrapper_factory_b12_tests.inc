/** Loaded identities only: Create is the real NDb wrapper factory, never a mock override. */
struct WrapperFactoryDbB12 : NDb::ApplicatorNameOverrider
{
	WrapperFactoryDbB12(const char* id, const NDb::BaseApplicator* inner)
	{
		SetDbId(NDb::DBID(id)); ProbeResourceCache::MarkLoaded(this);
		applicator = inner; enabled.sString = "true"; formulaName = "OuterWrapper";
	}
};

struct WrapperFactoryStatusDbB12 : NDb::StatusApplicator
{
	WrapperFactoryStatusDbB12()
	{
		SetDbId(NDb::DBID("/Mock/WrapperLeafStatus.xdb")); ProbeResourceCache::MarkLoaded(this);
		enabled.sString = "true"; startCondition.sString = "true"; lifeTime.sString = "10";
		formulaName = "InnerStatus";
	}
};

/** Real ability endpoints and teardown expose ownership errors rather than simulating buffs. */
struct WrapperFactoryFixtureB12
{
	CObj<B9CastHero> sender, receiver;
	NDb::Ptr<NDb::Ability> abilityDb;
	CObj<PFAbilityData> ability;
	CObj<PFAbilityInstance> instance;
	std::vector<CObj<PFBaseApplicator>> active;
	float senderStrength, receiverStrength;
	explicit WrapperFactoryFixtureB12(PFWorld* world)
	{
		sender = B9MaleHero(world, NDb::FACTION_FREEZE); receiver = B9MaleHero(world, NDb::FACTION_FREEZE);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		sender->SetHealth(400); receiver->SetHealth(800);
		auto* raw = new NDb::Ability; raw->manaCost.sString = "0";
		auto* constants = new NDb::UnitConstantsContainer; auto* bonus = new NDb::UnitConstant;
		bonus->name = "WrapperBonus"; bonus->var.sString = "2";
		constants->vars.push_back(bonus); raw->constants = constants; abilityDb = raw;
		ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		instance = new PFAbilityInstance(ability, Target(receiver.GetPtr()), true);
		senderStrength = sender->GetStatValue(NDb::STAT_STRENGTH);
		receiverStrength = receiver->GetStatValue(NDb::STAT_STRENGTH);
	}
	static NDb::StatModApplicator* Stat(const char* add = "7")
	{
		auto* db = new NDb::StatModApplicator; db->lifeTime.sString = "10";
		db->enabled.sString = "true"; db->startCondition.sString = "true"; db->formulaName = "InnerStat";
		db->modifier.stat = NDb::STAT_STRENGTH; db->modifier.multValue.sString = "1";
		db->modifier.addValue.sString = add; db->dontUpdate = false;
		return db;
	}
	CObj<PFBaseApplicator> Create(const NDb::BaseApplicator* db, const CPtr<PFBaseApplicator>& parent = CPtr<PFBaseApplicator>())
	{
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(instance, target, parent); pars.pDBAppl = db;
		return CreateApplicator(pars);
	}
	bool Activate(const CObj<PFBaseApplicator>& app)
	{
		if (!app) return false;
		active.push_back(app); return ActivateApplicator(app, instance);
	}
	float Delta() const { return receiver->GetStatValue(NDb::STAT_STRENGTH) - receiverStrength; }
	~WrapperFactoryFixtureB12()
	{
		for (auto it = active.rbegin(); it != active.rend(); ++it) (*it)->Stop();
		receiver->Cleanup(); sender->Cleanup();
	}
};

static void WrapperFactoryB12(Checks& check, PFWorld* world)
{
	using F = WrapperFactoryFixtureB12;
	for (bool status : {false, true}) for (int depth : {1, 2, 4, 128})
	{
		F f(world); NDb::Ptr<NDb::BaseApplicator> leaf;
		if (status)
		{
			auto* db = new WrapperFactoryStatusDbB12;
			db->applicators.push_back(F::Stat("sLife/100 + tLife/100 + cWrapperBonus")); leaf = db;
		}
		else leaf = F::Stat("sLife/100 + tLife/100 + cWrapperBonus");
		const_cast<NDb::BaseApplicator*>(leaf.GetPtr())->enabled.sString = "cMissingInnerMustNotBeEvaluated";
		NDb::Ptr<NDb::BaseApplicator> chain = leaf;
		std::vector<NDb::Ptr<NDb::ApplicatorNameOverrider>> wrappers;
		for (int i = 0; i < depth; ++i)
		{
			char id[80]; std::snprintf(id, sizeof(id), "/Mock/WrapperDepth%d.xdb", i);
			auto* db = new WrapperFactoryDbB12(id, chain); db->enabled.sString = "cMissingMiddleMustNotBeEvaluated";
			wrappers.push_back(db); chain = db;
		}
		auto* outer = const_cast<NDb::ApplicatorNameOverrider*>(wrappers.back().GetPtr());
		outer->enabled.sString = "true";
		// Base lookup is safe to inspect even while the baseline factory still returns null.
		const Target target(f.receiver.GetPtr());
		PFApplCreatePars pars(f.instance, target); pars.pDBAppl = chain;
		CObj<PFBaseApplicator> lookup = new PFBaseApplicator(pars);
		check(lookup->GetDBBase().GetPtr() == leaf.GetPtr(), "typed descriptor lookup resolves the complete wrapper chain");
		check(lookup->GetApplicatorName() == "OuterWrapper", "outermost wrapper owns formula name at every nesting depth");
		auto app = f.Create(chain);
		check(app && (status ? dynamic_cast<PFApplStatus*>(app.GetPtr()) != 0 : dynamic_cast<PFApplStatMod*>(app.GetPtr()) != 0),
			"native wrapper factory constructs the real terminal gameplay subtype");
		if (!app) continue;
		check(app->GetDBBase().GetPtr() == leaf.GetPtr() && app->GetApplicatorName() == "OuterWrapper"
			&& app->GetAbility() == f.instance && app->GetAbilityOwner() == f.sender && app->GetReceiver() == f.receiver,
			"delegation preserves outer metadata and exact ability sender receiver parameters");
		check(f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"factory and Init do not activate or account for the effect prematurely");
		check(f.Activate(app) && f.Delta() == 14 && f.instance->GetActiveApplicatorsCount() == (status ? 2 : 1),
			"real activation evaluates leaf formulas with live endpoints and ability constants");
		outer->enabled.sString = "false"; check(!app->Step(0) && !app->IsEnabled() && f.Delta() == 0,
			"outer authored disable removes real stat or status child effects");
		outer->enabled.sString = "true"; check(!app->Step(0) && app->IsEnabled() && f.Delta() == 14,
			"outer enable restores effect without consulting middle or leaf enabled fields");
		app->Stop(); app->Stop();
		check(f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"wrapper Stop releases exact real child and ability ownership once");
	}
	for (bool status : {false, true}) for (bool inner : {false, true}) for (bool outer : {false, true})
	{
		F f(world); NDb::Ptr<NDb::BaseApplicator> leaf;
		if (status)
		{
			auto* db = new WrapperFactoryStatusDbB12; db->applicators.push_back(F::Stat()); leaf = db;
		}
		else leaf = F::Stat();
		const_cast<NDb::BaseApplicator*>(leaf.GetPtr())->enabled.sString = inner ? "true" : "false";
		auto* raw = new WrapperFactoryDbB12("/Mock/BooleanWrapper.xdb", leaf);
		raw->enabled.sString = outer ? "true" : "false"; raw->formulaName = "";
		NDb::Ptr<NDb::ApplicatorNameOverrider> db = raw; auto app = f.Create(db);
		check(app && f.Activate(app), "real DB wrapper factory admits stat and status for every enabled combination");
		if (!app) continue;
		check(app->IsAlive() && app->IsEnabled() == outer && f.Delta() == (outer ? 7 : 0),
			"only the outer condition controls initial status and stat enable ownership");
		check(app->GetApplicatorName().empty(), "empty outer formula name is intentional and never falls back to inner name");
		check(f.instance->GetActiveApplicatorsCount() == (status && outer ? 2 : 1),
			"disabled wrapped buffs remain alive without owning uncreated children");
	}
	for (bool ownerTarget : {false, true})
	{
		F f(world); auto* inner = F::Stat();
		inner->applyTarget = ownerTarget ? NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET : NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		NDb::Ptr<NDb::StatModApplicator> leaf = inner;
		auto* raw = new WrapperFactoryDbB12("/Mock/TargetWrapper.xdb", leaf);
		raw->applyTarget = ownerTarget ? NDb::APPLICATORAPPLYTARGET_ABILITYOWNER : NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET;
		NDb::Ptr<NDb::ApplicatorNameOverrider> db = raw; auto app = f.Create(db);
		check(app && f.Activate(app), "wrapped stat target routing goes through real Init and ActivateApplicator");
		if (!app) continue;
		check(app->GetReceiver() == (ownerTarget ? f.sender : f.receiver)
			&& f.Delta() == (ownerTarget ? 0 : 7)
			&& f.sender->GetStatValue(NDb::STAT_STRENGTH) == f.senderStrength + (ownerTarget ? 7 : 0),
			"outer applyTarget overrides conflicting inner routing without changing effect data");
	}
	for (const char* condition : {"sLife > 500", "cMissing", "1/0", "1e39", "compiled-only"})
	{
		F f(world); NDb::Ptr<NDb::StatModApplicator> leaf = F::Stat();
		auto* raw = new WrapperFactoryDbB12("/Mock/CheckedWrapper.xdb", leaf);
		if (strcmp(condition, "compiled-only") == 0) { raw->enabled.sString = ""; raw->enabled.compiledString = "unsupported-bytecode"; }
		else raw->enabled.sString = condition;
		NDb::Ptr<NDb::ApplicatorNameOverrider> db = raw; auto app = f.Create(db);
		check(app && f.Activate(app), "checked outer conditions use the actual factory-created buff");
		if (!app) continue;
		check(app->IsAlive() && !app->IsEnabled() && f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 1,
			"false invalid nonfinite and compiled-only outer conditions cannot mutate the receiver");
		raw->enabled.compiledString = ""; raw->enabled.sString = "sLife > 500"; f.sender->SetHealth(600);
		check(!app->Step(0) && app->IsEnabled() && f.Delta() == 7,
			"live checked outer condition can recover without rebuilding the wrapper");
	}
	for (int mode = 0; mode < 5; ++mode)
	{
		F f(world);
		auto* aRaw = new WrapperFactoryDbB12("/Mock/MalformedWrapperA.xdb", 0);
		auto* bRaw = new WrapperFactoryDbB12("/Mock/MalformedWrapperB.xdb", 0);
		NDb::Ptr<NDb::ApplicatorNameOverrider> a = aRaw, b = bRaw;
		if (mode == 1) aRaw->applicator = b;
		if (mode == 2) aRaw->applicator = a;
		if (mode == 3) { aRaw->applicator = b; bRaw->applicator = a; }
		if (mode == 4) aRaw->applicator = new NDb::TriggerApplicator;
		check(!f.Create(a) && f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"missing nested missing cyclic and unsupported terminal descriptors fail before effect activation");
		const Target target(f.receiver.GetPtr());
		PFApplCreatePars pars(f.instance, target); pars.pDBAppl = a;
		CObj<PFBaseApplicator> lookup = new PFBaseApplicator(pars);
		if (mode != 4) check(!lookup->GetDBBase(), "malformed descriptor lookup returns an empty reference instead of a wrong typed wrapper");
		check(lookup->GetApplicatorName() == "OuterWrapper", "outer name remains readable even when its inner chain is malformed");
		aRaw->applicator = 0; bRaw->applicator = 0;
	}
	{
		F f(world); NDb::Ptr<NDb::StatModApplicator> leaf = F::Stat();
		NDb::Ptr<NDb::ApplicatorNameOverrider> outer = new WrapperFactoryDbB12("/Mock/CreationParameters.xdb", leaf);
		const Target target(f.receiver.GetPtr());
		PFApplCreatePars pars(f.instance, target);
		check(!outer->Create(pars), "direct wrapper factory rejects missing descriptor ownership in creation parameters");
		pars.pDBAppl = new NDb::StatusApplicator;
		check(!outer->Create(pars), "direct wrapper factory rejects mismatched typed creation parameters before DBLinker construction");
	}
	{
		F f(world); auto* raw = new WrapperFactoryStatusDbB12;
		raw->applicators.push_back(F::Stat("4")); NDb::Ptr<NDb::StatusApplicator> leaf = raw;
		auto* aRaw = new WrapperFactoryDbB12("/Mock/StatusAliasA.xdb", leaf); aRaw->formulaName = "AliasA";
		auto* bRaw = new WrapperFactoryDbB12("/Mock/StatusAliasB.xdb", leaf); bRaw->formulaName = "AliasB";
		NDb::Ptr<NDb::ApplicatorNameOverrider> aDb = aRaw, bDb = bRaw;
		auto a = f.Create(aDb), b = f.Create(bDb);
		check(a && b && f.Activate(a) && f.Activate(b), "distinct wrapper aliases create the same actual status descriptor");
		if (a && b)
		{
			check(a->GetApplicatorName() == "AliasA" && b->GetApplicatorName() == "AliasB" && f.Delta() == 4,
				"status stacking uses inner DB identity while each wrapper retains its independent outer name");
			b->Stop(); check(f.Delta() == 4 && f.instance->GetActiveApplicatorsCount() == 2,
				"removing one wrapped status leaves its merged peer and child owned");
		}
	}
	{
		F f(world); NDb::Ptr<NDb::StatModApplicator> child = F::Stat("4");
		NDb::Ptr<NDb::ApplicatorNameOverrider> childWrapper = new WrapperFactoryDbB12("/Mock/ChildWrapper.xdb", child);
		auto* statusRaw = new WrapperFactoryStatusDbB12; statusRaw->applicators.push_back(childWrapper);
		NDb::Ptr<NDb::StatusApplicator> status = statusRaw;
		NDb::Ptr<NDb::ApplicatorNameOverrider> wrapper = new WrapperFactoryDbB12("/Mock/ParentWrapper.xdb", status);
		auto app = f.Create(wrapper);
		check(app && f.Activate(app), "generic status factory creates a real wrapped stat child through normal CreateAndActivateApplicators");
		struct Children
		{
			PFBaseApplicator* parent; int count = 0; bool valid = true;
			void operator()(CObj<PFBaseApplicator>& child)
			{
				if (child->GetParentAppl().GetPtr() != parent) return;
				++count; valid = valid && dynamic_cast<PFApplStatMod*>(child.GetPtr()) && child->GetApplicatorName() == "OuterWrapper";
			}
		} children{app.GetPtr()};
		if (app)
		{
			f.receiver->ForAllAppliedApplicators(children);
			check(children.count == 1 && children.valid && f.Delta() == 4 && f.instance->GetActiveApplicatorsCount() == 2,
				"real child retains status parent relationship, outer name and exact root ability ownership");
			app->Stop(); check(f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
				"generic status Stop reaches and releases its factory-created wrapped child");
		}
	}
	{
		F f(world); auto* statusRaw = new WrapperFactoryStatusDbB12;
		statusRaw->applicators.push_back(F::Stat("7")); NDb::Ptr<NDb::StatusApplicator> status = statusRaw;
		NDb::Ptr<NDb::ApplicatorNameOverrider> wrapper = new WrapperFactoryDbB12("/Mock/PaidWrapper.xdb", status);
		auto* talentRaw = new NDb::Talent; talentRaw->manaCost.sString = "25"; talentRaw->cooldownTime.sString = "0";
		talentRaw->naftaCost = 0; talentRaw->isUnitFreeAfterCast = true; talentRaw->applicators.push_back(wrapper);
		NDb::Ptr<NDb::Talent> talentDb = talentRaw;
		CObj<PFTalent> talent = new PFTalent(f.sender.GetPtr(), talentDb, 1, 0, 0, false);
		check(talent->Activate(), "real paid talent with authored wrapper activates");
		CObj<PFAbilityInstance> cast = f.sender->UseTalent(talent, Target(f.sender.GetPtr()));
		check(cast && f.sender->GetMana() == 175 && f.sender->GetStatValue(NDb::STAT_STRENGTH) == f.senderStrength + 7,
			"real paid casting dispatch reaches the wrapped status and stat child exactly once");
		if (cast)
		{
			check(cast->GetActiveApplicatorsCount() == 2, "paid cast owns both actual status and wrapped gameplay child");
			cast->RemoveApplicatorsFrom(f.sender.GetPtr());
			check(cast->GetActiveApplicatorsCount() == 0 && f.sender->GetStatValue(NDb::STAT_STRENGTH) == f.senderStrength,
				"paid cast removal balances all factory-created wrapper gameplay ownership");
		}
	}
	for (int mode = 0; mode < 3; ++mode)
	{
		F f(world); NDb::Ptr<NDb::StatModApplicator> siblingDb = F::Stat("3");
		auto sibling = f.Create(siblingDb);
		check(sibling && f.Activate(sibling) && f.Delta() == 3, "existing real stat sibling owns one ability count before wrapped rejection");
		auto* leafRaw = F::Stat();
		if (mode == 0) leafRaw->lifeTime.sString = "1/0";
		if (mode == 1) leafRaw->startCondition.sString = "false";
		if (mode == 2) { leafRaw->dontUpdate = true; leafRaw->modifier.addValue.sString = "cMissing"; }
		NDb::Ptr<NDb::StatModApplicator> leaf = leafRaw;
		NDb::Ptr<NDb::ApplicatorNameOverrider> outer = new WrapperFactoryDbB12("/Mock/RejectedLeaf.xdb", leaf);
		auto app = f.Create(outer);
		if (mode == 0) check(!app, "outer true cannot bypass the leaf checked lifetime during real Init");
		else
		{
			check(app && f.Activate(app) && !app->IsAlive(), "leaf startCondition and static coefficients still control terminal admission through a wrapper");
			if (app) app->Stop();
		}
		check(sibling->IsAlive() && f.Delta() == 3 && f.instance->GetActiveApplicatorsCount() == 1,
			"rejected wrapped leaf cannot mutate the receiver or steal existing sibling ownership");
	}
	{
		// Observe through unit state only: gameplay rings, not fixture-held statuses, own these buffs.
		B8BehaviourForeignWorld isolated(check);
		if (isolated.world)
		{
			F f(isolated.world);
			auto* low = new WrapperFactoryStatusDbB12; low->lifeTime.sString = "2";
			auto* high = new WrapperFactoryStatusDbB12; high->lifeTime.sString = ".5"; high->power = 10;
			low->applicators.push_back(F::Stat("3")); high->applicators.push_back(F::Stat("9"));
			for (auto* db : {low, high})
			{
				auto* flag = new NDb::FlagsApplicator; flag->enabled.sString = "true";
				flag->flag = NDb::UNITFLAG_FORBIDMOVE; flag->lifeTime.sString = "-1";
				db->applicators.push_back(flag);
			}
			vector<NDb::Ptr<NDb::BaseApplicator>> authored;
			authored.push_back(new WrapperFactoryDbB12("/Mock/WorldLow.xdb", low));
			authored.push_back(new WrapperFactoryDbB12("/Mock/WorldHigh.xdb", high));
			CreateAndActivateApplicators(authored, f.instance, Target(f.receiver.GetPtr()), CPtr<PFBaseApplicator>());
			check(f.Delta() == 9 && f.instance->GetActiveApplicatorsCount() == 4,
				"real wrapper factory and ring ownership retain blocked status plus winning status and children");
			isolated.world->Step(0, 0); isolated.world->Step(.5f, .5f);
			check(f.Delta() == 3 && f.instance->GetActiveApplicatorsCount() == 3
				&& f.receiver->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE),
				"real world expiration unblocks the wrapped predecessor and recreates its children without fixture ownership");
			isolated.world->Step(2, 2);
			check(f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0
				&& !f.receiver->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE),
				"real world retires all wrapped status and child effects with no fixture-held applicators");
		}
	}
}
