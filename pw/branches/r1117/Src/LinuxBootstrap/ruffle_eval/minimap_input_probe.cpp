#include "minimap_input.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
using Event = PwRuffleGameplayEvent;
using Kind = Event::Kind;
using Bounds = PwRuffleMinimapWorldBounds;
using Input = PwRuffleMinimapInput;
using Request = PwRuffleMinimapRequest;
using Action = Request::Action;
const Bounds World{10, 20, 110, 220};
std::size_t checks = 0;

/** Keep failures diagnostic and checks active in optimized/NDEBUG builds. */
void Check(bool condition, const std::string& message)
{
	++checks;
	if (!condition) throw std::runtime_error(message);
}

/** Compare double projection arithmetic, not the decoder's float quantization. */
bool Near(double actual, double expected)
{
	return std::isfinite(actual) && std::abs(actual - expected) <=
		1e-10 * (1 + std::abs(expected));
}

/** Mock the decoder's value records; command strings cannot authorize actions. */
Event Mouse(Kind kind, bool flag = false, float x = 0.5f, float y = 0.5f)
{
	Event event;
	event.kind = kind;
	event.flag = flag;
	event.x = x;
	event.y = y;
	return event;
}

/** Rejected/no-op results must not expose a stale destination. */
void None(const Request& request, const char* message)
{
	Check(request.action == Action::NoAction && request.worldX == 0 && request.worldY == 0, message);
}

/** Pin non-square, offset worlds, north-up orientation and explicit inverted artwork. */
void Projection()
{
	double x = -17, y = -31;
	Check(PwRuffleProjectMinimapInput(0.5, 0.5, World, x, y) && x == 60 && y == 120,
		"World center/offset");
	Check(PwRuffleProjectMinimapInput(0.25, 0.75, World, x, y) && x == 35 && y == 70,
		"Rectangular world/north-up Y");
	Check(PwRuffleProjectMinimapInput(0.25, 0.75, World, x, y, false) && x == 35 && y == 170,
		"Explicit non-inverted Y");
	const std::array<std::array<double, 2>, 4> rim{{
		{3.0 / 270, 0.5}, {267.0 / 270, 0.5}, {0.5, 3.0 / 270}, {0.5, 267.0 / 270}
	}};
	for (const auto& point : rim)
	{
		Check(PwRuffleProjectMinimapInput(point[0], point[1], World, x, y), "Closed authored rim rejected");
		Check(Near(x, 10 + point[0] * 100) && Near(y, 220 - point[1] * 200),
			"Circle was stretched to the world rectangle");
	}
	const std::array<std::array<double, 2>, 13> outside{{
		{0, 0}, {1, 0}, {0, 1}, {1, 1}, {0, 0.5}, {1, 0.5}, {0.5, 0}, {0.5, 1},
		{2.999 / 270, 0.5}, {267.001 / 270, 0.5}, {0.5, 2.999 / 270},
		{0.5, 267.001 / 270}, {0.9, 0.9}
	}};
	for (const auto& point : outside)
	{
		x = -17; y = -31;
		Check(!PwRuffleProjectMinimapInput(point[0], point[1], World, x, y), "Outside circle accepted");
		Check(x == -17 && y == -31, "Failed projection changed output");
	}
}

/** Right presses issue once; left presses/changed moves own camera gestures. */
void Gestures()
{
	Input input;
	None(input.Consume(Mouse(Kind::MinimapActionMove), World), "Orphan move");
	None(input.Consume(Mouse(Kind::MinimapMouseUp, false), World), "Orphan release");
	None(input.Consume(Mouse(Kind::MinimapMouseOver, true), World), "Hover issued command");
	auto request = input.Consume(Mouse(Kind::MinimapMouseDown, false), World);
	Check(request.action == Action::Move && request.worldX == 60 && request.worldY == 120,
		"Right down did not issue one move");
	None(input.Consume(Mouse(Kind::MinimapMouseDown, false, 0.25f), World), "Duplicate right down");
	None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Right drag moved");
	None(input.Consume(Mouse(Kind::MinimapMouseUp, false), World), "Right release moved");
	None(input.Consume(Mouse(Kind::MinimapMouseUp, false), World), "Duplicate release moved");
	Check(input.Consume(Mouse(Kind::MinimapMouseDown, false), World).action == Action::Move,
		"Next right press suppressed");
	input.Reset();
	request = input.Consume(Mouse(Kind::MinimapMouseDown, true, 0.25f, 0.75f), World);
	Check(request.action == Action::Camera && request.worldX == 35 && request.worldY == 70,
		"Left down without hover did not move camera");
	None(input.Consume(Mouse(Kind::MinimapMouseDown, true, 0.75f, 0.25f), World), "Duplicate left down");
	None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f, 0.75f), World), "Unchanged drag point");
	request = input.Consume(Mouse(Kind::MinimapActionMove, false, 0.75f, 0.25f), World);
	Check(request.action == Action::Camera && request.worldX == 85 && request.worldY == 170,
		"Left drag/Move flag ignored");
	None(input.Consume(Mouse(Kind::MinimapActionMove, true, 0.75f, 0.25f), World), "Duplicate Move");
	None(input.Consume(Mouse(Kind::MinimapMouseOver, true), World), "Duplicate hover");
	Check(input.Consume(Mouse(Kind::MinimapActionMove), World).action == Action::Camera,
		"Hover stole gesture ownership");
	request = input.Consume(Mouse(Kind::MinimapActionMove, false, 0.5f, 0.25f), World);
	Check(request.action == Action::Camera && request.worldX == 60 && request.worldY == 170,
		"Vertical-only drag suppressed");
	Check(input.Consume(Mouse(Kind::MinimapActionMove), World).action == Action::Camera,
		"Returning to a previous point suppressed");
	None(input.Consume(Mouse(Kind::MinimapMouseUp, true), World), "Left release issued command");
	None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Stale drag after release");
}

/** Any release cancels, matching AdventureScreen; competing downs never switch owner. */
void OwnershipAndCancellation()
{
	for (bool left : {false, true})
		for (bool releasedLeft : {false, true})
		{
			Input input;
			input.Consume(Mouse(Kind::MinimapMouseDown, left), World);
			None(input.Consume(Mouse(Kind::MinimapMouseUp, releasedLeft), World), "Mismatched release issued");
			None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Release retained owner");
			Check(input.Consume(Mouse(Kind::MinimapMouseDown, !left), World).action ==
				(left ? Action::Move : Action::Camera), "Release did not free owner");
		}
	for (bool left : {false, true})
	{
		Input input;
		input.Consume(Mouse(Kind::MinimapMouseDown, left), World);
		None(input.Consume(Mouse(Kind::MinimapMouseDown, !left), World), "Competing down issued");
		None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Competing down retained drag");
	}
	for (const auto& cancellation : {
		Mouse(Kind::MinimapMouseOver, false),
		Mouse(Kind::MinimapMouseOver, false, -2, 3),
		Mouse(Kind::MinimapMouseUp, true, -2, 3),
		Mouse(Kind::MinimapActionMove, false, 0, 0),
		Mouse(Kind::MinimapMouseOver, true, 0, 0),
		Mouse(Kind::MinimapMouseDown, true, 0, 0),
		Mouse(static_cast<Kind>(-1))})
	{
		Input input;
		input.Consume(Mouse(Kind::MinimapMouseDown, true), World);
		None(input.Consume(cancellation, World), "Cancellation issued");
		None(input.Consume(Mouse(Kind::MinimapMouseOver, true), World), "Hover after cancellation issued");
		None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Cancelled drag resumed");
		Check(input.Consume(Mouse(Kind::MinimapMouseDown, true), World).action == Action::Camera,
			"New press after cancellation rejected");
	}
	Input first, second;
	first.Consume(Mouse(Kind::MinimapMouseDown, true), World);
	None(second.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Instances share ownership");
	second.Reset();
	Check(first.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World).action == Action::Camera,
		"Other instance reset owner");
	for (const auto kind : {Kind::Unsupported, Kind::Informational, Kind::TalentClicked})
	{
		auto unrelated = Mouse(kind, false, std::numeric_limits<float>::quiet_NaN());
		unrelated.command = "MinimapMouseDown";
		None(first.Consume(unrelated, World), "Unrelated event dispatched via command text");
	}
	Check(first.Consume(Mouse(Kind::MinimapActionMove), World).action == Action::Camera,
		"Unrelated callback stole ownership");
	for (const char* command : {"SignalMouseClick", "CameraMouseClick", "automatic-target"})
	{
		auto unsupported = Mouse(Kind::Unsupported);
		unsupported.command = command;
		None(first.Consume(unsupported, World), "Unsupported command became an action");
	}
}

/** Reset is the explicit blur/session/queue-loss hook, including held right presses. */
void ResetAndProjectionChanges()
{
	for (bool left : {false, true})
	{
		Input input;
		const auto down = Mouse(Kind::MinimapMouseDown, left);
		const auto action = left ? Action::Camera : Action::Move;
		Check(input.Consume(down, World).action == action, "Initial press");
		input.Reset();
		input.Reset();
		None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Blur retained drag");
		None(input.Consume(Mouse(Kind::MinimapMouseUp, left), World), "Stale release after Reset");
		None(input.Consume(Mouse(Kind::MinimapMouseOver, true), World), "Hover after Reset");
		Check(input.Consume(down, World).action == action, "Reset retained duplicate suppression");
	}
	for (const auto field : {&Bounds::minX, &Bounds::minY, &Bounds::maxX, &Bounds::maxY})
	{
		Input input;
		input.Consume(Mouse(Kind::MinimapMouseDown, true), World);
		auto changed = World;
		changed.*field += 1;
		None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), changed), "Bounds change retained drag");
		None(input.Consume(Mouse(Kind::MinimapActionMove), World), "Old projection resumed drag");
	}
	Input input;
	input.Consume(Mouse(Kind::MinimapMouseDown, true), World);
	None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World, false), "Y flip retained drag");
	const auto request = input.Consume(Mouse(Kind::MinimapMouseDown, true, 0.25f, 0.75f), World, false);
	Check(request.action == Action::Camera && request.worldY == 170, "New projection not adopted");
}

/** Invalid finite/non-finite fields must reject atomically and cancel held gestures. */
void InvalidValues()
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	const double max = std::numeric_limits<double>::max();
	for (const double bad : {nan, inf, -inf, -max, max, -0.001, 1.001})
		for (bool badX : {false, true})
		{
			double x = -17, y = -31;
			Check(!PwRuffleProjectMinimapInput(badX ? bad : 0.5, badX ? 0.5 : bad, World, x, y),
				"Invalid normalized coordinate accepted");
			Check(x == -17 && y == -31, "Invalid coordinate modified result");
		}
	for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -0.001f, 1.001f})
		for (const auto kind : {Kind::MinimapMouseOver, Kind::MinimapMouseDown,
			Kind::MinimapMouseUp, Kind::MinimapActionMove})
			for (bool flag : {false, true})
				for (bool badX : {false, true})
				{
					Input input;
					input.Consume(Mouse(Kind::MinimapMouseDown, true), World);
					None(input.Consume(Mouse(kind, flag, badX ? bad : 0.5f, badX ? 0.5f : bad), World),
						"Malformed event issued");
					None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World),
						"Malformed event retained drag");
				}
	const auto rejectBounds = [](const Bounds& bounds)
	{
		double x = -17, y = -31;
		Check(!PwRuffleProjectMinimapInput(0.5, 0.5, bounds, x, y), "Invalid dimensions accepted");
		Check(x == -17 && y == -31, "Invalid dimensions modified output");
		Input input;
		input.Consume(Mouse(Kind::MinimapMouseDown, true), World);
		None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), bounds), "Invalid bounds issued");
		None(input.Consume(Mouse(Kind::MinimapActionMove, false, 0.25f), World), "Invalid bounds retained drag");
		None(input.Consume(Mouse(Kind::MinimapMouseDown, false), bounds), "Invalid bounds acquired owner");
		Check(input.Consume(Mouse(Kind::MinimapMouseDown, false), World).action == Action::Move,
			"Invalid bounds blocked next valid press");
	};
	rejectBounds(Bounds{});
	for (const auto field : {&Bounds::minX, &Bounds::minY, &Bounds::maxX, &Bounds::maxY})
		for (const double bad : {nan, inf, -inf})
		{
			auto bounds = World;
			bounds.*field = bad;
			rejectBounds(bounds);
		}
	for (const auto bounds : {Bounds{0, 0, 0, 1}, Bounds{0, 0, 1, 0}, Bounds{0, 0, -1, 1},
		Bounds{0, 0, 1, -1}, Bounds{-max, 0, max, 1}, Bounds{0, -max, 1, max}})
		rejectBounds(bounds);
	for (const double extent : {std::numeric_limits<double>::min(), max})
	{
		double x = 0, y = 0;
		Check(PwRuffleProjectMinimapInput(0.5, 0.5, {0, 0, extent, extent}, x, y) &&
			std::isfinite(x) && std::isfinite(y), "Finite positive dimensions rejected/overflowed");
	}
}

/** Exhaust the authored pixel mask using actual float callback coordinates. */
void ExhaustiveMask()
{
	for (int py = 0; py < 270; ++py)
		for (int px = 0; px < 270; ++px)
		{
			const int dx = px - 135, dy = py - 135;
			const bool inside = dx * dx + dy * dy <= 132 * 132;
			const auto event = Mouse(Kind::MinimapMouseDown, false,
				static_cast<float>(px) / 270, static_cast<float>(py) / 270);
			Input input;
			const auto request = input.Consume(event, World);
			Check((request.action == Action::Move) == inside, "Authored circle mask mismatch");
			if (!inside) continue;
			Check(Near(request.worldX, 10 + static_cast<double>(event.x) * 100) &&
				Near(request.worldY, 220 - static_cast<double>(event.y) * 200), "Callback coordinate scaling");
		}
}

/** Invert every marker-center grid point, with frame.cpp's 0..269 nearest-pixel rule. */
void ExhaustiveInverseProjection()
{
	const std::array<Bounds, 4> worlds{{World, {-320, -40, 880, 60}, {0, 0, 7, 900}, {-10, -20, -2, -1}}};
	for (const auto& bounds : worlds)
		for (bool invertY : {false, true})
			for (int py = 0; py < 270; ++py)
				for (int px = 0; px < 270; ++px)
				{
					const double u = static_cast<double>(px) / 269;
					const double v = static_cast<double>(py) / 269;
					const double dx = u * 270 - 135, dy = v * 270 - 135;
					double x = -17, y = -31;
					const bool accepted = PwRuffleProjectMinimapInput(u, v, bounds, x, y, invertY);
					Check(accepted == (dx * dx + dy * dy <= 132 * 132), "Inverse grid mask mismatch");
					if (!accepted) continue;
					const double forwardX = (x - bounds.minX) / (bounds.maxX - bounds.minX);
					const double forwardY = (invertY ? bounds.maxY - y : y - bounds.minY) /
						(bounds.maxY - bounds.minY);
					Check(Near(forwardX, u) && Near(forwardY, v), "Continuous projection is not inverse");
					Check(std::lround(forwardX * 269) == px && std::lround(forwardY * 269) == py,
						"Frame nearest-pixel projection is not inverse");
				}
}
}

/** Standalone engine/JSON/renderer-free probe; failure names the violated contract. */
int main()
{
	try
	{
		Projection();
		Gestures();
		OwnershipAndCancellation();
		ResetAndProjectionChanges();
		InvalidValues();
		ExhaustiveMask();
		ExhaustiveInverseProjection();
		std::cout << "Minimap input: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Minimap input failed after " << checks << " checks: " << error.what() << '\n';
		return 1;
	}
}
