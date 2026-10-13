/** Permanent storage edits reach actual paid talent costs only through normal cache refresh. */
static void PermanentTerrainCostB13(Checks& check)
{
	PermanentNatureFixtureB13 f(check);
	f.hero->TeleportTo(CVec2(25, 10)); f.hero->UpdateNatureType();
	auto* raw = new NDb::Talent;
	raw->naftaCost = 0; raw->useRange.sString = "0"; raw->requireLineOfSight = false;
	raw->manaCost.sString = "nt(0,80)"; raw->cooldownTime.sString = "0"; raw->cooldownTimeSecondState.sString = "0";
	NDb::Ptr<NDb::Talent> db = raw;
	CObj<PFTalent> talent = new PFTalent(f.hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
	check(f.hero->GetTerrainType() == TERR_B && f.hero->GetFaction() == NDb::FACTION_FREEZE,
		"permanent-cost fixture begins on actual non-native cached terrain");
	const auto cast = [&](float cost) {
		check(talent->IsEnoughMana() && talent->GetManaCost() == cost,
			"permanent terrain cache selects the original nt mana-cost branch");
		const float mana = f.hero->GetMana();
		CObj<PFAbilityInstance> paid = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(paid && f.hero->GetMana() == mana - cost,
			"actual talent cast pays the checked cost after permanent terrain changes");
	};
	cast(80);
	f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_B, NDb::NATURETYPE_A);
	check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A && f.hero->GetTerrainType() == TERR_B,
		"permanent storage changes immediately without silently refreshing the unit cache");
	check(talent->IsEnoughMana() && talent->GetManaCost() == 80,
		"pre-step ability cost continues to use its original cached terrain contract");
	f.World()->Step(.125f, .125f);
	check(f.hero->GetTerrainType() == TERR_A, "real world tick publishes permanent native terrain to the hero");
	cast(0);
	f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 4, 701);
	f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 1, 702);
	f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_A, NDb::NATURETYPE_B);
	f.World()->Step(1, 1);
	check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B && f.hero->GetTerrainType() == TERR_B,
		"expired stacked override cannot restore a free-cast cache over a permanent enemy-terrain edit");
	check(f.Map()->IsOverrideActive(701) && !f.Map()->IsOverrideActive(702),
		"cost recovery does not cancel independent surviving overlay lifetime");
	cast(80);
	f.World()->Step(3, 3);
	check(!f.Map()->IsOverrideActive(701) && f.hero->GetTerrainType() == TERR_B,
		"final overlay expiry keeps the edited terrain and unit cache");
	check(!talent->IsEnoughMana() && talent->GetManaCost() == 80,
		"paid terrain transitions deplete real mana without a stale free-cast quote");
}
