#include "../PF_GameLogic/PFApplMod.h"
#include "../PF_GameLogic/PFAbilityInstance.h"

/** Real buff lifecycle hooks; toggles mirror the engine's SetEnabled + hook protocol. */
namespace
{
struct StatBuffProbeB12 : PFApplStatMod
{
	int enabledCalls = 0;
	explicit StatBuffProbeB12(const PFApplCreatePars& p) : PFApplStatMod(p) {}
	void Enable() override { ++enabledCalls; PFApplStatMod::Enable(); }
	bool Initialize() { return Init(); }
	bool Run() { return Start(); }
	bool Tick(float dt) { return Step(dt); }
	void Finish() { Stop(); }
	void Toggle(bool enabled) { SetEnabled(enabled); if (enabled) Enable(); else Disable(); }
	float Magnitude() { return GetAbsModification(); }
};

/** Mock DB values are authored explicitly; the production evaluator is never stubbed. */
struct StatBuffFixtureB12
{
	CObj<PFBaseUnit> sender, receiver;
	NDb::Ptr<NDb::Ability> abilityDb;
	CObj<PFAbilityData> ability;
	NDb::Ptr<NDb::StatModApplicator> db;
	CObj<StatBuffProbeB12> app;
	ValueWithModifiers* stat;
	int originalCount;
	StatBuffFixtureB12(PFWorld* world, bool cached = false, bool variable = false,
		const char* mult = "sLife/500", const char* add = "cBuffAdd",
		const char* enabled = "true", const char* start = "true", const char* lifetime = "-1",
		PFBaseApplicator* parent = nullptr)
	{
		sender = Unit(world, NDb::FACTION_FREEZE, 40);
		receiver = Unit(world, NDb::FACTION_FREEZE, 20);
		NDb::Ability* rawAbility = new NDb::Ability;
		NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
		NDb::UnitConstant* constant = new NDb::UnitConstant;
		constant->name = "BuffAdd"; constant->var.sString = "sLife/100+rLife/200";
		constants->vars.push_back(constant); rawAbility->constants = constants;
		abilityDb = rawAbility;
		ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		NDb::StatModApplicator* raw = new NDb::StatModApplicator;
		raw->modifier.stat = NDb::STAT_STRENGTH;
		if (variable) raw->modifier.variable = "B12Buff";
		raw->modifier.multValue.sString = mult; raw->modifier.addValue.sString = add;
		raw->dontUpdate = cached; raw->enabled.sString = enabled;
		raw->startCondition.sString = start; raw->lifeTime.sString = lifetime;
		if (cached) raw->statModName = "BuffAdd";
		db = raw;
		stat = variable ? receiver->GetVariableVWM("B12Buff") : receiver->GetStat(NDb::STAT_STRENGTH);
		stat->SetBaseValue(20); originalCount = stat->GetModifiers().size();
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pParent = parent;
		pars.pWorld = world; pars.pOwner = sender; pars.pUpgraderAbilityData = ability; pars.pDBAppl = db;
		app = new StatBuffProbeB12(pars);
	}
	NDb::StatModApplicator& Edit() { return *const_cast<NDb::StatModApplicator*>(db.GetPtr()); }
	int Count() const { return stat->GetModifiers().size() - originalCount; }
	~StatBuffFixtureB12() { app->Finish(); }
};

static void StatBuffB12(Checks& check, PFWorld* world)
{
	// Real activation must balance completion notifications without stealing a sibling's count.
	for (int invalid = 0; invalid != 4; ++invalid)
	{
		StatBuffFixtureB12 f(world, true, false, "1", "3");
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(f.ability, Target(f.receiver.GetPtr()), true);
		PFApplCreatePars pars(cast, Target(f.receiver.GetPtr()));
		pars.pDBAppl = f.db;
		CObj<StatBuffProbeB12> live = new StatBuffProbeB12(pars);
		check(live->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(live.GetPtr()), cast),
			"persistent stat modifier activates through the real receiver lifecycle");
		check(cast->GetActiveApplicatorsCount() == 1 && live->IsLinkedToAppliedRing() && f.stat->GetValue() == 23,
			"persistent sibling owns one live ability count and stat modifier");
		const int retained = f.stat->GetModifiers().back().id;
		if (invalid == 0) f.Edit().modifier.multValue.sString = "cMissing";
		else if (invalid == 1) f.Edit().modifier.addValue.sString = "1/0";
		else if (invalid == 2) f.Edit().statModName = "Missing";
		else const_cast<NDb::UnitConstant*>(f.abilityDb->constants->vars[0].GetPtr())->var.sString = "1/0";
		CObj<StatBuffProbeB12> rejected = new StatBuffProbeB12(pars);
		check(rejected->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(rejected.GetPtr()), cast),
			"invalid static modifier reaches real activation and immediate completion");
		check(!rejected->IsAlive() && !rejected->IsEnabled() && rejected->enabledCalls == 0,
			"rejected static activation stops without buff enable or enabled state");
		check(cast->GetActiveApplicatorsCount() == 1 && live->IsAlive(),
			"rejected static activation preserves the existing sibling's ability count");
		check(f.Count() == 1 && f.stat->Retrieve(retained) && f.stat->GetValue() == 23,
			"rejected static activation leaves the sibling identifier and target value untouched");
		live->Finish();
		check(cast->GetActiveApplicatorsCount() == 0 && f.Count() == 0 && f.stat->GetValue() == 20 && !live->IsLinkedToAppliedRing(),
			"persistent sibling alone owns the final stop and target cleanup");
	}
	for (bool cached : {false, true}) for (bool variable : {false, true})
	{
		StatBuffFixtureB12 f(world, cached, variable);
		check(f.app->Initialize() && !f.app->Run(), "stat buff initializes and remains alive");
		check(f.Count() == 1 && f.stat->GetValue() == 55, "buff evaluates owner receiver and authored constant");
		check(f.stat->GetModifiers().back().provider == f.sender->GetObjectId(), "modifier preserves the ability owner provider");
		check(f.app->Magnitude() == 35 && f.Count() == 1, "active magnitude preserves one modifier");
		float constant = -1;
		check(f.app->GetStatModConstant(constant) == cached && (!cached || constant == 15), "cached named constant is checked in original context");
		f.sender->SetHealth(500);
		check(!f.app->Tick(.25f), "infinite buff steps");
		check(f.stat->GetValue() == (cached ? 55 : 30), "dontUpdate freezes formulas; dynamic step reads live stats");
		check(f.app->IsChanged() == !cached, "only a changed dynamic update sets changed flag");
		check(f.app->GetStatModConstant(constant) == cached && (!cached || constant == 15), "named constant stays frozen");
		f.app->SetChanged(false);
		check(!f.app->Tick(.25f) && !f.app->IsChanged(), "unchanged dynamic parameters do not flag a change");
		f.app->Toggle(true);
		check(f.Count() == 1, "repeated enable does not leak a modifier");
		// Dynamic base Step owns re-enabling, so author the disabled interval too.
		f.Edit().enabled.sString = "false";
		f.app->Toggle(false); f.app->Toggle(false);
		check(f.Count() == 0 && f.stat->GetValue() == 20, "repeated disable removes only the owned modifier");
		check(f.app->Magnitude() == (cached ? 35 : 10) && f.Count() == 0, "disabled preview does not retain a modifier");
		f.sender->SetHealth(200);
		check(!f.app->Tick(.25f) && f.Count() == 0, "disabled step cannot apply a modifier");
		f.Edit().enabled.sString = "true";
		f.sender->SetHealth(500); f.app->Toggle(true);
		check(f.Count() == 1 && f.stat->GetValue() == (cached ? 55 : 30), "re-enable follows cached or live policy");
		f.app->Finish(); f.app->Finish();
		check(f.Count() == 0 && !f.app->IsEnabled(), "stop is idempotent and cleans up");
	}
	for (bool cached : {false, true})
	{
		StatBuffFixtureB12 disabled(world, cached, false, "sLife/500", "cBuffAdd", "false");
		check(disabled.app->Initialize() && !disabled.app->Run() && disabled.Count() == 0, "disabled start keeps a live unapplied buff");
		disabled.sender->SetHealth(500); disabled.app->Toggle(true);
		check(disabled.stat->GetValue() == (cached ? 55 : 30), "disabled static start caches; dynamic start waits for enable");
		StatBuffFixtureB12 denied(world, cached, false, "1", "3", "true", "false");
		check(denied.app->Initialize() && denied.app->Run() && denied.Count() == 0, "false start condition completes without a modifier");
		StatBuffFixtureB12 finite(world, cached, false, "1", "3", "true", "true", ".5");
		check(finite.app->Initialize() && !finite.app->Run() && !finite.app->Tick(.25f), "finite lifetime remains before expiry");
		check(finite.app->Tick(.25f), "finite lifetime expires through the base buff");
		finite.app->Finish(); check(finite.Count() == 0, "expiry stop removes modifier");
	}
	for (bool cached : {false, true}) for (bool badMultiply : {false, true})
		for (const char* bad : {"", "cMissing", "1/0", "1e39", "sqrt(-1)", "unknown(sLife)", "sLife+"})
	{
		StatBuffFixtureB12 f(world, cached, false, badMultiply ? bad : "2", badMultiply ? "5" : bad);
		check(f.app->Initialize(), "invalid stat buff still resolves its real target");
		f.app->Run();
		check(f.Count() == 0 && f.stat->GetValue() == 20, "invalid start has no identity or partial fallback modifier");
		check(f.app->Magnitude() == 0 && f.Count() == 0, "invalid preview is neutral and leaves target untouched");
		f.app->Finish(); check(f.Count() == 0, "invalid buff cleanup is harmless");
	}
	{
		StatBuffFixtureB12 f(world, true);
		f.Edit().statModName = "Missing";
		check(f.app->Initialize() && f.app->Run() && f.Count() == 0, "failed named constant rejects the entire static start");
		check(f.app->Magnitude() == 0 && f.Count() == 0, "failed static start cannot preview constructor defaults");
		float value = 123;
		check(!f.app->GetStatModConstant(value) && value == 123, "failed static start cannot publish a fallback constant");
	}
	{
		StatBuffFixtureB12 f(world, false, false, "2", "");
		f.Edit().modifier.addValue.compiledString = "unsupported-native-bytecode";
		check(f.app->Initialize(), "compiled-only stat buff initializes"); f.app->Run();
		check(f.Count() == 0, "compiled-only formula does not become a fallback modifier");
	}
	for (bool badMultiply : {false, true}) for (const char* bad : {"cMissing", "1/0", "1e39"})
	{
		StatBuffFixtureB12 f(world);
		check(f.app->Initialize() && !f.app->Run(), "dynamic rejection fixture starts");
		const int id = f.stat->GetModifiers().back().id;
		if (badMultiply) f.Edit().modifier.multValue.sString = bad;
		else f.Edit().modifier.addValue.sString = bad;
		f.app->SetChanged(false);
		check(!f.app->Tick(.1f) && !f.app->IsChanged(), "invalid dynamic update does not signal a change");
		const auto* retained = f.stat->Retrieve(id);
		check(retained && retained->mulValue == 2 && retained->addValue == 15 && f.stat->GetValue() == 55,
			"invalid dynamic update retains last valid coefficients and identifier");
		check(f.app->Magnitude() == 0 && f.stat->Retrieve(id) && f.stat->GetValue() == 55,
			"invalid active preview cannot remove or replace the live modifier");
		f.Edit().modifier.multValue.sString = "1"; f.Edit().modifier.addValue.sString = "4";
		check(!f.app->Tick(.1f) && f.stat->GetValue() == 24, "dynamic update recovers on a valid formula");
	}
	{
		StatBuffFixtureB12 f(world, false, false, "2", "1/(sLife-500)");
		f.sender->SetHealth(500);
		check(f.app->Initialize(), "initially invalid dynamic buff initializes"); f.app->Run();
		check(f.Count() == 0, "failed enable leaves no modifier");
		f.sender->SetHealth(501);
		check(!f.app->Tick(.1f) && f.Count() == 1 && f.stat->GetValue() == 41, "enabled dynamic buff retries a failed initial evaluation");
	}
	for (const auto constraint : {NDb::STATCONSTRAINT_NONE, NDb::STATCONSTRAINT_LOWERLIMIT, NDb::STATCONSTRAINT_UPPERLIMIT})
	{
		StatBuffFixtureB12 f(world, false, true, "2", "3");
		const int other = f.stat->AddModifier(1, 10, 123);
		f.Edit().modifier.topModifier = true; f.Edit().constraint = constraint;
		check(f.app->Initialize() && !f.app->Run(), "top constrained buff starts");
		const float expected = constraint == NDb::STATCONSTRAINT_NONE ? 63 : constraint == NDb::STATCONSTRAINT_LOWERLIMIT ? 43 : 30;
		check(f.stat->GetValue() == expected && f.app->Magnitude() == expected - 30, "preview respects top and constraint metadata");
		f.app->Toggle(false);
		check(f.stat->GetValue() == 30 && f.stat->Retrieve(other), "disable preserves unrelated modifiers");
	}
	for (bool cached : {false, true}) for (bool aggregate : {false, true})
	{
		StatBuffFixtureB12 f(world, cached, true, aggregate ? "1e20" : "3e38", "0");
		if (aggregate) f.stat->AddModifier(1e20, 0, 123);
		const int before = f.Count();
		check(f.app->Initialize(), "overflow buff initializes"); f.app->Run();
		check(f.Count() == before, "overflowing modifier arithmetic is rejected before insertion");
		check(f.app->Magnitude() == 0 && f.Count() == before, "overflow preview cannot hide behind engine clamps");
	}
	{
		StatBuffFixtureB12 f(world, false, true, "1", "-4");
		check(f.app->Initialize() && !f.app->Run() && f.stat->GetValue() == 16 && f.app->Magnitude() == 4,
			"negative authored additions produce absolute magnitude");
		f.Edit().modifier.multValue.sString = "0"; f.Edit().modifier.addValue.sString = "0";
		check(!f.app->Tick(.1f) && f.stat->GetValue() == 0, "explicit zero coefficients are valid");
	}
	{
		StatBuffFixtureB12 f(world, true, true, "3e38", "0", "false");
		check(f.app->Initialize() && !f.app->Run() && f.Count() == 0,
			"disabled static start caches formulas without prematurely admitting arithmetic");
		f.stat->SetBaseValue(0); f.app->Toggle(true);
		check(f.Count() == 1 && f.stat->GetValue() == 0, "static enable checks current target arithmetic using cached coefficients");
	}
	{
		StatBuffFixtureB12 f(world, false, false, "2", "3");
		ValueWithModifiers core(30);
		f.receiver->GetStat(NDb::STAT_STRENGTH)->SetCore(&core);
		check(f.app->Initialize() && !f.app->Run() && f.stat->GetValue() == 103 && f.app->Magnitude() == 53,
			"buff preview retains the real core contribution");
		f.app->Finish(); f.receiver->GetStat(NDb::STAT_STRENGTH)->SetCore(nullptr);
	}
	{
		StatBuffFixtureB12 defaults(world, false, true, "1", "3", "", "", "");
		check(defaults.app->Initialize() && !defaults.app->Run() && !defaults.app->Tick(100) && defaults.Count() == 1,
			"empty lifecycle fields retain existing enabled start and infinite lifetime defaults");
		defaults.Edit().behaviorFlags = NDb::BUFFBEHAVIOR_STOPONDISABLE;
		defaults.Edit().enabled.sString = "false";
		defaults.app->Toggle(false);
		check(defaults.app->Tick(.1f) && defaults.Count() == 0, "STOPONDISABLE remains base-buff policy");
	}
	{
		StatBuffFixtureB12 parent(world, false, true, "1", "0");
		StatBuffFixtureB12 child(world, false, true, "1", "sLife/100", "true", "true", "-1", parent.app.GetPtr());
		check(parent.app->Initialize() && !parent.app->Run() && child.app->Initialize() && !child.app->Run(), "parent and child are real initialized buffs");
		parent.app->SetChanged(false); child.app->SetChanged(false); child.sender->SetHealth(500);
		check(!child.app->Tick(.1f) && child.app->IsChanged() && parent.app->IsChanged(), "dynamic coefficient changes notify the parent");
		parent.app->SetChanged(false); child.app->SetChanged(false); child.Edit().modifier.addValue.sString = "cMissing";
		check(!child.app->Tick(.1f) && !child.app->IsChanged() && !parent.app->IsChanged(), "invalid dynamic update does not notify the parent");
	}
}
}
