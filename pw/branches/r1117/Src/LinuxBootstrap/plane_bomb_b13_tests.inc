#include "../PF_GameLogic/PFApplMove.h"

/** Ability-on formulas read the original miscellaneous context on every evaluation. */
static void BombAbilityStateB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	NaftaFixtureB13 f(isolated.world);
	NDb::Ptr<NDb::BaseApplicator> db = WrapperFactoryFixtureB12::Stat(); auto app = f.Create(db);
	check(app != nullptr, "B13 bomb state fixture has a real applicator and registered ability");
	if (!app) return;
	for (bool on : {false, true, false})
	{
		if (on) f.ability->SwitchOn(); else f.ability->SwitchOff();
		for (const char* expression : {"mIsAbilityOn", "pMisc->IsAbilityOn()", "appl.IsAbilityOn()"})
		{
			const auto unit = LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver, f.ability);
			const auto effect = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app);
			check(unit.Succeeded() && unit.value == on && effect.Succeeded() && effect.value == on,
				"B13 ability-on aliases follow live toggle state in ability and applicator contexts");
		}
	}
	const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula("true ? 7 : mIsAbilityOn", nullptr, nullptr);
	check(lazy.Succeeded() && lazy.value == 7
		&& !LinuxBootstrap::EvaluateUnitNumericFormula("mIsAbilityOn", nullptr, nullptr).Succeeded(),
		"B13 selected ability-on needs actual misc context while unselected access stays lazy");
	for (const char* invalid : {"pMisc->IsAbilityOn(1)", "appl.IsAbilityOn(0)", "mIsAbilityOn()",
		"pMisc->GetObjectParent()->IsAbilityOn()", "pMisc->SwitchOn()", "appl.IsOn()"})
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("true ? 7 : ") + invalid, *app).Succeeded(),
			"B13 state binding rejects malformed getters mutation and unapproved parent traversal even in lazy branches");
	NRandom::RandomGenerator expected(*isolated.world->GetRndGen());
	const auto bad = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+mIsAbilityOn+cMissing", *app);
	check(!bad.Succeeded() && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 failed ability-state expression does not commit its preceding random draw");
	const int draw = expected.Next(1, 9); f.ability->SwitchOn();
	const auto good = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+mIsAbilityOn", *app);
	check(good.Succeeded() && good.value == draw + 1 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 successful ability-state expression commits the original seeded draw exactly once");
	std::printf("B13 bomb state checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Permanent world retirement must release effects that may survive an ordinary combat death. */
static void BombRetirementB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool killed : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		ApplicatorHistoryFixtureB13 f(check, isolated.world);
		auto creepDb = DeadCreatureDbB13(isolated.world);
		CObj<DeadCreatureB13<B8BehaviourCreep>> creep = new DeadCreatureB13<B8BehaviourCreep>(isolated.world, *creepDb, NDb::FACTION_FREEZE);
		f.receiver->Cleanup(); f.receiver = creep.GetPtr();
		const int sentCast = DeathCleanupCastB13(f);
		auto received = f.Apply(DeathCleanupStatB13("RetireIn", NDb::BUFFBEHAVIOR_DONTSTOPONDEATH));
		auto sent = f.Apply(DeathCleanupStatB13("RetireOut", NDb::BUFFBEHAVIOR_DONTSTOPONSENDERDEATH
			| NDb::BUFFBEHAVIOR_ENABLEDONSENDERDEATH), sentCast, nullptr, f.elsewhere);
		auto unrelated = f.Apply(f.Stat("RetireUnrelated"), 2, nullptr, f.other);
		f.receiver->GetVariableVWM("RetireVariable")->SetBaseValue(7);
		if (killed)
		{
			f.receiver->KillUnit(f.other.GetPtr(), PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
			isolated.world->Step(.1f, .1f);
		}
		check(received->IsAlive() && sent->IsAlive(), "B13 retirement fixture preserves effects until permanent removal");
		creep->RemoveCorpse();
		check(f.receiver->IsObjectDead() && !received->IsAlive() && !sent->IsAlive(),
			"B13 permanent removal stops both received and sent effects despite ordinary death survival flags");
		check(f.casts[0]->GetActiveApplicatorsCount() == 0 && f.casts[sentCast]->GetActiveApplicatorsCount() == 0,
			"B13 retirement releases every participating cast's active ownership");
		check(ApplicatorHistoryNamedCountB13(f.receiver) == 0 && f.receiver->GetVariable("RetireVariable") == 0,
			"B13 permanent removal clears weak effect names and unit variables after disabling effects");
		check(unrelated->IsAlive() && f.other->GetStatValue(NDb::STAT_STRENGTH) == 43,
			"B13 retirement leaves unrelated living effects intact");
		creep->RemoveCorpse();
		check(f.casts[0]->GetActiveApplicatorsCount() == 0 && f.casts[sentCast]->GetActiveApplicatorsCount() == 0 && unrelated->IsAlive(),
			"B13 repeated permanent removal cannot decrement another cast's effects");
	}
	std::printf("B13 bomb retirement checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** The original source 0,1 returns one second; later casts refresh the existing Fly instance. */
static void BombFlightRefreshB13(Checks& check, const NDb::FlyApplicator* db)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world || !db) return;
	NaftaFixtureB13 f(isolated.world);
	check(f.receiver->TeleportTo(CVec2(15.25f, 15.25f)), "B13 flight fixture uses an unrounded in-map point");
	NDb::Ptr<NDb::BaseApplicator> contextDb = WrapperFactoryFixtureB12::Stat(); auto context = f.Create(contextDb);
	check(context != nullptr, "B13 flight formula fixture uses the real transactional applicator random context");
	if (!context) return;
	f.ability->SwitchOn();
	NRandom::RandomGenerator expected(*isolated.world->GetRndGen());
	expected.Next(10, 20); const int second = expected.Next(30, 40);
	const auto sequenced = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(10,20),random(30,40)+mIsAbilityOn", *context);
	check(sequenced.Succeeded() && sequenced.value == second + 1 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 comma expressions preserve original seeded draw order and live ability context");
	for (const char* bad : {"random(10,20),cMissing", "random(10,20),1/0", "random(10,20),1e39f"})
	{
		expected = *isolated.world->GetRndGen();
		const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(bad, *context);
		check(!result.Succeeded() && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 failed comma evaluation rolls back preceding staged draws");
	}
	const auto lazy = LinuxBootstrap::EvaluateApplicatorNumericFormula("false ? random(10,20),cMissing : 7", *context);
	check(lazy.Succeeded() && lazy.value == 7 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 unselected comma sequences require no constants and consume no game random draws");
	expected.Next(10, 20);
	const auto discarded = LinuxBootstrap::EvaluateApplicatorNumericFormula("true ? random(10,20),5 : cMissing", *context);
	check(discarded.Succeeded() && discarded.value == 5 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 selected discarded comma operands still commit their real random draw");
	/** Invalid admission and refresh both leave the old duration and movement state untouched. */
	auto rejectInvalid = [&](bool flying) {
		for (const char* expression : {"cMissing", "1/0", "1e39f", "0,", "random(1,9),cMissing", "compiled-only"})
		{
			auto* raw = new NDb::FlyApplicator; raw->Assign(*db);
			raw->lifeTime.sString = expression; raw->lifeTime.compiledString.clear();
			if (std::strcmp(expression, "compiled-only") == 0) { raw->lifeTime.sString.clear(); raw->lifeTime.compiledString = "unsupported"; }
			NDb::Ptr<NDb::FlyApplicator> invalid = raw;
			const NRandom::RandomGenerator savedRng(*isolated.world->GetRndGen()); const CVec2 position = f.receiver->GetPos();
			auto rejected = f.Create(invalid); check(rejected && f.Activate(rejected), "B13 invalid Fly duration reaches real admission");
			check(rejected && !rejected->IsAlive() && f.instance->GetActiveApplicatorsCount() == int(flying)
				&& f.receiver->GetGhostMode() == (flying ? 5 : 0) && f.receiver->GetPos() == position
				&& GlyphRngEqualB13(*isolated.world->GetRndGen(), savedRng),
				"B13 invalid flight duration cannot create or extend flight, move the receiver, or consume a staged random draw");
		}
	};
	rejectInvalid(false);
	auto first = f.Create(db); check(first && f.Activate(first), "B13 original Fly factory activates its actual effect");
	if (!first) return;
	const auto duration = LinuxBootstrap::EvaluateApplicatorNumericFormula(db->lifeTime.sString.c_str(), *first);
	check(duration.Succeeded() && duration.value == 1, "B13 original 0,1 flight duration has C++ comma semantics, not a decimal or zero fallback");
	isolated.world->Step(.25f, .25f);
	check(first->IsAlive() && f.receiver->GetGhostMode() == 5 && f.receiver->GetPos() == CVec2(15.25f, 15.25f),
		"B13 original flight remains alive without premature landing correction");
	auto refresh = f.Create(db); check(refresh && f.Activate(refresh), "B13 original second Fly reaches the real refresh policy");
	check(first->IsAlive() && refresh && !refresh->IsAlive() && f.instance->GetActiveApplicatorsCount() == 1,
		"B13 repeated Fly extends the original instance instead of replacing or stacking it");
	isolated.world->Step(.75f, .75f);
	check(first->IsAlive() && f.receiver->GetGhostMode() == 5, "B13 refreshed Fly outlives its original one-second deadline");
	rejectInvalid(true);
	isolated.world->Step(.25f, .25f);
	check(!first->IsAlive() && f.receiver->GetGhostMode() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
		"B13 refreshed Fly retires exactly one second after refresh and releases its ghost token");
	for (const char* expression : {"", "0", "-1", "random(2,4)"})
	{
		NaftaFixtureB13 timed(isolated.world); auto* raw = new NDb::FlyApplicator; raw->Assign(*db);
		raw->lifeTime.sString = expression; raw->lifeTime.compiledString.clear();
		NDb::Ptr<NDb::FlyApplicator> timing = raw;
		const bool randomDuration = std::strcmp(expression, "random(2,4)") == 0;
		NRandom::RandomGenerator draws(*isolated.world->GetRndGen());
		if (randomDuration) { draws.Next(2, 4); draws.Next(2, 4); }
		auto effect = timed.Create(timing); check(effect && timed.Activate(effect), "B13 Fly preserves authored empty zero negative and random durations");
		check(effect && effect->IsAlive() && timed.receiver->GetGhostMode() == 5 && GlyphRngEqualB13(*isolated.world->GetRndGen(), draws),
			"B13 new flight preserves both original duration evaluation sites before stepping");
		float lifetime = 0;
		if (randomDuration)
		{
			lifetime = draws.Next(2, 4); auto again = timed.Create(timing);
			check(again && timed.Activate(again) && !again->IsAlive() && effect->IsAlive()
				&& GlyphRngEqualB13(*isolated.world->GetRndGen(), draws),
				"B13 refreshing flight preserves the single original duration draw");
			isolated.world->Step(lifetime - .25f, lifetime - .25f);
			check(effect->IsAlive(), "B13 seeded refresh remains active before its actual drawn deadline");
		}
		isolated.world->Step(.25f, .25f);
		check(effect && !effect->IsAlive() && timed.receiver->GetGhostMode() == 0 && timed.instance->GetActiveApplicatorsCount() == 0,
			"B13 zero negative or elapsed random duration retires on the original step boundary");
	}
	std::printf("B13 bomb flight refresh checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Exercise the unmodified Plane A4 graph, not a replacement bomb implementation. */
static void PlaneBombAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A4.TALENT.xdb"));
		const auto upgrade = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A4u.TALENT.xdb"));
		const auto rules = NDb::Get<NDb::AILogicParameters>(NDb::DBID("/GameLogic/AILogic.AILP.xdb"));
		const auto flightDb = NDb::Get<NDb::FlyApplicator>(NDb::DBID("/Items/Talents/Class/Plane/appl_A4_Fly.APLR.xdb"));
		check(flightDb && flightDb->lifeTime.sString == "0,1", "B13 original comma-duration flight descriptor loads unchanged");
		if (flightDb) BombFlightRefreshB13(check, flightDb);
		check(db && upgrade && rules && db->persistentId == "Plane_A4" && db->type == NDb::ABILITYTYPE_MULTIACTIVE,
			"B13 original A4 bomb and upgrade load unchanged with their MultiActive lifecycle");
		auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
		const auto savedConstants = ai->globalConstants; const auto savedScaling = ai->abilityAndDamageScalingParams;
		if (rules) { ai->globalConstants = rules->globalConstants; ai->abilityAndDamageScalingParams = rules->abilityAndDamageScalingParams; }
		if (db && upgrade && rules) for (bool burn : {false, true}) for (bool upgraded : {false, true}) for (bool early : {false, true})
		{
			RoutingFixtureB13 isolated(check); if (!isolated.world) continue;
			auto hero = CommandHeroB10(isolated.world); hero->ChangeFaction(burn ? NDb::FACTION_BURN : NDb::FACTION_FREEZE);
			auto center = B9MaleHero(isolated.world, hero->GetOppositeFaction()), outer = B9MaleHero(isolated.world, hero->GetOppositeFaction());
			auto outside = B9MaleHero(isolated.world, hero->GetOppositeFaction()), ally = B9MaleHero(isolated.world, hero->GetFaction());
			PFBaseUnit* units[] = {hero.GetPtr(), center.GetPtr(), outer.GetPtr(), outside.GetPtr(), ally.GetPtr()};
			for (auto* unit : units)
			{
				unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(10000); unit->SetHealth(10000);
				for (auto stat : {NDb::STAT_LIFEREGENERATION, NDb::STAT_LIFEREGENERATIONABSOLUTE,
					NDb::STAT_ENERGYREGENERATION, NDb::STAT_ENERGYREGENERATIONABSOLUTE, NDb::STAT_WILL, NDb::STAT_STAMINA})
					unit->GetStat(stat)->SetCoreValue(0);
			}
			hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(burn ? 50 : 160);
			hero->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(burn ? 160 : 50);
			hero->InstallTalent(db, false);
			PFTalentsSet::SetInfo info; info[0][0].pTalentDesc = db; info[0][0].refineRate = 1;
			if (upgraded) { info[0][1].pTalentDesc = upgrade; info[0][1].refineRate = 1; }
			hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info); hero->AddGold(2000);
			for (int slot = 0; slot <= int(upgraded); ++slot)
			{
				CObj<NCore::WorldCommand> buy = CreateCmdActivateTalent(hero.GetPtr(), 0, slot);
				check(buy && buy->CanExecute(), "B13 original bomb purchase uses real command admission");
				if (buy) buy->Execute(isolated.world);
				check(hero->GetTalent(0, slot)->IsActivated(), "B13 original bomb purchase activates its talent");
			}
			check(hero->TeleportTo(CVec2(50, 60)), "B13 bomb caster separates from the descriptor seed hero");
			check(center->TeleportTo(CVec2(53, 60.5f)) && outer->TeleportTo(CVec2(56, 60.5f))
				&& outside->TeleportTo(CVec2(65, 60.5f)) && ally->TeleportTo(CVec2(50.5f, 63)),
				"B13 bomb recipients occupy inner outer excluded and allied positions through real movement registration");
			CObj<PFTalent> talent = hero->GetTalent(0, 0); const float mana = hero->GetMana();
			check(hero->GetGold() == 2000 - db->naftaCost - (upgraded ? upgrade->naftaCost : 0),
				"B13 original bomb and optional upgrade pay their exact authored prime costs");
			const auto innerDamage = LinuxBootstrap::EvaluateUnitNumericFormula("cDamageCenter", hero, center, talent);
			const auto outerDamage = LinuxBootstrap::EvaluateUnitNumericFormula("cDamageNotCenter", hero, outer, talent);
			check(innerDamage.Succeeded() && innerDamage.value == (burn ? 818 : 184)
				&& outerDamage.Succeeded() && outerDamage.value == (burn ? 471 : 106),
				"B13 bomb uses original intellect scaling endpoints for independent center and upgraded outer damage");
			CObj<PFAbilityInstance> cast = hero->UseTalent(talent, Target(hero.GetPtr()));
			check(cast && talent->IsOn() && hero->GetMana() == mana - 200 && talent->GetCurrentCooldown() == 0,
				"B13 original bomb activation pays once and postpones cooldown until switch-off");
			isolated.world->Step(.1f, .1f);
			check(hero->GetVariable("IsFlying") == 1 && hero->GetGhostMode() == 7,
				"B13 original bomb flight status applies its live variable and ghost movement while on");
			bool countdown = false; int offStep = -1, spawnStep = -1, hitStep = -1, removedStep = -1;
			CObj<PFBaseUnit> bomb; CVec2 bombPosition(0, 0);
			CObj<PFBaseApplicator> firstFlight; bool continuousFlight = true; int maxFlights = 0;
			for (int step = 0; step < 150; ++step)
			{
				if (early && step == 5)
				{
					check(hero->CanUseTalent(talent), "B13 active bomb permits its own manual release despite flight restrictions and spent mana");
					hero->UseTalent(talent, Target(hero.GetPtr()));
					check(!talent->IsOn() && hero->GetMana() == mana - 200,
						"B13 manual release switches off without charging another cast");
				}
				isolated.world->Step(.1f, .1f);
				if (hero->FindApplicator("PlaneA4", talent, GLOBAL) != talent.GetPtr()) countdown = true;
				if (!talent->IsOn() && offStep < 0) offStep = step;
				CObj<PFBaseApplicator> flight; int flightCount = 0;
				auto observeFlight = [&](const CObj<PFBaseApplicator>& app) {
					if (dynamic_cast<PFApplFly*>(app.GetPtr()) && app->IsAlive()) { flight = app; ++flightCount; }
				};
				hero->ForAllAppliedApplicators(observeFlight); maxFlights = Max(maxFlights, flightCount);
				if (talent->IsOn())
				{
					if (!firstFlight && flight) firstFlight = flight;
					if (!flight || flight != firstFlight) continuousFlight = false;
				}
				if (!bomb)
				{
					auto observe = [&](PF_Core::WorldObjectBase& object) {
						auto* unit = dynamic_cast<PFBaseUnit*>(&object);
						if (unit && unit->GetUnitType() == NDb::UNITTYPE_DUMMYUNIT && unit->GetMasterUnit() == hero.GetPtr()) bomb = unit;
					};
					isolated.world->ForAllObjectsBeingStepped(observe);
					if (bomb) { spawnStep = step; bombPosition = bomb->GetPos(); }
				}
				if (center->GetHealth() < 10000 && hitStep < 0) hitStep = step;
				if (bomb && bomb->IsObjectDead() && removedStep < 0) removedStep = step;
			}
			std::printf("B13 bomb original burn=%d upgraded=%d early=%d off=%d spawn=%d hit=%d removed=%d damage=%g/%g bomb=%g,%g active=%d\n",
				burn, upgraded, early, offStep, spawnStep, hitStep, removedStep, 10000 - center->GetHealth(), 10000 - outer->GetHealth(),
				bombPosition.x, bombPosition.y, cast ? cast->GetActiveApplicatorsCount() : -1);
			check(countdown && (early ? offStep == 5 && spawnStep >= 5 && spawnStep <= 7 : offStep >= 20 && offStep <= 30 && spawnStep >= 16 && spawnStep <= 19),
				"B13 authored countdown or manual switch-off releases the bomb at the original deadline");
			check(firstFlight && continuousFlight && maxFlights == 1 && !firstFlight->IsAlive(),
				"B13 original A4 periodic flight refreshes one continuous Fly identity until release, without per-step landing and recreation");
			check(bomb && hitStep - spawnStep >= 3 && hitStep - spawnStep <= 5 && removedStep > hitStep
				&& removedStep - hitStep >= 14 && removedStep - hitStep <= 18,
				"B13 real bomb waits for its damage timer then removes its corpse after the authored effect delay");
			check(innerDamage.Succeeded() && outerDamage.Succeeded()
				&& 10000 - center->GetHealth() == innerDamage.value + (upgraded ? outerDamage.value : 0)
				&& 10000 - outer->GetHealth() == (upgraded ? outerDamage.value : 0),
				"B13 actual original bomb applies center damage once and purchased outer damage once including the center");
			check(ally->GetHealth() == 10000 && outside->GetHealth() == 10000 && hero->GetHealth() == 10000,
				"B13 original bomb targeting excludes allies owner and recipients outside both radii");
			check(hero->GetVariable("IsFlying") == 0 && hero->GetGhostMode() == 0 && hero->GetMana() == mana - 200,
				"B13 original bomb retires flight without repeated payment");
			check(cast && cast->IsFinished() && cast->GetActiveApplicatorsCount() == 0 && !talent->IsOn()
				&& talent->GetCurrentCooldown() > 70 && talent->GetCurrentCooldown() < 80,
				"B13 original bomb finishes its graph and advances the original 90-second cooldown");
			check(bomb && bomb->IsObjectDead() && !isolated.world->FindLinuxUnitByObjectId(bomb->GetObjectId()),
				"B13 original bomb removal releases its registered world identity");
			for (int step = 0; step < 900; ++step) isolated.world->Step(.1f, .1f);
			check(talent->IsReady() && hero->GetMana() == mana - 200 && cast && cast->GetActiveApplicatorsCount() == 0,
				"B13 original bomb becomes ready naturally without recreating effects or paying again");
			for (auto* unit : units) unit->Cleanup();
		}
		ai->globalConstants = savedConstants; ai->abilityAndDamageScalingParams = savedScaling;
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 bomb authored checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
