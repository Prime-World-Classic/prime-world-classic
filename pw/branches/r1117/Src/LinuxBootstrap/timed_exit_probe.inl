/** Exercise the actual timed-exit policy and local scheduler/transceiver with a mock world.
 * Included only in native Game.cpp; no display, assets or elapsed wall time are needed.
 */
bool RunLinuxTimedExitProbe()
{
	unsigned checks = 0, failures = 0;
	auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { ++failures; std::printf("Timed exit FAIL: %s\n", label); }
	};
	for (bool interactive : {false, true})
	for (int prefix : {0, 4, 9})
	{
		LinuxClientLaunchSettings settings;
		settings.runSeconds = 3;
		settings.bootstrapInteractiveWorld = interactive;
		LinuxBootstrapScreenRuntime runtime;
		runtime.localScheduler = new Game::LocalCmdScheduler(1984);
		runtime.localScheduler->StartGame();
		runtime.transceiver = new NCore::Transceiver(runtime.localScheduler, DEFAULT_GAME_STEP_LENGTH, false);
		CObj<LinuxBootstrapReplayProbeWorld> world = new LinuxBootstrapReplayProbeWorld;
		runtime.transceiverWorld = world;
		runtime.transceiver->SetWorld(world);
		runtime.transceiverWorldAttached = true;
		runtime.transceiverWorldStep = runtime.transceiver->GetWorldStep();
		CObj<NCore::PackedWorldCommand> command = new NCore::PackedWorldCommand;
		for (int step = 0; step < prefix; ++step)
		{
			runtime.localScheduler->SendMessage(command, true);
			runtime.localScheduler->Step(.1f);
		}
		runtime.localScheduler->SendMessage(command, true);
		runtime.localScheduler->QueueLinuxClientStatus(1);
		LinuxTimedDrain started;
		check(PollLinuxTimedExit(settings, runtime, 2.9, started) == LinuxTimedExit::Continue &&
			started.started < 0 && runtime.localScheduler->GetLinuxInputFinishStep() < 0, "no early input closure");
		check(PollLinuxTimedExit(settings, runtime, 3, started) == LinuxTimedExit::Draining,
			"both interactive and diagnostic timeouts drain accepted input");
		const int boundary = runtime.localScheduler->GetLinuxInputFinishStep();
		check(boundary == prefix + 1, "wall timeout seals the queued command and status");
		if (boundary < 0) continue;
		for (int step = 0; step < 24 && runtime.transceiverWorldStep < boundary; ++step)
		{
			runtime.localScheduler->SendMessage(command, true);
			runtime.localScheduler->QueueLinuxClientStatus(2);
			for (int prime = 0; prime < (step == 0 ? 3 : 1); ++prime) runtime.localScheduler->Step(.1f);
			runtime.transceiver->Step(DEFAULT_GAME_STEP_LENGTH);
			runtime.transceiverWorldStep = runtime.transceiver->GetWorldStep();
		}
		check(runtime.transceiverWorldStep == boundary && world->GetStepNumber() == boundary,
			"real transceiver reaches the sealed tail without a counter rewrite");
		check(world->GetCommands() == static_cast<size_t>(prefix + 1) && world->GetStatusUpdates() == 1,
			"accepted commands and status execute once; late input is rejected");
		check(PollLinuxTimedExit(settings, runtime, 3.5, started) == LinuxTimedExit::Complete,
			"completed tail permits normal exit before the simulation cap");
	}
	for (bool interactive : {false, true})
	{
		LinuxClientLaunchSettings settings;
		settings.runSeconds = 3;
		settings.bootstrapInteractiveWorld = interactive;
		LinuxBootstrapScreenRuntime runtime;
		runtime.localScheduler = new Game::LocalCmdScheduler(1984);
		runtime.localScheduler->StartGame();
		runtime.transceiver = new NCore::Transceiver(runtime.localScheduler, DEFAULT_GAME_STEP_LENGTH, false);
		runtime.transceiverWorldAttached = true;
		runtime.transceiverWorldStep = 0;
		LinuxTimedDrain started;
		check(PollLinuxTimedExit(settings, runtime, 8, started) == LinuxTimedExit::Draining && started.started == 8,
			"a delayed frame receives its drain grace period from sealing, not run expiry");
		check(PollLinuxTimedExit(settings, runtime, 8.99, started) == LinuxTimedExit::Draining && started.started == 8,
			"repeated polling cannot extend the shutdown deadline");
		check(PollLinuxTimedExit(settings, runtime, 9, started) == LinuxTimedExit::TimedOut,
			"a stalled consumer fails at the bounded drain deadline");
		LinuxSelectedMapPreview map;
		check(ServiceLinuxTimedExit(settings, map, runtime, 9, started) == LinuxTimedExit::TimedOut
			&& runtime.transceiverStepCalls == 0,
			"drain service does not drive a stalled consumer after its unchanged deadline");
		runtime.transceiverWorldStep = runtime.localScheduler->GetLinuxInputFinishStep();
		check(PollLinuxTimedExit(settings, runtime, 9, started) == LinuxTimedExit::Complete,
			"completion at the deadline wins over timeout");
	}
	// Use the production driver, not a separately hand-stepped scheduler/transceiver pair.
	for (bool interactive : {false, true}) for (int prefix : {0, 4, 9, 32})
	{
		LinuxClientLaunchSettings settings;
		settings.runSeconds = 3;
		settings.bootstrapInteractiveWorld = interactive;
		LinuxSelectedMapPreview map;
		LinuxBootstrapScreenRuntime runtime;
		runtime.localScheduler = new Game::LocalCmdScheduler(1984);
		runtime.localScheduler->StartGame();
		runtime.transceiver = new NCore::Transceiver(runtime.localScheduler, DEFAULT_GAME_STEP_LENGTH, false);
		CObj<LinuxBootstrapReplayProbeWorld> world = new LinuxBootstrapReplayProbeWorld;
		runtime.transceiverWorld = world;
		runtime.transceiver->SetWorld(world);
		runtime.transceiverWorldAttached = true;
		runtime.transceiverWorldStep = runtime.transceiver->GetWorldStep();
		runtime.mapLoadingJobCompleted = true;
		CObj<NCore::PackedWorldCommand> command = new NCore::PackedWorldCommand;
		for (int i = 0; i < prefix; ++i)
		{
			runtime.localScheduler->SendMessage(command, true);
			DriveLinuxBootstrapGameScheduler(settings, map, &runtime, i * .1);
		}
		runtime.localScheduler->SendMessage(command, true);
		runtime.localScheduler->QueueLinuxClientStatus(1);
		LinuxTimedDrain started;
		const auto beforeService = runtime.transceiverStepCalls;
		check(ServiceLinuxTimedExit(settings, map, runtime, 2.9, started) == LinuxTimedExit::Continue
			&& runtime.transceiverStepCalls == beforeService && started.started < 0,
			"ordinary frames retain their original driver and presentation path before run expiry");
		check(PollLinuxTimedExit(settings, runtime, 3, started) == LinuxTimedExit::Draining,
			"production driver seals accepted pending input before shutdown");
		const int boundary = runtime.localScheduler->GetLinuxInputFinishStep();
		for (int i = 0; i < 50 && runtime.transceiverWorldStep < boundary; ++i)
		{
			runtime.localScheduler->SendMessage(command, true);
			runtime.localScheduler->QueueLinuxClientStatus(2);
			const auto calls = runtime.transceiverStepCalls;
			const auto state = ServiceLinuxTimedExit(settings, map, runtime, 3.01 + i * .016, started,
				prefix * .1 + .01 + i * .016);
			check((state == LinuxTimedExit::Draining || state == LinuxTimedExit::Complete)
				&& runtime.transceiverStepCalls - calls <= 3,
				"sealed draining keeps wall pacing and a three-tick call bound without another render");
			if (interactive && i == 0)
			{
				const auto after = runtime.transceiverStepCalls;
				ServiceLinuxTimedExit(settings, map, runtime, 3.01, started, prefix * .1 + .01);
				check(runtime.transceiverStepCalls == after, "repeated shutdown service at one clock instant cannot invent simulation ticks");
			}
		}
		check(runtime.transceiverWorldStep == boundary && world->GetStepNumber() == boundary,
			"production driver drains the accepted tail within its unchanged grace period");
		check(world->GetCommands() == static_cast<size_t>(prefix + 1) && world->GetStatusUpdates() == 1,
			"production driver executes every sealed command and status exactly once");
		check(!interactive || (runtime.transceiverWorldStep >= 0
			&& runtime.interactiveClock.ticks >= static_cast<size_t>(runtime.transceiverWorldStep)
			&& runtime.interactiveClock.ticks - runtime.transceiverWorldStep <= 3),
			"interactive tail remains bounded by real clock ticks without counter rewriting");
		check(PollLinuxTimedExit(settings, runtime, 3.5, started) == LinuxTimedExit::Complete,
			"production driver reaches the existing timed exit barrier");
		const auto calls = runtime.transceiverStepCalls;
		check(ServiceLinuxTimedExit(settings, map, runtime, 9, started) == LinuxTimedExit::Complete
			&& runtime.transceiverStepCalls == calls,
			"completed service never advances beyond the sealed simulation boundary");
	}
	// Simulated wall time includes slow world work, while the real scheduler owns every tick.
	for (bool interactive : {false, true})
	{
		LinuxClientLaunchSettings settings; settings.runSeconds = 3;
		settings.bootstrapInteractiveWorld = interactive;
		LinuxSelectedMapPreview map;
		LinuxBootstrapScreenRuntime runtime;
		runtime.localScheduler = new Game::LocalCmdScheduler(1984); runtime.localScheduler->StartGame();
		runtime.transceiver = new NCore::Transceiver(runtime.localScheduler, DEFAULT_GAME_STEP_LENGTH, false);
		CObj<LinuxBootstrapReplayProbeWorld> world = new LinuxBootstrapReplayProbeWorld;
		runtime.transceiverWorld = world; runtime.transceiver->SetWorld(world);
		runtime.transceiverWorldAttached = true; runtime.mapLoadingJobCompleted = true;
		runtime.transceiverWorldStep = runtime.transceiver->GetWorldStep();
		CObj<NCore::PackedWorldCommand> command = new NCore::PackedWorldCommand;
		for (int i = 0; i < 32; ++i)
		{
			runtime.localScheduler->SendMessage(command, true);
			DriveLinuxBootstrapGameScheduler(settings, map, &runtime, i * .1);
		}
		runtime.localScheduler->SendMessage(command, true); runtime.localScheduler->QueueLinuxClientStatus(1);
		LinuxTimedDrain started;
		check(PollLinuxTimedExit(settings, runtime, 3, started) == LinuxTimedExit::Draining,
			"slow-world fixture seals real accepted input before consuming its tail");
		const int boundary = runtime.localScheduler->GetLinuxInputFinishStep();
		for (int i = 0; i < 4 && runtime.transceiverWorldStep < boundary; ++i)
		{
			const auto calls = runtime.transceiverStepCalls;
			const auto state = ServiceLinuxTimedExit(settings, map, runtime, 3.01 + i * 1.1, started, 3.2 + i * 1.1);
			check(state != LinuxTimedExit::TimedOut && runtime.transceiverStepCalls - calls <= 3,
				"forward progress survives a slow drain call without removing the per-call tick bound");
		}
		check(runtime.transceiverWorldStep == boundary && world->GetStepNumber() == boundary
			&& world->GetCommands() == 33 && world->GetStatusUpdates() == 1,
			"slow consumer drains the exact sealed commands and status without synthetic world ticks");
	}
	// Policy-only snapshots prove that progress cannot remove either timeout bound.
	for (bool hardLimit : {false, true})
	{
		LinuxClientLaunchSettings settings; settings.runSeconds = 3;
		LinuxBootstrapScreenRuntime runtime;
		runtime.localScheduler = new Game::LocalCmdScheduler(1984); runtime.localScheduler->StartGame();
		for (int i = 0; i < 20; ++i) runtime.localScheduler->Step(.1f);
		runtime.transceiver = new NCore::Transceiver(runtime.localScheduler, DEFAULT_GAME_STEP_LENGTH, false);
		runtime.transceiverWorldAttached = true; runtime.transceiverWorldStep = 0;
		LinuxTimedDrain drain;
		check(PollLinuxTimedExit(settings, runtime, 3, drain) == LinuxTimedExit::Draining,
			"drain policy initializes absolute and idle budgets at the actual seal time");
		if (hardLimit)
		{
			for (int i = 1; i <= 9; ++i)
			{
				runtime.transceiverWorldStep = i;
				check(PollLinuxTimedExit(settings, runtime, 3 + i * .5, drain) == LinuxTimedExit::Draining
					&& drain.started == 3, "actual forward progress renews only the idle budget");
			}
			runtime.transceiverWorldStep = 10;
			check(PollLinuxTimedExit(settings, runtime, 8, drain) == LinuxTimedExit::TimedOut,
				"even continuing progress cannot extend the five-second absolute drain bound");
		}
		else
		{
			runtime.transceiverWorldStep = 1;
			check(PollLinuxTimedExit(settings, runtime, 3.8, drain) == LinuxTimedExit::Draining,
				"a successful step starts a bounded fresh idle interval");
			runtime.transceiverWorldStep = 0;
			check(PollLinuxTimedExit(settings, runtime, 4.5, drain) == LinuxTimedExit::Draining
				&& drain.lastProgress == 3.8 && drain.worldStep == 1,
				"a regressing world snapshot cannot count as progress or reset the high-water mark");
			check(PollLinuxTimedExit(settings, runtime, 4.9, drain) == LinuxTimedExit::TimedOut,
				"a consumer that stalls after initial progress still fails after one idle second");
			runtime.transceiverWorldStep = 2;
			check(PollLinuxTimedExit(settings, runtime, 4.91, drain) == LinuxTimedExit::TimedOut,
				"late incomplete progress cannot restart a drain that already timed out");
		}
	}
	for (int mode = 0; mode < 5; ++mode)
	{
		LinuxClientLaunchSettings settings;
		settings.runSeconds = mode == 0 ? 0 : 3;
		settings.bootstrapInteractiveWorld = true;
		LinuxBootstrapScreenRuntime runtime;
		runtime.localScheduler = new Game::LocalCmdScheduler(1984);
		runtime.transceiver = new NCore::Transceiver(runtime.localScheduler, DEFAULT_GAME_STEP_LENGTH, false);
		runtime.transceiverWorldAttached = mode != 1;
		runtime.replayFileInputActive = mode == 2;
		if (mode == 3) runtime.transceiver = 0;
		if (mode == 4) runtime.localScheduler = 0;
		LinuxTimedDrain started;
		check(PollLinuxTimedExit(settings, runtime, 8, started) ==
			(mode == 0 ? LinuxTimedExit::Continue : LinuxTimedExit::Complete),
			"unbounded sessions continue; replay and unavailable local worlds need no drain");
		check(started.started < 0 && (!runtime.localScheduler || runtime.localScheduler->GetLinuxInputFinishStep() < 0),
			"nonlocal exits do not seal an unused scheduler");
	}
	std::printf("Timed exit: %u checks, %u failures\n", checks, failures);
	return failures == 0;
}
