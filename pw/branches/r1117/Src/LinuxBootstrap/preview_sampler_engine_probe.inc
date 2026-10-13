/** Negative exporter UVs, aliased material slots and host sampler ownership. */
bool RunLinuxPreviewSamplerEngineProbe()
{
	using namespace Render::LinuxPreviewSampler;
	unsigned checks=0, failures=0;
	const auto check=[&](bool good,const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout,"Preview sampler probe: %s result=%s\n",label,good ? "PASS" : "FAIL");
	};
	const auto bindSampler=reinterpret_cast<PFNGLBINDSAMPLERPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindSampler")));
	const auto genSamplers=reinterpret_cast<PFNGLGENSAMPLERSPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGenSamplers")));
	const auto deleteSamplers=reinterpret_cast<PFNGLDELETESAMPLERSPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteSamplers")));
	const auto parameter=reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glSamplerParameteri")));
	const auto isSampler=reinterpret_cast<PFNGLISSAMPLERPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glIsSampler")));
	if (!bindSampler || !genSamplers || !deleteSamplers || !parameter || !isSampler) { check(false,"GL3.3 sampler entry points"); return false; }
	glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	GLint oldActive=0, oldSampler=0, matrixMode=0;
	glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive); glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_SAMPLER_BINDING,&oldSampler); glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
	glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND); glDisable(GL_LIGHTING); glDisable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
	glColorMask(1,1,1,1); glViewport(0,0,128,128);
	glPixelStorei(GL_UNPACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ALIGNMENT,1);
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
	GLuint texture=0, host=0; glGenTextures(1,&texture); genSamplers(1,&host);
	const unsigned char rgba[]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
	glBindTexture(GL_TEXTURE_2D,texture); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
	parameter(host,GL_TEXTURE_MIN_FILTER,GL_NEAREST); parameter(host,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	parameter(host,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); parameter(host,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
	bindSampler(0,host);
	const auto sample=[&](Address address,float u,float v) {
		glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
		glActiveTexture(GL_TEXTURE3);
		{
			Scope selected(texture,address);
			check(selected.Ok(),"valid authored address admitted");
			glColor4ub(255,255,255,255); glBegin(GL_QUADS);
			glTexCoord2f(u,v); glVertex2f(-1,-1); glVertex2f(1,-1); glVertex2f(1,1); glVertex2f(-1,1); glEnd();
		}
		GLint active=0, sampler=0, binding=0;
		glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_SAMPLER_BINDING,&sampler); glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
		check(active==GL_TEXTURE3 && sampler==int(host) && binding==int(texture),"active unit and borrowed sampler/texture bindings restored");
		std::array<unsigned char,4> pixel{}; glReadPixels(64,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data()); return pixel;
	};
	for (int mode=0; mode<4; ++mode) {
		Address address; address.selected=true; address.u=address.v=mode;
		const auto pixel=sample(address,.25f,-.25f);
		check(mode==0 ? pixel[2]>250 && pixel[0]<3 && pixel[1]<3 : mode==3 ?
			pixel[0]<3 && pixel[1]<3 && pixel[2]<3 : pixel[0]>250 && pixel[1]<3 && pixel[2]<3,
			"negative V follows authored wrap, clamp, mirror or black border");
	}
	GLint wrapS=0,wrapT=0; glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrapS); glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&wrapT);
	check(wrapS==GL_CLAMP_TO_EDGE && wrapT==GL_CLAMP_TO_EDGE,"sampling never mutates borrowed texture addressing");
	for (int bad=0; bad<4; ++bad) {
		Address address; address.selected=true;
		if (bad==0) address.valid=false;
		if (bad==1) address.u=-1;
		if (bad==2) address.v=4;
		Scope rejected(bad==3 ? 0 : texture,address);
		check(!rejected.Ok(),"malformed selected address or missing texture fails closed");
	}
	{
		NDb::BasicMaterial material; material.DiffuseMap.texture=new NDb::Texture;
		material.DiffuseMap.samplerState.addressU=NDb::TEXTUREADDRESSTYPE_MIRROR;
		material.DiffuseMap.samplerState.addressV=NDb::TEXTUREADDRESSTYPE_WRAP;
		const auto address=Resolve(&material);
		check(address.selected && address.valid && address.u==2 && address.v==0,"authored BasicMaterial sampler snapshot");
		material.DiffuseMap.samplerState.addressU=static_cast<NDb::TextureAddressType>(256);
		check(Resolve(&material).selected && !Resolve(&material).valid,"invalid DB enum is rejected before narrowing");
		check(!Resolve(nullptr).selected,"unselected preview retains its legacy contract");
	}
	{
		std::vector<NDb::Ptr<NDb::Material>> materials;
		for (int mode=0; mode<2; ++mode) {
			auto* material=new NDb::BasicMaterial; material->DiffuseMap.texture=new NDb::Texture;
			material->DiffuseMap.samplerState.addressU=material->DiffuseMap.samplerState.addressV=static_cast<NDb::TextureAddressType>(mode);
			materials.push_back(material);
		}
		std::vector<size_t> aliased(2,0); LinuxHeroMeshPreview payload; std::vector<std::string> warnings;
		check(ProbeLinuxHeroSkinMeshPayloadFile("../Data/Heroes/Plane/_.skin",&aliased,&materials,10000,&payload,&warnings),
			"original skin payload loads with aliased texture slots");
		std::set<size_t> slots; bool metadata=true, negativeV=false;
		for (const auto& vertex : payload.triangleVertices) {
			slots.insert(vertex.samplerIndex); negativeV=negativeV || vertex.v<0;
			metadata=metadata && vertex.diffuseTextureIndex==0 && vertex.samplerIndex<2;
		}
		check(metadata && negativeV && slots.size()==2 && payload.samplerAddresses.size()==2 &&
			payload.samplerAddresses[0].u==0 && payload.samplerAddresses[1].u==1,
			"original fragment material IDs survive payload decoding independently of texture aliases");
		check(ProbeLinuxHeroSkinMeshPayloadFile("../Data/Heroes/Plane/_.skin",&aliased,nullptr,10000,&payload,&warnings) &&
			payload.samplerAddresses.empty(),"missing DB references retain legacy preview without allocation from raw material count");
	}
	std::array<GLint,16> owned{};
	bool cacheValid=true;
	for (int repeat=0; repeat<2; ++repeat) for (int u=0; u<4; ++u) for (int v=0; v<4; ++v) {
		Address address; address.selected=true; address.u=u; address.v=v;
		Scope selected(texture,address); GLint sampler=0; glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
		cacheValid=cacheValid && selected.Ok() && sampler && sampler!=int(host);
		if (!repeat) owned[u*4+v]=sampler;
		else cacheValid=cacheValid && owned[u*4+v]==sampler;
	}
	std::set<GLint> distinct(owned.begin(),owned.end());
	check(cacheValid && distinct.size()==16,"all sixteen address pairs reuse a bounded private cache");
	Address wrapped; wrapped.selected=true;
	{
		Scope outer(texture,wrapped); GLint outerSampler=0; glGetIntegerv(GL_SAMPLER_BINDING,&outerSampler);
		{
			Address clamped=wrapped; clamped.u=clamped.v=1;
			Scope inner(texture,clamped); GLint innerSampler=0; glGetIntegerv(GL_SAMPLER_BINDING,&innerSampler);
			check(outer.Ok() && inner.Ok() && innerSampler!=outerSampler,"nested material scopes retain separate address state");
			check(!Shutdown(),"shutdown refuses a live sampler scope");
		}
		GLint restored=0; glGetIntegerv(GL_SAMPLER_BINDING,&restored);
		check(restored==outerSampler && isSampler(outerSampler),"inner scope restores the live outer sampler");
	}
	GLuint cube=0; glGenTextures(1,&cube); glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
	{
		Scope wrongTarget(cube,wrapped);
		check(!wrongTarget.Ok(),"wrong-target texture rejects even when a complete 2D texture was already bound");
	}
	GLint restoredTexture=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&restoredTexture);
	check(restoredTexture==int(texture) && glGetError()==GL_NO_ERROR,"wrong-target failure consumes its error and restores the borrowed texture");
	glDeleteTextures(1,&cube);
	bool wrongThread=false;
	std::thread worker([&] { Scope rejected(texture,wrapped); wrongThread=!rejected.Ok() && !Shutdown(); }); worker.join();
	check(wrongThread,"a thread without the owning context cannot bind or release cached samplers");
	Display* display=glXGetCurrentDisplay(); const GLXContext context=glXGetCurrentContext();
	const GLXDrawable draw=glXGetCurrentDrawable(), read=glXGetCurrentReadDrawable();
	const bool unbound=glXMakeContextCurrent(display,None,None,nullptr);
	bool noContext=false;
	if (unbound) { Scope rejected(texture,wrapped); noContext=!rejected.Ok() && !Shutdown(); }
	const bool rebound=glXMakeContextCurrent(display,draw,read,context);
	check(unbound && noContext && rebound,"missing context rejects work without discarding live ownership");
	if (!rebound) { fprintf(stderr,"Preview sampler fixture could not restore its GLX context.\n"); return false; }
	glEnable(GLenum(0));
	check(!Shutdown() && isSampler(owned[0]),"shutdown refuses an outstanding GL error without deleting resources");
	check(Shutdown(),"owning context releases the sampler cache");
	bool released=true; for (auto sampler : owned) released=released && !isSampler(sampler);
	GLint restoredSampler=0; glGetIntegerv(GL_SAMPLER_BINDING,&restoredSampler);
	check(released && isSampler(host) && restoredSampler==int(host),"teardown deletes only owned samplers and preserves the borrowed binding");
	check(Shutdown(),"empty sampler shutdown is idempotent");
	const auto recreated=sample(wrapped,.25f,-.25f);
	check(recreated[2]>250 && recreated[0]<3,"cache recreation after teardown retains wrapped pixels");
	// Exercise the original Primitive/SmartRenderer consumer, not only the binding helper.
	{
		struct Vertex { CVec3 position; CVec2 uv; };
		const Vertex vertices[]={{{-.8f,0,-.8f},{.25f,-.25f}},{{.8f,0,-.8f},{.25f,-.25f}},
			{{.8f,0,.8f},{.25f,-.25f}},{{-.8f,0,.8f},{.25f,-.25f}}};
		const unsigned short indices[]={0,1,2,0,2,3};
		std::unique_ptr<IDirect3DVertexBuffer9> vb(Render::CreateVB(sizeof(vertices),Render::RENDER_POOL_MANAGED,vertices).GetPtr());
		std::unique_ptr<IDirect3DIndexBuffer9> ib(Render::CreateIB16(sizeof(indices),Render::RENDER_POOL_MANAGED,indices).GetPtr());
		Render::VertexFormatDescriptor format;
		format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,position),Render::VERTEXELEMENTTYPE_FLOAT3,Render::VERETEXELEMENTUSAGE_POSITION,0));
		format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,uv),Render::VERTEXELEMENTTYPE_FLOAT2,Render::VERETEXELEMENTUSAGE_TEXCOORD,0));
		Render::Primitive primitive; primitive.SetVertexDeclaration(Render::SmartRenderer::GetVertexFormatDeclaration(format));
		primitive.SetVertexStream(vb.get(),sizeof(Vertex)); primitive.SetIndexBuffer(ib.get());
		primitive.GetDipDescriptor()=Render::DipDescriptor(Render::RENDERPRIMITIVE_TRIANGLELIST,0,0,4,0,2);
		NDb::BasicMaterial material; material.DiffuseMap.texture=new NDb::Texture;
		material.DiffuseMap.samplerState.addressU=material.DiffuseMap.samplerState.addressV=NDb::TEXTUREADDRESSTYPE_WRAP;
		for (bool invalid : {false,true}) {
			glClear(GL_COLOR_BUFFER_BIT);
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
			Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
			if (invalid) material.DiffuseMap.samplerState.addressV=static_cast<NDb::TextureAddressType>(4);
			Select(&material); primitive.Bind(); primitive.Draw();
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
			unsigned char pixel[4]{}; glReadPixels(64,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
			check(invalid ? !pixel[0] && !pixel[1] && !pixel[2] : pixel[2]>60 && pixel[0]<3 && pixel[1]<3,
				invalid ? "actual renderer rejects malformed selected addressing without drawing" : "actual renderer samples the wrapped negative-V texel");
			check(!Current().selected,"immediate renderer teardown clears material selection");
		}
		Render::SmartRenderer::UnBindVertexBuffer(0); Render::SmartRenderer::BindIndexBuffer(DXIndexBufferRef());
	}
	// Same texture, different material slots must split immediate batches and pose-cache data.
	LinuxWindowOverlay overlay; overlay.ready=overlay.openglReady=true;
	LinuxSelectedHeroDbPreview hero; auto& mesh=hero.sceneAsset.meshPreview;
	mesh.ready=mesh.boundsValid=mesh.headerValid=true;
	mesh.minX=-.5f; mesh.maxX=.5f; mesh.maxZ=1;
	LinuxTextureAssetPreview asset; asset.sourceFile="__preview_sampler_alias";
	asset.artworkLoaded=asset.artwork.ready=true; asset.artwork.width=asset.artwork.height=2;
	asset.artwork.rgba.assign(rgba,rgba+sizeof(rgba)); hero.sceneAsset.skinDiffuseTextures.push_back(asset);
	for (int side=0; side<2; ++side) {
		Address address; address.selected=true; address.u=address.v=side;
		mesh.samplerAddresses.push_back(address);
		for (int corner=0; corner<3; ++corner) {
			LinuxHeroMeshPreviewVertex vertex;
			vertex.x=(side ? .25f : -.25f)+(corner==0 ? -.2f : corner==1 ? .2f : 0);
			vertex.z=corner==2 ? .9f : .1f; vertex.u=.25f; vertex.v=-.25f;
			vertex.texCoordValid=true; vertex.diffuseTextureIndex=0; vertex.samplerIndex=side;
			mesh.triangleVertices.push_back(vertex);
		}
	}
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-3,3,0,6,-1,1);
	LinuxBootstrap::FramePoseCache<LinuxHeroMeshPreviewVertex> poses;
	for (int repeat=0; repeat<2; ++repeat) {
		glClear(GL_COLOR_BUFFER_BIT);
		check(DrawLinuxHeroMeshPreview(&overlay,&hero,255,255,255,0,false,&poses),"actual aliased hero preview draws");
		unsigned char left[4]{},right[4]{};
		glReadPixels(36,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left); glReadPixels(92,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
		check(left[2]>240 && left[0]<8 && right[0]>240 && right[2]<8,"per-material addressing survives cold/warm texture and pose reuse");
	}
	check(poses.builds==1 && poses.hits==1 && overlay.heroPreviewDiffuseTextureCache.size()==1,"sampler identity does not duplicate texture or pose storage");
	LinuxHeroMeshPreview merged; MergeLinuxHeroMeshPayloadPreview(mesh,&merged); MergeLinuxHeroMeshPayloadPreview(mesh,&merged);
	check(merged.samplerAddresses.size()==4 && merged.triangleVertices.size()==12 &&
		merged.triangleVertices[0].samplerIndex==0 && merged.triangleVertices[3].samplerIndex==1 &&
		merged.triangleVertices[6].samplerIndex==2 && merged.triangleVertices[9].samplerIndex==3,
		"merged skin payloads remap material identities independently of shared texture indices");
	for (const auto& item : overlay.heroPreviewDiffuseTextureCache) glDeleteTextures(1,&item.second.texture);
	bindSampler(0,oldSampler); deleteSamplers(1,&host); glDeleteTextures(1,&texture);
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(matrixMode);
	glPopClientAttrib(); glPopAttrib(); glActiveTexture(oldActive);
	check(glGetError()==GL_NO_ERROR,"sampler fixtures leave no GL errors");
	fprintf(stdout,"Preview sampler summary: checks=%u failures=%u\n",checks,failures);
	return failures==0;
}
