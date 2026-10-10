#include "gameplay_events.h"
#include "gameplay_event_queue.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using Json = nlohmann::json;
using Event = PwRuffleGameplayEvent;
using Kind = Event::Kind;
using Limits = PwRuffleGameplayEventLimits;
int checks = 0;

/** Assertions intentionally remain enabled under NDEBUG. */
void Check(bool condition, const char* message)
{
	++checks;
	if (!condition) throw std::runtime_error(message);
}

/** Build precisely the tuple emitted by runtime.rs, not an invented object API. */
Json Pair(const std::string& command, const std::string& args)
{
	return Json::array({command, args});
}

/** Decode a one-element mock host queue through the public JSON boundary. */
Event One(const std::string& command, const std::string& args)
{
	const auto events = PwRuffleDecodeGameplayEvents(Json::array({Pair(command, args)}).dump());
	Check(events.size() == 1, "one callback produces one result");
	Check(events.front().command == command, "production callback spelling preserved");
	return events.front();
}

/** Reject the whole reply; caller-visible output must not acquire a valid prefix. */
void Reject(const std::string& input)
{
	std::vector<Event> output;
	bool rejected = false;
	try { output = PwRuffleDecodeGameplayEvents(input); }
	catch (const std::invalid_argument&) { rejected = true; }
	Check(rejected, "malformed callback must throw invalid_argument");
	Check(output.empty(), "malformed batch cannot return a partial prefix");
}

/** Exercise malformed command arguments while keeping the JSON envelope valid. */
void RejectArgs(const std::string& command, const std::string& args)
{
	Reject(Json::array({Pair(command, args)}).dump());
}

/** Pin original purchase/shortcut shape, bounds, duplicates and wire ordering. */
void TalentContracts()
{
	Check(PwRuffleDecodeGameplayEvents("[]").empty(), "empty queue is a no-op");
	for (int column = 0; column < 6; ++column)
		for (int row = 0; row < 6; ++row)
		{
			const auto event = One("TalentClicked", std::to_string(column) + " " + std::to_string(row));
			Check(event.kind == Kind::TalentClicked, "talent request classified");
			Check(event.column == column && event.row == row, "column and row never transposed");
		}
	const auto events = PwRuffleDecodeGameplayEvents(Json::array({
		Pair("TalentClicked", "5 1"), Pair("TalentToolTip", "1 2 4"),
		Pair("TalentClicked", "0 3"), Pair("TalentClicked", "5 1")}).dump());
	Check(events.size() == 4 && events[0].column == 5 && events[1].kind == Kind::Informational &&
		events[2].row == 3 && events[3].column == 5, "queue order and duplicate requests preserved");
	const auto spaced = One("TalentClicked", " \t5\r\n\v\f1 \t");
	Check(spaced.column == 5 && spaced.row == 1, "ASCII whitespace tokenization");
	for (const char* args : {"", "1", "1 2 3", "6 0", "0 6", "-1 0", "0 -1", "2147483648 0",
		"4294967296 0", "18446744073709551616 0", "1.0 2", "1e0 2", "+1 2", "0x1 2",
		"1 2tail", "1,2", "NaN 0", "Infinity 0", "1-2", "1\xC2\xA0" "2"})
		RejectArgs("TalentClicked", args);
}

/** Original tooltip notifications are distinct from unknown or forbidden requests. */
void NonRequests()
{
	for (const char* command : {"TalentToolTip", "TalentActionToolTip"})
	{
		for (const char* args : {"0 5 1", "1 2 4"})
		{
			const auto event = One(command, args);
			Check(event.kind == Kind::Informational, "tooltip cannot become an activation");
			Check(event.column == -1 && event.row == -1, "informational results contain no talent request");
		}
		for (const char* args : {"1", "1 0", "2 0 0", "-1 0 0", "1 6 0", "0 0 6", "1 0 0 0"})
			RejectArgs(command, args);
	}
	for (const char* command : {"UseSlot", "ActionBarClicked", "TalentActionClicked", "talentclicked",
		"TalentClickedExtra", "PortalClick", "InventoryItemClicked", "ShopItemClicked", "BuyImpulseBuff",
		"SignalMouseClick", "CameraMouseClick", "ActionBarLock", "ActionBarShowPanel", "EscMenuExitGame",
		"SendMessage", "UnknownCommand"})
	{
		const auto event = One(command, "0 0");
		Check(event.kind == Kind::Unsupported && event.column == -1 && event.row == -1,
			"unknown/forbidden callbacks never fall through to TalentClicked");
	}
	Check(One("SendMessage", u8"mock \u043f\u0440\u0438\u0432\u0435\u0442").kind == Kind::Unsupported,
		"unsupported UTF-8 text is inert");
}

/** Keep original normalized axes/button flags, including valid out-of-map motion. */
void MinimapContracts()
{
	const auto over = One("MinimapMouseOver", "1 0.25 0.75");
	Check(over.kind == Kind::MinimapMouseOver && over.flag && over.x == 0.25f && over.y == 0.75f,
		"minimap hover shape and axes");
	const auto out = One("MinimapMouseOver", "0 -0.125 1.25");
	Check(!out.flag && out.x == -0.125f && out.y == 1.25f, "mouseout coordinates are not clamped");
	const auto down = One("MinimapMouseDown", "1 0 1");
	Check(down.kind == Kind::MinimapMouseDown && down.flag && down.x == 0 && down.y == 1,
		"1 means left button");
	const auto up = One("MinimapMouseUp", "0 1 0");
	Check(up.kind == Kind::MinimapMouseUp && !up.flag && up.x == 1 && up.y == 0,
		"0 means right button, mouse-up still has coordinates");
	const auto move = One("MinimapActionMove", "1.25e-1 -2.5E+0");
	Check(move.kind == Kind::MinimapActionMove && move.x == 0.125f && move.y == -2.5f,
		"minimap move has two floats, no button flag");
	for (const char* command : {"MinimapMouseOver", "MinimapMouseDown", "MinimapMouseUp"})
		for (const char* args : {"", "1", "1 0", "0 0 0 0", "2 0 0", "-1 0 0", "true 0 0", "1.0 0 0",
			"1 NaN 0", "0 0 inf", "0 0 1e40", "1 0 0junk"})
			RejectArgs(command, args);
	for (const char* args : {"", "0", "0 0 0", "nan 0", "NaN 0", "inf 0", "-Infinity 0", "0 Infinity",
		"1e309 0", "0 -1e40", "1e-999 0", "0 1e-999", "0x1p2 0", "1,5 0", "1.2.3 0", "1e 0",
		"1e+ 0", "+1 0", "0 0tail"})
		RejectArgs("MinimapActionMove", args);
}

/** Strict tuples, valid JSON/UTF-8 and bounded bytes/nesting precede any request. */
void EnvelopeAndLimits()
{
	for (const char* input : {"", "null", "true", "42", "{}", "[{}]", "[null]", "[] trailing", "[",
		"[\"TalentClicked\",\"1 2\"]", "[[\"TalentClicked\"]]", "[[\"TalentClicked\",\"1 2\",\"3\"]]",
		"[[\"TalentClicked\",12]]", "[[\"TalentClicked\",true]]", "[[\"TalentClicked\",null]]",
		"[[12,\"1 2\"]]", "[[\"TalentClicked\",[]]]", "[[\"TalentClicked\",{}]]", "[[\"TalentClicked\",NaN]]",
		"[[\"TalentClicked\",\"1 2\"],]", "[[\"TalentClicked\",\"1 2\"]] []", "/*comment*/[]"})
		Reject(input);
	for (const char* command : {"", "TalentClicked ", " TalentClicked", "Talent-Clicked"})
		RejectArgs(command, "1 2");
	RejectArgs(std::string("TalentClicked\0ignored", 21), "1 2");
	RejectArgs("TalentClicked", std::string("1 2\0ignored", 11));
	RejectArgs("Unknown", std::string(1, '\x01'));
	RejectArgs("Unknown", std::string(1, '\x7f'));
	Reject(std::string("[[\"Unknown\",\"") + char(0xff) + "\"]]");
	Reject("[[\"Unknown\",\"\\ud800\"]]");
	Reject(std::string(4096, '[') + std::string(4096, ']'));
	Reject(std::string(Limits::MaxBatchBytes + 1, ' '));
	const std::string padded = "[]" + std::string(Limits::MaxBatchBytes - 2, ' ');
	Check(PwRuffleDecodeGameplayEvents(padded).empty(), "exact batch byte limit accepted");
	Check(One(std::string(Limits::MaxCommandBytes, 'X'), "").kind == Kind::Unsupported,
		"exact command byte limit accepted");
	RejectArgs(std::string(Limits::MaxCommandBytes + 1, 'X'), "");
	Check(One("Unknown", std::string(Limits::MaxArgumentBytes, 'x')).kind == Kind::Unsupported,
		"exact argument byte limit accepted");
	RejectArgs("Unknown", std::string(Limits::MaxArgumentBytes + 1, 'x'));
	RejectArgs("TalentClicked", std::string(Limits::MaxArgumentBytes - 2, '9') + " 0");
	auto queue = Json::array();
	for (std::size_t i = 0; i < Limits::MaxEvents; ++i) queue.push_back(Pair("TalentClicked", "5 1"));
	Check(PwRuffleDecodeGameplayEvents(queue.dump()).size() == Limits::MaxEvents, "exact queue limit accepted");
	queue.push_back(Pair("TalentClicked", "5 1"));
	Reject(queue.dump());
	Reject(Json::array({Pair("TalentClicked", "5 1"), Pair("TalentClicked", "1"),
		Pair("TalentClicked", "0 3")}).dump());
}

/** Mock consumer records requests only; live validation/targeting belongs to the client. */
void QueueContracts()
{
	PwRuffleGameplayEventQueue queue;
	queue.Append(R"([["TalentClicked","5 1"],["Unknown","ignored"],["TalentToolTip","0 5 1"]])");
	Check(queue.Pending() == 1 && queue.Received() == 3 && queue.Discarded() == 2, "queue allowlist counts");
	try { queue.Append(R"([["TalentClicked","2 3"],["TalentClicked","6 0"]])"); Check(false, "queue accepted invalid suffix"); }
	catch (const std::invalid_argument&) {}
	Check(queue.Pending() == 1 && queue.Received() == 3, "failed batch left a prefix or counters");
	auto first = queue.Drain();
	Check(first.size() == 1 && first[0].row == 1 && first[0].column == 5, "ordered owning handoff");
	Check(queue.Drain().empty(), "callback delivered twice");
	Json full = Json::array();
	for (std::size_t i = 0; i < Limits::MaxEvents; ++i) full.push_back(Pair("TalentClicked", "0 0"));
	queue.Append(full.dump());
	try { queue.Append(R"([["TalentClicked","1 1"]])"); Check(false, "queue overflow accepted"); }
	catch (const std::invalid_argument&) {}
	Check(queue.Pending() == Limits::MaxEvents && queue.Received() == 3 + Limits::MaxEvents, "overflow changed queue");
	queue.Clear();
	Check(queue.Drain().empty(), "shutdown retained requests");
	queue.Append(R"([["MinimapMouseDown","0 0.25 0.75"]])");
	Check(queue.Drain().front().kind == Kind::MinimapMouseDown, "queue cannot recover after clear");
}

void MockHandoff()
{
	struct MockClient
	{
		int purchases = 0;
		int uses = 0;
		int level = -1;
		int slot = -1;

		/** Simulate client policy without implementing commands or altering a real hero. */
		void Observe(const Event& event, bool bought, bool clientAllows)
		{
			if (event.kind != Kind::TalentClicked || !clientAllows) return;
			level = event.row;
			slot = event.column;
			if (bought) ++uses;
			else ++purchases;
		}
	} client;
	const auto event = One("TalentClicked", "5 1");
	Check(client.purchases == 0 && client.uses == 0, "decoding has no side effects");
	client.Observe(event, false, false);
	client.Observe(event, true, false);
	Check(client.purchases == 0 && client.uses == 0, "syntactically valid request is not permission");
	client.Observe(event, false, true);
	client.Observe(event, true, true);
	Check(client.purchases == 1 && client.uses == 1 && client.level == 1 && client.slot == 5,
		"live state chooses purchase/use, native handoff uses level then slot");
	for (const auto& ignored : {One("TalentActionToolTip", "1 5 1"), One("UseSlot", "0"),
		One("MinimapMouseDown", "1 0.5 0.5")})
		client.Observe(ignored, true, true);
	Check(client.purchases == 1 && client.uses == 1, "non-talent callbacks cannot activate talents");
}
}

/** Standalone C++17 mock probe: link only gameplay_events.cpp and nlohmann headers. */
int main()
{
	try
	{
		TalentContracts();
		NonRequests();
		MinimapContracts();
		EnvelopeAndLimits();
		MockHandoff();
		QueueContracts();
		std::cout << "gameplay_events_probe: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "gameplay_events_probe: " << error.what() << '\n';
		return 1;
	}
}
