/** FormulaPars nt is a lazy two-argument macro, not eager SwitchByBool. */
static void TerrainFormulaNumericB13(Checks& check)
{
	using namespace LinuxBootstrap;
	for (bool native : {false, true})
	{
		int terrainReads = 0, factionReads = 0, selectedReads = 0, otherReads = 0;
		const auto resolver = [&](const std::string& name, double& value) {
			if (name == "sTerrainType") { ++terrainReads; value = native ? 1 : 2; return true; }
			if (name == "sFctn") { ++factionReads; value = 1; return true; }
			if (name == "yes") { ++selectedReads; value = 7; return true; }
			if (name == "no") { ++otherReads; value = 3; return true; }
			return false;
		};
		const auto result = EvaluateNumericFormula("nt(yes, no) + nt(yes, no)", resolver);
		check(result.Succeeded() && result.value == (native ? 14 : 6), "nt selects using sender terrain versus sender faction");
		check(terrainReads == 1 && factionReads == 1 && selectedReads == (native ? 1 : 0) && otherReads == (native ? 0 : 1),
			"nt preserves per-evaluation symbol snapshot and does not resolve its unused branch");
		const char* lazy[] = {native ? "nt(7, 1/0)" : "nt(1/0, 7)",
			native ? "nt(7, missing)" : "nt(missing, 7)", native ? "nt(7, sqrt(-1))" : "nt(sqrt(-1),7)",
			native ? "nt(7, 1e39)" : "nt(1e39,7)", native ? "nt(nt(7, 1/0), missing)" : "nt(missing,nt(1/0,7))"};
		for (const char* expression : lazy)
		{
			const auto value = EvaluateNumericFormula(expression, resolver);
			check(value.Succeeded() && value.value == 7, "only the selected nt branch evaluates symbols or arithmetic");
		}
		const char* invalid[] = {native ? "nt(1/0,7)" : "nt(7,1/0)", native ? "nt(missing,7)" : "nt(7,missing)",
			native ? "nt(1e39,7)" : "nt(7,1e39)", "nt()", "nt(1)", "nt(1,2,3)", "nt(,2)", "nt(1,)",
			"nt((1,2),3)", "nt(1, unknown(2))", "nt(1,2+)", "nt(1,2);", "nt(1,0x10)", "nt(1,2=3)"};
		for (const char* expression : invalid)
		{
			const auto value = EvaluateNumericFormula(expression, resolver);
			check(!value.Succeeded() && std::isnan(value.value), "bad nt arity syntax selected dependency or arithmetic fails without a fallback");
		}
		struct Case { const char* text; float yes, no; };
		for (const Case& c : {Case{"2*nt(1+2,4+5)+1", 7,19}, Case{"nt(0?1:4,1?6:2)",4,6},
			Case{"max(nt(2,3),nt(4,1))",4,3}, Case{"nt(1&&2,0||3)",1,1}, Case{"!nt(0,2)",1,0},
			Case{"nt(1,2)<2 == true",1,0}, Case{"nt(1?nt(4,5):6,8)",4,8}})
		{
			const auto value = EvaluateNumericFormula(c.text, resolver);
			check(value.Succeeded() && value.value == (native ? c.yes : c.no), "nt grouping composes with existing numeric logical and conditional grammar");
		}
	}
	for (const char* expression : {"true ? 7 : nt(1/0,missing)", "false && nt(1,2)", "true || nt(1,2)"})
		check(EvaluateNumericFormula(expression).Succeeded(), "skipped entire nt needs no sender resolver");
	check(!EvaluateNumericFormula("nt(1,2)").Succeeded(), "selected nt cannot synthesize an absent sender context");
	check(!EvaluateNumericFormula("true ? 7 : nt(1,2,3)").Succeeded(), "nt arity is validated even in a skipped outer branch");
	std::string expression = "7";
	for (std::size_t i = 0; i < NumericFormulaMaxNesting + 1; ++i) expression = "nt(" + expression + ",0)";
	check(EvaluateNumericFormula(expression).error == NumericFormulaError::LimitExceeded, "nested nt remains subject to the existing nesting budget");
	const auto neutral = EvaluateNumericFormula("nt(7,3)", [](const std::string&, double& value) { value = 0; return true; });
	check(neutral.Succeeded() && neutral.value == 7, "original equality also regards neutral sender on neutral terrain as native");
	const auto native = [](const std::string&, double& value) { value = 1; return true; };
	expression = "7";
	for (std::size_t i = 0; i < NumericFormulaMaxNesting; ++i) expression = "nt(" + expression + ",0)";
	check(EvaluateNumericFormula(expression, native).value == 7, "exact nt parenthesis and conditional budget is supported");
	expression = "nt(1,0)";
	for (std::size_t i = 1; i < NumericFormulaMaxNesting; ++i) expression += "+nt(1,0)";
	check(EvaluateNumericFormula(expression, native).value == NumericFormulaMaxNesting, "flat nt calls share the existing total conditional budget");
	check(EvaluateNumericFormula(expression + "+nt(1,0)", native).error == NumericFormulaError::LimitExceeded, "flat nt calls cannot evade the conditional limit");
	expression = "nt(1,0)";
	for (std::size_t i = 0; i < NumericFormulaMaxSymbols - 2; ++i) expression += "+v" + std::to_string(i);
	check(EvaluateNumericFormula(expression, native).value == NumericFormulaMaxSymbols - 1, "nt condition getters count toward the symbol budget");
	check(EvaluateNumericFormula(expression + "+extra", native).error == NumericFormulaError::LimitExceeded, "injected nt getters cannot evade symbol limits");
	for (bool badTerrain : {false, true})
	{
		const auto invalid = EvaluateNumericFormula("nt(1,2)", [&](const std::string& name, double& value) {
			value = (name == "sTerrainType") == badTerrain ? std::numeric_limits<double>::infinity() : 1; return true;
		});
		check(!invalid.Succeeded() && std::isnan(invalid.value), "nonfinite selected nt condition getter cannot choose a fallback branch");
	}
}

/** Corrupt only the original serialized nature cache, without a replacement getter. */
struct TerrainFormulaCacheRestoreB13 : B10ActivationTimerRestore
{
	int nature;
	explicit TerrainFormulaCacheRestoreB13(int nature) : nature(nature) {}
	void DataChunk(chunk_id id, void* data, int size, int) override
	{
		if (id == 7 && size == sizeof(int)) std::memcpy(data, &nature, sizeof(nature));
	}
};

/** Expose lifecycle hooks only; all modifier evaluation and ownership remain engine code. */
struct TerrainFormulaStatB13 : StatBuffProbeB12
{
	OBJECT_METHODS(StatBuffProbeB12::typeId, TerrainFormulaStatB13);
public:
	TerrainFormulaStatB13() = default;
	explicit TerrainFormulaStatB13(const PFApplCreatePars& pars) : StatBuffProbeB12(pars) {}
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
};

/** Actual hero cache, real modifiers, C5 factory/grid overrides, and paid casting. */
static void TerrainFormulasB13(Checks& check)
{
	using namespace LinuxBootstrap;
	TerrainFormulaNumericB13(check);
	{
		NatureOverrideFixtureB13 f(check);
		check(f.hero->TeleportTo(CVec2(10,0)), "sender can sample a different authored cell from the receiver");
		f.hero->UpdateNatureType();
		const auto separate = EvaluateUnitNumericFormula("100*sTerrainType+10*rTerrainType+tTerrainType", f.hero, f.observer, f.ability);
		check(separate.Succeeded() && separate.value == 122, "sender and receiver terrain bindings do not alias each other");
		for (int nature : {-1, 3, std::numeric_limits<int>::max()})
		{
			TerrainFormulaCacheRestoreB13 restore(nature); f.hero->PFLogicObject::operator&(restore);
			check(f.hero->GetTerrainType() == nature, "fixture restores only the actual serialized terrain cache");
			for (const char* expression : {"sTerrainType", "nt(1,2)", "sTerrainType == sFctn"})
			{
				const auto value = EvaluateUnitNumericFormula(expression, f.hero, f.observer, f.ability);
				check(!value.Succeeded() && std::isnan(value.value), "invalid cached terrain is rejected before a comparison or nt can hide it");
			}
			check(EvaluateUnitNumericFormula("true ? 7 : sTerrainType", f.hero, f.observer, f.ability).value == 7, "unselected corrupt terrain is not read");
		}
		check(f.hero->TeleportTo(CVec2(25,10)), "terrain formula sender moves through original unit positioning");
		f.hero->UpdateNatureType();
		for (const char* name : {"sTerrainType", "rTerrainType", "tTerrainType"})
		{
			const auto value = EvaluateUnitNumericFormula(name, f.hero, f.observer, f.ability);
			check(value.Succeeded() && value.value == TERR_B, "s/r/t terrain identifiers read actual cached unit terrain");
		}
		for (const char* expression : {"sTerrainType", "rTerrainType", "tTerrainType", "nt(1,2)"})
			check(!EvaluateUnitNumericFormula(expression, nullptr, nullptr).Succeeded(), "selected missing terrain unit fails closed");
		for (const char* expression : {"false ? sTerrainType : 7", "true ? 7 : rTerrainType", "true || nt(1,2)"})
			check(EvaluateUnitNumericFormula(expression, nullptr, nullptr).Succeeded(), "unselected terrain lookup retains missing-context laziness");
		for (const auto& symbol : {std::pair<const char*, int>("TERR_N", TERR_N), {"TERR_A", TERR_A}, {"TERR_B", TERR_B}})
		{
			const auto value = EvaluateUnitNumericFormula(symbol.first, nullptr, nullptr);
			check(value.Succeeded() && value.value == symbol.second, "authored terrain enums retain their exact FormulaPars values");
		}
		for (const char* invalid : {"TerrainType", "sTerrain", "sTerrainType()", "sender.GetTerrainType()", "TERR_C"})
			check(!EvaluateUnitNumericFormula(invalid, f.hero, f.observer, f.ability).Succeeded(), "terrain bindings do not open a general member or inferred-name API");
		const auto evaluate = [&] { return EvaluateUnitNumericFormula("nt(11,22)", f.hero, f.observer, f.ability); };
		auto* cycle = new NDb::UnitConstant; cycle->name = "TerrainCycle"; cycle->var.sString = "nt(cTerrainCycle,7)";
		const_cast<NDb::UnitConstantsContainer*>(f.abilityDb->constants.GetPtr())->vars.push_back(cycle);
		CObj<PFAbilityData> recursive = new PFAbilityData(f.hero.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(EvaluateUnitNumericFormula("cTerrainCycle", f.hero, f.observer, recursive).value == 7, "nt skips an unselected recursive constant dependency");
		check(evaluate().Succeeded() && evaluate().value == 22, "nt selects sender other terrain even though receiver is on its native faction");
		auto overrideApp = f.Apply(f.Descriptor("sFctn", "2", "1"));
		check(f.Map()->GetNatureInPoint(25,10) == TERR_A && evaluate().value == 22, "immediate grid mutation does not bypass the original unit cache cadence");
		f.World()->Step(.5f,.5f);
		check(f.hero->GetTerrainType() == TERR_A && evaluate().Succeeded() && evaluate().value == 11, "world stepping publishes cached terrain to a new formula evaluation");
		check(!EvaluateUnitNumericFormula("cTerrainCycle", f.hero, f.observer, recursive).Succeeded(), "the same constant fails closed when terrain selects its cycle");
		f.World()->Step(.5f,.5f);
		check(!overrideApp->IsAlive() && f.hero->GetTerrainType() == TERR_B && evaluate().value == 22, "override expiry restores authored terrain formula branch through world stepping");
		check(EvaluateUnitNumericFormula("cTerrainCycle", f.hero, f.observer, recursive).value == 7, "failed recursive terrain constant is not cached across evaluations");
		NDb::PositionCastLimitation limit;
		const Target position(CVec3(10,0,0)); const CastLimitationsCheckParams cp(*f.ability, position);
		limit.condition.sString = "nt(false, true)";
		check(limit.Check(cp) == f.ability.GetPtr(), "nt in a position condition still uses the actual sender cache");
		limit.condition.sString = "rTerrainType == sTerrainType";
		check(!limit.Check(cp), "a position receiver cannot be silently substituted by the sender unit");
	}
	{
		NatureOverrideFixtureB13 f(check);
		f.hero->TeleportTo(CVec2(25,10)); f.hero->UpdateNatureType();
		auto* raw = new NDb::StatModApplicator;
		raw->enabled.sString = "true"; raw->startCondition.sString = "true"; raw->lifeTime.sString = "-1";
		raw->modifier.stat = NDb::STAT_STRENGTH; raw->modifier.multValue.sString = "1";
		raw->modifier.addValue.sString = "nt(5,2)";
		NDb::Ptr<NDb::StatModApplicator> db = raw;
		auto* stat = f.observer->GetStat(NDb::STAT_STRENGTH); stat->SetBaseValue(10);
		const int originalCount = stat->GetModifiers().size();
		const Target receiver(f.observer.GetPtr());
		PFApplCreatePars pars(f.cast, receiver); pars.pDBAppl = db; pars.pWorld = f.World();
		CObj<TerrainFormulaStatB13> app = new TerrainFormulaStatB13(pars);
		check(app->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), f.cast), "terrain stat fixture uses actual activation and owner notifications");
		f.effects.push_back(CObj<PFBaseApplicator>(app.GetPtr()));
		check(app->IsAlive() && stat->GetValue() == 12, "real dynamic stat buff activates with sender terrain branch");
		f.World()->GetRndGen()->SetSeed(831); NRandom::RandomGenerator expected(831);
		check(!EvaluateApplicatorNumericFormula("nt(7,random(3,6)+1/0)", *app).Succeeded(), "selected failing nt random branch remains a checked error");
		check(f.World()->GetRndGen()->Next() == expected.Next(), "failed nt evaluation rolls back its staged RNG before any caller mutation");
		const auto random = EvaluateApplicatorNumericFormula("nt(random(1,2),random(3,6))", *app);
		check(random.Succeeded() && random.value == expected.Next(3,6) && f.World()->GetRndGen()->Next() == expected.Next(), "nt consumes only the selected original engine RNG branch");
		auto terrain = f.Apply(f.Descriptor("sFctn", "2", ".3"));
		f.World()->Step(.1f,.1f);
		check(stat->GetValue() == 15 && stat->GetModifiers().size() == originalCount + 1, "dynamic stat buff follows changed cached terrain without duplicating ownership");
		raw->modifier.addValue.sString = "nt(1/0,2)";
		f.World()->Step(.1f,.1f);
		check(stat->GetValue() == 15, "invalid selected terrain branch retains the last valid modifier");
		f.World()->Step(.2f,.2f);
		check(stat->GetValue() == 12, "dynamic buff recovers when invalid arithmetic becomes an unselected branch");
		raw->enabled.sString = "sTerrainType == sFctn"; f.World()->Step(.1f,.1f);
		check(!app->IsEnabled() && stat->GetValue() == 10, "authored terrain condition disables the existing modifier");
		terrain = f.Apply(f.Descriptor("sFctn", "2", "10"), true); raw->modifier.addValue.sString = "nt(5,2)";
		app->Block(); f.World()->Step(.1f,.1f);
		check(!app->IsEnabled() && stat->GetValue() == 10, "blocked terrain buff remains inactive after native terrain resumes");
		app->Unblock();
		if (!app->IsEnabled() || stat->GetValue() != 15)
			std::printf("Terrain unblock: alive=%d enabled=%d blocked=%d value=%g cached=%d grid=%d override=%d sender=(%g,%g) receiver=(%g,%g)\n", app->IsAlive(), app->IsEnabled(), app->IsBlocked(), stat->GetValue(), f.hero->GetTerrainType(), f.Map()->GetNatureInPoint(25,10), terrain->IsAlive(), f.hero->GetPosition().x, f.hero->GetPosition().y, f.observer->GetPosition().x, f.observer->GetPosition().y);
		check(app->IsEnabled() && stat->GetValue() == 15, "original unblock enables with current checked terrain coefficients");
		f.World()->Step(.1f,.1f);
		check(app->IsEnabled() && stat->GetValue() == 15, "following Step keeps the modifier enabled under its authored terrain condition");
		app->Finish();
		check(stat->GetValue() == 10 && stat->GetModifiers().size() == originalCount, "terrain formula buff removes only its own modifier");
	}
	for (bool native : {false, true}) for (bool invalid : {false, true})
	{
		NatureOverrideFixtureB13 f(check);
		f.hero->TeleportTo(CVec2(25,10)); f.hero->UpdateNatureType();
		if (native) { f.Apply(f.Descriptor("sFctn", "2", "10")); f.World()->Step(.1f,.1f); }
		auto* db = new NDb::Talent; db->naftaCost = 0; db->useRange.sString = "0"; db->requireLineOfSight = false;
		db->manaCost.sString = invalid ? "nt(1/0,80)" : "nt(0,80)";
		db->cooldownTime.sString = "nt(3,7)"; db->cooldownTimeSecondState.sString = "0";
		NDb::Ptr<NDb::Talent> talentDb = db;
		CObj<PFTalent> talent = new PFTalent(f.hero.GetPtr(), talentDb, 1, 0, 0, false); talent->Activate();
		const float mana = f.hero->GetMana();
		CObj<PFAbilityInstance> paid = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		const bool rejected = invalid && native;
		check(bool(paid) == !rejected && f.hero->GetMana() == mana - (native ? 0 : 80), "actual paid cast evaluates only selected terrain cost before resource mutation");
		check(talent->GetCurrentCooldown() == (rejected ? 0 : native ? 3 : 7), "terrain cooldown branch starts only for a valid admitted cast");
	}
}

/** Read only the owned authored fields with their original executable-string serializers. */
struct TerrainFormulaDescriptorB13
{
	NDb::Ability* db;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("manaCost", &db->manaCost);
		saver.Add("cooldownTime", &db->cooldownTime);
		return 0;
	}
};

/** Actual shipped cost/cooldown consumers, without loading unrelated hero-kit resources. */
static void TerrainFormulasAuthoredB13(Checks& check, const char* dataRoot)
{
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Assassin/Ability_A3.TALENT.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "shipped Assassin talent XML is readable");
		if (xml && xml->IsOk())
		{
			auto* raw = new NDb::Ability; NDb::Ptr<NDb::Ability> db = raw;
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			TerrainFormulaDescriptorB13 descriptor = {raw}; saver->Add("Talent", &descriptor);
			check(db->manaCost.sString == "nt( 0, 80 )" && db->cooldownTime.sString == "15", "original serializers retain shipped terrain cost and cooldown fields");
			NatureOverrideFixtureB13 f(check);
			f.hero->TeleportTo(CVec2(25,10)); f.hero->UpdateNatureType();
			CObj<PFAbilityData> ability = new PFAbilityData(f.hero.GetPtr(), db.GetPtr(), NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(ability->GetManaCost() == 80 && ability->IsEnoughMana(), "actual authored ability quotes other-terrain mana without fallback");
			ability->SpendMana();
			check(f.hero->GetMana() == 120, "actual authored ability spends its other-terrain cost");
			auto terrain = f.Apply(f.Descriptor("sFctn", "2", ".2")); f.World()->Step(.1f,.1f);
			check(ability->IsEnoughMana() && ability->GetManaCost() == 0, "existing authored ability refreshes its quote after native terrain resumes");
			const float mana = f.hero->GetMana(); ability->SpendMana();
			check(f.hero->GetMana() == mana, "authored native-terrain free cast does not consume mana");
			f.World()->Step(.1f,.1f);
			check(ability->IsEnoughMana() && ability->GetManaCost() == 80, "authored cost returns after temporary terrain removal");
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
}
