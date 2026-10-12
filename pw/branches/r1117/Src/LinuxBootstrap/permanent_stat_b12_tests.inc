#include "../PF_GameLogic/PFApplMod.h"

/** Real permanent applicators: base versus core, live contexts and atomic rejection. */
namespace
{
struct PermanentStatProbeB12 : PFApplPermanentStatMod
{
	explicit PermanentStatProbeB12(const PFApplCreatePars& p) : PFApplPermanentStatMod(p) {}
	bool Run() { return Start(); }
};

static void PermanentStatB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_FREEZE, 20);
	NDb::Ability* rawAbility = new NDb::Ability;
	NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
	NDb::UnitConstant* constant = new NDb::UnitConstant;
	constant->name = "StatAdd"; constant->var.sString = "sLife/100+rLife/200";
	constants->vars.push_back(constant); rawAbility->constants = constants;
	NDb::Ptr<NDb::Ability> abilityDb = rawAbility;
	CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
	const auto run = [&](const char* mult, const char* add, float expected, bool variable = false,
		const char* enabled = "true", bool compiledOnly = false, float initial = 10, bool wrapped = false) {
		StatValueWithModifiers* unitStat = receiver->GetStat(NDb::STAT_STRENGTH);
		ValueWithModifiers separateCore(20);
		unitStat->SetCore(&separateCore);
		ValueWithModifiers* stat = variable ? receiver->GetVariableVWM("B12Permanent") : unitStat;
		stat->SetBaseValue(initial);
		const float core = unitStat->GetCoreValue();
		const int existing = stat->AddModifier(2, 3, 77);
		const int modifierCount = stat->GetModifiers().size();
		NDb::PermanentStatModApplicator* raw = new NDb::PermanentStatModApplicator;
		raw->modifier.stat = NDb::STAT_STRENGTH;
		if (variable) raw->modifier.variable = "B12Permanent";
		raw->modifier.multValue.sString = mult; raw->modifier.addValue.sString = add;
		raw->enabled.sString = enabled;
		if (compiledOnly) raw->modifier.addValue.compiledString = "unsupported-native-bytecode";
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		if (wrapped)
		{
			NDb::ApplicatorNameOverrider* wrapper = new NDb::ApplicatorNameOverrider;
			wrapper->applicator = db; wrapper->enabled.sString = enabled; db = wrapper;
		}
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pUpgraderAbilityData = ability; pars.pDBAppl = db;
		CObj<PermanentStatProbeB12> app = new PermanentStatProbeB12(pars);
		check(app->Init() && app->Run(), "permanent initializes and completes");
		check(stat->GetBaseValue() == expected, add);
		check(unitStat->GetCoreValue() == core, "permanent preserves core stat");
		check(stat->GetModifiers().size() == modifierCount && stat->Retrieve(existing)->mulValue == 2 &&
			stat->Retrieve(existing)->addValue == 3, "permanent preserves existing modifiers");
		stat->RemoveModifier(existing);
		unitStat->SetCore(nullptr);
	};
	run("2", "3", 23); run("sLife/500", "cStatAdd", 35);
	run("sLife/500", "cStatAdd", 35, true);
	run("1", "cStatAdd", 25, false, "true", false, 10, true);
	sender->SetHealth(400); receiver->SetHealth(600);
	run("sLife/200", "cStatAdd", 27); run("1", "tLife/100", 16);
	run("0", "0", 0); run("-2", "-3", -23);
	run("2", "3", 10, false, "false");
	for (const char* bad : {"", "cMissing", "1/0", "1e39", "sqrt(-1)", "unknown(sLife)", "sLife+"})
	{
		run(bad, "5", 10); run("2", bad, 10);
	}
	run("2", "", 10, false, "true", true);
	run("3e38", "0", 10); run("3e37", "3e38", 10);
	// A finite final double result must not conceal overflowing float multiplication.
	run("3e38", "-3e38", 2, true, "true", false, 2);
	run("1", "0", std::numeric_limits<float>::infinity(), true, "true", false,
		std::numeric_limits<float>::infinity());
}
}
