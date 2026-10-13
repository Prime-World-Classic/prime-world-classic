#include "../PF_GameLogic/PFRoadFlagpole.h"

/** Compare the world entrypoint with the existing terrain algorithm on loaded mock bytes. */
struct RoadNatureFixtureB13 : NatureQueriesFixtureB13
{
	NatureQueryCodecB12 direct;
	explicit RoadNatureFixtureB13(Checks& check, bool editable = false) : NatureQueriesFixtureB13(check)
	{
		auto* db = const_cast<NDb::Terrain*>(terrain.GetPtr());
		db->natureMap.attackSpeed = 1.f; db->natureMap.overrideSpeed = 0.f;
		db->natureMap.editableNatureMap = editable; Reload();
		check(LoadCore(direct), "road fixture loads the same original geometry in both owners");
	}
	void Set(int faction, int road, int segment)
	{
		Map()->SetDesiredPosition(NDb::EFaction(faction), NDb::ENatureRoad(road), segment);
		direct.SetDesiredPosition(NDb::ENatureType(faction), NDb::ENatureRoad(road), segment);
	}
	void Advance(float dt)
	{
		Map()->OnStep(dt); direct.OnStep(dt);
	}
	void Match()
	{
		bool same = true;
		for (int id = 0; id < 20; ++id) same = same && Map()->GetNatureInPoint(id) == direct.GetNature(id);
		check(same, "road evolution preserves original changed-quad cell classification");
		for (int side : {1, 2})
		{
			CVec2 points[3]; Map()->GetNatureBorders(NDb::EFaction(side), points);
			bool sameRoads = true;
			for (int road = 0; road < 3; ++road)
			{
				const CVec2 expected = direct.GetNatureBound(NDb::ENatureType(side), NDb::ENatureRoad(road));
				sameRoads = sameRoads && fabs2(points[road] - expected) < 1e-6f
					&& fabs(Map()->GetNaturePercent(NDb::EFaction(side), NDb::ENatureRoad(road))
						- direct.GetNaturePercent(NDb::ENatureType(side), NDb::ENatureRoad(road))) < 1e-6f;
			}
			check(sameRoads, "road percentages and world borders follow original slice positions independently per faction");
			if (!terrain->natureMap.editableNatureMap)
				check(fabs(Map()->GetNaturePercent(NDb::EFaction(side))
					- direct.GetNaturePercent(NDb::ENatureType(side))) < 1e-6f,
					"global road/base-weighted nature shares follow advancing fronts");
		}
	}
};

/** Read-only shipped files, with every loaded cell compared across eight original road states. */
static void RoadNatureAuthoredB13(Checks& check, const char* dataRoot)
{
	if (!dataRoot) return;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Maps/Multiplayer/MOBA/_.TERN.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "road regression reads shipped terrain");
		if (xml && xml->IsOk())
		{
			auto* raw = new NDb::Terrain; NDb::Ptr<NDb::Terrain> terrain = raw;
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true); NatureQueryDescriptorB12 descriptor = {raw}; saver->Add("Terrain", &descriptor);
			auto* adv = new NDb::AdvMap; adv->terrain = terrain;
			auto* desc = new NDb::AdvMapDescription; desc->map = adv; NDb::Ptr<NDb::AdvMapDescription> description = desc;
			CObj<NCore::IWorldBase> owner = PFWorld::CreatePFWorld(); auto* world = dynamic_cast<PFWorld*>(owner.GetPtr());
			const bool loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
			check(loaded, "shipped road regression loads the original map without a scene");
			if (loaded)
			{
				NatureQueryCodecB12 direct; const Terrain::NatureMapParams params = {&raw->natureMap, CVec3(world->GetMapSize(), 50)};
				const bool coreLoaded = direct.Initialize(params, &direct.modified);
				check(coreLoaded, "shipped road reference independently loads original grid storage");
				if (coreLoaded)
				{
					direct.modified.clear();
					auto* map = world->GetNatureMap();
					const int count = direct.GetSizeX() * direct.GetSizeY();
					std::vector<int> initial(count);
					for (int id = 0; id < count; ++id) initial[id] = direct.GetNature(id);
					int changed = 0, mismatches = 0, states = 0;
					for (int side : {1, 2})
					{
						for (int road = 0; road < 3; ++road)
						{
							const int segment = side == 1 ? 7 + road * 3 : 20 - road * 3;
							map->SetDesiredPosition(NDb::EFaction(side), NDb::ENatureRoad(road), segment);
							direct.SetDesiredPosition(NDb::ENatureType(side), NDb::ENatureRoad(road), segment);
						}
						for (float dt : {.125f, 1.f, 8.f, 40.f})
						{
							map->OnStep(dt); direct.OnStep(dt); direct.modified.clear(); ++states;
							for (int id = 0; id < count; ++id)
							{
								mismatches += map->GetNatureInPoint(id) != direct.GetNature(id);
								changed += map->GetNatureInPoint(id) != initial[id];
							}
							for (int faction : {1, 2})
							{
								check(fabs(map->GetNaturePercent(NDb::EFaction(faction)) - direct.GetNaturePercent(NDb::ENatureType(faction))) < 1e-6f,
									"shipped advancing global share matches original base-weighted roads");
								CVec2 points[3]; map->GetNatureBorders(NDb::EFaction(faction), points);
								for (int road = 0; road < 3; ++road)
									check(fabs2(points[road] - direct.GetNatureBound(NDb::ENatureType(faction), NDb::ENatureRoad(road))) < 1e-5f,
										"shipped advancing borders retain original bilinear world coordinates");
							}
						}
					}
					std::printf("Authored MOBA road evolution: cells=%d states=%d changed-samples=%d mismatches=%d\n", count, states, changed, mismatches);
					check(count == 27225 && states == 8 && changed > 0 && mismatches == 0,
						"all shipped MOBA cells follow original advancing and opposing road fronts");
				}
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
}

static void RoadNatureB13(Checks& check, const char* dataRoot)
{
	RoadNatureAuthoredB13(check, dataRoot);
	for (int side : {1, 2}) for (int road = 0; road < 3; ++road) for (int segment : {-1, 0, 1, 2, 3})
	{
		RoadNatureFixtureB13 f(check);
		f.Set(side, road, segment);
		check(f.Map()->GetNaturePercent(NDb::EFaction(side), NDb::ENatureRoad(road)) == 0,
			"setting a road target does not teleport its current border");
		for (float dt : {0.f, .03125f, .03125f, .125f, .5f, 4.f})
		{
			f.Advance(dt); f.Match();
		}
		const int target = side == 2 ? 3 - segment : segment > 0 && segment < 3 ? segment + 1 : segment;
		check(fabs(f.Map()->GetNaturePercent(NDb::EFaction(side), NDb::ENatureRoad(road)) - target / 3.f) < 1e-6f,
			"final target retains asymmetric A interior shift, B reflection and original -1 endpoint");
	}
	{
		RoadNatureFixtureB13 f(check);
		f.Set(1, 1, 1); f.Advance(.0625f);
		f.Set(1, 1, 1); f.Advance(.0625f);
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) == 0,
			"reissuing a target resets the original fractional slice accumulator");
		f.Advance(.0625f);
		check(fabs(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) - 1.f / 30) < 1e-6f,
			"two post-target fractional ticks advance exactly one original slice");
		f.Match();
		f.Set(2, 1, 1); f.Advance(4); f.Match();
		check(fabs(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) - 1.f / 3) < 1e-6f
			&& fabs(f.Map()->GetNaturePercent(NDb::FACTION_BURN, NDb::NATUREROAD_CENTER) - 2.f / 3) < 1e-6f,
			"opposite road targets retain original non-overlap correction");
		f.Set(2, 1, 3); f.Set(1, 1, 0); f.Advance(4); f.Match();
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) == 0
			&& f.Map()->GetNaturePercent(NDb::FACTION_BURN, NDb::NATUREROAD_CENTER) == 0,
			"both road fronts can retreat to their original base boundaries");
	}
	{
		RoadNatureFixtureB13 f(check);
		f.Set(1, 1, 1); f.Advance(.0625f);
		for (int side : {-1, 0, 3, INT_MIN, INT_MAX})
			f.Map()->SetDesiredPosition(NDb::EFaction(side), NDb::NATUREROAD_CENTER, 1);
		for (int road : {-1, 3, INT_MIN, INT_MAX})
			f.Map()->SetDesiredPosition(NDb::FACTION_FREEZE, NDb::ENatureRoad(road), 1);
		for (int segment : {-2, 4, INT_MIN, INT_MAX})
			f.Map()->SetDesiredPosition(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER, segment);
		f.Advance(.0625f); f.Match();
		check(fabs(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) - 1.f / 30) < 1e-6f,
			"invalid road commands preserve both valid pending target and fractional progress");
		for (float dt : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::max()}) { f.Advance(dt); f.Match(); }
	}
	{
		RoadNatureFixtureB13 f(check, true);
		f.Set(1, 1, 3); f.Advance(4); f.Match();
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) == 0,
			"editable nature maps retain disabled road evolution even when targets are set");
	}
	{
		RoadNatureFixtureB13 f(check);
		const CCircle circle(CVec2(25, 10), 2);
		f.Map()->Override(NDb::FACTION_BURN, circle, 2, 801);
		f.direct.Override(NDb::NATURETYPE_B, circle, 2, 801);
		for (int road = 0; road < 3; ++road) f.Set(1, road, 3);
		f.Advance(1.5f); f.Match();
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B && f.Map()->IsOverrideActive(801),
			"advancing road base cannot overwrite a still-owned temporary visible faction");
		f.Advance(.5f); f.Match();
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A && !f.Map()->IsOverrideActive(801),
			"temporary expiry restores the newly advanced road base instead of the original loaded faction");
	}
	{
		RoadNatureFixtureB13 f(check);
		CObj<B9CastHero> hero = B9MaleHero(f.World(), NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->TeleportTo(CVec2(25, 10)); hero->UpdateNatureType();
		auto* raw = new NDb::Talent; raw->naftaCost = 0; raw->manaCost.sString = "nt(0,80)";
		raw->cooldownTime.sString = "0"; raw->cooldownTimeSecondState.sString = "0";
		raw->useRange.sString = "0"; raw->requireLineOfSight = false;
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		check(hero->GetTerrainType() == TERR_B && talent->GetManaCost() == 80, "road-cost fixture begins on loaded enemy terrain");
		for (int road = 0; road < 3; ++road) f.Map()->SetDesiredPosition(NDb::FACTION_FREEZE, NDb::ENatureRoad(road), 3);
		f.World()->Step(2, 2);
		check(hero->GetTerrainType() == TERR_A, "normal world tick publishes advancing road terrain to the hero cache");
		// This independently owned talent refreshes its quote through the actual admission API.
		check(talent->IsEnoughMana() && talent->GetManaCost() == 0, "actual talent admission refreshes its quote from the advanced native-terrain cache");
		const float mana = hero->GetMana();
		CObj<PFAbilityInstance> cast = hero->UseTalent(talent, Target(hero.GetPtr()));
		check(cast && hero->GetMana() == mana, "actual free talent cast consumes the native-terrain cost after road advancement");
		hero->Cleanup();
	}
	{
		RoadNatureFixtureB13 f(check);
		NDb::AdvMapObject object; object.gameObject = new NDb::Flagpole;
		CObj<PFRoadFlagpole> flag = new PFRoadFlagpole(f.World(), object);
		NDb::ENatureRoad road; int segment;
		f.Map()->GetNatureSegment(flag->GetPosition().AsVec2D(), road, segment);
		check(flag->CanRaise(NDb::FACTION_FREEZE), "real isolated road flagpole is eligible for its first capture");
		flag->OnRaise(NDb::FACTION_FREEZE, nullptr); f.World()->Step(3, 3);
		check(flag->GetFaction() == NDb::FACTION_FREEZE
			&& f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, road) == 1,
			"actual last-flag capture callback advances the full original road");
		flag->OnDropFlag(nullptr); f.World()->Step(3, 3);
		check(flag->GetFaction() == NDb::FACTION_NEUTRAL && f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, road) == 0,
			"actual flag drop callback retracts the road to its base");
	}
}
