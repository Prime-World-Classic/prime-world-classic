#include "baked_vertex_colors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <utility>

#if defined(BAKED_COLORS_TEST_ALLOCATIONS)
namespace AllocationProbe
{
// Test-executable-only replacement, armed only across a single Decode call.
bool enabled = false;
std::size_t calls = 0, failAfter = 0;
}

void* operator new(std::size_t size)
{
	if (AllocationProbe::enabled && AllocationProbe::calls++ == AllocationProbe::failAfter)
		throw std::bad_alloc();
	if (void* memory = std::malloc(size ? size : 1)) return memory;
	throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }
#endif

namespace
{
using namespace LinuxBootstrap;
using Bytes = std::vector<std::uint8_t>;
using Code = BakedVertexColorError::Code;
std::size_t checks = 0, failures = 0;

void Check(bool condition, const char* name)
{
	++checks;
	if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}

/** Write explicit little-endian mock words, independent of host representation. */
void Word(Bytes& bytes, std::uint32_t value)
{
	for (unsigned shift = 0; shift != 32; shift += 8)
		bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void SetWord(Bytes& bytes, std::size_t at, std::uint32_t value)
{
	for (unsigned shift = 0; shift != 32; shift += 8)
		bytes[at++] = static_cast<std::uint8_t>(value >> shift);
}

void Component(Bytes& bytes, std::uint32_t crc, const Bytes& day, const Bytes& night)
{
	Word(bytes, crc);
	Word(bytes, static_cast<std::uint32_t>(day.size() / 4));
	bytes.insert(bytes.end(), day.begin(), day.end());
	Word(bytes, static_cast<std::uint32_t>(night.size() / 4));
	bytes.insert(bytes.end(), night.begin(), night.end());
}

/** Two placements of one geometry, unsorted keys, an empty and an unequal pair. */
Bytes Fixture()
{
	Bytes bytes;
	Word(bytes, 3);
	Word(bytes, 2); Word(bytes, 17); Word(bytes, 99); Word(bytes, 7);
	Word(bytes, 2);
	Component(bytes, 42, {1, 2, 3, 4, 253, 254, 255, 2}, {8, 9, 10, 11, 12, 13, 14, 15});
	Component(bytes, 0, {}, {});
	Word(bytes, 1);
	Component(bytes, 43, {20, 21, 22, 23}, {});
	Word(bytes, 0); Word(bytes, 17);
	Word(bytes, 1); Word(bytes, 17); Word(bytes, 99); Word(bytes, 1);
	Component(bytes, 42, {101, 102, 103, 104, 105, 106, 107, 108}, {111, 112, 113, 114, 115, 116, 117, 118});
	return bytes;
}

BakedVertexColors::Ptr Decode(Bytes bytes, const BakedVertexColorLimits& limits = {})
{
	BakedVertexColors::Ptr map;
	BakedVertexColorError error{Code::MissingObject, 999};
	Check(BakedVertexColors::Decode(std::move(bytes), map, error, limits), "decode accepted fixture");
	Check(map && error.code == Code::None && error.offset == 0, "success publishes map and clears error");
	return map;
}

void Reject(Bytes bytes, Code expected, const BakedVertexColorLimits& limits = {})
{
	BakedVertexColors::Ptr output;
	BakedVertexColorError error;
	auto seed = Fixture();
	BakedVertexColors::Decode(std::move(seed), output, error);
	Check(!BakedVertexColors::Decode(std::move(bytes), output, error, limits), "malformed decode rejected");
	Check(!output, "decode failure discards prior output");
	Check(error.code == expected, "decode failure diagnostic");
}

void MockTests()
{
	const auto fixture = Fixture();
	auto map = Decode(fixture);
	if (!map) return;
	Check(map->ObjectCount() == 3 && map->SceneCount() == 3 && map->ComponentCount() == 4,
		"original object/scene/component counts include empty records");
	Check(map->ColorCount() == 9 && map->ParsedBytes() == fixture.size() && map->SourceBytes() == fixture.size(),
		"source/parsed/colour accounting");
	BakedVertexColorView view, second;
	BakedVertexColorError error{Code::MissingScene, 123};
	Check(map->Lookup(0, 99, 0, 42, 2, view, error), "first unsorted scene lookup");
	Check(error.code == Code::None && error.offset == 0, "lookup clears old error");
	Check(view.Valid() && view.DayOffset() == 32 && view.NightOffset() == 44 && view.ColorCount() == 2 &&
		view.ByteCount() == 8 && view.GeometryCRC() == 42, "validated offsets and counts");
	Check(view.DayData() && std::memcmp(view.DayData(), fixture.data() + 32, 8) == 0 &&
		view.NightData() && std::memcmp(view.NightData(), fixture.data() + 44, 8) == 0,
		"original BGRA bytes/alpha preserved without conversion");
	Check(map->Lookup(2, 99, 0, 42, 2, second, error), "second placement with same geometry");
	Check(view.DayData() != second.DayData() && view.NightData() != second.NightData() &&
		std::memcmp(view.DayData(), second.DayData(), 8) != 0, "same geometry does not alias placement colours");
	for (const auto& bad : std::array<std::array<std::size_t, 6>, 9>{{
		{{3, 99, 0, 42, 2, static_cast<std::size_t>(Code::MissingObject)}},
		{{1, 99, 0, 42, 2, static_cast<std::size_t>(Code::MissingScene)}},
		{{0, 100, 0, 42, 2, static_cast<std::size_t>(Code::MissingScene)}},
		{{0, 99, 2, 42, 2, static_cast<std::size_t>(Code::MissingComponent)}},
		{{0, 99, 1, 0, 0, static_cast<std::size_t>(Code::EmptyColors)}},
		{{0, 7, 0, 43, 1, static_cast<std::size_t>(Code::EmptyColors)}},
		{{0, 99, 0, 43, 2, static_cast<std::size_t>(Code::GeometryMismatch)}},
		{{0, 99, 0, 42, 1, static_cast<std::size_t>(Code::VertexCountMismatch)}},
		{{0, 99, 0, 42, 0, static_cast<std::size_t>(Code::VertexCountMismatch)}}}})
	{
		auto stale = view;
		Check(!map->Lookup(bad[0], static_cast<std::uint32_t>(bad[1]), bad[2], static_cast<std::uint32_t>(bad[3]), bad[4], stale, error),
			"invalid lookup rejected");
		Check(!stale.Valid() && !stale.DayData() && !stale.NightData() && stale.ColorCount() == 0 &&
			stale.ByteCount() == 0 && stale.GeometryCRC() == 0 && stale.DayOffset() == 0 && stale.NightOffset() == 0,
			"lookup rejection removes every stale field");
		Check(error.code == static_cast<Code>(bad[5]), "lookup diagnostic");
	}
	Check(!map->Lookup(std::numeric_limits<std::size_t>::max(), 99, 0, 42, 2, second, error), "huge object index rejected");
	Check(!map->Lookup(0, 99, std::numeric_limits<std::size_t>::max(), 42, 2, second, error), "huge component index rejected");
	Check(!map->Lookup(0, 99, 0, 42, std::numeric_limits<std::size_t>::max(), second, error), "huge vertex count rejected");
	map.reset();
	Check(view.DayData() && view.DayData()[0] == 1 && view.NightData()[0] == 8, "view outlives map metadata/source argument");
	auto moved = std::move(view);
	Check(!view.Valid() && !view.DayData() && view.ByteCount() == 0 && moved.Valid(), "moved-from view is empty");

	for (std::size_t length = 0; length < fixture.size(); ++length)
	{
		Bytes truncated(fixture.begin(), fixture.begin() + static_cast<std::ptrdiff_t>(length));
		BakedVertexColors::Ptr output;
		Check(!BakedVertexColors::Decode(std::move(truncated), output, error) && !output && error.code != Code::None,
			"every byte truncation of nested mock rejected");
	}
	for (const auto offset : {0u, 4u, 8u, 20u, 28u, 40u})
		for (const auto impossible : {0x80000000u, 0xffffffffu, 0x7fffffffu})
		{
			auto bad = fixture;
			SetWord(bad, offset, impossible);
			Reject(std::move(bad), impossible > 0x7fffffffu ? Code::NegativeCount : Code::CountLimit);
		}
	auto duplicate = fixture;
	SetWord(duplicate, 16, 99);
	Reject(std::move(duplicate), Code::DuplicateScene);
	auto oversizedCount = fixture;
	SetWord(oversizedCount, 28, 1000);
	Reject(std::move(oversizedCount), Code::Truncated);

	for (const auto count : {1u, 2u, 3u, 4u, 65u})
	{
		auto padded = fixture;
		padded.resize(padded.size() + count, 0);
		auto result = Decode(padded);
		Check(result && result->ParsedBytes() == fixture.size() && result->SourceBytes() == padded.size(), "odd/even zero padding admitted");
		padded.back() = 1;
		Reject(std::move(padded), Code::NonzeroPadding);
	}
	for (std::size_t i = fixture.size(); i < fixture.size() + 17; ++i)
	{
		auto padded = fixture;
		padded.resize(fixture.size() + 17, 0);
		padded[i] = 0x80;
		Reject(std::move(padded), Code::NonzeroPadding);
	}
	auto empty = Decode({0, 0, 0, 0});
	Check(empty && empty->ObjectCount() == 0 && empty->ParsedBytes() == 4, "empty database accepted");
	Bytes unequal;
	for (auto word : {1u, 1u, 0u, 0u, 1u}) Word(unequal, word);
	Component(unequal, 0xffffffffu, {1, 2, 3, 4}, {5, 6, 7, 8, 9, 10, 11, 12});
	auto uneven = Decode(unequal);
	Check(uneven && !uneven->Lookup(0, 0, 0, 0xffffffffu, 1, second, error) && error.code == Code::VertexCountMismatch,
		"unequal nonempty streams decode but never admit");

	BakedVertexColorLimits limits;
	limits.maxSourceBytes = fixture.size() - 1;
	Reject(fixture, Code::SourceLimit, limits);
	limits.maxSourceBytes = fixture.size();
	Decode(fixture, limits);
	auto capacity = fixture;
	capacity.reserve(fixture.size() + 1);
	Reject(std::move(capacity), Code::SourceLimit, limits);
	limits = {}; limits.maxMetadataBytes = 0;
	Reject(fixture, Code::MetadataLimit, limits);
	auto metadata = Decode(fixture);
	if (metadata)
	{
		limits.maxMetadataBytes = metadata->MetadataBytes(); Decode(fixture, limits);
		--limits.maxMetadataBytes; Reject(fixture, Code::MetadataLimit, limits);
	}
	for (unsigned field = 0; field != 8; ++field)
	{
		limits = {};
		switch (field)
		{
		case 0: limits.maxObjects = 2; break;
		case 1: limits.maxScenesPerObject = 1; break;
		case 2: limits.maxScenes = 2; break;
		case 3: limits.maxComponentsPerScene = 1; break;
		case 4: limits.maxComponents = 3; break;
		case 5: limits.maxColorsPerStream = 1; break;
		case 6: limits.maxColors = 8; break;
		case 7: limits.maxHashBuckets = 16; break;
		}
		Reject(fixture, Code::CountLimit, limits);
	}
	limits = {}; limits.maxObjects = 3; limits.maxScenesPerObject = 2; limits.maxScenes = 3;
	limits.maxComponentsPerScene = 2; limits.maxComponents = 4; limits.maxColorsPerStream = 2;
	limits.maxColors = 9; limits.maxHashBuckets = 17;
	Decode(fixture, limits);
	limits.maxObjects = std::numeric_limits<std::size_t>::max();
	Bytes beyondHardLimit; Word(beyondHardLimit, 65537);
	Reject(std::move(beyondHardLimit), Code::CountLimit, limits);
	for (const auto boundary : std::array<std::array<std::uint32_t, 2>, 5>{{
		{{4, 257}}, {{8, 1048577}}, {{20, 4097}}, {{28, 4194305}}, {{40, 4194305}}}})
	{
		limits = {};
		limits.maxScenesPerObject = limits.maxHashBuckets = limits.maxComponentsPerScene =
			limits.maxColorsPerStream = std::numeric_limits<std::size_t>::max();
		auto bad = fixture;
		SetWord(bad, boundary[0], boundary[1]);
		Reject(std::move(bad), Code::CountLimit, limits);
	}
	Bytes oversized(64u * 1024u * 1024u + 1, 0);
	limits = {}; limits.maxSourceBytes = std::numeric_limits<std::size_t>::max();
	Reject(std::move(oversized), Code::SourceLimit, limits);

	Bytes nonadjacent;
	for (auto word : {1u, 3u, 17u, 99u, 1u, 99u, 0u, 0u, 0u}) Word(nonadjacent, word);
	Reject(std::move(nonadjacent), Code::DuplicateScene);
	Bytes highKeys;
	for (auto word : {1u, 2u, 17u, 0xffffffffu, 0x80000000u, 1u}) Word(highKeys, word);
	Component(highKeys, 0xffffffffu, {1, 2, 3, 4}, {5, 6, 7, 8});
	Word(highKeys, 0);
	auto highMap = Decode(std::move(highKeys));
	Check(highMap && highMap->Lookup(0, 0xffffffffu, 0, 0xffffffffu, 1, second, error),
		"hash and CRC are unsigned words, not signed counts");
	Check(highMap && !highMap->Lookup(0, 0x80000000u, 0, 0, 1, second, error) && error.code == Code::MissingComponent,
		"empty scene is valid metadata but admits no component");
	Bytes zeroCRC;
	for (auto word : {1u, 1u, 17u, 0u, 1u}) Word(zeroCRC, word);
	Component(zeroCRC, 0, {1, 2, 3, 4}, {5, 6, 7, 8});
	auto zeroMap = Decode(std::move(zeroCRC));
	Check(zeroMap && zeroMap->Lookup(0, 0, 0, 0, 1, second, error), "zero hash/CRC values are not missing sentinels");

	// Restore field mutations before another decode, with old admitted views retained.
	auto oldMap = Decode(fixture);
	BakedVertexColorView retained;
	if (oldMap) oldMap->Lookup(0, 99, 0, 42, 2, retained, error);
	auto invalid = fixture; invalid.pop_back();
	Check(!BakedVertexColors::Decode(std::move(invalid), oldMap, error) && !oldMap, "invalid map reload discards published map");
	Check(retained.Valid() && retained.DayData()[0] == 1, "failed reload does not invalidate already owned views");

	// Structured random mutations exercise validation without assuming every mutation is invalid.
	std::uint32_t state = 0x169b1523;
	for (unsigned i = 0; i != 4096; ++i)
	{
		auto bytes = fixture;
		state = state * 1664525u + 1013904223u;
		bytes[state % bytes.size()] ^= static_cast<std::uint8_t>((state >> 24) | 1u);
		BakedVertexColors::Ptr output;
		const bool ok = BakedVertexColors::Decode(std::move(bytes), output, error);
		Check(ok ? (output && error.code == Code::None) : (!output && error.code != Code::None), "mutation transactional outcome");
	}
}

/** Fail every allocation in turn and prove malformed preflight never allocates. */
void AllocationTests()
{
#if defined(BAKED_COLORS_TEST_ALLOCATIONS)
	bool succeeded = false;
	std::size_t failedPoints = 0;
	for (std::size_t failAfter = 0; failAfter != 32; ++failAfter)
	{
		auto source = Fixture();
		auto output = Decode(Fixture());
		BakedVertexColorError error;
		AllocationProbe::calls = 0; AllocationProbe::failAfter = failAfter; AllocationProbe::enabled = true;
		const bool ok = BakedVertexColors::Decode(std::move(source), output, error);
		AllocationProbe::enabled = false;
		if (ok)
		{
			Check(output && error.code == Code::None, "success after all allocation failure points");
			succeeded = true;
			break;
		}
		++failedPoints;
		Check(!output && error.code == Code::AllocationFailure, "every allocation failure is transactional");
	}
	Check(succeeded && failedPoints >= 7, "all index/source/result ownership allocations exercised");
	std::cout << "Allocation failure points: " << failedPoints << '\n';
	for (const auto duplicate : {false, true})
	{
		auto source = Fixture();
		if (duplicate) SetWord(source, 16, 99); else source.pop_back();
		BakedVertexColors::Ptr output;
		BakedVertexColorError error;
		AllocationProbe::calls = 0; AllocationProbe::failAfter = 0; AllocationProbe::enabled = true;
		const bool ok = BakedVertexColors::Decode(std::move(source), output, error);
		AllocationProbe::enabled = false;
		Check(!ok && !output && AllocationProbe::calls == 0 &&
			error.code == (duplicate ? Code::DuplicateScene : Code::Truncated), "malformed input rejected before any allocation");
	}
#endif
}

void RealAsset(const char* path)
{
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	Check(static_cast<bool>(stream), "original colour asset opened");
	if (!stream) return;
	const auto size = stream.tellg();
	Check(size == 30728192, "original asset byte size");
	if (size != 30728192) return;
	Bytes bytes(static_cast<std::size_t>(size));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Check(static_cast<bool>(stream), "original asset fully read");
	const auto* originalAddress = bytes.data();
	auto map = Decode(std::move(bytes));
	if (!map) return;
	Check(map->ObjectCount() == 7502 && map->ParsedBytes() == 7799560 && map->SourceBytes() == 30728192,
		"shipped object count/parsed bytes/padding");
	BakedVertexColorError error;
	BakedVertexColorView view;
	Check(map->Lookup(378, 172962304, 0, 3664469006u, 1950, view, error), "original Rough01N object 378 CRC/count admitted");
	if (!view.Valid()) return;
	Check(view.DayOffset() == 1727348 && view.NightOffset() == 1735152 && view.ColorCount() == 1950 && view.ByteCount() == 7800,
		"known original Rough01N stream offsets");
	Check(view.DayData() == originalAddress + 1727348 && view.NightData() == originalAddress + 1735152,
		"ownership transfer does not copy colour asset");
	Check(std::memcmp(view.DayData(), view.NightData(), view.ByteCount()) != 0, "original day/night bytes differ");
	const std::array<std::size_t, 22> objects{{378, 1293, 1412, 1425, 2465, 2509, 2558, 2686, 3625, 4065, 4136,
		4230, 5058, 5100, 5149, 5273, 6184, 6463, 6490, 6783, 7071, 7221}};
	std::set<std::size_t> dayOffsets, nightOffsets;
	std::size_t matched = 0;
	for (std::size_t object = 0; object < map->ObjectCount(); ++object)
	{
		BakedVertexColorView pair;
		if (!map->Lookup(object, 172962304, 0, 3664469006u, 1950, pair, error)) continue;
		++matched;
		Check(std::find(objects.begin(), objects.end(), object) != objects.end(), "original matched placement identity");
		Check(dayOffsets.insert(pair.DayOffset()).second && nightOffsets.insert(pair.NightOffset()).second,
			"22 placements keep separate arrays despite identical geometry CRC");
		Check(pair.NightOffset() == pair.DayOffset() + pair.ByteCount() + 4, "original paired vector layout");
	}
	Check(matched == 22, "exactly 22 original Rough01N placements");
	BakedVertexColorView other;
	Check(map->Lookup(1293, 172962304, 0, 3664469006u, 1950, other, error) &&
		std::memcmp(view.DayData(), other.DayData(), view.ByteCount()) != 0, "original same-geometry placements differ in colour content");
	Check(!map->Lookup(378, 172962304, 0, 3664469007u, 1950, other, error) && error.code == Code::GeometryMismatch && !other.Valid(),
		"real CRC mismatch clears old pair");
	Check(!map->Lookup(378, 172962304, 0, 3664469006u, 1949, other, error) && error.code == Code::VertexCountMismatch,
		"real vertex count mismatch rejected");
	for (const auto length : {1727347u, 1727348u, 1735147u, 1735148u, 1735151u, 1735152u, 1742951u, 7799559u})
	{
		Bytes truncated(originalAddress, originalAddress + length);
		BakedVertexColors::Ptr output = map;
		Check(!BakedVertexColors::Decode(std::move(truncated), output, error) && !output && error.code == Code::Truncated,
			"original asset truncation inside metadata/day/night or final record rejected");
	}
	for (const auto padding : {0u, 1u, 3u})
	{
		Bytes compact(originalAddress, originalAddress + map->ParsedBytes() + padding);
		auto output = Decode(std::move(compact));
		Check(output && output->ParsedBytes() == 7799560 && output->SourceBytes() == 7799560 + padding &&
			output->Lookup(378, 172962304, 0, 3664469006u, 1950, other, error),
			"original asset remains valid without padding or with a partial zero trailer");
	}
	Bytes corruptPadding(originalAddress, originalAddress + map->SourceBytes());
	corruptPadding.back() = 1;
	BakedVertexColors::Ptr rejected = map;
	Check(!BakedVertexColors::Decode(std::move(corruptPadding), rejected, error) && !rejected &&
		error.code == Code::NonzeroPadding && error.offset == 30728191,
		"original final padding byte is validated with exact diagnostic offset");
	std::cout << "Original asset: objects=" << map->ObjectCount() << " scenes=" << map->SceneCount()
		<< " components=" << map->ComponentCount() << " colors=" << map->ColorCount()
		<< " parsed=" << map->ParsedBytes() << " source=" << map->SourceBytes()
		<< " metadata=" << map->MetadataBytes() << " Rough01N placements=" << matched << '\n';
	const auto first = view.DayData()[0];
	map.reset();
	Check(view.DayData()[0] == first && view.NightData() != nullptr, "original admitted colours survive map release");
}
}

int main(int argc, char** argv)
{
	MockTests();
	AllocationTests();
	if (argc == 2) RealAsset(argv[1]);
	else if (argc != 1) { std::cerr << "Usage: baked_vertex_colors_tests [original.vtxcolor]\n"; return 2; }
	std::cout << "Baked vertex colors: " << checks << " checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
