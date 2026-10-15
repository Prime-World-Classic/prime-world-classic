/** Missing root/logic/AI data must leave actual world ownership and later sessions valid. */
static void AiDefaultsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	const NDb::Ptr<NDb::SessionRoot> previous = NDb::SessionRoot::GetRoot();
	NDb::Ptr<NDb::AILogicParameters> retained;
	for (int mode = 0; mode < 4; ++mode)
	{
		auto* logic = new NDb::SessionLogicRoot; NDb::Ptr<NDb::SessionLogicRoot> logicOwner = logic;
		auto* root = new NDb::SessionRoot; NDb::Ptr<NDb::SessionRoot> rootOwner = root;
		auto* custom = new NDb::AILogicParameters; NDb::Ptr<NDb::AILogicParameters> customOwner = custom;
		custom->followRange = 731.f;
		if (mode == 3) logic->aiLogic = custom;
		if (mode >= 2) root->logicRoot = logic;
		NDb::SessionRoot::InitRoot(mode == 0 ? nullptr : root);
		for (int repeat = 0; repeat < 3; ++repeat)
		{
			B8BehaviourForeignWorld first(check), second(check);
			if (!first.world || !second.world) continue;
			const auto* firstData = &first.world->GetAIWorld()->GetAIParameters();
			const auto* secondData = &second.world->GetAIWorld()->GetAIParameters();
			check(mode == 3 ? firstData == custom && secondData == custom : firstData != secondData,
				"B13 simultaneous worlds own independent fallback data while authored AI keeps exact shared identity");
			if (mode != 3)
			{
				if (!retained) retained = firstData;
				NDb::AILogicParameters defaults;
				check(firstData->followRange == defaults.followRange && secondData->followRange == defaults.followRange
					&& firstData->pickupItemRange == defaults.pickupItemRange && retained->followRange == defaults.followRange,
					"B13 fallback AI retains the original generated defaults and external references remain valid across world lifetimes");
			}
			else check(firstData->followRange == 731.f, "B13 authored AI data is not overwritten by bootstrap defaults");
			first.owner = nullptr;
			check(&second.world->GetAIWorld()->GetAIParameters() == secondData,
				"B13 destroying one world cannot invalidate another world's AI resource");
			second.owner = nullptr;
		}
	}
	NDb::SessionRoot::InitRoot(previous.GetPtr());
	std::puts("B13 fallback AI: worlds retired; releasing external descriptor reference"); std::fflush(stdout);
	retained = nullptr;
	{
		B8BehaviourForeignWorld restored(check);
		check(restored.world && previous && previous->logicRoot
			&& &restored.world->GetAIWorld()->GetAIParameters() == previous->logicRoot->aiLogic.GetPtr(),
			"B13 restored session starts with its own original AI data after all fallback worlds retire");
	}
	std::printf("B13 AI defaults checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
