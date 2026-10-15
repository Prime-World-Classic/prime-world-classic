/** Only expose malformed serialized ancestry for tests; scale still uses the original recursive getter. */
struct ParentScaleProbeB13 : PFBaseApplicator
{
	explicit ParentScaleProbeB13(const PFApplCreatePars& pars) : PFBaseApplicator(pars) {}
	void Parent(PFBaseApplicator* parent) { pParent = parent; }
};

/** Distinct real periodic counters prove parent navigation, root fallback and live reevaluation. */
static void ParentScaleB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	PeriodicFixtureB13 f(world); NDb::Ptr<NDb::BaseApplicator> periodic = f.Descriptor();
	auto root = f.Apply(check, periodic);
	auto middle = f.Create(periodic, root.GetPtr());
	check(middle && f.Activate(middle), "B13 scale chain retains a second real periodic parent");
	if (!root || !middle) return;
	root->Step(3);
	NDb::Ptr<NDb::BaseApplicator> leafDb = new NDb::HealApplicator;
	auto leaf = f.Create(leafDb, middle.GetPtr());
	check(leaf != nullptr, "B13 scale fixture uses an actual heal applicator beneath two periodic parents");
	if (!leaf) return;
	for (int iteration = 0; iteration < 3; ++iteration)
	{
		struct Expression { const char* text; float expected; };
		for (const auto& expression : {Expression{"mScale", 1}, Expression{"mParentScale", 1},
			Expression{"pMisc->GetScale()", 1}, Expression{"pMisc->GetParentScale()", 1},
			Expression{"appl.GetScale()", 1}, Expression{"appl.GetParentScale()", 1},
			Expression{"mObjectParent->GetScale()", 1},
			Expression{"mObjectParent->GetObjectParent()->GetScale()", float(iteration + 2)},
			Expression{"pMisc->GetObjectParent()->GetObjectParent()->GetScale()", float(iteration + 2)},
			Expression{"appl.GetObjectParent()->GetObjectParent()->GetScale()", float(iteration + 2)}})
		{
			const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression.text, *leaf);
			check(value.Succeeded() && value.value == expression.expected, "B13 read-only scale formulas navigate real parents without substituting the leaf or its ability");
		}
		const auto rootParent = LinuxBootstrap::EvaluateApplicatorNumericFormula("mObjectParent->GetScale()", *root);
		const auto rootParentScale = LinuxBootstrap::EvaluateApplicatorNumericFormula("mParentScale", *root);
		check(rootParent.Succeeded() && rootParent.value == iteration + 2 && rootParentScale.Succeeded() && rootParentScale.value == 1,
			"B13 root GetObjectParent returns self, while the distinct GetParentScale getter retains its original one fallback");
		root->Step(3);
	}
	for (const char* expression : {"mScale", "mParentScale", "mObjectParent->GetObjectParent()->GetScale()"})
	{
		const auto ability = LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver, f.ability);
		check(ability.Succeeded() && ability.value == 1, "B13 original ability context retains its own scale and self-parent behavior");
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver).Succeeded(), "B13 selected scale formulas reject absent miscellaneous context");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + expression, f.sender, f.receiver);
		check(lazy.Succeeded() && lazy.value == 7, "B13 valid unselected scale formulas do not demand a missing context");
		const auto selected = LinuxBootstrap::EvaluateSelectorConditionFormula(expression, f.sender, f.receiver, leaf.GetPtr());
		check(selected.Succeeded(), "B13 selector conditions retain original applicator scale context");
	}
	for (const char* expression : {"mObjectParent", "mObjectParent->GetScale(1)", "pMisc->GetObjectParent(1)->GetScale()",
		"mObjectParent->GetLife()", "mObjectParent->Stop()", "mObjectParent->GetObjectParent()->GetScale()()",
		"__misc_parent_scale_2", "appl.GetScale(1)", "pMisc->GetParentScale(0)"})
	{
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *leaf).Succeeded(), "B13 parent-scale support does not expose arbitrary object methods or reserved symbols");
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("true ? 1 : ") + expression, *leaf).Succeeded(),
			"B13 invalid object syntax remains invalid in an unselected branch");
	}
	std::string tooDeep = "pMisc";
	for (int i = 0; i < 17; ++i) tooDeep += "->GetObjectParent()";
	tooDeep += "->GetScale()";
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(tooDeep, *leaf).Succeeded(), "B13 formula parent traversal is syntactically bounded");
	std::string maximum = "pMisc";
	for (int i = 0; i < 16; ++i) maximum += "->GetObjectParent()";
	maximum += "->GetScale()";
	const auto maximumResult = LinuxBootstrap::EvaluateApplicatorNumericFormula(maximum, *leaf);
	check(maximumResult.Succeeded() && maximumResult.value == 5, "B13 maximum explicit parent depth preserves the original root self fallback");
	NRandom::RandomGenerator expected(*world->GetRndGen()); const int random = expected.Next(0, 10);
	const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(0,10)+mObjectParent->GetObjectParent()->GetScale()", *leaf);
	check(value.Succeeded() && value.value == random + 5 && world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
		"B13 successful parent reads commit the original seeded random expression once");
	root->Stop();
	const auto stopped = LinuxBootstrap::EvaluateApplicatorNumericFormula("mObjectParent->GetObjectParent()->GetScale()", *leaf);
	check(stopped.Succeeded() && stopped.value == 5, "B13 retained stopped parents remain readable as original historical formula context");
	std::printf("B13 parent scales checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Malformed ancestry must fail before invoking a recursively defined engine scale getter. */
static void ParentScaleSafetyB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	PeriodicFixtureB13 f(world); NDb::Ptr<NDb::BaseApplicator> db = new NDb::HealApplicator;
	const Target target(f.receiver.GetPtr()); PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db;
	for (int length : {1, 2, 16, 128, 129})
	{
		std::vector<CObj<ParentScaleProbeB13>> chain;
		for (int i = 0; i < length; ++i)
		{
			CObj<ParentScaleProbeB13> app = new ParentScaleProbeB13(pars);
			check(app->Init(), "B13 serialized ancestry fixture initializes original applicator context");
			if (!chain.empty()) app->Parent(chain.back());
			chain.push_back(app);
		}
		for (bool cycle : {false, true})
		{
			chain.front()->Parent(cycle ? chain.back().GetPtr() : nullptr);
			for (const char* expression : {"mScale", "mParentScale", "mObjectParent->GetObjectParent()->GetScale()"})
			{
				NRandom::RandomGenerator expected(*world->GetRndGen());
				const bool valid = !cycle && length <= 128;
				const int random = valid ? expected.Next(1, 3) : 0;
				const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(1,3)+") + expression, *chain.back());
				check(result.Succeeded() == valid && (!valid || result.value == random + 1),
					"B13 cyclic or excessive ancestry cannot enter recursive original scale getters");
				check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 rejected parent reads roll back earlier formula randomness");
			}
		}
		chain.front()->Parent(nullptr);
	}
	for (bool overflow : {false, true})
	{
		auto* raw = f.Descriptor(overflow ? "1e38" : "2", overflow ? "0" : "-3");
		raw->scaleMode = NDb::SCALECALCULATIONMODE_SCALEISTIME;
		NDb::Ptr<NDb::BaseApplicator> periodic = raw; auto parent = f.Apply(check, periodic);
		auto leaf = f.Create(db, parent.GetPtr()); if (!parent || !leaf) continue;
		if (overflow) for (int i = 0; i < 4; ++i) parent->Step(1.1e38f);
		check(overflow ? !std::isfinite(f.Scale(*parent)) : f.Scale(*parent) == -3,
			"B13 actual periodic time scale supplies signed and overflowing getter values without replacing engine arithmetic");
		for (const char* expression : {"mScale", "mParentScale", "mObjectParent->GetScale()"})
		{
			NRandom::RandomGenerator expected(*world->GetRndGen()); const int random = overflow ? 0 : expected.Next(0, 10);
			const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(0,10)+") + expression, *leaf);
			check(result.Succeeded() == !overflow && (overflow || result.value == random - 3),
				"B13 finite negative scale is preserved while nonfinite recursive results fail");
			check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 invalid scale results cannot commit earlier random draws");
		}
	}
	std::printf("B13 parent scale safety checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** The shipped energy potion uses grandparent scale inside an original constant, not the periodic strike count. */
static void ParentScalePotionB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto potion = NDb::Get<NDb::Consumable>(NDb::DBID("/Items/Consumables/EnergyPotion.ARCT.xdb"));
		check(potion && potion->applicators.size() == 1, "B13 original energy potion retains its complete parent status and timer chain");
		if (potion)
		{
			B8BehaviourForeignWorld isolated(check);
			if (isolated.world)
			{
				auto hero = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
				hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				hero->GetStat(NDb::STAT_ENERGY)->SetCoreValue(1000); isolated.world->Step(.01f, .01f);
				hero->ChangeMana(100 - hero->GetMana());
				check(hero->GetMaxEnergy() == 1000 && hero->GetEnergyRegen() == 0 && hero->GetEnergyRegenAbs() == 0,
					"B13 mana fixture has sufficient uncapped pool and no passive regeneration");
				check(hero->TakeConsumable(potion, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "B13 actual energy potion enters original inventory");
				CObj<PFAbilityInstance> cast = hero->UseConsumable(0, Target(hero.GetPtr()), false);
				check(cast != nullptr, "B13 original energy-potion ability is admitted without descriptor replacement");
				for (int i = 0; i < 4; ++i) isolated.world->Step(.01f, .01f);
				check(hero->GetMana() == 140 && hero->GetConsumable(0) && hero->GetConsumable(0)->GetQuantity() == 1,
					"B13 energy potion consumes one charge and heals the authored forty mana at its initial tick");
				for (int i = 0; i < 15; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetMana() == 140, "B13 energy potion does not replace its authored two-second period with per-step healing");
				for (int i = 0; i < 10; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetMana() == 180, "B13 grandparent scale stays one instead of increasing with the periodic count");
				for (int i = 0; i < 180; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetMana() == 500 && cast && cast->GetActiveApplicatorsCount() == 0,
					"B13 shipped energy potion delivers ten authored ticks and releases its complete status chain");
				for (int i = 0; i < 20; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetMana() == 500, "B13 expired energy potion cannot restore further mana");
				hero->Cleanup();
			}
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored parent-scale potion checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
