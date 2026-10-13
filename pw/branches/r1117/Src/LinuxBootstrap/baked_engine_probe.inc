/** Draw original Rough01N geometry/material/texture with two original placed color records. */
bool RunLinuxBakedEngineProbe(const std::string& capturePath, const LinuxClientEnvironment& environment,
	const NDb::BasicFXMaterial& originalFx)
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout, "Baked engine probe: %s result=%s\n", label, good ? "PASS" : "FAIL");
	};
	const auto scene = NDb::Get<NDb::DBSceneObject>(NDb::DBID("Terrain/Minor/Rough01N.SOBJ"));
	const auto* component = scene && scene->attached.size() == 1
		? dynamic_cast<const NDb::DBStaticSceneComponent*>(scene->attached[0].component.GetPtr()) : nullptr;
	check(component && component->attached.empty() && component->materialsReferences.size() == 1,
		"original scene has one static component and one material");
	if (!component || component->materialsReferences.empty()) return false;
	const auto* authored = dynamic_cast<const NDb::BasicMaterial*>(component->materialsReferences[0].GetPtr());
	const auto path = NormalizeRootFileSystemPath(component->geometryFileName.c_str());
	const auto* geometry = Render::RenderResourceManager::LoadStaticMeshGeometry(nstl::string(path.c_str()), false);
	check(authored && geometry && !geometry->primitives.empty(), "original material and common geometry resolve");
	if (!authored || !geometry || geometry->primitives.empty()) return false;
	{
		LinuxSelectedMapPreview discovered;
		discovered.staticGeometryPayloads.resize(1);
		ProbeLinuxMapSceneObjectStaticPayloads(environment,scene.GetPtr(),Placement(),378,&discovered);
		check(discovered.staticGeometryPayloads.size() == 2 && discovered.staticGeometryPayloads[1].hasBakedIdentity &&
			discovered.staticGeometryPayloads[1].originalObjectIndex == 378 &&
			discovered.staticGeometryPayloads[1].bakedSceneHash == scene->GetDBID().GetHashKey() &&
			discovered.staticGeometryPayloads[1].bakedComponentIndex == 0 && !discovered.staticGeometryPayloads[0].hasBakedIdentity,
			"production discovery preserves original object identity despite an earlier filtered payload");
		ProbeLinuxMapSceneObjectStaticPayloads(environment,scene.GetPtr(),Placement(),1293,&discovered);
		check(discovered.staticGeometryPayloads.size() == 3 && discovered.staticGeometryPayloads[2].originalObjectIndex == 1293,
			"production discovery keeps same-geometry placements distinct");
		for (int shape = 0; shape < 3; ++shape) {
			auto* alternate = new NDb::DBSceneObject(*scene);
			NDb::Ptr<NDb::DBSceneObject> held(alternate);
			if (shape == 0) alternate->attached.push_back(scene->attached[0]);
			else {
				auto* root = new NDb::DBStaticSceneComponent(*component);
				alternate->attached[0].component = root;
				if (shape == 1) root->attached.push_back(scene->attached[0]);
				else root->geometryFileName = "missing-baked-identity-fixture.stat";
			}
			LinuxSelectedMapPreview rejected;
			ProbeLinuxMapSceneObjectStaticPayloads(environment,alternate,Placement(),378,&rejected);
			bool marked = false;
			for (const auto& item : rejected.staticGeometryPayloads) marked |= item.hasBakedIdentity;
			check(!marked, "duplicate roots, child hierarchies and absent payloads cannot receive a guessed color identity");
		}
	}
	LinuxSelectedMapPreview map;
	check(LoadLinuxBakedVertexColors("Maps/Multiplayer/MOBA/_.vtxcolor", map.bakedVertexColors),
		"original map color file loads through RootFileSystem");
	if (!map.bakedVertexColors) return false;
	check(scene->GetDBID().GetHashKey() == 172962304u, "engine DBID hash matches original serialized scene identity");
	LinuxMapStaticGeometryPayloadPreview payload{};
	payload.rootGeometryFile = path; payload.localBounds = component->aabb;
	payload.materialReferences.push_back(component->materialsReferences[0]);
	payload.materialDiffuseTextureIndices.push_back(kLinuxHeroPreviewNoDiffuseTexture);
	payload.hasBakedIdentity = true; payload.originalObjectIndex = 378;
	payload.bakedSceneHash = scene->GetDBID().GetHashKey(); payload.bakedComponentIndex = 0;
	map.staticGeometryPayloads.push_back(payload);
	LinuxWindowOverlay overlay;
	glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	glViewport(0,0,256,256); glPixelStorei(GL_PACK_ALIGNMENT,1);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-5,5,-5,5,-30,30);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(60,1,0,0); glRotatef(20,0,0,1);
	const float savedNight = Render::SceneConstants::Global().nightFraction;
	using Pixels = std::array<unsigned char, 256*256*4>;
	// expected: 1 baked, 2 BasicFX, 0 unsupported preview, or negative fail-closed rejection.
	const auto draw = [&](float night, int expected = 1) {
		Render::SceneConstants::RetriveForModification().nightFraction = night;
		glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1); glDepthMask(GL_TRUE);
		glClearColor(0,0,0,1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		LinuxStaticTransparentPass pass(1, {}); LinuxRendererMaterialSlotStats stats;
		size_t payloads = 0, triangles = 0;
		const auto before = Render::LinuxBasicFx::GetStatistics();
		const auto batches = DrawLinuxMapRendererStaticMeshPayloads(&overlay,&map,0,0,1,&stats,&payloads,&triangles,pass);
		check(pass.Flush(), "opaque baked map draw leaves an empty valid transparent pass");
		const auto after = Render::LinuxBasicFx::GetStatistics();
		check(batches == 1 && payloads == 1 && (expected == 2
			? after.draws == before.draws + 1 && after.bakedDraws == before.bakedDraws : expected > 0
			? triangles == 2364 && after.bakedDraws == before.bakedDraws + 1
			: after.bakedDraws == before.bakedDraws && (expected < 0
				? after.bakedRejected > before.bakedRejected : after.bakedSelections == before.bakedSelections)),
			"actual map hook distinguishes original baked drawing, unsupported preview and rejection");
		Pixels pixels{}; glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
		unsigned covered = 0;
		for (size_t i = 0; i < pixels.size(); i += 4)
			if (pixels[i] || pixels[i+1] || pixels[i+2]) ++covered;
		check(expected < 0 ? covered == 0 : covered > 100,
			"selected malformed draws stay blank while valid original or preview geometry is visible");
		return pixels;
	};
	const auto day = draw(0), middle = draw(.5f), night = draw(1);
	const auto changed = [](const Pixels& a, const Pixels& b) {
		unsigned count = 0;
		for (size_t i = 0; i < a.size(); i += 4)
			if (a[i] != b[i] || a[i+1] != b[i+1] || a[i+2] != b[i+2]) ++count;
		return count;
	};
	check(changed(day,night) > 100 && changed(day,middle) > 100 && changed(middle,night) > 100,
		"cached mesh follows fresh day/night constants without rebuilding resources");
	check(overlay.staticMapResources[1].builds == 1 && overlay.staticMapResources[1].hits == 2,
		"day midpoint and night share one per-placement cached resource");
	if (!capturePath.empty()) {
		std::string error;
		check(CaptureLinuxOpenGlBackBufferPng(capturePath + "-baked-night.png",256,256,&error), "night capture succeeds");
	}
	check(draw(0) == day, "returning to day exactly restores original pixels");
	if (!capturePath.empty()) {
		std::string error;
		check(CaptureLinuxOpenGlBackBufferPng(capturePath + "-baked-day.png",256,256,&error), "day capture succeeds");
	}
	overlay.staticMapResources[1].Clear(); map.staticGeometryPayloads[0].originalObjectIndex = 1293;
	const auto second = draw(0);
	check(changed(day,second) > 100, "same geometry and WORLD use the second placement's distinct original lighting");
	check(overlay.staticMapResources[1].builds == 2, "map invalidation replaces placed colors with the rebuilt mesh");
	{
		auto* cached = overlay.staticMapResources[1].Get(0,1,[]() { return std::unique_ptr<LinuxStaticMapResource>(); });
		check(cached && cached->mesh && cached->mesh->GetVertexColors(), "cached baked mesh exposes its retained color descriptor");
		if (cached && cached->mesh) {
			std::weak_ptr<Render::StaticMesh> weak(cached->mesh);
			LinuxStaticTransparentPass pass(1, {});
			auto* retained = pass.Retain(cached->mesh,cached->world,component->aabb,1);
			overlay.staticMapResources[1].Clear();
			check(retained && !weak.expired(), "pending view keeps the aliased mesh and both backing buffers after eviction");
			if (retained) {
				glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				Render::BatchQueueSorter sorter(16,true); Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN,sorter,32);
				retained->mesh->RenderToQueue(queue);
				Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
				const auto before = Render::LinuxBasicFx::GetStatistics();
				for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
					for (Render::Batch* batch = queue.GetBatches(priority); batch; batch = batch->pNextBatch) {
						batch->Prepare(); batch->Draw();
					}
				Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
				Pixels pixels{}; glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
				check(pixels == second && Render::LinuxBasicFx::GetStatistics().bakedDraws == before.bakedDraws + 1,
					"retained real draw reads both native buffers correctly after map cache eviction");
			}
			check(pass.Flush() && weak.expired(), "flushing drops the last alias owner and its color allocations");
		}
	}
	const auto loads = Render::LinuxBasicFx::GetStatistics().loads;
	for (int mode = 0; mode < 18; ++mode) {
		auto* material = new NDb::BasicMaterial(*authored);
		map.staticGeometryPayloads[0].materialReferences[0] = material;
		map.staticGeometryPayloads[0].hasBakedIdentity = mode != 7;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const auto savedPins = Render::GetRuntimePins();
		if (mode == 0) material->ignoreRenderState = true;
		if (mode == 1) material->LightingPinValue = NDb::LIGHTINGPIN_LIGHTINGSH;
		if (mode == 2) material->OpacityPinValue = NDb::BOOLEANPIN_PRESENT;
		if (mode == 3) material->NormalMap.texture = material->DiffuseMap.texture;
		if (mode == 4) material->SpecularMap.texture = material->DiffuseMap.texture;
		if (mode == 5) material->specularPower = 2;
		if (mode == 6) material->renderState.blendMode = NDb::BLENDMODE_LERPBYALPHA;
		if (mode == 8) material->outputMul.R = nan;
		if (mode == 9) material->specularPower = nan;
		if (mode == 10) material->renderState.alphaTestRef = 256;
		if (mode == 11) material->renderState.culling = static_cast<NDb::OnOffState>(99);
		if (mode == 12) material->renderState.blendMode = static_cast<NDb::BlendMode>(99);
		if (mode == 14) Render::GetRuntimePins().DoDepthOutValue = NDb::BOOLPIN_TRUE;
		if (mode == 15) Render::GetRuntimePins().InstancingValue = NDb::BOOLEANPIN_PRESENT;
		if (mode == 16) Render::GetRuntimePins().RenderModeValue = static_cast<NDb::RenderModePin>(99);
		if (mode == 17) material->outputMul.A = material->outputAdd.A = nan;
		overlay.staticMapResources[1].Clear();
		const auto pixels = draw(mode == 13 ? nan : 0, mode < 8 ? 0 : mode == 17 ? 1 : -1);
		if (mode == 17) check(pixels == second, "unused BasicMaterial output alpha never changes original texture alpha or RGB");
		Render::GetRuntimePins() = savedPins;
	}
	map.staticGeometryPayloads[0].materialReferences[0] = component->materialsReferences[0];
	map.staticGeometryPayloads[0].hasBakedIdentity = true;
	overlay.staticMapResources[1].Clear();
	check(draw(0) == second && Render::LinuxBasicFx::GetStatistics().loads == loads,
		"valid recovery after unsupported and malformed siblings reuses one combined shader family");
	{
		// Mixed original resources are a state-isolation fixture, not a new authored asset.
		auto* fx = new NDb::BasicFXMaterial(originalFx);
		fx->renderState.blendMode = NDb::BLENDMODE_OFF;
		fx->renderState.alphaTest = fx->renderState.culling = NDb::ONOFFSTATE_OFF;
		fx->DiffuseMul = decltype(fx->DiffuseMul)(Render::HDRColor(0,0,0,0));
		fx->DiffuseAdd = decltype(fx->DiffuseAdd)(Render::HDRColor(.8f,.1f,.2f,1));
		map.staticGeometryPayloads[0].materialReferences[0] = fx;
		overlay.staticMapResources[1].Clear();
		draw(0,2);
		map.staticGeometryPayloads[0].materialReferences[0] = component->materialsReferences[0];
		overlay.staticMapResources[1].Clear();
		check(draw(0) == second && Render::LinuxBasicFx::GetStatistics().loads == loads,
			"baked, missing-color preview, BasicFX and baked interleaving cannot leak streams or reload shaders");
	}
	{
		auto* cached = overlay.staticMapResources[1].Get(0,1,[]() { return std::unique_ptr<LinuxStaticMapResource>(); });
		if (cached && cached->mesh->GetVertexColors()) {
			cached->mesh->GetVertexColors()->pVB1->storage.pop_back();
			draw(0,-1);
		}
		overlay.staticMapResources[1].Clear();
		check(draw(0) == second, "malformed supplied native color buffers fail closed and cannot poison replacement resources");
	}
	LinuxStaticMeshStorage storage;
	auto& mesh = storage.mesh;
	const auto bind = [&](size_t object, unsigned hash, size_t componentIndex, const Render::MeshGeometry& source) {
		return BindLinuxBakedVertexColors(storage,source,map.bakedVertexColors,object,hash,componentIndex);
	};
	check(bind(378,payload.bakedSceneHash,0,*geometry) && mesh.GetVertexColors() && mesh.GetVertexColors()->numColors == 1950,
		"original CRC and complete common vertex count admit both color buffers");
	for (unsigned mode = 0; mode < 3; ++mode) {
		check(!bind(mode == 0 ? 999999 : 378, mode == 1 ? 0 : payload.bakedSceneHash, mode == 2 ? 1 : 0,*geometry)
			&& !mesh.GetVertexColors(), "wrong object scene or component clears previous mesh colors");
		check(bind(378,payload.bakedSceneHash,0,*geometry), "valid lookup recovers after rejected identity");
	}
	const auto* original = geometry->primitives[0];
	{
		Render::MeshGeometry wrongCount;
		auto* primitive = new Render::Primitive;
		primitive->SetVertexStream(original->GetVertexBuffer(),original->GetVBStride()*2);
		wrongCount.primitives.push_back(primitive);
		check(!bind(378,payload.bakedSceneHash,0,wrongCount) && !mesh.GetVertexColors(),
			"same common VB CRC with a different vertex count cannot reuse colors");
	}
	{
		Render::MeshGeometry wrongCrc;
		const auto* common = Get(original->GetVertexBuffer());
		std::unique_ptr<IDirect3DVertexBuffer9> buffer(Render::CreateVB(common->storage.size(),Render::RENDER_POOL_MANAGED).GetPtr());
		Render::FillVB(buffer.get(),common->storage.size(),&common->storage[0]);
		buffer->storage[0] ^= 1;
		auto* primitive = new Render::Primitive; primitive->SetVertexStream(buffer.get(),original->GetVBStride());
		wrongCrc.primitives.push_back(primitive);
		check(!bind(378,payload.bakedSceneHash,0,wrongCrc) && !mesh.GetVertexColors(),
			"changed common VB CRC cannot reuse colors despite matching vertex count");
	}
	check(bind(378,payload.bakedSceneHash,0,*geometry), "original cached geometry remains unchanged by rejection fixtures");
	for (int mode = 0; mode < 3; ++mode) {
		std::unique_ptr<IDirect3DVertexBuffer9> other(Render::CreateVB(4,Render::RENDER_POOL_MANAGED).GetPtr());
		Render::MeshGeometry split;
		auto* first = new Render::Primitive; first->SetVertexStream(original->GetVertexBuffer(),original->GetVBStride());
		split.primitives.push_back(first);
		auto* second = mode == 2 ? nullptr : new Render::Primitive;
		if (second) second->SetVertexStream(mode ? original->GetVertexBuffer() : Render::DXVertexBufferRef(other.get()),
			mode ? original->GetVBStride()*2 : original->GetVBStride());
		split.primitives.push_back(second);
		check(!bind(378,payload.bakedSceneHash,0,split) && !mesh.GetVertexColors(),
			"a differing second fragment buffer, stride or null primitive rejects the entire common color binding");
	}
	auto unavailable = map.bakedVertexColors;
	check(!LoadLinuxBakedVertexColors("missing-baked-color-fixture.vtxcolor",unavailable) && !unavailable,
		"failed map asset replacement clears the previous decoded map");
	Render::SceneConstants::RetriveForModification().nightFraction = savedNight;
	Render::LinuxBasicFx::ClearSelection(); Render::SmartRenderer::UnBindTexture(0u);
	check(glGetError() == GL_NO_ERROR, "baked engine fixtures leave no GL errors");
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
	glPopClientAttrib(); glPopAttrib();
	fprintf(stdout, "Baked engine summary: checks=%u failures=%u\n", checks, failures);
	return !failures;
}
