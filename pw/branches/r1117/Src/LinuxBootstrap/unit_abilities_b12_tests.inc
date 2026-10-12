/** Native construction must invoke the original authored slot initializer, without fixture repair. */
static void UnitAbilitiesB12(Checks& check)
{
	B8BehaviourForeignWorld local(check); if (!local.world) return;
	CObj<PFBaseUnit> seed = Unit(nullptr, NDb::FACTION_FREEZE, 40);
	for (int slots : {0, 2, 4, 8})
	{
		auto* raw = new NDb::Unit; raw->stats = seed->DbUnitDesc()->stats; raw->abilities.clear();
		for (int i = 0; i < slots; ++i)
		{
			if (i == 2) { raw->abilities.push_back(nullptr); continue; }
			auto* ability = new NDb::Ability; ability->type = NDb::ABILITYTYPE_ACTIVE;
			ability->manaCost.sString = "7"; ability->cooldownTime.sString = "2";
			auto* damage = new NDb::DamageApplicator; damage->damage.sString = "25";
			damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; ability->applicators.push_back(damage);
			raw->abilities.push_back(ability);
		}
		NDb::Ptr<NDb::Unit> db = raw;
		CObj<ProbeUnit> unit = new ProbeUnit(local.world, db, NDb::FACTION_FREEZE);
		const int capacity = NDb::KnownEnum<NDb::EAbility>::SizeOf();
		check(unit->GetAbilitiesCount() == capacity, "ordinary native construction creates the fixed original ability-slot count");
		check(unit->GetGlobalCooldown() == local.world->GetAIWorld()->GetAIParameters().globalCooldownTime,
			"construction loads the original authored global cooldown");
		for (int i = 0; i < capacity; ++i)
		{
			const auto* ability = unit->GetAbility(i); const bool populated = i < slots && i != 2;
			check(bool(ability) == populated, "authored null slots remain null and excessive descriptors stay bounded");
			if (ability)
				check(ability->GetOwner() == unit.GetPtr() && ability->GetWorld() == local.world
					&& ability->GetAbilityTypeId() == NDb::ABILITYTYPEID_ABILITY0 + i
					&& ability->GetDBDesc() == raw->abilities[i], "constructed slot preserves exact descriptor owner world and type identity");
		}
		check(!unit->GetAbility(-1) && !unit->GetAbility(capacity), "out-of-range authored slots cannot escape fixed initialization");
		if (unit->GetAbility(1))
		{
			CObj<PFBaseUnit> receiver = Unit(local.world, NDb::FACTION_BURN, 40);
			CObj<PFAbilityInstance> cast = unit->UseAbility(1, Target(receiver.GetPtr()));
			check(cast && receiver->GetHealth() == 975 && unit->GetMana() == 193,
				"a constructor-populated slot delivers a real paid damage cast without manual SetAbility");
			check(unit->GetAbility(1)->GetCurrentCooldown() == 2, "constructor-populated ability retains normal cooldown ownership");
			if (cast) cast->Cancel();
		}
		unit->Cleanup();
	}
}
