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
	// Real forward/inverse map projection must retain both edges in tall windows.
	LinuxSelectedMapPreview map;
	map.tactical.ready = true;
	map.tactical.minX = map.tactical.minY = 0;
	map.tactical.maxX = map.tactical.maxY = 410;
	runtime.mapPreviewRenderedPitchDegrees = 56;
	runtime.mapPreviewRenderedYawDegrees = 0;
	runtime.mapPreviewZoom = 1.18f;
	runtime.mapPreviewPanX = runtime.mapPreviewPanZ = 0;
	for (const auto size : {std::pair<int, int>{1280, 720}, {2160, 3840}, {720, 1280}, {960, 768}})
	{
		settings.width = size.first; settings.height = size.second;
		for (float x : {20.f, 205.f, 390.f})
		{
			int sx = 0, sy = 0;
			float wx = 0, wy = 0;
			check(ProjectLinuxMapPreviewWorldToScreen(settings, map, runtime, CVec2(x, 205), &sx, &sy) &&
				sx >= 0 && sx < size.first && sy >= 0 && sy < size.second, "map edge remains in resized viewport");
			check(ProjectLinuxMapPreviewScreenToWorld(settings, map, runtime, sx, sy, &wx, &wy, false) &&
				fabs(wx - x) < 3 && fabs(wy - 205) < 3, "resized map projection round trip");
		}
	}
	// A local gameplay view must not be clamped back to whole-map inspection scale.
	runtime.mapPreviewZoom = 8.f;
	ClampLinuxMapPreviewCamera(&runtime);
	check(runtime.mapPreviewZoom == 8.f, "local gameplay zoom survives camera clamp");
	runtime.localMapCamera = true;
	runtime.localMapFocusHeight = 12.f;
	map.terrainHeightmap.ready = true;
	map.terrainHeightmap.sampledWidth = map.terrainHeightmap.sampledHeight = 2;
	map.terrainHeightmap.worldWidth = map.terrainHeightmap.worldHeight = 410;
	map.terrainHeightmap.sampledHeights = {0.f, 20.f, 40.f, 120.f};
	for (const auto size : {std::pair<int, int>{1280, 720}, {720, 1280}})
	for (float yaw : {0.f, 42.f, 158.f, 270.f})
	for (float pitch : {35.f, 56.f, 72.f})
	for (const auto target : {CVec2(84, 103), CVec2(310, 344)}) {
		settings.width = size.first; settings.height = size.second;
		runtime.mapPreviewRenderedPitchDegrees = pitch;
		runtime.mapPreviewRenderedYawDegrees = yaw;
		runtime.mapPreviewPanX = (205.f - target.x) * (92.f / 410.f);
		runtime.mapPreviewPanZ = (205.f - target.y) * (92.f / 410.f);
		runtime.localMapFocusHeight = SampleLinuxMapTerrainHeightAtWorld(map.terrainHeightmap, target.x, target.y);
		const auto frame = ResolveLinuxMapCameraFrame(settings, map, runtime);
		map.terrainHeightmap.minHeight = 0;
		map.terrainHeightmap.maxHeight = 120;
		const auto window = ResolveLinuxMapSceneRequest(settings, map, runtime);
		check(window.Valid() && std::abs(window.focusX - target.x) < .01 && std::abs(window.focusY - target.y) < .01,
			"scene window tracks camera focus at every tested aspect and rotation");
		double sx = -1, sy = -1;
		const double ground = SampleLinuxMapTerrainHeightAtWorld(map.terrainHeightmap, target.x, target.y) + .04 / frame.scale;
		check(frame.Project({target.x, target.y, ground}, sx, sy), "local ground projects at its true height");
		LinuxBootstrap::MapCameraFrame::Point hit;
		check(TraceLinuxMapCameraTerrain(frame, map.terrainHeightmap, sx, sy, hit) &&
			std::abs(hit.x - target.x) < .001 && std::abs(hit.y - target.y) < .001 &&
			std::abs(hit.z - ground) < .001, "rotated local camera hits the rendered triangle in both cell halves");
		int ix = -1, iy = -1;
		float wx = 0, wy = 0;
		check(ProjectLinuxMapPreviewWorldToScreen(settings, map, runtime, target, &ix, &iy) &&
			ProjectLinuxMapPreviewScreenToWorld(settings, map, runtime, ix, iy, &wx, &wy, false) &&
			std::abs(wx - target.x) < .5 && std::abs(wy - target.y) < .5, "actual local integer picking round trip");
		check(!frame.Project({-1000, -1000, 0}, sx, sy), "offscreen actor cannot be snapped to a viewport edge");
	}
	runtime.localMapFollow = true;
	PanLinuxMapPreview(&runtime, 1, 1, "test-pan");
	check(!runtime.localMapFollow, "manual pan detaches follow");
	check(std::abs(SampleLinuxMapTerrainHeightAtWorld(map.terrainHeightmap, 102.5f, 102.5f) - 15.f) < .001f,
		"ground sampler uses rendered triangle instead of bilinear height");
	using Camera = LinuxBootstrap::MapCameraFrame;
	for (const auto bad : {Camera(0, 0, 410, 410, 0, 720, 8, 56, 0, 0, 0, true, 0),
		Camera(0, 0, 0, 410, 1280, 720, 8, 56, 0, 0, 0, true, 0),
		Camera(0, 0, 410, 410, 1280, 720, 8, 0, 0, 0, 0, true, 0),
		Camera(0, 0, 410, 410, 1280, 720, NAN, 56, 0, 0, 0, true, 0)}) {
		double x = 123, y = 456;
		check(!bad.valid && !bad.Project({205, 205, 0}, x, y) && x == 123 && y == 456,
			"invalid camera preserves unpublished output");
	}
	fprintf(stdout, "Native input: %u checks, %u failures\n", checks, failures);
	return failures == 0;
}
