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
		double started = -1;
		check(PollLinuxTimedExit(settings, runtime, 2.9, started) == LinuxTimedExit::Continue &&
			started < 0 && runtime.localScheduler->GetLinuxInputFinishStep() < 0, "no early input closure");
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
		double started = -1;
		check(PollLinuxTimedExit(settings, runtime, 8, started) == LinuxTimedExit::Draining && started == 8,
			"a delayed frame receives its drain grace period from sealing, not run expiry");
		check(PollLinuxTimedExit(settings, runtime, 8.99, started) == LinuxTimedExit::Draining && started == 8,
			"repeated polling cannot extend the shutdown deadline");
		check(PollLinuxTimedExit(settings, runtime, 9, started) == LinuxTimedExit::TimedOut,
			"a stalled consumer fails at the bounded drain deadline");
		runtime.transceiverWorldStep = runtime.localScheduler->GetLinuxInputFinishStep();
		check(PollLinuxTimedExit(settings, runtime, 9, started) == LinuxTimedExit::Complete,
			"completion at the deadline wins over timeout");
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
		double started = -1;
		check(PollLinuxTimedExit(settings, runtime, 8, started) ==
			(mode == 0 ? LinuxTimedExit::Continue : LinuxTimedExit::Complete),
			"unbounded sessions continue; replay and unavailable local worlds need no drain");
		check(started < 0 && (!runtime.localScheduler || runtime.localScheduler->GetLinuxInputFinishStep() < 0),
			"nonlocal exits do not seal an unused scheduler");
	}
	std::printf("Timed exit: %u checks, %u failures\n", checks, failures);
	return failures == 0;
}
