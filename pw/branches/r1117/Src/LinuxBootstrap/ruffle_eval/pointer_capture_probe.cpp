#include "pointer_capture.h"
#include <cstdio>

/** Exhaust initial ownership, release positions, all buttons and focus cancellation. */
int main()
{
	using Kind = PwRufflePointerCapture::Kind;
	int checks = 0, failures = 0;
	const auto check = [&](PwRufflePointerCapture::Decision d, bool forward, bool consume)
	{
		++checks;
		if (d.forward != forward || d.consume != consume) ++failures;
	};
	for (unsigned button = 0; button < 3; ++button)
	for (bool press : {false, true})
	for (bool release : {false, true})
	{
		PwRufflePointerCapture capture;
		check(capture.Route(Kind::Down, press, button), press, press);
		check(capture.Route(Kind::Down, !press, button), press, press);
		check(capture.Route(Kind::Move, !press), press, press);
		check(capture.Route(Kind::Up, release, button), press, press);
		check(capture.Route(Kind::Move, release), true, false);
		check(capture.Route(Kind::Up, release, button), false, release);
		capture.Route(Kind::Down, press, button);
		capture.Reset();
		check(capture.Route(Kind::Up, false, button), false, false);
	}
	PwRufflePointerCapture capture;
	check(capture.Route(Kind::Wheel, true), true, true);
	check(capture.Route(Kind::Wheel, false), false, false);
	check(capture.Route(Kind::Leave, false), true, false);
	check(capture.Route(Kind::Down, false, 2), false, false);
	check(capture.Route(Kind::Down, true, 0), true, true);
	check(capture.Route(Kind::Move, false), true, true);
	check(capture.Route(Kind::Up, false, 0), true, true);
	check(capture.Route(Kind::Move, true), false, false);
	check(capture.Route(Kind::Up, true, 2), false, false);
	check(capture.Route(Kind::Move, false), true, false);
	try { capture.Route(Kind::Down, true, 3); ++failures; } catch (const std::invalid_argument&) {}
	try { capture.Route(static_cast<Kind>(99), true); ++failures; } catch (const std::invalid_argument&) {}
	std::printf("Pointer capture: %d decisions, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
