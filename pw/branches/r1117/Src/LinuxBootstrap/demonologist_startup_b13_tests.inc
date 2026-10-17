namespace demonologist_startup_b13
{
/** Observe registered summons without changing their AI, placement, variables or lifetime. */
static std::vector<CObj<PFBaseSummonedUnit>> Summons(PFWorld* world)
{
	std::vector<CObj<PFBaseSummonedUnit>> result;
	auto collect = [&](PF_Core::WorldObjectBase& object) {
		if (auto* summon = dynamic_cast<PFBaseSummonedUnit*>(&object))
			if (!summon->IsDead() && !summon->IsObjectDead()
				&& world->FindLinuxUnitByObjectId(summon->GetObjectId()) == summon)
				result.emplace_back(summon);
	};
	world->ForAllObjectsBeingStepped(collect);
	return result;
}

/** Use the production factory: InitHero/ActivateTakenOnStart precede AttachToPlayer. */
struct Fixture
{
	B8BehaviourForeignWorld map;
	CObj<PFBaseMaleHero> hero;
	static NCore::TPlayersStartInfo Players()
	{
		NCore::TPlayersStartInfo players;
		players.push_back(NCore::PlayerStartInfo(0, NCore::ETeam::Team1, NCore::ETeam::Team1,
			NCore::EPlayerType::Human, 1939, NCore::ESex::Male));
		return players;
	}
	explicit Fixture(Checks& check, const NDb::Hero* descriptor) : map(check, Players())
	{
		if (!map.world) return;
		PFBaseHero::SpawnInfo spawn;
		spawn.pHero = descriptor;
		spawn.playerId = 0;
		spawn.inTeamId = 0;
		spawn.placement = Placement(CVec3(20, 15, 0), QNULL, CVec3(1, 1, 1));
		spawn.bInitTalents = true;
		spawn.bInitInventory = true;
		spawn.usePlayerInfoTalentSet = false;
		// A human rating avoids the predefined-set bot refinement path; no custom set is supplied.
		spawn.playerInfo.heroRating = 1500;
		CObj<PFBaseHero> created = NWorld::CreateHero(map.world, spawn);
		hero = dynamic_cast<PFBaseMaleHero*>(created.GetPtr());
		if (map.world->GetPlayer(0)) map.world->GetPlayer(0)->SetIsPlaying(true);
		map.world->Step(0, 0);
	}
	~Fixture()
	{
		if (!hero) return;
		if (hero->GetTalentsSet()) hero->GetTalentsSet()->CleanSet();
		hero->DetachFromPlayer();
		hero->Cleanup();
	}
};

/** Constructor dispatches subscribe immediately; the older hero joins the ring only on the zero tick. */
static void CheckStartupOrder(Checks& check, Fixture& f, const PFTalent* talent)
{
	int index = 0, heroIndex = -1;
	std::vector<std::pair<int, const PFDispatch*>> dispatches;
	auto collect = [&](PF_Core::WorldObjectBase& object) {
		if (&object == f.hero.GetPtr()) heroIndex = index;
		if (const auto* dispatch = dynamic_cast<const PFDispatch*>(&object))
			if (dispatch->GetSender() == f.hero.GetPtr() && dispatch->GetAbility()
				&& dispatch->GetAbility()->GetData() == talent && !dispatch->IsObjectDead() && dispatch->Started())
				dispatches.emplace_back(index, dispatch);
		++index;
	};
	f.map.world->ForAllObjectsBeingStepped(collect);
	bool registeredDetached = !dispatches.empty(), beforeHero = heroIndex >= 0 && !dispatches.empty();
	bool laterIds = !dispatches.empty();
	for (const auto& entry : dispatches)
	{
		const PFDispatch* dispatch = entry.second;
		const auto* immediateDb = dynamic_cast<const NDb::DispatchImmediate*>(dispatch->GetDBBase().GetPtr());
		registeredDetached &= dynamic_cast<const PFDispatchImmediate*>(dispatch) && immediateDb && !immediateDb->instantApply
			&& f.map.world->GetObjectById(dispatch->GetObjectId()) == dispatch
			&& !dispatch->GetAbility()->OwnsNativeDispatch(dispatch);
		beforeHero &= entry.first < heroIndex;
		laterIds &= dispatch->GetObjectId() > f.hero->GetObjectId();
	}
	check(registeredDetached, "Demonologist startup: constructor leaves real registered deferred PFDispatchImmediate children pending before gameplay");
	check(beforeHero, "Demonologist startup: original step-ring subscription places constructor dispatches before their hero sender");
	check(laterIds, "Demonologist startup: later-created dispatch IDs do not determine their earlier step-ring subscription");
}

/** GLOBAL lookup includes blocked live statuses, unlike enabled-only CountApplicators. */
static void CheckStartupTimer(Checks& check, Fixture& f, const PFTalent* talent)
{
	const IMiscFormulaPars* context = talent;
	const IMiscFormulaPars* first = f.hero->FindApplicator("A0_WaitTimer", context, GLOBAL, 0);
	const IMiscFormulaPars* second = f.hero->FindApplicator("A0_WaitTimer", context, GLOBAL, 1);
	check(first != context && second == context,
		"Demonologist startup: initial normal ticks create exactly one live named A0_WaitTimer, including blocked statuses");
	const auto* timer = first != context ? dynamic_cast<const PFApplBuff*>(first) : nullptr;
	check(timer && timer->IsAlive() && timer->IsEnabled() && !timer->IsBlocked()
		&& timer->GetAbilityData() == talent && timer->GetLifetime() == 40.f
		&& timer->GetDuration() > 0.f && timer->GetDuration() <= 40.f,
		"Demonologist startup: the sole original A0 timer remains enabled with its authored forty-second lifetime");
}

/** Each stage checks the engine registry, original owner group and actual presentation snapshot. */
static void CheckSummons(Checks& check, Fixture& f, const NDb::Summoned* descriptor,
	bool opensWarFog, int expected, const char* stage, int initialId)
{
	const auto units = Summons(f.map.world);
	const int grouped = f.hero->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "");
	std::printf("B13 Demonologist startup %s: registered=%zu grouped=%d aliveVariable=%.3f\n",
		stage, units.size(), grouped, f.hero->GetVariable("DemonsIsAlive"));
	check(int(units.size()) == expected, stage);
	check(grouped == expected && f.hero->GetMaxAllowedSummons(NDb::SUMMONTYPE_PRIMARY, "") == 3,
		"Demonologist startup: original primary unnamed group has the live count and authored limit three");
	check(f.hero->GetSummonsCount(NDb::SUMMONTYPE_SECONDARY, "") == 0
		&& f.hero->GetSummonsCount(NDb::SUMMONTYPE_PET, "") == 0,
		"Demonologist startup: Demon2 is not reassigned to a secondary or pet group");
	check(f.hero->GetVariable("DemonsIsAlive") == float(expected),
		"Demonologist startup: original summon child applicators maintain the owner's live demon count");
	vector<LinuxDynamicWorldMarker> markers;
	f.map.world->GetLinuxVisibleDynamicWorldMarkers(markers, 640, f.hero, f.hero->GetPos());
	int visibleSummons = 0;
	for (const auto& marker : markers)
		if (marker.kind == LinuxDynamicWorldMarker::KIND_SUMMON) ++visibleSummons;
	check(visibleSummons == expected, "Demonologist startup: faction-visible presentation includes every live Demon2");
	vector<LinuxDynamicWorldMarker> minimap;
	f.map.world->GetLinuxVisibleDynamicWorldMarkers(minimap, 640, f.hero, f.hero->GetPos(), -1, true);
	int minimapSummons = 0;
	for (const auto& marker : minimap)
		if (marker.kind == LinuxDynamicWorldMarker::KIND_SUMMON) ++minimapSummons;
	check(minimapSummons == (opensWarFog ? expected : 0),
		"Demonologist startup: minimap includes opening-fog summons and excludes no-fog summons per authored policy");
	for (const auto& unit : units)
	{
		check(unit->GetMasterUnit() == f.hero.GetPtr() && unit->GetWorld() == f.map.world
			&& unit->GetFaction() == f.hero->GetFaction() && unit->DbUnitDesc() == descriptor,
			"Demonologist startup: each exact shipped Demon2 belongs to the actual human hero");
		check(unit->GetOpenWarFog() == opensWarFog,
			"Demonologist startup: live summon retains its resolved authored openWarFog policy");
		check(unit->GetHiddenCounter() == 0
			&& unit->IsVisibleForFaction(f.hero->GetFaction()),
			"Demonologist startup: original summon is nonhidden and visible to its owner");
		bool onMinimap = false;
		for (const auto& marker : minimap)
			if (marker.objectId == unit->GetObjectId()) onMinimap = true;
		check(onMinimap == opensWarFog,
			"Demonologist startup: exact summon ID follows the authored minimap fog eligibility");
		const LinuxDynamicWorldMarker* found = nullptr;
		for (const auto& marker : markers)
			if (marker.objectId == unit->GetObjectId()) found = &marker;
		check(found && found->kind == LinuxDynamicWorldMarker::KIND_SUMMON && descriptor->sceneObject
			&& found->unitDbid == descriptor->GetDBID().GetFormatted()
			&& found->sceneObjectDbid == descriptor->sceneObject->GetDBID().GetFormatted()
			&& found->objectId != f.hero->GetObjectId(),
			"Demonologist startup: snapshot keeps the exact summon ID and original Demon2 scene, not the hero asset");
	}
	if (initialId >= 0)
	{
		PFBaseUnit* initial = f.map.world->FindLinuxUnitByObjectId(initialId);
		check(initial && !initial->IsDead() && initial->GetMasterUnit() == f.hero.GetPtr(),
			"Demonologist startup: replenishment preserves the initial demon instead of replacing the group");
	}
}

/**
 * Fixture limits: the existing empty 40x30 map has no combatants, lanes or client scene.
 * Session rules, hero, default set, A0 and every nested applicator remain shipped resources.
 * Ordinary 125ms world ticks exercise dispatches and the original timer; no leaf activation,
 * variable seeding, descriptor replacement, forced fog, teleport or AI suppression is used.
 * Presentation checks cover engine snapshots, not GL mesh rendering or full TopA1 startup.
 * Ordering coverage is limited to constructor-time immediate dispatches. Later birth-tick
 * traversal and moving-projectile/physics scheduling are not claims of this regression.
 */
static void Run(Checks& check)
{
	const auto descriptor = NDb::Get<NDb::Hero>(NDb::DBID("/Heroes/Demonolog/_.HROB.xdb"));
	const auto set = NDb::Get<NDb::TalentsSet>(NDb::DBID("/Items/Talents/Demonolog.TALENTSET.xdb"));
	const auto a0 = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Demonolog/Ability0.TALENT.xdb"));
	const auto demon = NDb::Get<NDb::Summoned>(NDb::DBID("/Summons/Demon2/_.SUMMONED.xdb"));
	check(descriptor && set && a0 && demon, "Demonologist startup: shipped hero, default set, A0 and Demon2 load");
	if (!descriptor || !set || !a0 || !demon) return;
	const auto leaf = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/Demonolog/SummonDemons.APLR.xdb"));
	// These authored inline children override only the count; inspect resolved inheritance, never activate them.
	const auto startup = NDb::Get<NDb::SummonApplicator>(NDb::DBID(a0->GetDBID().GetFileName(), "DA48D792-B5F5-416B-9B99-19FBBFB62510"));
	const auto replenish = NDb::Get<NDb::SummonApplicator>(NDb::DBID(a0->GetDBID().GetFileName(), "500CC249-A5C3-428D-BB55-CD2CE31C6D67"));
	check(leaf && startup && replenish, "Demonologist startup: original leaf and both inherited A0 summon descriptors load");
	if (!leaf || !startup || !replenish) return;
	check(startup->openWarFog == leaf->openWarFog && replenish->openWarFog == leaf->openWarFog,
		"Demonologist startup: initial and replenishment summons inherit the original leaf fog policy");
	std::printf("B13 Demonologist authored openWarFog: leaf=%d initial=%d replenish=%d\n",
		int(leaf->openWarFog), int(startup->openWarFog), int(replenish->openWarFog));
	check(!descriptor->defaultTalentsSets.empty() && descriptor->defaultTalentsSets[0].GetPtr() == set.GetPtr(),
		"Demonologist startup: original first default talent set is Demonolog.TALENTSET");
	const bool validSlot = set->levels.size() > 0 && set->levels[0].talents.size() > 5;
	check(validSlot && set->levels[0].talents[5].talent.GetPtr() == a0.GetPtr()
		&& set->levels[0].talents[5].status == NDb::TALENTSLOTSTATUS_NORMAL && a0->isTakenOnStart,
		"Demonologist startup: exact authored A0 is taken-on-start at zero-based slot (0,5)");
	// PreparePredefinedSet indexes the full grid; reject incomplete resource loading before the factory.
	bool completeSet = set->levels.size() >= PFTalentsSet::LEVELS_COUNT;
	for (int level = 0; completeSet && level < PFTalentsSet::LEVELS_COUNT; ++level)
		completeSet = set->levels[level].talents.size() >= PFTalentsSet::SLOTS_COUNT;
	check(completeSet, "Demonologist startup: shipped default talent grid is complete before InitHero");
	if (!completeSet) return;
	Fixture f(check, descriptor);
	check(f.hero && f.map.world, "Demonologist startup: production CreateHero constructs the real male hero");
	if (!f.hero || !f.map.world) return;
	check(f.hero->GetDbHero() == descriptor.GetPtr()
		&& f.map.world->FindLinuxUnitByObjectId(f.hero->GetObjectId()) == f.hero.GetPtr(),
		"Demonologist startup: exact shipped hero is registered without a synthetic replacement");
	PFPlayer* player = f.map.world->GetPlayer(0);
	check(player && !player->IsBot() && player->IsPlaying() && player->GetHero() == f.hero.GetPtr()
		&& f.hero->GetPlayer() == player && f.hero->GetFaction() == NDb::FACTION_FREEZE,
		"Demonologist startup: factory attaches the exact human player after constructor initialization");
	PFTalent* talent = f.hero->GetTalent(0, 5);
	check(talent && talent->GetDBDesc() == a0.GetPtr() && talent->GetOwner() == f.hero.GetPtr() && talent->IsActivated(),
		"Demonologist startup: InitHero activates the original A0 through ActivateTakenOnStart");
	const NDb::UnitConstant* timer = talent ? talent->GetConstant("A0_SummonsTimer") : nullptr;
	check(timer && timer->var.sString == "40", "Demonologist startup: replenishment uses the authored forty-second constant");
	CheckStartupOrder(check, f, talent);
	int ticks = 0;
	const auto advanceTo = [&](int seconds) {
		while (ticks < seconds * 8) { f.map.world->Step(.125f, .125f); ++ticks; }
	};
	// Non-instant child dispatches get normal simulation ticks, not direct activation.
	advanceTo(1);
	CheckStartupTimer(check, f, talent);
	const auto initial = Summons(f.map.world);
	const int initialId = initial.size() == 1 ? initial[0]->GetObjectId() : -1;
	CheckSummons(check, f, demon, leaf->openWarFog, 1, "one initial Demon2 by one second", initialId);
	advanceTo(39);
	CheckSummons(check, f, demon, leaf->openWarFog, 1, "no replenishment before forty seconds", initialId);
	// Allow a few ordinary ticks for nested dispatch/program notifications at each boundary.
	advanceTo(42);
	CheckSummons(check, f, demon, leaf->openWarFog, 2, "second Demon2 after the first authored forty-second wait", initialId);
	advanceTo(79);
	CheckSummons(check, f, demon, leaf->openWarFog, 2, "no third Demon2 before the next forty-second wait", initialId);
	advanceTo(83);
	CheckSummons(check, f, demon, leaf->openWarFog, 3, "third Demon2 after two authored forty-second waits", initialId);
	advanceTo(125);
	CheckSummons(check, f, demon, leaf->openWarFog, 3, "authored replenishment stops at three after another full interval", initialId);
	check(!f.hero->IsDead() && talent && talent == f.hero->GetTalent(0, 5) && talent->IsActivated(),
		"Demonologist startup: the same live human hero and original passive survive replenishment");
}
}

/** Dedicated C112 regression; restore the harness root after destroying all real world objects. */
static void DemonologistStartupB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	check(dataRoot && *dataRoot, "Demonologist startup: original data root is required");
	if (!dataRoot || !*dataRoot) return;
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto root = NDb::Get<NDb::SessionRoot>(NDb::DBID("/Session.ROOT.xdb"));
		check(root && root->logicRoot && root->logicRoot->aiLogic, "Demonologist startup: original session rules load");
		if (root && root->logicRoot && root->logicRoot->aiLogic)
		{
			NDb::SessionRoot::InitRoot(root);
			demonologist_startup_b13::Run(check);
		}
		NDb::SessionRoot::InitRoot(saved);
	}
	NDb::SetResourceCache(nullptr);
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 Demonologist startup checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
