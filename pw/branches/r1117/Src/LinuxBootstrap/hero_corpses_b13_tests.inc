#include "../System/BinChunkSerializer.h"
#include "../System/BinChunkSerializerBase.h"
#include "../System/ChunklessSaver.h"

/** Observe actual hero timers and object retirement; do not replace any lifecycle method. */
struct HeroCorpseUnitB13 : DeadCreatureB13<B8BehaviourHero>
{
	using DeadCreatureB13<B8BehaviourHero>::DeadCreatureB13;
	int dies = 0;
	int* destroyed = nullptr;
	~HeroCorpseUnitB13() override { if (destroyed) ++*destroyed; }
	void OnDie() override { ++dies; PFBaseHero::OnDie(); }
	float AdvanceTime() const { return advanceRespawnDelay; }
	bool InAIWorld() const { return ringField.isLinked(); }
	void RetireAgain() { PFBaseHero::RemoveCorpse(); }
};

/** Loaded descriptors and SpawnInfo exercise clone identity without the unsupported Clone factory. */
static CObj<HeroCorpseUnitB13> HeroCorpseCreateB13(PFWorld* world, bool clone = false,
	float lifetime = 2, int unrelatedHides = 0)
{
	auto source = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	auto* death = new ApplicatorQueryDbB12<NDb::UnitDeathParameters>("/Mock/HeroCorpseDeath.xdb");
	death->deathTime = lifetime; death->unsummonTime = .5f;
	death->observeOffset = .75f; death->fallOffset = 1;
	auto* raw = new ApplicatorQueryDbB12<NDb::Hero>("/Mock/HeroCorpse.xdb");
	raw->stats = source->stats; raw->targetingParams = source->targetingParams;
	raw->deathParameters = death;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = raw; spawn.isClone = clone;
	spawn.placement = Placement(CVec3(5,5,0), QNULL, CVec3(1,1,1));
	CObj<HeroCorpseUnitB13> hero = new HeroCorpseUnitB13(world, spawn, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDATTACK);
	for (int i = 0; i != unrelatedHides; ++i) hero->UpdateHiddenState(false);
	world->Step(0, 0);
	return hero;
}

/** Reach the existing private drain without changing engine headers or simulating its cleanup. */
struct HeroCorpseDrainTagB13
{
	using Pointer = void (PFWorld::*)(bool);
	friend Pointer HeroCorpseDrainMemberB13(HeroCorpseDrainTagB13);
};
template<HeroCorpseDrainTagB13::Pointer member>
struct HeroCorpseDrainAccessB13
{
	friend HeroCorpseDrainTagB13::Pointer HeroCorpseDrainMemberB13(HeroCorpseDrainTagB13) { return member; }
};
template struct HeroCorpseDrainAccessB13<&PFWorld::KillDeadUnits>;

/** Exercise the actual native serialization callback using the engine's tagged saver. */
struct HeroCorpseSerializeTagB13
{
	using Pointer = void (PFBaseHero::*)(IBinSaver&);
	friend Pointer HeroCorpseSerializeMemberB13(HeroCorpseSerializeTagB13);
};
template<HeroCorpseSerializeTagB13::Pointer member>
struct HeroCorpseSerializeAccessB13
{
	friend HeroCorpseSerializeTagB13::Pointer HeroCorpseSerializeMemberB13(HeroCorpseSerializeTagB13) { return member; }
};
template struct HeroCorpseSerializeAccessB13<&PFBaseHero::OnSerialize>;

/** A focused serialization envelope, not a claim of supported complete world saves. */
struct HeroCorpseImageB13
{
	PFBaseHero* hero;
	bool legacy;
	int marker = 12345;
	int ownershipTags = -1;
	int operator&(IBinSaver& saver)
	{
		saver.Add(43, &marker);
		if (saver.IsReading() && dynamic_cast<BinChunkSaverBase*>(&saver)) ownershipTags = saver.CountChunks(44);
		if (!legacy) (hero->*HeroCorpseSerializeMemberB13(HeroCorpseSerializeTagB13()))(saver);
		return 0;
	}
};

/** Drain the existing death queue explicitly: its timing is parent-owned, outside these tests. */
static void HeroCorpseKillB13(PFWorld* world, PFBaseHero* hero, int flags = 0)
{
	hero->KillUnit(nullptr, flags | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	(world->*HeroCorpseDrainMemberB13(HeroCorpseDrainTagB13()))(false);
	world->ProcessAddRemove();
}

/** Ordinary heroes retain their world identity and add one corpse hide at retirement. */
static void HeroCorpseOrdinaryB13(Checks& check)
{
	for (int hides : {0, 1}) for (int flags : {0, int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON),
		int(PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE), int(PFBaseUnit::UNITDIEFLAGS_DONTPLAYDEATHANIMATION)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto* world = isolated.world; auto hero = HeroCorpseCreateB13(world, false, 2, hides);
		hero->SetForbidRespawn(true);
		const int id = hero->GetObjectId();
		HeroCorpseKillB13(world, hero, flags);
		const bool forced = (flags & PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE) == PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE;
		check(hero->IsDead() && !hero->IsObjectDead() && !hero->InAIWorld(), "ordinary hero death leaves the world identity but removes AI membership");
		check(hero->GetTimeToRespawn() < 0 && hero->AdvanceTime() < 0, "forbid-respawn hero has no automatic resurrection timer");
		if (!forced)
		{
			const float lifetime = flags & PFBaseUnit::UNITDIEFLAGS_UNSUMMON ? .5f : 2;
			check(hero->CorpseTime() == lifetime && hero->removals == 0 && hero->GetHiddenCounter() == hides,
				"ordinary hero uses authored corpse deadline without hiding at death");
			world->Step(lifetime, lifetime);
			check(hero->CorpseTime() == 0 && hero->removals == 0 && hero->GetHiddenCounter() == hides,
				"hero corpse remains at the exact zero deadline");
			world->Step(.125f, .125f);
		}
		check(hero->removals == 1 && hero->CorpseTime() == -1 && hero->GetHiddenCounter() == hides + 1,
			"hero corpse retirement disables its timer and adds exactly one independent hide contribution");
		check(hero->dies == 0 && !hero->IsObjectDead() && world->IsSubscribed(hero)
			&& world->FindLinuxUnitByObjectId(id) == hero.GetPtr(), "ordinary corpse removal never calls object Die or unregisters the hero");
		world->Step(3, 3);
		check(hero->removals == 1 && hero->GetHiddenCounter() == hides + 1 && hero->IsDead(),
			"later dead ticks do not retire or hide an ordinary corpse again");
		hero->RetireAgain(); hero->RetireAgain();
		check(hero->GetHiddenCounter() == hides + 1 && hero->CorpseTime() == -1 && hero->dies == 0,
			"repeated explicit corpse removal cannot acquire multiple ordinary hide contributions");
	}
}

/** Clones keep creature death policy, never ordinary hero auto-respawn, and retire like creeps. */
static void HeroCorpseClonesB13(Checks& check)
{
	for (int flags : {0, int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON), int(PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto* world = isolated.world; auto hero = HeroCorpseCreateB13(world, true, 2, 1);
		const int id = hero->GetObjectId();
		check(hero->IsClone() && !hero->IsTrueHero() && !hero->HasBehaviour(), "real SpawnInfo creates clone identity without the unsupported factory");
		HeroCorpseKillB13(world, hero, flags);
		check(hero->GetTimeToRespawn() < 0 && hero->AdvanceTime() < 0, "clone death does not start ordinary hero respawn timers");
		if (!(flags & PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE))
		{
			const float lifetime = flags & PFBaseUnit::UNITDIEFLAGS_UNSUMMON ? .5f : 2;
			world->Step(lifetime, lifetime);
			check(hero->CorpseTime() == 0 && hero->dies == 0 && !hero->IsObjectDead(), "clone corpse preserves the authored strict deadline");
			world->Step(.125f, .125f);
		}
		check(hero->removals == 1 && hero->dies == 1 && hero->CorpseTime() == -1 && hero->IsObjectDead(),
			"clone corpse removal runs the base timer reset then object Die exactly once");
		check(!world->IsSubscribed(hero) && world->FindLinuxUnitByObjectId(id) != hero.GetPtr(),
			"retired clone is removed from exact registration and scheduling");
		check(hero->GetHiddenCounter() == 1, "clone corpse removal does not add the ordinary hero hide contribution");
		const int ticks = hero->ticks;
		world->Step(10, 10);
		check(hero->ticks == ticks && hero->dies == 1 && hero->IsDead(), "retired clone neither ticks again nor automatically resurrects");
		hero->RetireAgain();
		check(hero->dies == 1 && hero->GetHiddenCounter() == 1, "repeated retired-clone removal does not repeat object death");
	}
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	auto hero = HeroCorpseCreateB13(isolated.world, true, .25f);
	int destroyed = 0; hero->destroyed = &destroyed;
	CPtr<HeroCorpseUnitB13> weak = hero.GetPtr();
	HeroCorpseKillB13(isolated.world, hero);
	hero = nullptr;
	isolated.world->Step(.5f, .5f);
	check(!IsValid(weak) && destroyed == 1, "world-owned clone releases its final strong owner after actual corpse retirement");
	// The baseline stub leaves the clone registered; do not let its later destruction borrow a dead stack counter.
	if (IsValid(weak)) weak->destroyed = nullptr;
}

/** Scheduled advance must balance corpse hiding whether expiry happened before, after or at zero. */
static void HeroCorpseScheduledB13(Checks& check)
{
	for (int hides : {0, 1}) for (int timing : {0, 1, 2})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto* world = isolated.world;
		auto hero = HeroCorpseCreateB13(world, false, timing == 1 ? 6 : 2, hides);
		HeroCorpseKillB13(world, hero);
		const float delay = hero->GetTimeToRespawn(), advance = hero->AdvanceTime();
		check(delay == (timing == 1 ? 6 : 5) && advance == delay - 1, "authored automatic respawn and advance clocks remain unchanged");
		if (timing == 0)
		{
			world->Step(2.125f, 2.125f);
			check(hero->removals == 1 && hero->GetHiddenCounter() == hides + 1, "scheduled revival fixture first retires the corpse");
			world->Step(advance - 2.125f, advance - 2.125f);
		}
		else if (timing == 2)
		{
			world->Step(2, 2);
			check(hero->CorpseTime() == 0 && hero->removals == 0, "coarse advance starts with an exact-zero unretired corpse");
			world->Step(advance - 2, advance - 2);
		}
		else world->Step(advance, advance);
		check(hero->IsResurrecting() && hero->IsDead() && hero->GetTimeToRespawn() == 1,
			"advance does not restore health or shorten the final respawn second");
		check(hero->removals == 1 && hero->CorpseTime() == -1 && hero->GetHiddenCounter() == hides,
			"scheduled advance retires at most once and balances only corpse hiding including the zero boundary");
		world->Step(1, 1);
		check(!hero->IsDead() && !hero->IsObjectDead() && hero->InAIWorld() && hero->HasBehaviour(),
			"ordinary automatic resurrection retains real world identity and restores AI behavior");
		check(hero->GetHiddenCounter() == hides && hero->removals == 1 && hero->AdvanceTime() < 0,
			"completed automatic resurrection leaves no corpse hide or active advance timer");
	}
}

/** Native explicit resurrection stays immediate but must balance the new corpse hide. */
static void HeroCorpseImmediateB13(Checks& check)
{
	for (int hides : {0, 1}) for (int stage : {0, 1, 2, 3})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto* world = isolated.world; auto hero = HeroCorpseCreateB13(world, false, 2, hides);
		HeroCorpseKillB13(world, hero);
		if (stage == 1) world->Step(2, 2);
		if (stage == 2) world->Step(2.125f, 2.125f);
		if (stage == 3) world->Step(4, 4);
		check(hero->IsDead(), "explicit resurrection fixture is still dead before its direct native call");
		hero->TeleportTo(CVec2(9,7), false, false);
		const int ticks = hero->ticks;
		hero->Resurrect(PFBaseHero::ResurrectParams(PFBaseHero::ResurrectParams::Death, .5f, .25f));
		check(!hero->IsDead() && hero->ticks == ticks && hero->GetHealth() == hero->GetMaxHealth() * .5f
			&& hero->GetEnergy() == hero->GetMaxEnergy() * .25f, "explicit native resurrection remains immediate with exact authored resource fractions");
		check(hero->GetPosition().x == 9 && hero->GetPosition().y == 7 && hero->InAIWorld(),
			"immediate Death-position resurrection preserves requested placement and re-registers AI ownership");
		check(hero->GetHiddenCounter() == hides && hero->removals == 1 && hero->CorpseTime() == -1,
			"immediate revival balances corpse retirement without consuming unrelated hide layers");
		check(hero->GetTimeToRespawn() < 0 && hero->AdvanceTime() < 0 && hero->ObserveTime() == -1
			&& hero->FallTime() == -1, "explicit resurrection clears existing creature and hero timers");
		world->Step(.125f, .125f);
		check(hero->ticks == ticks + 1 && hero->removals == 1 && hero->GetHiddenCounter() == hides,
			"explicitly revived hero transfers once to C6 movement without another corpse retirement");
	}
}

/** Exercise the real resurrection applicator and a second complete death/retirement cycle. */
static void HeroCorpseEffectsB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	auto* world = isolated.world; ApplicatorHistoryFixtureB13 f(check, world);
	const_cast<NDb::Ability*>(f.db.GetPtr())->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
	auto hero = HeroCorpseCreateB13(world, false, .5f, 1); hero->SetForbidRespawn(true);
	auto* raw = new ApplicatorQueryDbB12<NDb::ResurrectApplicator>("/Mock/HeroCorpseResurrect.xdb");
	raw->enabled.sString = "true"; NDb::Ptr<NDb::ResurrectApplicator> db = raw;
	for (int cycle = 0; cycle != 2; ++cycle)
	{
		HeroCorpseKillB13(world, hero);
		world->Step(.625f, .625f);
		check(hero->GetHiddenCounter() == 2 && hero->removals == cycle + 1, "repeated real death adds one corpse hide per lifetime");
		auto app = f.Apply(db, 0, nullptr, hero);
		check(dynamic_cast<PFResurrectApplicator*>(app.GetPtr()) && !app->IsAlive() && !hero->IsDead(),
			"loaded resurrection factory restores the actual dead hero immediately");
		check(hero->GetHiddenCounter() == 1 && hero->removals == cycle + 1 && hero->HasBehaviour(),
			"actual resurrection effect balances corpse hiding across repeated lifetimes");
	}
	hero->CleanupHistoryApplicators(); hero->Cleanup();
}

/** Deferred clone death stays on the creature path without starting ordinary hero respawn. */
static void HeroCorpseDeferredB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	auto* world = isolated.world; auto hero = HeroCorpseCreateB13(world, true);
	hero->SetHealth(1);
	hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	world->Step(.25f, .25f);
	check(!hero->IsDead() && !hero->IsObjectDead() && hero->GetTimeToRespawn() < 0 && hero->removals == 0,
		"deferred clone death keeps live health and does not run corpse or hero respawn timers");
	HeroCorpseKillB13(world, hero, PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE);
	check(hero->IsObjectDead() && hero->dies == 1 && hero->GetTimeToRespawn() < 0,
		"final forced clone death retires the same identity rather than entering hero respawn");
}

/** A disabled corpse timer has not necessarily acquired a hide contribution. */
static void HeroCorpseDisabledB13(Checks& check)
{
	for (float lifetime : {-2.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		for (bool force : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto hero = HeroCorpseCreateB13(isolated.world, false, lifetime, 1);
		hero->SetForbidRespawn(true);
		HeroCorpseKillB13(isolated.world, hero, force ? PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE : 0);
		hero->Resurrect();
		check(!hero->IsDead() && hero->GetHiddenCounter() == 1,
			"disabled authored corpse timer and forced retirement each preserve the unrelated hide on immediate revival");
	}
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	auto hero = HeroCorpseCreateB13(isolated.world, false, 2, 1);
	const_cast<NDb::BaseHero*>(hero->GetDbHero())->deathParameters = nullptr;
	hero->SetForbidRespawn(true);
	HeroCorpseKillB13(isolated.world, hero);
	hero->Resurrect();
	check(!hero->IsDead() && hero->GetHiddenCounter() == 1,
		"missing death descriptor does not let immediate revival release an unrelated hide contribution");
}

/** Restore true/false ownership without guessing from the deliberately identical hide counters. */
static void HeroCorpseSerializationB13(Checks& check)
{
	for (bool owns : {false, true}) for (bool legacy : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto hero = HeroCorpseCreateB13(isolated.world, false, -1, owns ? 1 : 2);
		hero->SetForbidRespawn(true);
		HeroCorpseKillB13(isolated.world, hero, owns ? PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE : 0);
		HeroCorpseImageB13 outgoing{hero.GetPtr(), legacy};
		CObj<MemoryStream> stream = new MemoryStream;
		{
			CObj<IBinSaver> writer = CreateWriteBinSaver(stream, 0, false);
			writer->Add(2, &outgoing);
		}
		// A new actual hero represents an already restored inherited counter, with no ownership inferred from it.
		auto restored = HeroCorpseCreateB13(isolated.world, false, -1, 2);
		restored->SetForbidRespawn(true); HeroCorpseKillB13(isolated.world, restored);
		HeroCorpseImageB13 incoming{restored.GetPtr(), false}; incoming.marker = 0;
		stream->Seek(0, SEEKORIGIN_BEGIN);
		{
			CObj<IBinSaver> reader = CreateReadBinSaver(stream, nullptr);
			reader->Add(2, &incoming);
		}
		check(incoming.marker == 12345, "real tagged hero envelope preserves the existing tag before the optional ownership tag");
		check(incoming.ownershipTags == (legacy ? 0 : 1), "only the new native tagged hero image contains ownership tag 44");
		restored->Resurrect();
		check(!restored->IsDead() && restored->GetHiddenCounter() == (owns && !legacy ? 1 : 2),
			"tagged ownership restore releases only a saved corpse hide; legacy absence never infers ownership");
	}
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	auto hero = HeroCorpseCreateB13(isolated.world, false, -1, 1);
	hero->SetForbidRespawn(true); HeroCorpseKillB13(isolated.world, hero, PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE);
	CObj<MemoryStream> oldImage = new MemoryStream, newImage = new MemoryStream;
	HeroCorpseImageB13 legacy{hero.GetPtr(), true}, current{hero.GetPtr(), false};
	{
		CObj<IBinSaver> oldWriter = CreateChunklessSaver(oldImage, nullptr, false);
		CObj<IBinSaver> newWriter = CreateChunklessSaver(newImage, nullptr, false);
		oldWriter->Add(2, &legacy); newWriter->Add(2, &current);
	}
	check(oldImage->GetSize() == newImage->GetSize()
		&& !memcmp(oldImage->GetBuffer(), newImage->GetBuffer(), oldImage->GetSize()),
		"native ownership does not append bytes to the shared chunkless protocol");
	newImage->Seek(0, SEEKORIGIN_BEGIN);
	{
		CObj<IBinSaver> reader = CreateChunklessSaver(newImage, nullptr, true);
		reader->Add(2, &current);
	}
	hero->Resurrect();
	check(hero->GetHiddenCounter() == 2, "unsupported chunkless restoration clears ownership rather than stealing a hide by inference");

	// Default construction is also used by object factories during loading and destruction reconstruction.
	struct DefaultHero : PFBaseHero { DefaultHero() : PFBaseHero() {} };
	CObj<DefaultHero> defaultHero = new DefaultHero;
	HeroCorpseImageB13 defaultImage{defaultHero.GetPtr(), false};
	CObj<MemoryStream> defaultStream = new MemoryStream;
	{
		CObj<IBinSaver> writer = CreateWriteBinSaver(defaultStream, 0, false);
		writer->Add(2, &defaultImage);
	}
	HeroCorpseKillB13(isolated.world, hero, PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE);
	const int beforeRestore = hero->GetHiddenCounter();
	defaultStream->Seek(0, SEEKORIGIN_BEGIN);
	{
		CObj<IBinSaver> reader = CreateReadBinSaver(defaultStream, nullptr);
		reader->Add(2, &current);
	}
	check(current.ownershipTags == 1, "default-constructed native hero serializes its initialized ownership bit");
	hero->Resurrect();
	check(hero->GetHiddenCounter() == beforeRestore, "loading saved false ownership clears previous true ownership without inferring from the counter");
}

static void HeroCorpsesB13(Checks& check)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	auto* params = new NDb::HeroRespawnParams;
	params->heroBaseRespawnTime = params->heroMinRespawnTime = 5;
	params->heroRespawnDelayMultiplier = 0; params->overtimeRespawnParams.overtime = 100000;
	auto* ai = new NDb::AILogicParameters; ai->heroRespawnParams = params; ai->heroAdvanceRespawnTime = 1;
	auto* logic = new NDb::SessionLogicRoot; logic->aiLogic = ai; logic->unitLogicParameters = new NDb::UnitsLogicParameters;
	auto* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	HeroCorpseOrdinaryB13(check);
	HeroCorpseClonesB13(check);
	HeroCorpseScheduledB13(check);
	HeroCorpseImmediateB13(check);
	HeroCorpseEffectsB13(check);
	HeroCorpseDeferredB13(check);
	HeroCorpseDisabledB13(check);
	HeroCorpseSerializationB13(check);
	NDb::SessionRoot::InitRoot(saved);
}
