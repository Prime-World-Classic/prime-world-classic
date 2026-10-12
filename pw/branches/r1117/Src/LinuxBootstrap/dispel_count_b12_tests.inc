/** Authored dispel limits use checked integer conversion and real distinct status groups. */
static void DispelCountB12(Checks& check, PFWorld* world)
{
	struct Case { const char* expression; int removed; bool compiled = false; };
	for (const auto& test : {Case{"", 1}, Case{"1", 1}, Case{"1 + 1", 2}, Case{"sLife / 500", 2},
		Case{"2.9", 2}, Case{"3", 3}, Case{"0", 0}, Case{"-2", 0}, Case{"1 / 0", 0}, Case{"cMissing", 0},
		Case{"2147483648", 0}, Case{"1e39", 0}, Case{"", 0, true}, Case{"true ? 2 : cMissing", 2}})
	{
		StatusStackFixtureB12 f(world); f.other->ChangeFaction(NDb::FACTION_BURN);
		for (const char* name : {"/Mock/DispelCountA.xdb", "/Mock/DispelCountB.xdb", "/Mock/DispelCountC.xdb"})
		{
			auto* status = StatusStackFixtureB12::Db(name, "3"); status->dispellPriority = NDb::DISPELLPRIORITY_HIGH;
			NDb::Ptr<NDb::StatusApplicator> db = status; f.Begin(check, db, 1);
		}
		auto* dispel = new NDb::DispellApplicator; dispel->maxEffectsToDispell.sString = test.expression;
		if (test.compiled) dispel->maxEffectsToDispell.compiledString = "not-native";
		vector<NDb::Ptr<NDb::BaseApplicator>> effects; effects.push_back(dispel);
		CreateAndActivateApplicators(effects, f.instance[0], Target(f.hero.GetPtr()), nullptr);
		if (f.Delta() != 3 * (3 - test.removed)) std::printf("Dispel count case: %s expected=%d delta=%g\n", test.expression, test.removed, f.Delta());
		check(f.Delta() == 3 * (3 - test.removed), "checked authored dispel count removes exactly the admitted distinct groups");
		check(f.instance[1]->GetActiveApplicatorsCount() == 2 * (3 - test.removed)
			&& f.instance[0]->GetActiveApplicatorsCount() == 0, "dispel count conversion preserves balanced status child ownership");
	}
	{
		struct MissingSelector : NDb::MultipleTargetSelector
		{
			PFTargetSelector* Create(PFWorld*) const override { return nullptr; }
		};
		StatusStackFixtureB12 f(world); f.other->ChangeFaction(NDb::FACTION_BURN);
		NDb::Ptr<NDb::StatusApplicator> db = StatusStackFixtureB12::Db("/Mock/UnsupportedDispelSelector.xdb", "3");
		f.Begin(check, db, 1);
		auto* dispel = new NDb::DispellApplicator; dispel->maxEffectsToDispell.sString = "1"; dispel->targets = new MissingSelector;
		vector<NDb::Ptr<NDb::BaseApplicator>> effects; effects.push_back(dispel);
		CreateAndActivateApplicators(effects, f.instance[0], Target(f.hero.GetPtr()), nullptr);
		check(f.Delta() == 3 && f.instance[1]->GetActiveApplicatorsCount() == 2 && f.instance[0]->GetActiveApplicatorsCount() == 0,
			"unsupported dispel selector cannot dereference a null factory or remove fallback effects");
	}
}
