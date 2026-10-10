#include "ability_display.h"
#include <cstdio>
#include <limits>

/** Exercise all combinations, including invalid inactive state and authored zero. */
int main()
{
	const float values[] = {0, 10, -1, std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
	int checks = 0, failures = 0;
	for (unsigned i = 0; i < 6; ++i)
	for (unsigned j = 0; j < 6; ++j)
	for (unsigned k = 0; k < 6; ++k)
	{
		const auto view = LinuxBootstrap::MakeAbilityCooldownDisplay(values[i], values[j], values[k]);
		const bool valid = i < 2 && j < 2 && k < 2;
		++checks;
		if (view.supported != valid || view.current != (valid ? values[i] : 0)
			|| view.maximum != (valid ? values[j] : 0)) ++failures;
	}
	std::printf("Ability display: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
