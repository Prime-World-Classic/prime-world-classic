/** Original member-property syntax is an alias of the existing read-only numeric getter whitelist. */
static void ResurrectPropertyAliasesB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	NaftaFixtureB13 f(world);
	for (const char* property : {"Life", "LifeDrain", "LifeRegen", "LifeRegenAbs", "MaxLife", "Energy", "MaxEnergy",
		"Strength", "Intellect", "BaseAttack", "BaseBaseAttack", "MoveSpeed", "CritMult", "BaseLife", "ZZimaSex"})
	for (const char* object : {"sender", "receiver", "pFirst", "pSecond"})
	{
		const bool sender = std::strcmp(object, "sender") == 0 || std::strcmp(object, "pFirst") == 0;
		const std::string field = std::string(object) + (object[0] == 'p' ? "->" : ".") + property;
		const auto expected = LinuxBootstrap::EvaluateUnitNumericFormula(std::string(sender ? "s" : "r") + property, f.sender, f.receiver);
		const auto actual = LinuxBootstrap::EvaluateUnitNumericFormula(field, f.sender, f.receiver);
		check(actual.Succeeded() && expected.Succeeded() && actual.value == expected.value,
			"B13 original bare member properties reuse existing live numeric getters and exact endpoint identity");
	}
	for (const char* expression : {"receiver.LifeUnknown", "receiver.Life()", "receiver.Life(1)", "receiver.Life.x",
		"receiver.Life[0]", "receiver.Gold", "receiver.life", "sender->Life", "pSecond.Life", "receiver.MaxLife=1",
		"receiver.Life++", "receiver.Life/*x*/", "receiver.Life.__unitvar_s_Life"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver).Succeeded(),
			"B13 member aliases do not expose mutation, arbitrary fields, calls, nesting or identifier prefixes");
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 1 : ") + expression, f.sender, f.receiver).Succeeded(),
			"B13 unsupported member syntax cannot hide in an unselected branch");
	}
	for (const char* field : {"receiver.Life", "pSecond->Energy"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(field, f.sender, nullptr).Succeeded(),
			"B13 selected property alias requires its actual receiver context");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("false ? ") + field + " : 7", f.sender, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 valid property aliases preserve lazy missing-receiver behavior");
	}
	const auto candidate = LinuxBootstrap::EvaluateSelectorConditionFormula("receiver.Life", f.sender, f.sender, nullptr);
	check(candidate.Succeeded() && candidate.value == 400, "B13 selector property alias reads its actual selected candidate");
	NDb::Ptr<NDb::StatModApplicator> db = WrapperFactoryFixtureB12::Stat(); auto app = f.Create(db);
	NRandom::RandomGenerator expectedRng(*world->GetRndGen());
	const auto rejected = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,100)+receiver.LifeUnknown", *app);
	check(!rejected.Succeeded() && GlyphRngEqualB13(*world->GetRndGen(), expectedRng),
		"B13 invalid property alias cannot commit a preceding random draw");
	const float draw = expectedRng.Next(1, 100);
	const auto accepted = LinuxBootstrap::EvaluateApplicatorNumericFormula("receiver.Life>0 ? random(1,100) : cMissing", *app);
	check(accepted.Succeeded() && accepted.value == draw && GlyphRngEqualB13(*world->GetRndGen(), expectedRng),
		"B13 real live property condition preserves lazy constants and exactly one committed random draw");
	std::printf("B13 resurrection property aliases checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Use actual ability/factory ownership, permitting authored effects to target dead heroes. */
struct ResurrectEffectFixtureB13 : WrapperFactoryFixtureB12
{
	explicit ResurrectEffectFixtureB13(PFWorld* world) : WrapperFactoryFixtureB12(world)
	{
		const_cast<NDb::Ability*>(abilityDb.GetPtr())->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
		receiver->SetForbidRespawn(true); sender->SetForbidRespawn(true);
	}
	static NDb::ResurrectApplicator* Descriptor(float health = 25, float energy = 50, const char* enabled = "true")
	{
		auto* db = new NDb::ResurrectApplicator;
		db->healthPercent = health; db->energyPercent = energy; db->enabled.sString = enabled;
		return db;
	}
};

/** Original admission must finish without revival on a false, unsupported or cancelled start. */
static void ResurrectEffectAdmissionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = ResurrectEffectFixtureB13;
	for (const char* condition : {"true", "", "false", "receiver.Life<=0", "receiver.Life>0",
		"false ? cMissing : true", "cMissing", "1/0", "compiled-only"})
	{
		F f(isolated.world); HeroCorpseKillB13(isolated.world, f.receiver);
		const float energy = f.receiver->GetEnergy(), delay = f.receiver->GetTimeToRespawn();
		auto* raw = F::Descriptor(25, 50, condition);
		if (std::strcmp(condition, "compiled-only") == 0) { raw->enabled.sString = ""; raw->enabled.compiledString = "unsupported"; }
		NDb::Ptr<NDb::ResurrectApplicator> db = raw;
		const bool allowed = !*condition || std::strcmp(condition, "true") == 0
			|| std::strcmp(condition, "receiver.Life<=0") == 0 || std::strcmp(condition, "false ? cMissing : true") == 0;
		int events = 0; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (event->GetType() == NDb::BASEUNITEVENT_RESURRECT) ++events; return 0u;
		};
		f.receiver->AddEventListener(listener.GetPtr());
		auto app = f.Create(db); check(app && f.Activate(app), "B13 conditional resurrection uses the real instant-effect lifecycle");
		check(f.receiver->IsDead() != allowed && events == int(allowed),
			"B13 false or invalid resurrection condition cannot restore life or publish resurrection");
		check(allowed ? (f.receiver->GetHealth() == f.receiver->GetMaxHealth() * .25f
			&& f.receiver->GetEnergy() == f.receiver->GetMaxEnergy() * .5f)
			: (f.receiver->GetHealth() == 0 && f.receiver->GetEnergy() == energy && f.receiver->GetTimeToRespawn() == delay),
			"B13 admitted resurrection uses authored fractions while rejected starts preserve dead resources and timer");
		check(app && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 conditional resurrection balances the original ability counter on every outcome");
		f.receiver->RemoveEventListener(listener.GetPtr()); listener->action = nullptr;
	}
	{
		F f(isolated.world); HeroCorpseKillB13(isolated.world, f.receiver);
		NDb::Ptr<NDb::ResurrectApplicator> db = F::Descriptor();
		const Target target(f.receiver.GetPtr()); PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db;
		CObj<AbsorbShieldParentB13> parent = new AbsorbShieldParentB13(pars);
		auto app = f.Create(db, CPtr<PFBaseApplicator>(parent.GetPtr()));
		check(app && f.Activate(app) && f.receiver->IsDead() && parent->starts == 1 && parent->stops == 1
			&& f.instance->GetActiveApplicatorsCount() == 0,
			"B13 parent-cancelled resurrection cannot revive after its balanced start/stop callbacks");
	}
	{
		F f(isolated.world); NDb::Ptr<NDb::ResurrectApplicator> db = F::Descriptor();
		const float health = f.receiver->GetHealth(), energy = f.receiver->GetEnergy();
		auto app = f.Create(db); check(app && f.Activate(app) && f.receiver->GetHealth() == health
			&& f.receiver->GetEnergy() == energy && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 resurrection effect never refills an already-living target");
	}
	{
		ApplicatorHistoryFixtureB13 f(check, isolated.world);
		const_cast<NDb::Ability*>(f.db.GetPtr())->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
		auto hero = HeroCorpseCreateB13(isolated.world); HeroCorpseKillB13(isolated.world, hero);
		isolated.world->Step(4, 4);
		check(hero->IsResurrecting(), "B13 actual world tick enters the original final resurrection-advance window");
		const float delay = hero->GetTimeToRespawn(), energy = hero->GetEnergy();
		NDb::Ptr<NDb::ResurrectApplicator> db = F::Descriptor(); auto app = f.Apply(db, 0, nullptr, hero);
		check(app && !app->IsAlive() && hero->IsDead() && hero->GetEnergy() == energy && hero->GetTimeToRespawn() == delay,
			"B13 resurrection effect cannot override an already-advancing scheduled revival");
		isolated.world->Step(1, 1);
		check(!hero->IsDead() && hero->GetHealth() == hero->GetMaxHealth() && hero->GetEnergy() == hero->GetMaxEnergy(),
			"B13 rejected advance-window effect leaves the original full-resource scheduled revival intact");
		hero->Cleanup();
	}
	std::printf("B13 resurrection effect admission checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Clamp finite authored percentages independently; retain the established native zero-health fallback. */
static void ResurrectEffectFractionsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = ResurrectEffectFixtureB13;
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	struct Case { float health, energy; };
	for (Case test : {Case{25, 50}, {50, 25}, {100, 100}, {150, -25}, {0, 0}, {-50, 150},
		{.001f, .0001f}, {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
		{nan, 50}, {50, nan}, {inf, 50}, {50, -inf}})
	{
		F f(isolated.world); const CVec3 spawn = f.receiver->GetPosition();
		HeroCorpseKillB13(isolated.world, f.receiver); f.receiver->TeleportTo(CVec2(9, 7), false, false);
		const float oldEnergy = f.receiver->GetEnergy(), delay = f.receiver->GetTimeToRespawn();
		NDb::Ptr<NDb::ResurrectApplicator> db = F::Descriptor(test.health, test.energy);
		auto app = f.Create(db); check(app && f.Activate(app), "B13 percentage resurrection enters the actual factory/start/stop path");
		if (std::isfinite(test.health) && std::isfinite(test.energy))
		{
			float health = f.receiver->GetMaxHealth() * Clamp(test.health * 1e-2f, 0.f, 1.f);
			if (health <= EPS_VALUE) health = f.receiver->GetMaxHealth();
			check(!f.receiver->IsDead() && f.receiver->GetHealth() == health
				&& f.receiver->GetEnergy() == f.receiver->GetMaxEnergy() * Clamp(test.energy * 1e-2f, 0.f, 1.f),
				"B13 finite resurrection percentages retain original independent clamp and float order plus native zero-health fallback");
			check(f.receiver->GetPosition().x == spawn.x && f.receiver->GetPosition().y == spawn.y,
				"B13 authored resurrection uses the original base destination, not the corpse position");
		}
		else check(f.receiver->IsDead() && f.receiver->GetHealth() == 0 && f.receiver->GetEnergy() == oldEnergy
			&& f.receiver->GetTimeToRespawn() == delay && f.receiver->GetPosition().x == 9 && f.receiver->GetPosition().y == 7,
			"B13 nonfinite percentage cannot partially restore resources, move the corpse or consume its timer");
		check(app && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 percentage rejection and success both finish original instant-effect ownership");
	}
	std::printf("B13 resurrection effect fractions checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

using ResurrectXmlPathB13 = std::vector<std::pair<string, int>>;

/** Locate only inline resurrection payloads using the engine XML tree, without resource precaching. */
static void ResurrectXmlPathsB13(const NXml::CXmlNode* node, ResurrectXmlPathB13& path,
	std::vector<ResurrectXmlPathB13>& found, int& budget)
{
	if (!node || --budget < 0 || path.size() > 64) return;
	if (node->GetName() == "ResurrectApplicator") { found.push_back(path); return; }
	const auto& children = node->GetNodes();
	for (int i = 0; i < children.size() && budget > 0; ++i)
	{
		const auto* child = children[i]; const string name = child->GetName().ToString();
		path.push_back({name, name == "Item" || name == "item" ? i + 1 : 0});
		ResurrectXmlPathsB13(child, path, found, budget); path.pop_back();
	}
}

/** Deserialize unchanged inline payloads through IXmlSaver, then exercise their real factory and conditions.
 * These are payload tests with explicit ability context, not complete Angel/Reaper/Boss ability-kit claims.
 */
static void ResurrectEffectAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	struct Source { const char* path; int count; bool supported; };
	for (Source source : {Source{"Items/Talents/Class/Angel/Ability_A5_2.TALENT.xdb", 2, true},
		{"Items/Talents/Class/Reaper/Ability_A4_2.TALENT.xdb", 1, true},
		{"Impulses/_.IBCN.xdb", 1, true}, {"Creeps/Bosses/Boss1/Resurrect.APLR.xdb", 1, false}})
	{
		CObj<Stream> xml = files->OpenFile(source.path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 original resurrection payload source opens read-only"); if (!xml || !xml->IsOk()) continue;
		CObj<NXml::CXmlReader> reader = new NXml::CXmlReader(xml);
		const auto* root = reader->GetRootElement(); ResurrectXmlPathB13 path{{root->GetName().ToString(), 0}};
		std::vector<ResurrectXmlPathB13> found; int budget = 8192;
		ResurrectXmlPathsB13(root, path, found, budget);
		check(budget > 0 && int(found.size()) == source.count, "B13 bounded original XML traversal finds every expected resurrection payload");
		for (const auto& fields : found)
		{
			CObj<Stream> stream = files->OpenFile(source.path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
			CObj<IXmlSaver> saver = CreateXmlSaver(stream, true); bool opened = true;
			for (size_t i = 0; i + 1 < fields.size(); ++i)
				if (!saver->StartChunk(fields[i].first.c_str(), fields[i].second)) { opened = false; break; }
			check(opened, "B13 original payload path reopens through the engine typed XML serializer"); if (!opened) continue;
			auto* raw = new NDb::ResurrectApplicator; NDb::Ptr<NDb::ResurrectApplicator> db = raw;
			saver->Add("ResurrectApplicator", raw);
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			ResurrectEffectFixtureB13 f(isolated.world);
			PFBaseHero* hero = raw->applyTarget == NDb::APPLICATORAPPLYTARGET_ABILITYOWNER ? f.sender.GetPtr() : f.receiver.GetPtr();
			HeroCorpseKillB13(isolated.world, hero);
			const float energy = hero->GetEnergy(), delay = hero->GetTimeToRespawn();
			auto app = f.Create(db); check(app && f.Activate(app), "B13 unchanged authored resurrection payload uses its original target routing and factory");
			if (source.supported)
			{
				check((raw->healthPercent == 25 || raw->healthPercent == 50 || raw->healthPercent == 100)
					&& !hero->IsDead() && hero->GetHealth() == hero->GetMaxHealth() * (raw->healthPercent * 1e-2f)
					&& hero->GetEnergy() == hero->GetMaxEnergy() * (raw->energyPercent * 1e-2f),
					"B13 Angel quarter/half and Reaper/impulse full resurrection preserve unchanged original resource percentages");
			}
			else check(hero->IsDead() && hero->GetEnergy() == energy && hero->GetTimeToRespawn() == delay,
				"B13 unsupported Boss1 parent-object condition cannot turn into an unconditional resurrection");
			check(app && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 original resurrection payload balances ability ownership whether admitted or rejected");
		}
	}
	std::printf("B13 authored resurrection effects checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
