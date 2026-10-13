/** Original MOBA catalog integration: bounded nearby selection, stable identity and cache epochs. */
static bool RunLinuxSceneWindowEngineProbe(const LinuxClientEnvironment& environment)
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool value, const char* message) {
		++checks;
		if (!value) { ++failures; fprintf(stderr, "Scene window: %s\n", message); }
	};
	LinuxMapCatalogEntry entry;
	entry.descriptor = "Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb";
	LinuxSelectedMapPreview map;
	map.tactical.ready = true;
	map.tactical.minX = map.tactical.minY = 0;
	map.tactical.maxX = map.tactical.maxY = 410;
	ProbeSelectedMapStaticSceneObjects(environment, entry, &map);
	check(map.sceneCatalog && !map.sceneCatalog->discovering, "original catalog finalized");
	if (!map.sceneCatalog) return false;
	const auto& catalog = *map.sceneCatalog;
	const auto spawnTile = std::find_if(catalog.terrain.begin(), catalog.terrain.end(),
		[](const auto& tile) { return tile.originalObjectIndex == 1187; });
	check(spawnTile != catalog.terrain.end(), "original hero-spawn terrain tile exists");
	if (spawnTile != catalog.terrain.end()) {
		check(spawnTile->terrainLayerSelection.count==1 && spawnTile->terrainLayerSelection.ids[0]==6 &&
			!spawnTile->terrainLayerSelection.LocalUV(), "real spawn tile selects global Bank layer6");
		check(map.terrainHeightmap.fullLayerMask.size()==430336 && map.terrainHeightmap.globalTerrainMaterials.size()==11,
			"native map retains the complete mask and compact valid material registry");
		check(spawnTile->materialReferences.size()==1 && map.terrainHeightmap.globalTerrainMaterials.size()>4 &&
			spawnTile->materialReferences[0].GetPtr()==map.terrainHeightmap.globalTerrainMaterials[4].GetPtr(),
			"baked terrain binds the authored layer material instead of local editor material");
		check(spawnTile->bakedTerrain && map.bakedTerrain && map.bakedTerrain->TileCount() == 1681,
			"catalog references the validated original baked stream");
		check(spawnTile->placement.pos == CVec3(0, 0, 0) && spawnTile->placement.scale == CVec3(1, 1, 1),
			"baked terrain world positions must not receive editor placement again");
		Render::AABB bounds;
		bounds.Set(spawnTile->localBounds);
		check(std::abs(bounds.center.z - 6.682475f) < .001f &&
			std::abs(bounds.halfSize.z - .247755f) < .001f,
			"local spawn ground uses the shipped deformed height bounds, not the flat editor mesh");
		if (map.bakedTerrain) {
			auto storage = std::make_shared<LinuxStaticMeshStorage>();
			const auto* geometry = CreateLinuxBakedTerrainGeometry(*storage, map.bakedTerrain, 1187);
			const auto* tile = map.bakedTerrain->GetTile(1187);
			check(geometry && geometry->primitives.size() == 1 && geometry->triangleCount == tile->triangleCount,
				"real baked tile builds one original visible triangle-list primitive");
			check(storage->terrainVertices && storage->terrainIndices &&
				storage->terrainVertices->storage.size() == tile->vertexCount * 44 &&
				storage->terrainIndices->storage.size() == tile->indexCount * 4 &&
				!std::memcmp(storage->terrainVertices->storage.begin(), map.bakedTerrain->VertexData(1187), tile->vertexCount * 44) &&
				!std::memcmp(storage->terrainIndices->storage.begin(), map.bakedTerrain->IndexData(1187), tile->indexCount * 4),
				"native buffers preserve every original position, UV, colour, mask and index byte");
			std::weak_ptr<LinuxStaticMeshStorage> weak = storage;
			std::shared_ptr<Render::StaticMesh> retained(storage, &storage->mesh);
			storage.reset();
			check(!weak.expired(), "retained mesh keeps its native terrain buffers alive");
			retained.reset();
			check(weak.expired(), "last retained mesh releases native terrain buffers and geometry");
		}
	}
	check(map.staticSceneObjectScanCount == 7478 && !map.staticSceneObjectScanLimitHit,
		"catalog traverses original objects beyond the former 2048 scan cap");
	check(catalog.terrain.size() == 1681 && catalog.statics.size() > 256 && catalog.animated.size() > 48,
		"discovery is independent of active render budgets");
	check(catalog.metadataDrops == 0 && catalog.candidates.size() <= LinuxBootstrap::SceneWindowLimits::catalogCandidates,
		"shipped metadata fits the catalog budget");
	check(map.staticDiffuseTextures.empty() && map.staticGeometryPayloads.empty(),
		"metadata discovery never decodes textures or publishes an arbitrary first window");
	LinuxWindowOverlay overlay;
	std::set<size_t> unionObjects;
	std::vector<size_t> firstSelection;
	for (const auto focus : {CVec2(384, 164), CVec2(205, 205), CVec2(40, 245)}) {
		LinuxBootstrap::SceneWindowRequest request;
		request.focusX = focus.x; request.focusY = focus.y;
		request.bounds = {focus.x - 55, focus.y - 55, focus.x + 55, focus.y + 55};
		check(UpdateLinuxMapSceneWindow(environment, request, &map, &overlay), "distant location publishes a new window");
		check(overlay.staticMapResources[0].Size() == 0 && overlay.staticMapResources[1].Size() == 0 &&
			overlay.animatedMapResources.Size() == 0, "every indexed cache cleared before new payload publication");
		check(!map.staticGeometryPayloads.empty() && map.staticGeometryPayloads.size() <= 256 &&
			!map.terrainElementPayloads.empty() && map.terrainElementPayloads.size() <= 192 &&
			map.animatedGeometryPayloads.size() <= 48 && map.staticDiffuseTextures.size() <= 96,
			"each real window obeys unchanged active budgets");
		check(map.sceneWindowMetrics.cumulativeDrops == 0, "three local windows stay within process input limits");
		const auto outer = request.bounds.Expanded(request.guardBand);
		bool overlaps = true, identity = true, late = false;
		size_t staticIndex = 0, terrainIndex = 0, animatedIndex = 0;
		for (const auto index : map.sceneWindow.Selected()) {
			const auto& candidate = catalog.candidates[index];
			overlaps = overlaps && candidate.bounds.Overlaps(outer);
			if (candidate.kind == LinuxBootstrap::SceneWindowKind::Static) {
				const auto& payload = map.staticGeometryPayloads[staticIndex++];
				const auto& original = catalog.statics[candidate.payloadIndex];
				identity = identity && payload.originalObjectIndex == candidate.objectIndex &&
					payload.bakedSceneHash == original.bakedSceneHash && payload.bakedComponentIndex == original.bakedComponentIndex &&
					payload.geometryFile == original.geometryFile && payload.hasBakedIdentity == original.hasBakedIdentity;
			} else if (candidate.kind == LinuxBootstrap::SceneWindowKind::Terrain) {
				identity = identity && map.terrainElementPayloads[terrainIndex++].originalObjectIndex == candidate.objectIndex;
			} else identity = identity && map.animatedGeometryPayloads[animatedIndex++].originalObjectIndex == candidate.objectIndex;
			if (candidate.kind != LinuxBootstrap::SceneWindowKind::Terrain) {
				late = late || candidate.objectIndex > 2048;
				unionObjects.insert(candidate.objectIndex);
			}
		}
		check(overlaps && identity && late, "overlapping original late-map identities survive sorting and remapping");
		const auto validTextures = [&](const auto& indices) {
			for (const auto index : indices)
				if (index != kLinuxHeroPreviewNoDiffuseTexture && index >= map.staticDiffuseTextures.size()) return false;
			return true;
		};
		bool remapped = true;
		for (const auto& payload : map.staticGeometryPayloads) remapped = validTextures(payload.materialDiffuseTextureIndices) && remapped;
		for (const auto& payload : map.terrainElementPayloads) remapped = validTextures(payload.materialDiffuseTextureIndices) && remapped;
		for (const auto& payload : map.terrainElementPayloads)
			for (const auto& faction : payload.terrainTextureIndices) remapped=validTextures(faction) && remapped;
		for (const auto& payload : map.animatedGeometryPayloads)
			for (const auto& skin : payload.skinGeometryPayloads) remapped = validTextures(skin.materialDiffuseTextureIndices) && remapped;
		check(remapped, "all active diffuse slots remap inside this window");
		overlay.ready=overlay.openglReady=true;
		check(PrepareLinuxTerrainNatureMask(overlay,map,nullptr,-1),"native pre-world neutral mask initialized");
		check(overlay.terrainNatureMask.Width()==656 && overlay.terrainNatureMask.Height()==656 &&
			overlay.terrainNatureMask.AllChannels()==1,"native mask uses original map resolution and neutral channel");
		const auto uploads=overlay.terrainNatureUploads;
		check(PrepareLinuxTerrainNatureMask(overlay,map,nullptr,-1) && overlay.terrainNatureUploads==uploads,
			"same world step never reuploads unchanged nature");
		if (focus.x==384) {
			const auto tile=std::find_if(map.terrainElementPayloads.begin(),map.terrainElementPayloads.end(),
				[](const auto& payload) { return payload.originalObjectIndex==1187; });
			check(tile!=map.terrainElementPayloads.end(),"spawn tile admitted with all faction textures");
			if (tile!=map.terrainElementPayloads.end()) {
				glPushAttrib(GL_ALL_ATTRIB_BITS);
				glViewport(0,0,128,128); glDisable(GL_SCISSOR_TEST); glDisable(GL_ALPHA_TEST);
				glDepthMask(GL_TRUE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
				glClearColor(.08f,.02f,.05f,1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
				GLint mode=0; glGetIntegerv(GL_MATRIX_MODE,&mode);
				glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-6,6,-6,6,-20,20);
				glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(90,1,0,0);
				std::vector<unsigned char> before(128*128*4),after(before.size());
				glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,before.data());
				const auto statistics=Render::LinuxBasicFx::GetStatistics();
				check(DrawLinuxBakedTerrainLayers(overlay,map,*tile,{385,165,6.682475f,1})==1,"actual Bank tile draws original ordinary shader");
				check(Render::LinuxBasicFx::GetStatistics().terrainDraws==statistics.terrainDraws+1 &&
					Render::LinuxBasicFx::GetStatistics().terrainRejected==statistics.terrainRejected,"engine terrain bridge submits without fallback");
				glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,after.data());
				size_t changed=0; std::set<unsigned> colors;
				for (size_t i=0; i<after.size(); i+=4) {
					if (std::memcmp(before.data()+i,after.data()+i,4)) ++changed;
					colors.insert(unsigned(after[i])|(unsigned(after[i+1])<<8)|(unsigned(after[i+2])<<16));
				}
				check(changed>1000 && colors.size()>16,"real shaded terrain survives culling and covers expected pixels");
				glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GLenum(mode)); glPopAttrib();
			}
		}
		bool hasSky = false;
		for (const auto& payload : map.staticGeometryPayloads)
			for (const auto& material : payload.materialReferences)
				hasSky = hasSky || dynamic_cast<const NDb::SkyMaterial*>(material.GetPtr()) != nullptr;
		check(!hasSky, "unported sky spheres must not occlude the hero as opaque local scenery");
		if (firstSelection.empty()) firstSelection = map.sceneWindow.Selected();
		else check(firstSelection != map.sceneWindow.Selected(), "distant views cannot reuse the old first scene");
		for (auto& cache : overlay.staticMapResources)
			cache.Get(0, 1, [] { return std::make_unique<LinuxStaticMapResource>(); });
		overlay.animatedMapResources.Get(0, 1, [] { return std::make_unique<LinuxAnimatedMapResource>(); });
		const auto builds = map.sceneWindowBuilds, revision = map.sceneWindow.Revision();
		const auto geometryBytes = LinuxMapSceneAssetLedger().GeometryBytes();
		for (unsigned frame = 0; frame < 100; ++frame)
			check(!UpdateLinuxMapSceneWindow(environment, request, &map, &overlay), "stationary window does no work");
		check(builds == map.sceneWindowBuilds && revision == map.sceneWindow.Revision() &&
			geometryBytes == LinuxMapSceneAssetLedger().GeometryBytes() && overlay.staticMapResources[0].Size() == 1,
			"stationary frames preserve counters, resources and input budget");
	}
	check(unionObjects.size() > 256, "camera traversal reaches more than the old fixed scene");
	DeleteOpenGlTexture(&overlay.terrainNatureTexture);
	for (auto& texture : overlay.mapPreviewDiffuseTextures) DeleteOpenGlTexture(&texture);
	fprintf(stdout, "Scene window engine: %u checks, %u failures, distinctObjects=%zu\n", checks, failures, unionObjects.size());
	return failures == 0;
}
