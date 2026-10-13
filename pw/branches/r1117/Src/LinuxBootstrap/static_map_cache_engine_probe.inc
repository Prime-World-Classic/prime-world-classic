/** Exercise the real map hooks; cached meshes must outlive both local queues and cache eviction. */
bool RunLinuxStaticMapCacheProbe(const NDb::BasicFXMaterial& authored, const NDb::DBStaticSceneComponent& component)
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char* name) {
		++checks; if (!good) ++failures;
		fprintf(stdout, "Static map cache probe: %s result=%s\n", name, good ? "PASS" : "FAIL");
	};
	glPushAttrib(GL_ALL_ATTRIB_BITS); glViewport(0,0,64,64);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-7,7,-7,7,-30,30);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(60,1,0,0); glRotatef(20,0,1,0);
	const auto clear = [&]() {
		glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1); glDepthMask(GL_TRUE);
		glClearColor(0,0,0,1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	};
	const auto read = [&]() {
		std::array<unsigned char, 64*64*4> pixels{};
		glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
		return pixels;
	};
	const auto peek = [](LinuxWindowOverlay& overlay, unsigned group) -> LinuxStaticMapResource* {
		auto& cache = overlay.staticMapResources[group];
		return cache.Size() ? cache.Get(0,1,[]() { return std::unique_ptr<LinuxStaticMapResource>(); }) : nullptr;
	};
	for (bool transparent : {false, true}) {
		LinuxWindowOverlay overlay; LinuxSelectedMapPreview map;
		auto* material = new NDb::BasicFXMaterial(authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->renderState.blendMode = transparent ? NDb::BLENDMODE_LERPBYALPHA : NDb::BLENDMODE_OFF;
		material->renderState.culling = NDb::ONOFFSTATE_OFF;
		material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0,0,0,0));
		material->DiffuseAdd = decltype(material->DiffuseAdd)(Render::HDRColor(.8f,.1f,.2f,.5f));
		LinuxMapStaticGeometryPayloadPreview payload{};
		payload.rootGeometryFile = NormalizeRootFileSystemPath(component.geometryFileName.c_str());
		payload.localBounds = component.aabb;
		payload.materialReferences.push_back(NDb::Ptr<NDb::Material>(material));
		payload.materialDiffuseTextureIndices.push_back(kLinuxHeroPreviewNoDiffuseTexture);
		map.staticGeometryPayloads.push_back(payload); map.terrainElementPayloads.push_back(payload);
		const auto collect = [&](LinuxStaticTransparentPass& pass) {
			LinuxRendererMaterialSlotStats stats; size_t payloads = 0, triangles = 0;
			const size_t terrain = DrawLinuxMapRendererTerrainElementPayloads(&overlay,&map,0,0,1,&stats,&payloads,&triangles,pass);
			check(transparent ? !terrain && !payloads && !triangles : terrain == 1 && payloads == 1 && triangles > 0,
				"terrain hook preserves immediate/deferred counters");
			const size_t objects = DrawLinuxMapRendererStaticMeshPayloads(&overlay,&map,0,0,1,&stats,&payloads,&triangles,pass);
			check(transparent ? !objects && !payloads && !triangles : objects == 1 && payloads == 1 && triangles > 0,
				"static-object hook preserves immediate/deferred counters");
		};
		std::array<unsigned char, 64*64*4> first{};
		for (unsigned frame = 0; frame < 3; ++frame) {
			clear(); LinuxStaticTransparentPass pass(2, {}); collect(pass);
			check(pass.Flush(), "each frame owns a fresh once-only transparent pass");
			const auto pixels = read();
			if (!frame) first = pixels;
			unsigned covered = 0;
			for (size_t i = 0; i < pixels.size(); i += 4) if (pixels[i] || pixels[i+1] || pixels[i+2]) ++covered;
			check(covered > 10 && pixels == first, "cached and first-frame geometry produce identical nonblank pixels");
			for (unsigned group = 0; group < 2; ++group)
				check(overlay.staticMapResources[group].builds == 1 && overlay.staticMapResources[group].hits == frame,
					"each map group builds its mesh/material once and reuses it on later frames");
		}
		auto* terrain = peek(overlay,0); auto* object = peek(overlay,1);
		check(terrain && object && terrain->mesh && object->mesh && terrain->mesh != object->mesh,
			"terrain and static-object slots never alias despite identical indices and counts");
		clear();
		{
			LinuxStaticTransparentPass pass(2, {}); collect(pass);
			std::weak_ptr<Render::StaticMesh> firstMesh, secondMesh;
			if (terrain) firstMesh = terrain->mesh;
			if (object) secondMesh = object->mesh;
			for (auto& cache : overlay.staticMapResources) cache.Clear();
			check(!firstMesh.expired() && !secondMesh.expired(), "pending view retains meshes after map cache invalidation");
			check(pass.Flush() && read() == first, "eviction cannot remove queued geometry or change its pixels");
			check(firstMesh.expired() && secondMesh.expired(), "flushing releases the last retained mesh owners");
		}
		// Replacing a map with the same number of payloads must rebuild both groups.
		clear();
		{
			LinuxStaticTransparentPass pass(2, {}); collect(pass);
			check(overlay.staticMapResources[0].builds == 2 && overlay.staticMapResources[1].builds == 2,
				"same-sized map replacement rebuilds mesh/material resources after explicit clear");
			std::weak_ptr<Render::StaticMesh> borrowed;
			if (auto* entry = peek(overlay,1)) borrowed = entry->mesh;
			for (auto& cache : overlay.staticMapResources) cache.Clear();
			glViewport(0,0,32,32);
			check(!pass.Flush() && borrowed.expired(), "view mismatch discards retained resources without a draw");
			glViewport(0,0,64,64);
		}
		map.staticGeometryPayloads[0].rootGeometryFile = "missing-static-cache-fixture.stat";
		const auto before = overlay.staticMapResources[1].builds;
		for (unsigned frame = 0; frame < 2; ++frame) {
			LinuxStaticTransparentPass pass(1, {}); LinuxRendererMaterialSlotStats stats;
			size_t payloads = 99, triangles = 99;
			check(!DrawLinuxMapRendererStaticMeshPayloads(&overlay,&map,0,0,1,&stats,&payloads,&triangles,pass) &&
				!payloads && !triangles && pass.Flush(), "missing mesh stays undrawn without stale material reuse");
		}
		check(overlay.staticMapResources[1].builds == before+1 && overlay.staticMapResources[1].failures == 1,
			"missing mesh load is cached until explicit map invalidation");
	}
	check(glGetError() == GL_NO_ERROR, "cache fixtures leave no GL errors");
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glPopAttrib();
	fprintf(stdout, "Static map cache summary: checks=%u failures=%u\n", checks, failures);
	return failures == 0;
}
