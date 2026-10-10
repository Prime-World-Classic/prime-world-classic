#include "minimap_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using View = PwRuffleMinimapBackgroundView;
using Bounds = PwRuffleMinimapWorldBounds;
using Marker = PwRuffleMinimapMarker;
using Frame = PwRuffleMinimapFrame;
using Team = Marker::Team;
using Kind = Marker::Kind;
using Pixel = std::array<std::uint8_t, 4>;
const Pixel Background{17, 33, 65, 127};
const Pixel Ally{86, 220, 92, 255};
const Pixel Enemy{232, 74, 66, 255};
const Pixel Neutral{232, 192, 72, 255};
const Pixel Self{255, 255, 255, 255};
const Bounds World{10, 20, 110, 220};
int checks = 0;

/** Keep checks active in NDEBUG builds. */
void Check(bool condition, const std::string& message)
{
	++checks;
	if (!condition) throw std::runtime_error(message);
}

Pixel At(const Frame& frame, int x, int y)
{
	const auto offset = static_cast<std::size_t>(y) * frame.strideBytes + static_cast<std::size_t>(x) * 4;
	return {frame.rgba.at(offset), frame.rgba.at(offset + 1), frame.rgba.at(offset + 2), frame.rgba.at(offset + 3)};
}

View OnePixel(const Pixel& pixel = Background)
{
	return {pixel.data(), pixel.size(), 1, 1, 4};
}

Marker Unit(double x, double y, Kind kind = Kind::Hero, Team team = Team::Ally)
{
	return {x, y, team, kind, false, false, true};
}

std::size_t Painted(const Frame& frame)
{
	std::size_t painted = 0;
	for (int y = 0; y < 270; ++y)
		for (int x = 0; x < 270; ++x)
			if (At(frame, x, y) != Background) ++painted;
	return painted;
}

/** Require explicit rejection, not a blank/fallback minimap or clipped input value. */
void Reject(const std::string& field, const std::function<void()>& operation)
{
	bool rejected = false;
	try { operation(); }
	catch (const std::invalid_argument& error)
	{
		rejected = true;
		Check(std::string(error.what()).find(field) != std::string::npos,
			"Wrong diagnostic: " + std::string(error.what()));
	}
	Check(rejected, "Accepted invalid " + field);
}

/** Pin top-down axes, unmodified straight alpha, padding, nearest sampling and ownership. */
void BackgroundPixels()
{
	const std::vector<std::uint8_t> pixels{
		255, 1, 2, 0, 3, 254, 4, 64, 99, 99, 99, 99,
		5, 6, 253, 128, 252, 251, 7, 255
	};
	const View view{pixels.data(), pixels.size(), 2, 2, 12};
	const auto before = pixels;
	const auto frame = PwRuffleBuildMinimapFrame(view, World, {});
	Check(frame.width == 270 && frame.height == 270 && frame.strideBytes == 1080 &&
		frame.rgba.size() == 291600, "Output layout");
	Check(frame.rgba.data() != view.pixels && pixels == before, "Background mutated or aliased");
	for (int y = 0; y < 270; ++y)
		for (int x = 0; x < 270; ++x)
		{
			const int offset = (y < 135 ? 0 : 12) + (x < 135 ? 0 : 4);
			Check(At(frame, x, y) == Pixel{pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]},
				"RGBA/padding/orientation changed");
		}
	Pixel source{213, 121, 37, 0};
	const auto owned = PwRuffleBuildMinimapFrame(OnePixel(source), World, {});
	source.fill(0);
	Check(At(owned, 269, 269) == Pixel{213, 121, 37, 0}, "Owned result lost straight-alpha transparent RGB");
	std::vector<std::uint8_t> downsample(540 * 540 * 4);
	for (int y = 0; y < 540; ++y)
		for (int x = 0; x < 540; ++x)
		{
			const auto offset = (y * 540 + x) * 4;
			downsample[offset] = static_cast<std::uint8_t>(x);
			downsample[offset + 1] = static_cast<std::uint8_t>(y);
			downsample[offset + 2] = 53;
			downsample[offset + 3] = 177;
		}
	const auto half = PwRuffleBuildMinimapFrame({downsample.data(), downsample.size(), 540, 540, 2160}, World, {});
	for (int y = 0; y < 270; ++y)
		for (int x = 0; x < 270; ++x)
			Check(At(half, x, y) == Pixel{static_cast<std::uint8_t>(x * 2 + 1),
				static_cast<std::uint8_t>(y * 2 + 1), 53, 177}, "Nearest center sample mismatch");
}

/** World Y is inverted exactly once; primitives at all four corners clip symmetrically. */
void AxesAndClipping()
{
	const auto view = OnePixel();
	const std::array<Marker, 4> corners{Unit(10, 220), Unit(110, 220), Unit(10, 20), Unit(110, 20)};
	const std::array<std::array<int, 2>, 4> points{{{0, 0}, {269, 0}, {0, 269}, {269, 269}}};
	for (std::size_t i = 0; i < corners.size(); ++i)
	{
		const auto frame = PwRuffleBuildMinimapFrame(view, World, {corners[i]});
		Check(At(frame, points[i][0], points[i][1]) == Ally, "Corner world orientation");
		Check(Painted(frame) == 11, "Radius-3 corner circle clipping");
	}
	for (const auto kind : {Kind::Hero, Kind::Creep, Kind::Objective})
	{
		const auto center = PwRuffleBuildMinimapFrame(view, World, {Unit(60, 120, kind, Team::Enemy)});
		Check(At(center, 135, 135) == Enemy, "World center rounding");
		const std::size_t area = kind == Kind::Hero ? 29 : kind == Kind::Creep ? 9 : 49;
		Check(Painted(center) == area, "Primitive size");
		const auto corner = PwRuffleBuildMinimapFrame(view, World, {Unit(10, 220, kind)});
		Check(Painted(corner) == (kind == Kind::Hero ? 11u : kind == Kind::Creep ? 4u : 16u), "Clipped primitive size");
	}
	auto self = Unit(60, 120);
	self.self = true;
	const auto frame = PwRuffleBuildMinimapFrame(view, World, {self});
	Check(Painted(frame) == 49 && At(frame, 135, 135) == Self, "Self circle radius/color");
	for (const auto marker : {Unit(9.999, 120), Unit(110.001, 120), Unit(60, 19.999), Unit(60, 220.001),
		Unit(std::numeric_limits<double>::max(), -std::numeric_limits<double>::max())})
		Check(Painted(PwRuffleBuildMinimapFrame(view, World, {marker})) == 0, "Outside center falsely clamped to an edge");
}

/** Caller visibility/death filtering and deterministic self-last layering. */
void VisibilityAndLayering()
{
	const auto view = OnePixel();
	for (int hidden = 0; hidden < 3; ++hidden)
	{
		auto marker = Unit(60, 120);
		if (hidden == 0) marker.visible = false;
		if (hidden == 1) marker.dead = true;
		if (hidden == 2) marker.kind = Kind::Unsupported;
		Check(Painted(PwRuffleBuildMinimapFrame(view, World, {marker})) == 0, "Hidden/dead/unsupported marker drawn");
	}
	auto self = Unit(60, 120);
	self.self = true;
	std::vector<Marker> markers{self, Unit(60, 120, Kind::Hero, Team::Enemy),
		Unit(60, 120, Kind::Objective, Team::Neutral), Unit(60, 120, Kind::Creep)};
	const auto layered = PwRuffleBuildMinimapFrame(view, World, markers);
	std::reverse(markers.begin(), markers.end());
	Check(PwRuffleBuildMinimapFrame(view, World, markers).rgba == layered.rgba, "Cross-kind priority changed with input order");
	Check(At(layered, 135, 135) == Self, "Self hidden under another kind");
	Check(At(layered, 138, 138) == Neutral, "Objective square shape overwritten outside circles");
	const auto tie = PwRuffleBuildMinimapFrame(view, World,
		{Unit(60, 120), Unit(60, 120, Kind::Hero, Team::Enemy)});
	Check(At(tie, 135, 135) == Enemy, "Same-kind last paint did not win");
	Check(At(tie, 0, 0) == Background && At(tie, 135, 135)[3] == 255, "Opaque marker/background alpha corrupted");
}

/** Exercise all size arithmetic before the implementation can dereference invalid views. */
void InvalidInputs()
{
	const auto view = OnePixel();
	for (int failure = 0; failure < 9; ++failure)
	{
		auto bad = view;
		if (failure == 0) bad.pixels = nullptr;
		if (failure == 1) bad.width = 0;
		if (failure == 2) bad.height = 0;
		if (failure == 3) bad.width = 4097;
		if (failure == 4) bad.height = std::numeric_limits<std::uint32_t>::max();
		if (failure == 5) bad.strideBytes = 3;
		if (failure == 6) bad.byteCount = 3;
		if (failure == 7) bad.strideBytes = std::numeric_limits<std::size_t>::max();
		if (failure == 8) bad.byteCount = std::numeric_limits<std::size_t>::max();
		Reject("background", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(bad, World, {})); });
	}
	auto padded = view;
	padded.height = 2;
	padded.strideBytes = View::MaxBytes;
	padded.byteCount = View::MaxBytes;
	Reject("background", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(padded, World, {})); });
	for (int failure = 0; failure < 5; ++failure)
	{
		auto bounds = World;
		if (failure == 0) bounds.maxX = bounds.minX;
		if (failure == 1) bounds.maxY = bounds.minY;
		if (failure == 2) bounds.maxX = bounds.minX - 1;
		if (failure == 3) bounds.maxY = bounds.minY - 1;
		if (failure == 4) { bounds.minX = -std::numeric_limits<double>::max(); bounds.maxX = std::numeric_limits<double>::max(); }
		Reject("bounds", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, bounds, {})); });
	}
	for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
	{
		for (const auto field : {&Bounds::minX, &Bounds::minY, &Bounds::maxX, &Bounds::maxY})
		{
			auto bounds = World;
			bounds.*field = bad;
			Reject("bounds", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, bounds, {})); });
		}
		for (const auto field : {&Marker::worldX, &Marker::worldY})
		{
			auto marker = Unit(60, 120);
			marker.*field = bad;
			marker.visible = false;
			Reject("coordinates", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, World, {marker})); });
		}
	}
	auto marker = Unit(60, 120);
	marker.team = static_cast<Team>(3);
	Reject("team", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, World, {marker})); });
	marker = Unit(60, 120);
	marker.kind = static_cast<Kind>(4);
	Reject("kind", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, World, {marker})); });
	marker = Unit(60, 120, Kind::Creep);
	marker.self = true;
	Reject("self", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, World, {marker})); });
	std::vector<Marker> tooMany(Marker::MaxCount + 1, Unit(60, 120));
	Reject("markers", [&] { static_cast<void>(PwRuffleBuildMinimapFrame(view, World, tooMany)); });
}

/** Exact resource boundaries are supported without allocating a source-sized copy. */
void ResourceBoundaries()
{
	std::vector<Marker> markers(Marker::MaxCount, Unit(60, 120));
	Check(Painted(PwRuffleBuildMinimapFrame(OnePixel(), World, markers)) == 29, "640 markers rejected");
	std::vector<std::uint8_t> source(View::MaxBytes, 73);
	const View view{source.data(), source.size(), 4096, 4096, 4096 * 4};
	const auto frame = PwRuffleBuildMinimapFrame(view, World, {});
	Check(frame.rgba.size() == Frame::PixelBytes && frame.rgba.size() < source.size(), "Output budget depends on source size");
	Check(At(frame, 0, 0) == Pixel{73, 73, 73, 73} && At(frame, 269, 269) == Pixel{73, 73, 73, 73},
		"Maximum background boundary rejected or sampled incorrectly");
	const Bounds tiny{0, 0, std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::denorm_min()};
	Check(Painted(PwRuffleBuildMinimapFrame(OnePixel(), tiny, {Unit(0, 0)})) == 11, "Small finite extent overflowed");
}

/** Reproduce rectangular bitmap leakage when the shipped SWF has no authored mask. */
void AuthoredCircleClip()
{
	auto frame = PwRuffleBuildMinimapFrame(OnePixel(), World, {});
	Check(At(frame, 0, 0) == Background, "Builder must remain unclipped by default");
	for (int y = 0; y < 270; ++y)
		for (int x = 0; x < 270; ++x)
		{
			const auto offset = static_cast<std::size_t>(y) * frame.strideBytes + static_cast<std::size_t>(x) * 4;
			frame.rgba[offset] = static_cast<std::uint8_t>(x % 251 + 1);
			frame.rgba[offset + 1] = static_cast<std::uint8_t>(y % 251 + 1);
			frame.rgba[offset + 2] = static_cast<std::uint8_t>((x * 3 + y * 7) % 256);
			frame.rgba[offset + 3] = static_cast<std::uint8_t>((x + y) % 256);
		}
	const auto original = frame;
	const auto* storage = frame.rgba.data();
	const auto capacity = frame.rgba.capacity();
	PwRuffleClipMinimapFrame(frame);
	Check(At(frame, 0, 0) == Pixel{0, 0, 0, 0}, "Unmasked minimap leaks outside authored circle");
	Check(frame.width == 270 && frame.height == 270 && frame.strideBytes == 1080 &&
		frame.rgba.size() == Frame::PixelBytes && frame.rgba.data() == storage && frame.rgba.capacity() == capacity,
		"Clip changed metadata/storage");
	for (int y = 0; y < 270; ++y)
		for (int x = 0; x < 270; ++x)
		{
			const bool inside = std::hypot(x - 135.0, y - 135.0) <= 132.0;
			Check(At(frame, x, y) == (inside ? At(original, x, y) : Pixel{0, 0, 0, 0}),
				"Circle geometry or interior RGBA changed");
		}
	for (const auto point : {std::array<int, 2>{3, 135}, {267, 135}, {135, 3}, {135, 267},
		{135, 135}, {128, 128}, {128, 127}})
		Check(At(frame, point[0], point[1]) == At(original, point[0], point[1]),
			"Circle boundary/center/transparent interior not preserved");
	for (const auto point : {std::array<int, 2>{2, 135}, {268, 135}, {135, 2}, {135, 268},
		{0, 0}, {269, 0}, {0, 269}, {269, 269}})
		Check(At(frame, point[0], point[1]) == Pixel{0, 0, 0, 0}, "Outside circle not transparent black");
	const auto clipped = frame.rgba;
	PwRuffleClipMinimapFrame(frame);
	Check(frame.rgba == clipped && frame.rgba.data() == storage, "Repeated clipping changed pixels/storage");
	for (int failure = 0; failure < 13; ++failure)
	{
		auto invalid = original;
		if (failure == 0) invalid.width = 0;
		if (failure == 1) invalid.width = 269;
		if (failure == 2) invalid.width = 271;
		if (failure == 3) invalid.height = 0;
		if (failure == 4) invalid.height = 269;
		if (failure == 5) invalid.height = 271;
		if (failure == 6) invalid.strideBytes = 0;
		if (failure == 7) invalid.strideBytes = 1084;
		if (failure == 8) invalid.rgba.pop_back();
		if (failure == 9) invalid.rgba.push_back(0);
		if (failure == 10) invalid.rgba.clear();
		if (failure == 11) invalid.width = std::numeric_limits<std::uint32_t>::max();
		if (failure == 12) invalid.strideBytes = std::numeric_limits<std::uint32_t>::max();
		const auto before = invalid;
		Reject("frame", [&] { PwRuffleClipMinimapFrame(invalid); });
		Check(invalid.width == before.width && invalid.height == before.height &&
			invalid.strideBytes == before.strideBytes && invalid.rgba == before.rgba,
			"Clip partially changed an invalid frame");
	}
}
}

/** Standalone headless test; link only minimap_frame.cpp and the C++17 standard library. */
int main()
{
	try
	{
		BackgroundPixels();
		AxesAndClipping();
		VisibilityAndLayering();
		InvalidInputs();
		ResourceBoundaries();
		AuthoredCircleClip();
		std::cout << "Minimap frame probe: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Minimap frame probe: " << error.what() << '\n';
		return 1;
	}
}
