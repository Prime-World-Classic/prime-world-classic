#include "client_inspection.h"
#include "hud_calls.h"
#include "action_calls.h"
#include <GL/gl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

void PwRuffleClientInspection::Focus(bool focused)
{
	if (!host_.IsReady() || (focused_ && *focused_ == focused)) return;
	std::string response;
	if (!host_.Request(nlohmann::json{{"action", "input"}, {"event", {{"type", "focus"},
		{"focused", focused}}}}.dump(), response, error_))
	{
		std::fprintf(stderr, "Ruffle focus disabled: %s\n", error_.c_str());
		Reset();
		return;
	}
	focused_ = focused;
	if (!focused) { pointerCapture_.Reset(); events_.Clear(); }
}

bool PwRuffleClientInspection::Pointer(PwRufflePointerCapture::Kind kind, int x, int y,
	unsigned button, double wheelLines, unsigned width, unsigned height)
{
	using Kind = PwRufflePointerCapture::Kind;
	if (!host_.IsReady()) return false;
	if (!host_.MatchesViewport(width, height))
	{
		Focus(false);
		return kind == Kind::Down || kind == Kind::Up || kind == Kind::Wheel;
	}
	if (focused_ && !*focused_) return false;
	try
	{
		std::string response;
		const auto request = [&](const nlohmann::json& value)
		{
			if (!host_.Request(value.dump(), response, error_)) throw std::runtime_error(error_);
		};
		bool hit = host_.ContainsPixel(x, y);
		if (kind == Kind::Down || kind == Kind::Up || kind == Kind::Wheel)
		{
			// Flash uses an invisible modal shield. Alpha coverage alone misses it.
			request({{"path", "EscMenuNonclickable_mc"}, {"op", "get"}, {"method", "visible"}, {"args", nlohmann::json::array()}});
			hit = hit || nlohmann::json::parse(response).at("value").get<bool>();
		}
		const auto decision = pointerCapture_.Route(kind, hit, button);
		if (decision.forward)
		{
			nlohmann::json event;
			if (kind == Kind::Leave) event = {{"type", "mouse_leave"}};
			else if (kind == Kind::Wheel) event = {{"type", "wheel"}, {"lines", wheelLines}};
			else
			{
				event = {{"type", kind == Kind::Move ? "mouse_move" : kind == Kind::Down ? "mouse_down" : "mouse_up"},
					{"x", x}, {"y", y}};
				if (kind != Kind::Move)
				{
					const char* buttons[] = {"left", "middle", "right"};
					event["button"] = buttons[button];
				}
			}
			request({{"action", "input"}, {"event", event}});
			++pointerEvents_;
		}
		if (decision.consume) ++consumedPointerEvents_;
		return decision.consume;
	}
	catch (const std::exception& error)
	{
		error_ = error.what();
		std::fprintf(stderr, "Ruffle pointer disabled: %s\n", error_.c_str());
		Reset();
		return true; // A failed HUD gesture must not become a world command.
	}
}

bool PwRuffleClientInspection::Draw(const std::string& library, const std::string& data,
	unsigned width, unsigned height, double deltaMs, const PwRuffleHudState& hud,
	const PwRuffleActionState& actions, const PwRuffleMinimapState& minimap)
{
	if (library.empty() || (attempted_ && !host_.IsReady())) return false;
	try
	{
		// Establish the clean error boundary required by the compositor. Keep prior
		// engine failures visible and separate; never discard errors after drawing.
		for (unsigned pending = 0; pending < 16; ++pending)
		{
			const GLenum code = glGetError();
			if (code == GL_NO_ERROR) break;
			if (++priorGlErrors_ == 1)
				std::fprintf(stderr, "Ruffle inspection: pre-existing engine GL error %u\n", code);
			if (pending == 15) throw std::runtime_error("Engine GL error queue did not clear");
		}
		std::string response;
		const auto request = [&](const char* json)
		{
			if (!host_.Request(json, response, error_)) throw std::runtime_error(error_);
		};
		if (!attempted_)
		{
			attempted_ = true;
			const auto movie = std::filesystem::path(data) / "UI/Screens/Combat/Flash/main.swf";
			if (!host_.Open(library, data, movie.string(), error_)) throw std::runtime_error(error_);
			request(R"({"path":"LocalizationResources","method":"LocalizationComplete","args":[]})");
			request(R"({"path":"mainInterface","method":"HideAllWindows","args":[]})");
			request(R"({"action":"step","frames":3})");
		}
		if (!std::isfinite(deltaMs)) throw std::runtime_error("Nonfinite inspection frame time");
		const auto identities = PwRuffleHeroIdentityCalls(hud);
		const auto values = PwRuffleHeroValueCalls(hud);
		if (!identity_.empty() && identity_ != identities.dump())
			throw std::runtime_error("Hero identity changed; reopen inspection for the new session");
		if (identity_.empty() && !identities.empty())
		{
			for (const auto& call : identities) { request(call.dump().c_str()); ++hudCalls_; }
			identity_ = identities.dump();
		}
		if (values_ != values.dump())
		{
			for (const auto& call : values) { request(call.dump().c_str()); ++hudCalls_; }
			values_ = values.dump();
		}
		const auto actionCalls = actions_ ? PwRuffleActionUpdateCalls(actions, *actions_) : PwRuffleActionInitCalls(actions);
		if (!actionCalls.empty() && identity_.empty()) throw std::runtime_error("Talents require hero identity");
		for (const auto& call : actionCalls) { request(call.dump().c_str()); ++actionCalls_; }
		if (!actions.talents.empty()) actions_ = actions;
		if (minimap.background.pixels)
		{
			auto frame = PwRuffleBuildMinimapFrame(minimap.background, minimap.bounds, minimap.markers);
			PwRuffleClipMinimapFrame(frame);
			if (minimapBitmap_.empty())
			{
				request(nlohmann::json{{"action", "bitmap_create"}, {"width", frame.width}, {"height", frame.height}}.dump().c_str());
				minimapBitmap_ = nlohmann::json::parse(response).at("id").get<std::string>();
				request(nlohmann::json{{"path", "miniMap_mc.miniMapAnim_mc.mapImage"}, {"op", "set"},
					{"method", "bitmapData"}, {"args", {{{"$handle", minimapBitmap_}}}}}.dump().c_str());
				request(R"({"path":"miniMap_mc.miniMapAnim_mc.mapImage","op":"set","method":"smoothing","args":[false]})");
				request(R"({"path":"miniMap_mc.miniMapAnim_mc","method":"correctSize","args":[]})");
			}
			if (minimapPixels_ != frame.rgba)
			{
				if (!host_.UploadBitmap(std::stoull(minimapBitmap_), frame.width, frame.height,
					frame.rgba.data(), frame.rgba.size(), error_)) throw std::runtime_error(error_);
				minimapPixels_ = frame.rgba;
				++minimapUploads_;
			}
		}
		if (minimap.matchSeconds && *minimap.matchSeconds != matchSeconds_)
		{
			if (*minimap.matchSeconds < 0) throw std::runtime_error("Negative simulation clock");
			request(nlohmann::json{{"path", "miniMap_mc"}, {"op", "set"}, {"method", "GameTime"},
				{"args", {*minimap.matchSeconds}}}.dump().c_str());
			matchSeconds_ = *minimap.matchSeconds;
		}
		if (!host_.Draw(width, height, std::clamp(deltaMs, 0.0, 250.0), error_))
			throw std::runtime_error(error_);
		// Publish only after the frame and runtime diagnostics pass. Execution belongs
		// to the client command loop, never to a renderer or a Flash callback stack.
		request(R"({"action":"events"})");
		const auto callbacks = response;
		request(R"({"action":"stats"})");
		if (nlohmann::json::parse(response).at("runtime_errors") != 0)
			throw std::runtime_error("Combat SWF reported runtime errors");
		events_.Append(callbacks);
		++frames_;
		return true;
	}
	catch (const std::exception& error)
	{
		error_ = error.what();
		std::fprintf(stderr, "Ruffle inspection disabled: %s\n", error_.c_str());
		Reset();
		return false;
	}
}
