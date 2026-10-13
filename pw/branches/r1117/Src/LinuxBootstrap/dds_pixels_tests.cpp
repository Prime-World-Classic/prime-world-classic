#include "dds_pixels.h"
#include "dds_mip_chain.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace {
	using namespace LinuxBootstrap;
	unsigned checks = 0, failures = 0;
	void Check(bool good, const char *message) {
		++checks;
		if (!good) {
			++failures;
			std::cerr << "FAIL " << message << '\n';
		}
	}
	/** Independent little-endian header and selector packing, without decoder structs. */
	void Word(std::vector<unsigned char> &data, size_t index, std::uint32_t value) {
		for (unsigned i = 0; i < 4; ++i)
			data.at(index * 4 + i) = static_cast<unsigned char>(value >> (i * 8));
	}
	std::vector<unsigned char> Fixture(unsigned width, unsigned height, char format, bool reverse = false) {
		const size_t blockSize = format == '1' ? 8 : 16;
		std::vector<unsigned char> data(128 + size_t((width + 3) / 4) * ((height + 3) / 4) * blockSize, 0);
		Word(data, 0, 0x20534444);
		Word(data, 1, 124);
		Word(data, 2, 0x81007);
		Word(data, 3, height);
		Word(data, 4, width);
		Word(data, 7, 1);
		Word(data, 19, 32);
		Word(data, 20, 4);
		Word(data, 21, 0x00545844 | (unsigned(format) << 24));
		Word(data, 27, 0x1000);
		for (size_t offset = 128; offset < data.size(); offset += blockSize) {
			auto *block = data.data() + offset;
			if (format == '3')
				std::fill(block, block + 8, 255);
			if (format == '5')
				block[0] = 255;
			auto *colour = block + (format == '1' ? 0 : 8);
			colour[0] = reverse ? 0 : 31;
			colour[1] = reverse ? 248 : 0;
			colour[2] = reverse ? 31 : 0;
			colour[3] = reverse ? 0 : 248;
			std::fill(colour + 4, colour + 8, 0xe4);
		}
		return data;
	}
	bool Cleared(const DdsPixels &image) { return !image.width && !image.height && image.rgba.empty(); }
	void Mocks() {
		DdsPixels image;
		for (char format : {'1', '3', '5'})
			for (bool reverse : {false, true})
				for (const auto size : {std::array<unsigned, 2>{4, 4}, {1, 1}, {1, 9}, {7, 3}, {9, 7}}) {
					const auto data = Fixture(size[0], size[1], format, reverse);
					Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid &&
							  image.width == size[0] && image.height == size[1] &&
							  image.rgba.size() == size_t(size[0]) * size[1] * 4,
						  "tiny, odd and multiple-block images have exact output extent");
					std::array<std::array<unsigned char, 4>, 4> expected = {
						{{0, 0, 255, 255}, {255, 0, 0, 255}, {85, 0, 170, 255}, {170, 0, 85, 255}}};
					if (reverse) {
						std::swap(expected[0], expected[1]);
						std::swap(expected[2], expected[3]);
					}
					if (format == '1' && !reverse) {
						expected[2] = {127, 0, 127, 255};
						expected[3] = {0, 0, 0, 0};
					}
					bool same = image.rgba.size() == size_t(size[0]) * size[1] * 4;
					if (same)
						for (unsigned y = 0; y < size[1]; ++y)
							for (unsigned x = 0; x < size[0]; ++x)
								for (unsigned c = 0; c < 4; ++c)
									same = same && image.rgba[(size_t(y) * size[0] + x) * 4 + c] == expected[x % 4][c];
					Check(same, "565 endpoint expansion, BC1 transparency and BC2/3 four-colour mode");
					std::vector<unsigned char> unaligned(1, 0);
					unaligned.insert(unaligned.end(), data.begin(), data.end());
					DdsPixels copied;
					Check(DecodeDdsBasePixels(unaligned.data() + 1, data.size(), copied) == DdsPixelsStatus::Valid &&
							  copied.rgba == image.rgba,
						  "unaligned input decodes without word loads");
				}
		// Distinct block endpoints expose incorrect row strides or copying padded edge pixels.
		{
			auto data = Fixture(5, 5, '1');
			const unsigned short endpoints[] = {0xf800, 0x07e0, 0x001f, 0xffff};
			const std::array<std::array<unsigned char, 4>, 4> colours = {
				{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}}};
			for (unsigned i = 0; i < 4; ++i) {
				auto *block = data.data() + 128 + i * 8;
				std::fill(block, block + 8, 0);
				block[0] = static_cast<unsigned char>(endpoints[i]);
				block[1] = static_cast<unsigned char>(endpoints[i] >> 8);
			}
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid,
				  "four distinct cropped blocks decode");
			bool same = image.rgba.size() == 100;
			if (same)
				for (unsigned y = 0; y < 5; ++y)
					for (unsigned x = 0; x < 5; ++x)
						for (unsigned c = 0; c < 4; ++c)
							same = same && image.rgba[(y * 5 + x) * 4 + c] == colours[(y / 4) * 2 + x / 4][c];
			Check(same, "edge crop retains each block's position");
		}
		{
			auto data = Fixture(4, 4, '3');
			for (unsigned i = 0; i < 8; ++i)
				data[128 + i] = static_cast<unsigned char>((2 * i) | ((2 * i + 1) << 4));
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid,
				  "BC2 alpha block decodes");
			bool same = image.rgba.size() == 64;
			if (same)
				for (unsigned i = 0; i < 16; ++i)
					same = same && image.rgba[i * 4 + 3] == i * 17;
			Check(same, "all sixteen BC2 explicit alpha values are replicated to eight bits");
		}
		for (bool reverse : {false, true}) {
			auto data = Fixture(4, 4, '5');
			data[128] = reverse ? 10 : 220;
			data[129] = reverse ? 220 : 10;
			std::uint64_t indices = 0;
			for (unsigned i = 0; i < 16; ++i)
				indices |= std::uint64_t(i % 8) << (i * 3);
			for (unsigned i = 0; i < 6; ++i)
				data[130 + i] = static_cast<unsigned char>(indices >> (i * 8));
			const std::array<unsigned, 8> expected = reverse
														 ? std::array<unsigned, 8>{10, 220, 52, 94, 136, 178, 0, 255}
														 : std::array<unsigned, 8>{220, 10, 190, 160, 130, 100, 70, 40};
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid,
				  "BC3 alpha block decodes");
			bool same = image.rgba.size() == 64;
			if (same)
				for (unsigned i = 0; i < 16; ++i)
					same = same && image.rgba[i * 4 + 3] == expected[i % 8];
			Check(same, "both BC3 alpha endpoint orders and all eight indices");
		}
		const auto valid = Fixture(7, 5, '5');
		{
			// A shipped Gunslinger endpoint pair: exact division differs from Mesa's approximate lerp.
			auto data=Fixture(4,4,'5'); data[128]=252; data[129]=2;
			std::uint64_t indices=0;
			for (unsigned i=0; i<16; ++i) indices|=std::uint64_t(i%8)<<(i*3);
			for (unsigned i=0; i<6; ++i) data[130+i]=static_cast<unsigned char>(indices>>(i*8));
			const std::array<unsigned,8> expected={252,2,216,180,144,109,73,37};
			bool same=DecodeDdsBasePixels(data.data(),data.size(),image)==DdsPixelsStatus::Valid && image.rgba.size()==64;
			if (same) for (unsigned i=0; i<16; ++i) same=same && image.rgba[i*4+3]==expected[i%8];
			Check(same,"BC3 non-integral alpha interpolation retains exact integer division");
		}
		for (size_t length = 0; length < valid.size(); ++length) {
			image = {9, 9, {1, 2, 3}};
			Check(DecodeDdsBasePixels(valid.data(), length, image) == DdsPixelsStatus::Invalid && Cleared(image),
				  "every base truncation rejects and clears prior output");
		}
		for (const auto change : {std::array<unsigned, 2>{0, 0},
								  {1, 123},
								  {2, 0},
								  {3, 0},
								  {4, 4097},
								  {6, 2},
								  {7, 5},
								  {19, 31},
								  {27, 0},
								  {28, 0x200},
								  {28, 0x200000}}) {
			auto data = valid;
			Word(data, change[0], change[1]);
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Invalid && Cleared(image),
				  "invalid header rejected");
		}
		for (char format : {'2', '4', 'X'}) {
			auto data = valid;
			Word(data, 21, 0x00545844 | (unsigned(format) << 24));
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Unsupported &&
					  Cleared(image),
				  "premultiplied or unknown codec not relabeled");
		}
		for (std::uint32_t flags : {4u, 5u, 0x80000004u}) {
			auto data = valid;
			Word(data, 20, flags);
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid,
				  "authored alpha and NVTT normal metadata accepted");
			DdsMipChain chain;
			Check(InspectDdsMipChain(data.data(), data.size(), chain) ==
					  (flags == 4 ? DdsMipStatus::Valid : DdsMipStatus::Unsupported),
				  "full-chain flag policy unchanged");
		}
		for (std::uint32_t flags : {0u, 0x44u, 0x20004u}) {
			auto data = valid;
			Word(data, 20, flags);
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Unsupported &&
					  Cleared(image),
				  "unknown format flags fail closed");
		}
		for (unsigned extra : {0u, 1u, 15u}) {
			auto data = valid;
			Word(data, 7, 3);
			data.resize(data.size() + extra, 0);
			Check(DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid,
				  "complete base survives incomplete unused mip tail");
			DdsMipChain chain;
			Check(InspectDdsMipChain(data.data(), data.size(), chain) == DdsMipStatus::Invalid,
				  "base decoder never admits incomplete chain to GPU uploader");
		}
		Check(DecodeDdsBasePixels(nullptr, valid.size(), image) == DdsPixelsStatus::Invalid && Cleared(image),
			  "null source rejected");
		Check(DecodeDdsBasePixels(valid.data(), SIZE_MAX, image) == DdsPixelsStatus::Invalid && Cleared(image),
			  "oversized source rejected before access");
	}
	void Originals(const std::string &root) {
		for (const char *path : {"/Heroes/Plane/_.dds", "/Heroes/Gunslinger/S2/_.dds", "/Heroes/Crusader/_.dds",
								 "/Terrain/Elements/old/Rock_NRM.dds"}) {
			std::ifstream file(root + path, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() < 128 || file.tellg() > 64 * 1024 * 1024) {
				Check(false, "shipped fixture available");
				continue;
			}
			std::vector<unsigned char> data(static_cast<size_t>(file.tellg()));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
				Check(false, "shipped fixture read");
				continue;
			}
			DdsPixels image;
			DdsBaseLevel base;
			Check(InspectDdsBaseLevel(data.data(), data.size(), base) == DdsMipStatus::Valid &&
					  DecodeDdsBasePixels(data.data(), data.size(), image) == DdsPixelsStatus::Valid &&
					  image.width == base.level.width && image.height == base.level.height &&
					  image.rgba.size() == size_t(image.width) * image.height * 4,
				  "shipped DDS base decodes");
			const auto original = image.rgba;
			if (base.format==DdsBlockFormat::Bc3 && image.rgba.size()==size_t(image.width)*image.height*4) {
				// Independent BC3 oracle from the published alpha table, not a second library call.
				bool alphaSame=true;
				for (unsigned y=0; y<image.height; ++y) for (unsigned x=0; x<image.width; ++x) {
					const auto* block=data.data()+128+(size_t(y/4)*((image.width+3)/4)+x/4)*16;
					std::uint64_t indices=0;
					for (unsigned byte=0; byte<6; ++byte) indices|=std::uint64_t(block[byte+2])<<(byte*8);
					const unsigned code=unsigned((indices>>(((y%4)*4+x%4)*3))&7);
					unsigned alpha=0;
					if (code<2) alpha=block[code];
					else if (block[0]>block[1]) alpha=((8-code)*unsigned(block[0])+(code-1)*unsigned(block[1]))/7;
					else if (code<6) alpha=((6-code)*unsigned(block[0])+(code-1)*unsigned(block[1]))/5;
					else alpha=code==6 ? 0 : 255;
					alphaSame=alphaSame && image.rgba[(size_t(y)*image.width+x)*4+3]==alpha;
				}
				Check(alphaSame,"all shipped BC3 alpha pixels match independent exact integer interpolation");
			}
			Check(DecodeDdsBasePixels(data.data(), 128 + base.level.bytes, image) == DdsPixelsStatus::Valid &&
					  image.rgba == original,
				  "unused authored mip bytes do not change base pixels");
		}
	}
} // namespace
int main(int argc, char **argv) {
	if (argc > 2)
		return 2;
	if (argc == 2)
		Originals(argv[1]);
	else
		Mocks();
	std::cout << "DDS pixels: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
