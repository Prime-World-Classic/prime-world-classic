#pragma once

#include <array>

namespace pwshader {

/** Values after the caller's native preview-scale clamp; scale must be positive. */
struct PreviewTransform {
	float centerX = 0.0f;
	float centerY = 0.0f;
	float minZ = 0.0f;
	float scale = 1.0f;
};

/** BasicFX pair zero VS c0..3 (WORLD) and c8..11 (ViewProjection), as float4 rows. */
struct BasicFxTransformRegisters {
	std::array<std::array<float, 4>, 4> world{};
	std::array<std::array<float, 4>, 4> viewProjection{};
};

/** Convert native preview transforms without accessing GL or engine state.
 * worldRowMajor is copied unchanged into WORLD; Matrix43 callers supply row 3 as
 * (0, 0, 0, 1). The other arrays use GL column-major storage, not transposed rows.
 * ViewProjection applies preview, model-view, projection, then z = (GLz + GLw)/2.
 * Mojo's subsequent z = 2z - w therefore recovers GL clip depth without dividing
 * by w. Preview uses (x-centerX)*scale, 0.04f+(z-minZ)*scale, (y-centerY)*scale.
 * Composition uses double intermediates and checked float narrowing. Float
 * rounding can differ from the old staged CPU path; point-result overflow is
 * outside this register-only API. No cull mode or other shader registers change.
 * Returns false for nonfinite input, nonpositive scale or unrepresentable output;
 * on failure every byte of output remains unchanged. No scale clamp is applied.
 */
bool BuildBasicFxTransformRegisters(
	const std::array<float, 16>& worldRowMajor,
	const std::array<float, 16>& modelViewColumnMajor,
	const std::array<float, 16>& projectionColumnMajor,
	const PreviewTransform& preview,
	BasicFxTransformRegisters& output) noexcept;

}
