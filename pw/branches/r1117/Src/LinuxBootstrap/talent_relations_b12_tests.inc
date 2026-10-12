/** Restore authored relation tables after testing actual set loading and grouped cooldowns. */
struct TalentRelationDbScopeB12
{
	NDb::AILogicParameters& ai;
	NDb::Ptr<NDb::TalentGroupList> groups;
	NDb::Ptr<NDb::TalentKitList> kits;
	NDb::Ptr<NDb::TalentPackList> packs;
	explicit TalentRelationDbScopeB12(PFWorld* world)
		: ai(const_cast<NDb::AILogicParameters&>(world->GetAIWorld()->GetAIParameters())),
		groups(ai.talentGroups), kits(ai.talentKits), packs(ai.talentPacks) {}
	~TalentRelationDbScopeB12() { ai.talentGroups = groups; ai.talentKits = kits; ai.talentPacks = packs; }
};

/** Real set loading supplies groups/kits/packs before default talents activate. */
static void TalentRelationsB12(Checks& check, PFWorld* world)
{
	TalentRelationDbScopeB12 saved(world);
	CObj<B10CommandHero> hero = CommandHeroB10(world);
	NDb::Talent* seed = new NDb::Talent; seed->naftaCost = 0; seed->manaCost.sString = "0";
	hero->InstallTalent(seed, false);
	PFTalentsSet::SetInfo info;
	for (int slot = 0; slot < 4; ++slot)
	{
		NDb::Talent* db = new NDb::Talent; db->naftaCost = 0; db->manaCost.sString = slot == 0 ? "70" : "0";
		db->persistentId = NStr::StrFmt("relations_%d", slot); db->cooldownTime.sString = "20";
		db->isTakenOnStart = slot == 0;
		info[0][slot].pTalentDesc = db; info[0][slot].refineRate = 1;
	}
	NDb::TalentGroupList* groups = new NDb::TalentGroupList;
	groups->groups.resize(2);
	groups->groups[0].cooldown = 8;
	groups->groups[0].talents.push_back(info[0][0].pTalentDesc);
	groups->groups[0].talents.push_back(info[0][1].pTalentDesc);
	groups->groups[1].cooldown = 3;
	groups->groups[1].talents.push_back(info[0][1].pTalentDesc);
	groups->groups[1].talents.push_back(info[0][2].pTalentDesc);
	NDb::TalentKit* kit = new NDb::TalentKit;
	kit->talents.push_back(info[0][0].pTalentDesc); kit->talents.push_back(info[0][1].pTalentDesc);
	NDb::TalentKitList* kits = new NDb::TalentKitList; kits->kits.push_back(kit);
	kits->kits.push_back(nullptr);
	NDb::TalentPack* pack = new NDb::TalentPack; pack->id = "authored_pack"; pack->iconStyle = "pack_icon";
	pack->talents.push_back(info[0][2].pTalentDesc);
	NDb::TalentPack* later = new NDb::TalentPack; later->id = "later_pack";
	later->talents.push_back(info[0][2].pTalentDesc);
	NDb::TalentPackList* packs = new NDb::TalentPackList;
	packs->packs.push_back(nullptr); packs->packs.push_back(pack); packs->packs.push_back(later);
	saved.ai.talentGroups = groups; saved.ai.talentKits = kits; saved.ai.talentPacks = packs;
	hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
	PFTalent* first = hero->GetTalent(0, 0), *second = hero->GetTalent(0, 1);
	PFTalent* third = hero->GetTalent(0, 2), *outside = hero->GetTalent(0, 3);
	check(first->GetGroup() == &groups->groups[0] && second->GetGroup() == &groups->groups[1],
		"group assignment preserves last matching descriptor precedence");
	check(hero->GetTalentsSet()->GetTalentsFromGroup(&groups->groups[0]).size() == 2 &&
		hero->GetTalentsSet()->GetTalentsFromGroup(&groups->groups[1]).size() == 2,
		"each authored group retains its matching real talent members");
	check(first->IsInKit() && second->IsInKit() && !third->IsInKit() && first->GetTalentsWithinKit() == 2,
		"loaded talents acquire their real kit and its authored membership count");
	check(third->GetPack() == pack && third->GetPackIconStyle() == "pack_icon" &&
		static_cast<PFBaseHero*>(hero.GetPtr())->HasTalentFromPack("authored_pack") &&
		!static_cast<PFBaseHero*>(hero.GetPtr())->HasTalentFromPack("later_pack"),
		"pack assignment skips null records and preserves first-match precedence");
	check(!first->IsActivated() && first->GetActivatedWithinKit() == 0,
		"relations are installed before explicit taken-on-start activation");
	hero->GetTalentsSet()->ActivateTakenOnStart();
	check(first->IsActivated() && first->GetActivatedWithinKit() == 1 && second->GetActivatedWithinKit() == 1,
		"default activation updates the shared kit count through the actual talent lifecycle");
	check(second->Activate() && first->GetActivatedWithinKit() == 2, "later purchase advances the shared kit count once");
	check(!second->Activate() && first->GetActivatedWithinKit() == 2, "rejected duplicate purchase cannot increment the kit count");
	hero->GetTalentsSet()->Reset();
	check(first->GetActivatedWithinKit() == 2 &&
		hero->GetTalentsSet()->GetTalentsFromGroup(&groups->groups[0]).size() == 2,
		"true-hero reset preserves existing relation ownership and purchased kit counts");
	first->RestartCooldown(20);
	check(first->GetCurrentCooldown() == 20 && second->GetCurrentCooldown() == 8 && third->GetCurrentCooldown() == 0 &&
		outside->GetCurrentCooldown() == 0, "group cooldown updates siblings but never recursively enters overlapping groups");
	second->RestartCooldown(12); first->DropCooldown(true); third->DropCooldown(true);
	first->RestartCooldown(5);
	check(second->GetCurrentCooldown() == 12 && third->GetCurrentCooldown() == 0,
		"group cooldown never shortens an existing longer cooldown or reaches an unrelated group");
	second->RestartCooldown(1);
	check(third->GetCurrentCooldown() == 3 && first->GetCurrentCooldown() == 5,
		"overlapping member propagates only its final authored group");
	first->DropCooldown(true); second->DropCooldown(true);
	CObj<PFAbilityInstance> cast = hero->UseTalent(first, Target(hero.GetPtr()));
	check(cast && cast->IsActivated() && hero->GetMana() == 130 &&
		first->GetCurrentCooldown() == 20 && second->GetCurrentCooldown() == 8,
		"actual paid talent activation applies the authored sibling group cooldown");
	if (cast) cast->Cancel();
	for (float bad : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		groups->groups[0].cooldown = bad; second->DropCooldown(true);
		first->RestartCooldown(5);
		check(second->GetCurrentCooldown() == 0, "invalid group duration cannot poison a sibling cooldown");
	}
	hero->RestartGroupCooldowns(nullptr);
	check(first->GetCurrentCooldown() == 5, "null group source is inert");
	PFBaseHero::SpawnInfo spawn; spawn.pHero = dynamic_cast<const NDb::Hero*>(hero->GetDbHero());
	spawn.isClone = true; spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	CObj<B10CommandHero> clone = new B10CommandHero(world, spawn);
	clone->InstallTalent(info[0][0].pTalentDesc, false);
	clone->GetTalentsSet()->LoadSet(clone.GetPtr(), info);
	PFTalent* cloneFirst = clone->GetTalent(0, 0), *cloneThird = clone->GetTalent(0, 2);
	cloneFirst->SetGroup(nullptr); cloneFirst->SetKit(nullptr); cloneThird->SetPack(nullptr);
	clone->GetTalentsSet()->Reset();
	check(clone->IsClone() && cloneFirst->GetGroup() == &groups->groups[0] && cloneFirst->IsInKit() &&
		cloneThird->GetPack() == pack, "clone reset reconstructs the authored relation pointers");
	groups->groups[0].cooldown = 8; second->DropCooldown(true);
	hero->RestartGroupCooldowns(cloneFirst);
	check(second->GetCurrentCooldown() == 0, "a foreign talent cannot initiate this hero's group cooldown");
	clone->GetTalentsSet()->CleanSet();
	PFTalentsSet::SetInfo sparse;
	sparse[5][5] = info[0][2]; sparse[0][0] = info[0][0]; sparse[0][0].status = NDb::TALENTSLOTSTATUS_LOCKED;
	hero->GetTalentsSet()->LoadSet(hero.GetPtr(), sparse);
	check(!hero->GetTalent(0, 0) && hero->GetTalent(5, 5)->GetPack() == pack &&
		hero->GetTalentsSet()->GetTalentsFromGroup(&groups->groups[0]).empty(),
		"reloading rebuilds sparse-grid relations and excludes unavailable slots without stale members");
	saved.ai.talentGroups = nullptr; saved.ai.talentKits = nullptr; saved.ai.talentPacks = nullptr;
	hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
	check(!hero->GetTalent(0, 0)->IsInGroup() && !hero->GetTalent(0, 0)->IsInKit() &&
		!hero->GetTalent(0, 2)->IsInPack(), "missing optional relation tables leave real talents ungrouped");
	hero->GetTalentsSet()->CleanSet();
}
