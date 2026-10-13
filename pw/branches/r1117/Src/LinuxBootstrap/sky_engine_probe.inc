/** Real admitted MOBA sky geometry: raster coverage, original curves, depth and cache lifetime. */
template<class Check>
static void CheckLinuxSkyDraw(LinuxWindowOverlay& overlay, const LinuxSelectedMapPreview& map, Check check)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	glViewport(0,0,128,128); glPixelStorei(GL_PACK_ALIGNMENT,1);
	GLint mode=0; glGetIntegerv(GL_MATRIX_MODE,&mode);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-50,50,-50,50,-220,220);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(56,1,0,0);
	using Pixels=std::array<unsigned char,128*128*4>;
	const auto render=[&](double elapsed) {
		glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1); glDepthMask(GL_TRUE);
		glClearColor(0,0,0,1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		const auto before=Render::LinuxBasicFx::GetStatistics();
		const auto draws=DrawLinuxMapSky(overlay,map,{205,205,0,92.f/410},elapsed);
		check(draws==map.skyGeometryPayloads.size() && draws>0 &&
			Render::LinuxBasicFx::GetStatistics().skyRejected==before.skyRejected,"real admitted sky meshes draw without fallback");
		Pixels pixels{}; glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
		std::array<float,128*128> depths{}; glReadPixels(0,0,128,128,GL_DEPTH_COMPONENT,GL_FLOAT,depths.data());
		check(std::all_of(depths.begin(),depths.end(),[](float value) { return value==1; }),"actual sky preserves every scene depth sample");
		size_t colored=0; std::set<unsigned> colors;
		for (size_t i=0; i<pixels.size(); i+=4) {
			if (pixels[i] || pixels[i+1] || pixels[i+2]) ++colored;
			colors.insert(unsigned(pixels[i])|(unsigned(pixels[i+1])<<8)|(unsigned(pixels[i+2])<<16));
		}
		check(colored>1000 && colors.size()>16,"original sky geometry survives culling and shows its real texture");
		return pixels;
	};
	const auto first=render(10), middle=render(185), end=render(360), clamped=render(710);
	// A depth-only foreground plane at the preview's world origin must hide the sky.
	// Read-only depth alone cannot stop a nearer shell under the orthographic preview camera.
	glColorMask(1,1,1,1); glDepthMask(GL_TRUE); glClearColor(0,0,0,1); glClearDepth(.5);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	check(DrawLinuxMapSky(overlay,map,{205,205,0,92.f/410},710)==map.skyGeometryPayloads.size(),
		"actual sky submission behind foreground depth");
	Pixels occluded{}; glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,occluded.data());
	bool foregroundIntact=true;
	for (size_t i=0; i<occluded.size(); i+=4) foregroundIntact=foregroundIntact && !occluded[i] && !occluded[i+1] && !occluded[i+2];
	check(foregroundIntact,"actual sky enclosure cannot cover nearer foreground geometry in preview mode");
	check(first!=middle,"authored 350-second sky UV curve changes pixels");
	check(end==clamped,"original Clamp curve holds the last key");
	const auto builds=overlay.staticMapResources[2].builds;
	overlay.staticMapResources[2].Clear();
	check(render(710)==clamped && overlay.staticMapResources[2].builds==builds+map.skyGeometryPayloads.size(),
		"sky cache rebuild retains map animation epoch and pixels");
	check(render(10)==first,"presentation clock reset starts original sky curves again");
	// Exercise the actual bridge with selected malformed material state, not only backend enums.
	if (!map.skyGeometryPayloads.empty()) {
		const auto& payload=map.skyGeometryPayloads.front();
		const auto* original=dynamic_cast<const NDb::SkyMaterial*>(payload.materialReferences.front().GetPtr());
		auto* resource=overlay.staticMapResources[2].Get(0,map.skyGeometryPayloads.size(),[] {
			return std::unique_ptr<LinuxStaticMapResource>();
		});
		const auto textureIndex=payload.materialDiffuseTextureIndices.front();
		check(original && resource && textureIndex<overlay.mapPreviewDiffuseTextures.size(),"sky material rejection fixture ready");
		if (original && resource && textureIndex<overlay.mapPreviewDiffuseTextures.size()) {
			const auto& texture=overlay.mapPreviewDiffuseTextures[textureIndex];
			for (int kind=0; kind<13; ++kind) {
				NDb::SkyMaterial material(*original);
				float elapsed=1; unsigned levels=texture.mipLevels;
				const auto nan=std::numeric_limits<float>::quiet_NaN();
				if (kind==0) material.ignoreRenderState=true;
				if (kind==1) material.renderState.blendMode=NDb::BLENDMODE_LERPBYALPHA;
				if (kind==2) material.renderState.alphaTest=static_cast<NDb::OnOffState>(7);
				if (kind==3) material.renderState.culling=static_cast<NDb::OnOffState>(7);
				if (kind==4) material.renderState.alphaTestRef=256;
				if (kind==5) material.DiffuseMap.samplerState.addressU=NDb::TEXTUREADDRESSTYPE_CLAMP;
				if (kind==6) material.DiffuseMap.samplerState.mipFilter=static_cast<NDb::MipFilterType>(7);
				if (kind==7) material.uOffset1=decltype(material.uOffset1)(nan);
				if (kind==8) material.vOffset2=decltype(material.vOffset2)(nan);
				if (kind==9) elapsed=nan;
				if (kind==10) elapsed=-1;
				if (kind==11) material.outputMul.R=nan;
				if (kind==12) levels=0;
				glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
				Render::BatchQueueSorter sorter(16,true); Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN,sorter,16);
				resource->mesh->RenderToQueue(queue);
				const auto before=Render::LinuxBasicFx::GetStatistics();
				Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
				for (int priority=0; priority<NDb::MATERIALPRIORITY_COUNT; ++priority)
					for (auto* batch=queue.GetBatches(priority); batch; batch=batch->pNextBatch) {
						batch->Prepare(); Render::LinuxBasicFx::SelectSky(material,texture.texture,levels,elapsed); batch->Draw();
					}
				Render::LinuxBasicFx::ClearSelection(); Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
				const auto after=Render::LinuxBasicFx::GetStatistics();
				check(after.skyDraws==before.skyDraws && after.skyRejected==before.skyRejected+1,
					"malformed sky material remains selected and rejects without fallback");
				Pixels pixels{}; glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
				bool blank=true;
				for (size_t i=0; i<pixels.size(); i+=4) blank=blank && !pixels[i] && !pixels[i+1] && !pixels[i+2];
				check(blank,"rejected actual sky material leaves framebuffer untouched");
			}
		}
	}
	glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GLenum(mode));
	glPopClientAttrib(); glPopAttrib();
}
