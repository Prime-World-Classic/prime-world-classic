/** Real authored grid storage in an isolated world, plus the original direct terrain API. */
struct PermanentNatureFixtureB13 : NatureOverrideFixtureB13
{
	NatureQueryCodecB12 direct;
	explicit PermanentNatureFixtureB13(Checks& check, bool editable = true, float speed = 0, float gridStep = 10)
		: NatureOverrideFixtureB13(check, speed, gridStep)
	{
		auto* db = const_cast<NDb::Terrain*>(terrain.GetPtr());
		db->natureMap.editableNatureMap = editable;
		files->SetFileWithContents(db->natureMap.natureMapFile, NatureQueryImageB12(db->natureMap));
		Map()->OnLoaded(terrain);
		files->SetFileWithContents(db->natureMap.natureMapFile, NatureQueryImageB12(db->natureMap));
		const Terrain::NatureMapParams params = {&db->natureMap, CVec3(40, 30, 50)};
		check(direct.Initialize(params, &direct.modified), "permanent nature fixture loads the actual original grid codec");
	}
};

/** Entry-point failures are separate from already-present overlay ownership behavior. */
static void PermanentNatureEntrypointsB13(Checks& check)
{
	{
		PermanentNatureFixtureB13 f(check);
		CObj<PFLogicObject> object = new PFLogicObject(f.World(), CVec3(25, 10, 0), nullptr);
		f.World()->GetAIWorld()->RegisterObject(object); object->UpdateNatureType();
		f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_N,
			"circle permanent mutation changes the matching authored base cell immediately");
		check(f.observer->GetTerrainType() == TERR_B && object->GetTerrainType() == TERR_B,
			"permanent mutation retains original deferred unit and non-unit cache notification");
		f.Map()->OnStep(0);
		check(object->GetTerrainType() == TERR_N, "original nature callback publishes to the non-unit AI ring");
		check(f.observer->GetTerrainType() == TERR_B, "unit nature cache is refreshed by its own Step, not the non-unit callback");
		f.World()->Step(.125f, .125f);
		check(f.observer->GetTerrainType() == TERR_N && object->GetTerrainType() == TERR_N,
			"real world step publishes the permanent change to both unit and non-unit caches");
		object->OnDie();
	}
	{
		PermanentNatureFixtureB13 f(check);
		vector<CVec2> polygon;
		polygon.push_back(CVec2(24, 9)); polygon.push_back(CVec2(26, 9));
		polygon.push_back(CVec2(26, 11)); polygon.push_back(CVec2(24, 11));
		f.Map()->ChangeNature(polygon, static_cast<NDb::ENatureType>(-1), NDb::NATURETYPE_A);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A,
			"polygon permanent mutation uses original negative-from wildcard semantics");
		f.Map()->OnStep(0);
		f.World()->Step(.125f, .125f);
		check(f.observer->GetTerrainType() == TERR_A, "polygon edit reaches the real observer cache on the world step");
	}
}

/** Diagnose the exact permanent/temporary interaction before choosing any new native policy. */
static void PermanentNatureOwnershipB13(Checks& check)
{
	for (bool stacked : {false, true})
	{
		PermanentNatureFixtureB13 f(check);
		const CCircle circle(CVec2(25, 10), 2);
		f.direct.Override(NDb::NATURETYPE_A, circle, stacked ? 4 : 1, 301);
		if (stacked) f.direct.Override(NDb::NATURETYPE_A, circle, 1, 302);
		f.direct.OnStep(0);
		f.direct.EditNature(7, NDb::NATURETYPE_N);
		const auto& edited = f.direct.GetElementInfo(7);
		check(edited.natureBase == TERR_N && edited.natureCur == TERR_N && edited.ownerData == 0,
			"original direct EditNature immediately changes base and visible nature and clears cell ownership");
		check(f.direct.IsOverrideActive(301) && (!stacked || f.direct.IsOverrideActive(302)),
			"original permanent edit does not cancel registry ownership or queued temporary entries");
		f.direct.OnStep(1);
		const auto& after = f.direct.GetElementInfo(7);
		std::printf("Permanent interaction stacked=%d: after first expiry base=%u current=%u owner=%u\n",
			int(stacked), after.natureBase, after.natureCur, after.ownerData);
		check(after.natureBase == TERR_N, "temporary expiry cannot discard the permanently edited base");
		check(after.natureCur == TERR_N && after.ownerData == 0,
			"original permanent visible edit is not overwritten by resuming an already-running inactive owner");
		f.direct.OnStep(3);
		check(f.direct.GetNature(7) == TERR_N && !f.direct.IsOverrideActive(301),
			"all temporary entries eventually expire to the edited permanent base");
	}
}

/** Cleared ownership is not cancellation; old timers and future activations still run. */
static void PermanentNatureStacksB13(Checks& check)
{
	const CCircle sample(CVec2(25, 10), 2);
	for (int edit = 0; edit < 3; ++edit)
	{
		PermanentNatureFixtureB13 f(check);
		f.direct.Override(NDb::NATURETYPE_A, sample, 6, 401);
		f.direct.Override(NDb::NATURETYPE_A, sample, 4, 402);
		f.direct.Override(NDb::NATURETYPE_A, sample, 1, 403);
		f.direct.OnStep(0);
		if (edit) f.direct.EditNature(7, edit == 1 ? NDb::NATURETYPE_N : NDb::NATURETYPE_A);
		check(f.direct.GetElementInfo(7).ownerData == (edit ? 0u : 403u),
			"same-visible permanent edit also clears ownership without cancelling entries");
		for (unsigned owner : {401u, 402u, 403u})
			check(f.direct.IsOverrideActive(owner), "permanent edit preserves each independent registry lifetime");
		f.direct.OnStep(1);
		check(f.direct.GetElementInfo(7).ownerData == (edit ? 0u : 402u)
			&& f.direct.GetNature(7) == (edit == 1 ? TERR_N : TERR_A),
			"first stack expiry restores only a cell still owned by that expired entry");
		f.direct.OnStep(3);
		check(f.direct.GetElementInfo(7).ownerData == (edit ? 0u : 401u)
			&& f.direct.GetNature(7) == (edit == 1 ? TERR_N : TERR_A),
			"second stack expiry cannot reacquire ownership cleared by permanent editing");
		check(f.direct.IsOverrideActive(401) && !f.direct.IsOverrideActive(402) && !f.direct.IsOverrideActive(403),
			"inactive-stack restoration does not reset or prematurely cancel registry lifetimes");
		f.direct.OnStep(2);
		check(f.direct.GetElementInfo(7).ownerData == 0 && !f.direct.IsOverrideActive(401)
			&& f.direct.GetNature(7) == (edit == 1 ? TERR_N : edit == 2 ? TERR_A : TERR_B),
			"multi-stack expiry returns to the exact retained permanent base");
	}
	{
		PermanentNatureFixtureB13 f(check);
		f.direct.Override(NDb::NATURETYPE_A, sample, 6, 411);
		f.direct.Override(NDb::NATURETYPE_A, sample, 4, 412);
		f.direct.EditNature(7, NDb::NATURETYPE_N);
		f.direct.Override(NDb::NATURETYPE_A, sample, 1, 413);
		check(f.direct.GetElementInfo(7).ownerData == 413, "a new immediate override can legitimately acquire a permanently edited cell");
		f.direct.OnStep(1);
		check(f.direct.GetElementInfo(7).ownerData == 412 && f.direct.GetNature(7) == TERR_A,
			"new valid ownership retains original stack restoration, not a blanket cancellation policy");
		f.direct.OnStep(5);
		check(f.direct.GetElementInfo(7).ownerData == 0 && f.direct.GetNature(7) == TERR_N,
			"new immediate override and retained stack eventually restore the permanent base");
	}
	for (bool queuedBeforeEdit : {false, true})
	{
		PermanentNatureFixtureB13 f(check, true, 10);
		f.direct.Override(NDb::NATURETYPE_A, sample, 4, 421);
		if (queuedBeforeEdit) f.direct.Override(NDb::NATURETYPE_A, CCircle(CVec2(15, 10), 12), 3, 422);
		f.direct.EditNature(7, NDb::NATURETYPE_N);
		if (!queuedBeforeEdit) f.direct.Override(NDb::NATURETYPE_A, CCircle(CVec2(15, 10), 12), 3, 422);
		f.direct.OnStep(.5f);
		check(f.direct.GetNature(7) == TERR_N && f.direct.GetElementInfo(7).ownerData == 0,
			"permanent edit does not activate a future wave early");
		f.direct.OnStep(.5f);
		check(f.direct.GetNature(7) == TERR_A && f.direct.GetElementInfo(7).ownerData == 422,
			"both pre-existing and newly queued delayed activations may acquire the edited cell");
		f.direct.OnStep(3);
		check(f.direct.GetNature(7) == TERR_N && f.direct.GetElementInfo(7).ownerData == 0,
			"delayed activation retains the original permanent base at final expiry");
	}
	for (bool survivingStack : {false, true})
	{
		PermanentNatureFixtureB13 f(check, true, 10);
		f.direct.Override(NDb::NATURETYPE_A, sample, survivingStack ? 4 : 1, 431);
		f.direct.Override(NDb::NATURETYPE_A, CCircle(CVec2(15, 10), 12), 2, 432);
		f.direct.EditNature(7, NDb::NATURETYPE_N);
		f.direct.OnStep(2);
		check(f.direct.GetNature(7) == (survivingStack ? TERR_A : TERR_N)
			&& f.direct.GetElementInfo(7).ownerData == (survivingStack ? 431u : 0u),
			"same-tick delayed activation and expiry neither reapplies expired changedOverrides nor cancels a live restored owner");
		f.direct.OnStep(2);
		check(f.direct.GetNature(7) == TERR_N && f.direct.GetElementInfo(7).ownerData == 0,
			"pending changedOverrides processing cannot leave ownership behind after final expiry");
	}
}

/** Compare every real cell against original center-distance and polygon classification. */
static void PermanentNatureGeometryB13(Checks& check)
{
	for (int shape = 0; shape < 5; ++shape)
	{
		PermanentNatureFixtureB13 f(check);
		const CVec2 center = shape == 0 ? CVec2(20, 20) : shape == 1 ? CVec2(-5, 0)
			: shape == 2 ? CVec2(25, 10) : shape == 3 ? CVec2(100, 100) : CVec2(0, 0);
		const float radius = shape == 0 ? 10 : shape == 1 ? 15 : shape == 2 ? 2 : shape == 3 ? 1 : 60;
		f.Map()->ChangeNature(center, radius, static_cast<NDb::ENatureType>(-123), NDb::NATURETYPE_B);
		for (int id = 0; id < 20; ++id)
		{
			CVec2 point; f.direct.GetElementPosition(id, point);
			const bool inside = fabs2(center.x - point.x, center.y - point.y) <= radius * radius;
			check(f.Map()->GetNatureInPoint(id) == (inside ? TERR_B : (id % 5) % 3),
				"circle uses original inclusive center-distance on the actual staggered hex grid");
		}
	}
	for (int shape = 0; shape < 7; ++shape)
	{
		PermanentNatureFixtureB13 f(check);
		vector<CVec2> polygon;
		if (shape == 0) polygon.push_back(CVec2(25, 10));
		if (shape == 1) { polygon.push_back(CVec2(5, 10)); polygon.push_back(CVec2(35, 10)); }
		if (shape == 2 || shape == 3)
		{
			polygon.push_back(CVec2(5, 0)); polygon.push_back(CVec2(35, 0));
			polygon.push_back(CVec2(35, 20)); polygon.push_back(CVec2(5, 20));
			if (shape == 3) nstl::reverse(polygon.begin(), polygon.end());
		}
		if (shape == 4)
		{
			polygon.push_back(CVec2(-10, -10)); polygon.push_back(CVec2(30, -10));
			polygon.push_back(CVec2(30, 5)); polygon.push_back(CVec2(10, 5));
			polygon.push_back(CVec2(10, 25)); polygon.push_back(CVec2(-10, 25));
		}
		if (shape == 5) { polygon.push_back(CVec2(100, 100)); polygon.push_back(CVec2(105, 105)); }
		if (shape == 6)
		{
			polygon.push_back(CVec2(-1, -1)); polygon.push_back(CVec2(60, -1));
			polygon.push_back(CVec2(60, 40)); polygon.push_back(CVec2(-1, 40));
		}
		f.Map()->ChangeNature(polygon, static_cast<NDb::ENatureType>(std::numeric_limits<int>::min()), NDb::NATURETYPE_B);
		for (int id = 0; id < 20; ++id)
		{
			CVec2 point; f.direct.GetElementPosition(id, point);
			const bool inside = ClassifyPolygon(polygon, point) != CP_OUTSIDE;
			check(f.Map()->GetNatureInPoint(id) == (inside ? TERR_B : (id % 5) % 3),
				"polygon retains original boundary, point, segment, winding, concave and outside policies");
		}
	}
}

/** Public filters inspect authored base, not the temporary visible value. */
static void PermanentNatureFiltersB13(Checks& check)
{
	for (bool polygon : {false, true})
	{
		PermanentNatureFixtureB13 f(check);
		vector<CVec2> vertices; vertices.push_back(CVec2(25, 10));
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 4, 501);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 1, 502);
		if (polygon) f.Map()->ChangeNature(vertices, NDb::NATURETYPE_A, NDb::NATURETYPE_N);
		else f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_A, NDb::NATURETYPE_N);
		check(f.Map()->GetNatureInPoint(7) == TERR_A, "from filter does not match the temporary visible faction");
		if (polygon) f.Map()->ChangeNature(vertices, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
		else f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
		check(f.Map()->GetNatureInPoint(7) == TERR_N, "from filter matches the original stored base beneath temporary overrides");
		f.World()->Step(1, 1);
		check(f.Map()->GetNatureInPoint(7) == TERR_N && f.observer->GetTerrainType() == TERR_N
			&& f.Map()->IsOverrideActive(501) && !f.Map()->IsOverrideActive(502),
			"world permanent entrypoint retains cleared ownership, timers and actual observer cache through stack expiry");
		f.World()->Step(3, 3);
		check(f.Map()->GetNatureInPoint(7) == TERR_N && !f.Map()->IsOverrideActive(501),
			"world permanent entrypoint survives final original overlay expiry");
	}
	PermanentNatureFixtureB13 f(check, false);
	f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
	vector<CVec2> point; point.push_back(CVec2(25, 10));
	f.Map()->ChangeNature(point, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
	f.Map()->OnStep(0);
	check(f.Map()->GetNatureInPoint(7) == TERR_B && f.observer->GetTerrainType() == TERR_B,
		"loaded noneditable authored terrain rejects both permanent mutation entrypoints");
}

/** Rejected input must not partially mutate cells or the independent override registry. */
static void PermanentNatureInvalidB13(Checks& check)
{
	PermanentNatureFixtureB13 f(check);
	f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 2, 601);
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	for (int bad = 0; bad < 15; ++bad)
	{
		CVec2 center(25, 10); float radius = 2;
		auto from = static_cast<NDb::ENatureType>(-1), to = NDb::NATURETYPE_N;
		if (bad == 0) center.x = nan;
		if (bad == 1) center.y = inf;
		if (bad == 2) center.x = -inf;
		if (bad == 3) center.y = 1e30f;
		if (bad == 4) radius = nan;
		if (bad == 5) radius = inf;
		if (bad == 6) radius = 1e30f;
		if (bad == 7) radius = -1;
		if (bad == 8) radius = 0;
		if (bad == 9) from = static_cast<NDb::ENatureType>(3);
		if (bad == 10) from = static_cast<NDb::ENatureType>(std::numeric_limits<int>::max());
		if (bad == 11) to = static_cast<NDb::ENatureType>(-1);
		if (bad == 12) to = static_cast<NDb::ENatureType>(3);
		if (bad == 13) to = static_cast<NDb::ENatureType>(std::numeric_limits<int>::max());
		if (bad == 14) to = static_cast<NDb::ENatureType>(std::numeric_limits<int>::min());
		f.Map()->ChangeNature(center, radius, from, to);
		check(f.Map()->GetNatureInPoint(7) == TERR_A && f.Map()->IsOverrideActive(601),
			"invalid circle numeric or enum input cannot edit the cell or cancel its owner");
	}
	for (int bad = 0; bad < 10; ++bad)
	{
		vector<CVec2> polygon;
		polygon.push_back(CVec2(24, 9)); polygon.push_back(CVec2(26, 9));
		polygon.push_back(CVec2(26, 11)); polygon.push_back(CVec2(24, 11));
		auto from = static_cast<NDb::ENatureType>(-1), to = NDb::NATURETYPE_N;
		if (bad == 0) polygon[3].x = nan;
		if (bad == 1) polygon[3].y = inf;
		if (bad == 2) polygon[3].x = -inf;
		if (bad == 3) polygon[3].y = 1e30f;
		if (bad == 4) polygon.clear();
		if (bad == 5) from = static_cast<NDb::ENatureType>(3);
		if (bad == 6) from = static_cast<NDb::ENatureType>(std::numeric_limits<int>::max());
		if (bad == 7) to = static_cast<NDb::ENatureType>(-1);
		if (bad == 8) to = static_cast<NDb::ENatureType>(3);
		if (bad == 9) to = static_cast<NDb::ENatureType>(std::numeric_limits<int>::max());
		f.Map()->ChangeNature(polygon, from, to);
		check(f.Map()->GetNatureInPoint(7) == TERR_A && f.Map()->IsOverrideActive(601),
			"invalid polygon is rejected atomically even when an earlier vertex would match");
	}
	f.Map()->OnStep(2);
	check(f.Map()->GetNatureInPoint(7) == TERR_B && !f.Map()->IsOverrideActive(601),
		"rejected edits retain original base and normal expiry");
	{
		const CVec2 loadedSize = f.World()->GetMapSize();
		// Keep geometry small: mismatched context must be rejected before either original iterator runs.
		f.World()->SetMapSize(CVec2(1, 1));
		f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
		check(f.Map()->GetNatureInPoint(7) == TERR_B, "circle rejects a world size changed since the grid was loaded");
		vector<CVec2> point; point.push_back(CVec2(25, 10));
		f.Map()->ChangeNature(point, static_cast<NDb::ENatureType>(-1), NDb::NATURETYPE_A);
		check(f.Map()->GetNatureInPoint(7) == TERR_B, "polygon rejects a world size changed since the grid was loaded");
		f.World()->SetMapSize(loadedSize);
	}
	{
		PermanentNatureFixtureB13 fractional(check, true, 0, 8.5f);
		fractional.Map()->ChangeNature(CVec2(20, 15), 100, static_cast<NDb::ENatureType>(-1), NDb::NATURETYPE_B);
		vector<CVec2> bounds;
		bounds.push_back(CVec2(-10, -10)); bounds.push_back(CVec2(60, -10));
		bounds.push_back(CVec2(60, 60)); bounds.push_back(CVec2(-10, 60));
		fractional.Map()->ChangeNature(bounds, static_cast<NDb::ENatureType>(-1), NDb::NATURETYPE_B);
		bool unchanged = true;
		for (int id = 0; id < 20; ++id) unchanged = unchanged && fractional.Map()->GetNatureInPoint(id) == (id % 5) % 3;
		check(unchanged, "unrepresentable original iterator corner is rejected without an invalid unsigned hex range");
		fractional.Map()->ChangeNature(CVec2(21.25f, 8.5f), 1, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
		check(fractional.Map()->GetNatureInPoint(7) == TERR_N,
			"fractional-step interior circle retains the original iterator when its corners are representable");
		vector<CVec2> point; point.push_back(CVec2(21.25f, 8.5f));
		fractional.Map()->ChangeNature(point, NDb::NATURETYPE_N, NDb::NATURETYPE_A);
		check(fractional.Map()->GetNatureInPoint(7) == TERR_A,
			"fractional-step interior point polygon is not rejected with an unsafe whole-map boundary");
	}
	f.Map()->OnLoaded(nullptr);
	f.Map()->ChangeNature(CVec2(25, 10), 2, NDb::NATURETYPE_B, NDb::NATURETYPE_A);
	vector<CVec2> point; point.push_back(CVec2(25, 10));
	f.Map()->ChangeNature(point, NDb::NATURETYPE_B, NDb::NATURETYPE_A);
	check(f.Map()->GetNatureInPoint(7) == TERR_N, "unloaded world nature has no synthetic mutation fallback");
}

/** Shipped bytes are read-only; only an in-memory descriptor copy enables the edit case. */
static void PermanentNatureAuthoredB13(Checks& check, const char* dataRoot)
{
	if (!dataRoot) return;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	RootFileSystem::RegisterFileSystem(files);
	for (bool editable : {false, true})
	{
		CObj<Stream> xml = files->OpenFile("Maps/Multiplayer/MOBA/_.TERN.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "permanent mutation opens the shipped terrain descriptor read-only");
		if (!xml || !xml->IsOk()) continue;
		auto* terrain = new NDb::Terrain; NDb::Ptr<NDb::Terrain> terrainDb = terrain;
		CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
		NatureQueryDescriptorB12 descriptor = {terrain}; saver->Add("Terrain", &descriptor);
		check(!terrain->natureMap.editableNatureMap, "shipped MOBA descriptor disallows permanent edits by default");
		terrain->natureMap.editableNatureMap = editable;
		auto* adv = new NDb::AdvMap; adv->terrain = terrain;
		auto* desc = new NDb::AdvMapDescription; desc->map = adv;
		NDb::Ptr<NDb::AdvMapDescription> description = desc;
		CObj<NCore::IWorldBase> owner = PFWorld::CreatePFWorld();
		auto* world = dynamic_cast<PFWorld*>(owner.GetPtr());
		const bool loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
		check(loaded && !world->GetScene(), "permanent mutation loads actual shipped grid bytes without renderer storage");
		if (!loaded) continue;
		auto* map = world->GetNatureMap(); CVec2 position(0, 0); bool found = false;
		for (int y = 1; y < 160 && !found; ++y) for (int x = 1; x < 160 && !found; ++x)
		{
			position = CVec2((x + (y & 1) * .5f) * 2.5f, y * 2.5f);
			found = map->GetNatureInPoint(position.x, position.y) == TERR_B;
		}
		check(found, "shipped permanent mutation selects a real authored enemy cell");
		if (!found) continue;
		map->ChangeNature(position, 1, NDb::NATURETYPE_B, NDb::NATURETYPE_N);
		check(map->GetNatureInPoint(position.x, position.y) == (editable ? TERR_N : TERR_B),
			"shipped circle uses loaded storage and the descriptor's permanent edit permission");
		map->OnStep(0);
		vector<CVec2> point; point.push_back(position);
		map->ChangeNature(point, static_cast<NDb::ENatureType>(-1), NDb::NATURETYPE_A);
		map->OnStep(0);
		check(map->GetNatureInPoint(position.x, position.y) == (editable ? TERR_A : TERR_B),
			"shipped point polygon follows original classification and authored edit permission");
	}
	RootFileSystem::UnregisterFileSystem(files);
}

static void PermanentNatureB13(Checks& check, const char* dataRoot)
{
	// Give each isolated world owned DB parameters, not the native static fallback.
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	auto* logic = new NDb::SessionLogicRoot;
	logic->aiLogic = new NDb::AILogicParameters;
	logic->unitLogicParameters = new NDb::UnitsLogicParameters;
	auto* root = new NDb::SessionRoot; root->logicRoot = logic;
	NDb::SessionRoot::InitRoot(root);
	PermanentNatureEntrypointsB13(check);
	PermanentNatureOwnershipB13(check);
	PermanentNatureStacksB13(check);
	PermanentNatureGeometryB13(check);
	PermanentNatureFiltersB13(check);
	PermanentNatureInvalidB13(check);
	PermanentNatureAuthoredB13(check, dataRoot);
	NDb::SessionRoot::InitRoot(saved);
}
