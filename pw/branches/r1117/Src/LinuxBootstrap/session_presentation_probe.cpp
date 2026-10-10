#include "session_presentation.h"
#include "world_hud_layout.h"
#include <cstdio>

/// Reject intersections, including those caused by unusually narrow or wide windows.
bool Overlaps(const LinuxBootstrap::HudRect& a, const LinuxBootstrap::HudRect& b)
{
	return a.x < b.x + b.width && b.x < a.x + a.width &&
		a.y < b.y + b.height && b.y < a.y + a.height;
}

/// Exhaust all readiness/override combinations without a display or game assets.
int main()
{
	using LinuxBootstrap::SessionPresentation;
	int failures = 0;
	for (unsigned flags = 0; flags < 4; ++flags)
		if (LinuxBootstrap::CanDispatchBootstrapInput((flags & 1) != 0, (flags & 2) != 0) != (flags != 0))
			++failures;
	for (unsigned flags = 0; flags < 32; ++flags)
	{
		const bool inspect = (flags & 1) != 0;
		const bool legacy = (flags & 2) != 0;
		const bool map = (flags & 4) != 0;
		const bool world = (flags & 8) != 0;
		const bool render = (flags & 16) != 0;
		const SessionPresentation result = LinuxBootstrap::ResolveSessionPresentation(inspect, legacy, map, world, render);
		const bool valid = inspect ? result == SessionPresentation::Loading :
			legacy ? result == SessionPresentation::LegacyOverlay :
			(map && world && render) ? result == SessionPresentation::World : result == SessionPresentation::Loading;
		if (!valid)
		{
			std::printf("Session presentation: flags=%u FAIL\n", flags);
			++failures;
		}
	}
	std::printf("Session presentation: 32 mode and 4 input combinations, failures=%d\n", failures);
	unsigned layouts = 0;
	for (int width = 640; width <= 3840; width += 80)
		for (int height = 360; height <= 2160; height += 24)
		{
			const LinuxBootstrap::WorldHudLayout layout = LinuxBootstrap::ResolveWorldHudLayout(width, height);
			const LinuxBootstrap::HudRect rects[] = {layout.minimap, layout.hero, layout.scoreboard};
			bool valid = layout.ready && layout.hero.width >= 488 && !Overlaps(rects[0], rects[1]) &&
				!Overlaps(rects[1], rects[2]) && !Overlaps(rects[0], rects[2]);
			for (const LinuxBootstrap::HudRect& rect : rects)
				valid = valid && rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= width &&
					rect.y + rect.height <= height && layout.Contains(rect.x, rect.y) &&
					rect.Contains(rect.x + rect.width - 1, rect.y + rect.height - 1) &&
					!rect.Contains(rect.x + rect.width, rect.y) && !rect.Contains(rect.x, rect.y + rect.height);
			valid = valid && !layout.Contains(-1, -1) && !layout.Contains(width / 2, height / 2 - 8);
			if (!valid)
			{
				std::printf("World HUD: %dx%d FAIL\n", width, height);
				++failures;
			}
			++layouts;
		}
	const int invalid[][2] = {{0, 0}, {-1, 720}, {1280, -1}, {639, 720}, {1280, 359}};
	for (const auto& size : invalid)
		if (LinuxBootstrap::ResolveWorldHudLayout(size[0], size[1]).ready)
			++failures;
	std::printf("World HUD: %u layouts, failures=%d\n", layouts, failures);
	return failures ? 1 : 0;
}
