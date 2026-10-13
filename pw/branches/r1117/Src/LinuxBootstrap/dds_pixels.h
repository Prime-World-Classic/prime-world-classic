#pragma once
#include <cstddef>
#include <vector>

namespace LinuxBootstrap {
	enum class DdsPixelsStatus { Valid, Unsupported, Invalid, AllocationFailure };
	/** Straight RGBA8, base image only. No GPU state or colour-space transform is involved. */
	struct DdsPixels {
		unsigned width = 0, height = 0;
		std::vector<unsigned char> rgba;
	};
	/** Decode checked BC1/2/3 blocks through vendored Squish and crop boundary blocks.
	 * Clear output on any failure. Mip tails are not decoded or declared valid here.
	 * At most 4096 pixels per axis, 64 MiB source and 64 MiB output, matching the inspector.
	 */
	DdsPixelsStatus DecodeDdsBasePixels(const void *data, std::size_t bytes, DdsPixels &output) noexcept;
} // namespace LinuxBootstrap
