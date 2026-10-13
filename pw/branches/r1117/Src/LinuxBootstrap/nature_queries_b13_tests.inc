/** Original binary geometry with independently known linear world/attack coordinates. */
static string NatureQueriesImageB13(const NDb::DBNatureMap& db, bool singular = false, bool nonfiniteBorder = false)
{
	const string cells = NatureQueryImageB12(db);
	CObj<MemoryStream> image = new MemoryStream;
	image->Write(cells.data(), cells.size()); image->Seek(4, SEEKORIGIN_BEGIN);
	const CVec2 center(20, 15); image->Write(&center, sizeof(center));
	CVec2 nodes[6][6];
	for (int row = 0; row < 6; ++row) for (int col = 0; col < 6; ++col)
	{
		nodes[row][col] = CVec2((col - 1) * 10.f + 5, row * 6.f);
		image->Write(&nodes[row][col], sizeof(CVec2));
	}
	for (int row = 0; row < 5; ++row) for (int col = 0; col < 5; ++col)
	{
		CVec2 point = nodes[row][col];
		if (nonfiniteBorder && row == 0 && col == 1) point.x = std::numeric_limits<float>::quiet_NaN();
		const BilinearSolver2D solver(point, singular ? point : nodes[row][col + 1],
			singular ? point : nodes[row + 1][col], singular ? point : nodes[row + 1][col + 1]);
		image->Write(&solver, sizeof(solver));
	}
	for (int row = 0; row < 4; ++row) for (int col = 0; col < 5; ++col)
	{
		Terrain::NatureMapElementInfo cell = {};
		cell.attackPos = CVec2(col - .5f, row * (10.f / 6.f));
		// Exact stored attack-space endpoints are not inside either base.
		if (row == 0 && col == 1) cell.attackPos.x = 0;
		if (row == 0 && col == 3) cell.attackPos.x = 3;
		cell.natureCur = cell.natureOld = cell.natureBase = col % 3;
		image->Write(&cell, sizeof(cell));
	}
	return string(image->GetBuffer(), image->GetSize());
}

/** Scene-less loaded engine world; all mock geometry is authored input, not a query override. */
struct NatureQueriesFixtureB13
{
	Checks& check;
	B8BehaviourForeignWorld loaded;
	CObj<TestFileSystem> files;
	NDb::Ptr<NDb::Terrain> terrain;
	PFWorld* World() const { return loaded.world; }
	PFWorldNatureMap* Map() const { return World()->GetNatureMap(); }
	NatureQueriesFixtureB13(Checks& check) : check(check), loaded(check)
	{
		files = new TestFileSystem("", false); RootFileSystem::RegisterFileSystem(files);
		auto* raw = new NDb::Terrain; raw->elemXCount = 4; raw->elemYCount = 3; raw->tilesPerElement = 4;
		raw->natureMap.gridStep = 10; raw->natureMap.segmentCount = 3;
		raw->natureMap.natureMapFile = "/Mock/NatureQueriesB13.naturemap";
		for (int road = 0; road < 3; ++road)
		{
			auto& geometry = raw->natureMap.attackSpaceRoads[road]; geometry.centerWidth = 2;
			geometry.nodes.push_back(CVec2(5, 27 - 12 * road)); geometry.nodes.push_back(CVec2(35, 27 - 12 * road));
		}
		terrain = raw; Reload();
	}
	void Reload(bool singular = false, bool nonfiniteBorder = false)
	{
		files->SetFileWithContents(terrain->natureMap.natureMapFile, NatureQueriesImageB13(terrain->natureMap, singular, nonfiniteBorder));
		Map()->OnLoaded(terrain);
		check(Map()->GetNatureInPoint(20.f, 0.f) == TERR_B, "query fixture loads actual authored cells and solver geometry");
	}
	bool LoadCore(NatureQueryCodecB12& codec, bool singular = false)
	{
		files->SetFileWithContents(terrain->natureMap.natureMapFile, NatureQueriesImageB13(terrain->natureMap, singular));
		const Terrain::NatureMapParams params = {&terrain->natureMap, CVec3(40, 30, 50)};
		return codec.Initialize(params, &codec.modified);
	}
	~NatureQueriesFixtureB13() { RootFileSystem::UnregisterFileSystem(files); }
};

static void NatureQueriesB13(Checks& check)
{
	{
		NatureQueriesFixtureB13 f(check);
		check(f.Map()->GetNumSegments() == 3, "segment count comes from initialized attack-space geometry");
		for (float x : {0.f, 10.f, 20.f, 30.f, 40.f})
			check(f.Map()->IsAtBase(x, 0) == (x == 0 || x == 40), "base query uses strict stored attack-space endpoints");
		for (int faction = 1; faction <= 2; ++faction)
		{
			check(std::fabs(f.Map()->GetNaturePercent(NDb::EFaction(faction)) - .2f) < 1e-6f,
				"global percent includes original one-segment bases, not counts of authored colored cells");
			CVec2 borders[3] = {CVec2(-99, -99), CVec2(-99, -99), CVec2(-99, -99)};
			f.Map()->GetNatureBorders(NDb::EFaction(faction), borders);
			for (int road = 0; road < 3; ++road)
			{
				check(f.Map()->GetNaturePercent(NDb::EFaction(faction), NDb::ENatureRoad(road)) == 0,
					"road percentage excludes bases and reads original unmoved fronts");
				check(borders[road].x == (faction == 1 ? 5 : 35) && borders[road].y == 27 - road * 12,
					"all original road borders restore through loaded bilinear geometry");
			}
		}
		for (float y : {3.f, 12.f, 15.f, 24.f, 27.f}) for (float x : {0.f, 7.f, 17.f, 27.f, 40.f})
		{
			NDb::ENatureRoad road = NDb::ENatureRoad(-9); int segment = -99;
			f.Map()->GetNatureSegment(CVec2(x, y), road, segment);
			check(road == (y < 12 ? NDb::NATUREROAD_BOTTOM : y < 24 ? NDb::NATUREROAD_CENTER : NDb::NATUREROAD_TOP)
				&& segment == int((x - 5) / 10), "segment uses original strip thresholds and truncation, including base-side range");
		}
		for (float x : {-5.f, 45.f})
		{
			NDb::ENatureRoad road = NDb::ENatureRoad(-9); int segment = -99;
			f.Map()->GetNatureSegment(CVec2(x, 15), road, segment);
			check(road == NDb::NATUREROAD_CENTER && segment == (x < 0 ? -1 : 4),
				"base-side segment results retain negative and beyond-road values without clamping");
		}
		f.Map()->Override(NDb::FACTION_BURN, CCircle(CVec2(0, 0), 2), 1, 901);
		check(f.Map()->GetNatureInPoint(0.f, 0.f) == TERR_B && f.Map()->IsAtBase(0, 0)
			&& std::fabs(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE) - .2f) < 1e-6f,
			"temporary cell overrides do not redefine base geometry or road-derived percentages");
		f.World()->Step(1, 1);
		CObj<B9CastHero> hero = B9MaleHero(f.World(), NDb::FACTION_FREEZE);
		NDb::Ptr<NDb::Ability> db = new NDb::Ability;
		CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, true, false);
		check(std::fabs(ability->GetTerrainPart(NDb::FACTION_FREEZE) - .2f) < 1e-6f,
			"actual ability gameplay getter observes restored world percentage");
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(ability, Target(hero.GetPtr()), false);
		auto* raw = new NDb::ModifyTerrainApplicator; raw->enabled.sString = "false";
		NDb::Ptr<NDb::ModifyTerrainApplicator> appDb = raw;
		const Target target(hero.GetPtr()); PFApplCreatePars pars(cast, target); pars.pWorld = f.World(); pars.pDBAppl = appDb;
		CObj<PFBaseApplicator> app = CreateApplicator(pars);
		check(app && ActivateApplicator(app, cast) && std::fabs(static_cast<const IMiscFormulaPars*>(app.GetPtr())->GetTerrainPart(NDb::FACTION_BURN) - .2f) < 1e-6f,
			"actual factory applicator gameplay getter observes the same world percentage");
		f.Map()->SetDesiredPosition(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER, 1); f.World()->Step(.25f, .25f);
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::NATUREROAD_CENTER) == 0
			&& std::fabs(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE) - .2f) < 1e-6f,
			"read-only checkpoint leaves native writable front controls disabled");
		const_cast<NDb::Terrain*>(f.terrain.GetPtr())->natureMap.segmentCount = 99;
		check(f.Map()->GetNumSegments() == 3, "loaded segment metadata does not follow an unvalidated external DB edit");
		hero->Cleanup();
	}
	for (const auto& percents : {std::pair<float, float>(25, 10), {0, 10}, {25, 0}})
	{
		NatureQueriesFixtureB13 f(check); auto* db = const_cast<NDb::Terrain*>(f.terrain.GetPtr());
		db->natureMap.baseAPercent = percents.first; db->natureMap.baseBPercent = percents.second; f.Reload();
		const float bases[2] = {percents.first * .01f, percents.second * .01f};
		const int automatic[2] = {bases[0] > 0 ? 0 : 1, bases[1] > 0 ? 0 : 1};
		for (int side = 0; side < 2; ++side)
			check(std::fabs(f.Map()->GetNaturePercent(NDb::EFaction(side + 1))
				- (automatic[side] / float(3 + automatic[0] + automatic[1]) * (1 - bases[0] - bases[1]) + bases[side])) < 1e-6f,
				"authored explicit and automatic base percentages preserve original weighting");
	}
	for (int invalid = 0; invalid < 6; ++invalid)
	{
		NatureQueriesFixtureB13 f(check); auto* db = const_cast<NDb::Terrain*>(f.terrain.GetPtr());
		db->natureMap.baseAPercent = invalid == 0 ? -1 : invalid == 1 ? 101 : invalid == 2 ? 60
			: invalid == 3 ? std::numeric_limits<float>::quiet_NaN() : invalid == 4 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::max();
		db->natureMap.baseBPercent = invalid == 2 ? 60 : 0; f.Reload();
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE) == 0 && f.Map()->GetNaturePercent(NDb::FACTION_BURN) == 0,
			"invalid authored base percentages reject instead of clamping or exposing nonfinite shares");
	}
	{
		NatureQueriesFixtureB13 f(check); auto* db = const_cast<NDb::Terrain*>(f.terrain.GetPtr());
		db->natureMap.editableNatureMap = true; f.Reload();
		f.Map()->SetCustomNaturePercents(.25f, .5f);
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE) == .25f && f.Map()->GetNaturePercent(NDb::FACTION_BURN) == .5f
			&& f.Map()->GetNaturePercent(NDb::FACTION_NEUTRAL) == .25f, "editable map preserves original explicit A/B and complementary neutral shares");
		for (int invalid : {-1, 3, INT_MAX})
			check(f.Map()->GetNaturePercent(NDb::EFaction(invalid)) == 0, "editable custom branch also rejects invalid factions");
		for (const auto& value : {std::pair<float, float>(-1, .5f), {.5f, -1},
			{std::numeric_limits<float>::quiet_NaN(), 0}, {0, std::numeric_limits<float>::quiet_NaN()}})
		{
			// This existing setter is not changed by the read-only checkpoint.
			f.Map()->SetCustomNaturePercents(value.first, value.second);
			check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE) == 0 && f.Map()->GetNaturePercent(NDb::FACTION_BURN) == 0
				&& f.Map()->GetNaturePercent(NDb::FACTION_NEUTRAL) == 0, "invalid custom state cannot leak through any percentage query");
		}
		f.Map()->SetCustomNaturePercents(.25f, .5f);
		check(f.Map()->GetNaturePercent(NDb::FACTION_NEUTRAL) == .25f, "valid custom queries recover after rejected custom state");
	}
	{
		NatureQueriesFixtureB13 f(check); f.Reload(false, true);
		CVec2 points[3] = {CVec2(71, 72), CVec2(73, 74), CVec2(75, 76)};
		f.Map()->GetNatureBorders(NDb::FACTION_FREEZE, points);
		check(points[0] == CVec2(71, 72) && points[1] == CVec2(73, 74) && points[2] == CVec2(75, 76),
			"a nonfinite last-road border cannot partially replace earlier caller outputs");
	}
	{
		NatureQueriesFixtureB13 f(check);
		for (int faction : {-1, 0, 3, INT_MAX})
		{
			check(f.Map()->GetNaturePercent(NDb::EFaction(faction)) == 0, "non-editable percentages accept only original A/B factions");
			CVec2 points[3] = {CVec2(71, 72), CVec2(73, 74), CVec2(75, 76)};
			f.Map()->GetNatureBorders(NDb::EFaction(faction), points);
			check(points[0] == CVec2(71, 72) && points[1] == CVec2(73, 74) && points[2] == CVec2(75, 76),
				"invalid border faction leaves all output points untouched");
			for (int road : {-1, 0, 2, 3, INT_MAX})
				check(f.Map()->GetNaturePercent(NDb::EFaction(faction), NDb::ENatureRoad(road)) == 0,
					"invalid road/faction pair cannot index fixed original arrays");
		}
		for (int road : {-1, 3, INT_MAX})
			check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE, NDb::ENatureRoad(road)) == 0,
				"invalid road is rejected for a valid faction too");
		f.Map()->GetNatureBorders(NDb::FACTION_FREEZE, nullptr);
		for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
			-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
		{
			check(!f.Map()->IsAtBase(invalid, 0) && !f.Map()->IsAtBase(0, invalid), "invalid base coordinates reject unsafe hex conversion");
			NDb::ENatureRoad road = NDb::ENatureRoad(-9); int segment = -99;
			f.Map()->GetNatureSegment(CVec2(invalid, 15), road, segment);
			check(road == -9 && segment == -99, "nonfinite or unrepresentable geometry query leaves outputs untouched");
			f.Map()->GetNatureSegment(CVec2(17, invalid), road, segment);
			check(road == -9 && segment == -99, "vertical invalid or overflowing geometry preserves both caller outputs too");
		}
		NatureQueryCodecB12 original; check(f.LoadCore(original, true), "singular solver regression uses the actual binary loader");
		NDb::ENatureRoad road = NDb::ENatureRoad(-9); int segment = -99;
		original.GetNatureSegment(CVec2(17, 15), road, segment);
		check(road == -9 && segment == -99, "original core must reject a nonfinite solver result before converting to int");
		f.Reload(true); road = NDb::ENatureRoad(-9); segment = -99;
		f.Map()->GetNatureSegment(CVec2(17, 15), road, segment);
		check(road == -9 && segment == -99, "world segment query preserves outputs when authored solver geometry is singular");
	}
	{
		NatureQueriesFixtureB13 f(check); auto* db = const_cast<NDb::Terrain*>(f.terrain.GetPtr());
		for (auto& road : db->natureMap.attackSpaceRoads) road.nodes.clear();
		f.Reload(); NatureQueryCodecB12 original; check(f.LoadCore(original), "empty eligible-road set remains a loadable authored grid");
		check(original.GetNaturePercent(NDb::NATURETYPE_A) == 0, "original core empty-road denominator returns finite zero");
		check(f.Map()->GetNaturePercent(NDb::FACTION_FREEZE) == 0, "world empty-road percentage does not expose NaN");
		db->natureMap.natureMapFile = "/Mock/MissingNatureQueriesB13.naturemap"; f.Map()->OnLoaded(f.terrain);
		check(f.Map()->GetNumSegments() == 0 && !f.Map()->IsAtBase(0, 0), "unavailable map does not expose stale DB segment metadata");
		NDb::ENatureRoad road = NDb::ENatureRoad(-9); int segment = -99;
		f.Map()->GetNatureSegment(CVec2(17, 15), road, segment);
		check(road == -9 && segment == -99, "missing grid leaves original output references untouched");
	}
}

/** Read-only shipped geometry, compared with the same original core codec in isolation. */
static void NatureQueriesAuthoredB13(Checks& check, const char* dataRoot)
{
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Maps/Multiplayer/MOBA/_.TERN.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "query regression reads shipped terrain descriptor");
		if (xml && xml->IsOk())
		{
			auto* raw = new NDb::Terrain; NDb::Ptr<NDb::Terrain> terrain = raw;
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true); NatureQueryDescriptorB12 descriptor = {raw}; saver->Add("Terrain", &descriptor);
			auto* adv = new NDb::AdvMap; adv->terrain = terrain;
			auto* desc = new NDb::AdvMapDescription; desc->map = adv; NDb::Ptr<NDb::AdvMapDescription> description = desc;
			CObj<NCore::IWorldBase> owner = PFWorld::CreatePFWorld(); auto* world = dynamic_cast<PFWorld*>(owner.GetPtr());
			const bool loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
			check(loaded && !world->GetScene(), "shipped query geometry loads without terrain visuals");
			if (loaded)
			{
				NatureQueryCodecB12 original; const Terrain::NatureMapParams params = {&raw->natureMap, CVec3(world->GetMapSize(), 50)};
				check(original.Initialize(params, &original.modified), "independent original core reads the shipped geometry payload");
				auto* map = world->GetNatureMap();
				check(map->GetNumSegments() == 31, "shipped segment count is the original 31");
				for (int side = 1; side <= 2; ++side)
				{
					check(std::fabs(map->GetNaturePercent(NDb::EFaction(side)) - 1.f / 33) < 1e-6f,
						"shipped initial global share is base weighting, not the colored-cell fraction");
					CVec2 borders[3] = {CVec2(-99, -99), CVec2(-99, -99), CVec2(-99, -99)};
					map->GetNatureBorders(NDb::EFaction(side), borders);
					for (int road = 0; road < 3; ++road)
					{
						const CVec2 expected = original.GetNatureBound(NDb::ENatureType(side), NDb::ENatureRoad(road));
						check(std::isfinite(expected.x) && std::isfinite(expected.y) && fabs2(expected - borders[road]) < 1e-6f,
							"shipped border matches original loaded attack-space interpolation");
					}
				}
				int baseCells = 0, baseMismatch = 0, segmentMismatch = 0;
				for (int y = 0; y < 165; ++y) for (int x = 0; x < 165; ++x)
				{
					const CVec2 pos((x + (y & 1) * .5f) * 2.5f, y * 2.5f);
					const bool base = original.IsAtBase(pos.x, pos.y); baseCells += base;
					baseMismatch += map->IsAtBase(pos.x, pos.y) != base;
					if (x % 8 || y % 8) continue;
					NDb::ENatureRoad expectedRoad = NDb::ENatureRoad(-9), actualRoad = expectedRoad; int expectedSegment = -99, actualSegment = -99;
					original.GetNatureSegment(pos, expectedRoad, expectedSegment); map->GetNatureSegment(pos, actualRoad, actualSegment);
					segmentMismatch += expectedRoad != actualRoad || expectedSegment != actualSegment;
				}
				std::printf("Authored MOBA queries: base-cells=%d base-mismatch=%d segment-mismatch=%d\n", baseCells, baseMismatch, segmentMismatch);
				check(baseCells > 0 && baseMismatch == 0, "every shipped hex center preserves original base membership");
				check(segmentMismatch == 0, "shipped sampled world positions preserve original road and segment ranges");
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
}
