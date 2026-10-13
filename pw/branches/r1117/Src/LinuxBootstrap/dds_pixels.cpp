#include "dds_pixels.h"
#include "dds_mip_chain.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include "colourblock.h"
#include "alpha.h"

namespace LinuxBootstrap {
	DdsPixelsStatus DecodeDdsBasePixels(const void *data, std::size_t bytes, DdsPixels &output) noexcept {
		output = {};
		DdsBaseLevel base;
		const auto status = InspectDdsBaseLevel(data, bytes, base);
		if (status != DdsMipStatus::Valid)
			return status == DdsMipStatus::Unsupported ? DdsPixelsStatus::Unsupported : DdsPixelsStatus::Invalid;
		try {
			DdsPixels decoded;
			decoded.width = base.level.width;
			decoded.height = base.level.height;
			decoded.rgba.resize(size_t(decoded.width) * decoded.height * 4);
			const size_t blockBytes = base.format == DdsBlockFormat::Bc1 ? 8 : 16;
			const size_t columns = (decoded.width + 3) / 4;
			const auto *source = static_cast<const unsigned char *>(data) + base.level.offset;
			for (unsigned y = 0; y < decoded.height; y += 4)
				for (unsigned x = 0; x < decoded.width; x += 4) {
					const auto *block = source + (size_t(y / 4) * columns + x / 4) * blockBytes;
					std::array<unsigned char, 64> rgba{};
					squish::DecompressColour(rgba.data(), block + (base.format == DdsBlockFormat::Bc1 ? 0 : 8),
											 base.format == DdsBlockFormat::Bc1);
					if (base.format == DdsBlockFormat::Bc2)
						squish::DecompressAlphaDxt3(rgba.data(), block);
					if (base.format == DdsBlockFormat::Bc3)
						squish::DecompressAlphaDxt5(rgba.data(), block);
					for (unsigned row = 0; row < std::min(4u, decoded.height - y); ++row)
						std::memcpy(decoded.rgba.data() + (size_t(y + row) * decoded.width + x) * 4,
									rgba.data() + row * 16, size_t(std::min(4u, decoded.width - x)) * 4);
				}
			output = std::move(decoded);
			return DdsPixelsStatus::Valid;
		} catch (const std::bad_alloc &) {
			return DdsPixelsStatus::AllocationFailure;
		}
	}
} // namespace LinuxBootstrap
