#include "../PF_GameLogic/PFCastLimitations.h"

/** Only nature lookup is controlled: purchase, admission, payment, dispatch and effects are engine code. */
struct PositionNatureTalentB12 : PFTalent
{
	mutable std::vector<CVec2> samples;
	int forcedNature = -1;
	PositionNatureTalentB12(PFBaseMaleHero* hero, const NDb::Talent* db) : PFTalent(hero, db, 1, 0, 0, false) {}
	int GetNatureTypeInPos(CVec2 position) const override
	{
		samples.push_back(position);
		if (forcedNature != -1) return forcedNature;
		// Controlled authored terrain cells, distinct from the hero's spawn position (5, 5).
		return position.x < 10 ? TERR_A : position.x < 20 ? TERR_B : TERR_N;
	}
};

struct PositionCastFixtureB12
{
	CObj<B9CastHero> hero;
	NDb::Ptr<NDb::Talent> db;
	NDb::Ptr<NDb::PositionCastLimitation> limitation;
	CObj<PositionNatureTalentB12> talent;
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
		db = raw; talent = new PositionNatureTalentB12(hero.GetPtr(), db);
		strength = hero->GetStatValue(NDb::STAT_STRENGTH);
	}
	~PositionCastFixtureB12() { hero->Cleanup(); }
};

static void PositionCastB12(Checks& check, PFWorld* world)
{
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
		const Target target(CVec3(15, 5, 0)); const CastLimitationsCheckParams cp(*f.talent, target);
		check((f.limitation->Check(cp) == f.talent.GetPtr()) == entry.allow,
			"position factory returns the original ability only for a successful checked custom-context condition");
		if (int(f.talent->samples.size()) != entry.calls)
			std::printf("Position lookup count: source=%s expected=%d actual=%zu\n", entry.formula, entry.calls, f.talent->samples.size());
		check(int(f.talent->samples.size()) == entry.calls,
			"nature accessor evaluation respects short-circuit branches and the exact syntax whitelist");
		bool exact = true;
		for (const auto& point : f.talent->samples) exact = exact && point.x == 15 && point.y == 5;
		check(exact, "nature accessor receives the true ICustomFormulaPars position, not the owner's coordinates");
		check((CheckPositionLimitation(*f.limitation, cp) == f.talent.GetPtr()) == entry.allow,
			"direct position helper agrees with the DB factory");
		check(f.talent->CheckCastLimitations(target) == (entry.allow ? nullptr : f.limitation.GetPtr()),
			"normal ability limitation list returns the exact denied position descriptor");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(f.talent, target);
		check(bool(cast) == entry.allow && f.hero->GetMana() == (entry.allow ? 180 : 200)
			&& f.talent->GetCurrentCooldown() == (entry.allow ? 5 : 0)
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + (entry.allow ? 3 : 0),
			"position admission atomically gates actual paid casting, cooldown and owner effect");
	}
	for (auto faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN}) for (float x : {5.f, 15.f, 25.f})
	{
		PositionCastFixtureB12 f(world, authored, faction); f.talent->Activate();
		const bool allow = (faction == NDb::FACTION_FREEZE && x == 15) || (faction == NDb::FACTION_BURN && x == 5);
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(f.talent, Target(CVec3(x, 5, 0)));
		check(bool(cast) == allow && f.hero->GetMana() == (allow ? 180 : 200),
			"authored BurnLand permits enemy nature only for either actual caster faction");
	}
	for (bool compiledOnly : {false, true})
	{
		PositionCastFixtureB12 f(world, compiledOnly ? "" : authored); f.talent->Activate();
		const_cast<NDb::PositionCastLimitation*>(f.limitation.GetPtr())->condition.compiledString = "unsupported-x86-bytecode";
		check(bool(f.hero->UseTalent(f.talent, Target(CVec3(15, 5, 0)))) == !compiledOnly,
			"position formula uses authored source alongside bytecode but never evaluates compiled-only data");
	}
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		CObj<B9CastHero> target = B9MaleHero(world, NDb::FACTION_BURN); target->TeleportTo(CVec2(15, 5));
		check(f.hero->UseTalent(f.talent, Target(target.GetPtr())) && !f.talent->samples.empty()
			&& f.talent->samples.front().x == target->GetPosition().x,
			"unit targets supply their actual position through a custom receiver rather than unit formula properties");
		target->Cleanup();
	}
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		const Target target(CVec3(15, 5, 0));
		f.talent->forcedNature = TERR_N;
		check(!f.hero->UseTalent(f.talent, target) && f.hero->GetMana() == 200,
			"neutral nature denies before payment");
		f.talent->forcedNature = TERR_B;
		check(f.hero->UseTalent(f.talent, target) && f.hero->GetMana() == 180,
			"same-tick nature change is sampled live without cached admission");
	}
	for (int nature : {-5, 3, std::numeric_limits<int>::max()})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate(); f.talent->forcedNature = nature;
		check(!f.hero->UseTalent(f.talent, Target(CVec3(15, 5, 0))) && f.hero->GetMana() == 200,
			"out-of-domain terrain values cannot satisfy the enemy-nature predicate");
	}
	for (bool skip : {false, true})
	{
		PositionCastFixtureB12 f(world, skip ? "true || appl.GetNatureTypeInPos(rPos2D)" : "false || appl.GetNatureTypeInPos(rPos2D)");
		const Target outside(CVec3(41, 5, 0)); const CastLimitationsCheckParams cp(*f.talent, outside);
		check((f.limitation->Check(cp) == f.talent.GetPtr()) == skip && f.talent->samples.empty(),
			"unselected nature access does not inspect grid bounds while selected out-of-bounds access fails closed");
	}
	for (const CVec3& point : {CVec3(0, 0, 0), CVec3(40, 30, 0)})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->forcedNature = TERR_B;
		const Target target(point);
		check(f.limitation->Check(CastLimitationsCheckParams(*f.talent, target)) == f.talent.GetPtr()
			&& f.talent->samples.size() == 1 && f.talent->samples.front().x == point.x && f.talent->samples.front().y == point.y,
			"finite world-edge coordinates remain inclusive as in the original nature grid");
	}
	for (bool loseAdmission : {false, true})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(f.talent.GetPtr(), Target(CVec3(15, 5, 0)), false);
		B10ActivationTimerRestore timer(1); pending->operator&(timer);
		check(pending->ApplyToTarget() && f.hero->GetMana() == 200 && f.talent->GetCurrentCooldown() == 0,
			"delayed position cast performs admission without early payment or cooldown");
		f.talent->forcedNature = loseAdmission ? TERR_N : TERR_B;
		pending->Update(1);
		check(f.hero->GetMana() == (loseAdmission ? 200 : 180) && f.talent->GetCurrentCooldown() == (loseAdmission ? 0 : 5)
			&& f.hero->GetStatValue(NDb::STAT_STRENGTH) == f.strength + (loseAdmission ? 0 : 3),
			"delayed activation rechecks live nature before payment cooldown and gameplay mutation");
		pending->Cancel();
	}
	{
		PositionCastFixtureB12 f(world, "cPositionGate");
		auto* constant = const_cast<NDb::UnitConstant*>(f.db->constants->vars[0].GetPtr());
		constant->var.sString = authored;
		const Target target(CVec3(15, 5, 0));
		check(f.limitation->Check(CastLimitationsCheckParams(*f.talent, target)) == f.talent.GetPtr() && f.talent->samples.size() == 1,
			"named constants inherit the custom position context and its lazy nature binding");
		constant->var.sString = "cPositionGate";
		check(!f.limitation->Check(CastLimitationsCheckParams(*f.talent, target)),
			"position-context constants retain the existing checked cycle guard");
	}
	for (const CVec3& point : {CVec3(-1, 5, 0), CVec3(41, 5, 0), CVec3(15, 31, 0),
		CVec3(std::numeric_limits<float>::max(), 5, 0), CVec3(std::numeric_limits<float>::infinity(), 5, 0),
		CVec3(15, std::numeric_limits<float>::quiet_NaN(), 0), CVec3(15, 5, std::numeric_limits<float>::infinity())})
	{
		PositionCastFixtureB12 f(world, authored); f.talent->Activate();
		check(!f.hero->UseTalent(f.talent, Target(point)) && f.hero->GetMana() == 200 && f.talent->samples.empty(),
			"invalid or out-of-map geometry is rejected before nature lookup or paid mutation");
	}
	{
		PositionCastFixtureB12 f(world, "true"); const Target target(CVec3(15, 5, 0));
		CastLimitationsCheckParams cp(*f.talent, target); cp.pAbility = nullptr;
		check(!f.limitation->Check(cp), "missing position ability is rejected without dereference");
		cp.pAbility = f.talent; cp.pTarget = nullptr;
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
			check(!f.hero->UseTalent(f.talent, Target(target.GetPtr())) && f.hero->GetMana() == 200,
				"foreign-world unit targets cannot bypass position admission with a literal condition");
			target->Cleanup();
		}
	}
	{
		// Observe, rather than conceal, the separate native terrain runtime boundary.
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		NDb::Ptr<NDb::Ability> db = new NDb::Ability;
		CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(ability->GetNatureTypeInPos(CVec2(15, 5)) == TERR_N,
			"unmodified native nature runtime currently reports neutral; mock lookup does not claim a live grid port");
		const auto wrongContext = LinuxBootstrap::EvaluateUnitNumericFormula(authored, hero.GetPtr(), hero.GetPtr(), ability);
		check(!wrongContext.Succeeded(), "unit formula context cannot masquerade as the custom position receiver");
		hero->Cleanup();
	}
}
