#include "../PF_GameLogic/PFCastLimitations.h"
#include "../PF_GameLogic/PFWorldNatureMap.h"
#include "../System/FileSystem/TestFileSystem.h"

/** Reuse the authored codec fixture defined later in the probe's include order. */
static string NatureQueryImageB12(const NDb::DBNatureMap& db, int invalidCell);
static int positionQueryDestructionsB12 = 0;

/** Isolate actual talent casts from the outer probe world's unloaded nature grid. */
struct PositionCastTerrainB12
{
	B8BehaviourForeignWorld loaded;
	CObj<TestFileSystem> files;
	NDb::Ptr<NDb::Terrain> terrain;
	explicit PositionCastTerrainB12(Checks& check) : loaded(check)
	{
		files = new TestFileSystem("", false);
		RootFileSystem::RegisterFileSystem(files);
		auto* raw = new NDb::Terrain;
		raw->elemXCount = 4; raw->elemYCount = 3; raw->tilesPerElement = 4;
		raw->natureMap.gridStep = 10; raw->natureMap.segmentCount = 3;
		raw->natureMap.attackSpeed = 1; raw->natureMap.overrideSpeed = 0;
		raw->natureMap.natureMapFile = "/Mock/PositionCastB12.naturemap";
		terrain = raw;
		if (loaded.world)
		{
			Reload();
			check(Map()->GetNatureInPoint(15.f, 10.f) == TERR_A
				&& Map()->GetNatureInPoint(25.f, 10.f) == TERR_B
				&& Map()->GetNatureInPoint(5.f, 10.f) == TERR_N,
				"position terrain: actual loader exposes authored friendly, enemy and neutral cells");
		}
	}
	PFWorldNatureMap* Map() const { return loaded.world->GetNatureMap(); }
	void Reload()
	{
		files->SetFileWithContents(terrain->natureMap.natureMapFile, NatureQueryImageB12(terrain->natureMap, 0));
		Map()->OnLoaded(terrain);
	}
	~PositionCastTerrainB12() { RootFileSystem::UnregisterFileSystem(files); }
};

/** Query instrumentation uses the protected inert ability constructor, never a talent tombstone. */
struct PositionNatureAbilityB12 : PFAbilityData
{
	OBJECT_METHODS(PFAbilityData::typeId, PositionNatureAbilityB12);
public:
	// The public ability constructor allocates a constants map with a weak self-reference.
	// Reconstruction must use its empty protected constructor to release the final weak shell.
	PositionNatureAbilityB12() = default;
	~PositionNatureAbilityB12() { ++positionQueryDestructionsB12; }
	mutable std::vector<CVec2> samples;
	int forcedNature = -1;
	PositionNatureAbilityB12(PFBaseMaleHero* hero, const NDb::Talent* db)
		: PFAbilityData(hero, db, NDb::ABILITYTYPEID_TALENT, false, false) {}
	int GetNatureTypeInPos(CVec2 position) const override
	{
		samples.push_back(position);
		if (forcedNature != -1) return forcedNature;
		// Controlled query results, distinct from the hero's spawn position (5, 5).
		return position.x < 10 ? TERR_A : position.x < 20 ? TERR_B : TERR_N;
	}
};

struct PositionCastFixtureB12
{
	CObj<B9CastHero> hero;
	NDb::Ptr<NDb::Talent> db;
	NDb::Ptr<NDb::PositionCastLimitation> limitation;
	CObj<PFTalent> talent;
	CObj<PositionNatureAbilityB12> query;
	float strength;
	PositionCastFixtureB12(PFWorld* world, const char* expression, NDb::EFaction faction = NDb::FACTION_FREEZE)
	{
		hero = B9MaleHero(world, faction); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		auto* raw = new NDb::Talent; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5";
		raw->naftaCost = 0; raw->isUnitFreeAfterCast = true;
		auto* limit = new NDb::PositionCastLimitation; limit->condition.sString = expression;
		limitation = limit; raw->castLimitations.push_back(limit);
		auto* effect = new NDb::PermanentStatModApplicator;
		effect->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		effect->enabled.sString = "true"; effect->modifier.stat = NDb::STAT_STRENGTH;
		effect->modifier.multValue.sString = "1"; effect->modifier.addValue.sString = "3";
		raw->applicators.push_back(effect);
		auto* constants = new NDb::UnitConstantsContainer; auto* constant = new NDb::UnitConstant;
		constant->name = "PositionGate"; constant->var.sString = "sLife > 500";
		constants->vars.push_back(constant); raw->constants = constants;
		db = raw;
		talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false);
		query = new PositionNatureAbilityB12(hero.GetPtr(), db);
		strength = hero->GetStatValue(NDb::STAT_STRENGTH);
	}
	~PositionCastFixtureB12() { hero->Cleanup(); }
};

static void PositionCastB12(Checks& check, PFWorld* unloadedWorld)
{
	PositionCastTerrainB12 terrain(check);
	PFWorld* world = terrain.loaded.world;
	if (!world) return;
	const Target enemy(CVec3(25, 10, 0));
	{
		PositionCastFixtureB12 f(world, "true");
		CPtr<CObjectBase> weak(f.query.GetPtr());
		CObjectBase* address = weak.GetPtr();
		f.query->samples.reserve(8); f.query->samples.push_back(CVec2(15, 5));
		f.query->forcedNature = TERR_B;
		const int destroyed = positionQueryDestructionsB12;
		check(weak->GetSizeOf() == sizeof(PositionNatureAbilityB12)
			&& weak->GetTypeId() == PFAbilityData::typeId,
			"position fixture lifetime: concrete live size retains production ability typeId");
		f.query = nullptr;
		auto* shell = dynamic_cast<PositionNatureAbilityB12*>(weak.GetPtr());
		check(!IsValid(weak) && weak.GetPtr() == address && shell,
			"position fixture lifetime: last strong release preserves concrete invalid weak shell");
		check(shell && shell->samples.empty() && shell->samples.capacity() == 0 && shell->forcedNature == -1
			&& positionQueryDestructionsB12 == destroyed + 1,
			"position fixture lifetime: original sample allocation releases before inert reconstruction");
		check(weak->GetSizeOf() == sizeof(PositionNatureAbilityB12) && weak->GetTypeId() == PFAbilityData::typeId,
			"position fixture lifetime: shell keeps concrete size and production typeId");
		weak = nullptr;
		check(positionQueryDestructionsB12 == destroyed + 2,
			"position fixture lifetime: final weak release deletes shell without a recreated self-reference");
	}
	const char* authored = "appl.GetNatureTypeInPos(rPos2D) != sFctn && appl.GetNatureTypeInPos(rPos2D) != TERR_N";
	struct Case { const char* formula; bool allow; int calls; };
	for (const auto& entry : {
		Case{authored, true, 1}, Case{"true", true, 0}, Case{"false", false, 0},
		Case{"", false, 0}, Case{"-2", true, 0}, Case{"cPositionGate", true, 0},
		Case{"sFctn == 1", true, 0}, Case{"rLife == sLife", false, 0},
		Case{"receiver.vGate == 0", false, 0}, Case{"rPos2D", false, 0},
		Case{"false && appl.GetNatureTypeInPos(rPos2D)", false, 0},
		Case{"true || appl.GetNatureTypeInPos(rPos2D)", true, 0},
		Case{"true ? cPositionGate : appl.GetNatureTypeInPos(rPos2D)", true, 0},
		Case{"appl.GetNatureTypeInPos(rPos2D) == 2 || cMissing", true, 1},
		Case{"appl.GetNatureTypeInPos(rPos2D) == 1 && 1/0", false, 1},
		Case{"cMissing", false, 0}, Case{"1/0", false, 0}, Case{"1e39", false, 0},
		Case{"sLife+", false, 0}, Case{"appl.GetNatureTypeInPos(sPos2D)", false, 0},
		Case{"appl.GetNatureTypeInPos(rPos2D.x)", false, 0},
		Case{"appl.GetNatureTypeInPos(rPos2D, 0)", false, 0},
		Case{"appl.GetNatureTypeInPos(rPos2D); true", false, 0},
		Case{"appl.GetTerrainType()", false, 0}, Case{"__position_nature", false, 0},
		Case{"true || appl.GetSomethingElse(rPos2D)", false, 0}})
	{
		PositionCastFixtureB12 f(world, entry.formula);
		check(f.talent->Activate(), "position-limited talent purchases through the real activation policy");
		const Target target(CVec3(15, 5, 0)); const CastLimitationsCheckParams cp(*f.query, target);
		check((f.limitation->Check(cp) == f.query.GetPtr()) == entry.allow,
			"position factory returns the original ability only for a successful checked custom-context condition");
		if (int(f.query->samples.size()) != entry.calls)
			std::printf("Position lookup count: source=%s expected=%d actual=%zu\n", entry.formula, entry.calls, f.query->samples.size());
		check(int(f.query->samples.size()) == entry.calls,
			"nature accessor evaluation respects short-circuit branches and the exact syntax whitelist");
		bool exact = true;
		for (const auto& point : f.query->samples) exact = exact && point.x == 15 && point.y == 5;
		check(exact, "nature accessor receives the true ICustomFormulaPars position, not the owner's coordinates");
		check((CheckPositionLimitation(*f.limitation, cp) == f.query.GetPtr()) == entry.allow,
			"direct position helper agrees with the DB factory");
		check(f.query->CheckCastLimitations(target) == (entry.allow ? nullptr : f.limitation.GetPtr()),
			"normal ability limitation list returns the exact denied position descriptor");
		check(f.talent->GetNatureTypeInPos(enemy.GetPosition().AsVec2D()) == TERR_B
			&& f.talent->CheckCastLimitations(enemy) == (entry.allow ? nullptr : f.limitation.GetPtr()),
			"real talent admission agrees with instrumented query on an actual authored enemy cell");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(f.talent, enemy);
		check(bool(cast) == entry.allow && f.hero->GetMana() == (entry.allow ? 180 : 200)
			&& f.talent->GetCurrentCooldown() == (entry.allow ? 5 : 0)
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + (entry.allow ? 3 : 0),
			"position admission atomically gates actual paid casting, cooldown and owner effect");
	}
	for (auto faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN}) for (float x : {15.f, 25.f, 5.f})
	{
		PositionCastFixtureB12 f(world, authored, faction); f.talent->Activate();
		const bool allow = (faction == NDb::FACTION_FREEZE && x == 25) || (faction == NDb::FACTION_BURN && x == 15);
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(f.talent, Target(CVec3(x, 10, 0)));
		check(bool(cast) == allow && f.hero->GetMana() == (allow ? 180 : 200)
			&& f.talent->GetCurrentCooldown() == (allow ? 5 : 0)
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + (allow ? 3 : 0),
			"authored BurnLand permits enemy nature only for either actual caster faction");
	}
	for (bool compiledOnly : {false, true})
	{
		PositionCastFixtureB12 f(world, compiledOnly ? "" : authored); f.talent->Activate();
		const_cast<NDb::PositionCastLimitation*>(f.limitation.GetPtr())->condition.compiledString = "unsupported-x86-bytecode";
		check(bool(f.hero->UseTalent(f.talent, enemy)) == !compiledOnly
			&& f.hero->GetMana() == (compiledOnly ? 200 : 180)
			&& f.talent->GetCurrentCooldown() == (compiledOnly ? 0 : 5)
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + (compiledOnly ? 0 : 3),
			"position formula uses authored source alongside bytecode but never evaluates compiled-only data");
	}
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		CObj<B9CastHero> target = B9MaleHero(world, NDb::FACTION_BURN); target->TeleportTo(CVec2(25, 10));
		f.query->forcedNature = TERR_B;
		const Target unitTarget(target.GetPtr());
		check(f.limitation->Check(CastLimitationsCheckParams(*f.query, unitTarget)) == f.query.GetPtr()
			&& f.hero->UseTalent(f.talent, unitTarget) && !f.query->samples.empty()
			&& f.query->samples.front().x == target->GetPosition().x
			&& f.query->samples.front().y == target->GetPosition().y
			&& f.hero->GetMana() == 180 && f.talent->GetCurrentCooldown() == 5
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + 3,
			"unit targets supply their actual position through a custom receiver rather than unit formula properties");
		target->Cleanup();
	}
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		const Target target = enemy;
		terrain.Map()->Override(NDb::FACTION_NEUTRAL, CCircle(target.GetPosition().AsVec2D(), 2), 10, 901);
		check(f.talent->GetNatureTypeInPos(target.GetPosition().AsVec2D()) == TERR_N,
			"real talent observes same-tick neutral terrain override");
		check(!f.hero->UseTalent(f.talent, target) && f.hero->GetMana() == 200
			&& f.talent->GetCurrentCooldown() == 0 && f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength,
			"neutral nature denies before payment");
		terrain.Map()->Override(NDb::FACTION_BURN, CCircle(target.GetPosition().AsVec2D(), 2), 10, 902);
		check(f.hero->UseTalent(f.talent, target) && f.hero->GetMana() == 180
			&& f.talent->GetCurrentCooldown() == 5 && f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + 3,
			"same-tick nature change is sampled live without cached admission");
		terrain.Reload();
	}
	for (int nature : {-5, 3, std::numeric_limits<int>::max()})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate(); f.query->forcedNature = nature;
		check(!f.query->ApplyToTarget(Target(CVec3(15, 5, 0))) && f.hero->GetMana() == 200
			&& f.query->GetCurrentCooldown() == 0 && f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength
			&& f.query->samples.size() == 1 && f.query->samples.front().x == 15 && f.query->samples.front().y == 5,
			"out-of-domain terrain values cannot satisfy the enemy-nature predicate");
	}
	for (bool skip : {false, true})
	{
		PositionCastFixtureB12 f(world, skip ? "true || appl.GetNatureTypeInPos(rPos2D)" : "false || appl.GetNatureTypeInPos(rPos2D)");
		const Target outside(CVec3(41, 5, 0)); const CastLimitationsCheckParams cp(*f.query, outside);
		check((f.limitation->Check(cp) == f.query.GetPtr()) == skip && f.query->samples.empty(),
			"unselected nature access does not inspect grid bounds while selected out-of-bounds access fails closed");
	}
	for (const CVec3& point : {CVec3(0, 0, 0), CVec3(40, 30, 0)})
	{
		PositionCastFixtureB12 f(world, authored); f.query->forcedNature = TERR_B;
		const Target target(point);
		check(f.limitation->Check(CastLimitationsCheckParams(*f.query, target)) == f.query.GetPtr()
			&& f.query->samples.size() == 1 && f.query->samples.front().x == point.x && f.query->samples.front().y == point.y,
			"finite world-edge coordinates remain inclusive as in the original nature grid");
	}
	for (bool loseAdmission : {false, true})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(f.talent.GetPtr(), enemy, false);
		B10ActivationTimerRestore timer(1); pending->operator&(timer);
		check(pending->ApplyToTarget() && f.hero->GetMana() == 200 && f.talent->GetCurrentCooldown() == 0,
			"delayed position cast performs admission without early payment or cooldown");
		terrain.Map()->Override(loseAdmission ? NDb::FACTION_NEUTRAL : NDb::FACTION_BURN,
			CCircle(enemy.GetPosition().AsVec2D(), 2), 10, 903);
		check(f.talent->GetNatureTypeInPos(enemy.GetPosition().AsVec2D()) == (loseAdmission ? TERR_N : TERR_B),
			"pending real talent observes authored map override before delayed activation");
		pending->Update(1);
		check(f.hero->GetMana() == (loseAdmission ? 200 : 180) && f.talent->GetCurrentCooldown() == (loseAdmission ? 0 : 5)
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + (loseAdmission ? 0 : 3),
			"delayed activation rechecks live nature before payment cooldown and gameplay mutation");
		pending->Cancel();
		terrain.Reload();
	}
	{
		PositionCastFixtureB12 f(world, "cPositionGate");
		auto* constant = const_cast<NDb::UnitConstant*>(f.db->constants->vars[0].GetPtr());
		constant->var.sString = authored;
		const Target target(CVec3(15, 5, 0));
		check(f.limitation->Check(CastLimitationsCheckParams(*f.query, target)) == f.query.GetPtr() && f.query->samples.size() == 1,
			"named constants inherit the custom position context and its lazy nature binding");
		constant->var.sString = "cPositionGate";
		check(!f.limitation->Check(CastLimitationsCheckParams(*f.query, target)),
			"position-context constants retain the existing checked cycle guard");
	}
	for (const CVec3& point : {CVec3(-1, 5, 0), CVec3(41, 5, 0), CVec3(15, 31, 0),
		CVec3(std::numeric_limits<float>::max(), 5, 0), CVec3(std::numeric_limits<float>::infinity(), 5, 0),
		CVec3(15, std::numeric_limits<float>::quiet_NaN(), 0), CVec3(15, 5, std::numeric_limits<float>::infinity())})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		check(!f.query->ApplyToTarget(Target(point)) && !f.hero->UseTalent(f.talent, Target(point))
			&& f.hero->GetMana() == 200 && f.query->samples.empty()
			&& f.talent->GetCurrentCooldown() == 0 && f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength,
			"invalid or out-of-map geometry is rejected before nature lookup or paid mutation");
	}
	{
		PositionCastFixtureB12 f(world, "true"); const Target target(CVec3(15, 5, 0));
		CastLimitationsCheckParams cp(*f.query, target); cp.pAbility = nullptr;
		check(!f.limitation->Check(cp), "missing position ability is rejected without dereference");
		cp.pAbility = f.query; cp.pTarget = nullptr;
		check(!f.limitation->Check(cp), "missing position target is rejected without dereference");
		const Target empty; cp.pTarget = &empty;
		check(!f.limitation->Check(cp), "empty target cannot pass a literal-true position condition");
		CObj<PFLogicObject> object = new PFLogicObject(world, CVec3(15, 5, 0), nullptr);
		const Target nonunit(object.GetPtr()); cp.pTarget = &nonunit;
		check(!f.limitation->Check(cp), "nonunit objects are not silently reinterpreted as unit position targets");
	}
	{
		B8BehaviourForeignWorld other(check);
		if (other.world)
		{
			PositionCastFixtureB12 f(world, "true"); f.talent->Activate();
			CObj<B9CastHero> target = B9MaleHero(other.world, NDb::FACTION_BURN);
			check(!f.hero->UseTalent(f.talent, Target(target.GetPtr())) && f.hero->GetMana() == 200
				&& f.talent->GetCurrentCooldown() == 0 && f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength,
				"foreign-world unit targets cannot bypass position admission with a literal condition");
			target->Cleanup();
		}
	}
	{
		// Observe, rather than conceal, the separate native terrain runtime boundary.
		CObj<B9CastHero> hero = B9MaleHero(unloadedWorld, NDb::FACTION_FREEZE);
		NDb::Ptr<NDb::Ability> db = new NDb::Ability;
		CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(ability->GetNatureTypeInPos(CVec2(15, 5)) == TERR_N,
			"unloaded outer world remains neutral; position fixture terrain does not leak into other tests");
		const auto wrongContext = LinuxBootstrap::EvaluateUnitNumericFormula(authored, hero.GetPtr(), hero.GetPtr(), ability);
		check(!wrongContext.Succeeded(), "unit formula context cannot masquerade as the custom position receiver");
		hero->Cleanup();
	}
}
