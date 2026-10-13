/** White-box CPU probe: compile the implementation here to test its private pure validators.
 * No context is created and no GL function is called. The normal backend library is not linked.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// This executable owns one implementation TU; no backend definitions cross its boundary.
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif
#include "shader_backend.cpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pwshader;
struct Checks {
	unsigned count = 0, failed = 0;
	bool quiet = false;
	void Test(bool ok, const char* name) {
		++count; if (!ok) ++failed;
		if (!quiet) std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	}
};

Bytes Read(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	const auto size = file.tellg();
	if (!file || size <= 0 || size > 4*1024*1024) throw std::runtime_error("invalid archive file");
	Bytes bytes(static_cast<size_t>(size)); file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("short archive read");
	return bytes;
}
void Put32(Bytes& bytes, size_t at, uint32_t value) {
	for (size_t i = 0; i < 4; ++i) bytes.at(at+i) = static_cast<unsigned char>(value >> (8*i));
}

/** Borrowed reflection edits live only through the immediate validation call. */
void ReflectionTests(Checks& check, const MOJOSHADER_parseData& original, bool vertex, size_t slot) {
	const bool valid = ExactFamilyReflection(&original, vertex, slot);
	check.Test(valid, "actual selected stage matches strict family reflection");
	if (!valid) return;
	check.Test(!ExactFamilyReflection(&original, vertex, 3), "invalid family slot rejected");
	check.Test(!ExactFamilyReflection(nullptr, vertex, slot), "missing parse result rejected");
	auto changed = original;
	changed.major_ver = 2;
	check.Test(!ExactFamilyReflection(&changed, vertex, slot), "shader model mismatch rejected");
	changed = original; changed.minor_ver = 1;
	check.Test(!ExactFamilyReflection(&changed, vertex, slot), "minor shader version rejected");
	changed = original; changed.shader_type = vertex ? MOJOSHADER_TYPE_PIXEL : MOJOSHADER_TYPE_VERTEX;
	check.Test(!ExactFamilyReflection(&changed, vertex, slot), "wrong shader stage rejected");
	changed = original; changed.error_count = 1;
	check.Test(!ExactFamilyReflection(&changed, vertex, slot), "parse error rejected");
	MOJOSHADER_preshader preshader{};
	changed = original; changed.preshader = &preshader;
	check.Test(!ExactFamilyReflection(&changed, vertex, slot), "unexpected preshader rejected");
	for (int kind = 0; kind < 5; ++kind) {
		auto uniforms = std::vector<MOJOSHADER_uniform>(original.uniforms, original.uniforms + original.uniform_count);
		changed = original; changed.uniforms = uniforms.data();
		if (kind == 0) --changed.uniform_count;
		if (kind == 1) uniforms[0].type = MOJOSHADER_UNIFORM_INT;
		if (kind == 2) uniforms[0].array_count = 1;
		if (kind == 3) uniforms[0].constant = 1;
		if (kind == 4) uniforms[0].index = uniforms[1].index;
		check.Test(!ExactFamilyReflection(&changed, vertex, slot), "unexpected uniform count/type/array/constant/duplicate rejected");
	}
	for (int kind = 0; kind < 3; ++kind) {
		auto attributes = std::vector<MOJOSHADER_attribute>(original.attributes, original.attributes + original.attribute_count);
		changed = original; changed.attributes = attributes.data();
		if (kind == 0) --changed.attribute_count;
		if (kind == 1) attributes[0].usage = MOJOSHADER_USAGE_NORMAL;
		if (kind == 2) attributes[0].index = 1;
		check.Test(!ExactFamilyReflection(&changed, vertex, slot), "unexpected vertex/pixel input rejected");
	}
	for (int kind = 0; kind < 4; ++kind) {
		auto outputs = std::vector<MOJOSHADER_attribute>(original.outputs, original.outputs + original.output_count);
		changed = original; changed.outputs = outputs.data();
		if (kind == 0) --changed.output_count;
		if (kind == 1) ++changed.output_count;
		if (kind == 2) outputs[0].usage = MOJOSHADER_USAGE_DEPTH;
		if (kind == 3) outputs.back().index = 1;
		check.Test(!ExactFamilyReflection(&changed, vertex, slot), "missing/extra/depth/indexed output rejected");
	}
	if (vertex) {
		for (int kind = 0; kind < 6; ++kind) {
			auto constant = original.constants[0];
			changed = original; changed.constants = &constant;
			if (kind == 0) changed.constant_count = 0;
			if (kind == 1) changed.constant_count = 2;
			if (kind == 2) constant.type = MOJOSHADER_UNIFORM_INT;
			if (kind == 3) constant.index = 5;
			if (kind == 4) constant.value.f[0] = 0;
			if (kind == 5) constant.value.f[3] = std::numeric_limits<float>::quiet_NaN();
			check.Test(!ExactFamilyReflection(&changed, vertex, slot), "unexpected shader-defined VS constant rejected");
		}
		changed = original; changed.sampler_count = 1;
		check.Test(!ExactFamilyReflection(&changed, vertex, slot), "VS sampler rejected");
	} else {
		for (int kind = 0; kind < 4; ++kind) {
			auto sampler = original.samplers[0];
			changed = original; changed.samplers = &sampler;
			if (kind == 0) changed.sampler_count = 0;
			if (kind == 1) sampler.index = 1;
			if (kind == 2) sampler.type = MOJOSHADER_SAMPLER_CUBE;
			if (kind == 3) sampler.texbem = 1;
			check.Test(!ExactFamilyReflection(&changed, vertex, slot), "unexpected pixel sampler rejected");
		}
		MOJOSHADER_constant constant{};
		changed = original; changed.constant_count = 1; changed.constants = &constant;
		check.Test(!ExactFamilyReflection(&changed, vertex, slot), "shader-defined PS constant rejected");
		check.Test(ExactFamilyReflection(&original, false, slot == 0 ? 0 : 3-slot),
			"opacity pairs have the same interface but require separate semantic pixel tests");
	}
	if (slot == 0) {
		changed = original; changed.output_count = 0;
		check.Test(ExactReflection(&changed, vertex), "legacy pair-zero reflection acceptance unchanged");
	}
}

/** Require the shipped fixture before applying mutations at its known offsets. */
void Run(Checks& check, const Bytes& archive) {
	FamilySpans selected{};
	const bool valid = bool(Directory(archive, selected));
	check.Test(valid, "actual 96-pair archive accepted");
	if (!valid) return;
	const FamilySpans expected{{
		{{{772,464},{1236,300}}}, {{{772,464},{6236,384}}}, {{{772,464},{10088,384}}}
	}};
	for (size_t slot = 0; slot < 3; ++slot) for (size_t stage = 0; stage < 2; ++stage) {
		const bool matches = selected[slot][stage].offset == expected[slot][stage].offset &&
			selected[slot][stage].size == expected[slot][stage].size;
		check.Test(matches, "directory selects exact fixed-family stage span");
		if (!matches) return;
	}
	for (size_t slot = 0; slot < 3; ++slot) for (size_t stage = 0; stage < 2; ++stage) {
		const auto span = selected[slot][stage];
		Budget budget;
		const auto* parsed = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL120, "main", archive.data()+span.offset,
			unsigned(span.size), nullptr, 0, nullptr, 0, Budget::Allocate, Budget::Free, &budget);
		check.Test(parsed && parsed->error_count == 0, "CPU parser accepts actual selected bytecode");
		if (parsed && !parsed->error_count) ReflectionTests(check, *parsed, stage == 0, slot);
		MOJOSHADER_freeParseData(parsed);
		check.Test(budget.live == 0, "CPU parser allocations released");
	}
	for (size_t length : {size_t(0), size_t(3), size_t(4), size_t(771)}) {
		const Bytes shortArchive(archive.begin(), archive.begin()+length);
		check.Test(!Directory(shortArchive, selected), "truncated directory rejected before access");
		Checks rejected;
		rejected.quiet = true;
		Run(rejected, shortArchive);
		check.Test(rejected.count == 1 && rejected.failed == 1, "probe stops before parsing a truncated input");
	}
	// A valid directory alone does not authorize fixture-specific offsets or reflection edits.
	Bytes otherArchive(788, 0);
	Put32(otherArchive, 0, 96);
	for (size_t pair = 0; pair < 96; ++pair) {
		Put32(otherArchive, 4+pair*8, 772);
		Put32(otherArchive, 8+pair*8, 780);
	}
	Put32(otherArchive, 772, 0xfffe0300U); Put32(otherArchive, 776, 0x0000ffffU);
	Put32(otherArchive, 780, 0xffff0300U); Put32(otherArchive, 784, 0x0000ffffU);
	Checks rejected;
	rejected.quiet = true;
	Run(rejected, otherArchive);
	check.Test(rejected.count == 2 && rejected.failed == 1, "probe stops before using another valid archive as the shipped fixture");
	for (size_t pair : {size_t(0), size_t(12), size_t(24), size_t(95)}) {
		for (uint32_t offset : {4u, 773u, UINT32_MAX, 1236u}) {
			auto bad = archive; Put32(bad, 4+pair*8, offset);
			check.Test(!Directory(bad, selected), "invalid selected or unselected directory offset rejected");
		}
	}
	auto bad = archive; Put32(bad, 0, UINT32_MAX);
	check.Test(!Directory(bad, selected), "oversized pair count rejected");
	bad = archive; Put32(bad, 1232, 0);
	check.Test(!Directory(bad, selected), "missing shared VS END rejected");
	BasicFxOpacityPrograms refs;
	check.Test(refs.simple.Empty() && refs.additiveOpacity.Empty() && refs.blendOpacity.Empty(), "default family is three empty CPU tickets");
}
}

#include "baked_cpu_tests.inc"
#include "terrain_cpu_tests.inc"
#include "sky_cpu_tests.inc"

int main(int argc, char** argv) {
	if (argc < 2 || argc > 5) return 2;
	Checks check;
	try {
		Run(check, Read(argv[1]));
		if (argc >= 3) BakedCpuTests(check, Read(argv[2]));
		if (argc >= 4) TerrainCpuTests(check, Read(argv[3]));
		if (argc == 5) SkyCpuTests(check, Read(argv[4]));
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; check.Test(false, "CPU exception"); }
	std::cout << "SUMMARY checks=" << check.count << " failures=" << check.failed << '\n';
	return check.failed ? 1 : 0;
}
