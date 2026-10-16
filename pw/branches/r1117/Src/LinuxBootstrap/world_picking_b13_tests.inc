// Included after the B8/B9, shop, and summon fixtures in applicator_formula_probe.cpp.
#include "../PF_GameLogic/DBVisualRoots.h"

namespace world_picking_b13
{
/** Keep the existing logic root while supplying scoped original UI category data. */
struct Categories
{
	NDb::Ptr<NDb::SessionRoot> saved;
	NDb::Ptr<NDb::SessionRoot> root;
	NDb::Ptr<NDb::SessionUIRoot> ui;
	NDb::Ptr<NDb::UnitCategories> categories;
	Categories() : saved(NDb::SessionRoot::GetRoot())
	{
		auto* session = new NDb::SessionRoot;
		if (saved) session->Assign(*saved);
		root = session;
		auto* visual = new NDb::SessionUIRoot; ui = visual; session->uiRoot = visual;
		auto* values = new NDb::UnitCategories; categories = values; visual->unitCategories = values;
		values->elements.resize(NDb::KnownEnum<NDb::EUnitType>::SizeOf(), NDb::UNITCATEGORY_INVALID);
		for (auto& category : values->elements) category = NDb::UNITCATEGORY_INVALID;
		NDb::SessionRoot::InitRoot(root);
	}
	~Categories() { NDb::SessionRoot::InitRoot(saved); }
	void Set(NDb::EUnitType type, NDb::EUnitCategory category)
	{
		const_cast<NDb::UnitCategories*>(categories.GetPtr())->elements[type] = category;
	}
};

/** Own real engine endpoints; no overridden picking, visibility, or range implementation. */
struct Fixture
{
	B8BehaviourForeignWorld map;
	CObj<B9CastHero> observer;
	std::vector<CObj<PFBaseUnit>> units;
	explicit Fixture(Checks& check) : map(check)
	{
		if (!map.world) return;
		observer = B9MaleHero(map.world, NDb::FACTION_FREEZE);
		observer->TeleportTo(CVec2(3, 3), false, false);
		Keep(observer.GetPtr());
	}
	~Fixture() { for (auto& unit : units) unit->Cleanup(); }
	void Keep(PFBaseUnit* unit)
	{
		unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDATTACK);
		unit->EnableAbilityAI(false); units.emplace_back(unit);
	}
	CObj<B9CastHero> Hero(const CVec2& position, NDb::EFaction faction = NDb::FACTION_FREEZE)
	{
		PFBaseHero::SpawnInfo spawn; spawn.pHero = observer->GetDbHero();
		spawn.placement = Placement(CVec3(position, 0), QNULL, CVec3(1, 1, 1));
		CObj<B9CastHero> hero = new B9CastHero(map.world, spawn, faction);
		hero->EnemyVisible(true); Keep(hero.GetPtr()); return hero;
	}
	PFBaseUnit* Pick(const CVec2& point, bool enemiesOnly = false, const PFAbilityData* ability = nullptr)
	{
		return map.world->PickLinuxUnitAt(point, observer, enemiesOnly, ability);
	}
};

/** Detect the omitted identity from the real snapshot, independently of allocator/hash order. */
static void Uncapped(Checks& check)
{
	for (bool heroes : {true, false})
	{
		Fixture f(check); if (!f.map.world) continue;
		auto* raw = new NDb::AdvMapCreep;
		raw->stats = f.observer->DbUnitDesc()->stats; raw->targetingParams = f.observer->DbUnitDesc()->targetingParams;
		raw->creepType = NDb::CREEPTYPE_FACTIONALNORMAL;
		NDb::Ptr<NDb::AdvMapCreep> db = raw;
		std::vector<CObj<PFBaseMovingUnit>> candidates;
		for (int i = 0; i != 650; ++i)
		{
			if (heroes) candidates.emplace_back(f.Hero(CVec2(6, 6)).GetPtr());
			else
			{
				CObj<PFCommonCreep> creep = new PFCommonCreep(f.map.world, *db, NDb::FACTION_FREEZE,
					Placement(CVec3(6, 6, 0), QNULL, CVec3(1, 1, 1)), vector<CVec2>());
				f.Keep(creep.GetPtr()); candidates.emplace_back(creep.GetPtr());
			}
		}
		f.map.world->ProcessAddRemove();
		vector<LinuxDynamicWorldMarker> markers;
		f.map.world->GetLinuxDynamicWorldMarkers(markers, 640);
		check(markers.size() == 640, "world picking: actual hero/creep markers saturate the legacy 640-entry export");
		PFBaseMovingUnit* omitted = nullptr;
		for (const auto& candidate : candidates)
		{
			bool exported = false;
			for (const auto& marker : markers) exported |= marker.objectId == candidate->GetObjectId();
			if (!exported) { omitted = candidate.GetPtr(); break; }
		}
		check(omitted && f.map.world->FindLinuxUnitByObjectId(omitted->GetObjectId()) == omitted,
			"world picking: overflow target is an actual registered omitted identity, not an assumed allocation ID");
		if (!omitted) continue;
		omitted->TeleportTo(CVec2(16, 12), false, false);
		check(omitted->IsInRange(CVec2(16, 12), 0) && omitted->IsVisibleForFaction(f.observer->GetFaction()),
			"world picking: omitted target has a visible live engine footprint at the isolated click");
		check(f.Pick(CVec2(16, 12)) == omitted, heroes
			? "world picking: registered hero outside 640 markers remains pickable by exact identity"
			: "world picking: registered common creep outside 640 markers remains pickable by exact identity");
		omitted->TeleportTo(CVec2(18, 12), false, false);
		check(!f.Pick(CVec2(16, 12)) && f.Pick(CVec2(18, 12)) == omitted,
			"world picking: current world position replaces the previous marker position immediately");
	}
}

/** Each constructor must be represented even when its type is absent from the visual marker export. */
static void UnitKinds(Checks& check)
{
	for (int kind = 0; kind != 6; ++kind)
	{
		Fixture f(check); if (!f.map.world) continue;
		CObj<PFBaseUnit> unit;
		if (kind == 0)
		{
			const auto db = DeadCreatureDbB13(f.map.world);
			unit = new PFBaseSummonedUnit(CPtr<PFWorld>(f.map.world), db, NDb::UNITTYPE_SUMMON,
				CPtr<PFBaseUnit>(f.observer.GetPtr()), Placement(CVec3(12, 12, 0), QNULL, CVec3(1, 1, 1)), true);
		}
		else
		{
			NDb::Building* db = kind == 1 ? static_cast<NDb::Building*>(new NDb::Tower)
				: kind == 2 ? static_cast<NDb::Building*>(new NDb::MainBuilding)
				: kind == 3 ? static_cast<NDb::Building*>(new NDb::Quarter)
				: kind == 4 ? static_cast<NDb::Building*>(new NDb::Shop) : new NDb::Building;
			db->stats = f.observer->DbUnitDesc()->stats; db->faction = NDb::FACTION_FREEZE;
			NDb::AdvMapObject object; object.gameObject = db; object.lockMap = false;
			object.offset = CPlacement(CVec3(12, 12, 0), QNULL, CVec3(1, 1, 1));
			if (kind == 1) unit = new PFTower(f.map.world, object);
			else if (kind == 2) unit = new PFMainBuilding(f.map.world, object);
			else if (kind == 3) unit = new PFQuarters(f.map.world, object);
			else if (kind == 4) unit = new PFShop(f.map.world, object);
			else unit = new PFBuilding(f.map.world, object);
		}
		f.Keep(unit); f.map.world->ProcessAddRemove();
		unit->SetObjectSizes(6, 6, 6);
		const char* labels[] = {"world picking: actual summon is selectable", "world picking: actual tower is selectable",
			"world picking: actual main building is selectable", "world picking: actual quarters is selectable",
			"world picking: actual shop is selectable", "world picking: actual generic building is selectable"};
		check(f.map.world->FindLinuxUnitByObjectId(unit->GetObjectId()) == unit && f.Pick(CVec2(12, 12)) == unit, labels[kind]);
		check(unit->IsInRange(CVec2(15, 12), 0) && f.Pick(CVec2(15, 12)) == unit,
			"world picking: real circular footprint includes its exact authored boundary");
		check(!unit->IsInRange(CVec2(15.125f, 12), 0) && !f.Pick(CVec2(15.125f, 12)),
			"world picking: clicks outside the actual footprint cannot borrow marker padding");
	}
}

/** Compare occupied and empty tiles after actual building mask rasterization and placement transforms. */
static void BuildingTiles(Checks& check)
{
	for (int transform = 0; transform != 3; ++transform)
	{
		Fixture f(check); if (!f.map.world) continue;
		auto* db = new NDb::Tower; db->stats = f.observer->DbUnitDesc()->stats; db->faction = NDb::FACTION_FREEZE;
		db->collision = CTRect<float>(3, -1, 7, 2); db->lockMask.tileSize = 1;
		db->lockMask.mask.push_back(CTPoint<int>(0, 0)); db->lockMask.mask.push_back(CTPoint<int>(3, 2));
		NDb::AdvMapObject object; object.gameObject = db; object.lockMap = true;
		object.offset = CPlacement(CVec3(14, 14, 0), transform == 0 ? QNULL : CQuat(0.8f, CVec3(0, 0, 1)),
			transform == 2 ? CVec3(1.5f, .75f, 1) : CVec3(1, 1, 1));
		CObj<PFTower> tower = new PFTower(f.map.world, object); f.Keep(tower);
		f.map.world->ProcessAddRemove(); tower->SetObjectSizes(1, 1, 1);
		check(!tower->GetOccupiedTiles().empty(), "world picking: original tower constructor produces a noncentral sparse footprint");
		bool hits = true, outsideCircle = false, misses = true, hole = false;
		TileMap* tiles = f.map.world->GetTileMap();
		for (const auto& tile : tower->GetOccupiedTiles())
		{
			const CVec2 point = tiles->GetPointByTile(tile);
			hits &= tower->IsInRange(point, 0) && f.Pick(point) == tower.GetPtr();
			outsideCircle |= fabs2(point - tower->GetPosition().AsVec2D()) > 9;
		}
		for (int y = 3; y <= 10; ++y) for (int x = 3; x <= 12; ++x)
		{
			const CVec2 point = tiles->GetPointByTile(x, y);
			if (!tower->IsInRange(point, 0)) { hole = true; misses &= !f.Pick(point); }
		}
		check(hits && outsideCircle, "world picking: identity/rotated/scaled occupied tiles pick the tower beyond its center circle");
		check(misses && hole && !tower->IsInRange(tower->GetPosition().AsVec2D(), 0)
			&& !f.Pick(tower->GetPosition().AsVec2D()),
			"world picking: sparse holes and the unoccupied building center do not become invented circular hits");
	}
}

/** Corrupt only stored footprint entries; restore them before any building/map cleanup can consume them. */
static void BuildingTileBounds(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	auto* db = new NDb::Tower; db->stats = f.observer->DbUnitDesc()->stats; db->faction = NDb::FACTION_FREEZE;
	db->collision = CTRect<float>(-1, -1, 1, 1); db->lockMask.tileSize = 1;
	NDb::AdvMapObject object; object.gameObject = db; object.lockMap = true;
	object.offset = CPlacement(CVec3(14, 14, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFTower> tower = new PFTower(f.map.world, object); f.Keep(tower);
	f.map.world->ProcessAddRemove();
	struct RestoreTiles
	{
		vector<SVector>& tiles;
		const vector<SVector> saved;
		explicit RestoreTiles(PFBuilding& building)
			: tiles(const_cast<vector<SVector>&>(building.GetOccupiedTiles())), saved(tiles) {}
		~RestoreTiles() { tiles = saved; }
	} restore(*tower);
	check(!restore.saved.empty(), "world picking: malformed-tile fixture starts with real constructor-owned occupied tiles");
	if (restore.saved.empty()) return;
	TileMap* map = f.map.world->GetTileMap();
	const SVector hit = restore.saved[0];
	const CVec2 point = map->GetPointByTile(hit);
	check(f.Pick(point) == tower, "world picking: valid occupied-tile fixture is selectable before corruption");
	const int low = std::numeric_limits<int>::min(), high = std::numeric_limits<int>::max();
	for (const SVector& bad : {SVector(-1, hit.y), SVector(hit.x, -1),
		SVector(map->GetSizeX(), hit.y), SVector(hit.x, map->GetSizeY()),
		SVector(low, hit.y), SVector(hit.x, low), SVector(high, hit.y), SVector(hit.x, high),
		SVector(46341, 46341)})
	{
		for (bool first : {false, true})
		{
			restore.tiles = restore.saved; restore.tiles.push_back(bad);
			if (first) std::swap(restore.tiles[0], restore.tiles[restore.tiles.size() - 1]);
			// Do not call IsInRange here: legacy SVector subtraction/squaring can overflow.
			check(!f.Pick(point), first
				? "world picking: malformed leading occupied tile is rejected before original integer range arithmetic"
				: "world picking: malformed trailing occupied tile rejects the whole footprint despite an earlier valid hit");
			restore.tiles = restore.saved;
			check(f.Pick(point) == tower, "world picking: restoring valid occupied tiles restores selection immediately");
		}
	}
}

/** Inject malformed endpoint geometry only through an existing test fixture, never through movement/fog. */
static void Geometry(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = f.observer->GetDbHero();
	spawn.placement = Placement(CVec3(12, 12, 0), QNULL, CVec3(1, 1, 1));
	CObj<B12ShopQueryHero> hero = new B12ShopQueryHero(f.map.world, spawn); f.Keep(hero);
	f.map.world->ProcessAddRemove();
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	const float high = std::numeric_limits<float>::max();
	hero->SetObjectSizes(2, 2, 2);
	for (const CVec2& point : {CVec2(12, 12), CVec2(13, 12), CVec2(12, 13),
		CVec2(std::nextafter(13.f, 12.f), 12), CVec2(std::nextafter(13.f, inf), 12), CVec2(13, 13)})
		check(f.Pick(point) == (hero->IsInRange(point, 0) ? hero.GetPtr() : nullptr),
			"world picking: exact, adjacent-float and diagonal click boundaries agree with original IsInRange");
	hero->SetObjectSizes(0, 0, 0);
	check(f.Pick(CVec2(12, 12)) == hero && !f.Pick(CVec2(12.125f, 12)),
		"world picking: zero-size unit has only its exact center, without a synthetic minimum click radius");
	for (float size : {-1.f, nan, inf, -inf, high})
	{
		hero->SetObjectSize(size);
		check(!f.Pick(CVec2(12, 12)), "world picking: negative/nonfinite/unrepresentable footprint sizes fail closed");
	}
	hero->SetObjectSizes(2, 2, 2);
	for (const CVec3& point : {CVec3(nan, 12, 0), CVec3(12, nan, 0), CVec3(12, 12, nan),
		CVec3(inf, 12, 0), CVec3(12, -inf, 0), CVec3(high, high, 0), CVec3(-high, 12, 0)})
	{
		hero->Position(point);
		check(!f.Pick(CVec2(12, 12)), "world picking: malformed live XYZ geometry is rejected before visibility/tile conversion");
	}
	hero->Position(CVec3(12, 12, 0));
	const CVec2 bounds = f.map.world->GetMapSize();
	for (const CVec2& point : {CVec2(nan, 12), CVec2(12, nan), CVec2(inf, 12), CVec2(12, -inf),
		CVec2(high, high), CVec2(-high, 12), CVec2(-.125f, 12), CVec2(12, -.125f),
		CVec2(bounds.x, 12), CVec2(12, bounds.y), CVec2(bounds.x + 1, bounds.y + 1)})
		check(!f.Pick(point), "world picking: invalid or out-of-map clicks fail before any integer tile conversion");
	hero->Position(CVec3(0, 0, 0));
	check(f.Pick(CVec2(0, 0)) == hero && !f.Pick(CVec2(-.125f, 0)) && !f.Pick(CVec2(0, -.125f)),
		"world picking: zero map edge is valid but overlapping footprint does not admit negative clicks");
	hero->Position(CVec3(std::nextafter(bounds.x, 0.f), std::nextafter(bounds.y, 0.f), 0));
	check(f.Pick(hero->GetPosition().AsVec2D()) == hero && !f.Pick(bounds),
		"world picking: the last in-map float is valid and upper map bounds are exclusive");
	hero->Position(CVec3(12, 12, 0));
	check(f.Pick(CVec2(12, 12)) == hero, "world picking: rejected numeric inputs do not poison subsequent valid queries");
}

/** Live fog, faction, flags, death, and world registration decide eligibility independently of pointer lifetime. */
static void Eligibility(Checks& check)
{
	Fixture f(check), foreign(check); if (!f.map.world || !foreign.map.world) return;
	const CVec2 point(12, 12);
	auto enemy = f.Hero(point, NDb::FACTION_BURN);
	enemy->TeleportTo(CVec2(32, 20), false, false); f.map.world->ProcessAddRemove();
	check(!enemy->IsVisibleForFaction(NDb::FACTION_FREEZE) && !f.Pick(CVec2(32, 20)),
		"world picking: cached enemy visibility cannot bypass current faction war fog");
	enemy->TeleportTo(point, false, false);
	B8BehaviourReveal reveal(f.map.world, NDb::FACTION_FREEZE);
	f.map.world->ProcessAddRemove();
	check(enemy->IsVisibleForFaction(NDb::FACTION_FREEZE) && f.Pick(point, true) == enemy,
		"world picking: a visible enemy is selected by exact pointer in enemies-only mode");
	enemy->EnemyVisible(false);
	check(!f.Pick(point), "world picking: hidden enemy stays unpickable even with a retained registered identity");
	enemy->EnemyVisible(true); enemy->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	check(!f.Pick(point), "world picking: nonself FORBIDPICK excludes an otherwise visible enemy");
	enemy->RemoveFlag(NDb::UNITFLAG_FORBIDPICK); enemy->ChangeFaction(NDb::FACTION_FREEZE);
	check(f.Pick(point) == enemy && !f.Pick(point, true), "world picking: faction changes immediately alter enemies-only eligibility");
	enemy->Hide(true);
	check(enemy->GetHiddenCounter() > 0 && enemy->IsVisibleForFaction(NDb::FACTION_FREEZE) && !f.Pick(point),
		"world picking: actual hidden allied unit is excluded despite own-faction visibility bypass");
	enemy->Hide(false);
	check(f.Pick(point) == enemy, "world picking: original Hide(false) restores the live allied endpoint");
	enemy->ChangeFaction(NDb::FACTION_NEUTRAL); enemy->EnemyVisible(true);
	check(f.Pick(point, true) == enemy, "world picking: a visible neutral is hostile to the observer faction");
	enemy->ChangeFaction(NDb::FACTION_BURN); enemy->EnemyVisible(true);
	check(f.Pick(point, true) == enemy, "world picking: restored enemy relation restores selection");
	f.observer->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	check(f.Pick(f.observer->GetPosition().AsVec2D()) == f.observer && !f.Pick(f.observer->GetPosition().AsVec2D(), true),
		"world picking: observer retains the original self FORBIDPICK exception, but is never its own enemy");
	f.observer->RemoveFlag(NDb::UNITFLAG_FORBIDPICK);
	f.map.world->UnsubscribeObject(enemy.GetPtr());
	check(f.Pick(point, true) == enemy, "world picking: registered unit remains selectable independently of its step subscription");
	f.map.world->UnregisterObject(enemy.GetPtr(), false); f.map.world->ProcessAddRemove();
	check(!f.Pick(point) && !f.map.world->FindLinuxUnitByObjectId(enemy->GetObjectId()),
		"world picking: external strong references cannot retain an unregistered unit in the pick set");
	auto remote = foreign.Hero(point); foreign.map.world->ProcessAddRemove();
	check(!f.Pick(point), "world picking: identical foreign-world coordinates cannot enter the local registry traversal");
	auto dying = f.Hero(point); f.map.world->ProcessAddRemove();
	check(f.Pick(point) == dying, "world picking: fresh live replacement enters the authoritative registry");
	dying->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(dying->IsDead() && !f.Pick(point), "world picking: dead hero is excluded before its retained corpse retires");
	// The ordinary B8 descriptor has no death parameters, so it never enters forced corpse removal.
	const auto corpseDb = DeadCreatureDbB13(f.map.world);
	CObj<DeadCreatureB13<B8BehaviourCreep>> retired = new DeadCreatureB13<B8BehaviourCreep>(
		f.map.world, *corpseDb, NDb::FACTION_FREEZE);
	f.Keep(retired);
	check(retired->GetDeathParams() && retired->GetDeathParams() == corpseDb->deathParameters.GetPtr(),
		"world picking: forced-retirement fixture supplies the original death-parameter prerequisite");
	retired->TeleportTo(CVec2(18, 12), false, false); f.map.world->ProcessAddRemove();
	check(f.Pick(CVec2(18, 12)) == retired, "world picking: real creep is selectable before forced corpse retirement");
	const float retirementTime = f.map.world->GetTimeElapsed();
	retired->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(retired->removals == 1 && retired->IsObjectDead() && f.map.world->GetTimeElapsed() == retirementTime,
		"world picking: real forced corpse hook retires the creep exactly once without a positive world step");
	f.map.world->ProcessAddRemove();
	check(!f.map.world->FindLinuxUnitByObjectId(retired->GetObjectId()),
		"world picking: forced corpse retirement removes the retained creep from the authoritative registry");
	check(!f.Pick(CVec2(18, 12)),
		"world picking: retired creep with an external owner cannot remain in the pick set");
}

/** Optional ability context cannot borrow a different owner, missing descriptor, or invalid observer identity. */
static void ObserverAndAbility(Checks& check)
{
	Fixture f(check), foreign(check); if (!f.map.world || !foreign.map.world) return;
	const CVec2 point(12, 12);
	auto target = f.Hero(point);
	auto* raw = new NDb::Ability; raw->type = NDb::ABILITYTYPE_ACTIVE; raw->targetType = NDb::SPELLTARGET_ALLY;
	NDb::Ptr<NDb::Ability> db = raw;
	CObj<PFAbilityData> local = new PFAbilityData(f.observer.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityData> otherOwner = new PFAbilityData(target.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityData> remote = new PFAbilityData(foreign.observer.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityData> missing = new PFAbilityData(f.observer.GetPtr(), NDb::Ptr<NDb::Ability>(), NDb::ABILITYTYPEID_SPECIAL);
	f.map.world->ProcessAddRemove(); foreign.map.world->ProcessAddRemove();
	check(f.Pick(point) == target && f.Pick(point, false, local) == target,
		"world picking: absent optional ability and valid observer-owned ability both select the live endpoint");
	check(!f.Pick(point, false, otherOwner), "world picking: same-world ability owned by another unit cannot borrow the observer");
	check(!f.Pick(point, false, remote), "world picking: foreign-world ability owner cannot authorize local picking");
	check(!missing->GetDBDesc() && !f.Pick(point, false, missing),
		"world picking: supplied ability with missing descriptor is rejected rather than treated as no ability");
	check(!f.map.world->PickLinuxUnitAt(point, nullptr, false)
		&& !f.map.world->PickLinuxUnitAt(point, nullptr, false, local),
		"world picking: null observer fails with and without optional ability context");
	check(!f.map.world->PickLinuxUnitAt(point, foreign.observer, false)
		&& !f.map.world->PickLinuxUnitAt(point, foreign.observer, false, remote),
		"world picking: foreign observer fails even when its own ability is internally consistent");
	f.map.world->UnregisterObject(f.observer.GetPtr(), false); f.map.world->ProcessAddRemove();
	check(!f.map.world->FindLinuxUnitByObjectId(f.observer->GetObjectId()) && !f.observer->IsDead(),
		"world picking: unregistered observer fixture retains a live pointer but no authoritative world identity");
	check(!f.Pick(point) && !f.Pick(point, false, local),
		"world picking: retained unregistered observer cannot pick with or without its retained ability");
}

/** Original category preferences precede distance; distance then actual object ID resolve equal ranks. */
static void Priority(Checks& check)
{
	Categories root;
	Fixture f(check); if (!f.map.world) return;
	B8BehaviourReveal reveal(f.map.world, NDb::FACTION_FREEZE);
	const CVec2 point(12, 12);
	auto ally = f.Hero(CVec2(12.5f, 12)); auto enemy = f.Hero(CVec2(13, 12), NDb::FACTION_BURN);
	auto creep = B8BehaviourMoving(f.map.world, NDb::FACTION_BURN); f.Keep(creep);
	creep->TeleportTo(point, false, false); creep->EnemyVisible(true);
	for (PFBaseUnit* unit : {static_cast<PFBaseUnit*>(ally.GetPtr()), static_cast<PFBaseUnit*>(enemy.GetPtr()), static_cast<PFBaseUnit*>(creep.GetPtr())})
		unit->SetObjectSizes(4, 4, 4);
	f.map.world->ProcessAddRemove();
	check(f.Pick(point) == creep, "world picking: uncategorized heroes receive no invented priority over the nearest creep");
	root.Set(NDb::UNITTYPE_HEROMALE, NDb::UNITCATEGORY_HEROES);
	check(f.Pick(point) == enemy && f.Pick(point, true) == enemy,
		"world picking: configured enemy hero preference outranks a closer allied hero and enemy creep");
	auto* raw = new NDb::Ability; raw->type = NDb::ABILITYTYPE_ACTIVE;
	NDb::Ptr<NDb::Ability> db = raw;
	CObj<PFAbilityData> ability = new PFAbilityData(f.observer.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	raw->targetType = NDb::SPELLTARGET_ALLY;
	check(f.Pick(point, false, ability) == ally && f.Pick(point, true, ability) == creep,
		"world picking: ALLY without ENEMY prefers allied heroes without bypassing enemies-only admission");
	for (NDb::ESpellTarget mask : {NDb::SPELLTARGET_ENEMY,
		static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_ALLY | NDb::SPELLTARGET_ENEMY), NDb::SPELLTARGET_SELF})
	{
		raw->targetType = mask;
		check(f.Pick(point, false, ability) == enemy, "world picking: enemy, mixed and non-ally masks retain original enemy-hero preference");
	}
	root.Set(NDb::UNITTYPE_HEROMALE, NDb::UNITCATEGORY_INVALID);
	root.Set(NDb::UNITTYPE_CREEP, NDb::UNITCATEGORY_HEROES);
	creep->TeleportTo(CVec2(13.5f, 12), false, false);
	check(f.Pick(point) == creep, "world picking: authored category mapping, not C++ hero type, determines preference");
	root.Set(NDb::UNITTYPE_CREEP, NDb::UNITCATEGORY_INVALID);
	check(f.Pick(point) == ally, "world picking: equal nonpreferred rank chooses center distance before object ID");
	root.Set(NDb::UNITTYPE_HEROMALE, NDb::UNITCATEGORY_HEROES);
	const_cast<NDb::SessionUIRoot*>(root.ui.GetPtr())->unitCategories = nullptr;
	check(f.Pick(point) == ally, "world picking: missing unit categories do not imply hard-coded hero priority");
	const_cast<NDb::SessionRoot*>(root.root.GetPtr())->uiRoot = nullptr;
	check(f.Pick(point) == ally, "world picking: missing UI root falls back to geometric ordering");
	const_cast<NDb::SessionRoot*>(root.root.GetPtr())->uiRoot = root.ui;
	const_cast<NDb::SessionUIRoot*>(root.ui.GetPtr())->unitCategories = root.categories;
	const_cast<NDb::UnitCategories*>(root.categories.GetPtr())->elements.clear();
	check(f.Pick(point) == ally, "world picking: truncated category elements are bounded and confer no preference");
	ally->TeleportTo(point, false, false); enemy->TeleportTo(point, false, false);
	creep->TeleportTo(point, false, false);
	PFBaseUnit* expected = ally.GetPtr();
	for (PFBaseUnit* unit : {static_cast<PFBaseUnit*>(enemy.GetPtr()), static_cast<PFBaseUnit*>(creep.GetPtr())})
		if (unit->GetObjectId() < expected->GetObjectId()) expected = unit;
	const NRandom::RandomGenerator saved(*f.map.world->GetRndGen());
	const float health = expected->GetHealth(), mana = f.observer->GetMana();
	for (int repeat = 0; repeat != 8; ++repeat)
		check(f.Pick(point) == expected, "world picking: equal-distance overlapping units use the smallest live ID deterministically");
	check(GlyphRngEqualB13(*f.map.world->GetRndGen(), saved) && expected->GetHealth() == health
		&& f.observer->GetMana() == mana && !f.observer->HasQueuedStates(),
		"world picking: repeated selection is read-only for RNG, resources and hero orders");
	expected->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	PFBaseUnit* next = nullptr;
	for (PFBaseUnit* unit : {static_cast<PFBaseUnit*>(ally.GetPtr()), static_cast<PFBaseUnit*>(enemy.GetPtr()), static_cast<PFBaseUnit*>(creep.GetPtr())})
		if (unit != expected && (!next || unit->GetObjectId() < next->GetObjectId())) next = unit;
	check(f.Pick(point) == next, "world picking: forbidden tie winner is skipped in favor of the next live ID");
}

/** Load shipped tower and summon leaf through the same filesystem/cache setup as other B13 probes. */
static void Authored(Checks& check, const char* dataRoot)
{
	check(dataRoot && *dataRoot, "world picking: shipped asset proof requires original data root");
	if (!dataRoot || !*dataRoot) return;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		Fixture f(check);
		const auto towerDb = NDb::Get<NDb::Tower>(NDb::DBID("/Maps/Multiplayer/MOBA/Towers/TopA1.TOWR.xdb"));
		const auto summonDb = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/Demonolog/SummonDemons.APLR.xdb"));
		const auto talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Demonolog/Ability0.TALENT.xdb"));
		const auto rules = NDb::Get<NDb::AILogicParameters>(NDb::DBID("/GameLogic/AILogic.AILP.xdb"));
		check(f.map.world && towerDb && summonDb && talent && rules, "world picking: shipped TopA1 and Demonologist summon resources load");
		if (f.map.world && towerDb)
		{
			NDb::AdvMapObject object; object.gameObject = towerDb; object.lockMap = true;
			object.offset = CPlacement(CVec3(20, 16, 0), CQuat(.7f, CVec3(0, 0, 1)), CVec3(1.25f, 1.25f, 1));
			CObj<PFTower> tower = new PFTower(f.map.world, object); f.Keep(tower);
			f.map.world->ProcessAddRemove();
			bool exact = !tower->GetOccupiedTiles().empty();
			for (const auto& tile : tower->GetOccupiedTiles())
			{
				const CVec2 point = f.map.world->GetTileMap()->GetPointByTile(tile);
				exact &= tower->IsInRange(point, 0) && f.Pick(point) == tower.GetPtr();
			}
			check(tower->DbUnitDesc() == towerDb.GetPtr() && exact,
				"world picking: shipped TopA1 selects the exact tower across its transformed original occupied tiles");
		}
		if (f.map.world && summonDb && talent && rules)
		{
			auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
			const auto saved = ai->globalConstants; ai->globalConstants = rules->globalConstants;
			{
				NaftaFixtureB13 cast(f.map.world); cast.abilityDb = talent.GetPtr();
				cast.ability = new PFAbilityData(cast.sender.GetPtr(), cast.abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
				cast.instance = new PFAbilityInstance(cast.ability, Target(cast.receiver.GetPtr()), true);
				cast.sender->GetVariableVWM("SaveDemons")->SetBaseValue(1);
				auto app = cast.Create(summonDb);
				check(app && cast.Activate(app), "world picking: shipped Demonologist leaf executes the real summon applicator");
				const auto summons = SummonCountUnitsB13(f.map.world, cast.sender);
				check(summons.size() == 1, "world picking: authored leaf creates one actual owned summon");
				if (summons.size() == 1)
				{
					summons[0]->TeleportTo(CVec2(12, 12), false, false);
					check(summons[0]->DbUnitDesc() == summonDb->summonedUnits[0].GetPtr()
						&& f.Pick(CVec2(12, 12)) == summons[0].GetPtr(),
						"world picking: original Demon2 leaf creature is pickable although omitted from visual marker kinds");
				}
			}
			ai->globalConstants = saved;
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
}
}

/** Focused full-engine C110 regression entry point; the parent harness supplies the dedicated CLI mode. */
static void WorldPickingB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	world_picking_b13::Uncapped(check);
	world_picking_b13::UnitKinds(check);
	world_picking_b13::BuildingTiles(check);
	world_picking_b13::BuildingTileBounds(check);
	world_picking_b13::Geometry(check);
	world_picking_b13::Eligibility(check);
	world_picking_b13::ObserverAndAbility(check);
	world_picking_b13::Priority(check);
	world_picking_b13::Authored(check, dataRoot);
	std::printf("B13 world picking checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
