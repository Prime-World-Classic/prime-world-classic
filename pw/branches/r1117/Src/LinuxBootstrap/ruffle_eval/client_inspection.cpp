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
		// Drain only; inspection must never execute an unbound gameplay command.
		request(R"({"action":"events"})");
		callbacks_ += nlohmann::json::parse(response).size();
		request(R"({"action":"stats"})");
		if (nlohmann::json::parse(response).at("runtime_errors") != 0)
			throw std::runtime_error("Combat SWF reported runtime errors");
		++frames_;
		return true;
	}
	catch (const std::exception& error)
	{
		error_ = error.what();
		std::fprintf(stderr, "Ruffle inspection disabled: %s\n", error_.c_str());
		host_.Reset();
		return false;
	}
}
