#include "client_inspection.h"
#include <GL/gl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

bool PwRuffleClientInspection::Draw(const std::string& library, const std::string& data,
	unsigned width, unsigned height, double deltaMs)
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
