/** Independent block colours expose legacy DXT mode and 565 expansion errors. */
bool RunLinuxDdsPreviewEngineProbe(const LinuxClientEnvironment &environment) {
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char *label) {
		++checks;
		if (!good)
			++failures;
		fprintf(stdout, "DDS preview probe: %s result=%s\n", label, good ? "PASS" : "FAIL");
	};
	for (char format : {'1', '3', '5'})
		for (bool reverse : {false, true}) {
			SDDSFileHeader header;
			header.header.dwHeaderFlags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_LINEARSIZE;
			header.header.dwWidth = header.header.dwHeight = 4;
			header.header.dwMipMapCount = 1;
			header.header.dwSurfaceFlags = DDS_SURFACE_FLAGS_TEXTURE;
			header.header.ddspf = format == '1' ? DDSPF_DXT1 : format == '3' ? DDSPF_DXT3 : DDSPF_DXT5;
			const size_t size = format == '1' ? 8 : 16;
			header.header.dwPitchOrLinearSize = size;
			std::array<unsigned char, 16> block{};
			if (format == '3')
				std::fill(block.begin(), block.begin() + 8, 255);
			if (format == '5')
				block[0] = 255;
			auto *colour = block.data() + (format == '1' ? 0 : 8);
			colour[0] = reverse ? 0 : 31;
			colour[1] = reverse ? 248 : 0;
			colour[2] = reverse ? 31 : 0;
			colour[3] = reverse ? 0 : 248;
			std::fill(colour + 4, colour + 8, 0xe4);
			MemoryStream source;
			source.Write(&header, sizeof(header));
			source.Write(block.data(), size);
			source.Seek(0, SEEKORIGIN_BEGIN);
			Render::LinuxDdsPreview::Image decoded;
			check(Render::LinuxDdsPreview::Load(std::addressof(static_cast<Stream &>(source)), decoded) &&
					  decoded.width == 4 && decoded.height == 4 && decoded.rgba.size() == 64,
				  "complete original-format mock block decodes");
			if (decoded.rgba.size() != 64)
				continue;
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
			for (unsigned x = 0; x < 4; ++x) {
				bool same = true;
				for (unsigned y = 0; y < 4; ++y)
					for (unsigned channel = 0; channel < 4; ++channel)
						same = same && decoded.rgba[(y * 4 + x) * 4 + channel] == expected[x][channel];
				check(same, "565 endpoints and DXT1-only transparent branch match independent RGBA colours");
			}
		}
	// Existing uncompressed artwork keeps its byte order and alpha.
	{
		SDDSFileHeader header;
		header.header.dwHeaderFlags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_PITCH;
		header.header.dwWidth = 2;
		header.header.dwHeight = 1;
		header.header.dwPitchOrLinearSize = 8;
		header.header.dwSurfaceFlags = DDS_SURFACE_FLAGS_TEXTURE;
		header.header.ddspf = DDSPF_A8R8G8B8;
		const unsigned char bgra[] = {11, 22, 33, 44, 55, 66, 77, 88};
		MemoryStream source;
		source.Write(&header, sizeof(header));
		source.Write(bgra, sizeof(bgra));
		source.Seek(0, SEEKORIGIN_BEGIN);
		Render::LinuxDdsPreview::Image image;
		check(Render::LinuxDdsPreview::Load(std::addressof(static_cast<Stream &>(source)), image) && image.width == 2 &&
				  image.height == 1 && image.rgba == std::vector<unsigned char>({33, 22, 11, 44, 77, 66, 55, 88}),
			  "legacy uncompressed BGRA artwork retains RGBA channel order");
		for (unsigned missing = 1; missing <= sizeof(bgra); ++missing) {
			MemoryStream truncated;
			truncated.Write(&header, sizeof(header));
			truncated.Write(bgra, sizeof(bgra) - missing);
			image = {9, 9, {1}};
			check(!Render::LinuxDdsPreview::Load(std::addressof(static_cast<Stream &>(truncated)), image) &&
					  !image.width && !image.height && image.rgba.empty(),
				  "truncated legacy raw bytes reject and clear prior output");
		}
		for (unsigned invalid = 0; invalid < 7; ++invalid) {
			auto corrupt = header;
			if (invalid == 0)
				corrupt.header.dwPitchOrLinearSize = 64;
			if (invalid == 1)
				corrupt.header.dwWidth = 4097;
			if (invalid == 2)
				corrupt.header.ddspf.dwRGBBitCount = 8;
			if (invalid == 3)
				corrupt.header.dwCubemapFlags = 0x200;
			if (invalid == 4)
				corrupt.header.dwDepth = 2;
			if (invalid == 5) {
				corrupt.header.ddspf.dwRGBBitCount = 16;
				corrupt.header.dwPitchOrLinearSize = 4;
				corrupt.header.ddspf.dwRBitMask = 65535;
			}
			if (invalid == 6)
				corrupt.header.dwSize = 0;
			MemoryStream source;
			source.Write(&corrupt, sizeof(corrupt));
			source.Write(bgra, sizeof(bgra));
			image = {9, 9, {1}};
			check(!Render::LinuxDdsPreview::Load(std::addressof(static_cast<Stream &>(source)), image) &&
					  !image.width && !image.height && image.rgba.empty(),
				  "malformed raw layout is rejected before legacy conversion");
		}
	}
	// Preserve the legacy non-BC1/2/3 conversion instead of relabeling premultiplied codecs.
	for (unsigned mode = 0; mode < 4; ++mode) {
		SDDSFileHeader header;
		header.header.dwHeaderFlags = DDS_HEADER_FLAGS_TEXTURE;
		header.header.dwWidth = header.header.dwHeight = 4;
		header.header.dwSurfaceFlags = DDS_SURFACE_FLAGS_TEXTURE;
		header.header.ddspf = mode == 0 ? DDSPF_R5G6B5 : mode == 1 ? DDSPF_R8G8B8 : mode == 2 ? DDSPF_DXT2 : DDSPF_DXT4;
		const size_t bytes = mode == 0 ? 32 : mode == 1 ? 48 : 16;
		std::vector<unsigned char> pixels(bytes, 255);
		MemoryStream source;
		source.Write(&header, sizeof(header));
		source.Write(pixels.data(), pixels.size());
		Render::LinuxDdsPreview::Image image;
		const bool loaded = Render::LinuxDdsPreview::Load(std::addressof(static_cast<Stream &>(source)), image);
		CArray2D<DWORD> legacy;
		source.Seek(0, SEEKORIGIN_BEGIN);
		bool same = loaded && NImage::LoadImageDDS(&legacy, std::addressof(static_cast<Stream &>(source))) &&
					image.rgba.size() == 64;
		if (same)
			for (unsigned y = 0; y < 4; ++y)
				for (unsigned x = 0; x < 4; ++x) {
					const DWORD pixel = legacy[y][x];
					const size_t at = (y * 4 + x) * 4;
					same = same && image.rgba[at] == ((pixel >> 16) & 255) &&
						   image.rgba[at + 1] == ((pixel >> 8) & 255) && image.rgba[at + 2] == (pixel & 255) &&
						   image.rgba[at + 3] == ((pixel >> 24) & 255);
				}
		check(same, "legacy RGB16, RGB24 and premultiplied DXT2/4 conversion unchanged");
	}
	glPushAttrib(GL_TEXTURE_BIT);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	GLint active = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_SKIP_ROWS, 0);
	glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
	const auto *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
	const bool s3tc = extensions && std::strstr(extensions, "GL_EXT_texture_compression_s3tc");
	for (const char *path : {"Heroes/Plane/_.dds", "Heroes/Gunslinger/S2/_.dds", "Heroes/Crusader/_.dds",
							 "Terrain/Elements/old/Rock_NRM.dds"}) {
		CObj<Stream> source = RootFileSystem::OpenFile(path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		Render::LinuxDdsPreview::Image image;
		const bool loaded = source && Render::LinuxDdsPreview::Load(source, image);
		check(loaded && image.width > 0 && image.height > 0 &&
				  image.rgba.size() == size_t(image.width) * image.height * 4,
			  "shipped base images remain readable with incomplete mip tails or alpha metadata flags");
		if (!loaded)
			continue;
		LinuxLoadingArtwork artwork;
		std::string error;
		const fs::path root = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
		check(LoadDdsArtwork(root / "Data" / path, &artwork, &error) && artwork.ready &&
				  artwork.width == int(image.width) && artwork.height == int(image.height) &&
				  artwork.rgba == image.rgba,
			  "Game artwork loader uses the corrected base image");
		const auto texture = Render::Load2DTextureFromFile(nstl::string(path), true);
		const GLuint handle = texture ? texture->GetOpenGLTexture() : 0;
		std::vector<unsigned char> uploaded(image.rgba.size(), 0);
		if (handle) {
			glBindTexture(GL_TEXTURE_2D, handle);
			GLint width = 0, height = 0;
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
			check(width == int(image.width) && height == int(image.height),
				  "TextureManager uploads the original image extent");
			if (width == int(image.width) && height == int(image.height))
				glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploaded.data());
		}
		check(handle && uploaded == image.rgba && glGetError() == GL_NO_ERROR,
			  "TextureManager GPU pixels match corrected artwork bytes exactly");
		if (s3tc) {
			std::vector<unsigned char> data(size_t(source->GetSize()));
			source->Seek(0, SEEKORIGIN_BEGIN);
			const bool read = source->Read(data.data(), data.size()) == int(data.size());
			LinuxBootstrap::DdsBaseLevel base;
			const bool valid = read && LinuxBootstrap::InspectDdsBaseLevel(data.data(), data.size(), base) ==
										   LinuxBootstrap::DdsMipStatus::Valid;
			check(valid, "GPU compressed reference has independently bounded base blocks");
			if (valid) {
				GLuint compressed = 0;
				glGenTextures(1, &compressed);
				glBindTexture(GL_TEXTURE_2D, compressed);
				const GLenum format =
					base.format == LinuxBootstrap::DdsBlockFormat::Bc1	 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
					: base.format == LinuxBootstrap::DdsBlockFormat::Bc2 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
																		 : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				glCompressedTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, base.level.bytes,
									   data.data() + base.level.offset);
				glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploaded.data());
				unsigned different = 0, maximum = 0;
				for (size_t i = 0; i < uploaded.size(); ++i) {
					const unsigned delta = unsigned(std::abs(int(uploaded[i]) - int(image.rgba[i])));
					maximum = std::max(maximum, delta);
					// Mesa's approximate BC3 alpha lerp can differ by two from Squish's exact division.
					// Endpoints and explicit alpha stay exact; RGB keeps its one-unit conversion allowance.
					unsigned tolerance=i%4==3 ? 0 : 1;
					if (i%4==3 && base.format==LinuxBootstrap::DdsBlockFormat::Bc3) {
						const size_t x=(i/4)%image.width,y=(i/4)/image.width;
						const auto* block=data.data()+128+((y/4)*((image.width+3)/4)+x/4)*16;
						std::uint64_t indices=0;
						for (unsigned byte=0; byte<6; ++byte) indices|=std::uint64_t(block[byte+2])<<(byte*8);
						const unsigned code=unsigned((indices>>(((y%4)*4+x%4)*3))&7);
						if (code>=2 && (block[0]>block[1] || code<6)) tolerance=2;
					}
					if (delta > tolerance)
						++different;
				}
				fprintf(stdout, "DDS GPU reference: path=%s bytes=%zu outside-tolerance=%u maximum=%u\n", path,
						uploaded.size(), different, maximum);
				check(!different && glGetError() == GL_NO_ERROR,
					  "driver reference preserves exact alpha endpoints and bounded interpolation differences");
				glDeleteTextures(1, &compressed);
			}
		}
	}
	if (!s3tc)
		fprintf(stdout, "DDS GPU reference unavailable; CPU and native upload checks remain mandatory\n");
	glPopClientAttrib();
	glPopAttrib();
	glActiveTexture(active);
	fprintf(stdout, "DDS preview summary: checks=%u failures=%u\n", checks, failures);
	return failures == 0;
}
