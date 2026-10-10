/** Exercise native key identities through the actual bootstrap consumers with mock state.
 * Included after the handlers in the Linux Game.cpp branch; no window or assets needed.
 */
bool RunLinuxNativeInputProbe()
{
	unsigned failures = 0, checks = 0;
	auto check = [&](bool valid, const char* label) {
		++checks;
		if (!valid) { ++failures; fprintf(stdout, "Native input FAIL: %s\n", label); }
	};
	auto key = [](unsigned long symbol, bool down = true) {
		const auto normalized = NMainFrame::NormalizeLinuxKeyInput(symbol);
		NMainFrame::SWindowsMsg message = {};
		message.msg = down ? NMainFrame::SWindowsMsg::KEY_DOWN : NMainFrame::SWindowsMsg::KEY_UP;
		message.nKey = normalized.virtualKey;
		message.nativeKeySym = normalized.nativeKeySym;
		message.nRep = 1;
		return message;
	};
	CObj<LinuxHwInput> hardware = new LinuxHwInput;
	const struct { unsigned long symbol; const char* control; } bindings[] = {
		{XK_Escape, "ESC"}, {XK_Tab, "TAB"}, {XK_ISO_Left_Tab, "TAB"},
		{XK_BackSpace, "BACKSPACE"}, {XK_Return, "ENTER"}, {XK_KP_Enter, "NUM_ENTER"},
		{XK_Up, "UP"}, {XK_Down, "DOWN"}, {XK_Left, "LEFT"}, {XK_Right, "RIGHT"},
		{XK_Home, "HOME"}, {XK_End, "END"}, {XK_Prior, "PG_UP"}, {XK_Next, "PG_DOWN"},
		{XK_Insert, "INSERT"}, {XK_Delete, "DELETE"}, {XK_minus, "-"},
		{XK_Shift_L, "LSHIFT"}, {XK_Shift_R, "RSHIFT"}, {XK_Control_L, "LCTRL"},
		{XK_Control_R, "RCTRL"}, {XK_Alt_L, "LALT"}, {XK_Alt_R, "RALT"},
		{XK_F1, "F1"}, {XK_F4, "F4"}, {XK_F10, "F10"}, {XK_F12, "F12"},
		{XK_s, "S"}, {XK_a, "A"}, {XK_1, "1"}
	};
	for (const auto& binding : bindings)
	for (bool down : {false, true})
	{
		TLinuxMainFrameMessages messages;
		messages.push_back(key(binding.symbol, down));
		hardware->SetFrameMessages(messages);
		vector<Input::HwEvent> events;
		hardware->Poll(events);
		check(events.size() == 1 && hardware->ControlName(events[0].ControlId()) == binding.control &&
			events[0].Activated() == down, binding.control);
	}
	for (unsigned long symbol : {XK_KP_1, XK_KP_3, XK_Super_L, XK_Super_R})
	{
		TLinuxMainFrameMessages messages;
		messages.push_back(key(symbol));
		hardware->SetFrameMessages(messages);
		vector<Input::HwEvent> events;
		hardware->Poll(events);
		check(events.empty(), "unsupported keys cannot alias letters/brackets");
	}
	LinuxInputState input;
	auto press = [&](unsigned long symbol) { input.rawMessages.clear(); input.rawMessages.push_back(key(symbol)); };
	LinuxMapCatalog catalog;
	catalog.entries.resize(12);
	LinuxMapBrowserState browser;
	browser.selectedIndex = 5;
	press(XK_Up); UpdateMapBrowserState(input, catalog, false, &browser);
	check(browser.selectedIndex == 4, "map Up");
	press(XK_Next); UpdateMapBrowserState(input, catalog, false, &browser);
	check(browser.selectedIndex == 11, "map PageDown clamps");
	press(XK_End); UpdateMapBrowserState(input, catalog, false, &browser);
	check(browser.selectedIndex == 11, "map End");
	press(XK_Home); UpdateMapBrowserState(input, catalog, false, &browser);
	check(browser.selectedIndex == 0, "map Home");
	press(XK_Down); UpdateMapBrowserState(input, catalog, true, &browser);
	check(browser.selectedIndex == 0, "reserved map arrow");
	LinuxBootstrapScreenRuntime runtime;
	runtime.replayFileInputActive = true;
	runtime.replayInputPlaybackRate = 1;
	press(XK_KP_Add); HandleLinuxReplayInputControls(input, 1280, 720, &runtime);
	check(runtime.replayInputPlaybackRate == 2, "replay keypad plus");
	press(XK_KP_Subtract); HandleLinuxReplayInputControls(input, 1280, 720, &runtime);
	check(runtime.replayInputPlaybackRate == 1, "replay keypad minus");
	runtime.replayInputPlaybackRate = 4;
	press(XK_KP_0); HandleLinuxReplayInputControls(input, 1280, 720, &runtime);
	check(runtime.replayInputPlaybackRate == 1, "replay keypad zero");
	for (unsigned long symbol : {XK_Delete, XK_Insert, XK_F1})
	{
		press(symbol);
		check(!HandleLinuxReplayInputControls(input, 1280, 720, &runtime), "replay collision rejected");
	}
	check(runtime.replayInputManualStepRequests == 0 && runtime.replayInputPauseToggleCount == 0,
		"unrelated special keys must not step or pause replay");
	press(XK_period); HandleLinuxReplayInputControls(input, 1280, 720, &runtime);
	check(runtime.replayInputManualStepRequests == 1, "ordinary period still steps");
	LinuxClientLaunchSettings settings;
	settings.bootstrapLegacyLobbyOverlay = true;
	LinuxHeroCatalog heroes;
	LinuxLocalMatchPreview lineup;
	LinuxUiRootPreview ui;
	runtime.visibleMenuReady = true;
	runtime.visibleMenuSelectedAction = 0;
	bool consumed = false;
	press(XK_Down);
	HandleLinuxVisibleMenuHotkeys(settings, input, catalog, &browser, heroes, &lineup, ui, &runtime, &consumed);
	check(consumed && runtime.visibleMenuSelectedAction == 1, "legacy menu Down");
	press(XK_F10);
	HandleLinuxVisibleMenuHotkeys(settings, input, catalog, &browser, heroes, &lineup, ui, &runtime, &consumed);
	check(runtime.diagnosticsOverlayActive, "F10 diagnostics");
	fprintf(stdout, "Native input: %u checks, %u failures\n", checks, failures);
	return failures == 0;
}
