#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace LinuxBootstrap {

/** Original TerrainLayerManager ascending-ID selection, before material resolution.
 * ID0 (holes) remains in the list, ID1 means local material/UV, and IDs2+ use
 * the compact valid global-material registry. Only the first four IDs render.
 */
struct TerrainLayerSelection {
	std::array<std::uint8_t,4> ids{};
	std::size_t count = 0, total = 0;
	bool LocalUV() const noexcept { return count && ids[0]==1; }
};

/** Dimensions of a raw 16-texel-per-element mask, capped at 65,536 elements/16MiB.
 * Returns zero for empty, overflowing or unsupported dimensions; does not allocate.
 */
std::size_t TerrainLayerMaskBytes(std::size_t columns, std::size_t rows) noexcept;

/** Select the original inclusive one-texel border footprint around a grid cell.
 * Descriptor order is not grid order: the caller supplies its authored cell.
 * Validates the exact borrowed byte extent and clears output on every failure.
 * Frequency sorting, weight generation and invalid-material filtering are not performed.
 */
bool SelectTerrainLayers(const std::uint8_t* mask, std::size_t bytes,
	std::size_t columns, std::size_t rows, std::size_t column, std::size_t row,
	TerrainLayerSelection& output) noexcept;

}
