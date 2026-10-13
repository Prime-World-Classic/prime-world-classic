/**
 * Real material preparation and retained queues keep shader opacity separate from blending.
 * The original _applyOpacity.hlsl multiplies RGB for AdditiveOpacity, alpha for
 * BlendOpacity, and neither for Simple. MaterialSpec.inl supplies opacity*master
 * in PS c6.x; no oracle below reads native backend program choices or constants.
 */
bool RunLinuxBasicFxOpacityProbe(const NDb::BasicFXMaterial& authored, const Render::MeshGeometry& quad,
	const std::string& capturePath)
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout, "BasicFX opacity probe: %s result=%s\n", label, good ? "PASS" : "FAIL");
	};
	glPushAttrib(GL_ALL_ATTRIB_BITS); glViewport(0,0,64,64);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-4,4,-4,4,-10,10);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(-90,1,0,0);
	const auto savedDepthPin = Render::GetRuntimePins().DoDepthOutValue;
	Render::GetRuntimePins().DoDepthOutValue = NDb::BOOLPIN_FALSE;
	const auto loads = Render::LinuxBasicFx::GetStatistics().loads;
	const auto pixel = [&](const std::array<float, 4>& expected, int x = 32) {
		unsigned char actual[4]{}; glReadPixels(x,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
		for (size_t i = 0; i < 4; ++i)
			if (std::abs(int(actual[i])-int(std::round(std::max(0.f,std::min(1.f,expected[i]))*255))) > 2) return false;
		return true;
	};
	// Byte-aligned alpha keeps equality meaningful for GL's permitted fixed-point alpha test.
	const std::array<float, 4> background{{.1f,.2f,.3f,.4f}}, source{{.2f,.4f,.6f,128.f/255}};
	const auto clear = [&]() {
		glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1); glDepthMask(GL_TRUE);
		glClearColor(background[0],background[1],background[2],background[3]); glClearDepth(1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	};
	const auto draw = [&](NDb::OpacityAndEmissiveModePin pin, float opacity, float master,
		NDb::BlendMode blend, int alphaReference, bool valid, bool visible, bool depthOut = false) {
		fprintf(stdout, "BasicFX opacity case: pin=%d opacity=%g master=%g blend=%d alphaRef=%d depthOut=%d\n",
			static_cast<int>(pin), opacity, master, static_cast<int>(blend), alphaReference, depthOut);
		clear();
		auto* material = new NDb::BasicFXMaterial(authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->OpacityAndEmissiveModePinValue = pin;
		material->opacity = decltype(material->opacity)(opacity); material->masterOpacity = master;
		material->renderState.blendMode = blend; material->renderState.culling = NDb::ONOFFSTATE_OFF;
		material->renderState.alphaTest = alphaReference < 0 ? NDb::ONOFFSTATE_OFF : NDb::ONOFFSTATE_ON;
		material->renderState.alphaTestRef = alphaReference < 0 ? 127 : alphaReference;
		material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0,0,0,0));
		material->DiffuseAdd = decltype(material->DiffuseAdd)(Render::HDRColor(source[0],source[1],source[2],source[3]));
		const auto beforeSelect = Render::LinuxBasicFx::GetStatistics();
		Render::LinuxBasicFx::Select(material, nullptr);
		const auto afterSelect = Render::LinuxBasicFx::GetStatistics();
		check(Render::LinuxBasicFx::IsSelected() && afterSelect.selections == beforeSelect.selections+1
			&& afterSelect.unsupported == beforeSelect.unsupported,
			"constant family remains selected including malformed numeric values and invalid pin");
		check(Render::LinuxBasicFx::IsTransparentMaterial(material) == (blend != NDb::BLENDMODE_OFF),
			"retained-pass classification follows authored blend, not opacity pin or value");
		Matrix43 world; Identity(&world);
		vector<Render::BaseMaterial*> materials; materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material));
		auto mesh = std::make_unique<Render::StaticMesh>(); mesh->Initialize(world, &quad, materials);
		const auto before = Render::LinuxBasicFx::GetStatistics();
		Render::GetRuntimePins().DoDepthOutValue = depthOut ? NDb::BOOLPIN_TRUE : NDb::BOOLPIN_FALSE;
		if (blend == NDb::BLENDMODE_OFF) {
			Render::BatchQueueSorter sorter(16, false);
			Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32); mesh->RenderToQueue(queue);
			Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true); queue.Render();
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		} else {
			LinuxStaticTransparentPass pass(1, {});
			NDb::AABB bounds; bounds.minX = bounds.minY = -3; bounds.maxX = bounds.maxY = 3;
			auto* owner = pass.Retain(std::move(mesh), world, bounds, 0);
			unsigned deferred = 0;
			if (owner) {
				Render::BatchQueueSorter sorter(16, false);
				Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32); owner->mesh->RenderToQueue(queue);
				for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
					for (auto* batch = queue.GetBatches(priority); batch; batch = batch->pNextBatch)
						if (pass.Defer(*owner, *batch, kLinuxHeroPreviewNoDiffuseTexture)) ++deferred;
			}
			check(deferred == 1 && pass.Flush(), "opacity material survives submitting queue destruction");
		}
		Render::GetRuntimePins().DoDepthOutValue = NDb::BOOLPIN_FALSE;
		const auto after = Render::LinuxBasicFx::GetStatistics();
		check(after.unsupported == before.unsupported && (valid ?
			after.draws == before.draws+1 && after.rejected == before.rejected :
			after.draws == before.draws && after.rejected > before.rejected),
			"actual draw/rejection counters match opacity admission without unsupported fallback");
		auto expected = background;
		if (valid && visible) {
			auto shaded = source;
			if (pin == NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY)
				for (size_t i = 0; i < 3; ++i) shaded[i] *= opacity*master;
			else if (pin == NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY) shaded[3] *= opacity*master;
			if (blend == NDb::BLENDMODE_OFF) expected = shaded;
			else for (size_t i = 0; i < 3; ++i)
				expected[i] = blend == NDb::BLENDMODE_LERPBYALPHA ?
					shaded[i]*shaded[3] + background[i]*(1-shaded[3]) : shaded[i]+background[i];
		}
		check(pixel(expected), "independent source-RGB/source-alpha oracle matches framebuffer");
	};
	for (float opacity : {0.f,.25f,1.f,1.1f,-.5f})
		for (const auto pin : {NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE, NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,
			NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY})
			draw(pin,opacity,1,NDb::BLENDMODE_OFF,-1,true,true);
	for (float master : {.5f,2.f,.25f,1.f}) {
		draw(NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,.5f,master,NDb::BLENDMODE_LERPBYALPHA,-1,true,true);
		draw(NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY,.5f,master,NDb::BLENDMODE_ADDCOLOR,-1,true,true);
	}
	for (const auto pin : {NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE, NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,
		NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY})
		draw(pin,0,.5f,NDb::BLENDMODE_LERPBYALPHA,-1,true,true);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY,.5f,1,NDb::BLENDMODE_OFF,63,true,true);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY,.5f,1,NDb::BLENDMODE_OFF,64,true,true);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY,.5f,1,NDb::BLENDMODE_OFF,65,true,false);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,.5f,1,NDb::BLENDMODE_OFF,127,true,true);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,.5f,1,NDb::BLENDMODE_OFF,128,true,true);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,.5f,1,NDb::BLENDMODE_OFF,129,true,false);
	for (const auto pin : {NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE, NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY,
		NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY}) {
		for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
			draw(pin,invalid,1,NDb::BLENDMODE_OFF,-1,false,false);
			draw(pin,1,invalid,NDb::BLENDMODE_LERPBYALPHA,-1,false,false);
		}
		// Simple ignores the product, but each authored operand must still be finite.
		const bool simple = pin == NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE;
		draw(pin,std::numeric_limits<float>::max(),2,NDb::BLENDMODE_OFF,-1,simple,simple);
		draw(pin,2,-std::numeric_limits<float>::max(),NDb::BLENDMODE_LERPBYALPHA,-1,simple,simple);
		for (const auto blend : {NDb::BLENDMODE_OFF, NDb::BLENDMODE_LERPBYALPHA})
			draw(pin,.25f,.5f,blend,-1,false,false,true);
		// A valid draw immediately after each rejected family verifies selection recovery.
		draw(pin,.25f,.5f,NDb::BLENDMODE_OFF,-1,true,true);
	}
	for (int pin : {-1,99})
		for (const auto blend : {NDb::BLENDMODE_OFF, NDb::BLENDMODE_LERPBYALPHA})
			draw(static_cast<NDb::OpacityAndEmissiveModePin>(pin),1,1,blend,-1,false,false);
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE,.25f,.5f,NDb::BLENDMODE_OFF,127,true,true);
	// Matching shader families must not make BatchQueue reuse another material's c6.
	for (const auto pin : {NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY, NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY})
	for (const auto blend : {NDb::BLENDMODE_OFF, NDb::BLENDMODE_LERPBYALPHA})
	for (bool reverse : {false,true}) {
		fprintf(stdout, "BasicFX opacity queued case: pin=%d blend=%d reverse=%d\n",
			static_cast<int>(pin), static_cast<int>(blend), reverse);
		clear();
		Render::BatchQueueSorter sorter(16, false);
		Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32);
		LinuxStaticTransparentPass pass(2, {});
		std::vector<std::unique_ptr<Render::StaticMesh>> opaqueOwners;
		unsigned deferred = 0;
		for (int submission = 0; submission < 2; ++submission) {
			const int side = reverse ? 1-submission : submission;
			auto* material = new NDb::BasicFXMaterial(authored);
			NDb::Ptr<NDb::BasicFXMaterial> held(material);
			material->OpacityAndEmissiveModePinValue = pin;
			material->opacity = decltype(material->opacity)(side ? .75f : .25f);
			material->masterOpacity = side ? 1.f : .5f;
			material->renderState.blendMode = blend;
			material->renderState.culling = NDb::ONOFFSTATE_OFF;
			material->renderState.alphaTest = NDb::ONOFFSTATE_OFF;
			material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0,0,0,0));
			material->DiffuseAdd = decltype(material->DiffuseAdd)(Render::HDRColor(source[0],source[1],source[2],source[3]));
			Matrix43 world; Identity(&world); world._11 = world._22 = .35f; world._14 = side ? 2 : -2;
			vector<Render::BaseMaterial*> materials;
			materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material));
			auto mesh = std::make_unique<Render::StaticMesh>(); mesh->Initialize(world, &quad, materials);
			if (blend == NDb::BLENDMODE_OFF) {
				mesh->RenderToQueue(queue); opaqueOwners.push_back(std::move(mesh));
			} else {
				NDb::AABB bounds; bounds.minX = bounds.minY = -3; bounds.maxX = bounds.maxY = 3;
				auto* owner = pass.Retain(std::move(mesh), world, bounds, side);
				if (owner) {
					Render::BatchQueueSorter localSorter(16, false);
					Render::BatchQueue local(Render::BatchQueue::INDEX_MAIN, localSorter, 32);
					owner->mesh->RenderToQueue(local);
					for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
						for (auto* batch = local.GetBatches(priority); batch; batch = batch->pNextBatch)
							if (pass.Defer(*owner, *batch, kLinuxHeroPreviewNoDiffuseTexture)) ++deferred;
				}
			}
		}
		check(pixel(background,16) && pixel(background,48), "same-family collection has not drawn either queued material");
		const auto before = Render::LinuxBasicFx::GetStatistics();
		if (blend == NDb::BLENDMODE_OFF) {
			Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true); queue.Render();
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		} else {
			check(deferred == 2 && pass.Flush(), "same-family retained owners outlive both submitting queues");
			check(pass.GetCounts(0).batches == 1 && pass.GetCounts(1).batches == 1,
				"same-family retained batch accounting keeps both owners");
		}
		const auto after = Render::LinuxBasicFx::GetStatistics();
		check(after.draws == before.draws+2 && after.rejected == before.rejected && after.unsupported == before.unsupported,
			"both same-family materials draw without rejection or fallback");
		for (int side = 0; side < 2; ++side) {
			auto shaded = source;
			const float expectedC6 = side ? .75f : .125f;
			if (pin == NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY)
				for (size_t i = 0; i < 3; ++i) shaded[i] *= expectedC6;
			else shaded[3] *= expectedC6;
			auto expected = shaded;
			if (blend == NDb::BLENDMODE_LERPBYALPHA) {
				for (size_t i = 0; i < 3; ++i) expected[i] = shaded[i]*shaded[3] + background[i]*(1-shaded[3]);
				expected[3] = background[3];
			}
			check(pixel(expected, side ? 48 : 16), side ?
				"right queued material retains its own opacity and master constants" :
				"left queued material retains its own opacity and master constants");
		}
		queue.Clear();
	}
	// Copy a real Animated track into an otherwise admitted descriptor to isolate
	// the animation gate; changing only the emissive pin isolates that gate too.
	const auto animated = NDb::Get<NDb::BasicFXMaterial>(NDb::DBID("Heroes/IronKnight/FXChanEye.MTRL"));
	check(animated && animated->opacity.IsAnimated(), "shipped FXChanEye provides an actual animated opacity track");
	for (int unsupported = 0; unsupported < 4; ++unsupported) {
		auto* material = new NDb::BasicFXMaterial(authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->renderState.blendMode = NDb::BLENDMODE_LERPBYALPHA;
		material->opacity = decltype(material->opacity)(1.f); material->masterOpacity = 1;
		material->OpacityAndEmissiveModePinValue = NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE;
		Render::LinuxBasicFx::Select(material, nullptr);
		check(Render::LinuxBasicFx::IsSelected() && Render::LinuxBasicFx::IsTransparentMaterial(material),
			"unsupported case starts with an admitted selected constant material");
		if (unsupported == 3) material->OpacityAndEmissiveModePinValue = NDb::OPACITYANDEMISSIVEMODEPIN_EMISSIVE;
		else {
			if (!animated) continue;
			const NDb::OpacityAndEmissiveModePin pins[] = {NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE,
				NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY, NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY};
			material->OpacityAndEmissiveModePinValue = pins[unsupported]; material->opacity = animated->opacity;
		}
		const auto before = Render::LinuxBasicFx::GetStatistics();
		check(!Render::LinuxBasicFx::IsTransparentMaterial(material),
			"animated and emissive descriptors remain outside the retained constant-opacity family");
		Render::LinuxBasicFx::Select(material, nullptr);
		const auto after = Render::LinuxBasicFx::GetStatistics();
		check(!Render::LinuxBasicFx::IsSelected() && after.unsupported == before.unsupported+1
			&& after.selections == before.selections && after.draws == before.draws && after.rejected == before.rejected,
			"unsupported animated or emissive selection clears prior state without masquerading as malformed admission");
	}
	draw(NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY,.75f,.5f,NDb::BLENDMODE_LERPBYALPHA,-1,true,true);
	// Evidence uses the resolved shipped geometry and its own material-slot map,
	// never Flat03's material pasted onto the mock quad or onto another mesh.
	const auto flatScene = NDb::Get<NDb::DBSceneObject>(NDb::DBID("Terrain/Minor/Flat03.SOBJ"));
	NDb::Ptr<NDb::DBStaticSceneComponent> flatComponent;
	if (flatScene)
		for (int i = 0; i < flatScene->attached.size(); ++i) {
			const auto* candidate = dynamic_cast<const NDb::DBStaticSceneComponent*>(flatScene->attached[i].component.GetPtr());
			if (candidate && candidate->geometryFileName == "/Terrain/Minor/Flat03.stat") { flatComponent = candidate; break; }
		}
	const auto* flatMaterial = flatComponent && flatComponent->materialsReferences.size() > 1 ?
		dynamic_cast<const NDb::BasicFXMaterial*>(flatComponent->materialsReferences[1].GetPtr()) : nullptr;
	const auto* flatTexture = flatMaterial ? dynamic_cast<const NDb::Texture*>(flatMaterial->DiffuseMap.texture.GetPtr()) : nullptr;
	check(flatMaterial && flatMaterial->OpacityAndEmissiveModePinValue == NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY
		&& !flatMaterial->opacity.IsAnimated() && flatMaterial->opacity.GetValue(0) == 1.1f
		&& flatMaterial->masterOpacity == 1 && flatMaterial->renderState.blendMode == NDb::BLENDMODE_LERPBYALPHA
		&& flatTexture && flatTexture->textureFileName == "/Terrain/Minor/Flat03.dds",
		"resolved Flat03 slot 1 retains authored BlendOpacity 1.1, blend state and original texture");
	const auto* flatGeometry = flatComponent ? Render::RenderResourceManager::LoadStaticMeshGeometry(
		nstl::string(NormalizeRootFileSystemPath(flatComponent->geometryFileName.c_str()).c_str()), false) : nullptr;
	bool mapped = flatGeometry && !flatGeometry->primitives.empty()
		&& static_cast<size_t>(flatGeometry->primitives.size()) <= sizeof(flatGeometry->materialID)/sizeof(flatGeometry->materialID[0]);
	unsigned opacityPrimitives = 0;
	if (mapped) for (int i = 0; i < flatGeometry->primitives.size(); ++i) {
		const int slot = flatGeometry->materialID[i];
		if (slot < 0 || slot >= flatComponent->materialsReferences.size() || !flatComponent->materialsReferences[slot]) mapped = false;
		else if (flatComponent->materialsReferences[slot].GetPtr() == flatMaterial) ++opacityPrimitives;
	}
	check(mapped && flatMaterial && opacityPrimitives > 0,
		"shipped Flat03 geometry resolves real primitives bound to its authored opacity material");
	if (mapped && flatMaterial && opacityPrimitives) {
		clear();
		// View the authored plane's front face; do not override its original back-face culling.
		glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(-60,1,0,0); glRotatef(20,0,1,0);
		{
			Matrix43 world; Identity(&world);
			vector<Render::BaseMaterial*> materials;
			for (int i = 0; i < flatGeometry->primitives.size(); ++i)
				materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(
					flatComponent->materialsReferences[flatGeometry->materialID[i]].GetPtr()));
			auto mesh = std::make_unique<Render::StaticMesh>(); mesh->Initialize(world, flatGeometry, materials);
			LinuxStaticTransparentPass pass(1, {});
			auto* owner = pass.Retain(std::move(mesh), world, flatComponent->aabb, 0);
			unsigned deferred = 0, sibling = 0;
			if (owner) {
				Render::BatchQueueSorter sorter(16, false);
				Render::BatchQueue local(Render::BatchQueue::INDEX_MAIN, sorter, 32); owner->mesh->RenderToQueue(local);
				for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
					for (auto* batch = local.GetBatches(priority); batch; batch = batch->pNextBatch) {
						if (pass.Defer(*owner, *batch, kLinuxHeroPreviewNoDiffuseTexture)) {
							++deferred;
							check(batch->pMaterial->GetDBMaterial() == flatMaterial,
								"Flat03 deferred batch keeps the resolved original material, not a probe replacement");
						} else ++sibling;
					}
			}
			check(deferred == opacityPrimitives && sibling > 0,
				"Flat03 collection admits its opacity slot without claiming its BasicMaterial sibling");
			const auto before = Render::LinuxBasicFx::GetStatistics();
			check(pass.Flush(), "shipped Flat03 opacity primitives survive submitting queue destruction");
			const auto after = Render::LinuxBasicFx::GetStatistics();
			check(after.draws == before.draws+opacityPrimitives && after.rejected == before.rejected
				&& after.unsupported == before.unsupported && pass.GetCounts(0).batches == opacityPrimitives
				&& pass.GetCounts(0).payloads == 1 && pass.GetCounts(0).triangles > 0,
				"shipped Flat03 opacity slot reaches native shader drawing with real geometry counters");
			std::vector<unsigned char> pixels(64*64*4);
			glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
			unsigned changed = 0;
			for (size_t i = 0; i < pixels.size(); i += 4)
				if (std::abs(int(pixels[i])-26) > 2 || std::abs(int(pixels[i+1])-51) > 2 ||
					std::abs(int(pixels[i+2])-77) > 2) ++changed;
			check(changed > 0, "shipped Flat03 opacity geometry and texture produce nonblank pixels");
			fprintf(stdout, "BasicFX opacity Flat03 evidence: primitives=%u deferred=%u sibling=%u triangles=%lu changedPixels=%u\n",
				opacityPrimitives, deferred, sibling, static_cast<unsigned long>(pass.GetCounts(0).triangles), changed);
			if (!capturePath.empty()) {
				std::string error;
				check(CaptureLinuxOpenGlBackBufferPng(capturePath + "-opacity-flat03.png",64,64,&error),
					"capture the shipped opacity primitive without its unsupported sibling");
				if (!error.empty()) fprintf(stderr, "Flat03 opacity capture: %s\n", error.c_str());
			}
		}
		glPopMatrix();
	}
	check(Render::LinuxBasicFx::GetStatistics().loads == loads, "material switching never reloads the shader family");
	check(glGetError() == GL_NO_ERROR, "opacity fixtures leave no GL errors");
	Render::LinuxBasicFx::ClearSelection();
	Render::GetRuntimePins().DoDepthOutValue = savedDepthPin;
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glPopAttrib();
	fprintf(stdout, "BasicFX opacity summary: checks=%u failures=%u\n", checks, failures);
	return failures == 0;
}
