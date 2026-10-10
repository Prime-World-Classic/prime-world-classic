#include "../System/systemStdAfx.h"
#include "scheduler_drain_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PW_Client/LocalCmdScheduler.h"
#include "../Core/GameCommand.h"
#include <cstdio>

bool RunPrimeWorldLinuxSchedulerDrainProbe()
{
	int checks = 0, failures = 0;
	auto check = [&](bool result, const char* name)
	{
		++checks;
		if (!result) { ++failures; std::printf("Scheduler drain FAIL: %s\n", name); }
	};
	for (int prefix = 0; prefix < 16; ++prefix)
	{
		StrongMT<Game::LocalCmdScheduler> scheduler = new Game::LocalCmdScheduler(1984);
		scheduler->StartGame();
		CObj<NCore::PackedWorldCommand> before = new NCore::PackedWorldCommand;
		CObj<NCore::PackedWorldCommand> after = new NCore::PackedWorldCommand;
		for (int i = 0; i < prefix; ++i)
		{
			scheduler->SendMessage(before, true);
			scheduler->Step(0.1f);
		}
		// Include an unsealed command and status, not only already-ready segments.
		scheduler->SendMessage(before, true);
		scheduler->QueueLinuxClientStatus(1);
		const int boundary = scheduler->CloseLinuxInput();
		check(boundary == prefix + 1, "boundary includes current segment and replay offset");
		for (int i = 0; i < 8; ++i)
		{
			scheduler->SendMessage(after, i % 2 == 0);
			scheduler->QueueLinuxClientStatus(2);
			scheduler->Step(0.1f);
			check(scheduler->CloseLinuxInput() == boundary, "closing repeatedly retains the barrier");
		}
		int commands = 0, statuses = 0, last = -1;
		while (CObj<NCore::SyncSegment> segment = scheduler->GetSyncSegment())
		{
			check(segment->step == ++last, "segments remain ordered without gaps");
			for (int i = 0; i < segment->commands.size(); ++i)
			{
				++commands;
				check(segment->commands[i] == before, "no post-close command is admitted");
			}
			statuses += segment->statuses.size();
		}
		check(commands == prefix + 1, "all accepted commands drain exactly once");
		check(statuses == 1 && scheduler->GetLinuxConsumedStatusCount() == 1,
			"accepted status drains once and late statuses are rejected");
		check(last >= boundary, "empty tail reaches the final replay step");
	}
	std::printf("Scheduler drain: %d checks, %d failures\n", checks, failures);
	return failures == 0;
}
