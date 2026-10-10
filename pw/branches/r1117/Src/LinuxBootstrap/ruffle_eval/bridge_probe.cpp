#include "bridge.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using Json = nlohmann::json;

/** Keep all checks active in Release builds, unlike assert(). */
static void Check(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

/** Release response ownership through the same library that allocated it. */
struct Response
{
	PwRuffleBuffer bytes{};
	Response() = default;
	Response(const Response&) = delete;
	Response& operator=(const Response&) = delete;
	~Response() { pw_ruffle_buffer_free(&bytes); }
	Json Parse() const { return Json::parse(bytes.data, bytes.data + bytes.len); }
};

/** Minimal C++ consumer of the C ABI, with no knowledge of Rust or Tamarin types. */
struct Host
{
	PwRuffleHost id = 0;
	Host(const std::string& data, const std::string& movie)
	{
		const std::string config = Json{{"data", data}, {"movie", movie}}.dump();
		Response response;
		const int status = pw_ruffle_open(reinterpret_cast<const uint8_t*>(config.data()), config.size(), &id, &response.bytes);
		if (status != PW_RUFFLE_OK) throw std::runtime_error("Open failed: " + (response.bytes.len ? response.Parse().dump() : std::to_string(status)));
		Check(id != 0 && response.Parse().at("abi") == 1, "Invalid host/version");
	}
	Host(const Host&) = delete;
	Host& operator=(const Host&) = delete;
	~Host() { if (id) pw_ruffle_close(id); }
	Json Request(const Json& request, int expected = PW_RUFFLE_OK)
	{
		const std::string input = request.dump();
		Response response;
		const int status = pw_ruffle_request(id, reinterpret_cast<const uint8_t*>(input.data()), input.size(), &response.bytes);
		Json result = response.bytes.len ? response.Parse() : Json();
		if (status != expected) throw std::runtime_error("Request failed: " + input + " -> " + result.dump());
		return result;
	}
};

/** Build typed calls with a proven JSON library, not concatenated string escaping. */
static Json Call(const char* path, const char* method, Json arguments)
{
	return {{"path", path}, {"method", method}, {"args", arguments}};
}

int main(int argc, char** argv)
{
	try
	{
		Check(argc == 5, "Usage: PrimeWorldRuffleBridgeProbe DATA MOVIE FRAME.png loading|combat");
		Check(pw_ruffle_abi_version() == 1, "Unsupported ABI");
		PwRuffleHost invalid = 99;
		Response error;
		Check(pw_ruffle_open(nullptr, 0, &invalid, &error.bytes) != PW_RUFFLE_OK && invalid == 0, "Invalid input accepted");
		Host host(argv[1], argv[2]);
		const bool loading = std::string(argv[4]) == "loading";
		Check(loading || std::string(argv[4]) == "combat", "Unknown mode");
		host.Request(Call("LocalizationResources", "LocalizationComplete", Json::array()));
		host.Request({{"action", "tick"}, {"delta_ms", 16.0}});
		host.Request({{"action", "tick"}, {"delta_ms", -1}}, PW_RUFFLE_ERROR);
		host.Request({{"action", "input"}, {"event", {{"type", "focus"}, {"focused", true}}}});
		if (loading)
		{
			const Json name = Json::parse("\"\\u0411\\u043e\\u0442\"");
			host.Request({{"path", "LocalizationResources"}, {"op", "set"}, {"method", "BotName"}, {"args", {name}}});
			Check(host.Request({{"path", "LocalizationResources"}, {"op", "get"}, {"method", "BotName"}, {"args", Json::array()}}).at("value") == name, "UTF-8 round trip failed");
			host.Request(Call("LoaderWindowInterface", "SetForceColors", {{0, 100, 200}, {4294967295u, 4278255360u, 4294901760u}}));
			host.Request(Call("LoaderWindowInterface", "SetLoadingState", {true}));
			host.Request(Call("LoaderWindowInterface", "SetMapBack", {":/UI/Styles/LoadingBack/PVP.dds", ":/UI/Styles/LoadingBack/PVP_Logo.dds"}));
			for (const auto& position : {std::make_pair("backGround", -320), std::make_pair("logo", 295)})
				Check(host.Request({{"path", position.first}, {"op", "get"}, {"method", "x"}, {"args", Json::array()}}).at("value") == position.second, "Synchronous layout mismatch");
			host.Request(Call("LoaderWindowInterface", "SetLoadingStatusText", {"Prime World"}));
		}
		else
		{
			const auto text = host.Request({{"path", "chatBar.chatInput_mc"}, {"op", "get"}, {"method", "text_txt"}, {"args", Json::array()}}).at("id");
			host.Request({{"receiver", text}, {"op", "set"}, {"method", "type"}, {"args", {"input"}}});
			host.Request({{"receiver", text}, {"op", "set"}, {"method", "text"}, {"args", {""}}});
			host.Request({{"path", "stage"}, {"op", "set"}, {"method", "focus"}, {"args", {{{"$handle", text}}}}});
			const Json unicode = Json::parse("\"PW \\u0411\\u043e\\u0442 \\ud83d\\ude00\"");
			host.Request({{"action", "input"}, {"event", {{"type", "text"}, {"text", unicode}}}});
			Check(host.Request({{"receiver", text}, {"op", "get"}, {"method", "text"}, {"args", Json::array()}}).at("value") == unicode, "Native Unicode input did not reach chat");
			host.Request({{"action", "input"}, {"event", {{"type", "key_down"}, {"key", "Shift"}}}});
			host.Request({{"action", "input"}, {"event", {{"type", "focus"}, {"focused", false}}}});
			Check(host.Request({{"path", "stage"}, {"op", "get"}, {"method", "focus"}, {"args", Json::array()}}).at("type") == "null", "Focus loss did not clear the stage");
			host.Request({{"action", "input"}, {"event", {{"type", "text"}, {"text", "ignored"}}}});
			Check(host.Request({{"receiver", text}, {"op", "get"}, {"method", "text"}, {"args", Json::array()}}).at("value") == unicode, "Blurred chat accepted input");
			host.Request({{"action", "input"}, {"event", {{"type", "mouse_move"}, {"x", 2}, {"y", 2}}}});
			host.Request({{"action", "input"}, {"event", {{"type", "wheel"}, {"lines", -1}}}});
			host.Request({{"action", "release"}, {"id", text}});
			Check(host.Request(Call("mainInterface", "IsWindowVisible", {0})).at("type") == "boolean", "Boolean return lost");
			Check(host.Request(Call("mainInterface", "GetTalentActionBarIndex", {0, 0})).at("value") == -1, "Integer return lost");
			const auto object = host.Request(Call("mainInterface", "GetActionBarItemDisplayObject", {0, false})).at("id");
			host.Request({{"receiver", object}, {"op", "set"}, {"method", "visible"}, {"args", {false}}});
			host.Request({{"action", "step"}, {"frames", 120}});
			Check(host.Request({{"receiver", object}, {"op", "get"}, {"method", "visible"}, {"args", Json::array()}}).at("value") == false, "Rooted object lost");
			Check(host.Request({{"receiver", object}, {"method", "contains"}, {"args", {{{"$handle", object}}}}}).at("value") == true, "Object argument lost");
			host.Request({{"receiver", object}, {"op", "set"}, {"method", "visible"}, {"args", {true}}});
			Host other(argv[1], argv[2]);
			other.Request({{"receiver", object}, {"op", "get"}, {"method", "visible"}, {"args", Json::array()}}, PW_RUFFLE_ERROR);
			host.Request({{"action", "release"}, {"id", object}});
			host.Request({{"receiver", object}, {"op", "get"}, {"method", "visible"}, {"args", Json::array()}}, PW_RUFFLE_ERROR);
			Check(!host.Request({{"action", "events"}}).empty(), "FSCommands missing");
			Check(host.Request({{"action", "events"}}).empty(), "Callback poll duplicated events");
		}
		int wrongThread = 0;
		std::thread worker([&] { wrongThread = pw_ruffle_close(host.id); });
		worker.join();
		Check(wrongThread == PW_RUFFLE_INVALID_HOST, "Wrong-thread access accepted");
		const auto stats = host.Request({{"action", "stats"}});
		Check(stats.at("handles") == 0 && stats.at("runtime_errors") == 0, "Leaked handles or runtime errors");
		const auto frame = host.Request({{"action", "capture"}, {"path", argv[3]}});
		Check(frame.at("width") == 1280 && frame.at("height") == 720 && frame.at("non_background_pixels") > 1000, "Invalid framebuffer");
		for (const auto& size : {std::make_pair(640,480), std::make_pair(257,193), std::make_pair(1280,720)})
		{
			host.Request({{"action", "surface"}, {"width", size.first}, {"height", size.second}, {"transparent", true}});
			host.Request({{"action", "surface"}, {"width", 0}, {"height", size.second}, {"transparent", true}}, PW_RUFFLE_ERROR);
			PwRuffleFrame pixels{};
			Response diagnostic;
			const int result = pw_ruffle_render(host.id, &pixels, &diagnostic.bytes);
			const bool dimensions = pixels.width == unsigned(size.first) && pixels.height == unsigned(size.second) &&
				pixels.stride == pixels.width * 4 && pixels.rgba.len == size_t(pixels.stride) * pixels.height;
			size_t transparent = 0, visible = 0;
			for (size_t i = 3; i < pixels.rgba.len; i += 4)
			{
				transparent += pixels.rgba.data[i] == 0;
				visible += pixels.rgba.data[i] > 0;
			}
			pw_ruffle_buffer_free(&pixels.rgba);
			Check(result == PW_RUFFLE_OK && dimensions && visible > 100, "Raw frame layout failed");
			Check(loading || transparent > 100, "Combat background is not transparent");
		}
		const auto id = host.id;
		Check(pw_ruffle_close(id) == PW_RUFFLE_OK, "Close failed");
		host.id = 0;
		Check(pw_ruffle_close(id) == PW_RUFFLE_INVALID_HOST, "Stale host accepted");
		std::cout << "Native C++/Ruffle ABI " << argv[4] << " PASS " << frame.dump() << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
