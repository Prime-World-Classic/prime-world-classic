#include "stdafx.h"
#include "LinuxDdsPreview.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "../System/ImageDDS.h"
#include "../System/Stream.h"
#include "../System/2Darray.h"
#include "../System/ImageFormatDDS.h"
#include "../System/ImageInternal.h"
#include <cstring>
#include <new>
namespace Render {
	namespace LinuxDdsPreview {
		bool Load(Stream *source, Image &output) {
			output = {};
			if (!source || !source->IsOk() || source->GetSize() < 128 || source->GetSize() > 64 * 1024 * 1024 + 128)
				return false;
			SDDSFileHeader file;
			static_assert(sizeof(file) == 128, "Original DDS header layout");
			source->Seek(0, SEEKORIGIN_BEGIN);
			if (source->Read(&file, sizeof(file)) != sizeof(file))
				return false;
			const auto &h = file.header;
			if (file.dwSignature != SDDSFileHeader::SIGNATURE || h.dwSize != 124 || h.ddspf.dwSize != 32 ||
				(h.dwHeaderFlags & DDS_HEADER_FLAGS_TEXTURE) != DDS_HEADER_FLAGS_TEXTURE ||
				!(h.dwSurfaceFlags & DDS_SURFACE_FLAGS_TEXTURE) || h.dwCubemapFlags ||
				(h.dwHeaderFlags & DDS_HEADER_FLAGS_VOLUME) || h.dwDepth > 1 || !h.dwWidth || !h.dwHeight ||
				h.dwWidth > 4096 || h.dwHeight > 4096)
				return false;
			try {
				if ((h.ddspf.dwFlags & DDS_FOURCC) &&
					(h.ddspf.dwFourCC == DDSPF_DXT1.dwFourCC || h.ddspf.dwFourCC == DDSPF_DXT3.dwFourCC ||
					 h.ddspf.dwFourCC == DDSPF_DXT5.dwFourCC)) {
					if (source->GetSize() > 64 * 1024 * 1024)
						return false;
					std::vector<unsigned char> bytes(size_t(source->GetSize()));
					source->Seek(0, SEEKORIGIN_BEGIN);
					if (source->Read(bytes.data(), bytes.size()) != int(bytes.size()))
						return false;
					return LinuxBootstrap::DecodeDdsBasePixels(bytes.data(), bytes.size(), output) ==
						   LinuxBootstrap::DdsPixelsStatus::Valid;
				}
				// Retain legacy conversion only after bounding the exact bytes it will consume.
				size_t required = 0;
				if (h.ddspf.dwFlags & DDS_FOURCC) {
					if ((h.ddspf.dwFourCC != DDSPF_DXT2.dwFourCC && h.ddspf.dwFourCC != DDSPF_DXT4.dwFourCC) ||
						h.dwWidth % 4 || h.dwHeight % 4)
						return false;
					required = size_t(h.dwWidth) * h.dwHeight;
				} else {
					if ((h.ddspf.dwFlags & DDS_RGB) != DDS_RGB ||
						(h.ddspf.dwRGBBitCount != 16 && h.ddspf.dwRGBBitCount != 24 && h.ddspf.dwRGBBitCount != 32))
						return false;
					const size_t row = size_t(h.dwWidth) * (h.ddspf.dwRGBBitCount / 8);
					if ((h.dwHeaderFlags & DDS_HEADER_FLAGS_PITCH) && h.dwPitchOrLinearSize != row)
						return false;
					required = row * h.dwHeight;
					if (h.ddspf.dwRGBBitCount == 16) {
						const auto &p = h.ddspf;
						SPixelConvertInfo masks(p.dwABitMask, p.dwRBitMask, p.dwGBitMask, p.dwBBitMask);
						if (masks.dwABits > 8 || masks.dwRBits > 8 || masks.dwGBits > 8 || masks.dwBBits > 8 ||
							(p.dwABitMask | p.dwRBitMask | p.dwGBitMask | p.dwBBitMask) > 65535u)
							return false;
					}
				}
				if (required > size_t(source->GetSize()) - 128)
					return false;
				source->Seek(0, SEEKORIGIN_BEGIN);
				CArray2D<DWORD> pixels;
				if (!NImage::LoadImageDDS(&pixels, source) || pixels.IsEmpty())
					return false;
				output.width = pixels.GetSizeX();
				output.height = pixels.GetSizeY();
				output.rgba.resize(size_t(output.width) * output.height * 4);
				for (unsigned y = 0; y < output.height; ++y)
					for (unsigned x = 0; x < output.width; ++x) {
						const DWORD pixel = pixels[y][x];
						const size_t at = (size_t(y) * output.width + x) * 4;
						output.rgba[at] = static_cast<unsigned char>(pixel >> 16);
						output.rgba[at + 1] = static_cast<unsigned char>(pixel >> 8);
						output.rgba[at + 2] = static_cast<unsigned char>(pixel);
						output.rgba[at + 3] = static_cast<unsigned char>(pixel >> 24);
					}
				return !output.rgba.empty();
			} catch (const std::bad_alloc &) {
				output = {};
				return false;
			}
		}
	} // namespace LinuxDdsPreview
} // namespace Render
#endif
