#include "../PF_GameLogic/PFCastLimitations.h"

/** Real paid talents and literal DB predicates isolate condition admission from dispatch timing. */
static void CastConditionB12(Checks& check, PFWorld* world)
{
	struct Case { const char* expression; bool allow; bool compiled; };
	for (const auto& entry : {Case{"true", true, false}, Case{"false", false, false}, Case{"1", true, false},
		Case{"-2", true, false}, Case{"0", false, false}, Case{"", true, false}, Case{"", false, true},
		Case{"true", true, true}, Case{"sLife == rLife", true, false},
		Case{"sLife == 1000 && rLife == 1000", false, false},
		Case{"sender.vCharge >= cLimit", true, false}, Case{"receiver.vCharge >= cLimit", false, false},
		Case{"cMissing", false, false}, Case{"1/0", false, false}, Case{"sqrt(-1)", false, false},
		Case{"1e39", false, false}, Case{"sLife+", false, false}, Case{"unknown(sLife)", false, false},
		Case{"true ? 1 : cMissing", true, false}})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->GetVariableVWM("Charge")->SetBaseValue(5);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 40);
		NDb::Talent* raw = new NDb::Talent; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5"; raw->naftaCost = 0;
		NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
		NDb::UnitConstant* constant = new NDb::UnitConstant; constant->name = "Limit"; constant->var.sString = "5";
		constants->vars.push_back(constant); raw->constants = constants;
		NDb::ConditionCastLimitation* condition = new NDb::ConditionCastLimitation;
		condition->condition.sString = entry.expression;
		if (entry.compiled) condition->condition.compiledString = "unavailable-native-bytecode";
		raw->castLimitations.push_back(condition);
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		Target target(receiver.GetPtr()); CastLimitationsCheckParams cp(*talent, target);
		if ((condition->Check(cp) == talent.GetPtr()) != entry.allow)
			std::printf("Condition case mismatch: source=%s compiled=%d expected=%d sender=%g receiver=%g\n",
				entry.expression, entry.compiled, entry.allow, hero->GetLife(), receiver->GetLife());
		check((condition->Check(cp) == talent.GetPtr()) == entry.allow, "condition DB factory returns the original ability only on checked success");
		check((CheckConditionLimitation(*condition, cp) == talent.GetPtr()) == entry.allow,
			"direct condition helper agrees with the authored factory and checked formula result");
		check((talent->CheckCastLimitations(target) == nullptr) == entry.allow, "ability reports the exact denied condition through its normal limitation list");
		CObj<PFAbilityInstance> cast = hero->UseTalent(talent, target);
		check(bool(cast) == entry.allow && hero->GetMana() == (entry.allow ? 180 : 200) &&
			receiver->GetHealth() == (entry.allow ? 975 : 1000) && talent->GetCurrentCooldown() == (entry.allow ? 5 : 0),
			"checked condition admits or rejects actual damage payment and cooldown as one cast");
	}
	{
		CustomEnergyFixtureB12 f(world); f.Values(10, 100, 4);
		NDb::Talent* raw = new NDb::Talent; raw->manaCost.sString = "0"; raw->cooldownTime.sString = "0"; raw->naftaCost = 0;
		NDb::ConditionCastLimitation* condition = new NDb::ConditionCastLimitation;
		condition->condition.sString = "sender.vUniquePower >= 3"; raw->castLimitations.push_back(condition);
		NDb::PermanentStatModApplicator* consume = new NDb::PermanentStatModApplicator;
		consume->modifier.variable = "UniquePower"; consume->modifier.multValue.sString = "1"; consume->modifier.addValue.sString = "-3";
		raw->applicators.push_back(consume);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(f.hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		for (int expected : {7, 4, 1})
			check(f.hero->UseTalent(talent, Target(f.hero.GetPtr())) && f.hero->GetCustomEnergyValue() == expected,
				"same-tick custom affordability admits each funded variable-consuming cast");
		check(!f.hero->UseTalent(talent, Target(f.hero.GetPtr())) && f.hero->GetCustomEnergyValue() == 1,
			"same-tick resource depletion rejects the next cast before its permanent effect");
		f.hero->Simulate(.5f);
		check(f.hero->UseTalent(talent, Target(f.hero.GetPtr())) && f.hero->GetCustomEnergyValue() == 0 && f.hero->GetMana() == 0,
			"regenerated custom energy restores admission independently from disabled mana");
	}
	for (bool loseAdmission : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->GetVariableVWM("Charge")->SetBaseValue(5);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 40);
		NDb::Talent* raw = new NDb::Talent; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5"; raw->naftaCost = 0;
		NDb::ConditionCastLimitation* condition = new NDb::ConditionCastLimitation;
		condition->condition.sString = "sender.vCharge >= 3"; raw->castLimitations.push_back(condition);
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; raw->applicators.push_back(damage);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(talent.GetPtr(), Target(receiver.GetPtr()), false);
		B10ActivationTimerRestore timer(1); pending->operator&(timer);
		check(pending->ApplyToTarget() && hero->GetMana() == 200 && receiver->GetHealth() == 1000,
			"condition-bearing delayed cast starts without early payment or damage");
		hero->GetVariableVWM("Charge")->SetBaseValue(loseAdmission ? 2 : 6);
		pending->Update(1);
		check(hero->GetMana() == (loseAdmission ? 200 : 180) && receiver->GetHealth() == (loseAdmission ? 1000 : 975) &&
			talent->GetCurrentCooldown() == (loseAdmission ? 0 : 5),
			"delayed activation samples the live condition before dispatch payment and cooldown");
		pending->Cancel();
	}
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 40); receiver->SetHealth(500);
		NDb::Ptr<NDb::Ability> db = new NDb::Ability;
		CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1, false, false);
		NDb::ConditionCastLimitation condition; condition.condition.sString = "rLife == 500";
		Target unit(receiver.GetPtr()), point(CVec3(5, 5, 0)), invalid;
		check(condition.Check(CastLimitationsCheckParams(*ability, unit)) == ability.GetPtr() &&
			!condition.Check(CastLimitationsCheckParams(*ability, point)), "unit conditions use the receiver while ground conditions substitute the owner as Windows does");
		condition.condition.sString = "rLife == sLife";
		check(condition.Check(CastLimitationsCheckParams(*ability, point)) == ability.GetPtr(), "ground-target owner substitution preserves the formula context");
		condition.condition.sString = "true";
		check(!condition.Check(CastLimitationsCheckParams(*ability, invalid)), "an invalid target cannot satisfy even a constant condition");
		Target badPoint(CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0));
		check(!condition.Check(CastLimitationsCheckParams(*ability, badPoint)), "nonfinite ground geometry is rejected before formula evaluation");
		CastLimitationsCheckParams missing(*ability, unit); missing.pAbility = nullptr;
		check(!condition.Check(missing), "missing condition ability is rejected without dereferencing it");
		missing.pAbility = ability; missing.pTarget = nullptr;
		check(!condition.Check(missing), "missing condition target is rejected without dereferencing it");
	}
}
