#include "action_calls.h"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
using Json = nlohmann::json;
using State = PwRuffleActionState;
using Purchase = State::PurchaseState;
using Slot = State::SlotState;
int checks = 0;

/** Assertions remain effective in release/NDEBUG builds. */
void Check(bool condition, const std::string& message)
{
	++checks;
	if (!condition) throw std::runtime_error(message);
}

/** Real-shaped mock records only; no production defaults are copied from this fixture. */
State Fixture()
{
	State::Talent automatic;
	automatic.column = 2;
	automatic.row = 1;
	automatic.iconPath = u8"/UI/Talents/\u043e\u0433\u043e\u043d\u044c.dds";
	automatic.alternativeIconPath = "UI/Talents/second.dds";
	automatic.active = true;
	automatic.desiredIndex = -1;
	automatic.upgradeLevel = 3;
	automatic.classTalent = false;
	automatic.cost = 137;
	automatic.purchase = Purchase::Bought;
	automatic.status = Slot::ActiveSpecial;
	automatic.cooldown = 2.5;
	automatic.maxCooldown = 8.75;
	automatic.alternativeState = true;
	auto reserved = automatic;
	reserved.column = 5;
	reserved.row = 3;
	reserved.iconPath = ":/UI/Talents/fixed.dds";
	reserved.alternativeIconPath.clear();
	reserved.desiredIndex = 0;
	reserved.upgradeLevel = 1;
	reserved.classTalent = true;
	reserved.cost = 293;
	reserved.purchase = Purchase::CanBuy;
	reserved.status = Slot::NotEnoughMana;
	reserved.cooldown = 0;
	reserved.maxCooldown = 17;
	reserved.alternativeState = false;
	return State{{automatic, reserved}};
}

/** Full envelope equality pins argument order, arity, JSON types, and method spelling. */
void Call(const Json& call, const char* method, const Json& args)
{
	Check(call == Json({{"action", "invoke"}, {"path", "mainInterface"},
		{"method", method}, {"args", args}}), std::string("Wrong contract: ") + method);
}

/** Minimal mock of only the authored shortcut-creation/reservation conditions. */
struct AuthoredBarMock
{
	std::array<int, 36> purchase{};
	std::array<bool, 36> active{};
	std::array<int, 36> desired{};
	std::array<bool, 10> reserved{};
	std::array<int, 10> occupants{};
	int shortcuts = 0;
	int notifications = 0;

	AuthoredBarMock()
	{
		purchase.fill(3); // Deliberately not eligible; bootstrap must establish a buying transition.
		desired.fill(-2);
		occupants.fill(-1);
	}

	void Apply(const Json& calls)
	{
		for (const auto& call : calls)
		{
			const auto method = call.at("method").get<std::string>();
			const auto& args = call.at("args");
			if (method == "OnTalentsStateChanged") { ++notifications; continue; }
			const auto cell = args[1].get<std::size_t>() * 6 + args[0].get<std::size_t>();
			if (method == "SetTalentIcon")
			{
				active[cell] = args[4].get<bool>();
				desired[cell] = args[5].get<int>();
				if (active[cell] && desired[cell] >= 0) reserved[desired[cell]] = true;
			}
			else if (method == "SetTalentState")
			{
				const int next = args[2].get<int>();
				if ((purchase[cell] == 1 || purchase[cell] == 2) && next == 0 && active[cell])
				{
					int target = desired[cell];
					if (target == -1)
						for (int i = 0; i < 10; ++i)
							if (occupants[i] == -1 && !reserved[i]) { target = i; break; }
					if (target >= 0 && occupants[target] == -1)
					{
						occupants[target] = static_cast<int>(cell);
						++shortcuts;
					}
				}
				purchase[cell] = next;
			}
		}
	}
};

void ContractsAndBuying()
{
	State state = Fixture();
	const auto calls = PwRuffleActionInitCalls(state);
	Check(calls.size() == 8, "Init count");
	Call(calls[0], "SetTalentIcon", Json::array({2, 1,
		u8":/UI/Talents/\u043e\u0433\u043e\u043d\u044c.dds", ":/UI/Talents/second.dds", true, -1, 3, false, 137}));
	Call(calls[1], "SetTalentIcon", Json::array({5, 3, ":/UI/Talents/fixed.dds", "", true, 0, 1, true, 293}));
	Call(calls[2], "SetTalentState", Json::array({2, 1, 2}));
	Call(calls[3], "SetTalentState", Json::array({2, 1, 0}));
	Call(calls[4], "SetTalentStatus", Json::array({2, 1, 1, 2.5, 8.75, true}));
	Call(calls[5], "SetTalentState", Json::array({5, 3, 1}));
	Call(calls[6], "SetTalentStatus", Json::array({5, 3, 3, 0.0, 17.0, false}));
	Call(calls[7], "OnTalentsStateChanged", Json::array());
	Check(Json::parse(calls.dump()) == calls, "UTF-8 round trip");
	AuthoredBarMock mock;
	// Removing the bootstrap reproduces the missing-bought-shortcut condition.
	auto broken = calls;
	broken.erase(broken.begin() + 2);
	mock.Apply(broken);
	Check(mock.shortcuts == 0, "Mock failed to expose missing buying transition");
	mock = AuthoredBarMock{};
	mock.Apply(calls);
	Check(mock.shortcuts == 1 && mock.occupants[1] == 8 && mock.occupants[0] == -1,
		"Automatic shortcut stole an unbought talent's reserved position");
	Check(mock.notifications == 1, "Missing batched notification");
	Check(PwRuffleActionUpdateCalls(state, state).empty(), "Unchanged snapshot emitted calls");
	auto previous = state;
	state.talents[1].purchase = Purchase::Bought;
	auto update = PwRuffleActionUpdateCalls(state, previous);
	Check(update.size() == 3, "CanBuy -> Bought update count");
	Call(update[0], "SetTalentState", Json::array({5, 3, 0}));
	Call(update[1], "SetTalentStatus", Json::array({5, 3, 3, 0.0, 17.0, false}));
	Call(update[2], "OnTalentsStateChanged", Json::array());
	mock.Apply(update);
	Check(mock.shortcuts == 2 && mock.occupants[0] == 23, "Bought fixed talent not installed");
	mock.Apply(PwRuffleActionUpdateCalls(state, state));
	Check(mock.shortcuts == 2, "Repeat snapshot duplicated a shortcut");
	previous.talents[1].purchase = Purchase::NotEnoughPrime;
	update = PwRuffleActionUpdateCalls(state, previous);
	Check(update.size() == 4, "NoMoney -> Bought needs bridge state");
	Call(update[0], "SetTalentState", Json::array({5, 3, 2}));
	Call(update[1], "SetTalentState", Json::array({5, 3, 0}));
	previous = state;
	state.talents[0].cooldown = 7.25;
	state.talents[0].status = Slot::Chosen;
	state.talents[0].alternativeState = false;
	update = PwRuffleActionUpdateCalls(state, previous);
	Check(update.size() == 1, "Status update recreated icons/states");
	Call(update[0], "SetTalentStatus", Json::array({2, 1, 5, 7.25, 8.75, false}));
	std::reverse(state.talents.begin(), state.talents.end());
	Check(PwRuffleActionUpdateCalls(state, previous) == update, "Input order changed output");
}

/** Cover all supported purchase transitions, including the unaffordable -> bought edge. */
void PurchaseTransitions()
{
	for (const bool active : {false, true})
		for (int before = 0; before < 4; ++before)
			for (int after = 0; after < 4; ++after)
			{
				if (before == 0 && after != 0) continue; // Respec is explicitly unsupported.
				auto previous = Fixture();
				previous.talents.resize(1);
				previous.talents[0].active = active;
				previous.talents[0].purchase = static_cast<Purchase>(before);
				auto state = previous;
				state.talents[0].purchase = static_cast<Purchase>(after);
				AuthoredBarMock mock;
				mock.Apply(PwRuffleActionInitCalls(previous));
				const auto update = PwRuffleActionUpdateCalls(state, previous);
				mock.Apply(update);
				Check(mock.purchase[8] == after, "Purchase transition lost the actual state");
				Check(mock.shortcuts == (active && after == 0 ? 1 : 0), "Transition lost/duplicated shortcut");
				Check(update.empty() == (before == after), "Purchase diff did not match changes");
				if (before != after)
					Call(update.back(), "OnTalentsStateChanged", Json::array());
				Check(PwRuffleActionUpdateCalls(state, state).empty(), "Transition is not stable");
			}
	const auto previous = Fixture();
	for (int field = 0; field < 4; ++field)
	{
		auto state = previous;
		if (field == 0) state.talents[0].status = Slot::NotEnoughLife;
		if (field == 1) state.talents[0].cooldown = 1.25;
		if (field == 2) state.talents[0].maxCooldown = 19.5;
		if (field == 3) state.talents[0].alternativeState = false;
		const auto update = PwRuffleActionUpdateCalls(state, previous);
		Check(update.size() == 1 && update[0]["method"] == "SetTalentStatus", "Single-field status update missed");
	}
	auto state = previous;
	state.talents[1].iconPath = "/UI/Talents/fixed.dds";
	Check(PwRuffleActionUpdateCalls(state, previous).empty(), "Equivalent resource prefixes changed metadata");
}

/** Demand explicit failures, including when the invalid data is in the prior snapshot. */
void Reject(const std::string& field, const std::function<void(State&)>& change)
{
	const State good = Fixture();
	State bad = good;
	change(bad);
	for (int mode = 0; mode < 3; ++mode)
	{
		bool rejected = false;
		try
		{
			if (mode == 0) static_cast<void>(PwRuffleActionInitCalls(bad));
			else if (mode == 1) static_cast<void>(PwRuffleActionUpdateCalls(bad, good));
			else static_cast<void>(PwRuffleActionUpdateCalls(good, bad));
		}
		catch (const std::invalid_argument& e)
		{
			rejected = true;
			Check(std::string(e.what()).find(field) != std::string::npos, "Wrong diagnostic: " + std::string(e.what()));
		}
		Check(rejected, "Accepted invalid " + field);
	}
}

void InvalidInputs()
{
	Reject("column", [](State& s) { s.talents[0].column = 6; });
	Reject("column", [](State& s) { s.talents[0].column = -1; });
	Reject("row", [](State& s) { s.talents[0].row = -1; });
	Reject("row", [](State& s) { s.talents[0].row = 6; });
	Reject("talents", [](State& s) { s.talents.resize(37); });
	Reject("duplicate", [](State& s) { s.talents.push_back(s.talents[0]); });
	Reject("desiredIndex", [](State& s) { s.talents[0].desiredIndex = 10; });
	Reject("desiredIndex", [](State& s) { s.talents[0].desiredIndex = -3; });
	Reject("collision", [](State& s) { s.talents[0].desiredIndex = 0; });
	Reject("upgradeLevel", [](State& s) { s.talents[0].upgradeLevel = 4; });
	Reject("cost", [](State& s) { s.talents[0].cost = -1; });
	Reject("cost", [](State& s) { s.talents[0].cost = std::int64_t{1} << 31; });
	Reject("active", [](State& s) { s.talents[0].active.reset(); });
	Reject("classTalent", [](State& s) { s.talents[0].classTalent.reset(); });
	Reject("alternativeState", [](State& s) { s.talents[0].alternativeState.reset(); });
	Reject("purchase", [](State& s) { s.talents[0].purchase = static_cast<Purchase>(4); });
	Reject("purchase", [](State& s) { s.talents[0].purchase = Purchase::Invalid; });
	Reject("status", [](State& s) { s.talents[0].status = static_cast<Slot>(7); });
	Reject("status", [](State& s) { s.talents[0].status = Slot::Invalid; });
	for (const double bad : {-0.1, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::max()})
	{
		Reject("cooldown", [bad](State& s) { s.talents[0].cooldown = bad; });
		Reject("maxCooldown", [bad](State& s) { s.talents[0].maxCooldown = bad; });
	}
	for (const std::string path : {"", "../a.dds", ":/UI/../a.dds", ":/UI/./a.dds", ":/",
		"UI//a.dds", "UI/a.dds/", "C:/a.dds", "http://a.dds", "UI\\a.dds", "UI/\na.dds",
		"UI/\xc0\xaf.dds", "UI/\xed\xa0\x80.dds", "UI/\xf4\x90\x80\x80.dds", "UI/\xe2\x82"})
		Reject("iconPath", [path](State& s) { s.talents[0].iconPath = path; });
	Reject("iconPath", [](State& s) { s.talents[0].iconPath = std::string("a\0.dds", 6); });
	Reject("iconPath", [](State& s) { s.talents[0].iconPath = std::string(State::MaxResourceBytes, 'x'); });
	Reject("alternativeIconPath", [](State& s) { s.talents[0].alternativeIconPath = "../a.dds"; });
	Reject("capacity", [](State& s)
	{
		s.talents.clear();
		for (int i = 0; i < 11; ++i)
		{
			auto t = Fixture().talents[0];
			t.column = i % 6;
			t.row = i / 6;
			s.talents.push_back(t);
		}
	});
}

/** No VM reset/removal API is smuggled into runtime update requests. */
void UnsupportedChanges()
{
	const auto original = Fixture();
	for (int mode = 0; mode < 4; ++mode)
	{
		auto changed = original;
		if (mode == 0) changed.talents.pop_back();
		if (mode == 1) changed.talents[0].iconPath = "UI/different.dds";
		if (mode == 2) changed.talents[0].purchase = Purchase::CanBuy;
		if (mode == 3) changed.talents[0].column = 1;
		bool rejected = false;
		try { static_cast<void>(PwRuffleActionUpdateCalls(changed, original)); }
		catch (const std::invalid_argument&) { rejected = true; }
		Check(rejected, "Accepted unsupported loadout/respec change");
	}
}

void BoundariesAndOmissions()
{
	Check(PwRuffleActionInitCalls({}) == Json::array(), "Empty state invented icons");
	Check(PwRuffleActionUpdateCalls({}, {}) == Json::array(), "Empty diff emitted calls");
	State state = Fixture();
	state.talents.resize(1);
	state.talents[0].active = false;
	auto calls = PwRuffleActionInitCalls(state);
	Check(calls.size() == 4, "Passive bought talent got a bootstrap transition");
	AuthoredBarMock mock;
	mock.Apply(calls);
	Check(mock.shortcuts == 0, "Passive talent gained an action shortcut");
	state.talents[0].active = true;
	state.talents[0].desiredIndex = -2;
	mock.Apply(PwRuffleActionInitCalls(state));
	Check(mock.shortcuts == 0, "Excluded active talent gained a shortcut");
	state.talents[0].desiredIndex = 9;
	state.talents[0].column = state.talents[0].row = 5;
	state.talents[0].cost = std::numeric_limits<std::int32_t>::max();
	state.talents[0].iconPath = ":/" + std::string(State::MaxResourceBytes - 6, 'x') + ".dds";
	state.talents[0].cooldown = std::numeric_limits<float>::max();
	state.talents[0].maxCooldown = 0;
	calls = PwRuffleActionInitCalls(state);
	Check(calls[3]["args"][3] == std::numeric_limits<float>::max(), "Cooldown was clamped");
	Check(calls[3]["args"][4] == 0, "Zero max cooldown changed");
	for (int purchase = 0; purchase < 4; ++purchase)
		for (int status = 0; status < 7; ++status)
		{
			state.talents[0].purchase = static_cast<Purchase>(purchase);
			state.talents[0].status = static_cast<Slot>(status);
			Check(!PwRuffleActionInitCalls(state).empty(), "Valid enum rejected");
		}
	state.talents.clear();
	for (int i = 0; i < 36; ++i)
	{
		auto t = Fixture().talents[0];
		t.column = i % 6;
		t.row = i / 6;
		t.active = false;
		t.desiredIndex = -2;
		state.talents.push_back(t);
	}
	Check(PwRuffleActionInitCalls(state).size() == 109, "Full 6x6 grid rejected");
}
}

/** Standalone C++17 probe: link only action_calls.cpp and nlohmann/json headers. */
int main()
{
	try
	{
		ContractsAndBuying();
		PurchaseTransitions();
		InvalidInputs();
		UnsupportedChanges();
		BoundariesAndOmissions();
		std::cout << "Action calls probe: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Action calls probe: " << e.what() << '\n';
		return 1;
	}
}
