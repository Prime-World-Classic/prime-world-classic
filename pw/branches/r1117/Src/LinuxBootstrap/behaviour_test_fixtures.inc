#ifndef PW_B8_BEHAVIOUR_TEST_FIXTURES_INCLUDED
#define PW_B8_BEHAVIOUR_TEST_FIXTURES_INCLUDED

#include "../PF_GameLogic/TileMap.h"
#include "../PF_GameLogic/WarFog.h"

#if !defined(PW_LINUX_NULL_RENDER)
#error B8 behavior tests require PW_LINUX_NULL_RENDER.
#endif

/** Reuse the probe's literal stats; all initialization remains engine-owned. */
static NDb::Ptr<NDb::Creature> B8BehaviourCreatureDb(PFWorld* world, NDb::EFaction faction)
{
	CObj<PFBaseUnit> seed = Unit(world, faction, 20);
	NDb::Creature* raw = new NDb::Creature;
	raw->stats = seed->DbUnitDesc()->stats;
	NDb::UnitTargetingParameters* targeting = new NDb::UnitTargetingParameters;
	targeting->chaseRange = 20;
	targeting->targetingRange = 20;
	targeting->onlyAutoTargeting = false;
	raw->targetingParams = targeting;
	return raw;
}

/** Expose caches and seed metadata without replacing behavior, visibility, or the FSM. */
struct B8BehaviourCreep : PFBaseCreep
{
	B8BehaviourCreep(PFWorld* world, const NDb::Creature& db, NDb::EFaction faction)
		: PFBaseCreep(world, db, faction, NDb::UNITTYPE_CREEP, -1,
			Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1))) {}
	int ScreamCount() const { return screamTargets.size(); }
	void EnemyVisible(bool visible) { visibleForEnemy = visible; }
	void SeedHeroAttack(PFBaseUnit* victim) { lastHeroAttack.pVictim = victim; }
};

static CObj<B8BehaviourCreep> B8BehaviourMoving(PFWorld* world, NDb::EFaction faction)
{
	NDb::Ptr<NDb::Creature> db = B8BehaviourCreatureDb(world, faction);
	return new B8BehaviourCreep(world, *db, faction);
}

/** Supply endpoint metadata only; mounting itself uses the engine AttachUnit path. */
struct B8BehaviourRider : B8BehaviourCreep
{
	CPtr<PFBaseUnit> mountedOn;
	B8BehaviourRider(PFWorld* world, const NDb::Creature& db, NDb::EFaction faction)
		: B8BehaviourCreep(world, db, faction) {}
	CPtr<PFBaseUnit> GetMountedOn() const override { return mountedOn; }
};

/** Expose queue stepping, excluding unrelated unit movement and automatic targeting. */
struct B8BehaviourHero : PFBaseHero
{
	B8BehaviourHero(PFWorld* world, const SpawnInfo& spawn, NDb::EFaction faction)
		: PFBaseHero(world, spawn, NDb::UNITTYPE_HEROMALE, faction, faction) {}
	void AdvanceOrders(float dt) { FsmStep(dt); }
	void EnemyVisible(bool visible) { visibleForEnemy = visible; }
};

static CObj<B8BehaviourHero> B8BehaviourHeroUnit(PFWorld* world, NDb::EFaction faction, bool clone = false)
{
	NDb::Ptr<NDb::Creature> source = B8BehaviourCreatureDb(world, faction);
	NDb::Hero* raw = new NDb::Hero;
	raw->stats = source->stats;
	raw->targetingParams = source->targetingParams;
	NDb::Ptr<NDb::Hero> db = raw;
	PFBaseHero::SpawnInfo spawn;
	spawn.pHero = db;
	spawn.isClone = clone;
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	return new B8BehaviourHero(world, spawn, faction);
}

/** Observe the real scream hook and still execute its production implementation. */
struct B8BaseScreamObserver : PFBaseBehaviour
{
	int screams = 0;
	CPtr<PFBaseUnit> screamedTarget;
	ScreamTarget::ScreamType screamedType = ScreamTarget::ScreamAlert;
	explicit B8BaseScreamObserver(PFBaseMovingUnit* owner) : PFBaseBehaviour(owner) {}
	void DoScream(const CPtr<PFBaseUnit>& target, ScreamTarget::ScreamType type) override
	{
		++screams;
		screamedTarget = target;
		screamedType = type;
		PFBaseBehaviour::DoScream(target, type);
	}
};

struct B8HeroScreamObserver : PFHeroBehaviour
{
	int screams = 0;
	CPtr<PFBaseUnit> screamedTarget;
	ScreamTarget::ScreamType screamedType = ScreamTarget::ScreamAlert;
	explicit B8HeroScreamObserver(PFBaseMovingUnit* owner) : PFHeroBehaviour(owner) {}
	void DoScream(const CPtr<PFBaseUnit>& target, ScreamTarget::ScreamType type) override
	{
		++screams;
		screamedTarget = target;
		screamedType = type;
		PFBaseBehaviour::DoScream(target, type);
	}
};

/** A scoped real fog observer makes direct damage fixtures deterministically visible. */
struct B8BehaviourReveal
{
	PFWorld* world;
	int id;
	B8BehaviourReveal(PFWorld* world, NDb::EFaction faction)
		: world(world), id(world->GetFogOfWar()->AddObject(
			world->GetTileMap()->GetTile(5.f, 5.f), faction,
			world->GetTileMap()->GetLenghtInTiles(20.f)))
	{
		world->GetFogOfWar()->StepVisibility(0);
	}
	~B8BehaviourReveal()
	{
		world->GetFogOfWar()->RemoveObject(id);
		world->GetFogOfWar()->StepVisibility(0);
	}
};

/** A second loaded engine world distinguishes foreign targets from absent context. */
struct B8BehaviourForeignWorld
{
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = nullptr;
	explicit B8BehaviourForeignWorld(Checks& check)
	{
		NDb::Terrain* terrain = new NDb::Terrain;
		terrain->elemXCount = 4;
		terrain->elemYCount = 3;
		terrain->tilesPerElement = 4;
		NDb::AdvMap* map = new NDb::AdvMap;
		map->terrain = terrain;
		NDb::AdvMapDescription* description = new NDb::AdvMapDescription;
		description->map = map;
		NDb::Ptr<NDb::AdvMapDescription> db = description;
		owner = PFWorld::CreatePFWorld();
		world = dynamic_cast<PFWorld*>(owner.GetPtr());
		const bool loaded = world && world->LoadMap(db, 0, NCore::TPlayersStartInfo(), 0,
			false, PFResourcesCollection::TalentMap());
		check(loaded, "B8 foreign mock world loads");
		if (!loaded)
			world = nullptr;
	}
};

/** Return the production attack state without constructing a second attack model. */
static const PFBaseUnitAttackState* B8BehaviourAttack(const PFBaseMovingUnit& owner)
{
	return dynamic_cast<const PFBaseUnitAttackState*>(owner.GetCurrentState());
}

#endif
