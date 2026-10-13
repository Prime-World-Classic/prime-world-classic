#include "dds_mip_chain.h"
#include <algorithm>
#include <cstring>

// Reuse the original fixed-width DDS structures without importing Direct3D types.
using DWORD=std::uint32_t;
#define MAKEFOURCC(a,b,c,d) (DWORD(a)|(DWORD(b)<<8)|(DWORD(c)<<16)|(DWORD(d)<<24))
#include "../Render/DDS.h"
#undef MAKEFOURCC

namespace LinuxBootstrap {
namespace {
/** Shared disk-layout checks; base-level and whole-chain extent policies stay separate. */
bool ReadHeader(const void* data, std::size_t bytes, DDS_HEADER& header) noexcept {
	static_assert(sizeof(DDS_HEADER)==124 && sizeof(DDS_PIXELFORMAT)==32,"Original DDS disk layout");
	if (!data || bytes<128 || bytes>64*1024*1024) return false;
	DWORD magic=0;
	std::memcpy(&magic,data,4);
	std::memcpy(&header,static_cast<const unsigned char*>(data)+4,sizeof(header));
	if (magic!=DDS_MAGIC || header.dwSize!=124 || header.ddspf.dwSize!=32 ||
		(header.dwHeaderFlags&DDS_HEADER_FLAGS_TEXTURE)!=DDS_HEADER_FLAGS_TEXTURE ||
		!(header.dwSurfaceFlags&DDS_SURFACE_FLAGS_TEXTURE) || header.dwCubemapFlags ||
		(header.dwHeaderFlags&DDS_HEADER_FLAGS_VOLUME) || header.dwDepth>1 ||
		!header.dwWidth || !header.dwHeight || header.dwWidth>4096 || header.dwHeight>4096)
		return false;
	const auto count=std::max(DWORD(1),header.dwMipMapCount);
	unsigned maximum=1;
	for (auto dimension=std::max(header.dwWidth,header.dwHeight);dimension>1;dimension/=2) ++maximum;
	return count<=maximum;
}
}
DdsMipStatus InspectDdsMipChain(const void* data, std::size_t bytes, DdsMipChain& output) noexcept {
	output={}; DDS_HEADER header{};
	if (!ReadHeader(data,bytes,header)) return DdsMipStatus::Invalid;
	const auto count=std::max(DWORD(1),header.dwMipMapCount);
	if (header.ddspf.dwFlags!=DDS_FOURCC) return DdsMipStatus::Unsupported;
	DdsMipChain chain;
	if (header.ddspf.dwFourCC==DDSPF_DXT1.dwFourCC) chain.format=DdsBlockFormat::Bc1;
	else if (header.ddspf.dwFourCC==DDSPF_DXT3.dwFourCC) chain.format=DdsBlockFormat::Bc2;
	else if (header.ddspf.dwFourCC==DDSPF_DXT5.dwFourCC) chain.format=DdsBlockFormat::Bc3;
	else return DdsMipStatus::Unsupported;
	const size_t block=chain.format==DdsBlockFormat::Bc1?8:16;
	size_t offset=128;
	auto width=header.dwWidth,height=header.dwHeight;
	for (size_t i=0;i<count;++i) {
		const size_t levelBytes=size_t((width+3)/4)*((height+3)/4)*block;
		if (levelBytes>bytes-offset) return DdsMipStatus::Invalid;
		chain.levels[i]={width,height,offset,levelBytes};
		chain.rgbaBytes+=size_t(width)*height*4; offset+=levelBytes;
		width=std::max(DWORD(1),width/2); height=std::max(DWORD(1),height/2);
	}
	if (offset!=bytes) return DdsMipStatus::Invalid;
	chain.count=count; output=chain;
	return DdsMipStatus::Valid;
}
DdsMipStatus InspectDdsBaseLevel(const void* data, std::size_t bytes, DdsBaseLevel& output) noexcept {
	output={}; DDS_HEADER header{};
	if (!ReadHeader(data,bytes,header)) return DdsMipStatus::Invalid;
	if (!(header.ddspf.dwFlags&DDS_FOURCC) || (header.ddspf.dwFlags&~DWORD(DDS_FOURCC|1u|0x80000000u)))
		return DdsMipStatus::Unsupported;
	DdsBaseLevel base;
	if (header.ddspf.dwFourCC==DDSPF_DXT1.dwFourCC) base.format=DdsBlockFormat::Bc1;
	else if (header.ddspf.dwFourCC==DDSPF_DXT3.dwFourCC) base.format=DdsBlockFormat::Bc2;
	else if (header.ddspf.dwFourCC==DDSPF_DXT5.dwFourCC) base.format=DdsBlockFormat::Bc3;
	else return DdsMipStatus::Unsupported;
	const size_t block=base.format==DdsBlockFormat::Bc1 ? 8 : 16;
	const size_t extent=size_t((header.dwWidth+3)/4)*((header.dwHeight+3)/4)*block;
	if (extent>bytes-128) return DdsMipStatus::Invalid;
	base.level={header.dwWidth,header.dwHeight,128,extent}; output=base;
	return DdsMipStatus::Valid;
}
}
