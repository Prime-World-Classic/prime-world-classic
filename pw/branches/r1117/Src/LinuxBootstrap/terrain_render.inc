/** Upload only the bounded current-nature image, preserving caller unpack/binding state. */
static bool UploadLinuxTerrainNatureTexture(LinuxWindowOverlay& overlay)
{
	const auto& mask=overlay.terrainNatureMask;
	if (!mask.AllChannels() || mask.Pixels().empty() || !glXGetCurrentContext()) return false;
	const auto bindBuffer=reinterpret_cast<PFNGLBINDBUFFERPROC>(glXGetProcAddressARB(
		reinterpret_cast<const GLubyte*>("glBindBuffer")));
	if (!bindBuffer) return false;
	GLint active=0,binding=0,unpack=0;
	glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
	constexpr GLenum parameters[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
	GLint values[4]{};
	for (size_t i=0; i<4; ++i) {
		glGetIntegerv(parameters[i],&values[i]); glPixelStorei(parameters[i],i?0:1);
	}
	bindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
	auto& texture=overlay.terrainNatureTexture;
	if (!texture.texture) glGenTextures(1,&texture.texture);
	glBindTexture(GL_TEXTURE_2D,texture.texture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,GLsizei(mask.Width()),GLsizei(mask.Height()),0,
		GL_RGBA,GL_UNSIGNED_BYTE,mask.Pixels().data());
	const GLenum error=glGetError();
	const bool ok=texture.texture && error==GL_NO_ERROR;
	for (size_t i=0; i<4; ++i) glPixelStorei(parameters[i],values[i]);
	bindBuffer(GL_PIXEL_UNPACK_BUFFER,GLuint(unpack));
	glBindTexture(GL_TEXTURE_2D,GLuint(binding)); glActiveTexture(GLenum(active));
	if (ok) {
		texture.width=unsigned(mask.Width()); texture.height=unsigned(mask.Height());
		++overlay.terrainNatureUploads;
	}
	return ok;
}

/** Cache original hex identities and refresh actual gameplay nature once per world step.
 * A neutral image is used only before a world exists. A live invalid world never falls
 * back to invented neutral state. GPU nature transitions/noise remain a separate port.
 */
static bool PrepareLinuxTerrainNatureMask(LinuxWindowOverlay& overlay, const LinuxSelectedMapPreview& map,
	const NWorld::PFWorld* world, int step)
{
	if (!map.bakedTerrain) return false;
	const auto* nature=world?world->GetNatureMap():nullptr;
	if (world && !nature) { overlay.terrainNatureReady=false; return false; }
	const auto& heightmap=map.terrainHeightmap;
	const size_t width=(std::clamp)(heightmap.layerMaskWidth,size_t(32),size_t(1024));
	const size_t height=(std::clamp)(heightmap.layerMaskHeight,size_t(32),size_t(1024));
	auto* user=const_cast<NWorld::PFWorldNatureMap*>(nature);
	const auto locate=[](void* owner,float x,float y) noexcept {
		const auto* map=static_cast<const NWorld::PFWorldNatureMap*>(owner);
		return map?map->LocateElement(x,y):0;
	};
	if (overlay.terrainNatureGeometry.lock()!=map.bakedTerrain || overlay.terrainNatureOwner!=nature ||
		step<overlay.terrainNatureStep || overlay.terrainNatureMask.Width()!=width || overlay.terrainNatureMask.Height()!=height) {
		overlay.terrainNatureReady=false;
		if (!overlay.terrainNatureMask.Initialize(width,height,heightmap.worldWidth,heightmap.worldHeight,locate,user)) return false;
		overlay.terrainNatureGeometry=map.bakedTerrain; overlay.terrainNatureOwner=nature;
		overlay.terrainNatureStep=-1; overlay.terrainNatureUploadPending=true;
		fprintf(stdout,"Native terrain nature: pixels=%zux%zu cells=%zu live=%d\n",width,height,overlay.terrainNatureMask.CellCount(),world!=nullptr);
	}
	if (overlay.terrainNatureReady && overlay.terrainNatureStep==step) return true;
	const auto query=[](void* owner,int id) noexcept {
		const auto* map=static_cast<const NWorld::PFWorldNatureMap*>(owner);
		return map?map->GetNatureInPoint(id):0;
	};
	bool changed=false;
	if (!overlay.terrainNatureMask.Refresh(query,user,changed)) { overlay.terrainNatureReady=false; return false; }
	overlay.terrainNatureUploadPending=overlay.terrainNatureUploadPending || changed;
	if (overlay.terrainNatureUploadPending && !UploadLinuxTerrainNatureTexture(overlay)) {
		overlay.terrainNatureReady=false; return false;
	}
	overlay.terrainNatureUploadPending=false; overlay.terrainNatureReady=true; overlay.terrainNatureStep=step;
	return true;
}

/** Original tile bytes plus admitted layer textures; no material creation or hidden asset load.
 * First active N/A/B pass writes depth, later passes add RGB. Zero-weight faction passes
 * are skipped using the sampled image's conservative bilinear-repeat tile footprint.
 */
static size_t DrawLinuxBakedTerrainLayers(LinuxWindowOverlay& overlay, const LinuxSelectedMapPreview& map,
	const LinuxMapStaticGeometryPayloadPreview& payload, const pwshader::PreviewTransform& preview)
{
	const auto fail=[&](const char* message) {
		if (++overlay.terrainLayerFailures==1) fprintf(stderr,"Native terrain layer input rejected: %s\n",message);
		return size_t(0);
	};
	const auto* tile=map.bakedTerrain?map.bakedTerrain->GetTile(payload.originalObjectIndex):nullptr;
	const auto& selection=payload.terrainLayerSelection;
	if (!tile || !overlay.terrainNatureReady || !selection.count || selection.count>4 ||
		!overlay.terrainNatureTexture.texture) return fail("geometry, nature mask or layer selection unavailable");
	const unsigned channels=overlay.terrainNatureMask.Channels(tile->minimum[0],tile->minimum[1],tile->maximum[0],tile->maximum[1]);
	if (!channels) return fail("invalid terrain bounds or empty nature coverage");
	if ((channels&(channels-1)) && !Render::LinuxBasicFx::SupportsTerrainAdditive())
		return fail("mixed-nature tile needs indexed blending before its first pass");
	std::array<std::array<GLuint,4>,3> textures{};
	std::array<std::array<unsigned,4>,3> mipLevels{};
	// Resolve every needed sampler before any faction pass, so a missing later texture
	// cannot leave a partially shaded tile. All indices were admitted by the scene ledger.
	for (size_t nature=0; nature<3; ++nature) if (channels&(1u<<nature))
		for (size_t layer=0; layer<selection.count; ++layer) {
			textures[nature][layer]=ResolveLinuxMapPreviewDiffuseTexture(&overlay,&map,payload.terrainTextureIndices[nature][layer]);
			if (!textures[nature][layer]) return fail("budget-admitted faction diffuse texture unavailable");
			mipLevels[nature][layer]=overlay.mapPreviewDiffuseTextures[payload.terrainTextureIndices[nature][layer]].mipLevels;
		}
	pwshader::IndexedDraw draw;
	draw.vertices={map.bakedTerrain->VertexData(payload.originalObjectIndex),size_t(tile->vertexCount)*44};
	draw.indexData={map.bakedTerrain->IndexData(payload.originalObjectIndex),size_t(tile->indexCount)*4};
	draw.stride=44; draw.uvOffset=24;
	draw.indexType=pwshader::IndexType::UInt32; draw.indexCount=size_t(tile->triangleCount)*3;
	draw.dayColors={draw.vertices,32,44}; draw.nightColors={draw.vertices,36,44}; draw.terrainWeights={draw.vertices,40,44};
	draw.natureTexture=overlay.terrainNatureTexture.texture;
	draw.alphaTest=pwshader::AlphaTest::GreaterEqual; draw.alphaReference=127; draw.cullMode=pwshader::CullMode::Back;
	Render::LinuxBasicFx::TerrainConstants constants;
	constants.layers=unsigned(selection.count); constants.localUV=selection.LocalUV();
	constants.nightFraction=Render::SceneConstants::Global().nightFraction;
	constants.natureMapping={1/map.terrainHeightmap.worldWidth,-1/map.terrainHeightmap.worldHeight,0,1};
	const pwshader::Float4 weights[]={ {0,1,0,0},{0,0,1,0},{1,0,0,0} };
	size_t passes=0;
	for (size_t nature=0; nature<3; ++nature) if (channels&(1u<<nature)) {
		draw.terrainTextures=textures[nature]; constants.natureWeight=weights[nature];
		draw.terrainMipLevels=mipLevels[nature];
		draw.blendMode=passes?pwshader::BlendMode::AddColor:pwshader::BlendMode::Off;
		if (!Render::LinuxBasicFx::DrawTerrain(draw,constants,preview)) return fail("original terrain shader draw failed");
		++passes; ++overlay.terrainLayerDraws;
	}
	return passes;
}
