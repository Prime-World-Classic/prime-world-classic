/** Authored groups share cooldowns even for members not currently in inventory. */
static void ConsumableGroupsB11(Checks& check, PFWorld* world)
{
	NDb::AILogicParameters& ai = const_cast<NDb::AILogicParameters&>(world->GetAIWorld()->GetAIParameters());
	const NDb::Ptr<NDb::ConsumableGroupList> saved = ai.consumableGroups;
	NDb::Consumable* first = new NDb::Consumable;
	first->maxQuantity = 5; first->cooldownTime.sString = "8"; first->manaCost.sString = "0";
	NDb::Consumable* second = new NDb::Consumable;
	second->maxQuantity = 5; second->cooldownTime.sString = "10";
	NDb::Consumable* outside = new NDb::Consumable;
	NDb::Ptr<NDb::Consumable> a = first, b = second, c = outside;
	NDb::ConsumableGroupList* list = new NDb::ConsumableGroupList;
	NDb::ConsumableGroup group;
	group.cooldown = 3; group.consumables.push_back(a); group.consumables.push_back(b);
	group.consumables.push_back(NDb::Ptr<NDb::Consumable>());
	list->groups.push_back(group);
	ai.consumableGroups = list;
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	hero->TakeConsumable(a, 2);
	CObj<PFConsumableAbilityData> own = hero->GetConsumableAbility(a);
	CObj<PFConsumableAbilityData> unrelated = hero->GetConsumableAbility(c);
	check(own->GetGroup() == &list->groups[0] && !unrelated->GetGroup(),
		"group admission uses descriptor identity without conflating empty mock DB IDs");
	check(hero->UseConsumable(0, Target(hero.GetPtr()), false) != nullptr,
		"group cooldown begins at an actual consumable cast");
	CObj<PFConsumableAbilityData> peer = hero->GetConsumableAbility(b);
	check(own->GetCurrentCooldown() == 8 && peer->GetCurrentCooldown() == 3 && unrelated->GetCurrentCooldown() == 0,
		"casting keeps its own cooldown and starts absent-inventory group members only");
	peer->PFAbilityData::RestartCooldown(10);
	own->RestartCooldown(8);
	check(peer->GetCurrentCooldown() == 10, "group restart never shortens a longer existing cooldown");
	hero->Simulate(1);
	check(own->GetCurrentCooldown() == 7 && peer->GetCurrentCooldown() == 9,
		"group-created cached ability data advances in real hero simulation");
	peer->PFAbilityData::RestartCooldown(0);
	hero->RemoveConsumable(0);
	own->RestartCooldown(8);
	check(peer->GetCurrentCooldown() == 3, "cached group cooldown survives inventory removal");
	hero->RestartGroupCooldowns(nullptr);
	check(peer->GetCurrentCooldown() == 3, "missing group initiator cannot mutate cooldowns");
	own->SetGroup(nullptr); peer->SetGroup(nullptr);
	ai.consumableGroups = saved;
}
