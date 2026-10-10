#include "minimap_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace
{
using View = PwRuffleMinimapBackgroundView;
using Bounds = PwRuffleMinimapWorldBounds;
using Marker = PwRuffleMinimapMarker;
using Frame = PwRuffleMinimapFrame;
using Pixel = std::array<std::uint8_t, 4>;

void Require(bool condition, const char* field, const char* rule)
{
	if (!condition)
		throw std::invalid_argument(std::string("Minimap ") + field + ": " + rule);
}

/** Validate the full borrowed footprint before multiplication or pixel reads. */
void Validate(const View& background, const Bounds& bounds, const std::vector<Marker>& markers)
{
	Require(background.pixels && background.width && background.height &&
		background.width <= View::MaxDimension && background.height <= View::MaxDimension,
		"background", "missing pixels or dimensions outside 1..4096");
	const std::size_t rowBytes = static_cast<std::size_t>(background.width) * 4;
	Require(background.strideBytes >= rowBytes && background.strideBytes <= View::MaxBytes &&
		background.byteCount <= View::MaxBytes, "background", "invalid stride or resource budget");
	Require(background.height - 1 <= (View::MaxBytes - rowBytes) / background.strideBytes,
		"background", "strided footprint exceeds resource budget");
	const auto required = (background.height - 1) * background.strideBytes + rowBytes;
	Require(background.byteCount >= required, "background", "pixel buffer is shorter than its footprint");
	Require(std::isfinite(bounds.minX) && std::isfinite(bounds.minY) &&
		std::isfinite(bounds.maxX) && std::isfinite(bounds.maxY), "bounds", "non-finite coordinate");
	const double width = bounds.maxX - bounds.minX;
	const double height = bounds.maxY - bounds.minY;
	Require(std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0,
		"bounds", "extents must be finite and positive");
	Require(markers.size() <= Marker::MaxCount, "markers", "input exceeds 640 markers");
	for (const auto& marker : markers)
	{
		Require(std::isfinite(marker.worldX) && std::isfinite(marker.worldY), "coordinates", "non-finite marker");
		const auto team = static_cast<std::int32_t>(marker.team);
		const auto kind = static_cast<std::int32_t>(marker.kind);
		Require(team >= 0 && team <= 2, "team", "expected Neutral, Ally or Enemy");
		Require(kind >= 0 && kind <= 3, "kind", "expected Unsupported, Hero, Creep or Objective");
		Require(!marker.self || marker.kind == Marker::Kind::Hero, "self", "only a hero may be self");
	}
}

int Layer(const Marker& marker)
{
	if (marker.self) return 3;
	if (marker.kind == Marker::Kind::Hero) return 2;
	if (marker.kind == Marker::Kind::Objective) return 1;
	return 0;
}

Pixel Color(const Marker& marker)
{
	if (marker.self) return {255, 255, 255, 255};
	if (marker.team == Marker::Team::Ally) return {86, 220, 92, 255};
	if (marker.team == Marker::Team::Enemy) return {232, 74, 66, 255};
	return {232, 192, 72, 255};
}

/** Small integer circle/square raster; clip edges without moving the true center. */
void Paint(Frame& frame, const Marker& marker, const Bounds& bounds)
{
	const int x = static_cast<int>(std::lround((marker.worldX - bounds.minX) / (bounds.maxX - bounds.minX) * (Frame::Side - 1)));
	const int y = static_cast<int>(std::lround((bounds.maxY - marker.worldY) / (bounds.maxY - bounds.minY) * (Frame::Side - 1)));
	const bool circle = marker.kind == Marker::Kind::Hero;
	const int radius = marker.self ? 4 : marker.kind == Marker::Kind::Creep ? 1 : 3;
	const auto color = Color(marker);
	for (int dy = -radius; dy <= radius; ++dy)
		for (int dx = -radius; dx <= radius; ++dx)
		{
			if (circle && dx * dx + dy * dy > radius * radius) continue;
			const int px = x + dx;
			const int py = y + dy;
			if (px < 0 || py < 0 || px >= static_cast<int>(Frame::Side) || py >= static_cast<int>(Frame::Side)) continue;
			const auto offset = static_cast<std::size_t>(py) * frame.strideBytes + static_cast<std::size_t>(px) * 4;
			std::copy(color.begin(), color.end(), frame.rgba.begin() + offset);
		}
}
}

PwRuffleMinimapFrame PwRuffleBuildMinimapFrame(const PwRuffleMinimapBackgroundView& background,
	const PwRuffleMinimapWorldBounds& bounds, const std::vector<PwRuffleMinimapMarker>& markers)
{
	Validate(background, bounds, markers);
	Frame frame;
	frame.width = frame.height = Frame::Side;
	frame.strideBytes = Frame::Side * 4;
	frame.rgba.resize(Frame::PixelBytes);
	for (std::size_t y = 0; y < Frame::Side; ++y)
	{
		const auto sourceY = (2 * y + 1) * background.height / (2 * Frame::Side);
		for (std::size_t x = 0; x < Frame::Side; ++x)
		{
			const auto sourceX = (2 * x + 1) * background.width / (2 * Frame::Side);
			const auto source = background.pixels + sourceY * background.strideBytes + sourceX * 4;
			std::copy_n(source, 4, frame.rgba.begin() + y * frame.strideBytes + x * 4);
		}
	}
	for (int layer = 0; layer < 4; ++layer)
		for (const auto& marker : markers)
		{
			if (!marker.visible || marker.dead || marker.kind == Marker::Kind::Unsupported || Layer(marker) != layer) continue;
			if (marker.worldX < bounds.minX || marker.worldX > bounds.maxX ||
				marker.worldY < bounds.minY || marker.worldY > bounds.maxY) continue;
			Paint(frame, marker, bounds);
		}
	return frame;
}

void PwRuffleClipMinimapFrame(PwRuffleMinimapFrame& frame)
{
	Require(frame.width == Frame::Side && frame.height == Frame::Side &&
		frame.strideBytes == Frame::Side * 4 && frame.rgba.size() == Frame::PixelBytes,
		"frame", "clip requires exactly 270x270 RGBA pixels, stride 1080 and 291600 bytes");
	constexpr int center = 135;
	constexpr int radius = 132;
	for (int y = 0; y < static_cast<int>(Frame::Side); ++y)
		for (int x = 0; x < static_cast<int>(Frame::Side); ++x)
		{
			const int dx = x - center;
			const int dy = y - center;
			if (dx * dx + dy * dy <= radius * radius) continue;
			const auto offset = static_cast<std::size_t>(y) * frame.strideBytes + static_cast<std::size_t>(x) * 4;
			std::fill_n(frame.rgba.begin() + offset, 4, std::uint8_t{0});
		}
}
