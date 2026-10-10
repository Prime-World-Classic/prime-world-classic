#define None 0L
#include "ground_talent_target.h"
#undef None

#include <array>
#include <cstdio>
#include <cstdlib>

namespace
{
using State = LinuxBootstrap::GroundTalentTargetState;
using LinuxBootstrap::CanUseLinuxGroundTalentState;
using LinuxBootstrap::IsLinuxGroundTalentReady;
std::size_t checks = 0;

/** Abort with an actionable label; no asserts that disappear in release builds. */
void Check(bool condition, const char* label)
{
	++checks;
	if (condition) return;
	std::fprintf(stderr, "Ground talent target failed: %s (check %zu)\n", label, checks);
	std::exit(1);
}

/** Mock one exact owned, bought ground talent; geometry and range are separate facts. */
State Ready()
{
	State state;
	state.row = 2;
	state.column = 4;
	state.clientId = state.ownerClientId = 7;
	state.exactHero = state.human = state.playing = state.alive = true;
	state.controlsAllowed = state.bought = state.active = state.usable = true;
	state.position = state.land = state.targetValid = state.castAllowed = true;
	state.x = 10;
	state.y = 20;
	state.width = 100;
	state.height = 200;
	state.useRange = 15;
	state.inRange = true;
	return state;
}

/** Exhaust all 16 Boolean facts, including contradictory live snapshots. */
void ReadinessMatrix()
{
	for (unsigned mask = 0; mask < (1u << 16); ++mask)
	{
		State state = Ready();
		bool* fields[] = {&state.exactHero, &state.human, &state.playing, &state.alive,
			&state.controlsAllowed, &state.bought, &state.active, &state.usable,
			&state.position, &state.land, &state.targetValid, &state.castAllowed,
			&state.lineOfSight, &state.alternativeTargets, &state.outOfRangeAllowed, &state.inRange};
		for (unsigned bit = 0; bit < 16; ++bit) *fields[bit] = (mask & (1u << bit)) != 0;
		const bool ready = (mask & 0x3ffu) == 0x3ffu && (mask & 0x3000u) == 0;
		const bool usable = ready && (mask & 0xc00u) == 0xc00u && (mask & 0xc000u) != 0;
		Check(IsLinuxGroundTalentReady(state) == ready, "readiness permission/type matrix");
		Check(CanUseLinuxGroundTalentState(state) == usable, "full policy Boolean matrix");
	}
	Check(!CanUseLinuxGroundTalentState(State{}), "default facts reject");
}

/** Authored rows/columns and exact client identity must never be silently substituted. */
void IdentityMatrix()
{
	for (int row = -1; row <= 6; ++row)
	for (int column = -1; column <= 6; ++column)
	for (int client : {-1, 0, 7})
	for (int owner : {-1, 0, 7, 8})
	{
		State state = Ready();
		state.row = row;
		state.column = column;
		state.clientId = client;
		state.ownerClientId = owner;
		const bool expected = row >= 0 && row < 6 && column >= 0 && column < 6 && client >= 0 && client == owner;
		Check(CanUseLinuxGroundTalentState(state) == expected, "slot/owner identity matrix");
	}
	for (int edge : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		State state = Ready();
		state.row = edge;
		Check(!CanUseLinuxGroundTalentState(state), "extreme row rejects before indexing");
		state = Ready();
		state.column = edge;
		Check(!CanUseLinuxGroundTalentState(state), "extreme column rejects before indexing");
	}
}

/** Cover both edges of rectangular maps and every nonfinite coordinate/extent. */
void CoordinateMatrix()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const std::array<float, 9> xs{{-1, -0.0f, 0, 50, std::nextafter(100.0f, 0.0f), 100, inf, -inf, nan}};
	const std::array<bool, 9> inside{{false, true, true, true, true, false, false, false, false}};
	for (std::size_t x = 0; x < xs.size(); ++x)
	for (std::size_t y = 0; y < xs.size(); ++y)
	{
		State state = Ready();
		state.x = xs[x];
		state.y = xs[y] * 2;
		Check(CanUseLinuxGroundTalentState(state) == (inside[x] && inside[y]), "finite rectangular half-open coordinates");
	}
	for (float z : {-100.0f, -0.0f, 0.0f, 100.0f, inf, -inf, nan})
	{
		State state = Ready();
		state.z = z;
		Check(CanUseLinuxGroundTalentState(state) == std::isfinite(z), "height must be finite but is not a map axis");
	}
	for (float width : {-1.0f, -0.0f, 0.0f, 10.0f, 100.0f, inf, -inf, nan})
	for (float height : {-1.0f, -0.0f, 0.0f, 20.0f, 200.0f, inf, -inf, nan})
	{
		State state = Ready();
		state.width = width;
		state.height = height;
		Check(CanUseLinuxGroundTalentState(state) == (width == 100 && height == 200), "invalid or boundary map extents reject");
	}
	State state = Ready();
	state.width = state.height = std::numeric_limits<float>::max();
	state.x = state.y = std::nextafter(state.width, 0.0f);
	Check(CanUseLinuxGroundTalentState(state), "large finite map avoids overflowing distance math");
	state.x = state.width;
	Check(!CanUseLinuxGroundTalentState(state), "large finite map still excludes upper edge");
	state = Ready();
	state.width = state.height = std::numeric_limits<float>::denorm_min();
	state.x = state.y = 0;
	Check(CanUseLinuxGroundTalentState(state), "strictly positive subnormal extents are finite");
}

/** Production range semantics, not an independently calculated mock distance. */
void RangeMatrix()
{
	const float inf = std::numeric_limits<float>::infinity();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	for (float range : {-inf, -std::numeric_limits<float>::max(), -1.0f, -0.0f, 0.0f,
		std::numeric_limits<float>::denorm_min(), 1.0f, std::numeric_limits<float>::max(), inf, nan})
	for (bool bypass : {false, true})
	for (bool inRange : {false, true})
	{
		State state = Ready();
		state.useRange = range;
		state.outOfRangeAllowed = bypass;
		state.inRange = inRange;
		const bool allowed = std::isfinite(range) && (range <= 0 || bypass || inRange);
		Check(CanUseLinuxGroundTalentState(state) == allowed, "finite production range/bypass matrix");
		state.castAllowed = false;
		Check(!CanUseLinuxGroundTalentState(state), "range bypass cannot bypass cast limitations");
		state.castAllowed = true;
		state.x = -1;
		Check(!CanUseLinuxGroundTalentState(state), "range bypass cannot bypass map bounds");
	}
}

/** Reevaluate changed facts between submit and execute; no cached authorization. */
void StateChangesAndScope()
{
	const std::array<bool State::*, 12> required{{&State::exactHero, &State::human, &State::playing,
		&State::alive, &State::controlsAllowed, &State::bought, &State::active, &State::usable,
		&State::position, &State::land, &State::targetValid, &State::castAllowed}};
	for (auto field : required)
	{
		State state = Ready();
		Check(CanUseLinuxGroundTalentState(state), "submission snapshot accepts");
		state.*field = false;
		Check(!CanUseLinuxGroundTalentState(state), "execution rejects changed permission/readiness");
		state.*field = true;
		Check(CanUseLinuxGroundTalentState(state), "restored facts evaluated afresh");
	}
	State state = Ready();
	state.inRange = false;
	Check(!CanUseLinuxGroundTalentState(state), "hero moved out of range before execution");
	state.inRange = true;
	state.ownerClientId = 8;
	Check(!CanUseLinuxGroundTalentState(state), "ownership changed before execution");
	state = Ready();
	state.width = state.x;
	Check(!CanUseLinuxGroundTalentState(state), "map changed before execution");
	for (bool script : {false, true})
	for (bool position : {false, true})
	for (bool bot : {false, true})
		Check(LinuxBootstrap::NeedsLinuxGroundTalentValidation(script, position, bot) ==
			(!script && position && !bot), "only nonscript non-bot position commands take new policy");
}
}

/** Standalone C++17 probe of the same inline policy used by the engine wrapper. */
int main()
{
	ReadinessMatrix();
	IdentityMatrix();
	CoordinateMatrix();
	RangeMatrix();
	StateChangesAndScope();
	std::printf("Ground talent target: %zu checks, 0 failures\n", checks);
	return 0;
}
