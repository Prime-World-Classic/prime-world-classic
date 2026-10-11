/** Timed travel uses checked authored formulas, not the bootstrap executor's silent zero default. */
static void DispatchFormulaB11(Checks& check, PFWorld* world)
{
	for (const char* expression : {"sStrength/tStrength", "cFlight", "tStrength ? 2 : cMissing",
		"cMissing", "1/0", "1e39", " ", "compiled-only"})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		raw->cooldownTime.sString = "5";
		NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
		NDb::UnitConstant* flight = new NDb::UnitConstant;
		flight->name = "Flight";
		flight->var.sString = "sStrength/tStrength";
		constants->vars.push_back(flight);
		raw->constants = constants;
		NDb::DispatchByTime* timed = new NDb::DispatchByTime;
		const bool compiledOnly = std::strcmp(expression, "compiled-only") == 0;
		timed->lifeTime.sString = compiledOnly ? "" : expression;
		if (compiledOnly) timed->lifeTime.compiledString = "unsupported-native-bytecode";
		raw->dispatch = timed;
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		const bool valid = expression[0] == 's' || expression[0] == 't' || std::strcmp(expression, "cFlight") == 0;
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(static_cast<bool>(cast) == valid, "B11 malformed lifetime cannot create paid dispatch work");
		check(sender->GetMana() == (valid ? 130 : 200) && data->GetCurrentCooldown() == (valid ? 5 : 0)
			&& receiver->GetHealth() == 1000, "B11 lifetime validation precedes resources, cooldown and payload");
		if (valid)
		{
			sender->Step(.5f);
			check(cast && !cast->IsFinished() && receiver->GetHealth() == 1000,
				"B11 formula result retains actual timed payload before its deadline");
			sender->Step(1.5f);
			check(cast && cast->IsFinished() && receiver->GetHealth() == 975,
				"B11 checked lifetime and local constants deliver at the authored deadline");
		}
		else if (cast) cast->Cancel();
	}
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	NDb::Ability* raw = new NDb::Ability;
	NDb::DispatchByTime* timed = new NDb::DispatchByTime;
	timed->lifeTime.sString = "tStrength/sStrength";
	raw->dispatch = timed;
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	damage->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
	raw->applicators.push_back(damage);
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(CVec3(6, 5, 0)));
	sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	sender->Step(.5f);
	check(cast && !cast->IsFinished() && sender->GetHealth() == 1000,
		"B11 position dispatch lifetime uses original sender as its formula target");
	sender->Step(.5f);
	check(cast && cast->IsFinished() && sender->GetHealth() == 975,
		"B11 positioned timed dispatch delivers its authored owner payload");
}
