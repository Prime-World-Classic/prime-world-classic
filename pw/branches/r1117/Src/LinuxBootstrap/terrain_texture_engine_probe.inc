/** Real authored DDS upload/readback with hostile unpack state and private buffer ownership. */
template<class Check>
static void CheckLinuxDdsUpload(const LinuxTextureAssetPreview& source, const Check& check)
{
	using LinuxBootstrap::DdsMipStatus;
	LinuxWindowOverlay::OpenGlTexture texture;
	if (!LinuxDdsGlExtension("GL_EXT_texture_compression_s3tc")) {
		check(UploadLinuxDdsMipTexture(source,texture)==DdsMipStatus::Unsupported && !texture.texture,
			"missing S3TC does not allocate or pretend to upload authored mips");
		return;
	}
	const auto bindBuffer=reinterpret_cast<PFNGLBINDBUFFERPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindBuffer")));
	const auto genBuffers=reinterpret_cast<PFNGLGENBUFFERSPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGenBuffers")));
	const auto deleteBuffers=reinterpret_cast<PFNGLDELETEBUFFERSPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteBuffers")));
	const auto readback=reinterpret_cast<PFNGLGETCOMPRESSEDTEXIMAGEPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGetCompressedTexImage")));
	check(bindBuffer && genBuffers && deleteBuffers && readback,"native compressed upload/readback entrypoints");
	if (!bindBuffer || !genBuffers || !deleteBuffers || !readback) return;
	GLint active=0,binding=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
	glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	GLuint buffer=0,hostTexture=0;
	genBuffers(1,&buffer); bindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
	glGenTextures(1,&hostTexture); glBindTexture(GL_TEXTURE_2D,hostTexture); glActiveTexture(GL_TEXTURE3);
	const GLenum parameters[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,
		GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST,
		GL_UNPACK_COMPRESSED_BLOCK_WIDTH,GL_UNPACK_COMPRESSED_BLOCK_HEIGHT,
		GL_UNPACK_COMPRESSED_BLOCK_DEPTH,GL_UNPACK_COMPRESSED_BLOCK_SIZE};
	const GLint hostile[]={8,21,3,5,1,1,4,4,1,8};
	const size_t count=LinuxDdsGlExtension("GL_ARB_compressed_texture_pixel_storage")?10:6;
	for (size_t i=0;i<count;++i) glPixelStorei(parameters[i],hostile[i]);
	check(UploadLinuxDdsMipTexture(source,texture)==DdsMipStatus::Valid && texture.mipLevels==10,
		"authored upload ignores hostile PBO and pixel-store parameters");
	bool restored=true;
	for (size_t i=0;i<count;++i) { GLint value=0; glGetIntegerv(parameters[i],&value); restored=restored && value==hostile[i]; }
	GLint value=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&value); restored=restored && value==GL_TEXTURE3;
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value); restored=restored && GLuint(value)==buffer;
	glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&value); restored=restored && GLuint(value)==hostTexture;
	check(restored,"DDS upload restores active unit, prior texture, PBO and every unpack parameter");
	bindBuffer(GL_PIXEL_PACK_BUFFER,0);
	for (GLenum parameter : {GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST})
		glPixelStorei(parameter,0);
	glPixelStorei(GL_PACK_ALIGNMENT,1);
	if (count==10) for (GLenum parameter : {GL_PACK_COMPRESSED_BLOCK_WIDTH,GL_PACK_COMPRESSED_BLOCK_HEIGHT,
		GL_PACK_COMPRESSED_BLOCK_DEPTH,GL_PACK_COMPRESSED_BLOCK_SIZE}) glPixelStorei(parameter,0);
	if (texture.texture && source.ddsMipSource) {
		glBindTexture(GL_TEXTURE_2D,texture.texture);
		for (size_t i=0;i<source.ddsMips.count;++i) {
			const auto& mip=source.ddsMips.levels[i];
			GLint width=0,height=0,bytes=0;
			glGetTexLevelParameteriv(GL_TEXTURE_2D,GLint(i),GL_TEXTURE_WIDTH,&width);
			glGetTexLevelParameteriv(GL_TEXTURE_2D,GLint(i),GL_TEXTURE_HEIGHT,&height);
			glGetTexLevelParameteriv(GL_TEXTURE_2D,GLint(i),GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&bytes);
			check(width==GLint(mip.width) && height==GLint(mip.height) && bytes==GLint(mip.bytes),"GPU mip dimensions and compressed extent match authored source");
			if (bytes==GLint(mip.bytes)) {
				std::vector<unsigned char> actual(mip.bytes);
				readback(GL_TEXTURE_2D,GLint(i),actual.data());
				check(!std::memcmp(actual.data(),source.ddsMipSource->data()+mip.offset,mip.bytes),"GPU mip retains exact original block bytes without regeneration");
			}
		}
	}
	check(glGetError()==GL_NO_ERROR,"DDS upload and exact compressed readback leave no GL errors");
	DeleteOpenGlTexture(&texture);
	glPopClientAttrib(); deleteBuffers(1,&buffer);
	glBindTexture(GL_TEXTURE_2D,GLuint(binding)); glDeleteTextures(1,&hostTexture); glActiveTexture(GLenum(active));
}
