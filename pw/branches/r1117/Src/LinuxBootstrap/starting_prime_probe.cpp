#include "starting_prime.h"
#include <cstdio>

/** Mock authored map values preserve truncation and reject invalid allocations. */
int main()
{
	using LinuxBootstrap::ResolveStartingPrime;
	int checks = 0, failures = 0;
	const auto check = [&](bool ok) { ++checks; if (!ok) ++failures; };
	check(ResolveStartingPrime(1500, 5) == 300);
	check(ResolveStartingPrime(1000, 3) == 333);
	check(ResolveStartingPrime(0, 5) == 0);
	check(ResolveStartingPrime(4, 5) == 0);
	check(ResolveStartingPrime(5, 1) == 5);
	check(ResolveStartingPrime(-1, 5) == 0);
	check(ResolveStartingPrime(1500, 0) == 0);
	check(ResolveStartingPrime(1500, -1) == 0);
	check(ResolveStartingPrime(std::numeric_limits<float>::infinity(), 5) == 0);
	check(ResolveStartingPrime(std::numeric_limits<float>::quiet_NaN(), 5) == 0);
	check(ResolveStartingPrime(std::numeric_limits<float>::max(), 1) == 0);
	check(ResolveStartingPrime(2147483648.f, 1) == 0);
	check(ResolveStartingPrime(2147483520.f, 1) == 2147483520);
	std::printf("Starting prime: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
