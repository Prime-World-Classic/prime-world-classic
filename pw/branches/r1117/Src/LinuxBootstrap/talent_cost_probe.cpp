#include "talent_cost.h"
#include <cstdio>

/** Mock table values distinguish level indexing, explicit costs and missing data. */
int main()
{
	const int levels[] = {101, 203, 307, 409, 503, 607};
	int failures = 0;
	const auto check = [&](bool ok) { if (!ok) ++failures; };
	for (int level = 0; level < 6; ++level)
	{
		check(LinuxBootstrap::ResolveTalentCost(-1, level, levels, 6) == levels[level]);
		check(LinuxBootstrap::ResolveTalentCost(-9, level, levels, 6) == levels[level]);
		check(LinuxBootstrap::ResolveTalentCost(0, level, levels, 6) == 0);
		check(LinuxBootstrap::ResolveTalentCost(79, level, levels, 6) == 79);
	}
	const int unavailable = std::numeric_limits<int>::max();
	check(LinuxBootstrap::ResolveTalentCost(-1, -1, levels, 6) == unavailable);
	check(LinuxBootstrap::ResolveTalentCost(-1, 6, levels, 6) == unavailable);
	check(LinuxBootstrap::ResolveTalentCost(-1, 0, nullptr, 6) == unavailable);
	check(LinuxBootstrap::ResolveTalentCost(-1, 0, levels, 0) == unavailable);
	const int bad[] = {-1};
	check(LinuxBootstrap::ResolveTalentCost(-1, 0, bad, 1) == unavailable);
	std::printf("Talent cost: 29 checks, %d failures\n", failures);
	return failures ? 1 : 0;
}
