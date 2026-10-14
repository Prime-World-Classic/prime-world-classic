#include "Scene/AGHelpers.h"

/** Run before window/render setup: original graph semantics and shipped clip metadata require no GL. */
static bool RunLinuxAbilityTimingProbe()
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char* label) {
		++checks; if (!good) ++failures;
		std::printf("Ability animation timing: %s result=%s\n", label, good ? "PASS" : "FAIL");
	};
	{
		test::AnimationDataHolder data;
		auto* raw = new NDb::AnimGraphCreator; NDb::Ptr<NDb::AnimGraphCreator> creator = raw;
		raw->defaultNode = "normal";
		for (const char* name : {"normal", "attack", "subA", "subB"}) {
			data.AddAnimation(name, 2); test::CreateMacroNode(*raw, name, 2);
		}
		raw->macroNodes[1].nodeType = NDb::ANGRNODETYPE_DISPATCHER;
		for (int i = 2; i < 4; ++i) {
			raw->macroNodes[i].nodeType = NDb::ANGRNODETYPE_SUBNODE;
			raw->macroNodes[i].nextNode = "attack";
			test::CreateTransaction(raw->macroNodes[1], raw->macroNodes[i].nodeName, 0).probability = 50;
		}
		raw->macroNodes[0].startPar.value = .2f; raw->macroNodes[0].stopPar.value = .8f;
		raw->macroNodes[0].speed.value = 2;
		NDb::AnGrMarker marker; marker.name = "doFire"; marker.virtPar = .25f;
		raw->macroNodes[0].markers.push_back(marker);
		raw->macroNodes[2].markers.push_back(marker);
		marker.virtPar = .75f; raw->macroNodes[3].markers.push_back(marker);
		marker.name = "onlyB"; raw->macroNodes[3].markers.push_back(marker);
		test::MockAnimGraphBlender blender(data);
		auto* graph = DiAnimGraph::CreateByData(creator, data.animations, &blender);
		check(graph != nullptr, "original headless graph constructs");
		if (graph) {
			float value = 97;
			check(LinuxQueryAnimationTime(*graph, "normal", "doFire", value) && std::fabs(value-.15f) < 1e-5f,
				"trimmed clip marker uses original normalized node speed");
			check(LinuxQueryAnimationTime(*graph, "normal", nullptr, value) && std::fabs(value-graph->GetNodeDuration(0)) < 1e-6f,
				"work duration uses original graph duration query, not full clip length");
			check(LinuxQueryAnimationTime(*graph, "attack", "doFire", value) && std::fabs(value-.5f) < 1e-6f,
				"dispatcher returns first marked subnode, not random or average timing");
			check(LinuxQueryAnimationTime(*graph, "attack", "onlyB", value) && std::fabs(value-1.5f) < 1e-6f,
				"dispatcher skips a subnode lacking the requested marker");
			check(LinuxQueryAnimationTime(*graph, "attack", nullptr, value) && value == 2,
				"attack work duration uses first subnode");
			for (const char* missing : {"", "missing", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}) {
				value = 97;
				check(!LinuxQueryAnimationTime(*graph, missing, "doFire", value) && value == 97, "missing node preserves output");
			}
			value = 97;
			check(!LinuxQueryAnimationTime(*graph, "attack", "missing", value) && value == 97, "missing marker preserves output");
			graph->GetNodeData(2)->SetSpeed(0);
			check(!LinuxQueryAnimationTime(*graph, "attack", "doFire", value) && value == 97,
				"invalid first timing cannot fall through to another random child");
			check(blender.frames.empty(), "timing queries never ask the blender to sample a pose");
			DiAnimGraph::Destroy(graph);
		}
	}
	for (int malformed = 0; malformed < 8; ++malformed) {
		struct Header { char signature[4]; float minTime, maxTime; uint32_t count, offset; } header = {{'A','N','I','M'}, 1, 3, 1, 8};
		if (malformed == 1) header.minTime = std::numeric_limits<float>::quiet_NaN();
		if (malformed == 2) header.maxTime = .5f;
		if (malformed == 3) header.maxTime = 4000;
		if (malformed == 4) header.count = 513;
		if (malformed == 5) header.offset = UINT32_MAX;
		if (malformed == 6) header.offset = 0;
		MemoryStream stream(sizeof(header)+sizeof(TBoneTracks));
		stream.Write(&header, sizeof(header)); stream.Seek(0, SEEKORIGIN_BEGIN);
		if (malformed == 7) stream.SetSize(sizeof(header)-1);
		float held = 97;
		const bool good = LinuxAnimationClipDuration(stream, held);
		check(malformed == 0 ? good && held == 2 : !good && held == 97,
			"bounded original duration header handles valid, truncated and malformed metadata");
	}
	const auto scene = NDb::Get<NDb::DBSceneObject>(NDb::DBID("Heroes/Plane/_.SOBJ"));
	NDb::Unit unit; unit.sceneObject = scene;
	LinuxAuthoredAbilityTiming source;
	float duration = 0, fire = 0;
	std::srand(1729); const int expectedRandom = std::rand(); std::srand(1729);
	check(scene && source.Query(unit, "attack", nullptr, duration) && duration > 0,
		"shipped Plane attack duration available before any window, mesh or skeleton owner");
	check(std::rand() == expectedRandom, "metadata graph construction does not consume even presentation RNG");
	check(source.Query(unit, "attack", "doFire", fire) && fire > 0 && fire <= duration,
		"shipped Plane doFire marker available without a renderer");
	for (int i = 0; i < 4; ++i) {
		float same = 0;
		check(source.Query(unit, "attack", "doFire", same) && same == fire, "repeated timing is immutable and deterministic");
	}
	float work = 0;
	check(source.Query(unit, "ability1", "workTime1", work) && work > 0, "shipped ordinary ability uses original work marker");
	NDb::BaseHero hero; hero.heroSceneObjects.resize(1); hero.heroSceneObjects[0] = scene;
	float fallback = 0;
	check(source.Query(hero, "attack", "doFire", fallback) && fallback == fire, "original offscreen hero scene fallback matches unit scene");
	for (int i = 0; i < 257; ++i) {
		NDb::Unit empty; empty.sceneObject = new NDb::DBSceneObject;
		float held = 97;
		check(!source.Query(empty, "attack", "doFire", held) && held == 97, "empty scene cannot replace the caller's timing");
	}
	fallback = 0;
	check(source.Query(unit, "attack", "doFire", fallback) && fallback == fire, "bounded cache eviction preserves authored timing");
	std::printf("Ability animation timing: checks=%u failures=%u Plane work=%g attack=%g fire=%g\n", checks, failures, work, duration, fire);
	return failures == 0;
}
