#include "baked_terrain.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace LinuxBootstrap;
using Bytes = std::vector<std::uint8_t>;
unsigned checks = 0, failures = 0;
void Check(bool value, const char* message)
{
	++checks;
	if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
void Put(Bytes& bytes, std::size_t at, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void PutFloat(Bytes& bytes, std::size_t at, float value)
{
	std::uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	Put(bytes, at, bits);
}

/** Three world-space vertices, one visible and one hidden triangle, original H6 padding. */
Bytes Fixture()
{
	Bytes bytes(384, 0);
	Put(bytes, 0, 0x14554);
	const std::uint32_t header[] = {0x4c455458, 1, 1, 132, 24, 224, 356, 0, 0, 1, 12};
	for (std::size_t i = 0; i < 11; ++i) Put(bytes, 4 + i * 4, header[i]);
	const std::uint32_t fragment[] = {44, 0, 0, 600, 0, 1, 0, 0, 6, 16};
	for (std::size_t i = 0; i < 10; ++i) Put(bytes, 52 + i * 4, fragment[i]);
	const std::uint32_t declarations[6][5] = {
		{0, 0, 2, 0, 0}, {0, 12, 2, 3, 0}, {0, 24, 1, 5, 0},
		{0, 32, 4, 10, 0}, {0, 36, 4, 10, 1}, {0, 40, 4, 10, 2}};
	for (std::size_t i = 0; i < 6; ++i) for (std::size_t j = 0; j < 5; ++j)
		Put(bytes, 100 + i * 20 + j * 4, declarations[i][j]);
	for (unsigned i = 0; i < 3; ++i) {
		const std::size_t v = 228 + i * 44;
		PutFloat(bytes, v, 380.f + static_cast<float>(i));
		PutFloat(bytes, v + 4, 160.f + static_cast<float>(i));
		PutFloat(bytes, v + 8, 6.f + static_cast<float>(i));
		PutFloat(bytes, v + 20, 1);
		PutFloat(bytes, v + 24, .5f); PutFloat(bytes, v + 28, .75f);
		Put(bytes, v + 32, 0x80402010); Put(bytes, v + 36, 0x10203040); Put(bytes, v + 40, 0x000000ff);
	}
	for (unsigned i = 0; i < 6; ++i) Put(bytes, 360 + i * 4, i % 3);
	return bytes;
}

void Mocks()
{
	BakedTerrain::Ptr output;
	BakedTerrainError error;
	auto bytes = Fixture();
	Check(BakedTerrain::Decode(std::move(bytes), 1, output, error), "original mock schema admitted");
	Check(output && error.code == BakedTerrainError::Code::None, "success publishes immutable owner");
	if (!output) return;
	Check(output->TileCount() == 1 && output->VertexCount() == 3 && output->TriangleCount() == 1,
		"count actual VB vertices rather than legacy dip metadata");
	const auto* tile = output->GetTile(0);
	Check(tile && tile->indexCount == 6 && tile->minimum == std::array<float, 3>{{380, 160, 6}} &&
		tile->maximum == std::array<float, 3>{{382, 162, 8}}, "world bounds and hidden indices retained");
	Check(!output->GetTile(1) && !output->VertexData(1) && !output->IndexData(1), "out-of-range access is empty");
	Check(output->VertexData(0)[32] == 0x10 && output->VertexData(0)[35] == 0x80 &&
		output->VertexData(0)[40] == 0xff, "preserve original BGRA lighting and mask bytes");
	const auto held = output;
	const auto reject = [&](Bytes input, std::size_t expected = 1, const BakedTerrainLimits& limits = {}) {
		output = held;
		Check(!BakedTerrain::Decode(std::move(input), expected, output, error, limits) && !output &&
			error.code != BakedTerrainError::Code::None, "malformed decode clears stale owner and reports failure");
	};
	const auto fixture = Fixture();
	for (std::size_t length = 0; length < fixture.size(); ++length)
		reject(Bytes(fixture.begin(), fixture.begin() + static_cast<std::ptrdiff_t>(length)));
	for (std::size_t field : {0u, 4u, 8u, 12u, 16u, 20u, 24u, 28u, 32u, 36u, 40u, 44u,
		52u, 56u, 60u, 68u, 72u, 76u, 80u, 84u, 88u}) {
		auto corrupt = fixture; Put(corrupt, field, 0xffffffffu); reject(std::move(corrupt));
	}
	for (std::size_t field = 100; field < 220; field += 4) {
		auto corrupt = fixture; Put(corrupt, field, 0xffffffffu); reject(std::move(corrupt));
	}
	for (std::size_t index = 0; index < 6; ++index) {
		auto corrupt = fixture; Put(corrupt, 360 + index * 4, 3); reject(std::move(corrupt));
	}
	for (std::size_t field = 0; field < 8; ++field) for (float value : {
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()}) {
		auto corrupt = fixture; PutFloat(corrupt, 228 + field * 4, value); reject(std::move(corrupt));
	}
	for (std::size_t field = 0; field < 3; ++field) {
		auto corrupt = fixture; PutFloat(corrupt, 228 + field * 4, 1000001.f); reject(std::move(corrupt));
	}
	auto trailing = fixture; trailing.push_back(0); reject(std::move(trailing));
	reject(fixture, 0); reject(fixture, 2); reject(fixture, std::numeric_limits<std::size_t>::max());
	for (unsigned limit = 0; limit < 4; ++limit) {
		BakedTerrainLimits limits;
		if (limit == 0) limits.sourceBytes = fixture.size() - 1;
		if (limit == 1) limits.tiles = 0;
		if (limit == 2) limits.vertices = 2;
		if (limit == 3) limits.indices = 5;
		reject(fixture, 1, limits);
	}
	auto capacity = fixture; capacity.reserve(4096);
	BakedTerrainLimits capacityLimit; capacityLimit.sourceBytes = 1024;
	reject(std::move(capacity), 1, capacityLimit);
	auto two = fixture;
	two.insert(two.end(), fixture.begin() + 4, fixture.end());
	Check(BakedTerrain::Decode(std::move(two), 2, output, error) && output->TileCount() == 2 &&
		output->VertexCount() == 6 && output->GetTile(1)->minimum == held->GetTile(0)->minimum,
		"independent concatenated records retain descriptor ordering");
	std::uint32_t random = 1117;
	for (unsigned i = 0; i < 2000; ++i) {
		auto mutation = fixture;
		random = random * 1664525u + 1013904223u;
		mutation[random % mutation.size()] ^= static_cast<std::uint8_t>(random >> 24);
		const bool valid = BakedTerrain::Decode(std::move(mutation), 1, output, error);
		Check(valid ? output && error.code == BakedTerrainError::Code::None :
			!output && error.code != BakedTerrainError::Code::None, "mutation never publishes a partial owner");
	}
	Check(held->VertexCount() == 3 && held->VertexData(0)[32] == 0x10, "failed reloads preserve other shared owners");
}

void Original(const char* path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	const auto size = file.tellg();
	if (!file || size != 14254504) throw std::runtime_error("missing/wrong shipped MOBA fixture");
	Bytes bytes(static_cast<std::size_t>(size)); file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("short fixture read");
	BakedTerrain::Ptr output;
	BakedTerrainError error;
	Check(BakedTerrain::Decode(std::move(bytes), 1681, output, error), "complete shipped map admits");
	if (!output) { std::cerr << "error=" << int(error.code) << " offset=" << error.offset << '\n'; return; }
	Check(output->VertexCount() == 214067 && output->TriangleCount() == 272262, "original vertex/visible triangle totals");
	const auto* tile = output->GetTile(1187);
	Check(tile && tile->minimum[0] == 380 && tile->maximum[0] == 390 &&
		tile->minimum[2] > 6.43f && tile->maximum[2] > 6.93f, "original hero-spawn tile is not flat");
	for (std::size_t i = 0; i < output->TileCount(); ++i) {
		const auto* item = output->GetTile(i);
		Check(item && item->vertexCount && item->indexCount >= item->triangleCount * 3 &&
			item->vertexOffset < output->SourceBytes() && item->indexOffset < output->SourceBytes(), "every original tile bounded");
	}
}
}

int main(int argc, char** argv)
{
	try { if (argc == 2) Original(argv[1]); else if (argc == 1) Mocks(); else return 2; }
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
	std::cout << "Baked terrain: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
