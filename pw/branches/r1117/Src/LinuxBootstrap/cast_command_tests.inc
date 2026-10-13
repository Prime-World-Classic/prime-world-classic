#include "../Core/WorldCommand.h"
#include "../Core/GameCommand.h"
#include "../PF_GameLogic/HeroActions.h"
#include "../PF_GameLogic/PointersHolder.h"

/** Install descriptors through the production set; expose no alternate cast implementation. */
struct B10CommandHero : B9CastHero
{
	B10CommandHero(PFWorld* world, const SpawnInfo& spawn)
		: B9CastHero(world, spawn, NDb::FACTION_FREEZE) {}
	void InstallTalent(const NDb::Talent* db, bool bought = true)
	{
		pTalents = new PFTalentsSet(GetWorld());
		PFTalentsSet::SetInfo info;
		info[0][0].pTalentDesc = db; info[0][0].refineRate = 1;
		pTalents->LoadSet(this, info);
		if (bought) GetTalent(0, 0)->Activate();
	}
	void InstallPortal(const NDb::Talent* db, bool bought = true)
	{
		portal = new PFTalent(this, db, 1, -1, -2, false);
		if (bought) portal->Activate();
	}
};

/** Reuse the literal hero DB fixture while retaining a separately registered real actor. */
static CObj<B10CommandHero> CommandHeroB10(PFWorld* world)
{
	CObj<B9CastHero> seed = B9MaleHero(world, NDb::FACTION_FREEZE);
	PFBaseHero::SpawnInfo spawn;
	spawn.pHero = dynamic_cast<const NDb::Hero*>(seed->GetDbHero());
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	return new B10CommandHero(world, spawn);
}

/** Packed commands exercise real actor restoration, inventory and permission gates. */
static void ConsumableCommandAdmissionB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Consumable* raw = new NDb::Consumable;
	raw->manaCost.sString = "15"; raw->cooldownTime.sString = "0";
	raw->maxQuantity = 5; raw->requireLineOfSight = false;
	NDb::Ptr<NDb::Consumable> db = raw;
	check(hero->TakeConsumable(db, 2), "command fixture has actual consumable inventory");
	CObj<NCore::WorldCommand> command = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(hero.GetPtr()));
	check(command->CanExecute(), "live consumable command admits its exact actor and occupied slot");
	for (int slot : {-1, 1, 100})
	{
		CObj<NCore::WorldCommand> invalid = CreateCmdUseConsumable(hero.GetPtr(), slot, Target(hero.GetPtr()));
		check(!invalid->CanExecute(), "invalid or empty consumable slot fails command admission");
	}
	for (NDb::EUnitFlag flag : {NDb::UNITFLAG_FORBIDPLAYERCONTROL, NDb::UNITFLAG_INMINIGAME,
		NDb::UNITFLAG_FORBIDUSECONSUMABLES})
	{
		hero->AddFlag(flag);
		check(!command->CanExecute(), "consumable command observes live control and inventory permission");
		command->Execute(world);
		check(hero->GetMana() == 200 && hero->GetConsumable(0)->GetQuantity() == 2,
			"permission loss before execution cannot spend mana or inventory");
		hero->RemoveFlag(flag);
	}
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(command,
		world->GetPointerSerialization(), 1984, 0);
	CObj<PointersHolder> missingPointers = new PointersHolder(world, 0);
	CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(missingPointers);
	check(missing && !missing->CanExecute(), "restored consumable command rejects a missing actor");
	if (missing) missing->Execute(world);
	check(hero->GetMana() == 200 && hero->GetConsumable(0)->GetQuantity() == 2,
		"missing restored actor cannot substitute a world hero");
	hero->Cleanup();
}

/** The live unit queue approaches before payment and can cancel an uncommitted cast. */
static void ConsumableCommandQueueB10(Checks& check, PFWorld* world)
{
	for (int policy = 0; policy < 3; ++policy)
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4);
		NDb::Consumable* raw = new NDb::Consumable;
		raw->useRange.sString = "4"; raw->manaCost.sString = "70";
		raw->cooldownTime.sString = "0"; raw->maxQuantity = 5;
		raw->requireLineOfSight = false; raw->isUnitFreeAfterCast = true;
		NDb::Ptr<NDb::Consumable> db = raw;
		hero->TakeConsumable(db, 2);
		CObj<PFConsumable> item = const_cast<PFConsumable*>(hero->GetConsumable(0));
		CObj<NCore::WorldCommand> source = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(CVec3(15, 5, 0)));
		CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
			world->GetPointerSerialization(), 1984, 0);
		CObj<NCore::WorldCommand> command = packet->GetWorldCommand(world->GetPointerSerialization());
		check(command && command->CanExecute(), "packed consumable command retains its exact live actor");
		if (command) command->Execute(world);
		check(hero->HasQueuedStates() && hero->GetMana() == 200 && item->GetQuantity() == 2,
			"consumable command queues approach without instant payment");
		hero->Simulate(0);
		check(hero->IsMoving() && hero->GetMana() == 200 && item->GetQuantity() == 2,
			"real unit simulation enters consumable approach before payment");
		CPtr<CObjectBase> retired(dynamic_cast<CObjectBase*>(hero->GetCurrentState()));
		const int stateSize = retired ? retired->GetSizeOf() : 0;
		check(IsValid(retired) && nstl::string(retired->GetObjectTypeName()) == "LinuxConsumableCommandState",
			"native consumable command owns concrete intrusive reconstruction hooks");
		check(retired && retired->GetTypeId() == PFHeroUseConsumableState::typeId,
			"native consumable command preserves original numeric state identity");
		if (policy == 1) hero->DoStop();
		if (policy == 2) hero->RemoveConsumable(0);
		for (int i = 0; i < 100 && item->GetQuantity() == 2; ++i) hero->Simulate(0.1f);
		check(hero->GetMana() == (policy == 0 ? 130.f : 200.f) && item->GetQuantity() == (policy == 0 ? 1 : 2),
			"queued consumable pays once at arrival and never after stop or inventory removal");
		check(policy != 0 || hero->IsPositionInRange(CVec2(15, 5), 4),
			"queued consumable casts only inside the authored range");
		hero->DoStop();
		check(retired && !IsValid(retired) && nstl::string(retired->GetObjectTypeName()) == "LinuxConsumableCommandState",
			"completed or cancelled consumable retains its concrete weak shell");
		check(retired && retired->GetSizeOf() == stateSize && retired->GetTypeId() == PFHeroUseConsumableState::typeId,
			"consumable weak shell preserves allocation size and original numeric identity");
	}
}

/** Restored missing targets and nonfinite positions must never become a self cast. */
static void ConsumableCommandTargetsB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Consumable* raw = new NDb::Consumable;
	raw->manaCost.sString = "15"; raw->cooldownTime.sString = "0";
	raw->maxQuantity = 5; raw->requireLineOfSight = false;
	NDb::Ptr<NDb::Consumable> db = raw; hero->TakeConsumable(db, 2);
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<NCore::WorldCommand> source = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(unit.GetPtr()));
	check(source->CanExecute(), "consumable command admits an exact registered object target");
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
		world->GetPointerSerialization(), 1984, 0);
	CObj<PointersHolder> missingPointers = new PointersHolder(world, 0);
	missingPointers->Add(hero.GetPtr(), world->GetPointerSerialization()->GetObjectID(hero.GetPtr()));
	CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(missingPointers);
	check(missing && !missing->CanExecute(), "consumable command rejects a missing restored target");
	if (missing) missing->Execute(world);
	hero->Simulate(0);
	check(hero->GetMana() == 200 && hero->GetConsumable(0)->GetQuantity() == 2 && !hero->HasQueuedStates(),
		"missing consumable target cannot queue or pay for a fallback self cast");
	hero->DoStop();
	for (CVec3 point : {CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0),
		CVec3(5, std::numeric_limits<float>::infinity(), 0),
		CVec3(5, 5, std::numeric_limits<float>::infinity()), CVec3(-1, 5, 0),
		CVec3(world->GetMapSize().x, 5, 0)})
	{
		CObj<NCore::WorldCommand> invalid = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(point));
		check(!invalid->CanExecute(), "consumable command rejects malformed or off-map positions");
		invalid->Execute(world);
		check(!hero->HasQueuedStates() && !hero->GetCurrentState() && hero->GetMana() == 200,
			"invalid position cannot enqueue an order even if execution is called directly");
		hero->DoStop();
	}
	CObj<NCore::IWorldBase> foreignOwner = PFWorld::CreatePFWorld();
	PFWorld* foreignWorld = dynamic_cast<PFWorld*>(foreignOwner.GetPtr());
	const bool loaded = foreignWorld && foreignWorld->LoadMap(world->GetMapDescription(), 0,
		NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
	check(loaded, "foreign target fixture has a real independently loaded world");
	if (loaded)
	{
		CObj<PFBaseUnit> foreign = Unit(foreignWorld, NDb::FACTION_FREEZE, 20);
		CObj<NCore::WorldCommand> command = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(foreign.GetPtr()));
		check(!command->CanExecute(), "foreign target identity is not replaced by a matching local object id");
		command->Execute(world);
		check(!hero->HasQueuedStates() && hero->GetMana() == 200,
			"direct execution cannot queue a consumable for another world's object");
	}
}

/** Talent and portal commands share exact actor, purchase and live-control admission. */
static void TalentCommandAdmissionB10(Checks& check, PFWorld* world)
{
	for (bool portal : {false, true})
	for (bool bought : {false, true})
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		NDb::Talent* raw = new NDb::Talent;
		raw->type = NDb::ABILITYTYPE_ACTIVE;
		raw->targetType = NDb::SPELLTARGET_SELF;
		raw->useRange.sString = "0"; raw->manaCost.sString = "15";
		raw->naftaCost = 0; raw->cooldownTime.sString = "0";
		raw->requireLineOfSight = false;
		NDb::Ptr<NDb::Talent> db = raw;
		if (portal) hero->InstallPortal(db, bought); else hero->InstallTalent(db, bought);
		CObj<NCore::WorldCommand> command = portal ?
			CreateCmdUsePortal(hero.GetPtr(), Target(hero.GetPtr()), true) :
			CreateCmdUseTalent(hero.GetPtr(), 0, 0, Target(hero.GetPtr()), true);
		check(command->CanExecute() == bought, "talent and portal commands require real purchase admission");
		for (NDb::EUnitFlag flag : {NDb::UNITFLAG_FORBIDPLAYERCONTROL, NDb::UNITFLAG_INMINIGAME})
		{
			hero->AddFlag(flag);
			check(!command->CanExecute(), "talent and portal commands observe live control permissions");
			command->Execute(world);
			check(hero->GetMana() == 200 && !hero->HasQueuedStates(),
				"blocked talent or portal execution cannot spend resources or enqueue a cast");
			hero->RemoveFlag(flag);
		}
		CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(command,
			world->GetPointerSerialization(), 1984, 0);
		CObj<PointersHolder> pointers = new PointersHolder(world, 0);
		CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(pointers);
		check(missing && !missing->CanExecute(), "talent and portal commands reject missing restored actors");
		if (!portal)
			for (int slot : {-1, 1, 100})
			{
				CObj<NCore::WorldCommand> invalid = CreateCmdUseTalent(hero.GetPtr(), 0, slot, Target(hero.GetPtr()), true);
				check(!invalid->CanExecute(), "talent command rejects invalid or empty slots before lookup");
			}
		hero->DoStop();
	}
}

/** Packed talent orders must approach before payment; diagnostics observe actual casting. */
static void TalentCommandQueueB10(Checks& check, PFWorld* world, bool portal = false)
{
	for (int policy = 0; policy < 3; ++policy)
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4);
		NDb::Talent* raw = new NDb::Talent; raw->type = NDb::ABILITYTYPE_ACTIVE;
		raw->useRange.sString = "4"; raw->manaCost.sString = "70";
		raw->naftaCost = 0; raw->cooldownTime.sString = "0"; raw->requireLineOfSight = false;
		NDb::Ptr<NDb::Talent> db = raw;
		if (portal) hero->InstallPortal(db); else hero->InstallTalent(db);
		PFTalent* talent = portal ? hero->GetPortal() : hero->GetTalent(0, 0);
		CObj<NCore::WorldCommand> source = portal ? CreateCmdUsePortal(hero.GetPtr(), Target(CVec3(15, 5, 0)), true) :
			CreateCmdUseTalent(hero.GetPtr(), 0, 0, Target(CVec3(15, 5, 0)), true);
		const auto actualCasts = [portal]() {
			const auto diagnostic = GetLinuxHeroGameplayCommandDiagnostics();
			return portal ? diagnostic.usePortalActionAccepted : diagnostic.useTalentActionAccepted;
		};
		CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
			world->GetPointerSerialization(), 1984, 0);
		CObj<NCore::WorldCommand> command = packet->GetWorldCommand(world->GetPointerSerialization());
		ResetLinuxHeroGameplayCommandDiagnostics();
		check(command && command->CanExecute(), "packed talent order admits its exact actor and purchased talent");
		if (command) command->Execute(world);
		check(hero->HasQueuedStates() && hero->GetMana() == 200 && talent->GetLastUseStep() == -1 &&
			actualCasts() == 0,
			"talent command admission neither pays nor reports an actual cast");
		hero->Simulate(0);
		check(hero->IsMoving() && hero->GetMana() == 200,
			"real unit simulation approaches a queued talent target before payment");
		CPtr<CObjectBase> retired(dynamic_cast<CObjectBase*>(hero->GetCurrentState()));
		const int stateSize = retired ? retired->GetSizeOf() : 0;
		check(IsValid(retired) && nstl::string(retired->GetObjectTypeName()) == "LinuxTalentCommandState",
			"native talent or portal command owns concrete intrusive reconstruction hooks");
		check(retired && retired->GetTypeId() == PFHeroUseTalentState::typeId && stateSize > sizeof(PFHeroUseTalentState),
			"native talent command reports its additional storage without changing numeric identity");
		if (policy == 1) hero->DoStop();
		if (policy == 2) talent->AddForbid();
		for (int i = 0; i < 100 && talent->GetLastUseStep() == -1; ++i) hero->Simulate(0.1f);
		check(hero->GetMana() == (policy == 0 ? 130.f : 200.f) &&
			actualCasts() == (policy == 0 ? 1 : 0),
			"talent casts once at arrival and never after cancellation or permission loss");
		check(policy != 0 || (hero->IsPositionInRange(CVec2(15, 5), 4) && (portal ||
			(GetLinuxHeroGameplayCommandDiagnostics().useTalentLastUseStepBefore == -1 &&
			GetLinuxHeroGameplayCommandDiagnostics().useTalentLastUseStepAfter == talent->GetLastUseStep()))),
			"cast diagnostics capture actual state-driven talent execution");
		hero->DoStop();
		check(retired && !IsValid(retired) && nstl::string(retired->GetObjectTypeName()) == "LinuxTalentCommandState",
			"completed or cancelled talent and portal retain their concrete weak shell");
		check(retired && retired->GetSizeOf() == stateSize && retired->GetTypeId() == PFHeroUseTalentState::typeId,
			"talent weak shell preserves allocation size and original numeric identity");
	}
}

/** Portal position admission uses the same finite, half-open map boundary as other casts. */
static void PortalCommandTargetsB10(Checks& check, PFWorld* world)
{
	CObj<B10CommandHero> hero = CommandHeroB10(world);
	NDb::Talent* raw = new NDb::Talent; raw->type = NDb::ABILITYTYPE_ACTIVE;
	raw->useRange.sString = "4"; raw->manaCost.sString = "0"; raw->naftaCost = 0;
	raw->requireLineOfSight = false;
	NDb::Ptr<NDb::Talent> db = raw; hero->InstallPortal(db);
	for (CVec3 point : {CVec3(-1, 5, 0), CVec3(5, std::numeric_limits<float>::infinity(), 0),
		CVec3(world->GetMapSize().x, 5, 0)})
	{
		CObj<NCore::WorldCommand> command = CreateCmdUsePortal(hero.GetPtr(), Target(point), true);
		check(!command->CanExecute(), "portal command rejects malformed/off-map targets before queueing");
		command->Execute(world);
		check(!hero->HasQueuedStates(), "direct portal execution cannot queue a malformed target");
		hero->DoStop();
	}
}

/** Authored masks admit only the requested relation/kind; invalid-target counters stay authoritative. */
static void TalentCommandTargetMasksB10(Checks& check, PFWorld* world)
{
	for (bool portal : {false, true})
	for (int relation = 0; relation < 3; ++relation)
	for (int policy = 0; policy < 5; ++policy)
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		CObj<PFBaseUnit> other = Unit(world, relation == 2 ? NDb::FACTION_BURN : NDb::FACTION_FREEZE, 20);
		PFBaseUnit* target = relation == 0 ? static_cast<PFBaseUnit*>(hero.GetPtr()) : other.GetPtr();
		const unsigned kind = 1u << target->GetUnitKind();
		NDb::Talent* raw = new NDb::Talent; raw->type = NDb::ABILITYTYPE_ACTIVE;
		raw->targetType = static_cast<NDb::ESpellTarget>(policy == 0 ? 0 : policy == 1 ? NDb::SPELLTARGET_SELF :
			policy == 2 ? kind | NDb::SPELLTARGET_ALLY : policy == 3 ? kind | NDb::SPELLTARGET_ENEMY : NDb::SPELLTARGET_ENEMY);
		raw->useRange.sString = "4"; raw->manaCost.sString = "0"; raw->naftaCost = 0;
		raw->requireLineOfSight = false;
		NDb::Ptr<NDb::Talent> db = raw;
		if (portal) hero->InstallPortal(db); else hero->InstallTalent(db);
		CObj<NCore::WorldCommand> command = portal ? CreateCmdUsePortal(hero.GetPtr(), Target(target), true) :
			CreateCmdUseTalent(hero.GetPtr(), 0, 0, Target(target), true);
		const bool allowed = policy == 0 || (policy == 1 && relation == 0) ||
			(policy == 2 && relation == 1) || (policy == 3 && relation == 2);
		check(command->CanExecute() == allowed, "talent/portal target admission preserves authored self/faction/kind masks");
		if (!allowed)
		{
			command->Execute(world);
			check(!hero->HasQueuedStates(), "invalid authored talent target cannot replace an order through direct execution");
		}
		if (allowed && policy != 0)
		{
			target->SetInvalidAbilityTarget(true);
			check(!command->CanExecute(), "engine invalid-ability-target predicate overrides an otherwise allowed mask");
			target->SetInvalidAbilityTarget(false);
		}
		hero->DoStop();
	}
}

/** Pending state entry rechecks authored target facts that can change after command admission. */
static void QueuedTalentTargetChangesB10(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool portal : {false, true})
	for (int policy = 0; policy < 3; ++policy)
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		CObj<PFBaseUnit> target = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Talent* raw = new NDb::Talent; raw->type = NDb::ABILITYTYPE_ACTIVE;
		raw->targetType = static_cast<NDb::ESpellTarget>((1u << target->GetUnitKind()) | NDb::SPELLTARGET_ENEMY);
		raw->useRange.sString = "100"; raw->manaCost.sString = "70"; raw->naftaCost = 0;
		raw->requireLineOfSight = false; raw->cooldownTime.sString = "0";
		NDb::Ptr<NDb::Talent> db = raw;
		if (portal) hero->InstallPortal(db); else hero->InstallTalent(db);
		CObj<NCore::WorldCommand> command = portal ? CreateCmdUsePortal(hero.GetPtr(), Target(target.GetPtr()), true) :
			CreateCmdUseTalent(hero.GetPtr(), 0, 0, Target(target.GetPtr()), true);
		check(command->CanExecute(), "queued target-change fixture admits its authored enemy target");
		command->Execute(world);
		if (policy == 1) target->ChangeFaction(NDb::FACTION_FREEZE);
		if (policy == 2) target->SetInvalidAbilityTarget(true);
		ResetLinuxHeroGameplayCommandDiagnostics();
		hero->Simulate(0);
		const auto diagnostic = GetLinuxHeroGameplayCommandDiagnostics();
		check(hero->GetMana() == (policy == 0 ? 130.f : 200.f) &&
			(portal ? diagnostic.usePortalActionAccepted : diagnostic.useTalentActionAccepted) == (policy == 0 ? 1 : 0),
			"queued talent/portal rechecks faction and invalid-target policy before payment");
		hero->DoStop();
		if (policy == 2) target->SetInvalidAbilityTarget(false);
	}
}

/** Control loss must cancel pending work both before entry and while its movement child is active. */
static void QueuedCastControlLossB10(Checks& check, PFWorld* world)
{
	for (int kind = 0; kind < 3; ++kind)
	for (bool approaching : {false, true})
	for (NDb::EUnitFlag flag : {NDb::UNITFLAG_FORBIDPLAYERCONTROL, NDb::UNITFLAG_INMINIGAME})
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4);
		NDb::Talent* raw = new NDb::Talent; raw->type = NDb::ABILITYTYPE_ACTIVE;
		raw->useRange.sString = "4"; raw->manaCost.sString = "70"; raw->naftaCost = 0;
		raw->requireLineOfSight = false; raw->cooldownTime.sString = "0";
		NDb::Ptr<NDb::Talent> db = raw;
		if (kind == 1) hero->InstallTalent(db);
		if (kind == 2) hero->InstallPortal(db);
		if (kind == 0)
		{
			NDb::Consumable* item = new NDb::Consumable;
			item->useRange.sString = "4"; item->manaCost.sString = "70";
			item->requireLineOfSight = false; item->maxQuantity = 5;
			hero->TakeConsumable(item, 2);
		}
		const Target point(CVec3(15, 5, 0));
		CObj<NCore::WorldCommand> command = kind == 0 ? CreateCmdUseConsumable(hero.GetPtr(), 0, point) :
			kind == 1 ? CreateCmdUseTalent(hero.GetPtr(), 0, 0, point, true) : CreateCmdUsePortal(hero.GetPtr(), point, true);
		check(command->CanExecute(), "control-loss fixture starts from an admitted cast order");
		command->Execute(world);
		if (approaching)
		{
			hero->Simulate(0);
			check(hero->IsMoving() && hero->GetMana() == 200,
				"control-loss fixture enters a real prepayment movement child");
		}
		hero->AddFlag(flag);
		for (int i = 0; i < 50; ++i) hero->Simulate(0.1f);
		check(hero->GetMana() == 200 && (kind != 0 || hero->GetConsumable(0)->GetQuantity() == 2),
			"pending casts cannot pay after player-control loss or minigame entry");
		check(!hero->IsMoving() && !hero->HasQueuedStates() && !hero->GetCurrentState(),
			"control loss releases the queued cast and its active movement child");
		hero->RemoveFlag(flag); hero->DoStop();
	}
}
