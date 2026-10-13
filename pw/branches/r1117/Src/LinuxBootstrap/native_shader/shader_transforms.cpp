#include "shader_transforms.hpp"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace pwshader {

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
