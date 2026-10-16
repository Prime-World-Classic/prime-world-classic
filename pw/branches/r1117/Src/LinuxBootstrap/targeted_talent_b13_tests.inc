#include "targeted_talent.h"
#include "ground_talent_target.h"

/** Register a real human player through map loading, then attach a controlled test hero and authored talent. */
struct TargetedTalentFixtureB13
{
	static constexpr int ClientId = 1939;
	B8BehaviourForeignWorld map;
	CObj<B10CommandHero> hero;
	CObj<B9CastHero> victim;
	NDb::Ptr<NDb::Talent> db;
	static NCore::TPlayersStartInfo Players(bool reservePlayer = false)
	{
		NCore::TPlayersStartInfo players;
		players.push_back(NCore::PlayerStartInfo(0, NCore::ETeam::Team1, NCore::ETeam::Team1,
			NCore::EPlayerType::Human, ClientId, NCore::ESex::Male));
		if (reservePlayer)
			players.push_back(NCore::PlayerStartInfo(1, NCore::ETeam::Team1, NCore::ETeam::Team1,
				NCore::EPlayerType::Human, ClientId + 1, NCore::ESex::Male));
		return players;
	}
	explicit TargetedTalentFixtureB13(Checks& check, bool bought = true, bool reservePlayer = false)
		: map(check, Players(reservePlayer))
	{
		if (!map.world) return;
		hero = CommandHeroB10(map.world); victim = B9MaleHero(map.world, NDb::FACTION_BURN);
		hero->AttachToPlayer(map.world->GetPlayer(0)); map.world->GetPlayer(0)->SetIsPlaying(true);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); victim->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK); victim->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4);
		hero->EnableAbilityAI(false); victim->EnableAbilityAI(false); victim->TeleportTo(CVec2(8, 5));
		auto* raw = new NDb::Talent; raw->type = NDb::ABILITYTYPE_ACTIVE;
		raw->flags = NDb::ABILITYFLAGS_MOMENTARY; raw->requireLineOfSight = false;
		raw->targetType = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ENEMY);
		raw->naftaCost = 0; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5"; raw->useRange.sString = "10";
		auto* immediate = new NDb::DispatchImmediate; immediate->instantApply = true; raw->dispatch = immediate;
		auto* damage = new NDb::DamageApplicator; damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; raw->applicators.push_back(damage);
		db = raw; hero->InstallTalent(raw, bought); map.world->Step(.125f, .125f);
	}
	/** Retain the authored descriptor so each mutation is observed by production eligibility code. */
	NDb::Talent* Descriptor() const { return const_cast<NDb::Talent*>(db.GetPtr()); }
	PFTalent* Talent() const { return hero->GetTalent(0, 0); }
	bool CanUse(const Target& target) const { return CanUseLinuxTargetedTalent(hero, 0, 0, target, ClientId); }
	/** Create the same nonscript command and scheduler identity as the native HUD. */
	CObj<NCore::WorldCommand> Command(const Target& target) const
	{
		CObj<NCore::WorldCommand> command = CreateCmdUseLinuxTargetedTalent(hero.GetPtr(), 0, 0, target);
		if (command) command->SetId(ClientId);
		return command;
	}
	~TargetedTalentFixtureB13()
	{
		if (hero) { hero->DetachFromPlayer(); hero->Cleanup(); }
		if (victim) victim->Cleanup();
	}
};

/** Admission is read-only and binds both sides of the real player/hero association. */
static void TargetedTalentOwnershipB13(Checks& check)
{
	TargetedTalentFixtureB13 f(check); if (!f.map.world) return;
	const Target unit(f.victim.GetPtr());
	for (const auto slot : {nstl::pair<int, int>(-1, 0), {6, 0}, {0, -1}, {0, 6}, {0, 1}})
		check(!CanArmLinuxTargetedTalent(f.hero, slot.first, slot.second)
			&& !CanUseLinuxTargetedTalent(f.hero, slot.first, slot.second, unit, f.ClientId),
			"B13 targeted talent rejects out-of-bounds and unoccupied slots without substitution");
	check(!CanArmLinuxTargetedTalent(nullptr, 0, 0)
		&& !CanUseLinuxTargetedTalent(nullptr, 0, 0, unit, f.ClientId), "B13 missing caster cannot use another player's hero");
	check(!CanUseLinuxTargetedTalent(f.hero, 0, 0, unit, -1), "B13 targeted talent rejects an unresolved client UID");
	PFPlayer* player = f.hero->GetPlayer();
	player->SetUserID(-1);
	check(player->IsBot() && !f.CanUse(unit), "B13 a bot player cannot borrow manual human HUD admission");
	player->SetUserID(f.ClientId);
	player->SetIsPlaying(false);
	check(!f.CanUse(unit), "B13 disconnected human is not a manual HUD caster");
	player->SetIsPlaying(true); player->DetachHero();
	check(!f.CanUse(unit), "B13 a one-sided hero-to-player reference cannot authorize a HUD cast");
	player->AttachHero(f.hero.GetPtr());
	f.hero->DetachFromPlayer();
	check(!f.CanUse(unit), "B13 detached hero cannot borrow its former player's UID");
	f.hero->AttachToPlayer(player);
	for (NDb::EUnitFlag flag : {NDb::UNITFLAG_FORBIDPLAYERCONTROL, NDb::UNITFLAG_INMINIGAME})
	{
		f.hero->AddFlag(flag);
		check(!f.CanUse(unit), "B13 live player-control and minigame flags prevent manual unit selection");
		f.hero->RemoveFlag(flag);
	}
	for (int i = 0; i < 3; ++i)
		check(CanArmLinuxTargetedTalent(f.hero, 0, 0) && f.CanUse(unit), "B13 restored exact ownership makes the same talent selectable");
	check(f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000
		&& f.Talent()->GetCurrentCooldown() == 0 && f.Talent()->GetLastUseStep() == -1 && !f.hero->HasQueuedStates(),
		"B13 repeated HUD admission cannot pay, damage, start cooldown or enqueue gameplay");
	f.map.world->UnregisterObject(f.hero.GetPtr(), false); f.map.world->ProcessAddRemove();
	check(!f.CanUse(unit), "B13 a retained but unregistered hero cannot authorize a manual cast");
}

/** Target identity and authored kind/relation predicates are independent of pointer liveness. */
static void TargetedTalentEndpointsB13(Checks& check)
{
	TargetedTalentFixtureB13 f(check), foreign(check); if (!f.map.world || !foreign.map.world) return;
	const Target unit(f.victim.GetPtr()), self(f.hero.GetPtr()), remote(foreign.victim.GetPtr()), invalid;
	check(!f.CanUse(invalid) && !f.CanUse(remote), "B13 invalid and foreign-world targets never resolve to a local fallback");
	f.victim->EnemyVisible(false);
	check(!f.victim->IsVisibleForFaction(f.hero->GetFaction()) && !f.CanUse(unit),
		"B13 hidden enemy cannot be selected even when its world object still exists");
	f.victim->EnemyVisible(true);
	f.victim->SetInvalidAbilityTarget(true);
	check(!f.CanUse(unit), "B13 original invalid-ability-target condition overrides a matching authored mask");
	f.victim->SetInvalidAbilityTarget(false);
	f.victim->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	check(!f.CanUse(unit), "B13 forbidden-pick units cannot be acquired by a manual HUD click");
	f.victim->RemoveFlag(NDb::UNITFLAG_FORBIDPICK);
	auto* raw = f.Descriptor();
	raw->targetType = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROFEMALE | NDb::SPELLTARGET_ENEMY);
	check(!f.CanUse(unit), "B13 enemy relation cannot override an authored unit-kind mismatch");
	raw->targetType = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ALLY);
	check(!f.CanUse(unit), "B13 ally-only talent cannot select an enemy of the correct kind");
	f.victim->ChangeFaction(f.hero->GetFaction());
	check(f.CanUse(unit), "B13 ally-only talent selects the exact same unit after an actual faction change");
	check(!f.CanUse(self), "B13 ally targeting does not implicitly grant self targeting");
	raw->targetType = static_cast<NDb::ESpellTarget>(raw->targetType | NDb::SPELLTARGET_SELF);
	check(f.CanUse(self), "B13 explicit self permission admits the exact player hero");
	raw->targetType = NDb::SPELLTARGET_SELF;
	f.hero->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	check(CanArmLinuxTargetedTalent(f.hero, 0, 0) && f.CanUse(self) && !f.CanUse(unit),
		"B13 pure self targeting arms and retains the original self exception to forbidden pick");
	f.hero->RemoveFlag(NDb::UNITFLAG_FORBIDPICK);
	raw->targetType = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ENEMY);
	check(!f.CanUse(unit), "B13 a formerly hostile unit cannot retain enemy permission after changing faction");
	f.victim->ChangeFaction(NDb::FACTION_BURN);
	check(f.CanUse(unit), "B13 original enemy mask is usable after its live relation is restored");
	f.map.world->UnregisterObject(f.victim.GetPtr(), false); f.map.world->ProcessAddRemove();
	check(!f.CanUse(unit), "B13 strong external target references cannot authorize unregistered engine objects");
	check(f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000 && f.Talent()->GetLastUseStep() == -1,
		"B13 failed endpoint checks preserve resources, payload and cast history");
}

/** Readiness must consume actual purchase, affordability, cooldown and authored formula state. */
static void TargetedTalentReadinessB13(Checks& check)
{
	TargetedTalentFixtureB13 f(check, false); if (!f.map.world) return;
	const Target unit(f.victim.GetPtr());
	check(!CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit), "B13 an unbought unit talent cannot arm or select");
	check(f.Talent()->Activate() && f.CanUse(unit), "B13 real talent purchase restores unit selection");
	f.Talent()->AddForbid();
	check(!CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit), "B13 forbidden talent cannot remain selectable");
	f.Talent()->RemoveForbid();
	f.hero->ChangeMana(-181);
	check(f.hero->GetMana() == 19 && !CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit),
		"B13 one mana short rejects unit selection without borrowing another resource pool");
	f.hero->ChangeMana(1);
	check(f.hero->GetMana() == 20 && f.CanUse(unit), "B13 exact authored mana cost is affordable");
	f.hero->ChangeMana(180); f.Talent()->RestartCooldown(5);
	check(!CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit), "B13 active engine cooldown rejects unit selection");
	f.Talent()->DropCooldown(true);
	auto* raw = f.Descriptor(); raw->type = NDb::ABILITYTYPE_PASSIVE;
	check(!CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit), "B13 purchased passive talent is not a manual unit command");
	raw->type = NDb::ABILITYTYPE_ACTIVE;
	for (const char* expression : {"unknownRange", "1/0", "sqrt(-1)", "1e39", "-1"})
	{
		raw->useRange.sString = expression;
		check(!f.CanUse(unit), "B13 malformed, nonfinite and negative unit ranges cannot authorize a cast");
	}
	raw->useRange.sString = "10";
	auto* condition = new NDb::ConditionCastLimitation; raw->castLimitations.push_back(condition);
	for (const char* expression : {"false", "unknownCondition", "1/0"})
	{
		condition->condition.sString = expression;
		check(!f.CanUse(unit), "B13 failed or unsupported original cast condition rejects selection before payment");
	}
	f.victim->SetHealth(900); condition->condition.sString = "rLife == 900 && sLife == 1000";
	check(f.CanUse(unit), "B13 target-dependent cast limitations use the actual clicked receiver");
	condition->condition.sString = "true";
	NDb::Ptr<NDb::AlternativeTarget> alternative = new NDb::AlternativeTarget;
	raw->alternativeTargets.push_back(alternative);
	check(!CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit), "B13 unsupported authored alternatives cannot arm a unit talent");
	raw->alternativeTargets.clear();
	AbilityTarget alternate(unit); alternate.SetDBAlternativeTarget(alternative);
	check(!f.CanUse(alternate), "B13 an attached alternative endpoint cannot bypass unsupported-alternative admission");
	check(f.CanUse(unit), "B13 restored ordinary target remains usable after rejected alternative input");
	f.victim->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(f.victim->IsDead() && !f.CanUse(unit), "B13 a dead registered unit is not a live HUD endpoint");
}

/** Keep unit approach semantics distinct from the deliberately unchanged ground-only policy. */
static void TargetedTalentRangeAndGroundB13(Checks& check)
{
	TargetedTalentFixtureB13 f(check); if (!f.map.world) return;
	const Target unit(f.victim.GetPtr()); auto* raw = f.Descriptor(); raw->useRange.sString = "1";
	check(!f.hero->IsTargetInRange(unit, 1) && f.CanUse(unit), "B13 distant unit selection can enqueue the original approach HFSM");
	f.hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	check(!f.CanUse(unit), "B13 rooted caster cannot accept a unit target requiring approach");
	raw->useRange.sString = "10";
	check(f.CanUse(unit), "B13 rooted caster can still select an already in-range unit");
	raw->useRange.sString = "1";
	raw->flags = static_cast<NDb::EAbilityFlags>(raw->flags | NDb::ABILITYFLAGS_CANUSEOUTOFRANGE);
	check(f.CanUse(unit), "B13 authored out-of-range use avoids an invented movement requirement");
	raw->flags = NDb::ABILITYFLAGS_MOMENTARY;
	f.hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(0);
	f.victim->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(20);
	raw->useRange.sString = "1+tStrength";
	check(f.CanUse(unit), "B13 rooted unit range samples receiver stats instead of substituting the caster");
	raw->useRange.sString = "100/tStrength";
	check(!std::isfinite(f.Talent()->GetUseRange()) && CanArmLinuxTargetedTalent(f.hero, 0, 0) && f.CanUse(unit),
		"B13 arming does not reject a target-dependent range that is undefined for self but valid for the clicked unit");
	f.victim->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(0);
	check(CanArmLinuxTargetedTalent(f.hero, 0, 0) && !f.CanUse(unit),
		"B13 target-dependent range failure is rejected only after evaluating the actual selected receiver");
	f.hero->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
	raw->useRange.sString = "10"; raw->requireLineOfSight = true;
	check(CanArmLinuxTargetedTalent(f.hero, 0, 0) && f.hero->CanSee(f.victim->GetPosition()) && f.CanUse(unit),
		"B13 authored personal line of sight supports visible unit endpoints through original hero policy");
	B8BehaviourReveal teamVision(f.map.world, f.hero->GetFaction());
	f.hero->CloseWarFog(true); f.hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	check(f.victim->IsVisibleForFaction(f.hero->GetFaction()) && !f.hero->CanSee(f.victim->GetPosition()) && !f.CanUse(unit),
		"B13 lack of personal sight rejects a rooted cast even when faction visibility and distance pass");
	f.hero->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
	check(f.CanUse(unit), "B13 movable hero may approach to satisfy authored personal sight");
	raw->requireLineOfSight = false;
	raw->targetType = static_cast<NDb::ESpellTarget>(raw->targetType | NDb::SPELLTARGET_LINEOFSIGHT);
	check(CanArmLinuxTargetedTalent(f.hero, 0, 0) && f.CanUse(unit),
		"B13 direct unit clicks retain Windows selection rather than automatic-selector line-of-sight filtering");
	raw->targetType = NDb::SPELLTARGET_LAND;
	for (const char* range : {"1", "10", "0", "-1", "unknownRange"})
	for (const CVec3 point : {CVec3(8, 5, 0), CVec3(-1, 5, 0), CVec3(f.map.world->GetMapSize().x, 5, 0),
		CVec3(5, std::numeric_limits<float>::quiet_NaN(), 0)})
	{
		raw->useRange.sString = range; const Target target(point);
		check(f.CanUse(target) == CanUseLinuxGroundTalent(f.hero, 0, 0, target, f.ClientId),
			"B13 ground targeting delegates unchanged finite geometry and original range policy");
	}
	raw->useRange.sString = "10"; raw->requireLineOfSight = true;
	const Target point(CVec2(8, 5));
	check(!f.CanUse(point) && !CanUseLinuxGroundTalent(f.hero, 0, 0, point, f.ClientId),
		"B13 unit targeting must not silently broaden unsupported ground line-of-sight admission");
	raw->requireLineOfSight = false;
	raw->targetType = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_LAND | NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ENEMY);
	check(f.CanUse(unit) && f.CanUse(point), "B13 mixed authored masks retain both exact unit and ground endpoints");
	check(CanSelectLinuxTalentUnit(f.hero, f.Talent(), f.victim) && !CanSelectLinuxTalentUnit(f.hero, f.Talent(), f.hero),
		"B13 mixed click selection accepts the matching enemy but leaves ineligible self clicks to LAND");
	f.victim->SetInvalidAbilityTarget(true);
	check(CanSelectLinuxTalentUnit(f.hero, f.Talent(), f.victim) && !f.CanUse(unit) && f.CanUse(point),
		"B13 accepted unit selection is distinct from a failed ability check and must not retry LAND after cast rejection");
	f.victim->SetInvalidAbilityTarget(false);
	f.victim->AddFlag(NDb::UNITFLAG_FORBIDPICK);
	check(!CanSelectLinuxTalentUnit(f.hero, f.Talent(), f.victim) && f.CanUse(point),
		"B13 original forbidden-pick rejection leaves an independent authored LAND endpoint available");
	f.victim->RemoveFlag(NDb::UNITFLAG_FORBIDPICK);
}

/** Packed manual commands must deliver the original damage once, only after real approach. */
static void TargetedTalentCommandB13(Checks& check)
{
	for (bool approach : {false, true})
	{
		TargetedTalentFixtureB13 f(check); if (!f.map.world) continue;
		if (approach) { f.Descriptor()->useRange.sString = "4"; f.victim->TeleportTo(CVec2(15, 5)); }
		f.map.world->Step(.125f, .125f);
		const Target unit(f.victim.GetPtr());
		check(f.CanUse(unit), "B13 manual unit cast starts from a live HUD-admitted endpoint");
		CObj<NCore::WorldCommand> source = f.Command(unit);
		CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
			f.map.world->GetPointerSerialization(), f.ClientId, 0);
		CObj<NCore::WorldCommand> command = packet->GetWorldCommand(f.map.world->GetPointerSerialization());
		check(command && command->CanExecute(), "B13 packed manual talent retains its exact hero and unit target");
		ResetLinuxHeroGameplayCommandDiagnostics();
		if (command) command->Execute(f.map.world);
		check(f.hero->HasQueuedStates() && f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000
			&& f.Talent()->GetLastUseStep() == -1, "B13 command execution queues rather than paying or dispatching immediately");
		f.hero->Simulate(.125f);
		if (approach)
			check(f.hero->IsMoving() && f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000,
				"B13 original unit-target chase begins without early payment or damage");
		for (int i = 0; i < 100 && f.Talent()->GetLastUseStep() == -1; ++i) f.hero->Simulate(.125f);
		const auto diagnostics = GetLinuxHeroGameplayCommandDiagnostics();
		check(f.hero->GetMana() == 180 && f.victim->GetHealth() == 975 && f.Talent()->GetCurrentCooldown() > 0
			&& diagnostics.useTalentActionAccepted == 1, "B13 real manual unit cast pays once, delivers authored damage and starts cooldown");
		check(diagnostics.useTalentTargetObjectId == f.victim->GetObjectId()
			&& diagnostics.useTalentTargetFaction == f.victim->GetFaction()
			&& f.hero->IsTargetInRange(unit, f.Talent()->GetUseRange(unit)),
			"B13 actual cast evidence names the selected receiver and original in-range endpoint");
		check(!f.CanUse(unit) && command && !command->CanExecute(), "B13 cast cooldown rejects immediate duplicate manual selection");
		f.hero->DoStop();
		for (int i = 0; i < 4; ++i) f.map.world->Step(.125f, .125f);
		check(f.hero->GetMana() == 180 && f.victim->GetHealth() == 975
			&& GetLinuxHeroGameplayCommandDiagnostics().useTalentActionAccepted == 1,
			"B13 subsequent world ticks do not replay manual unit payment or payload");
	}
}

/** Original pending casts recheck live target and control changes before committing their effects. */
static void TargetedTalentLateInvalidityB13(Checks& check)
{
	for (bool approach : {false, true})
	for (int loss = 0; loss < 16; ++loss)
	{
		TargetedTalentFixtureB13 f(check); if (!f.map.world) continue;
		auto* raw = f.Descriptor();
		raw->targetType = static_cast<NDb::ESpellTarget>(raw->targetType | NDb::SPELLTARGET_VISIBLETARGETSONLY);
		auto* condition = new NDb::ConditionCastLimitation; condition->condition.sString = "true";
		raw->castLimitations.push_back(condition);
		if (approach) { raw->useRange.sString = "4"; f.victim->TeleportTo(CVec2(15, 5)); }
		f.map.world->Step(.125f, .125f);
		const Target unit(f.victim.GetPtr()); CObj<NCore::WorldCommand> command = f.Command(unit);
		CObj<PFTalent> originalTalent = f.Talent();
		CObj<PFPlayer> originalPlayer = f.hero->GetPlayer().GetPtr();
		check(f.CanUse(unit) && command->CanExecute(), "B13 late-loss fixture starts from a valid manual unit order");
		command->Execute(f.map.world);
		if (approach)
		{
			f.hero->Simulate(.125f);
			check(f.hero->IsMoving() && f.hero->GetMana() == 200, "B13 late-loss fixture enters real prepayment chase");
		}
		if (loss == 0) f.hero->DoStop();
		if (loss == 1) f.victim->SetInvalidAbilityTarget(true);
		if (loss == 2) f.victim->ChangeFaction(f.hero->GetFaction());
		if (loss == 3) f.hero->AddFlag(NDb::UNITFLAG_FORBIDPLAYERCONTROL);
		if (loss == 4) f.hero->AddFlag(NDb::UNITFLAG_INMINIGAME);
		if (loss == 5) f.Talent()->AddForbid();
		if (loss == 6) f.hero->ChangeMana(-200);
		if (loss == 7) condition->condition.sString = "false";
		if (loss == 8) f.victim->EnemyVisible(false);
		if (loss == 9) f.hero->GetPlayer()->SetIsPlaying(false);
		if (loss == 10) f.hero->GetPlayer()->DetachHero();
		if (loss == 11) f.hero->DetachFromPlayer();
		if (loss == 12) { f.map.world->UnregisterObject(f.hero.GetPtr(), false); f.map.world->ProcessAddRemove(); }
		if (loss == 13) f.hero->InstallTalent(raw);
		if (loss == 14)
		{
			f.map.world->UnregisterObject(f.victim.GetPtr(), false); f.map.world->ProcessAddRemove();
			check(IsUnitValid(f.victim.GetPtr())
				&& f.map.world->FindLinuxUnitByObjectId(f.victim->GetObjectId()) != f.victim.GetPtr(),
				"B13 late unregistration retains a live target reference without authoritative unit registry membership");
		}
		if (loss == 15)
		{
			check(f.map.world->IsSubscribed(originalPlayer.GetPtr()),
				"B13 registered human player participates in the original world step subscription");
			f.map.world->UnregisterObject(originalPlayer.GetPtr(), false); f.map.world->ProcessAddRemove();
			check(IsValid(originalPlayer) && f.map.world->GetPlayerByUID(f.ClientId) == originalPlayer.GetPtr()
				&& f.hero->GetPlayer() == originalPlayer.GetPtr() && !f.map.world->IsSubscribed(originalPlayer.GetPtr()),
				"B13 unregistration removes player subscription while UID lookup and hero association retain the object");
			check(!command->CanExecute(), "B13 retained but unregistered player cannot authorize an existing HUD command");
		}
		if (loss != 0 && loss != 13) check(!f.CanUse(unit), "B13 live invalidity withdraws HUD admission without clearing a different target");
		if (loss == 13) check(f.Talent() != originalTalent.GetPtr() && f.CanUse(unit),
			"B13 replacing a slot creates a different valid talent, not permission to retarget an old queued order");
		ResetLinuxHeroGameplayCommandDiagnostics();
		for (int i = 0; i < 80; ++i) f.hero->Simulate(.125f);
		check(f.hero->GetMana() == (loss == 6 ? 0 : 200) && f.victim->GetHealth() == 1000
			&& f.Talent()->GetCurrentCooldown() == 0 && f.Talent()->GetLastUseStep() == -1
			&& originalTalent->GetCurrentCooldown() == 0 && originalTalent->GetLastUseStep() == -1
			&& GetLinuxHeroGameplayCommandDiagnostics().useTalentActionAccepted == 0,
			"B13 cancellation or late target/control/resource/condition/ownership loss cannot commit a queued unit cast");
		f.hero->DoStop();
	}
}

/** Serialized missing endpoints fail closed even while another usable hero and unit remain live. */
static void TargetedTalentPackedIdentityB13(Checks& check)
{
	TargetedTalentFixtureB13 f(check); if (!f.map.world) return;
	const Target unit(f.victim.GetPtr()); CObj<NCore::WorldCommand> source = f.Command(unit);
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
		f.map.world->GetPointerSerialization(), f.ClientId, 0);
	for (int missing = 0; missing < 4; ++missing)
	{
		CObj<PointersHolder> pointers = new PointersHolder(f.map.world, 0);
		if (missing != 0) pointers->Add(f.hero.GetPtr(), f.map.world->GetPointerSerialization()->GetObjectID(f.hero.GetPtr()));
		if (missing != 1) pointers->Add(f.victim.GetPtr(), f.map.world->GetPointerSerialization()->GetObjectID(f.victim.GetPtr()));
		if (missing != 2) pointers->Add(f.hero->GetPlayer(), f.map.world->GetPointerSerialization()->GetObjectID(f.hero->GetPlayer()));
		if (missing != 3) pointers->Add(f.Talent(), f.map.world->GetPointerSerialization()->GetObjectID(f.Talent()));
		CObj<NCore::WorldCommand> command = packet->GetWorldCommand(pointers);
		check(command && !command->CanExecute(), "B13 packed HUD order rejects a missing actor, target, player or exact talent");
		if (command) command->Execute(f.map.world);
		f.hero->Simulate(.125f);
		check(f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000 && f.Talent()->GetLastUseStep() == -1
			&& !f.hero->HasQueuedStates(), "B13 failed packed HUD endpoints cannot cast through direct execution or a fallback");
	}
	source->SetId(f.ClientId + 1);
	check(!source->CanExecute(), "B13 changing a packed human command UID cannot transfer captured player ownership");
	source->Execute(f.map.world); f.hero->Simulate(.125f);
	check(f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000 && !f.hero->HasQueuedStates(),
		"B13 wrong command identity cannot commit captured human talent payment or damage");
}

/** Existing player APIs transfer a UID without changing the queued command's captured identities. */
static void TargetedTalentPlayerReplacementB13(Checks& check)
{
	for (int phase = 0; phase < 3; ++phase)
	{
		TargetedTalentFixtureB13 f(check, true, true); if (!f.map.world) continue;
		f.Descriptor()->useRange.sString = "4"; f.victim->TeleportTo(CVec2(15, 5));
		f.map.world->Step(.125f, .125f);
		const Target unit(f.victim.GetPtr());
		CObj<PFPlayer> capturedPlayer = f.hero->GetPlayer().GetPtr();
		CObj<PFTalent> capturedTalent = f.Talent();
		CObj<NCore::WorldCommand> original = f.Command(unit);
		check(original && original->CanExecute(), "B13 replacement fixture captures the original player and talent at construction");
		if (!original) continue;
		if (phase != 0) original->Execute(f.map.world);
		if (phase == 1)
			check(f.hero->HasQueuedStates() && f.hero->GetMana() == 200,
				"B13 captured manual order remains uncommitted between enqueue and first state entry");
		if (phase == 2)
		{
			f.hero->Simulate(.125f);
			check(f.hero->IsMoving() && f.hero->GetMana() == 200,
				"B13 captured manual order enters prepayment chase before authoritative player replacement");
		}
		PFPlayer* replacement = f.map.world->GetPlayer(1);
		capturedPlayer->SetUserID(f.ClientId + 2); replacement->SetUserID(f.ClientId);
		replacement->SetIsPlaying(true); f.hero->AttachToPlayer(replacement);
		check(replacement != capturedPlayer.GetPtr() && replacement->GetUserID() == f.ClientId
			&& f.map.world->GetPlayerByUID(f.ClientId) == replacement && f.hero->GetPlayer() == replacement
			&& replacement->GetHero() == f.hero.GetPtr() && !capturedPlayer->GetHero()
			&& f.Talent() == capturedTalent.GetPtr(),
			"B13 same UID now resolves to a different authoritative player while the exact hero and talent remain unchanged");
		CObj<NCore::WorldCommand> fresh = f.Command(unit);
		check(f.CanUse(unit) && fresh && fresh->CanExecute() && !original->CanExecute(),
			"B13 replacement player can issue a fresh command but cannot inherit the old captured order");
		if (phase == 0) original->Execute(f.map.world);
		ResetLinuxHeroGameplayCommandDiagnostics();
		for (int i = 0; i < 80; ++i) f.hero->Simulate(.125f);
		check(f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000 && capturedTalent->GetLastUseStep() == -1
			&& capturedTalent->GetCurrentCooldown() == 0 && GetLinuxHeroGameplayCommandDiagnostics().useTalentActionAccepted == 0,
			"B13 original command and queued state retain captured player identity across same-UID replacement");
		check(!f.hero->IsMoving() && !f.hero->HasQueuedStates(),
			"B13 captured-player replacement retires pending cast and approach without executing the fresh command");
		f.hero->DoStop();
	}
}

/** CanExecute validates captured endpoints; Execute must additionally validate the supplied world. */
static void TargetedTalentExecutionWorldB13(Checks& check)
{
	TargetedTalentFixtureB13 f(check), foreign(check); if (!f.map.world || !foreign.map.world) return;
	const Target unit(f.victim.GetPtr()), point(CVec2(8, 5)), invalid;
	check(!f.Command(point) && !f.Command(invalid), "B13 unit-only HUD factory rejects ground and invalid endpoints without a null dereference");
	CObj<NCore::WorldCommand> missing = CreateCmdUseLinuxTargetedTalent(nullptr, 0, 0, unit);
	check(!missing, "B13 HUD factory cannot construct a command from a missing actor");
	CObj<NCore::WorldCommand> command = f.Command(unit);
	check(command && command->CanExecute(), "B13 wrong-world fixture starts from a valid captured human command");
	if (!command) return;
	for (NCore::IWorldBase* world : {static_cast<NCore::IWorldBase*>(foreign.map.world), static_cast<NCore::IWorldBase*>(nullptr)})
	{
		CObj<IPFState> localState = f.hero->GetCurrentState(), foreignState = foreign.hero->GetCurrentState();
		command->Execute(world);
		check(command->CanExecute() && !f.hero->HasQueuedStates() && f.hero->GetCurrentState() == localState
			&& !foreign.hero->HasQueuedStates() && foreign.hero->GetCurrentState() == foreignState,
			"B13 wrong or missing execution world cannot enqueue or replace either world's hero state");
		f.hero->Simulate(.125f); foreign.hero->Simulate(.125f);
		check(f.hero->GetMana() == 200 && f.victim->GetHealth() == 1000 && f.Talent()->GetLastUseStep() == -1
			&& foreign.hero->GetMana() == 200 && foreign.victim->GetHealth() == 1000 && foreign.Talent()->GetLastUseStep() == -1,
			"B13 direct execution in another world cannot pay or substitute that world's matching-UID player");
	}
}

/** The existing ground-only HUD gate must admit authored units without weakening retained ground policy. */
static void TargetedTalentAdmissionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	TargetedTalentFixtureB13 f(check); if (!f.map.world) return;
	const Target unit(f.victim.GetPtr()), self(f.hero.GetPtr()), point(CVec2(8, 5));
	check(f.hero->GetPlayer() == f.map.world->GetPlayerByUID(f.ClientId) && f.victim->IsVisibleForFaction(f.hero->GetFaction()),
		"B13 targeted talent fixture owns the exact registered playing human and visible enemy");
	check(CanArmLinuxTargetedTalent(f.hero, 0, 0), "B13 authored unit-target talent can arm from the original HUD");
	check(CanUseLinuxTargetedTalent(f.hero, 0, 0, unit, f.ClientId), "B13 native HUD can select the exact live enemy unit");
	check(!CanUseLinuxTargetedTalent(f.hero, 0, 0, self, f.ClientId)
		&& !CanUseLinuxTargetedTalent(f.hero, 0, 0, point, f.ClientId), "B13 enemy unit target cannot silently become self or ground");
	check(!CanUseLinuxTargetedTalent(f.hero, 0, 0, unit, f.ClientId + 1), "B13 wrong client cannot borrow a live hero target");
	auto* raw = const_cast<NDb::Talent*>(f.db.GetPtr()); raw->targetType = NDb::SPELLTARGET_LAND;
	check(CanArmLinuxTargetedTalent(f.hero, 0, 0) && CanUseLinuxTargetedTalent(f.hero, 0, 0, point, f.ClientId),
		"B13 existing in-range ground talent admission remains supported");
	check(!CanUseLinuxTargetedTalent(f.hero, 0, 0, unit, f.ClientId), "B13 pure ground talent cannot acquire a unit endpoint");
	TargetedTalentOwnershipB13(check);
	TargetedTalentEndpointsB13(check);
	TargetedTalentReadinessB13(check);
	TargetedTalentRangeAndGroundB13(check);
	TargetedTalentCommandB13(check);
	TargetedTalentLateInvalidityB13(check);
	TargetedTalentPackedIdentityB13(check);
	TargetedTalentPlayerReplacementB13(check);
	TargetedTalentExecutionWorldB13(check);
	std::printf("B13 targeted talent admission checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
