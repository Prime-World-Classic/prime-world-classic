#pragma once

#include <algorithm>

namespace LinuxBootstrap
{
/// Pixel rectangle with half-open hit-test bounds, shared by HUD drawing and map input.
struct HudRect
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	bool Contains(int px, int py) const
	{
		return px >= x && py >= y && px < x + width && py < y + height;
	}
};

struct WorldHudLayout
{
	bool ready = false;
	HudRect minimap;
	HudRect hero;
	HudRect scoreboard;
	/// The entire visible panel consumes map input, not just its action icons.
	bool Contains(int x, int y) const
	{
		return ready && (minimap.Contains(x, y) || hero.Contains(x, y) || scoreboard.Contains(x, y));
	}
};

/// Reserve a stable bottom dock and top summary. Tiny windows omit the HUD instead of overflowing.
inline WorldHudLayout ResolveWorldHudLayout(int width, int height)
{
	WorldHudLayout result;
	if (width < 640 || height < 360)
		return result;
	const int margin = 8;
	const int gap = 8;
	const int mapSize = std::max(128, std::min(192, width / 6));
	const int heroWidth = std::min(860, width - margin * 2 - mapSize - gap);
	const int left = (width - mapSize - gap - heroWidth) / 2;
	result.minimap = {left, height - margin - mapSize - 32, mapSize, mapSize + 32};
	result.hero = {left + mapSize + gap, height - margin - 176, heroWidth, 176};
	const int scoreWidth = std::min(666, width - margin * 2);
	result.scoreboard = {(width - scoreWidth) / 2, margin, scoreWidth, 88};
	result.ready = true;
	return result;
}
}
