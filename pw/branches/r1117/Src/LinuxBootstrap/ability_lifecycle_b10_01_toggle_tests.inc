/** Toggle-on must register cast work with the unit, just like ordinary casts. */
static void AbilityToggleOwnershipB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	NDb::Talent* raw = new NDb::Talent;
	raw->type = NDb::ABILITYTYPE_SWITCHABLE;
	raw->manaCost.sString = "70";
	raw->cooldownTime.sString = "5";
	NDb::Ptr<NDb::Talent> db = raw;
	CObj<PFTalent> talent = new B9TimedTalent(hero.GetPtr(), db);
	CObj<PFAbilityInstance> cast = talent->Toggle(Target(hero.GetPtr()));
	check(cast && talent->IsOn() && hero->GetMana() == 130,
		"toggle starts a real paid multistate cast");
	check(cast && !cast->IsCastFinished() && talent->GetActiveInstancesCount() == 1,
		"toggle retains its cast instance while work is unfinished");
	hero->Simulate(1);
	check(cast && !cast->IsCastFinished(), "toggle work is not completed before its duration");
	hero->Simulate(2);
	check(cast && cast->IsCastFinished(), "unit simulation advances toggle-owned cast work");
	check(talent->IsOn() && talent->GetCurrentCooldown() == 0,
		"multistate cooldown waits for toggle-off");
	check(!talent->Toggle(Target(hero.GetPtr())) && !talent->IsOn()
		&& talent->GetCurrentCooldown() == 5 && hero->GetMana() == 130,
		"toggle-off starts authored cooldown without charging a second cast");
	hero->Cleanup();
}
