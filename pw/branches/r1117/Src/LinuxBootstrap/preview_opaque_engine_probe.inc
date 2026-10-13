/** Opaque and cutout BasicMaterial pixels must not inherit preview transparency. */
bool RunLinuxPreviewOpaqueEngineProbe()
{
	using namespace Render::LinuxPreviewOpaque;
	unsigned checks=0, failures=0;
	const auto check=[&](bool good,const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout,"Preview opaque probe: %s result=%s\n",label,good ? "PASS" : "FAIL");
	};
	const auto enablei=reinterpret_cast<PFNGLENABLEIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glEnablei")));
	const auto disablei=reinterpret_cast<PFNGLDISABLEIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDisablei")));
	const auto isEnabledi=reinterpret_cast<PFNGLISENABLEDIPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glIsEnabledi")));
	const auto bindSampler=reinterpret_cast<PFNGLBINDSAMPLERPROC>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindSampler")));
	if (!enablei || !disablei || !isEnabledi || !bindSampler) { check(false,"required GL3.3 state entry points"); return false; }
	glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	GLint active=0, sampler=0, matrixMode=0;
	glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_SAMPLER_BINDING,&sampler); bindSampler(0,0); glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
	const bool oldBlend0=isEnabledi(GL_BLEND,0), oldBlend1=isEnabledi(GL_BLEND,1);
	glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
	glEnable(GL_TEXTURE_2D); glColorMask(1,1,1,1); glDepthMask(GL_FALSE);
	glViewport(0,0,128,128); glPixelStorei(GL_UNPACK_ALIGNMENT,1); glPixelStorei(GL_PACK_ALIGNMENT,1);
	glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
	glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	enablei(GL_BLEND,0); disablei(GL_BLEND,1); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_LESS,.25f);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glClearColor(0,0,0,0);
	GLuint texture=0; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	const auto upload=[&](int alpha) {
		const unsigned char rgba[]={255,0,0,static_cast<unsigned char>(alpha)};
		glBindTexture(GL_TEXTURE_2D,texture); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
	};
	const auto redPixel=[] { unsigned char rgba[4]{}; glReadPixels(64,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba); return int(rgba[0]); };
	NDb::BasicMaterial material;
	material.renderState.blendMode=NDb::BLENDMODE_OFF;
	material.EmissivePinValue=NDb::EMISSIVEPIN_EMISSIVEOFF; material.OpacityPinValue=NDb::BOOLEANPIN_NONE;
	for (int ref : {-1,0,1,127,128,255}) for (int alpha : {0,1,126,127,128,254,255}) {
		material.renderState.alphaTest=ref<0 ? NDb::ONOFFSTATE_OFF : NDb::ONOFFSTATE_ON;
		material.renderState.alphaTestRef=ref<0 ? 127 : ref;
		upload(alpha); glClear(GL_COLOR_BUFFER_BIT);
		{
			Scope opaque(Resolve(&material));
			glColor4ub(255,255,255,255); glBegin(GL_QUADS);
			glTexCoord2f(.5f,.5f); glVertex2f(-1,-1); glVertex2f(1,-1); glVertex2f(1,1); glVertex2f(-1,1); glEnd();
		}
		const bool visible=ref<0 || alpha>=ref;
		check(visible ? redPixel()>250 : redPixel()==0,"opaque pixels obey disabled or original GEQUAL alpha testing without blending");
		GLint function=0; GLfloat reference=0; GLboolean depth=1;
		glGetIntegerv(GL_ALPHA_TEST_FUNC,&function); glGetFloatv(GL_ALPHA_TEST_REF,&reference); glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);
		check(isEnabledi(GL_BLEND,0) && !isEnabledi(GL_BLEND,1) && glIsEnabled(GL_ALPHA_TEST) &&
			function==GL_LESS && reference==.25f && !depth,"scope restores host alpha/blend state without altering depth or other outputs");
	}
	for (int bad=0; bad<4; ++bad) {
		State state; state.selected=true;
		if (bad==0) state.valid=false;
		if (bad==1) state.alphaTest=2;
		if (bad==2) state.alphaRef=-1;
		if (bad==3) state.alphaRef=256;
		Scope rejected(state); check(!rejected.Ok() && !rejected.TextureAlpha(),"malformed selected alpha policy fails closed before drawing");
	}
	material.renderState.alphaTest=static_cast<NDb::OnOffState>(256);
	check(Resolve(&material).selected && !Resolve(&material).valid,"raw authored alpha-test enum is validated before narrowing");
	material.renderState.alphaTest=NDb::ONOFFSTATE_OFF;
	for (int excluded=0; excluded<6; ++excluded) {
		NDb::BasicMaterial other=material;
		if (excluded==0) other.ignoreRenderState=true;
		if (excluded==1) other.isBlendModeOverrided=1;
		if (excluded==2) other.specialTransparency=true;
		if (excluded==3) other.renderState.blendMode=NDb::BLENDMODE_LERPBYALPHA;
		if (excluded==4) other.EmissivePinValue=NDb::EMISSIVEPIN_EMISSIVEFROMALPHA;
		if (excluded==5) other.OpacityPinValue=NDb::BOOLEANPIN_PRESENT;
		Scope legacy(Resolve(&other));
		check(!Resolve(&other).selected && legacy.Ok() && !legacy.TextureAlpha() && isEnabledi(GL_BLEND,0),
			"unsupported alpha/raster permutations preserve legacy preview policy");
	}
	check(!Resolve(nullptr).selected,"missing material does not select opaque state");
	{
		State state; state.selected=true; state.alphaTest=1; state.alphaRef=128;
		Scope outer(state); state.alphaTest=0;
		{ Scope inner(state); check(inner.Ok() && !glIsEnabled(GL_ALPHA_TEST),"nested opaque scope applies its own cutout policy"); }
		GLfloat reference=0; glGetFloatv(GL_ALPHA_TEST_REF,&reference);
		check(outer.Ok() && glIsEnabled(GL_ALPHA_TEST) && fabsf(reference-128.f/255.f)<.00001f && !isEnabledi(GL_BLEND,0),
			"nested scope restores outer cutout and opaque state");
	}
	bool wrongThread=false;
	std::thread worker([&] { State state; state.selected=true; Scope rejected(state); wrongThread=!rejected.Ok(); }); worker.join();
	check(wrongThread,"selected opaque draw requires a current context on its thread");
	{
		glEnable(GLenum(0)); State state; state.selected=true; Scope rejected(state);
		check(!rejected.Ok() && !rejected.TextureAlpha() && isEnabledi(GL_BLEND,0),
			"pre-existing GL error rejects selection without changing host blend state");
	}
	// Exercise the actual original Primitive consumer at a boundary broken by alpha*238/255.
	glDisable(GL_ALPHA_TEST);
	{
		struct Vertex { CVec3 position; CVec2 uv; };
		const Vertex vertices[]={{{-.8f,0,-.8f},{.5f,.5f}},{{.8f,0,-.8f},{.5f,.5f}},
			{{.8f,0,.8f},{.5f,.5f}},{{-.8f,0,.8f},{.5f,.5f}}};
		const unsigned short indices[]={0,1,2,0,2,3};
		std::unique_ptr<IDirect3DVertexBuffer9> vb(Render::CreateVB(sizeof(vertices),Render::RENDER_POOL_MANAGED,vertices).GetPtr());
		std::unique_ptr<IDirect3DIndexBuffer9> ib(Render::CreateIB16(sizeof(indices),Render::RENDER_POOL_MANAGED,indices).GetPtr());
		Render::VertexFormatDescriptor format;
		format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,position),Render::VERTEXELEMENTTYPE_FLOAT3,Render::VERETEXELEMENTUSAGE_POSITION,0));
		format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,uv),Render::VERTEXELEMENTTYPE_FLOAT2,Render::VERETEXELEMENTUSAGE_TEXCOORD,0));
		Render::Primitive primitive; primitive.SetVertexDeclaration(Render::SmartRenderer::GetVertexFormatDeclaration(format));
		primitive.SetVertexStream(vb.get(),sizeof(Vertex)); primitive.SetIndexBuffer(ib.get());
		primitive.GetDipDescriptor()=Render::DipDescriptor(Render::RENDERPRIMITIVE_TRIANGLELIST,0,0,4,0,2);
		for (int alpha : {0,127,128,255}) {
			upload(alpha); glClear(GL_COLOR_BUFFER_BIT);
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
			Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
			material.renderState.alphaTest=NDb::ONOFFSTATE_ON; material.renderState.alphaTestRef=128;
			Select(&material); primitive.Bind(); primitive.Draw();
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
			check(alpha>=128 ? redPixel()>250 : redPixel()==0,"actual renderer keeps texture alpha at the authored cutout boundary");
			check(!Current().selected && isEnabledi(GL_BLEND,0) && !glIsEnabled(GL_ALPHA_TEST),"renderer teardown clears selection and restores caller policy");
		}
		Render::SmartRenderer::UnBindVertexBuffer(0); Render::SmartRenderer::BindIndexBuffer(DXIndexBufferRef());
	}
	// Aliased opaque/cutout slots must not merge into one texture-only hero batch.
	LinuxWindowOverlay overlay; overlay.ready=overlay.openglReady=true;
	LinuxSelectedHeroDbPreview hero; auto& mesh=hero.sceneAsset.meshPreview;
	mesh.ready=mesh.boundsValid=mesh.headerValid=true; mesh.minX=-.5f; mesh.maxX=.5f; mesh.maxZ=1;
	LinuxTextureAssetPreview asset; asset.sourceFile="__preview_opaque_alias";
	asset.artworkLoaded=asset.artwork.ready=true; asset.artwork.width=asset.artwork.height=1;
	asset.artwork.rgba={255,0,0,128}; hero.sceneAsset.skinDiffuseTextures.push_back(asset);
	for (int side=0; side<2; ++side) {
		LinuxHeroMeshPreviewMaterial slot; slot.opaque.selected=true; slot.opaque.alphaTest=side;
		slot.opaque.alphaRef=129; mesh.materials.push_back(slot);
		for (int corner=0; corner<3; ++corner) {
			LinuxHeroMeshPreviewVertex vertex;
			vertex.x=(side ? .25f : -.25f)+(corner==0 ? -.2f : corner==1 ? .2f : 0);
			vertex.z=corner==2 ? .9f : .1f; vertex.u=vertex.v=.5f;
			vertex.texCoordValid=true; vertex.diffuseTextureIndex=0; vertex.materialIndex=side;
			mesh.triangleVertices.push_back(vertex);
		}
	}
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-3,3,0,6,-1,1);
	LinuxBootstrap::FramePoseCache<LinuxHeroMeshPreviewVertex> poses;
	for (int repeat=0; repeat<2; ++repeat) {
		glClear(GL_COLOR_BUFFER_BIT);
		check(DrawLinuxHeroMeshPreview(&overlay,&hero,255,255,255,0,false,&poses),"actual opaque/cutout hero preview draws");
		unsigned char left[4]{},right[4]{};
		glReadPixels(36,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left); glReadPixels(92,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
		check(left[0]>250 && right[0]==0,"same texture uses separate opaque and cutout policies after cold/warm pose reuse");
	}
	check(poses.builds==1 && poses.hits==1 && overlay.heroPreviewDiffuseTextureCache.size()==1,"opaque state does not duplicate cached poses or textures");
	LinuxHeroMeshPreview merged; MergeLinuxHeroMeshPayloadPreview(mesh,&merged); MergeLinuxHeroMeshPayloadPreview(mesh,&merged);
	check(merged.materials.size()==4 && merged.materials[2].opaque.alphaTest==0 && merged.materials[3].opaque.alphaTest==1 &&
		merged.triangleVertices[6].materialIndex==2 && merged.triangleVertices[9].materialIndex==3,
		"payload merging preserves per-slot alpha policy and remaps identities together");
	const auto body=NDb::Get<NDb::BasicMaterial>(NDb::DBID("Heroes/Plane/_2.MTRL"));
	const auto glass=NDb::Get<NDb::BasicMaterial>(NDb::DBID("Heroes/Plane/_1.MTRL"));
	check(body && glass && Resolve(body.GetPtr()).selected && !Resolve(glass.GetPtr()).selected,
		"shipped Plane body selects opaque policy while glass keeps its separate legacy path");
	for (const auto& item : overlay.heroPreviewDiffuseTextureCache) glDeleteTextures(1,&item.second.texture);
	glDeleteTextures(1,&texture); bindSampler(0,sampler);
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(matrixMode);
	glPopClientAttrib(); glPopAttrib(); glActiveTexture(active);
	(oldBlend0 ? enablei : disablei)(GL_BLEND,0); (oldBlend1 ? enablei : disablei)(GL_BLEND,1);
	check(glGetError()==GL_NO_ERROR,"opaque fixtures leave no GL errors");
	fprintf(stdout,"Preview opaque summary: checks=%u failures=%u\n",checks,failures);
	return failures==0;
}
