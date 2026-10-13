/** Exact extension lookup in the current native compatibility context. */
static bool LinuxDdsGlExtension(const char* extension)
{
	const auto getString=reinterpret_cast<PFNGLGETSTRINGIPROC>(glXGetProcAddressARB(
		reinterpret_cast<const GLubyte*>("glGetStringi")));
	if (!getString) return false;
	GLint count=0; glGetIntegerv(GL_NUM_EXTENSIONS,&count);
	for (GLint i=0;i<count;++i) {
		const auto* name=getString(GL_EXTENSIONS,GLuint(i));
		if (name && !std::strcmp(reinterpret_cast<const char*>(name),extension)) return true;
	}
	return false;
}

/** Upload validated authored BC1/2/3 bytes with the driver's native decoder.
 * No generated mip levels, CPU recompression, or mutation of a borrowed texture.
 * Publish only after every subresource succeeds; restore all unpack and binding state.
 */
static LinuxBootstrap::DdsMipStatus UploadLinuxDdsMipTexture(const LinuxTextureAssetPreview& preview,
	LinuxWindowOverlay::OpenGlTexture& output)
{
	using LinuxBootstrap::DdsMipStatus;
	if (!preview.ddsMipSource) return DdsMipStatus::Unsupported;
	if (!glXGetCurrentContext() || output.texture) return DdsMipStatus::Invalid;
	LinuxBootstrap::DdsMipChain chain;
	const auto& bytes=*preview.ddsMipSource;
	if (LinuxBootstrap::InspectDdsMipChain(bytes.data(),bytes.size(),chain)!=DdsMipStatus::Valid ||
		chain.levels[0].width!=preview.width || chain.levels[0].height!=preview.height) return DdsMipStatus::Invalid;
	if (!LinuxDdsGlExtension("GL_EXT_texture_compression_s3tc")) return DdsMipStatus::Unsupported;
	const auto bindBuffer=reinterpret_cast<PFNGLBINDBUFFERPROC>(glXGetProcAddressARB(
		reinterpret_cast<const GLubyte*>("glBindBuffer")));
	const auto upload=reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(glXGetProcAddressARB(
		reinterpret_cast<const GLubyte*>("glCompressedTexImage2D")));
	if (!bindBuffer || !upload) return DdsMipStatus::Unsupported;
	const bool blockStore=LinuxDdsGlExtension("GL_ARB_compressed_texture_pixel_storage");
	const GLenum parameters[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,
		GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST,
		GL_UNPACK_COMPRESSED_BLOCK_WIDTH,GL_UNPACK_COMPRESSED_BLOCK_HEIGHT,
		GL_UNPACK_COMPRESSED_BLOCK_DEPTH,GL_UNPACK_COMPRESSED_BLOCK_SIZE};
	const size_t parameterCount=blockStore?10:6;
	GLint previous[10]{},active=0,binding=0,pbo=0;
	glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding); glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);
	for (size_t i=0;i<parameterCount;++i) {
		glGetIntegerv(parameters[i],&previous[i]); glPixelStorei(parameters[i],i?0:1);
	}
	bindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
	GLuint texture=0; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
	const GLenum format=chain.format==LinuxBootstrap::DdsBlockFormat::Bc1?GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
		chain.format==LinuxBootstrap::DdsBlockFormat::Bc2?GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	for (size_t i=0;i<chain.count;++i) {
		const auto& level=chain.levels[i];
		upload(GL_TEXTURE_2D,GLint(i),format,GLsizei(level.width),GLsizei(level.height),0,GLsizei(level.bytes),bytes.data()+level.offset);
	}
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(chain.count-1));
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,chain.count>1?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	const GLenum error=glGetError();
	for (size_t i=0;i<parameterCount;++i) glPixelStorei(parameters[i],previous[i]);
	bindBuffer(GL_PIXEL_UNPACK_BUFFER,GLuint(pbo));
	glBindTexture(GL_TEXTURE_2D,GLuint(binding)); glActiveTexture(GLenum(active));
	if (!texture || error!=GL_NO_ERROR) {
		if (texture) glDeleteTextures(1,&texture);
		return DdsMipStatus::Invalid;
	}
	output.texture=texture; output.width=chain.levels[0].width; output.height=chain.levels[0].height;
	output.mipLevels=unsigned(chain.count);
	return DdsMipStatus::Valid;
}
