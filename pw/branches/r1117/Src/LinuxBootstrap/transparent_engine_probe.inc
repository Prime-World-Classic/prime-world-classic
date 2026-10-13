/** Real retained owners and BatchQueue draws, with independently calculated blended pixels. */
bool RunLinuxBasicFxTransparentProbe(const NDb::BasicFXMaterial& authored, const Render::MeshGeometry& quad,
	const NDb::DBStaticSceneComponent& component)
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char* name) {
		++checks; if (!good) ++failures;
		fprintf(stdout, "BasicFX transparent probe: %s result=%s\n", name, good ? "PASS" : "FAIL");
	};
	glPushAttrib(GL_ALL_ATTRIB_BITS); glViewport(0,0,64,64);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-4,4,-4,4,-10,10);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(-90,1,0,0);
	const auto clear = [&]() {
		glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1); glDepthMask(GL_TRUE);
		glClearColor(.1f,.2f,.3f,.4f); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	};
	const auto pixel = [&](unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
		unsigned char actual[4]{}; glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
		return std::abs(int(actual[0])-r) <= 2 && std::abs(int(actual[1])-g) <= 2 &&
			std::abs(int(actual[2])-b) <= 2 && std::abs(int(actual[3])-a) <= 2;
	};
	const auto makeMesh = [&](const Matrix43& world, Render::HDRColor color, NDb::BlendMode blend) {
		auto* material = new NDb::BasicFXMaterial(authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->renderState.blendMode = blend;
		material->renderState.culling = NDb::ONOFFSTATE_OFF;
		material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0,0,0,0));
		material->DiffuseAdd = decltype(material->DiffuseAdd)(color);
		vector<Render::BaseMaterial*> materials; materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material));
		auto mesh = std::make_unique<Render::StaticMesh>(); mesh->Initialize(world, &quad, materials); return mesh;
	};
	const auto enqueue = [&](LinuxStaticTransparentPass& pass, Render::HDRColor color, float z, unsigned group,
		bool validBounds = true, NDb::BlendMode blend = NDb::BLENDMODE_LERPBYALPHA) {
		Matrix43 world; Identity(&world); world._34 = z;
		NDb::AABB bounds; bounds.minX = bounds.minY = -3; bounds.maxX = bounds.maxY = 3;
		if (!validBounds) bounds.minX = std::numeric_limits<float>::quiet_NaN();
		auto* owner = pass.Retain(makeMesh(world, color, blend), world, bounds, group);
		if (!owner) return false;
		Render::BatchQueueSorter sorter(16, false);
		Render::BatchQueue local(Render::BatchQueue::INDEX_MAIN, sorter, 32); owner->mesh->RenderToQueue(local);
		unsigned deferred = 0;
		for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
			for (auto* batch = local.GetBatches(priority); batch; batch = batch->pNextBatch)
				if (pass.Defer(*owner, *batch, kLinuxHeroPreviewNoDiffuseTexture)) ++deferred;
		return deferred == 1;
	};
	const auto drawImmediate = [&](float z, Render::HDRColor color, bool opaque,
		NDb::BlendMode blend = NDb::BLENDMODE_LERPBYALPHA) {
		Matrix43 world; Identity(&world); world._34 = z;
		auto mesh = makeMesh(world, color, opaque ? NDb::BLENDMODE_OFF : blend);
		Render::BatchQueueSorter sorter(16, false); Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32);
		mesh->RenderToQueue(queue);
		Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true); queue.Render();
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
	};
	for (int reverse = 0; reverse < 2; ++reverse) {
		clear(); LinuxStaticTransparentPass pass(2, {});
		for (int i = 0; i < 2; ++i) {
			const int side = reverse ? 1-i : i;
			check(enqueue(pass, side ? Render::HDRColor(0,0,1,.5f) : Render::HDRColor(1,0,0,.5f), side ? 1 : 4, side),
				"retain owner across submitting helper/local queue destruction");
		}
		check(pixel(26,51,77,102), "collection draws no early pixels");
		check(pass.Flush(), "shared terrain/static-group queue flush");
		check(pixel(70,13,147,102), "far-to-near blend independent of payload-list submission order");
		check(pass.GetCounts(0).batches == 1 && pass.GetCounts(1).batches == 1 &&
			pass.GetCounts(0).payloads == 1 && pass.GetCounts(1).payloads == 1 &&
			pass.GetCounts(0).triangles == 2 && pass.GetCounts(1).triangles == 2,
			"deferred counters are attributed to actual draw groups");
		check(!pass.Flush() && !Render::LinuxBasicFx::IsSelected(), "flush is once-only and clears material selection");
	}
	{
		clear(); LinuxStaticTransparentPass pass(2, {});
		check(enqueue(pass, {1,0,0,.5f}, 4, 0) && enqueue(pass, {0,0,1,.5f}, 1, 1), "collect around later opaque unit depth");
		drawImmediate(2, {0,1,0,.25f}, true);
		float before = 0, after = 0; glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&before);
		check(pass.Flush() && pixel(0,128,128,64), "late opaque occluder blocks rear transparency but not foreground");
		glReadPixels(32,32,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&after);
		check(before == after, "transparent scene pass preserves opaque depth");
		glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glColor4f(1,1,0,1);
		// The view's camera maps X/Z to screen X/Y; use an identity overlay camera.
		glPushMatrix(); glLoadIdentity(); glBegin(GL_QUADS);
		glVertex3f(-1,-1,0); glVertex3f(1,-1,0); glVertex3f(1,1,0); glVertex3f(-1,1,0); glEnd(); glPopMatrix();
		check(pixel(255,255,0,255), "post-pass overlay remains in front");
	}
	{
		clear(); LinuxStaticTransparentPass pass(40, {});
		bool collected = true;
		for (unsigned i = 0; i < 40; ++i)
			collected = enqueue(pass, i % 2 ? Render::HDRColor(0,0,1,.5f) : Render::HDRColor(1,0,0,.5f), 1 + i*.1f, i%2) && collected;
		check(collected && pass.Flush(), "stable owners and queue capacity beyond old 32-batch local limit");
		check(pass.GetCounts(0).batches == 20 && pass.GetCounts(1).batches == 20 && pixel(170,0,85,102),
			"all grown retained owners draw in depth order");
	}
	{
		clear(); LinuxStaticTransparentPass pass(1, {});
		check(enqueue(pass, {1,0,0,.5f}, 1, 0, false) && pass.Flush(), "invalid bounds are consumed without opaque fallback");
		check(pass.GetCounts(0).rejected == 1 && !pass.GetCounts(0).batches && pixel(26,51,77,102), "invalid key does not reach sorting or drawing");
	}
	{
		clear(); LinuxStaticTransparentPass pass(1, {});
		check(enqueue(pass, {1,0,0,.5f}, 1, 0) && !enqueue(pass, {0,0,1,.5f}, 2, 1), "over-capacity owner cannot invalidate published batch links");
		check(pass.Flush() && pixel(140,26,38,102), "first retained owner survives rejected extra owner");
	}
	{
		clear(); LinuxStaticTransparentPass pass(1, {}); enqueue(pass, {1,0,0,.5f}, 1, 0);
		glViewport(0,0,32,32);
		check(!pass.Flush() && pixel(26,51,77,102) && !Render::LinuxBasicFx::IsSelected(), "view switch discards queued owners without drawing");
		glViewport(0,0,64,64);
	}
	{
		clear(); { LinuxStaticTransparentPass abandoned(1, {}); enqueue(abandoned, {1,0,0,.5f}, 1, 0); }
		LinuxStaticTransparentPass nextView(0, {});
		check(nextView.Flush() && pixel(26,51,77,102), "discard and empty next view never leak a previous queue");
		LinuxStaticTransparentPass oversized(std::numeric_limits<size_t>::max(), {});
		check(!oversized.Flush(), "oversized queue request rejected before capacity arithmetic/allocation");
	}
	{
		clear(); const auto before = Render::LinuxBasicFx::GetStatistics();
		drawImmediate(1, {1,0,0,.5f}, false);
		const auto after = Render::LinuxBasicFx::GetStatistics();
		check(after.draws == before.draws && after.rejected > before.rejected && pixel(26,51,77,102),
			"admitted transparency cannot bypass the retained scene pass");
	}
	struct BlendCase { NDb::BlendMode mode; unsigned char r, g, b; };
	const BlendCase modes[] = {
		{NDb::BLENDMODE_LERPBYALPHA,38,77,115}, {NDb::BLENDMODE_PREMULTIPLIEDLERP,64,128,191},
		{NDb::BLENDMODE_ADDCOLOR,77,153,230}, {NDb::BLENDMODE_ADDCOLORMULALPHA,51,102,153},
		{NDb::BLENDMODE_MULCOLOR,5,20,46}, {NDb::BLENDMODE_MULINVCOLOR,20,31,31}};
	for (const auto& mode : modes) {
		clear(); LinuxStaticTransparentPass pass(1, {});
		check(enqueue(pass, {.2f,.4f,.6f,.5f}, 1, 0, true, mode.mode) && pass.Flush(), "authored blend mode reaches shared pass");
		check(pixel(mode.r,mode.g,mode.b,102), "DB-to-backend blend mapping matches literal RGB oracle");
	}
	{
		clear(); LinuxStaticTransparentPass pass(1, {});
		const auto before = Render::LinuxBasicFx::GetStatistics();
		check(enqueue(pass, {1,0,0,.5f}, 1, 0, true, static_cast<NDb::BlendMode>(99)) && pass.Flush(),
			"malformed admitted blend cannot fall through to opaque preview");
		const auto after = Render::LinuxBasicFx::GetStatistics();
		check(after.draws == before.draws && after.rejected > before.rejected && pixel(26,51,77,102),
			"invalid blended descriptor is rejected at drawing");
	}
	{
		clear(); LinuxStaticTransparentPass pass(1, {}); enqueue(pass, {1,0,0,.5f}, 1, 0);
		unsigned boundaries = 0;
		DrawLinuxMapDynamicWorldMarkerPreview(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
			0,0,1,0, nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
			nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr, [&]() { ++boundaries; pass.Flush(); });
		check(boundaries == 1 && pixel(140,26,38,102), "missing dynamic world still flushes the map view exactly once");
	}
	for (int invalid = 0; invalid < 2; ++invalid) {
		clear(); LinuxWindowOverlay overlay; LinuxSelectedHeroDbPreview hero;
		auto* material = new NDb::BasicFXMaterial(authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->renderState.blendMode = invalid ? static_cast<NDb::BlendMode>(99) : NDb::BLENDMODE_LERPBYALPHA;
		LinuxHeroStaticGeometryPayloadPreview payload{};
		payload.rootGeometryFile = NormalizeRootFileSystemPath(component.geometryFileName.c_str());
		payload.materialReferences.push_back(NDb::Ptr<NDb::Material>(material));
		payload.materialDiffuseTextureIndices.push_back(kLinuxHeroPreviewNoDiffuseTexture);
		payload.localBounds = component.aabb; payload.locatorJointPlacement = false;
		hero.sceneAsset.staticGeometryPayloads.push_back(payload);
		size_t payloads = 0, triangles = 0; LinuxRendererMaterialSlotStats stats;
		const size_t batches = DrawLinuxHeroRendererStaticMeshPayloads(&overlay, &hero, nullptr, &stats, &payloads, &triangles, {});
		check(invalid ? !payloads && !batches && !triangles : payloads == 1 && batches == 1 && triangles > 0,
			"character payload hook counts successful deferred geometry only");
		clear(); LinuxSelectedMapPreview map;
		LinuxMapStaticGeometryPayloadPreview mapPayload{};
		mapPayload.rootGeometryFile = payload.rootGeometryFile; mapPayload.localBounds = component.aabb;
		mapPayload.materialReferences = payload.materialReferences;
		mapPayload.materialDiffuseTextureIndices = payload.materialDiffuseTextureIndices;
		map.staticGeometryPayloads.push_back(mapPayload);
		LinuxStaticTransparentPass pass(1, {});
		const size_t immediate = DrawLinuxMapRendererStaticMeshPayloadList(&overlay, &map, map.staticGeometryPayloads,
			0,0,1, &stats, &payloads, &triangles, pass, 1);
		check(!immediate && !payloads && !triangles && pass.Flush(), "map payload hook defers across its local scope");
		check(invalid ? !pass.GetCounts(1).triangles && pass.GetCounts(1).rejected == 1 :
			pass.GetCounts(1).payloads == 1 && pass.GetCounts(1).batches == 1 && pass.GetCounts(1).triangles > 0,
			"map payload hook attributes only accepted deferred geometry");
	}
	check(glGetError() == GL_NO_ERROR, "transparent engine fixtures leave no GL errors");
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glPopAttrib();
	fprintf(stdout, "BasicFX transparent summary: checks=%u failures=%u\n", checks, failures);
	return failures == 0;
}
