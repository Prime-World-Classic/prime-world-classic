#include "shader_transforms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {
using Matrix = std::array<float, 16>;
using Point = std::array<float, 3>;
using WidePoint = std::array<long double, 4>;
using pwshader::BasicFxTransformRegisters;
using pwshader::BuildBasicFxTransformRegisters;
using pwshader::PreviewTransform;

constexpr Matrix Identity{{
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
}};

struct Checks {
	unsigned count = 0;
	unsigned failures = 0;
	void Test(bool passed, const std::string& name)
	{
		++count;
		if (!passed) {
			++failures;
			std::cout << "FAIL " << name << '\n';
		}
	}
};

bool Near(long double actual, long double expected)
{
	return std::isfinite(actual) && std::isfinite(expected) &&
		std::abs(actual - expected) <= 0.00002L * std::max(1.0L, std::abs(expected));
}

BasicFxTransformRegisters Sentinel()
{
	BasicFxTransformRegisters result;
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			result.world[row][column] = float(101 + row * 4 + column);
			result.viewProjection[row][column] = -float(201 + row * 4 + column);
		}
	}
	return result;
}

// Independent point oracle: no composed matrices or production implementation helpers.
WidePoint GlPoint(const Matrix& columnMajor, const WidePoint& point)
{
	WidePoint result{};
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column)
			result[row] += static_cast<long double>(columnMajor[column * 4 + row]) * point[column];
	}
	return result;
}

WidePoint Oracle(const Matrix& world, const Matrix& modelView, const Matrix& projection,
	const PreviewTransform& preview, const Point& raw)
{
	const WidePoint position{{raw[0], raw[1], raw[2], 1.0L}};
	WidePoint transformed{};
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column)
			transformed[row] += static_cast<long double>(world[row * 4 + column]) * position[column];
	}
	const WidePoint displayed{{
		(transformed[0] - preview.centerX) * preview.scale,
		static_cast<long double>(0.04f) + (transformed[2] - preview.minZ) * preview.scale,
		(transformed[1] - preview.centerY) * preview.scale,
		1.0L
	}};
	return GlPoint(projection, GlPoint(modelView, displayed));
}

// Simulate the selected shader's two row-dot stages with float arithmetic.
std::array<float, 4> ShaderD3D(const BasicFxTransformRegisters& registers, const Point& raw)
{
	const std::array<float, 4> position{{raw[0], raw[1], raw[2], 1.0f}};
	std::array<float, 4> world{}, clip{};
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column)
			world[row] += registers.world[row][column] * position[column];
	}
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column)
			clip[row] += registers.viewProjection[row][column] * world[column];
	}
	return clip;
}

WidePoint ShaderGl(const BasicFxTransformRegisters& registers, const Point& raw)
{
	auto clip = ShaderD3D(registers, raw);
	clip[2] = 2.0f * clip[2] - clip[3];
	return {{clip[0], clip[1], clip[2], clip[3]}};
}

void CompareCase(Checks& checks, const std::string& name, const Matrix& world,
	const Matrix& modelView, const Matrix& projection, const PreviewTransform& preview)
{
	auto output = Sentinel();
	const bool built = BuildBasicFxTransformRegisters(world, modelView, projection, preview, output);
	checks.Test(built, name + " accepted");
	if (!built)
		return;
	for (unsigned row = 0; row < 4; ++row) {
		checks.Test(std::equal(output.world[row].begin(), output.world[row].end(),
			world.begin() + row * 4), name + " exact WORLD row " + std::to_string(row));
	}
	const Point points[] = {{0, 0, 0}, {1, -2, 3}, {-4, 0.5f, 2},
		{2.25f, -1.75f, -3.5f}, {0.125f, 0.25f, -0.5f}, {100, -200, 80}};
	for (unsigned i = 0; i < std::size(points); ++i) {
		const auto expected = Oracle(world, modelView, projection, preview, points[i]);
		const auto d3d = ShaderD3D(output, points[i]);
		const auto actual = ShaderGl(output, points[i]);
		const std::string label = name + " point " + std::to_string(i);
		for (unsigned coordinate = 0; coordinate < 4; ++coordinate) {
			checks.Test(Near(actual[coordinate], expected[coordinate]),
				label + " GL clip " + std::to_string(coordinate));
		}
		checks.Test(Near(d3d[2], 0.5L * (expected[2] + expected[3])), label + " D3D depth");
		if (std::abs(expected[3]) > 0.0001L) {
			checks.Test(Near(actual[2] / actual[3], expected[2] / expected[3]), label + " perspective depth");
		}
	}
}

void Reject(Checks& checks, const std::string& name, const Matrix& world,
	const Matrix& modelView, const Matrix& projection, const PreviewTransform& preview,
	const BasicFxTransformRegisters& seed)
{
	auto output = seed;
	std::array<unsigned char, sizeof(output)> before{};
	std::memcpy(before.data(), &output, sizeof(output));
	checks.Test(!BuildBasicFxTransformRegisters(world, modelView, projection, preview, output), name + " rejected");
	checks.Test(std::memcmp(before.data(), &output, sizeof(output)) == 0, name + " output bytes unchanged");
}

void InvalidCases(Checks& checks)
{
	const float nonfinite[] = {std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
	const auto seed = Sentinel();
	for (unsigned source = 0; source < 3; ++source) {
		for (unsigned index = 0; index < 16; ++index) {
			for (unsigned value = 0; value < std::size(nonfinite); ++value) {
				Matrix matrices[] = {Identity, Identity, Identity};
				matrices[source][index] = nonfinite[value];
				Reject(checks, "nonfinite matrix " + std::to_string(source) + " index " +
					std::to_string(index) + " value " + std::to_string(value),
					matrices[0], matrices[1], matrices[2], {}, seed);
			}
		}
	}
	for (unsigned index = 0; index < 4; ++index) {
		for (unsigned value = 0; value < std::size(nonfinite); ++value) {
			PreviewTransform preview;
			float* fields[] = {&preview.centerX, &preview.centerY, &preview.minZ, &preview.scale};
			*fields[index] = nonfinite[value];
			Reject(checks, "nonfinite preview " + std::to_string(index) + " value " + std::to_string(value),
				Identity, Identity, Identity, preview, seed);
		}
	}
	for (float scale : {0.0f, -0.0f, -1.0f, -std::numeric_limits<float>::denorm_min()})
		Reject(checks, "nonpositive scale", Identity, Identity, Identity, {0, 0, 0, scale}, seed);

	const float maximum = std::numeric_limits<float>::max();
	Matrix modelView = Identity, projection = Identity;
	modelView[0] = maximum;
	projection[0] = 2.0f;
	Reject(checks, "positive coefficient overflow", Identity, modelView, projection, {}, seed);
	projection[0] = -2.0f;
	Reject(checks, "negative coefficient overflow", Identity, modelView, projection, {}, seed);
	Reject(checks, "preview translation overflow", Identity, Identity, Identity, {maximum, 0, 0, 2}, seed);
	modelView = projection = Identity;
	modelView[15] = maximum;
	projection[15] = 2.0f;
	Reject(checks, "last VP coefficient overflow", Identity, modelView, projection, {}, seed);

	auto valid = seed;
	const bool built = BuildBasicFxTransformRegisters(Identity, Identity, Identity, {3, -4, 2, 0.5f}, valid);
	checks.Test(built, "valid output before failed replacement");
	if (built) {
		Reject(checks, "failed replacement preserves valid registers", Identity, modelView, projection, {}, valid);
		checks.Test(BuildBasicFxTransformRegisters(Identity, Identity, Identity, {}, valid), "recovery after rejected conversion");
	}
}

long double BasisDeterminant(const BasicFxTransformRegisters& registers)
{
	const auto origin = ShaderGl(registers, {0, 0, 0});
	auto x = ShaderGl(registers, {1, 0, 0});
	auto y = ShaderGl(registers, {0, 1, 0});
	auto z = ShaderGl(registers, {0, 0, 1});
	for (unsigned coordinate = 0; coordinate < 3; ++coordinate) {
		x[coordinate] -= origin[coordinate];
		y[coordinate] -= origin[coordinate];
		z[coordinate] -= origin[coordinate];
	}
	return x[0] * (y[1] * z[2] - y[2] * z[1]) -
		x[1] * (y[0] * z[2] - y[2] * z[0]) + x[2] * (y[0] * z[1] - y[1] * z[0]);
}

void BoundaryCases(Checks& checks)
{
	BasicFxTransformRegisters output;
	bool built = BuildBasicFxTransformRegisters(Identity, Identity, Identity, {}, output);
	checks.Test(built, "identity register fixture accepted");
	if (built) {
		const std::array<std::array<float, 4>, 4> expected{{
			{{1, 0, 0, 0}}, {{0, 0, 1, 0.04f}}, {{0, 0.5f, 0, 0.5f}}, {{0, 0, 0, 1}}
		}};
		checks.Test(output.viewProjection == expected, "exact identity preview and D3D rows");
		for (float glDepth : {-1.5f, -1.0f, 0.0f, 1.0f, 1.5f}) {
			const auto d3d = ShaderD3D(output, {0, glDepth, 0});
			checks.Test(d3d[2] == 0.5f * (glDepth + 1.0f), "near/far D3D clip depth including out-of-range values");
			checks.Test(ShaderGl(output, {0, glDepth, 0})[2] == glDepth, "near/far exact Mojo depth roundtrip");
		}
	}

	const float maximum = std::numeric_limits<float>::max();
	Matrix world = Identity;
	world[3] = maximum;
	built = BuildBasicFxTransformRegisters(world, Identity, Identity, {}, output);
	checks.Test(built, "finite maximum WORLD accepted without coupling into VP");
	if (built)
		checks.Test(output.world[0][3] == maximum, "maximum WORLD retained exactly");
	Matrix projection = Identity;
	projection[0] = maximum;
	built = BuildBasicFxTransformRegisters(Identity, Identity, projection, {}, output);
	checks.Test(built, "finite maximum VP accepted");
	if (built)
		checks.Test(output.viewProjection[0][0] == maximum, "maximum VP retained exactly");
	projection = Identity;
	projection[2] = projection[3] = maximum;
	built = BuildBasicFxTransformRegisters(Identity, Identity, projection, {}, output);
	checks.Test(built, "depth average uses wide intermediate rather than overflowing float sum");
	if (built)
		checks.Test(output.viewProjection[2][0] == maximum, "wide depth average narrows to representable result");

	for (float scale : {0.00001f, 0.125f, 1.0f, 4.0f}) {
		built = BuildBasicFxTransformRegisters(Identity, Identity, Identity, {0, 0, 0, scale}, output);
		checks.Test(built, "positive scale accepted");
		if (built)
			checks.Test(output.viewProjection[0][0] == scale, "positive scale not silently reclamped");
	}

	for (float xSign : {1.0f, -1.0f}) {
		world = Identity;
		world[0] = xSign;
		built = BuildBasicFxTransformRegisters(world, Identity, Identity, {0, 0, 0, 2}, output);
		checks.Test(built, "orientation fixture accepted");
		if (built)
			checks.Test(Near(BasisDeterminant(output), -8.0L * xSign), "Y/Z swap reverses handedness; WORLD reflection can reverse it again");
	}
}

}

int main()
{
	Checks checks;
	CompareCase(checks, "identity", Identity, Identity, Identity, {});
	const Matrix world{{
		1.25f, 0.375f, -0.5f, 2,
		-0.625f, 0.875f, 0.25f, -3,
		0.125f, -0.75f, 1.5f, 4,
		0, 0, 0, 1
	}};
	// Literal GL column-major fixtures, deliberately nonsymmetric and translated.
	const Matrix camera{{
		0.8f, -0.2f, 0.6f, 0,
		0.3f, 1.1f, -0.1f, 0,
		-0.5f, 0.4f, 0.9f, 0,
		1.25f, -2, -7, 1
	}};
	const Matrix perspective{{
		1.25f, 0, 0, 0,
		0, 1.75f, 0, 0,
		0.2f, -0.15f, -1.02f, -1,
		0, 0, -2.02f, 0
	}};
	const Matrix orthographic{{
		0.5f, 0, 0, 0,
		0, 0.75f, 0, 0,
		0, 0, -0.125f, 0,
		-0.25f, 0.5f, -0.75f, 1
	}};
	CompareCase(checks, "WORLD and preview", world, Identity, Identity, {3.5f, -6.25f, 2.125f, 0.375f});
	CompareCase(checks, "camera and orthographic", world, camera, orthographic, {-4, 3, 1, 2.5f});
	CompareCase(checks, "camera and perspective", world, camera, perspective, {3.5f, -6.25f, 2.125f, 0.375f});
	CompareCase(checks, "tiny positive scale", world, camera, perspective, {3.5f, -6.25f, 2.125f, 0.00001f});
	BasicFxTransformRegisters projected;
	const bool built = BuildBasicFxTransformRegisters(world, camera, perspective, {3.5f, -6.25f, 2.125f, 0.375f}, projected);
	checks.Test(built, "perspective w fixture accepted");
	if (built) {
		const auto first = ShaderD3D(projected, {0, 0, 0});
		const auto second = ShaderD3D(projected, {100, -200, 80});
		checks.Test(first[3] != 1.0f && first[3] != second[3], "perspective w remains point-dependent, not forced to one");
	}
	BoundaryCases(checks);
	InvalidCases(checks);
	std::cout << "SUMMARY checks=" << checks.count << " failures=" << checks.failures << '\n';
	return checks.failures ? 1 : 0;
}
