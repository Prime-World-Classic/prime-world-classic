/** The shipped Plane attack must read its actual sender's purchased talent, not a range fallback. */
static void TalentFormulaB13(Checks& check, PFWorld* world)
{
	const int before=check.count, failed=check.failed;
	auto hero=CommandHeroB10(world), receiver=CommandHeroB10(world);
	NDb::Talent* talent=new NDb::Talent;
	talent->persistentId="Plane_A4_2"; talent->naftaCost=0; talent->manaCost.sString="0";
	hero->InstallTalent(talent,false); receiver->InstallTalent(talent,true);
	hero->GetStat(NDb::STAT_RANGE)->SetCoreValue(14);
	const char* authored="sRange + (tal(\"Plane_A4_2\") ? 2 : 0)";
	const auto run=[&](const char* source,float expected) {
		const auto result=LinuxBootstrap::EvaluateUnitNumericFormula(source,hero.GetPtr(),receiver.GetPtr());
		check(result.Succeeded() && result.value==expected,source);
	};
	run(authored,14); run("tal(\"missing\")",0); run("tal(\"\")",0);
	check(hero->GetTalent(0,0)->Activate(),"B13 formula fixture purchases the real sender talent");
	run(authored,16); run("tal(\"Plane_A4_2\")+tal(\"Plane_A4_2\")",2);
	run("tal(\"plane_a4_2\")",0); run("tal ( \"Plane_A4_2\" )",1);
	run("tal(\"__talent_0\")",0); run("tal(\"missing);1+1\")",0);
	std::string many;
	for (unsigned i=0;i<LinuxBootstrap::NumericFormulaMaxSymbols;++i)
	{
		if (!many.empty()) many += "+";
		many += "tal(\"missing_"+std::to_string(i)+"\")";
	}
	run(many.c_str(),0);
	many += "+tal(\"one_more\")";
	check(!LinuxBootstrap::EvaluateUnitNumericFormula(many,hero.GetPtr(),nullptr).Succeeded(),
		"B13 talent bindings retain the scalar symbol bound");
	for (const char* source : {"tal()","tal(1)","tal(\"Plane_A4_2\",1)","tal(\"Plane_A4_2\")()",
		"tal('Plane_A4_2')","tal(\"Plane\\x41\")","tal(\"Plane\" \"_A4_2\")","__talent_0",
		"1 ? 3 : __talent_0","talconst(\"Plane_A4_2\",\"range\")","mastertal(\"Plane_A4_2\")"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(source,hero.GetPtr(),receiver.GetPtr()).Succeeded(),
			"B13 malformed or unsupported talent expression fails closed");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula(authored,nullptr,receiver.GetPtr()).Succeeded(),
		"B13 talent formula never substitutes the receiver for a missing sender");
	auto unit=Unit(world,NDb::FACTION_FREEZE,40);
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("tal(\"missing\")",unit.GetPtr(),hero.GetPtr()).Succeeded(),
		"B13 talent formula avoids the nonhero dummy getter");
	const auto lazy=LinuxBootstrap::EvaluateUnitNumericFormula("1 ? 3 : tal(\"missing\")",nullptr,nullptr);
	check(lazy.Succeeded() && lazy.value==3,"B13 unselected talent query needs no sender");
	NDb::BaseAttack* attackDb=B11AttackDescriptor(0); attackDb->useRange.sString=authored;
	NDb::UnitConstantsContainer* constants=new NDb::UnitConstantsContainer;
	NDb::UnitConstant* constant=new NDb::UnitConstant;
	constant->name="TalentRange"; constant->var.sString=authored; constants->vars.push_back(constant);
	attackDb->constants=constants;
	CObj<PFBaseAttackData> attack=new PFBaseAttackData(hero.GetPtr(),attackDb);
	hero->ReplaceBaseAttack(attack,false);
	check(attack->GetUseRange(receiver.GetPtr())==16,"B13 actual attack data evaluates the purchased authored range");
	const auto nested=LinuxBootstrap::EvaluateUnitNumericFormula("cTalentRange+tal(\"Plane_A4_2\")",
		hero.GetPtr(),receiver.GetPtr(),attack.GetPtr());
	check(nested.Succeeded() && nested.value==17,"B13 recursive constants own separate talent binding tables");
	hero->GetTalentsSet()->CleanSet();
	run(authored,14);
	check(attack->GetUseRange(receiver.GetPtr())==14,"B13 actual attack data sees removed talents without stale cached values");
	std::printf("Talent formula B13: checks=%d failed=%d\n",check.count-before,check.failed-failed);
}
