/** Observe the original true-clone registry without substituting its creation or cleanup. */
struct HeroCloneActionB13 : PFBaseHero::CloneFunc
{
	std::vector<CObj<PFBaseHero>> visited;
	std::function<void(PFBaseHero*)> action;
	void operator()(PFBaseHero* unit) override { visited.emplace_back(unit); if (action) action(unit); }
};

/** Real source/caster heroes with independently observable identity and authored corpse timings. */
struct HeroCloneFixtureB13 : NaftaFixtureB13
{
	PFWorld* world;
	CObj<PFPlayer> sourcePlayer;
	std::vector<CObj<PFBaseHero>> created;
	explicit HeroCloneFixtureB13(PFWorld* world) : NaftaFixtureB13(world), world(world)
	{
		auto* death = new NDb::UnitDeathParameters; death->deathTime = 2; death->unsummonTime = .5f;
		death->observeOffset = .75f; death->fallOffset = 1;
		const_cast<NDb::BaseHero*>(receiver->GetDbHero())->deathParameters = death;
		auto* talents = new NDb::TalentsSet; talents->levels.resize(PFTalentsSet::LEVELS_COUNT);
		for (auto& level : talents->levels) level.talents.resize(PFTalentsSet::SLOTS_COUNT);
		const_cast<NDb::Hero*>(static_cast<const NDb::Hero*>(receiver->GetDbHero()))->defaultTalentsSets.push_back(talents);
		sourcePlayer = new PFPlayer(world, 81, NCore::ETeam::Team2, NCore::ETeam::Team2, 1981, NCore::ESex::Male, false, 0, false, false);
		receiver->AttachToPlayer(sourcePlayer.GetPtr()); receiver->SetSkin("CloneFixtureSkin"); receiver->SetNaftaLevel(3);
	}
	CObj<PFBaseHero> Clone(const CloneParams& params)
	{
		CObj<PFBaseHero> clone = receiver->Clone(params, 2, static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ENEMY), "CloneFixture");
		if (clone)
		{
			clone->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); clone->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
			clone->EnableAbilityAI(false); created.push_back(clone);
		}
		return clone;
	}
	~HeroCloneFixtureB13()
	{
		for (const auto& unit : created) if (!unit->IsDead() && !unit->IsObjectDead()) unit->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_UNSUMMON);
		receiver->DetachFromPlayer();
	}
};

/** Original factory distinguishes true clones and slave summons, copying only requested base/global stats. */
static void HeroCloneCreationB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool slave : {false, true}) for (bool dummy : {false, true}) for (bool copy : {false, true}) for (bool talents : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		HeroCloneFixtureB13 f(isolated.world);
		auto* strength = f.receiver->GetStat(NDb::STAT_STRENGTH); strength->SetCoreValue(37);
		strength->AddModifier(1, 5, PF_Core::WORLD_ID); strength->AddModifier(1, 7, 9181);
		CloneParams params; params.bSlave = slave; params.isDummy = dummy; params.copyStats = copy;
		params.initTalents = talents; params.pOwner = f.sender; params.followRange = 4; params.lifeTime = 1;
		params.targetingParams = f.receiver->DbUnitDesc()->targetingParams;
		auto clone = f.Clone(params); check(clone != nullptr, "B13 original clone factory creates a registered native hero instead of returning null"); if (!clone) continue;
		check(clone->IsClone() && !clone->IsTrueHero() && clone->GetWorld() == isolated.world && clone->GetDbHero() == f.receiver->GetDbHero()
			&& clone->GetSkinId() == f.receiver->GetSkinId() && clone->GetNaftaLevel() == 3,
			"B13 clone preserves original descriptor skin level and clone identity");
		check(clone->GetFaction() == (slave ? f.sender->GetFaction() : f.receiver->GetFaction())
			&& clone->GetMasterUnit() == (slave ? static_cast<PFBaseUnit*>(f.sender.GetPtr()) : static_cast<PFBaseUnit*>(f.receiver.GetPtr()))
			&& clone->GetOriginalFaction() == f.receiver->GetOriginalFaction(), "B13 slave faction/master and original-source faction remain distinct");
		check(clone->GetPlayerId() == (slave ? -1 : f.receiver->GetPlayerId()) && !IsValid(clone->GetPlayer())
			&& f.sourcePlayer->GetHero() == f.receiver, "B13 true clone copies source player id without taking over its player's hero link");
		check(clone->GetStatValue(NDb::STAT_STRENGTH) == (copy ? 42 : 25),
			"B13 copied base stats and WORLD_ID modifiers exclude unrelated providers");
		HeroCloneActionB13 clones; f.receiver->ForAllClones(clones);
		check(clones.visited.size() == (slave ? 0u : 1u)
			&& f.sender->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "CloneFixture") == int(slave && !dummy),
			"B13 true clones use the clone registry, slave AI uses the named primary group and dummy slaves use neither");
		const auto* male = dynamic_cast<const PFBaseMaleHero*>(clone.GetPtr());
		check(male && bool(male->GetTalentsSet()) == talents, "B13 clone honors the original optional talent initialization");
		isolated.world->Step(1, 1); check(!clone->IsDead(), "B13 created clone remains alive at its exact controlled duration");
		isolated.world->Step(.125f, .125f);
		check(clone->IsDead() && !clone->HasBehaviour(), "B13 real clone behavior expires through normal world stepping");
		HeroCloneActionB13 after; f.receiver->ForAllClones(after);
		check(after.visited.empty() && f.sender->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "CloneFixture") == 0,
			"B13 clone death releases exactly its owning clone registry or named summon group");
		isolated.world->Step(.5f, .5f);
		check(clone->IsObjectDead() && !isolated.world->FindLinuxUnitByObjectId(clone->GetObjectId()),
			"B13 clone corpse retires its world identity instead of respawning as a player hero");
	}
	std::printf("B13 hero clone creation checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** True clones follow the existing hero state and synchronize source levels, then unsummon after source death. */
static void HeroCloneFollowB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	HeroCloneFixtureB13 f(isolated.world); CloneParams params; params.followRange = 2;
	auto clone = f.Clone(params); check(clone != nullptr, "B13 following clone exists"); if (!clone) return;
	clone->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE); check(clone->TeleportTo(CVec2(20, 5)), "B13 following clone starts away from its source");
	const float distance = fabs(clone->GetPos().x - f.receiver->GetPos().x);
	for (int i = 0; i < 16; ++i) isolated.world->Step(.125f, .125f);
	check(fabs(clone->GetPos().x - f.receiver->GetPos().x) < distance && !clone->IsDead(),
		"B13 true clone follows its actual source through the existing hero follow FSM and movement engine");
	f.receiver->DoLevelups(1); check(clone->GetNaftaLevel() == f.receiver->GetNaftaLevel(), "B13 source level changes reach registered true clones");
	f.receiver->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS); isolated.world->Step(.125f, .125f);
	check(clone->IsDead() && !clone->HasBehaviour(), "B13 true clone unsummons on the next behavior tick after source death");
	HeroCloneActionB13 remaining; f.receiver->ForAllClones(remaining); check(remaining.visited.empty(), "B13 owner-death cleanup removes true-clone registry identity");
	std::printf("B13 hero clone following checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Callback-driven removal must not invalidate clone enumeration or make newly created clones part of that visit. */
static void HeroCloneEnumerationB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool createDuringVisit : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		HeroCloneFixtureB13 f(isolated.world); CloneParams params;
		for (int i = 0; i < 3; ++i) f.Clone(params);
		check(f.created.size() == 3, "B13 clone enumeration fixture creates three distinct registered clones"); if (f.created.size() != 3) continue;
		HeroCloneActionB13 action; action.action = [&](PFBaseHero* unit) {
			unit->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_UNSUMMON);
			if (createDuringVisit && action.visited.size() == 1) f.Clone(params);
		};
		f.receiver->ForAllClones(action);
		check(action.visited.size() == 3 && f.created[0]->IsDead() && f.created[1]->IsDead() && f.created[2]->IsDead(),
			"B13 true-clone callbacks can erase each visited entry without skipping original siblings");
		HeroCloneActionB13 after; f.receiver->ForAllClones(after);
		check(after.visited.size() == (createDuringVisit ? 1u : 0u), "B13 clone created during callbacks waits until a later enumeration");
	}
	std::printf("B13 hero clone enumeration checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Collect actual registered clone identities, keeping ownership until normal cleanup is observed. */
static std::vector<CObj<PFBaseHero>> HeroCloneUnitsB13(HeroCloneFixtureB13& fixture)
{
	fixture.world->Step(0, 0); std::vector<CObj<PFBaseHero>> live;
	auto collect = [&](PF_Core::WorldObjectBase& object) {
		if (auto* hero = dynamic_cast<PFBaseHero*>(&object)) if (hero->IsClone() && !hero->IsDead())
		{
			hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE); hero->EnableAbilityAI(false);
			live.emplace_back(hero); fixture.created.emplace_back(hero);
		}
	};
	fixture.world->ForAllObjectsBeingStepped(collect); return live;
}

/** Mock authored clone configuration only; factory, hero creation, state, effects and resource writes stay real. */
static NDb::Ptr<NDb::CloneHeroApplicator> HeroCloneDbB13(bool slave, bool dummy)
{
	auto* raw = new NDb::CloneHeroApplicator; raw->enabled.sString = "true";
	raw->lifeTime.sString = "sLife/400"; raw->followRange.sString = "sRange+cWrapperBonus";
	raw->takeModDmg.sString = "cWrapperBonus"; raw->maxCount.sString = "cWrapperBonus";
	raw->takeTypeUnit = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ALLY | NDb::SPELLTARGET_ENEMY);
	raw->spawnStats.life.sString = "sLife/2"; raw->spawnStats.energy.sString = "rMaxEnergy/4";
	raw->flags = static_cast<NDb::ECloneFlags>(NDb::CLONEFLAGS_COPYSTATS | (slave ? NDb::CLONEFLAGS_MAKESLAVE : 0)
		| (dummy ? NDb::CLONEFLAGS_DUMMYBEHAVIOUR : 0)); raw->summonGroupName = "CloneApplicator";
	return raw;
}

/** The actual descriptor factory reaches original clone setup, child effects and timed cleanup. */
static void HeroCloneApplicatorB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool slave : {false, true}) for (bool dummy : {false, true}) for (bool wrapped : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		HeroCloneFixtureB13 f(isolated.world); auto db = HeroCloneDbB13(slave, dummy);
		auto* child = WrapperFactoryFixtureB12::Stat("cWrapperBonus"); child->formulaName = "CloneChild";
		const_cast<NDb::CloneHeroApplicator*>(db.GetPtr())->applicators.push_back(child);
		NDb::Ptr<NDb::BaseApplicator> outer = db.GetPtr();
		if (wrapped) outer = new WrapperFactoryDbB12("/Mock/WrappedClone.xdb", outer);
		auto app = f.Create(outer); check(app && f.Activate(app), "B13 real CloneHeroApplicator factory and metadata wrappers admit native clone creation"); if (!app) continue;
		const auto units = HeroCloneUnitsB13(f); check(units.size() == 1, "B13 actual clone applicator produces one real hero clone"); if (units.size() != 1) continue;
		const auto& clone = units[0];
		check(clone->GetUnitType() == (slave ? NDb::UNITTYPE_SUMMON : dummy ? NDb::UNITTYPE_DUMMYUNIT : NDb::UNITTYPE_HEROMALE)
			&& clone->GetLife() == 200 && clone->GetEnergy() == clone->GetMaxEnergy()/4,
			"B13 clone applicator preserves original type precedence and checked fresh-candidate spawn resources");
		check(clone->GetStatValue(NDb::STAT_STRENGTH) == 22 && ApplicatorHistoryNamedCountB13(clone) == 1
			&& f.instance->GetActiveApplicatorsCount() == 1, "B13 clone child effects retain the actual spawning cast after its instant parent retires");
		auto damage = DirectDamage(); damage.pSender = f.sender; damage.amount = 10;
		const float beforeHit = clone->GetLife(); clone->OnDamage(damage);
		check(clone->GetLife() == beforeHit-20, "B13 clone applicator's authored multiplier reaches original hero damage handling");
		isolated.world->Step(1, 1); check(!clone->IsDead(), "B13 clone applicator duration is honored at its exact boundary");
		isolated.world->Step(.125f, .125f); check(clone->IsDead(), "B13 dynamic clone lifetime expires after its strict deadline");
		isolated.world->Step(.5f, .5f);
		check(clone->IsObjectDead() && f.instance->GetActiveApplicatorsCount() == 0
			&& ApplicatorHistoryNamedCountB13(clone) == 0, "B13 clone death retires its actual child effects, cast ownership and corpse identity");
	}
	std::printf("B13 hero clone applicator checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Unsupported scalar reads cannot become zero/default clones; the non-slave limit stays unevaluated. */
static void HeroCloneAdmissionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool slave : {false, true}) for (int field = 0; field < 6; ++field)
	for (const char* text : {"cMissing", "1/0", "1e39f", "random(2,4)+cMissing", "compiled-only"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		HeroCloneFixtureB13 f(isolated.world); auto db = HeroCloneDbB13(slave, false);
		auto* raw = const_cast<NDb::CloneHeroApplicator*>(db.GetPtr());
		if (field == 2)
		{
			raw->maxCount.sString = text; raw->maxCount.compiledString.clear();
			if (std::strcmp(text, "compiled-only") == 0) { raw->maxCount.sString.clear(); raw->maxCount.compiledString = "unsupported"; }
		}
		else SummonSetupSourceB13(field == 0 ? raw->lifeTime : field == 1 ? raw->followRange
			: field == 3 ? raw->takeModDmg : field == 4 ? raw->spawnStats.life : raw->spawnStats.energy, text);
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Create(db);
		check(app && f.Activate(app), "B13 malformed clone data reaches its actual descriptor factory"); if (!app) continue;
		const auto units = HeroCloneUnitsB13(f);
		check(units.size() == (!slave && field == 2 ? 1u : 0u) && GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 failed clone scalar does not create a live fallback creature or spend its rejected random draw; unused limits stay lazy");
		check(!app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0, "B13 rejected clone admission retires instant cast ownership");
	}
	std::printf("B13 hero clone admission checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Optional clone talent initialization retains the existing global fallback when the hero has no predefined entry. */
static void HeroCloneTalentFallbackB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool global : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		HeroCloneFixtureB13 f(isolated.world);
		const_cast<NDb::Hero*>(static_cast<const NDb::Hero*>(f.receiver->GetDbHero()))->defaultTalentsSets.clear();
		auto& ai = const_cast<NDb::AILogicParameters&>(isolated.world->GetAIWorld()->GetAIParameters());
		const auto saved = ai.defaultTalentsSet;
		auto* fallback = new NDb::TalentsSet; auto* talent = new NDb::Talent; talent->minLevel = 0;
		fallback->levels[0].talents[0].talent = talent; fallback->levels[0].talents[0].status = NDb::TALENTSLOTSTATUS_NORMAL;
		NDb::Ptr<NDb::TalentsSet> retained = fallback; ai.defaultTalentsSet = global ? retained : nullptr;
		CloneParams params; params.initTalents = true; auto clone = f.Clone(params);
		check(clone != nullptr, "B13 empty hero talent-list clone reaches the existing global-or-empty fallback without indexing element zero");
		const auto* male = dynamic_cast<const PFBaseMaleHero*>(clone.GetPtr());
		check(male && male->GetTalentsSet() && bool(male->GetTalent(0, 0)) == global,
			"B13 optional clone talent initialization uses the real global set or an empty set when neither is authored");
		if (male && global && male->GetTalent(0, 0)) check(male->GetTalent(0, 0)->GetDBDesc() == talent,
			"B13 global fallback keeps the original authored talent identity");
		ai.defaultTalentsSet = saved;
	}
	std::printf("B13 hero clone talent fallback checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Direct callers cannot introduce nonfinite clone state or foreign slave ownership; repeated stop is harmless. */
static void HeroCloneGuardsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check), foreign(check); if (!isolated.world || !foreign.world) return;
	HeroCloneFixtureB13 f(isolated.world); auto other = B9MaleHero(foreign.world, NDb::FACTION_FREEZE);
	for (int field = 0; field < 3; ++field)
	for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		CloneParams params; if (field == 0) params.lifeTime = value; if (field == 1) params.followRange = value;
		CObj<PFBaseHero> unit = f.receiver->Clone(params, field == 2 ? value : 1.f, NDb::ESpellTarget(0), "InvalidClone");
		if (unit) f.created.push_back(unit);
		check(!unit, "B13 direct clone factory rejects nonfinite duration follow range or damage before creating a hero");
	}
	for (PFBaseHero* master : {static_cast<PFBaseHero*>(nullptr), static_cast<PFBaseHero*>(other.GetPtr())})
	{
		CloneParams params; params.bSlave = true; params.pOwner = master;
		check(!f.Clone(params), "B13 slave clone rejects missing or foreign-world master before creating cross-world links");
	}
	CloneParams params; auto unit = f.Clone(params); check(unit != nullptr, "B13 repeated clone-stop fixture uses a real cloned hero");
	if (unit)
	{
		CObj<PFBaseBehaviour> behaviour = const_cast<PFBaseBehaviour*>(unit->GetBehaviour());
		behaviour->OnStop(); behaviour->OnStop(); HeroCloneActionB13 remaining; f.receiver->ForAllClones(remaining);
		check(remaining.visited.empty(), "B13 clone stop clears ownership before any repeated callback can remove its registry entry again");
		isolated.world->Step(.125f, .125f);
		check(unit->IsDead() && !unit->HasBehaviour(), "B13 stopped clone with no behavior master unsummons on the next tick");
	}
	std::printf("B13 hero clone guards checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Load and execute the unchanged FireFox clone leaf and all its children using original ability/global constants. */
static void HeroCloneAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::CloneHeroApplicator>(NDb::DBID("/Items/Talents/Class/FireFox/A3Clone.APLR.xdb"));
		const auto talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/FireFox/Ability3.TALENT.xdb"));
		const auto upgrade = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/FireFox/Ability3_Upg1.TALENT.xdb"));
		const auto rules = NDb::Get<NDb::AILogicParameters>(NDb::DBID("/GameLogic/AILogic.AILP.xdb"));
		check(db && talent && upgrade && rules && db->applicators.size() == 5 && db->lifeTime.sString == "cIllusionDuration",
			"B13 original FireFox clone leaf and its five children load unchanged with ability/global context");
		auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
		const auto saved = ai->globalConstants; if (rules) ai->globalConstants = rules->globalConstants;
		if (db && talent && upgrade && rules) for (bool program : {false, true}) for (bool cloneOn : {false, true})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			HeroCloneFixtureB13 f(isolated.world); f.abilityDb = talent.GetPtr();
			f.sender->Cleanup(); auto caster = CommandHeroB10(isolated.world); f.sender = caster;
			caster->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); caster->SetHealth(400);
			caster->InstallTalent(upgrade, false);
			if (program)
			{
				caster->AddGold(caster->GetTalent(0, 0)->GetNaftaCost());
				check(caster->GetTalent(0, 0)->Activate() && caster->GetGold() == 0,
					"B13 actual paid FireFox upgrade talent enables its original clone program");
			}
			f.ability = new PFAbilityData(f.sender.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
			f.instance = new PFAbilityInstance(f.ability, Target(f.receiver.GetPtr()), true);
			f.sender->GetVariableVWM("FireFoxCloneOn")->SetBaseValue(cloneOn ? 1 : 0);
			f.sender->ChangeMana(f.sender->GetMaxEnergy()/4 - f.sender->GetEnergy());
			check(f.sender->TeleportTo(CVec2(12, 5)), "B13 original clone leaf has a distinct actual teleport destination");
			auto app = f.Create(db); check(app && f.Activate(app), "B13 unchanged FireFox clone leaf activates through the actual native factory");
			const auto units = HeroCloneUnitsB13(f); check(units.size() == 1, "B13 original FireFox leaf creates its real hero clone"); if (units.size() != 1) continue;
			const auto& clone = units[0];
			check(clone->GetLife() == 400 && clone->GetEnergy() == clone->GetMaxEnergy()/4 && clone->GetVariable("IsClone") == 1,
				"B13 unchanged FireFox resource fractions and permanent clone-variable child execute on the fresh clone");
			check(fabs(clone->GetPos().x-f.sender->GetPos().x) < .01f && fabs(clone->GetPos().y-f.sender->GetPos().y) < .01f,
				"B13 original teleport child places the clone at its actual ability owner");
			const auto* sequence = dynamic_cast<const NDb::ProgramApplicator*>(db->applicators[4].GetPtr());
			int programs = 0, waits = 0, enabledWaits = 0;
			auto observe = [&](const CObj<PFBaseApplicator>& child) {
				if (child->IsAlive() && child->GetDBBase()->GetObjectTypeID() == NDb::ProgramApplicator::typeId) ++programs;
				if (child->IsAlive() && sequence && child->GetDBBase() == sequence->applicators[0].applicator.GetPtr())
				{
					++waits; enabledWaits += int(child->IsEnabled());
				}
			};
			clone->ForAllAppliedApplicators(observe);
			check(programs == int(program) && waits == int(program) && enabledWaits == int(program && cloneOn),
				"B13 original upgrade talent gates the program; FireFoxCloneOn controls its first child's enabled state without shortening its lifetime");
			for (int i = 0; i < 32; ++i) isolated.world->Step(.125f, .125f);
			check(!clone->IsDead(), "B13 original FireFox clone remains alive at its authored four-second deadline");
			isolated.world->Step(.125f, .125f); check(clone->IsDead() && !clone->HasBehaviour(), "B13 original FireFox clone expires despite isolated/control flags");
			isolated.world->Step(.5f, .5f); HeroCloneActionB13 remaining; f.receiver->ForAllClones(remaining);
			check(clone->IsObjectDead() && remaining.visited.empty() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 original FireFox clone retires child effects, registry, cast and corpse through real lifecycle cleanup");
		}
		ai->globalConstants = saved;
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored hero clone checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Positive slave limits replace real clone identities; bad integer/range requests cannot kill an existing member. */
static void HeroCloneLimitsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (const char* expression : {"1", "2.9", "cWrapperBonus"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		HeroCloneFixtureB13 f(isolated.world); auto db = HeroCloneDbB13(true, false);
		auto* raw = const_cast<NDb::CloneHeroApplicator*>(db.GetPtr()); raw->lifeTime.sString = "0"; raw->maxCount.sString = expression;
		const int limit = std::strcmp(expression, "1") == 0 ? 1 : 2;
		for (int cast = 1; cast <= 4; ++cast)
		{
			auto app = f.Create(db); check(app && f.Activate(app), "B13 clone limit recast uses its actual applicator and owner group");
			const auto units = HeroCloneUnitsB13(f);
			check(int(units.size()) == std::min(cast, limit) && f.sender->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "CloneApplicator") == std::min(cast, limit)
				&& f.sender->GetMaxAllowedSummons(NDb::SUMMONTYPE_PRIMARY, "CloneApplicator") == limit,
				"B13 positive authored clone limits preserve numeric truncation and actual replacement count");
		}
		const auto old = HeroCloneUnitsB13(f);
		for (const char* bad : {"cMissing", "2147483648", "(-2147483648)", "random(2,4)+2147483904"})
		{
			raw->maxCount.sString = bad; const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Create(db);
			check(app && f.Activate(app), "B13 malformed clone capacity reaches checked integer admission");
			const auto current = HeroCloneUnitsB13(f); bool same = current.size() == old.size();
			for (const auto& unit : old) same = same && !unit->IsDead();
			check(same && f.sender->GetMaxAllowedSummons(NDb::SUMMONTYPE_PRIMARY, "CloneApplicator") == limit
				&& GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
				"B13 bad clone capacity or widened removal arithmetic rejects before existing clones, advertised limits or rejected RNG draws change");
		}
	}
	std::printf("B13 hero clone limits checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
