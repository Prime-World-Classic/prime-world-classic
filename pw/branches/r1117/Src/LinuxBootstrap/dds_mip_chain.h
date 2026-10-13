#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace LinuxBootstrap {
enum class DdsMipStatus { Valid, Unsupported, Invalid };
enum class DdsBlockFormat { Bc1, Bc2, Bc3 };

/** Borrowed subresource extent; no decoding or allocation is performed. */
struct DdsMipLevel {
	std::uint32_t width=0, height=0;
	std::size_t offset=0, bytes=0;
};
/** Original 2D DXT1/3/5 mip chain, capped at 4096 per axis and 64MiB source.
 * Partial chains are valid; absent mip count means one level. Other formats
 * remain unsupported, never reinterpreted as block-compressed data.
 */
struct DdsMipChain {
	DdsBlockFormat format=DdsBlockFormat::Bc1;
	std::array<DdsMipLevel,13> levels{};
	std::size_t count=0, rgbaBytes=0;
};
/** Validate every authored level and exact source extent using the original DDS layout.
 * Clear output on failure. Cubes, volumes and extended headers are not admitted.
 * rgbaBytes conservatively accounts for all decoded levels before GPU admission.
 */
DdsMipStatus InspectDdsMipChain(const void* data, std::size_t bytes, DdsMipChain& output) noexcept;
}
