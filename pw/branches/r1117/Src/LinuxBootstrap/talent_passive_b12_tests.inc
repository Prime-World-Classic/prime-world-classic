/** Purchases activate real passive effects; removing/replacing the set cancels owned work. */
static void TalentPassiveB12(Checks& check, PFWorld* world)
{
	for (bool takenOnStart : {false, true})
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		NDb::Talent* db = new NDb::Talent;
		db->isTakenOnStart = takenOnStart; db->naftaCost = takenOnStart ? 0 : 500;
		db->manaCost.sString = "0";
		NDb::StatModApplicator* passive = new NDb::StatModApplicator;
		passive->modifier.stat = NDb::STAT_STRENGTH;
		passive->modifier.multValue.sString = "1"; passive->modifier.addValue.sString = "4";
		db->passiveApplicators.push_back(passive);
		hero->InstallTalent(db, false);
		CObj<PFTalent> talent = hero->GetTalent(0, 0);
		const float original = hero->GetStatValue(NDb::STAT_STRENGTH);
		check(!talent->IsActivated() && talent->GetActiveInstancesCount() == 0,
			"loading a descriptor alone never activates its passive effects");
		if (takenOnStart)
		{
			hero->GetTalentsSet()->ActivateTakenOnStart();
			hero->GetTalentsSet()->ActivateTakenOnStart();
		}
		else
		{
			check(!hero->ActivateTalent(0, 0) && hero->GetStatValue(NDb::STAT_STRENGTH) == original &&
				talent->GetActiveInstancesCount() == 0, "unaffordable purchase cannot activate a passive");
			hero->AddGold(600);
			check(hero->ActivateTalent(0, 0) && hero->GetGold() == 100,
				"later affordable purchase pays once before applying its passive");
		}
		check(talent->IsActivated() && talent->GetActiveInstancesCount() == 1 &&
			hero->GetStatValue(NDb::STAT_STRENGTH) == original + 4,
			"default and paid activation each install exactly one passive instance");
		check(!hero->ActivateTalent(-1, 0) && hero->GetStatValue(NDb::STAT_STRENGTH) == original + 4,
			"invalid talent coordinates cannot alter the installed passive");
		hero->GetTalentsSet()->CleanSet();
		check(talent->GetActiveInstancesCount() == 0 && hero->GetStatValue(NDb::STAT_STRENGTH) == original,
			"default and paid passives both release on set cleanup despite retained talent references");
	}
	for (bool reload : {false, true})
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->AddGold(100);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Talent* raw = new NDb::Talent;
		raw->persistentId = "passive_owner"; raw->naftaCost = 25;
		raw->manaCost.sString = "10"; raw->cooldownTime.sString = "0";
		NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
		modifier->modifier.stat = NDb::STAT_STRENGTH;
		modifier->modifier.multValue.sString = "1"; modifier->modifier.addValue.sString = "5";
		raw->passiveApplicators.push_back(modifier);
		NDb::DispatchByTime* timed = new NDb::DispatchByTime; timed->lifeTime.sString = "10";
		raw->dispatch = timed;
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		hero->InstallTalent(raw, false);
		CObj<PFTalent> old = hero->GetTalent(0, 0);
		const float original = hero->GetStatValue(NDb::STAT_STRENGTH);
		const float gold = hero->GetGold();
		check(hero->ActivateTalent(0, 0), "passive talent buys through the real hero/set activation path");
		check(hero->GetStatValue(NDb::STAT_STRENGTH) == original + 5 && hero->GetGold() == gold - 25 &&
			old->GetActiveInstancesCount() == 1, "purchase applies its passive once after payment and progression");
		check(!hero->ActivateTalent(0, 0) && hero->GetStatValue(NDb::STAT_STRENGTH) == original + 5 &&
			hero->GetGold() == gold - 25, "repeated purchase cannot duplicate passive effects or payment");
		// Also exercise teardown independently when the untouched purchase stub omitted activation.
		old->ApplyPassivePart(true);
		check(hero->GetStatValue(NDb::STAT_STRENGTH) == original + 5,
			"explicit engine passive refresh replaces its effect rather than stacking copies");
		CObj<PFAbilityInstance> cast = hero->UseTalent(old.GetPtr(), Target(receiver.GetPtr()));
		check(cast && cast->IsActivated() && !cast->IsFinished() && hero->GetMana() == 190,
			"purchased talent also owns an actual paid in-flight dispatch");
		if (reload)
		{
			NDb::Talent* replacement = new NDb::Talent;
			replacement->persistentId = "replacement_passive"; replacement->naftaCost = 0; replacement->manaCost.sString = "0";
			NDb::StatModApplicator* next = new NDb::StatModApplicator;
			next->modifier.stat = NDb::STAT_STRENGTH; next->modifier.multValue.sString = "1"; next->modifier.addValue.sString = "9";
			replacement->passiveApplicators.push_back(next);
			PFTalentsSet::SetInfo info; info[0][0].pTalentDesc = replacement; info[0][0].refineRate = 1;
			hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
			check(hero->ActivateTalent(0, 0), "replacement set purchases its own passive talent");
		}
		else hero->GetTalentsSet()->CleanSet();
		check(hero->GetStatValue(NDb::STAT_STRENGTH) == original + (reload ? 9 : 0),
			"set cleanup removes old passive effects while preserving the replacement's contribution");
		check(cast && cast->IsFinished() && old->GetActiveInstancesCount() == 0,
			"retained old talent and cast references cannot keep removed-set gameplay alive");
		hero->Simulate(11);
		check(receiver->GetHealth() == 1000 && hero->GetMana() == 190,
			"removed talent dispatch cannot deal delayed damage and does not refund paid mana");
		hero->GetTalentsSet()->CleanSet(); hero->GetTalentsSet()->CleanSet();
		check(hero->GetStatValue(NDb::STAT_STRENGTH) == original,
			"repeated cleanup removes the final passive without lingering modifiers");
	}
}
