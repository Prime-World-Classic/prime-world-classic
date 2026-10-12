/** Query actual engine talent sets, including sparse grids, duplicates and live cooldowns. */
static void HeroTalentQueriesB12(Checks& check, PFWorld* world)
{
	CObj<B10CommandHero> hero = CommandHeroB10(world);
	PFBaseHero& base = *hero;
	check(!base.GetTalentById(nullptr) && !base.GetTalentById("missing") && !base.GetTalent(nullptr),
		"empty hero set has no persistent-ID talent");
	check(!base.HasTalentFromPack(nullptr) && !base.HasTalentFromPack("missing"),
		"empty hero set has no talent pack");
	check(base.GetUltimateCD() == -1, "hero without a purchased ultimate reports original minus-one sentinel");
	NDb::Talent* seed = new NDb::Talent; seed->naftaCost = 0; seed->manaCost.sString = "0";
	hero->InstallTalent(seed, false);
	PFTalentsSet::SetInfo info;
	for (int row = 0; row < PFTalentsSet::LEVELS_COUNT; ++row)
		for (int col = 0; col < PFTalentsSet::SLOTS_COUNT; ++col)
		{
			NDb::Talent* db = new NDb::Talent;
			db->persistentId = NStr::StrFmt("query_%d_%d", row, col);
			db->naftaCost = 0; db->manaCost.sString = "0"; db->cooldownTime.sString = "10";
			info[row][col].pTalentDesc = db; info[row][col].refineRate = 1;
		}
	hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
	for (int row = 0; row < PFTalentsSet::LEVELS_COUNT; ++row)
		for (int col = 0; col < PFTalentsSet::SLOTS_COUNT; ++col)
		{
			PFTalent* talent = hero->GetTalent(row, col);
			const char* id = info[row][col].pTalentDesc->persistentId.c_str();
			check(base.GetTalentById(id) == talent, "persistent-ID lookup scans every authored talent slot without purchase requirement");
			check(base.GetTalent(id) == static_cast<IMiscFormulaPars*>(talent),
				"formula talent lookup returns the actual ability interface");
		}
	check(!base.GetTalentById("QUERY_0_0") && !base.GetTalentById("query_0") &&
		!base.GetTalentById(nullptr), "talent IDs are exact and case-sensitive, with null rejected");
	NDb::Talent* firstDb = const_cast<NDb::Talent*>(info[0][0].pTalentDesc.GetPtr());
	NDb::Talent* lastDb = const_cast<NDb::Talent*>(info[PFTalentsSet::LEVELS_COUNT - 1][PFTalentsSet::SLOTS_COUNT - 1].pTalentDesc.GetPtr());
	lastDb->persistentId = firstDb->persistentId;
	PFTalent* first = hero->GetTalent(0, 0);
	PFTalent* last = hero->GetTalent(PFTalentsSet::LEVELS_COUNT - 1, PFTalentsSet::SLOTS_COUNT - 1);
	check(base.GetTalentById(firstDb->persistentId.c_str()) == first,
		"duplicate persistent IDs retain original first-row first-slot selection");
	NDb::TalentPack* rawPack = new NDb::TalentPack; rawPack->id = "live_pack";
	NDb::Ptr<NDb::TalentPack> pack = rawPack;
	last->SetPack(pack.GetPtr());
	check(base.HasTalentFromPack("live_pack") && !base.HasTalentFromPack("Live_pack"),
		"pack membership includes an unpurchased talent at the last slot with exact ID matching");
	last->SetPack(nullptr);
	check(!base.HasTalentFromPack("live_pack"), "pack removal is visible without a stale lookup cache");
	firstDb->isUltimateTalent = true;
	check(base.GetUltimateCD() == -1, "unpurchased ultimate is excluded from cooldown query");
	check(first->Activate(), "query fixture purchases the real talent");
	first->RecalculateAndRestartCooldown(); first->DropCooldown(true, 5, false);
	check(base.GetUltimateCD() == .5f, "ultimate query reports remaining divided by authored cooldown");
	first->Update(2, true);
	check(base.GetUltimateCD() == .3f, "ultimate query observes actual cooldown progression");
	first->RestartCooldown(15);
	check(base.GetUltimateCD() == 1, "ultimate query uses the actual runtime cooldown duration, not the authored base");
	first->DropCooldown(true);
	check(base.GetUltimateCD() == 0, "ready purchased ultimate reports zero rather than absent");
	firstDb->cooldownTime.sString = "0"; first->RecalculateAndRestartCooldown();
	check(base.GetUltimateCD() == -1, "zero-duration ultimate is skipped without division");
	lastDb->isUltimateTalent = true;
	check(last->Activate(), "late-slot ultimate can be purchased through the real engine");
	last->RecalculateAndRestartCooldown(); last->DropCooldown(true, 8, false);
	check(base.GetUltimateCD() == .2f, "zero-duration first ultimate does not hide a later valid ultimate");
	firstDb->cooldownTime.sString = "10"; first->RecalculateAndRestartCooldown(); first->DropCooldown(true, 3, false);
	check(base.GetUltimateCD() == .7f, "multiple purchased ultimates preserve original grid precedence");
	hero->GetTalentsSet()->CleanSet();
	check(!base.GetTalentById("query_0_0") && !base.HasTalentFromPack("live_pack") && base.GetUltimateCD() == -1,
		"cleaned talent set cannot expose stale query results");
}
