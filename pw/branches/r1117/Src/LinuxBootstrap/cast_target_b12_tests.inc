/** Exercise authored selector admission through the real factory and paid talent path. */
static void CastTargetB12(Checks& check, PFWorld* world)
{
	for (bool present : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> victim = Unit(world, NDb::FACTION_BURN, 40);
		if (present) hero->AssignTarget(victim.GetPtr(), true);
		auto* raw = new NDb::Talent; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5"; raw->naftaCost = 0;
		auto* limit = new NDb::TargetCastLimitation;
		auto* selector = new NDb::RelativeUnitTargetSelector; selector->relation = NDb::UNITRELATION_TARGET;
		limit->targetSelector = selector; raw->castLimitations.push_back(limit);
		auto* damage = new NDb::DamageApplicator; damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; raw->applicators.push_back(damage);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		Target target(hero.GetPtr()); CastLimitationsCheckParams cp(*talent, target);
		check((limit->Check(cp) == talent.GetPtr()) == present, "target limitation factory admits exactly a nonempty real selector");
		check((CheckTargetLimitation(*limit, cp) == talent.GetPtr()) == present, "selector helper agrees with the DB admission factory");
		check((talent->CheckCastLimitations(target) == nullptr) == present, "normal limitation list exposes a missing selector result");
		CObj<PFAbilityInstance> cast = hero->UseTalent(talent, target);
		check(bool(cast) == present && hero->GetMana() == (present ? 180 : 200)
			&& hero->GetHealth() == (present ? 975 : 1000) && talent->GetCurrentCooldown() == (present ? 5 : 0),
			"selector admission gates real payload payment and cooldown together");
		hero->DropTarget(); hero->Cleanup();
	}
	{
		B8BehaviourForeignWorld isolated(check);
		if (!isolated.world) return;
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> local = Unit(world, NDb::FACTION_BURN, 40), foreign = Unit(isolated.world, NDb::FACTION_BURN, 40);
		NDb::Ptr<NDb::Ability> db = new NDb::Ability;
		CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1, false, false);
		NDb::TargetCastLimitation limit;
		Target request(hero.GetPtr()); CastLimitationsCheckParams cp(*ability, request);
		check(!limit.Check(cp), "missing selector fails closed without creating an alternative selector");
		auto* selector = new NDb::RelativeUnitTargetSelector; selector->relation = NDb::UNITRELATION_TARGET;
		limit.targetSelector = selector;
		hero->AssignTarget(foreign.GetPtr(), true);
		check(!limit.Check(cp) && !CheckTargetLimitation(limit, cp), "foreign-world selected units cannot authorize a cast");
		hero->AssignTarget(local.GetPtr(), true);
		check(limit.Check(cp) == ability.GetPtr(), "local registered selected unit authorizes the original ability");
		Target invalid, foreignRequest(foreign.GetPtr()), badPoint(CVec3(std::numeric_limits<float>::quiet_NaN(), 0, 0));
		for (const Target* target : {&invalid, &foreignRequest, &badPoint})
			check(!limit.Check(CastLimitationsCheckParams(*ability, *target)), "invalid requester context never reaches selector admission");
		cp.pAbility = nullptr; check(!limit.Check(cp), "missing selector ability is rejected");
		cp.pAbility = ability; cp.pTarget = nullptr; check(!limit.Check(cp), "missing selector requester is rejected");
		cp.pTarget = &request;
		world->UnregisterObject(local.GetPtr(), false); world->ProcessAddRemove();
		check(!limit.Check(cp) && !CheckTargetLimitation(limit, cp), "unregistered selected units cannot authorize a cast through stale references");
		hero->DropTarget(); hero->Cleanup();
	}
}
