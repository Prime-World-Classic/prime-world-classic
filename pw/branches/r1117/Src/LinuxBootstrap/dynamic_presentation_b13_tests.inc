// Included after world_picking_b13_tests.inc to reuse its real-world and hero fixtures.
#include "../PF_GameLogic/PFNeutralCreep.h"

namespace dynamic_presentation_b13
{
using Fixture = world_picking_b13::Fixture;

/** Inspect value snapshots by actual identity without retaining an engine endpoint. */
static const LinuxDynamicWorldMarker* Find(const vector<LinuxDynamicWorldMarker>& markers, int id)
{
	for (const auto& marker : markers) if (marker.objectId == id) return &marker;
	return nullptr;
}

/** Seed caller storage so empty-result tests also verify replacement of an earlier snapshot. */
static vector<LinuxDynamicWorldMarker> Read(Fixture& f, int limit, const CVec2& focus, int preferred = -1, bool minimap = false)
{
	vector<LinuxDynamicWorldMarker> markers; markers.push_back(LinuxDynamicWorldMarker());
	f.map.world->GetLinuxVisibleDynamicWorldMarkers(markers, limit, f.observer, focus, preferred, minimap);
	return markers;
}

/** Compare ordering only; marker payload assertions remain tied to their original engine producers. */
static std::vector<int> Ids(const vector<LinuxDynamicWorldMarker>& markers)
{
	std::vector<int> ids;
	for (const auto& marker : markers) ids.push_back(marker.objectId);
	return ids;
}

/** Compare each initialized death field, avoiding struct padding and any invented corpse model. */
static bool SameDeath(const LinuxDeathPresentation& a, const LinuxDeathPresentation& b)
{
	return a.sequence == b.sequence && a.startedAt == b.startedAt && a.fallStartedAt == b.fallStartedAt
		&& a.fallSpeed == b.fallSpeed && a.active == b.active;
}

/** Selection is global before truncation; observer and eligible selection reserve the first two slots. */
static void Overflow(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	std::vector<CObj<B9CastHero>> heroes;
	for (int i = 0; i != 650; ++i) heroes.push_back(f.Hero(CVec2(6, 6)));
	f.map.world->ProcessAddRemove();
	vector<LinuxDynamicWorldMarker> legacy;
	f.map.world->GetLinuxDynamicWorldMarkers(legacy, 640);
	check(legacy.size() == 640, "dynamic presentation: actual registered heroes saturate the legacy marker export");
	std::vector<B9CastHero*> omitted;
	for (const auto& hero : heroes) if (!Find(legacy, hero->GetObjectId())) omitted.push_back(hero.GetPtr());
	std::sort(omitted.begin(), omitted.end(), [](const B9CastHero* a, const B9CastHero* b) {
		return a->GetObjectId() > b->GetObjectId();
	});
	check(omitted.size() >= 2, "dynamic presentation: overflow identities are observed omissions, not assumed allocation IDs");
	if (omitted.size() < 2) return;
	B9CastHero* near = omitted[0]; B9CastHero* selected = omitted[1];
	const CVec2 focus(18, 12);
	near->TeleportTo(focus, false, false); selected->TeleportTo(CVec2(32, 20), false, false);
	check(f.map.world->FindLinuxUnitByObjectId(near->GetObjectId()) == near
		&& f.map.world->FindLinuxUnitByObjectId(selected->GetObjectId()) == selected,
		"dynamic presentation: late omitted focus and selected heroes remain authoritative live objects");
	const auto focused = Read(f, 640, focus);
	check(focused.size() == 640 && focused[0].objectId == f.observer->GetObjectId()
		&& focused[1].objectId == near->GetObjectId(),
		"dynamic presentation: observer stays first and previously omitted nearest hero enters before the cap");
	for (int limit : {std::numeric_limits<int>::min(), -1, 0, 1, 2, 3, 639, 640, 641, std::numeric_limits<int>::max()})
	{
		const auto markers = Read(f, limit, focus, selected->GetObjectId());
		const int expected = limit <= 0 ? 0 : std::min(limit, 640);
		check(markers.size() == expected, "dynamic presentation: requested size is bounded to zero through 640 before allocation");
		if (expected > 0) check(markers[0].objectId == f.observer->GetObjectId(), "dynamic presentation: local observer owns the first available slot");
		if (expected > 1) check(markers[1].objectId == selected->GetObjectId(), "dynamic presentation: eligible late selection owns the second slot despite its distance");
		if (expected > 2) check(markers[2].objectId == near->GetObjectId(), "dynamic presentation: focus ordering follows the observer and explicit selection");
		std::vector<int> unique = Ids(markers); std::sort(unique.begin(), unique.end());
		check(std::adjacent_find(unique.begin(), unique.end()) == unique.end(), "dynamic presentation: each registered identity is exported at most once");
	}
	const auto self = Read(f, 3, focus, f.observer->GetObjectId());
	check(self.size() == 3 && self[0].objectId == f.observer->GetObjectId() && self[1].objectId == near->GetObjectId(),
		"dynamic presentation: preferring the observer does not duplicate its reserved entry");
	selected->Hide(true);
	check(Ids(Read(f, 3, focus, selected->GetObjectId())) == Ids(Read(f, 3, focus)),
		"dynamic presentation: a hidden preferred unit neither enters nor consumes a reserved slot");
	selected->Hide(false);
	f.map.world->UnregisterObject(selected, false); f.map.world->ProcessAddRemove();
	check(Ids(Read(f, 3, focus, selected->GetObjectId())) == Ids(Read(f, 3, focus)),
		"dynamic presentation: stale preferred identity falls back to current focus ordering");
	f.map.world->GetLinuxDynamicWorldMarkers(legacy, 10000);
	check(legacy.size() == 650, "dynamic presentation: diagnostic export retains its requested limit above 640");
}

/** Distance and actual ID break ties independently of creation order or the legacy hash traversal. */
static void Ordering(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	auto farther = f.Hero(CVec2(16, 12)); auto nearer = f.Hero(CVec2(12, 12));
	f.map.world->ProcessAddRemove();
	auto markers = Read(f, 10, CVec2(12, 12));
	check(markers.size() == 3 && markers[1].objectId == nearer->GetObjectId() && markers[2].objectId == farther->GetObjectId(),
		"dynamic presentation: focus distance precedes the older object's ID");
	farther->TeleportTo(CVec2(11, 12), false, false); nearer->TeleportTo(CVec2(13, 12), false, false);
	const int low = std::min(farther->GetObjectId(), nearer->GetObjectId());
	const int high = std::max(farther->GetObjectId(), nearer->GetObjectId());
	const NRandom::RandomGenerator rng(*f.map.world->GetRndGen());
	const float time = f.map.world->GetTimeElapsed(), health = nearer->GetHealth(), mana = f.observer->GetMana();
	const int refs = nearer->GetRefCount(), owners = nearer->GetObjCount();
	for (int repeat = 0; repeat != 8; ++repeat)
	{
		markers = Read(f, 10, CVec2(12, 12));
		check(markers.size() == 3 && markers[1].objectId == low && markers[2].objectId == high,
			"dynamic presentation: equal focus distances are ordered by actual ID deterministically");
	}
	check(GlyphRngEqualB13(*f.map.world->GetRndGen(), rng) && f.map.world->GetTimeElapsed() == time
		&& nearer->GetHealth() == health && f.observer->GetMana() == mana && !f.observer->HasQueuedStates()
		&& nearer->GetRefCount() == refs && nearer->GetObjCount() == owners,
		"dynamic presentation: snapshots do not advance RNG, clock, resources, orders, or endpoint ownership");
	for (int preferred : {-2, std::numeric_limits<int>::max()})
		check(Ids(Read(f, 10, CVec2(12, 12), preferred)) == Ids(markers),
			"dynamic presentation: absent preferred IDs cannot change the deterministic fallback");
	const auto offMap = Read(f, 10, CVec2(-20, -20));
	check(offMap.size() == 3 && offMap[1].objectId == farther->GetObjectId(),
		"dynamic presentation: finite off-map camera focus remains valid and orders by its actual distance");
}

/** Render eligibility is current visibility and hiding, not the manual-picking flag or observer health. */
static void Visibility(Checks& check)
{
	Fixture f(check), foreign(check); if (!f.map.world || !foreign.map.world) return;
	auto enemy = f.Hero(CVec2(32, 20), NDb::FACTION_BURN); auto ally = f.Hero(CVec2(12, 12));
	f.map.world->ProcessAddRemove(); foreign.map.world->ProcessAddRemove();
	check(!enemy->IsVisibleForFaction(NDb::FACTION_FREEZE) && !Find(Read(f, 20, CVec2(32, 20)), enemy->GetObjectId()),
		"dynamic presentation: cached enemy visibility cannot bypass current war fog");
	enemy->TeleportTo(CVec2(12, 12), false, false);
	B8BehaviourReveal reveal(f.map.world, NDb::FACTION_FREEZE);
	check(enemy->IsVisibleForFaction(NDb::FACTION_FREEZE) && Find(Read(f, 20, CVec2(12, 12)), enemy->GetObjectId()),
		"dynamic presentation: revealed enemy enters the visible snapshot");
	enemy->EnemyVisible(false);
	check(!Find(Read(f, 20, CVec2(12, 12), enemy->GetObjectId()), enemy->GetObjectId()),
		"dynamic presentation: explicit preference never exposes a stealth-hidden enemy");
	enemy->EnemyVisible(true); enemy->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	check(Find(Read(f, 20, CVec2(12, 12)), enemy->GetObjectId()), "dynamic presentation: FORBIDPICK does not erase a visible unit");
	enemy->RemoveFlag(NDb::UNITFLAG_FORBIDPICK);
	for (B9CastHero* unit : {ally.GetPtr(), enemy.GetPtr()})
	{
		unit->Hide(true);
		check(!Find(Read(f, 20, CVec2(12, 12), unit->GetObjectId()), unit->GetObjectId()),
			"dynamic presentation: actual hiding excludes allies and enemies even when explicitly selected");
		vector<LinuxDynamicWorldMarker> legacy; f.map.world->GetLinuxDynamicWorldMarkers(legacy, 20);
		check(Find(legacy, unit->GetObjectId()), "dynamic presentation: legacy diagnostic policy still reports hidden registered units");
		unit->Hide(false); unit->EnemyVisible(true);
		check(Find(Read(f, 20, CVec2(12, 12)), unit->GetObjectId()), "dynamic presentation: unhidden visible unit returns immediately");
	}
	enemy->EnemyVisible(false); enemy->ChangeFaction(NDb::FACTION_FREEZE);
	check(Find(Read(f, 20, CVec2(12, 12)), enemy->GetObjectId()), "dynamic presentation: current allied faction bypasses only enemy fog/stealth, not hiding");
	enemy->ChangeFaction(NDb::FACTION_BURN); enemy->EnemyVisible(false);
	check(!Find(Read(f, 20, CVec2(12, 12)), enemy->GetObjectId()), "dynamic presentation: renewed hostile relation rechecks current visibility");
	f.observer->Hide(true);
	const auto hiddenObserver = Read(f, 20, CVec2(12, 12));
	check(!Find(hiddenObserver, f.observer->GetObjectId()) && Find(hiddenObserver, ally->GetObjectId()),
		"dynamic presentation: hidden observer is not rendered but still authorizes its faction's visible units");
	f.observer->Hide(false);
	f.observer->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	const auto deadObserver = Read(f, 20, CVec2(12, 12));
	check(f.observer->IsDead() && !deadObserver.empty() && deadObserver[0].objectId == f.observer->GetObjectId()
		&& deadObserver[0].dead && Find(deadObserver, ally->GetObjectId()),
		"dynamic presentation: registered dead observer retains faction vision and its first snapshot slot");
	for (const PFBaseHero* observer : {static_cast<const PFBaseHero*>(nullptr), static_cast<const PFBaseHero*>(foreign.observer.GetPtr())})
	{
		vector<LinuxDynamicWorldMarker> markers = deadObserver;
		f.map.world->GetLinuxVisibleDynamicWorldMarkers(markers, 20, observer, CVec2(12, 12));
		check(markers.empty(), "dynamic presentation: null or foreign observer clears any previous visible snapshot");
	}
	f.map.world->UnregisterObject(f.observer.GetPtr(), false); f.map.world->ProcessAddRemove();
	check(Read(f, 20, CVec2(12, 12)).empty(), "dynamic presentation: retained unregistered observer cannot authorize a snapshot");
}

/** Match original no-war-fog summon rendering: only allied pets bypass current terrain visibility. */
static void NoWarFogSummons(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	auto enemyMaster = f.Hero(CVec2(6, 6), NDb::FACTION_BURN);
	const CVec2 point(32, 20);
	const auto db = DeadCreatureDbB13(f.map.world);
	std::vector<CObj<PFBaseSummonedUnit>> summons;
	for (PFBaseUnit* master : {static_cast<PFBaseUnit*>(f.observer.GetPtr()), static_cast<PFBaseUnit*>(enemyMaster.GetPtr())})
		for (NDb::EUnitType type : {NDb::UNITTYPE_SUMMON, NDb::UNITTYPE_PET})
		{
			CObj<PFBaseSummonedUnit> unit = new PFBaseSummonedUnit(CPtr<PFWorld>(f.map.world), db, type,
				CPtr<PFBaseUnit>(master), Placement(CVec3(point.x, point.y, 0), QNULL, CVec3(1, 1, 1)), true, false, false);
			f.Keep(unit); summons.push_back(unit);
		}
	f.map.world->ProcessAddRemove();
	auto* fog = f.map.world->GetFogOfWar(); fog->StepVisibility(0);
	for (const auto& unit : summons)
	{
		const bool ally = unit->GetFaction() == NDb::FACTION_FREEZE;
		const bool pet = unit->GetUnitType() == NDb::UNITTYPE_PET;
		check(!unit->GetOpenWarFog() && !unit->IsPlacementVisible(NDb::FACTION_FREEZE),
			"dynamic presentation: actual no-war-fog summon constructor leaves distant terrain invisible");
		check(unit->IsVisibleForFaction(NDb::FACTION_FREEZE) == ally,
			"dynamic presentation: allied faction shortcut alone does not establish no-war-fog terrain visibility");
		check(bool(Find(Read(f, 20, point, unit->GetObjectId()), unit->GetObjectId())) == (ally && pet),
			ally ? (pet ? "dynamic presentation: allied no-war-fog PET remains visible outside revealed terrain"
				: "dynamic presentation: allied no-war-fog non-PET is excluded despite faction visibility and preference")
				: "dynamic presentation: enemy no-war-fog summon or PET requires current terrain visibility");
	}

	// Use the original fog observer lifecycle; no summon state or cached visibility is fabricated.
	const int reveal = fog->AddObject(f.map.world->GetTileMap()->GetTile(point), NDb::FACTION_FREEZE,
		f.map.world->GetTileMap()->GetLenghtInTiles(5.f));
	fog->StepVisibility(0);
	const auto revealed = Read(f, 20, point);
	for (const auto& unit : summons)
		check(unit->IsPlacementVisible(NDb::FACTION_FREEZE) && unit->IsVisibleForFaction(NDb::FACTION_FREEZE)
			&& Find(revealed, unit->GetObjectId()),
			"dynamic presentation: actual terrain reveal admits allied non-PET and visible enemy no-war-fog summons");
	fog->RemoveObject(reveal); fog->StepVisibility(0);
	const auto covered = Read(f, 20, point);
	for (const auto& unit : summons)
	{
		const bool alliedPet = unit->GetFaction() == NDb::FACTION_FREEZE && unit->GetUnitType() == NDb::UNITTYPE_PET;
		check(!unit->IsPlacementVisible(NDb::FACTION_FREEZE)
			&& bool(Find(covered, unit->GetObjectId())) == alliedPet,
			"dynamic presentation: previously revealed terrain does not retain no-war-fog summons except allied PETs");
	}
}

/** Minimap-only exclusions precede the display cap; visible no-war-fog pets still belong in 3D. */
static void MinimapBeforeCap(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	const CVec2 focus(3, 3);
	const auto db = DeadCreatureDbB13(f.map.world);
	std::vector<CObj<PFBaseSummonedUnit>> pets;
	for (int i = 0; i != 650; ++i)
	{
		CObj<PFBaseSummonedUnit> pet = new PFBaseSummonedUnit(CPtr<PFWorld>(f.map.world), db, NDb::UNITTYPE_PET,
			CPtr<PFBaseUnit>(f.observer.GetPtr()), Placement(CVec3(6, 6, 0), QNULL, CVec3(1, 1, 1)), true, false, false);
		f.Keep(pet); pets.push_back(pet);
	}
	auto* towerDb = new NDb::Tower;
	towerDb->stats = f.observer->DbUnitDesc()->stats; towerDb->faction = NDb::FACTION_FREEZE;
	NDb::AdvMapObject object; object.gameObject = towerDb; object.lockMap = false;
	object.offset = CPlacement(CVec3(32, 20, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFTower> tower = new PFTower(f.map.world, object); f.Keep(tower);
	f.map.world->ProcessAddRemove(); f.map.world->GetFogOfWar()->StepVisibility(0);
	bool registered = true;
	for (const auto& pet : pets)
		registered = registered && !pet->GetOpenWarFog() && pet->GetUnitType() == NDb::UNITTYPE_PET
			&& pet->GetFaction() == f.observer->GetFaction()
			&& f.map.world->FindLinuxUnitByObjectId(pet->GetObjectId()) == pet.GetPtr();
	check(registered && pets.size() == 650, "dynamic presentation: minimap overflow uses 650 registered allied no-war-fog PETs");
	check(tower->IsVisibleForFaction(f.observer->GetFaction())
		&& f.map.world->FindLinuxUnitByObjectId(tower->GetObjectId()) == tower.GetPtr(),
		"dynamic presentation: farther minimap tower is visible and remains registered");
	const int observerId = f.observer->GetObjectId(), towerId = tower->GetObjectId(), petId = pets.back()->GetObjectId();
	const auto scene = Read(f, 640, focus);
	check(scene.size() == 640 && scene[0].objectId == observerId && !Find(scene, towerId)
		&& std::count_if(scene.begin(), scene.end(), [](const LinuxDynamicWorldMarker& marker) {
			return marker.kind == LinuxDynamicWorldMarker::KIND_SUMMON;
		}) == 639, "dynamic presentation: 3D keeps nearby no-war-fog PETs even when they displace the farther tower");
	check(Ids(Read(f, 2, focus, petId)) == std::vector<int>({observerId, petId}),
		"dynamic presentation: preferred no-war-fog PET remains eligible for the 3D second slot");
	const std::vector<int> expected{observerId, towerId};
	for (int preferred : {-1, towerId, petId})
	{
		const auto minimap = Read(f, 640, focus, preferred, true);
		check(Ids(minimap) == expected,
			preferred == towerId ? "dynamic presentation: minimap retains the eligible preferred tower after the observer"
				: preferred == petId ? "dynamic presentation: ineligible preferred PET cannot starve the minimap tower"
				: "dynamic presentation: minimap filters no-war-fog summons before the 640 cap so the farther tower survives");
		check(std::none_of(minimap.begin(), minimap.end(), [](const LinuxDynamicWorldMarker& marker) {
			return marker.kind == LinuxDynamicWorldMarker::KIND_SUMMON;
		}), "dynamic presentation: minimap omits every no-war-fog summon, including allied PETs");
		check(Ids(Read(f, 2, focus, preferred, true)) == expected,
			"dynamic presentation: minimap-ineligible pets consume neither ordinary nor preferred slots under a two-marker cap");
	}
	check(Ids(Read(f, 640, focus)) == Ids(scene), "dynamic presentation: minimap queries do not change the subsequent 3D snapshot");
}

/** Existing geometry injection avoids pathfinding/fog conversions while testing nonfinite and extreme values. */
static void Geometry(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = f.observer->GetDbHero();
	spawn.placement = Placement(CVec3(12, 12, 0), QNULL, CVec3(1, 1, 1));
	CObj<B12ShopQueryHero> far = new B12ShopQueryHero(f.map.world, spawn); f.Keep(far);
	CObj<B12ShopQueryHero> near = new B12ShopQueryHero(f.map.world, spawn); f.Keep(near);
	f.map.world->ProcessAddRemove();
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	const float high = std::numeric_limits<float>::max();
	const CVec2 savedBounds = f.map.world->GetMapSize();
	CVec2& bounds = const_cast<CVec2&>(f.map.world->GetMapSize());
	for (const CVec2& bad : {CVec2(0, 30), CVec2(40, 0), CVec2(-1, 30), CVec2(40, -1),
		CVec2(nan, 30), CVec2(40, nan), CVec2(inf, 30), CVec2(40, inf)})
	{
		bounds = bad;
		check(Read(f, 20, CVec2(12, 12)).empty(), "dynamic presentation: nonpositive or nonfinite world bounds clear the output");
		bounds = savedBounds;
	}
	for (const CVec2& focus : {CVec2(nan, 12), CVec2(12, nan), CVec2(inf, 12), CVec2(12, -inf)})
		check(Read(f, 20, focus, near->GetObjectId()).empty(), "dynamic presentation: nonfinite focus clears the output even with a preferred unit");
	for (const CVec3& bad : {CVec3(nan, 12, 0), CVec3(12, nan, 0), CVec3(12, 12, nan),
		CVec3(inf, 12, 0), CVec3(12, -inf, 0), CVec3(12, 12, inf), CVec3(12, 12, high), CVec3(12, 12, -high),
		CVec3(-.125f, 12, 0), CVec3(12, -.125f, 0), CVec3(savedBounds.x, 12, 0), CVec3(12, savedBounds.y, 0),
		CVec3(high, 12, 0)})
	{
		near->Position(bad);
		check(!Find(Read(f, 20, CVec2(12, 12), near->GetObjectId()), near->GetObjectId()),
			"dynamic presentation: malformed or out-of-map XYZ cannot enter through explicit selection");
	}
	near->Position(CVec3(12, 12, 0));
	for (float size : {-1.f, nan, inf, -inf, high})
	{
		near->SetObjectSize(size);
		check(!Find(Read(f, 20, CVec2(12, 12)), near->GetObjectId()), "dynamic presentation: negative, nonfinite or unsafe squared object size is omitted");
	}
	for (float size : {0.f, 1.0e18f})
	{
		near->SetObjectSize(size); const auto markers = Read(f, 20, CVec2(12, 12));
		const auto* marker = Find(markers, near->GetObjectId());
		check(marker && marker->objectSize == size, "dynamic presentation: zero and large finite sizes within arithmetic headroom retain exact metadata");
	}
	near->SetObjectSize(1);
	for (const CVec3& valid : {CVec3(0, 0, 0),
		CVec3(std::nextafter(savedBounds.x, 0.f), std::nextafter(savedBounds.y, 0.f), 0),
		CVec3(12, 12, 1.0e18f), CVec3(12, 12, -1.0e18f)})
	{
		near->Position(valid);
		check(Find(Read(f, 20, CVec2(12, 12)), near->GetObjectId()),
			"dynamic presentation: half-open map edges and safe large finite Z remain eligible");
	}
	far->Position(CVec3(12, 12, 0)); near->Position(CVec3(20, 12, 0));
	const auto extreme = Read(f, 20, CVec2(1.0e12f, 12));
	check(extreme.size() == 3 && extreme[0].objectId == f.observer->GetObjectId()
		&& extreme[1].objectId == near->GetObjectId() && extreme[2].objectId == far->GetObjectId(),
		"dynamic presentation: distant finite focus uses double distance rather than float-rounded equal scores");
	far->Position(CVec3(12, 12, 0)); near->Position(CVec3(14, 12, 0));
	check(Read(f, 20, CVec2(-high, high)).size() == 3, "dynamic presentation: finite extreme off-map focus is not rejected as a world click");
}

/** Preserve all existing marker kinds and add actual summoned creatures, not arbitrary base creeps. */
static void Kinds(Checks& check)
{
	Fixture f(check); if (!f.map.world) return;
	auto common = CommonCreepB11(f.map.world); f.Keep(common);
	auto* raw = new NDb::AdvMapCreep; raw->stats = f.observer->DbUnitDesc()->stats;
	raw->targetingParams = f.observer->DbUnitDesc()->targetingParams;
	NDb::Ptr<NDb::AdvMapCreep> db = raw;
	CObj<PFNeutralCreep> neutral = new PFNeutralCreep(f.map.world, *db,
		Placement(CVec3(10, 10, 0), QNULL, CVec3(1, 1, 1)), false, 0, nullptr, vector<CVec2>());
	neutral->ChangeFaction(NDb::FACTION_FREEZE); f.Keep(neutral);
	auto* towerDb = new NDb::Tower; towerDb->stats = db->stats; towerDb->faction = NDb::FACTION_FREEZE;
	NDb::AdvMapObject object; object.gameObject = towerDb; object.lockMap = false;
	object.offset = CPlacement(CVec3(14, 14, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFTower> tower = new PFTower(f.map.world, object); f.Keep(tower);
	auto* mainDb = new NDb::MainBuilding; mainDb->stats = db->stats; mainDb->faction = NDb::FACTION_FREEZE;
	object.gameObject = mainDb; object.offset = CPlacement(CVec3(20, 14, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFMainBuilding> main = new PFMainBuilding(f.map.world, object); f.Keep(main);
	const auto summonDb = DeadCreatureDbB13(f.map.world);
	CObj<PFBaseSummonedUnit> summon = new PFBaseSummonedUnit(CPtr<PFWorld>(f.map.world), summonDb, NDb::UNITTYPE_SUMMON,
		CPtr<PFBaseUnit>(f.observer.GetPtr()), Placement(CVec3(12, 12, 0), QNULL, CVec3(1, 1, 1)), true);
	f.Keep(summon); f.map.world->ProcessAddRemove();
	const auto markers = Read(f, 20, CVec2(12, 12));
	struct Expected { PFBaseUnit* unit; int kind; };
	for (const Expected& expected : {Expected{f.observer.GetPtr(), LinuxDynamicWorldMarker::KIND_HERO},
		{common.GetPtr(), LinuxDynamicWorldMarker::KIND_COMMON_CREEP}, {neutral.GetPtr(), LinuxDynamicWorldMarker::KIND_NEUTRAL_CREEP},
		{tower.GetPtr(), LinuxDynamicWorldMarker::KIND_TOWER}, {main.GetPtr(), LinuxDynamicWorldMarker::KIND_MAIN_BUILDING},
		{summon.GetPtr(), LinuxDynamicWorldMarker::KIND_SUMMON}})
	{
		const auto* marker = Find(markers, expected.unit->GetObjectId());
		check(marker && marker->kind == expected.kind && marker->x == expected.unit->GetPosition().x
			&& marker->y == expected.unit->GetPosition().y && marker->healthPercent == expected.unit->GetHealthPercent()
			&& marker->energyPercent == expected.unit->GetManaPercent(),
			"dynamic presentation: each original known engine kind and real summon exports its own live payload");
	}
	check(markers.size() == 6 && LinuxDynamicWorldMarker::KIND_SUMMON == 6,
		"dynamic presentation: summon kind is six and fixture-only base units do not invent additional render kinds");
}

/** Snapshots copy original corpse cues and never retain or advance the owned creature's lifecycle. */
static void Corpses(Checks& check)
{
	for (int flags : {0, int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON)})
	{
		Fixture f(check); if (!f.map.world) continue;
		auto db = DeadCreatureDbB13(f.map.world);
		const_cast<NDb::UnitDeathParameters*>(db->deathParameters.GetPtr())->fallSpeed = 3;
		CObj<DeadCreatureB13<PFBaseSummonedUnit>> unit = new DeadCreatureB13<PFBaseSummonedUnit>(CPtr<PFWorld>(f.map.world), db,
			NDb::UNITTYPE_SUMMON, CPtr<PFBaseUnit>(f.observer.GetPtr()), Placement(CVec3(12, 12, 0), QNULL, CVec3(1, 1, 1)), true);
		f.Keep(unit); f.map.world->ProcessAddRemove();
		const int id = unit->GetObjectId();
		unit->KillUnit(nullptr, flags | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const auto cue = unit->GetLinuxDeathPresentation();
		check(unit->IsDead() && !unit->IsObjectDead() && cue.active == (flags == 0),
			"dynamic presentation: actual animated death or unsummon establishes its original distinct corpse state");
		const int refs = unit->GetRefCount(), owners = unit->GetObjCount(), ticks = unit->ticks;
		const float lifetime = unit->CorpseTime();
		const auto snapshot = Read(f, 20, CVec2(12, 12)); const auto* marker = Find(snapshot, id);
		check(marker && marker->kind == LinuxDynamicWorldMarker::KIND_SUMMON && marker->dead && SameDeath(marker->death, cue),
			"dynamic presentation: registered corpse copies every original cue field without inventing an unsummon death animation");
		for (int repeat = 0; repeat != 4; ++repeat) Read(f, 20, CVec2(12, 12), id);
		check(unit->GetRefCount() == refs && unit->GetObjCount() == owners && unit->ticks == ticks
			&& unit->CorpseTime() == lifetime && SameDeath(unit->GetLinuxDeathPresentation(), cue),
			"dynamic presentation: retained marker values do not own the summon or consume corpse timer ticks");
		if (flags == 0)
		{
			f.map.world->Step(1.125f, 1.125f);
			const auto falling = Read(f, 20, CVec2(12, 12)); const auto* current = Find(falling, id);
			check(current && current->death.fallStartedAt == f.map.world->GetTimeElapsed()
				&& SameDeath(current->death, unit->GetLinuxDeathPresentation()) && marker && SameDeath(marker->death, cue),
				"dynamic presentation: a new snapshot observes real falling while the earlier copied value remains unchanged");
			f.map.world->Step(1, 1);
		}
		else f.map.world->Step(.625f, .625f);
		check(unit->removals == 1 && unit->IsObjectDead() && !f.map.world->FindLinuxUnitByObjectId(id),
			"dynamic presentation: actual normal/unsummon lifetime retires the creature exactly once despite retained marker values");
		check(!Find(Read(f, 20, CVec2(12, 12), id), id), "dynamic presentation: retired preferred corpse cannot survive in a fresh visible snapshot");
	}
}

/** The shipped leaf proves that a summon keeps its exact authored DB/scene identity instead of its owner's. */
static void Authored(Checks& check, const char* dataRoot)
{
	check(dataRoot && *dataRoot, "dynamic presentation: authored summon proof requires original data root");
	if (!dataRoot || !*dataRoot) return;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		Fixture f(check);
		const auto db = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/Demonolog/SummonDemons.APLR.xdb"));
		const auto talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Demonolog/Ability0.TALENT.xdb"));
		const auto rules = NDb::Get<NDb::AILogicParameters>(NDb::DBID("/GameLogic/AILogic.AILP.xdb"));
		check(f.map.world && db && talent && rules, "dynamic presentation: original Demonologist summon leaf and constants load");
		if (f.map.world && db && talent && rules)
		{
			auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
			const auto saved = ai->globalConstants; ai->globalConstants = rules->globalConstants;
			{
				NaftaFixtureB13 cast(f.map.world); cast.abilityDb = talent.GetPtr();
				cast.ability = new PFAbilityData(cast.sender.GetPtr(), cast.abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
				cast.instance = new PFAbilityInstance(cast.ability, Target(cast.receiver.GetPtr()), true);
				cast.sender->GetVariableVWM("SaveDemons")->SetBaseValue(1);
				auto app = cast.Create(db); check(app && cast.Activate(app), "dynamic presentation: original leaf creates its real owned summon");
				const auto units = SummonCountUnitsB13(f.map.world, cast.sender);
				check(units.size() == 1, "dynamic presentation: original leaf produces one exact Demon2 instance");
				if (units.size() == 1)
				{
					const auto& unit = units[0]; unit->TeleportTo(CVec2(12, 12), false, false);
					const auto* desc = unit->DbUnitDesc();
					const auto markers = Read(f, 20, CVec2(12, 12), unit->GetObjectId());
					const auto* marker = Find(markers, unit->GetObjectId());
					check(desc && desc == db->summonedUnits[0].GetPtr() && desc->sceneObject && marker
						&& marker->kind == LinuxDynamicWorldMarker::KIND_SUMMON && marker->objectId != cast.sender->GetObjectId()
						&& marker->unitDbid == desc->GetDBID().GetFormatted()
						&& marker->sceneObjectDbid == desc->sceneObject->GetDBID().GetFormatted()
						&& marker->unitDbid != cast.sender->DbUnitDesc()->GetDBID().GetFormatted(),
						"dynamic presentation: actual Demon2 exports its own exact identity and original scene DBID, not its owner's hero asset");
					vector<LinuxDynamicWorldMarker> legacy; f.map.world->GetLinuxDynamicWorldMarkers(legacy, 10000);
					const auto* diagnostic = Find(legacy, unit->GetObjectId());
					check(diagnostic && marker && diagnostic->kind == LinuxDynamicWorldMarker::KIND_SUMMON
						&& diagnostic->unitDbid == marker->unitDbid && diagnostic->sceneObjectDbid == marker->sceneObjectDbid,
						"dynamic presentation: diagnostic export adds the same real summon kind and original asset identity");
				}
			}
			ai->globalConstants = saved;
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
}
}

/** Dedicated C111 real-engine regression entry point, invoked by the parent probe harness. */
static void DynamicPresentationB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	dynamic_presentation_b13::Overflow(check);
	dynamic_presentation_b13::Ordering(check);
	dynamic_presentation_b13::Visibility(check);
	dynamic_presentation_b13::NoWarFogSummons(check);
	dynamic_presentation_b13::MinimapBeforeCap(check);
	dynamic_presentation_b13::Geometry(check);
	dynamic_presentation_b13::Kinds(check);
	dynamic_presentation_b13::Corpses(check);
	dynamic_presentation_b13::Authored(check, dataRoot);
	std::printf("B13 dynamic presentation checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
