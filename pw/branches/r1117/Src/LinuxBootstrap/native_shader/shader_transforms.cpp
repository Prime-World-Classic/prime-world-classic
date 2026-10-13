#include "shader_transforms.hpp"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace pwshader {

bool BuildBasicFxSortDepth(const std::array<float, 16>& world, const std::array<float, 16>& view,
	const PreviewTransform& preview, const std::array<float, 3>& minimum,
	const std::array<float, 3>& maximum, float& output) noexcept
{
	for (const auto* matrix : {&world, &view})
		for (float value : *matrix) if (!std::isfinite(value)) return false;
	for (float value : {preview.centerX, preview.centerY, preview.minZ, preview.scale})
		if (!std::isfinite(value)) return false;
	if (preview.scale <= 0 || world[12] != 0 || world[13] != 0 || world[14] != 0 || world[15] != 1 ||
		view[3] != 0 || view[7] != 0 || view[11] != 0 || view[15] != 1) return false;
	std::array<double, 4> center{0,0,0,1}, placed{}, eye{};
	for (size_t i = 0; i < 3; ++i) {
		if (!std::isfinite(minimum[i]) || !std::isfinite(maximum[i]) || minimum[i] > maximum[i]) return false;
		center[i] = (double(minimum[i]) + maximum[i]) * .5;
	}
	const auto representable = [](const std::array<double, 4>& point) {
		for (double value : point)
			if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
		return true;
	};
	for (size_t row = 0; row < 4; ++row)
		for (size_t column = 0; column < 4; ++column)
			placed[row] += double(world[row * 4 + column]) * center[column];
	if (!representable(placed)) return false;
	const std::array<double, 4> displayed{
		(placed[0] - preview.centerX) * preview.scale,
		double(.04f) + (placed[2] - preview.minZ) * preview.scale,
		(placed[1] - preview.centerY) * preview.scale, 1};
	if (!representable(displayed)) return false;
	for (size_t row = 0; row < 4; ++row)
		for (size_t column = 0; column < 4; ++column)
			eye[row] += double(view[column * 4 + row]) * displayed[column];
	if (!representable(eye)) return false;
	output = static_cast<float>(-eye[2]);
	return true;
}

bool BuildBasicFxTransformRegisters(
	const std::array<float, 16>& worldRowMajor,
	const std::array<float, 16>& modelViewColumnMajor,
	const std::array<float, 16>& projectionColumnMajor,
	const PreviewTransform& preview,
	BasicFxTransformRegisters& output) noexcept
{
	for (const auto* matrix : {&worldRowMajor, &modelViewColumnMajor, &projectionColumnMajor}) {
		for (float value : *matrix) {
			if (!std::isfinite(value))
				return false;
		}
	}
	for (float value : {preview.centerX, preview.centerY, preview.minZ, preview.scale}) {
		if (!std::isfinite(value))
			return false;
	}
	if (preview.scale <= 0.0f)
		return false;

	using WideRows = std::array<std::array<double, 4>, 4>;
	const double scale = preview.scale;
	const WideRows previewRows{{
		{{scale, 0, 0, -scale * preview.centerX}},
		{{0, 0, scale, double(0.04f) - scale * preview.minZ}},
		{{0, scale, 0, -scale * preview.centerY}},
		{{0, 0, 0, 1}}
	}};

	// GL matrices act on column vectors: projection * modelView * preview.
	WideRows eye{}, clip{};
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			for (unsigned k = 0; k < 4; ++k)
				eye[row][column] += double(modelViewColumnMajor[k * 4 + row]) * previewRows[k][column];
		}
	}
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			for (unsigned k = 0; k < 4; ++k)
				clip[row][column] += double(projectionColumnMajor[k * 4 + row]) * eye[k][column];
		}
	}
	// Undo Mojo's final depth conversion without altering perspective w.
	for (unsigned column = 0; column < 4; ++column)
		clip[2][column] = 0.5 * (clip[2][column] + clip[3][column]);

	BasicFxTransformRegisters candidate;
	const double maximum = std::numeric_limits<float>::max();
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			const double value = clip[row][column];
			if (!std::isfinite(value) || value < -maximum || value > maximum)
				return false;
			candidate.world[row][column] = worldRowMajor[row * 4 + column];
			candidate.viewProjection[row][column] = static_cast<float>(value);
		}
	}
	output = candidate;
	return true;
}

}
