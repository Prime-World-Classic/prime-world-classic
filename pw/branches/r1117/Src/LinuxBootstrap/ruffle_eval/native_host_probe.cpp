#include "native_host.h"
#include "minimap_input.h"
#include "client_inspection.h"
#include "hud_calls.h"
#include "action_calls.h"
#include <EGL/egl.h>
#include <GL/glx.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Check(bool condition, const std::string& error)
{
	if (!condition) throw std::runtime_error(error);
}

/** Own a real GLX drawable before the host, so its texture is destroyed first. */
struct WindowContext
{
	Display* display = nullptr;
	Window window = 0;
	Colormap colormap = 0;
	GLXContext context = nullptr;
	void Open()
	{
		display = XOpenDisplay(nullptr);
		Check(display != nullptr, "DISPLAY unavailable");
		int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
		XVisualInfo* visual = glXChooseVisual(display, DefaultScreen(display), attributes);
		Check(visual != nullptr, "No GLX visual");
		colormap = XCreateColormap(display, RootWindow(display, visual->screen), visual->visual, AllocNone);
		XSetWindowAttributes settings{};
		settings.colormap = colormap;
		settings.override_redirect = True;
		window = XCreateWindow(display, RootWindow(display, visual->screen), 0, 0, 1280, 720, 0,
			visual->depth, InputOutput, visual->visual, CWColormap | CWOverrideRedirect, &settings);
		context = glXCreateContext(display, visual, nullptr, True);
		XFree(visual);
		Check(window && context, "Cannot create GLX context");
		XStoreName(display, window, "Prime World native Ruffle context probe");
		XMapWindow(display, window);
		XSync(display, False);
		Check(glXMakeCurrent(display, window, context), "Cannot activate GLX context");
		glDrawBuffer(GL_BACK);
		glReadBuffer(GL_BACK);
	}
	~WindowContext()
	{
		if (context) { glXMakeCurrent(display, None, nullptr); glXDestroyContext(display, context); }
		if (window) XDestroyWindow(display, window);
		if (colormap) XFreeColormap(display, colormap);
		if (display) XCloseDisplay(display);
	}
	void CheckCurrent() const
	{
		Check(glXGetCurrentContext() == context && glXGetCurrentDisplay() == display &&
			glXGetCurrentDrawable() == window && glXGetCurrentReadDrawable() == window,
			"GLX context or draw/read drawable was not restored");
		Check(eglGetCurrentContext() == EGL_NO_CONTEXT, "Ruffle left an EGL context current");
		Check(glGetString(GL_VERSION) != nullptr, "GL dispatch was not restored");
		Check(glGetError() == GL_NO_ERROR, "Unexpected host GL error");
	}
};
}

/** Exercise real SWF -> EGL readback -> GLX composition, failure recovery, and repeated teardown. */
int main(int argc, char** argv)
{
	try
	{
		const bool benchmark = argc == 5 && std::string(argv[4]) == "--benchmark";
		Check(argc == 4 || benchmark, "Usage: PrimeWorldRuffleNativeHostProbe LIBRARY DATA COMBAT_SWF [--benchmark]");
		WindowContext window;
		window.Open();
		PwRuffleNativeHost host;
		std::string error, response;
		Check(!host.Open("relative.so", argv[2], argv[3], error), "Accepted relative library");
		Check(!host.Open("/nonexistent/pw-ruffle.so", argv[2], argv[3], error), "Accepted missing library");
		Check(!host.Request("{}", response, error), "Accepted request while closed");
		window.CheckCurrent();
		if (benchmark)
		{
			Check(host.Open(argv[1], argv[2], argv[3], error), error);
			Check(host.Request(R"({"path":"LocalizationResources","method":"LocalizationComplete","args":[]})", response, error), error);
			Check(host.Request(R"({"path":"mainInterface","method":"HideAllWindows","args":[]})", response, error), error);
			for (int i = 0; i < 5; ++i) Check(host.Draw(1280, 720, 16, error), error);
			const auto before = host.Timing();
			for (int i = 0; i < 60; ++i)
			{
				Check(host.Draw(1280, 720, 1000.0 / 60, error), error);
				window.CheckCurrent();
			}
			const auto after = host.Timing();
			Check(after.frames - before.frames == 60, "Timing did not count successful frames");
			const double tick = (after.tickMs - before.tickMs) / 60;
			const double render = (after.renderMs - before.renderMs) / 60;
			const double composite = (after.compositeMs - before.compositeMs) / 60;
			const double coverage = (after.coverageMs - before.coverageMs) / 60;
			for (double stage : {tick, render, composite, coverage})
				Check(std::isfinite(stage) && stage >= 0, "Invalid measured stage time");
			Check(host.MatchesViewport(1280, 720) && !host.ContainsPixel(640, 100), "Benchmark lost transparent coverage");
			Check(host.Request(R"({"action":"stats"})", response, error), error);
			Check(nlohmann::json::parse(response).at("runtime_errors") == 0, "Benchmark SWF runtime errors");
			std::cout << nlohmann::json{{"frames", 60}, {"width", 1280}, {"height", 720},
				{"tick_ms", tick}, {"render_readback_ms", render}, {"composite_ms", composite},
				{"coverage_ms", coverage}, {"draw_ms", tick + render + composite + coverage},
				{"gl_renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER))}}.dump() << '\n';
			Check(!host.Draw(0, 720, 16, error) && host.Timing().frames == after.frames,
				"Failed draw changed successful timing counters");
			Check(host.Reset() && host.Timing().frames == after.frames, "Teardown lost profiling counters");
			return 0;
		}
		for (int cycle = 0; cycle < 2; ++cycle)
		{
			Check(host.Open(argv[1], argv[2], argv[3], error), error);
			window.CheckCurrent();
			Check(host.Request(R"({"path":"LocalizationResources","method":"LocalizationComplete","args":[]})", response, error), error);
			Check(host.Request(R"({"path":"mainInterface","method":"HideAllWindows","args":[]})", response, error), error);
			Check(host.Request(R"({"action":"step","frames":3})", response, error), error);
			Check(!host.Request("invalid JSON", response, error), "Accepted malformed JSON");
			window.CheckCurrent();
			Check(!host.Draw(0, 720, 16, error), "Accepted invalid viewport");
			Check(!host.Draw(1280, 720, -1, error), "Accepted negative delta");
			for (const auto size : {std::array<unsigned, 2>{640, 480}, {257, 193}, {1280, 720}})
			{
				glDisable(GL_SCISSOR_TEST);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glClearColor(17 / 255.f, 203 / 255.f, 71 / 255.f, 1);
				glClear(GL_COLOR_BUFFER_BIT);
				glViewport(3, 7, 109, 113);
				glEnable(GL_SCISSOR_TEST);
				glScissor(1, 2, 11, 13);
				const EGLenum api = eglQueryAPI();
				Check(host.Draw(size[0], size[1], 16, error), error);
				Check(host.MatchesViewport(size[0], size[1]) && !host.MatchesViewport(size[0] + 1, size[1]), "Stale coverage dimensions");
				Check(!host.ContainsPixel(-1, 0) && !host.ContainsPixel(size[0], 0), "Coverage escaped viewport");
				window.CheckCurrent();
				Check(eglQueryAPI() == api, "EGL client API changed");
				GLint viewport[4];
				glGetIntegerv(GL_VIEWPORT, viewport);
				Check(viewport[0] == 3 && viewport[1] == 7 && viewport[2] == 109 && viewport[3] == 113 && glIsEnabled(GL_SCISSOR_TEST), "Host GL state changed");
				std::vector<uint8_t> pixels(size[0] * size[1] * 4);
				glReadPixels(0, 0, size[0], size[1], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
				size_t background = 0;
				for (size_t i = 0; i < pixels.size(); i += 4)
					if (pixels[i] == 17 && pixels[i + 1] == 203 && pixels[i + 2] == 71) ++background;
				std::cout << "cycle=" << cycle << " size=" << size[0] << 'x' << size[1] << " background=" << background << '\n';
				Check(background > 100 && background < size[0] * size[1] - 100, "Missing transparent background or rendered SWF pixels");
			}
			Check(host.Request(R"({"action":"stats"})", response, error), error);
			Check(nlohmann::json::parse(response).at("runtime_errors") == 0, "SWF runtime errors");
			Check(host.Reset(), "Host close failed");
			Check(host.Reset(), "Repeated close failed");
			Check(!host.IsReady(), "Host remained open");
			window.CheckCurrent();
		}
		// Distinct mock values must reach the shipped SWF's public player model.
		PwRuffleHudState hud;
		auto& hero = hud.hero.emplace();
		hero.heroId = 7; hero.heroName = "Bridge QA"; hero.heroClass = "Test hero";
		hero.isMale = true; hero.isBot = false; hero.force = 123;
		hero.faction = 2; hero.originalFaction = 1; hero.rating = 1500;
		hero.damageType = 0; hero.forceColors = {{0, 0xffffff}, {200, 0xff0000}};
		auto& vitals = hud.values.emplace();
		vitals.level = 6; vitals.health = 301; vitals.maxHealth = 907;
		vitals.energy = 59; vitals.maxEnergy = 311; vitals.isVisible = true;
		vitals.isPickable = true; vitals.resurrectionSeconds = -1;
		vitals.channeling = 0; vitals.healthRegen = 2; vitals.energyRegen = 3;
		vitals.isCameraLocked = false; vitals.ultimateCooldown = -1;
		Check(host.Open(argv[1], argv[2], argv[3], error), error);
		Check(host.Request(R"({"path":"LocalizationResources","method":"LocalizationComplete","args":[]})", response, error), error);
		Check(host.Request(R"({"path":"mainInterface","method":"HideAllWindows","args":[]})", response, error), error);
		Check(host.Request(R"({"action":"step","frames":3})", response, error), error);
		for (const auto& calls : {PwRuffleHeroIdentityCalls(hud), PwRuffleHeroValueCalls(hud)})
			for (const auto& call : calls) Check(host.Request(call.dump(), response, error), error);
		const nlohmann::json heroFields{{"HeroId", 7}, {"Level", 6}, {"CurrentHealth", 301},
			{"MaximumHealth", 907}, {"CurrentMana", 59}, {"MaximumMana", 311}, {"IsOurHero", true}};
		for (const auto& field : heroFields.items())
		{
			Check(host.Request(nlohmann::json{{"path", "Players.0"}, {"op", "get"}, {"method", field.key()},
				{"args", nlohmann::json::array()}}.dump(), response, error), error);
			Check(nlohmann::json::parse(response).at("value") == field.value(), "Authored hero model mismatch: " + field.key());
		}
		Check(host.Request(R"({"action":"stats"})", response, error), error);
		Check(nlohmann::json::parse(response).at("runtime_errors") == 0, "Hero binding runtime errors");
		PwRuffleActionState actions;
		PwRuffleActionState::Talent talent;
		talent.column = 1; talent.row = 0; talent.iconPath = "UI/Styles/Icons/Talents/_570.dds";
		talent.active = true; talent.desiredIndex = 3; talent.upgradeLevel = 0;
		talent.classTalent = true; talent.cost = 300;
		talent.purchase = PwRuffleActionState::PurchaseState::Bought;
		talent.status = PwRuffleActionState::SlotState::Active;
		talent.cooldown = 0; talent.maxCooldown = 12; talent.alternativeState = false;
		actions.talents.push_back(talent);
		talent.column = 2; talent.desiredIndex = -1; talent.purchase = PwRuffleActionState::PurchaseState::NotEnoughPrime;
		actions.talents.push_back(talent);
		for (const auto& call : PwRuffleActionInitCalls(actions)) Check(host.Request(call.dump(), response, error), error);
		const auto slotIndex = [&](int column)
		{
			Check(host.Request(nlohmann::json{{"path", "mainInterface"}, {"method", "GetTalentActionBarIndex"},
				{"args", {column, 0}}}.dump(), response, error), error);
			return nlohmann::json::parse(response).at("value").get<int>();
		};
		Check(slotIndex(1) == 3 && slotIndex(2) == -1, "Bought/unbought shortcut initialization mismatch");
		auto updated = actions;
		updated.talents[1].purchase = PwRuffleActionState::PurchaseState::Bought;
		updated.talents[0].cooldown = 4.5;
		for (const auto& call : PwRuffleActionUpdateCalls(updated, actions)) Check(host.Request(call.dump(), response, error), error);
		Check(slotIndex(1) == 3 && slotIndex(2) == 0, "New purchase shortcut placement mismatch");
		Check(PwRuffleActionUpdateCalls(updated, updated).empty(), "Repeated action snapshot generated calls");
		Check(host.Draw(1280, 720, 16, error), error);
		Check(host.Request(R"({"action":"stats"})", response, error), error);
		Check(nlohmann::json::parse(response).at("runtime_errors") == 0, "Talent binding runtime errors");
		Check(host.Request(R"({"action":"bitmap_create","width":270,"height":270})", response, error), error);
		const auto bitmap = nlohmann::json::parse(response).at("id").get<std::string>();
		const auto bitmapId = std::stoull(bitmap);
		std::vector<uint8_t> mapPixels(270 * 270 * 4, 255);
		Check(host.Request(nlohmann::json{{"path", "miniMap_mc.miniMapAnim_mc.mapImage"}, {"op", "set"},
			{"method", "bitmapData"}, {"args", {{{"$handle", bitmap}}}}}.dump(), response, error), error);
		Check(host.Request(R"({"path":"miniMap_mc.miniMapAnim_mc","method":"correctSize","args":[]})", response, error), error);
		// Isolate pixel transport from the authored startup fade; do not advance time here.
		Check(host.Request(R"({"path":"miniMap_mc","op":"set","method":"alpha","args":[1]})", response, error), error);
		for (const auto color : {std::array<uint8_t, 3>{213, 43, 67}, {31, 109, 227}})
		{
			for (size_t i = 0; i < mapPixels.size(); i += 4)
			{
				std::copy(color.begin(), color.end(), mapPixels.begin() + i);
				mapPixels[i + 3] = 255;
			}
			PwRuffleMinimapFrame clipped{270, 270, 1080, mapPixels};
			PwRuffleClipMinimapFrame(clipped);
			mapPixels = std::move(clipped.rgba);
			Check(host.UploadBitmap(bitmapId, 270, 270, mapPixels.data(), mapPixels.size(), error), error);
			window.CheckCurrent();
			glDisable(GL_SCISSOR_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(17 / 255.f, 203 / 255.f, 71 / 255.f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			Check(host.Draw(1280, 720, 0, error), error);
			std::array<uint8_t, 4> pixel{};
			glReadPixels(1160, 155, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
			std::cout << "minimap pixel=" << unsigned(pixel[0]) << ',' << unsigned(pixel[1]) << ',' << unsigned(pixel[2]) << '\n';
			Check(std::equal(color.begin(), color.end(), pixel.begin()), "Original minimap did not display updated bitmap pixels");
			glReadPixels(1018, 270, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
			Check(pixel[0] == 17 && pixel[1] == 203 && pixel[2] == 71, "Minimap escaped its authored circular boundary");
			Check(host.ContainsPixel(1160, 565) && !host.ContainsPixel(1018, 449), "Alpha coverage disagrees with minimap pixels");
		}
		Check(!host.UploadBitmap(bitmapId, 270, 270, mapPixels.data(), mapPixels.size() - 1, error), "Accepted short map buffer");
		Check(!host.UploadBitmap(bitmapId, 135, 540, mapPixels.data(), mapPixels.size(), error), "Accepted wrong bitmap dimensions");
		window.CheckCurrent();
		Check(host.Request(R"({"action":"stats"})", response, error), error);
		Check(nlohmann::json::parse(response).at("runtime_errors") == 0, "Minimap binding runtime errors");
		// Locate the actual authored button through getBounds, not hard-coded artwork coordinates.
		const std::string buttonPath = "actionBarContainer.actionBar_mc.talents_btn";
		Check(host.Request(nlohmann::json{{"path", buttonPath}, {"op", "get"}, {"method", "root"},
			{"args", nlohmann::json::array()}}.dump(), response, error), error);
		const auto root = nlohmann::json::parse(response).at("id");
		Check(host.Request(nlohmann::json{{"path", buttonPath}, {"method", "getBounds"},
			{"args", {{{"$handle", root}}}}}.dump(), response, error), error);
		const auto rect = nlohmann::json::parse(response).at("id");
		const auto coordinate = [&](const char* field)
		{
			Check(host.Request(nlohmann::json{{"receiver", rect}, {"op", "get"}, {"method", field},
				{"args", nlohmann::json::array()}}.dump(), response, error), error);
			return nlohmann::json::parse(response).at("value").get<double>();
		};
		const double buttonX = coordinate("x") + coordinate("width") / 2;
		const double buttonY = coordinate("y") + coordinate("height") / 2;
		std::cout << "Talent window button=" << buttonX << ',' << buttonY << '\n';
		for (const bool visible : {true, false})
		{
			for (const char* type : {"mouse_move", "mouse_down", "mouse_up"})
			{
				nlohmann::json event{{"type", type}, {"x", buttonX}, {"y", buttonY}};
				if (std::string(type) != "mouse_move") event["button"] = "left";
				Check(host.Request(nlohmann::json{{"action", "input"}, {"event", event}}.dump(), response, error), error);
			}
			Check(host.Draw(1280, 720, 16, error), error);
			Check(host.Request(R"({"path":"mainInterface","method":"IsWindowVisible","args":[0]})", response, error), error);
			Check(nlohmann::json::parse(response).at("value") == visible, "Authored talent window did not toggle through pointer events");
		}
		Check(host.Reset(), "Hero/action/minimap/input test teardown failed");
		PwRuffleClientInspection inspection;
		const std::array<uint8_t, 4> background{43, 67, 109, 255};
		PwRuffleMinimapState minimap;
		minimap.background = {background.data(), background.size(), 1, 1, 4};
		minimap.bounds = {0, 0, 100, 100};
		minimap.matchSeconds = 75;
		PwRuffleMinimapMarker marker;
		marker.worldX = marker.worldY = 50;
		marker.kind = PwRuffleMinimapMarker::Kind::Hero;
		marker.self = marker.visible = true;
		minimap.markers.push_back(marker);
		glEnd(); // A mock engine error must be reported separately, not disable composition.
		Check(inspection.Draw(argv[1], argv[2], 1280, 720, 16, hud, actions, minimap), inspection.Error());
		Check(inspection.Frames() == 1 && inspection.PriorGlErrors() == 1, "Prior engine GL error was lost");
		Check(inspection.HudCalls() == 5, "Hero initialization call count");
		const auto initialActionCalls = inspection.ActionCalls();
		Check(initialActionCalls > 0, "Talent initialization was not sent");
		Check(inspection.MinimapUploads() == 1, "Minimap initial upload missing");
		Check(inspection.Draw(argv[1], argv[2], 1280, 720, 16, hud, actions, minimap), inspection.Error());
		Check(inspection.MinimapUploads() == 1, "Unchanged minimap uploaded again");
		Check(inspection.ActionCalls() == initialActionCalls, "Unchanged talents were rebound");
		Check(inspection.HudCalls() == 5, "Unchanged hero was rebound");
		vitals.health = 207;
		minimap.markers[0].worldX = 75;
		Check(inspection.Draw(argv[1], argv[2], 1280, 720, 16, hud, updated, minimap), inspection.Error());
		Check(inspection.MinimapUploads() == 2, "Moving minimap marker not uploaded");
		Check(inspection.ActionCalls() > initialActionCalls, "Talent updates were not sent");
		Check(inspection.HudCalls() == 6, "Changed health was not sent");
		using Kind = PwRufflePointerCapture::Kind;
		inspection.Focus(true);
		inspection.TakeGameplayEvents();
		inspection.Pointer(Kind::Move, 285, 683, 0, 0, 1280, 720);
		Check(inspection.Pointer(Kind::Down, 285, 683, 0, 0, 1280, 720), "Authored shortcut press escaped");
		Check(inspection.Pointer(Kind::Up, 285, 683, 0, 0, 1280, 720), "Authored shortcut release escaped");
		Check(inspection.Draw(argv[1], argv[2], 1280, 720, 16, hud, updated, minimap), inspection.Error());
		const auto leaveEpoch = inspection.InputEpoch();
		inspection.Pointer(Kind::Leave, -1, -1, 0, 0, 1280, 720);
		Check(inspection.InputEpoch() != leaveEpoch, "Pointer leave did not cancel held gestures");
		const auto requests = inspection.TakeGameplayEvents();
		Check(std::count_if(requests.begin(), requests.end(), [](const auto& event)
		{
			return event.kind == PwRuffleGameplayEvent::Kind::TalentClicked && event.column == 2 && event.row == 0;
		}) == 1, "Original shortcut did not emit exact talent request");
		Check(inspection.TakeGameplayEvents().empty(), "Authored callback delivered twice");
		Check(inspection.MinimapBounds() && inspection.MinimapBounds()->maxX == 100,
			"Input has no composed minimap bounds");
		PwRuffleMinimapInput minimapInput;
		for (const unsigned button : {2u, 0u})
		{
			inspection.Pointer(Kind::Move, 1160, 565, button, 0, 1280, 720);
			Check(inspection.Pointer(Kind::Down, 1160, 565, button, 0, 1280, 720), "Minimap press escaped");
			Check(inspection.Pointer(Kind::Up, 1160, 565, button, 0, 1280, 720), "Minimap release escaped");
			Check(inspection.Draw(argv[1], argv[2], 1280, 720, 16, hud, updated, minimap), inspection.Error());
			inspection.Pointer(Kind::Leave, -1, -1, 0, 0, 1280, 720);
			int requests = 0;
			for (const auto& event : inspection.TakeGameplayEvents())
			{
				std::cout << "Minimap callback button=" << button << " command=" << event.command << " flag=" << event.flag
					<< " xy=" << event.x << ',' << event.y << '\n';
				const auto request = minimapInput.Consume(event, *inspection.MinimapBounds());
				if (request.action == PwRuffleMinimapRequest::Action::NoAction) continue;
				++requests;
				Check(request.action == (button == 2 ? PwRuffleMinimapRequest::Action::Move :
					PwRuffleMinimapRequest::Action::Camera), "Authored minimap button semantics changed");
				Check(request.worldX > 50 && request.worldX < 65 && request.worldY > 50 && request.worldY < 65,
					"Authored minimap normalized projection/orientation mismatch");
			}
			Check(requests == 1, "Authored minimap did not emit exactly one destination");
		}
		Check(inspection.Pointer(Kind::Down, 1160, 565, 0, 0, 1280, 720), "HUD press escaped to world");
		Check(inspection.Pointer(Kind::Up, 640, 100, 0, 0, 1280, 720), "HUD release escaped to world");
		Check(!inspection.Pointer(Kind::Down, 640, 100, 0, 0, 1280, 720), "World press was captured");
		Check(!inspection.Pointer(Kind::Up, 1160, 565, 0, 0, 1280, 720), "World release became a HUD click");
		Check(inspection.Pointer(Kind::Wheel, 1160, 565, 0, 1, 1280, 720), "HUD wheel escaped");
		Check(!inspection.Pointer(Kind::Wheel, 640, 100, 0, 1, 1280, 720), "World wheel captured");
		Check(inspection.Pointer(Kind::Down, 1160, 565, 0, 0, 1280, 720), "HUD focus-test press failed");
		const auto epoch = inspection.InputEpoch();
		inspection.Focus(false);
		Check(inspection.InputEpoch() != epoch, "Focus loss did not cancel gesture epoch");
		Check(inspection.PendingCallbacks() == 0, "Focus loss retained gameplay requests");
		inspection.Focus(true);
		const auto inputsBefore = inspection.PointerEvents();
		Check(inspection.Pointer(Kind::Up, 1160, 565, 0, 0, 1280, 720), "Orphan HUD release escaped");
		Check(inspection.PointerEvents() == inputsBefore, "Focus loss retained held pointer");
		Check(inspection.Pointer(Kind::Down, 1160, 565, 0, 0, 960, 768), "Stale resize coverage accepted");
		Check(inspection.Pointer(Kind::Up, 640, 100, 0, 0, 960, 768), "Stale resize release escaped to world");
		Check(inspection.Pointer(Kind::Wheel, 640, 100, 0, 1, 960, 768), "Stale resize wheel escaped to world");
		Check(inspection.Pointer(Kind::Down, 640, 100, 2, 0, 960, 768), "Second stale resize press escaped to world");
		Check(inspection.PointerEvents() == inputsBefore, "Resize forwarded stale pointer coordinates");
		inspection.Focus(true);
		Check(inspection.Draw(argv[1], argv[2], 960, 768, 16, hud, updated, minimap), inspection.Error());
		window.CheckCurrent();
		Check(inspection.Reset(), "Inspection teardown failed");
		Check(!inspection.MinimapBounds(), "Shutdown retained minimap input bounds");
		PwRuffleClientInspection missing;
		Check(!missing.Draw("/nonexistent/pw-ruffle.so", argv[2], 1280, 720, 16) &&
			missing.WasAttempted() && !missing.IsReady() && !missing.Error().empty(), "Missing-library fallback failed");
		Check(!missing.Draw(argv[1], argv[2], 1280, 720, 16), "Disabled inspection unexpectedly retried");
		std::cout << "Native Ruffle/GLX context probe PASS\n";
		return 0;
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
