#include "../PF_GameLogic/PFWorldNatureMap.h"
#include "../Terrain/NatureMap.h"
#include "../System/Crc32Checksum.h"
#include "../System/FileSystem/TestFileSystem.h"
#include "../System/FileSystem/WinFileSystem.h"
#include "../System/MemoryStream.h"

/** Authored Windows-format mock bytes, consumed by the real non-editable grid loader. */
static string NatureQueryImageB12(const NDb::DBNatureMap& db, int invalidCell = 0)
{
	Crc32Checksum crc;
	const float width = 40, height = 30;
	crc.Add(width).Add(height).Add(db.gridStep).Add(db.segmentCount);
	for (const auto* roads : {&db.attackSpaceRoads, &db.logicRoads})
		for (const auto& road : *roads)
		{
			crc.Add(road.leftWidth).Add(road.centerWidth).Add(road.rightWidth);
			if (!road.nodes.empty()) crc.Add(reinterpret_cast<const unsigned char*>(&road.nodes[0]), road.nodes.size() * sizeof(CVec2));
		}
	crc.Add(db.baseA).Add(db.baseAOffset).Add(db.baseB).Add(db.baseBOffset);
	const unsigned elemSize = 16, version = 3, checksum = crc.Add(elemSize).Add(version).Get();
	CObj<MemoryStream> image = new MemoryStream;
	image->Write(&checksum, sizeof(checksum));
	// Stored attack-space geometry is unused by read-only cell lookup, but retain its actual file layout.
	const CVec2 zero(0, 0);
	image->Write(&zero, sizeof(zero));
	for (int i = 0; i < 6 * (db.segmentCount + 3); ++i) image->Write(&zero, sizeof(zero));
	const char solver[48] = {};
	for (int i = 0; i < 5 * (db.segmentCount + 2); ++i) image->Write(solver, sizeof(solver));
	for (int y = 0; y < 4; ++y) for (int x = 0; x < 5; ++x)
	{
		Terrain::NatureMapElementInfo cell = {};
		cell.attackPos = CVec2(float(x), float(y));
		cell.natureCur = cell.natureOld = cell.natureBase = x % 3;
		if (x == 0 && y == 0)
		{
			if (invalidCell == 1) cell.attackPos.x = std::numeric_limits<float>::quiet_NaN();
			if (invalidCell == 2) cell.natureCur = 3;
			if (invalidCell == 3) cell.attackPos.x = std::numeric_limits<float>::max();
		}
		image->Write(&cell, sizeof(cell));
	}
	return string(image->GetBuffer(), image->GetSize());
}

/** Check the original codec in memory; no file saving or renderer is used. */
struct NatureQueryCodecB12 : Terrain::NatureMap
{
	NatureMapElementIdList modified;
	NatureQueryCodecB12() : Terrain::NatureMap(false) {}
	bool Encode(Stream* out) const { return OnSave(out, false); }
	bool OnLoadAtRuntime(Stream*, int, bool) override { return false; }
	bool OnSaveAtRuntime(Stream*, bool) const override { return false; }
};

/** No nature-query override: payment and admission use the actual PFWorldNatureMap. */
static void NatureQueryB12(Checks& check)
{
	B8BehaviourForeignWorld isolated(check);
	PFWorld* world = isolated.world;
	if (!world) return;
	CObj<TestFileSystem> files = new TestFileSystem("", false);
	RootFileSystem::RegisterFileSystem(files);
	NDb::Terrain* raw = new NDb::Terrain;
	raw->elemXCount = 4; raw->elemYCount = 3; raw->tilesPerElement = 4;
	raw->natureMap.gridStep = 10; raw->natureMap.segmentCount = 3;
	raw->natureMap.natureMapFile = "/Mock/NatureQueryB12.naturemap";
	NDb::Ptr<NDb::Terrain> db = raw;
	files->SetFileWithContents(raw->natureMap.natureMapFile, NatureQueryImageB12(raw->natureMap));
	{
		NatureQueryCodecB12 codec;
		const Terrain::NatureMapParams params = {&raw->natureMap, CVec3(40, 30, 50)};
		const bool loaded = codec.Initialize(params, &codec.modified);
		check(loaded, "original non-editable codec accepts a Windows-format authored image");
		CObj<MemoryStream> out = new MemoryStream;
		check(loaded && codec.Encode(out), "native codec round trip writes successfully in memory");
		check(string(out->GetBuffer(), out->GetSize()) == NatureQueryImageB12(raw->natureMap),
			"native save keeps the same 32-bit checksum and exact Windows gameplay bytes");
	}
	// TestFileSystem owns one stream per name; the original loader closes it.
	files->SetFileWithContents(raw->natureMap.natureMapFile, NatureQueryImageB12(raw->natureMap));
	auto* map = world->GetNatureMap();
	map->OnLoaded(db);
	check(!world->GetScene(), "nature grid loads without a scene or renderer");
	for (int x = 0; x < 5; ++x)
	{
		check(map->LocateElement(x * 10.f, 0) == x, "original hex-grid locator returns the authored row cell");
		check(map->GetNatureInPoint(x * 10.f, 0) == x % 3, "world position reads the authored neutral/freeze/burn cell");
		check(map->GetNatureInPoint(x) == x % 3, "world element ID reads the same authored cell");
	}
	check(map->LocateElement(15, 9) == 6 && map->GetNatureInPoint(15.f, 9.f) == TERR_A,
		"odd-row hex selection is the original algorithm, not rectangular rounding");
	check(map->LocateElement(-4, 0) == 0 && map->LocateElement(-6, 0) == -1,
		"original edge hexagons are retained without clamping to rectangular map bounds");
	map->Reset();
	map->OnStep(.25f);
	check(map->GetNatureInPoint(20.f, 0.f) == TERR_B, "reset and disabled native evolution retain the loaded authored query grid");
	for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()})
		check(map->LocateElement(invalid, 0) == -1 && map->GetNatureInPoint(0.f, invalid) == TERR_N,
			"nonfinite and unrepresentable grid coordinates fail before integer conversion");
	check(map->GetNatureInPoint(-1) == TERR_N && map->GetNatureInPoint(20) == TERR_N
		&& map->GetNatureInPoint(INT_MAX) == TERR_N, "invalid cell IDs remain neutral without indexing the grid");
	for (float x : {0.f, 10.f, 20.f})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		auto* talentDb = new NDb::Talent; talentDb->manaCost.sString = "20";
		talentDb->cooldownTime.sString = "5"; talentDb->naftaCost = 0; talentDb->isUnitFreeAfterCast = true;
		auto* limitation = new NDb::PositionCastLimitation;
		limitation->condition.sString = "appl.GetNatureTypeInPos(rPos2D) != sFctn && appl.GetNatureTypeInPos(rPos2D) != TERR_N";
		talentDb->castLimitations.push_back(limitation);
		NDb::Ptr<NDb::Talent> talentOwner = talentDb;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), talentOwner, 1, 0, 0, false);
		check(talent->Activate(), "real nature-limited talent activates");
		check(talent->GetNatureTypeInPos(CVec2(x, 0)) == int(x / 10), "actual ability queries the authored grid without a test override");
		CObj<PFAbilityInstance> cast = hero->UseTalent(talent, Target(CVec3(x, 0, 0)));
		check(bool(cast) == (x == 20) && hero->GetMana() == (x == 20 ? 180 : 200)
			&& talent->GetCurrentCooldown() == (x == 20 ? 5 : 0), "authored enemy nature alone admits a paid BurnLand cast");
		hero->Cleanup();
	}
	for (int bad = 0; bad < 12; ++bad)
	{
		string image = NatureQueryImageB12(raw->natureMap, bad == 2 ? 1 : bad == 9 ? 2 : bad == 10 ? 3 : 0);
		if (bad == 0) image[0] ^= 1;
		if (bad == 1) image.resize(7);
		if (bad == 11) image.resize(image.size() - 1);
		files->SetFileWithContents(raw->natureMap.natureMapFile, image);
		const float step = raw->natureMap.gridStep;
		const int segments = raw->natureMap.segmentCount;
		if (bad == 3) raw->natureMap.gridStep = 0;
		if (bad == 4) raw->natureMap.gridStep = std::numeric_limits<float>::quiet_NaN();
		if (bad == 5) raw->natureMap.gridStep = std::numeric_limits<float>::denorm_min();
		if (bad == 6) raw->natureMap.gridStep = -1;
		if (bad == 7) raw->natureMap.segmentCount = 0;
		if (bad == 8) raw->natureMap.segmentCount = INT_MAX;
		map->OnLoaded(db);
		check(map->LocateElement(20, 0) == -1 && map->GetNatureInPoint(20.f, 0.f) == TERR_N,
			"invalid or corrupt replacement cannot retain stale terrain or generate replacement factions");
		raw->natureMap.gridStep = step;
		raw->natureMap.segmentCount = segments;
	}
	files->SetFileWithContents(raw->natureMap.natureMapFile, NatureQueryImageB12(raw->natureMap));
	map->OnLoaded(db);
	check(map->GetNatureInPoint(20.f, 0.f) == TERR_B, "valid authored grid reload recovers after rejected data");
	raw->natureMap.natureMapFile = "/Mock/MissingNatureB12.naturemap";
	map->OnLoaded(db);
	check(map->LocateElement(20, 0) == -1 && map->GetNatureInPoint(20.f, 0.f) == TERR_N,
		"missing file remains an unavailable neutral grid, never editable generated terrain");
	RootFileSystem::UnregisterFileSystem(files);
}

/** Deserialize the shipped grid descriptor through the original DB serializer, without loading visuals. */
struct NatureQueryDescriptorB12
{
	NDb::Terrain* terrain;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("elemXCount", &terrain->elemXCount);
		saver.Add("elemYCount", &terrain->elemYCount);
		saver.Add("tilesPerElement", &terrain->tilesPerElement);
		saver.Add("natureMap", &terrain->natureMap);
		return 0;
	}
};

static void NatureQueryAuthoredB12(Checks& check, const char* dataRoot)
{
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Maps/Multiplayer/MOBA/_.TERN.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "shipped MOBA terrain XML is readable");
		if (xml && xml->IsOk())
		{
			auto* terrain = new NDb::Terrain;
			NDb::Ptr<NDb::Terrain> db = terrain;
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			NatureQueryDescriptorB12 descriptor = {terrain};
			saver->Add("Terrain", &descriptor);
			check(terrain->elemXCount == 41 && terrain->elemYCount == 41 && terrain->natureMap.gridStep == 2.5f
				&& terrain->natureMap.segmentCount == 31, "original DB serializer loads actual authored nature dimensions");
			auto* adv = new NDb::AdvMap; adv->terrain = terrain;
			auto* desc = new NDb::AdvMapDescription; desc->map = adv;
			NDb::Ptr<NDb::AdvMapDescription> description = desc;
			CObj<NCore::IWorldBase> owner = PFWorld::CreatePFWorld();
			auto* world = dynamic_cast<PFWorld*>(owner.GetPtr());
			const bool loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
			check(loaded && !world->GetScene(), "real MOBA nature descriptor loads in a scene-less engine world");
			if (loaded)
			{
				auto* map = world->GetNatureMap();
				CObj<Stream> binary = RootFileSystem::OpenFile(terrain->natureMap.natureMapFile, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
				const int columns = 165, rows = 165;
				const int offset = 4 + 8 + 6 * (31 + 3) * 8 + 5 * (31 + 2) * 48;
				// NatureMapVisual appends its own payload after the gameplay cells.
				check(binary && binary->GetSize() >= offset + columns * rows * 16, "shipped Windows grid contains the exact 32-bit gameplay layout");
				if (binary && binary->GetSize() >= offset + columns * rows * 16)
				{
					binary->Seek(offset, SEEKORIGIN_BEGIN);
					int counts[3] = {}, mismatches = 0;
					CVec2 examples[3];
					for (int i = 0; i < columns * rows; ++i)
					{
						Terrain::NatureMapElementInfo cell = {};
						binary->Read(&cell, sizeof(cell));
						const int expected = cell.natureCur;
						const CVec2 pos((i % columns + ((i / columns) & 1) * .5f) * 2.5f, (i / columns) * 2.5f);
						if (expected < 3)
						{
							++counts[expected];
							if (pos.x < 400 && pos.y < 400) examples[expected] = pos;
						}
						if (map->LocateElement(pos.x, pos.y) != i || map->GetNatureInPoint(i) != expected
							|| map->GetNatureInPoint(pos.x, pos.y) != expected) ++mismatches;
					}
					std::printf("Authored MOBA grid: neutral=%d freeze=%d burn=%d mismatches=%d cells=%d\n",
						counts[0], counts[1], counts[2], mismatches, columns * rows);
					check(counts[0] && counts[1] && counts[2], "shipped grid contains actual neutral and both faction cells");
					check(mismatches == 0, "every shipped cell ID and hex center agrees with the original Windows payload");
					CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
					NDb::Ptr<NDb::Talent> talentDb = new NDb::Talent;
					CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), talentDb, 1, 0, 0, false);
					for (int nature = 0; nature < 3; ++nature)
						check(talent->GetNatureTypeInPos(examples[nature]) == nature, "actual ability getter reads shipped authored nature without an override");
					hero->Cleanup();
				}
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
}
