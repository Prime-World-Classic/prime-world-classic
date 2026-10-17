/** Original Demon2 asset/GL proof, included after the preview draw functions and graph bank.
 * Marker IDs identify presentation owners only, not registered world objects.
 * This covers the base scene and creep idle/move graphs, not skins, world ownership,
 * spawn effects, ordinary corpse lifetime or the unsupported unsummon presentation.
 * The production map-unit draw uses its zero-height fallback for isolated asset framing.
 * A nonempty capturePath retains original idle/move framebuffer PNGs with summon suffixes.
 */
static bool RunLinuxSummonPresentationEngineProbe(const LinuxClientEnvironment& environment, const std::string& capturePath)
{
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool good, const char* label) {
		++checks;
		if (!good) ++failures;
		fprintf(stdout, "Summon presentation probe: %s result=%s\n", label, good ? "PASS" : "FAIL");
		return good;
	};
	const auto finish = [&]() {
		fprintf(stdout, "Summon presentation summary: checks=%u failures=%u scope=base-scene-asset-only draw=map-unit terrain=zero-height-fallback world-ownership=not-tested unsummon=not-tested\n",
			checks, failures);
		return failures == 0;
	};
	if (!check(glGetString(GL_VERSION) != nullptr, "current native OpenGL context")) return finish();

	const NDb::DBID unitId("/Summons/Demon2/_.SUMMONED");
	const NDb::DBID sceneId("/Summons/Demon2/_.SOBJ");
	const auto unit = NDb::Get<NDb::Summoned>(unitId);
	const auto scene = NDb::Get<NDb::DBSceneObject>(sceneId);
	if (!check(unit && scene && unit->GetDBID() == unitId && scene->GetDBID() == sceneId &&
		unit->sceneObject.GetPtr() == scene.GetPtr(), "original Demon2 descriptor owns the exact base scene")) return finish();
	const auto root = NDb::SessionRoot::GetRoot();
	const auto creepSet = NDb::Get<NDb::AnimSet>(NDb::DBID("/GameLogic/AnimSets/Creep.ANS"));
	if (!check(root && root->visualRoot && creepSet &&
		root->visualRoot->animSets.sets.size() > NDb::ANIMSETID_CREEP &&
		root->visualRoot->animSets.sets[NDb::ANIMSETID_CREEP].GetPtr() == creepSet.GetPtr(),
		"session ANIMSETID_CREEP resolves the original creep state map")) return finish();

	LinuxSelectedHeroDbPreview preview;
	const std::string unitDbid = ToStdString(unit->GetDBID().GetFormatted());
	const std::string sceneDbid = ToStdString(scene->GetDBID().GetFormatted());
	ProbeLinuxDynamicWorldUnitMeshPreview(environment, unitDbid, sceneDbid,
		NWorld::LinuxDynamicWorldMarker::KIND_SUMMON, -1, &preview);
	const auto& asset = preview.sceneAsset;
	if (!check(preview.found && preview.dbid == unitDbid && preview.persistentId == sceneDbid &&
		asset.dbid == sceneDbid && IsLinuxHeroMeshDbPreviewRenderableForMap(preview),
		"dynamic-unit loader retains exact original unit and scene identities")) return finish();
	if (!check(asset.animatedComponentCount == 1 && asset.animationComponent &&
		asset.skeletonPreview.ready && asset.animationPreview.ready && asset.animationSkinningPreview.ready &&
		asset.animationSkinningPreview.jointsCount > 0 && asset.meshPreview.skinningTriangleCount > 0,
		"original single-skeleton mesh and animation payloads are supported")) return finish();

	std::vector<bool> usedTextures(asset.skinDiffuseTextures.size(), false);
	size_t texturedTriangles = 0;
	const auto& vertices = asset.meshPreview.triangleVertices;
	for (size_t i = 0; i + 2 < vertices.size(); i += 3)
	{
		const auto& a = vertices[i];
		const auto& b = vertices[i + 1];
		const auto& c = vertices[i + 2];
		const size_t texture = a.diffuseTextureIndex;
		if (!a.texCoordValid || !b.texCoordValid || !c.texCoordValid ||
			texture != b.diffuseTextureIndex || texture != c.diffuseTextureIndex || texture >= usedTextures.size()) continue;
		usedTextures[texture] = true;
		++texturedTriangles;
	}
	bool textureAssetsReady = texturedTriangles > 0;
	for (size_t i = 0; i < usedTextures.size(); ++i)
		if (usedTextures[i]) textureAssetsReady &= asset.skinDiffuseTextures[i].artworkLoaded &&
			!asset.skinDiffuseTextures[i].sourceFile.empty();
	if (!check(textureAssetsReady, "mesh UVs reference decoded original diffuse textures")) return finish();

	const float meshHeight = asset.meshPreview.maxZ - asset.meshPreview.minZ;
	if (!check(std::isfinite(meshHeight) && meshHeight > 0,
		"authored mesh height permits finite map-unit framing")) return finish();
	const float mapScale = 5.2f / std::max(.1f, meshHeight);
	// A different working-set entry may share the same DB resources without sharing ownership.
	LinuxHeroSceneAssetPreview unrelatedAsset = asset;
	const auto ownedBank = std::make_shared<LinuxAnimationGraphBank>();
	auto& bank = *ownedBank;
	int worldToken = 0;
	auto sceneToken = std::make_shared<int>(1);
	NWorld::LinuxDynamicWorldMarker idleMarker;
	idleMarker.objectId = 1;
	idleMarker.kind = NWorld::LinuxDynamicWorldMarker::KIND_SUMMON;
	idleMarker.unitDbid = unit->GetDBID().GetFormatted();
	idleMarker.sceneObjectDbid = scene->GetDBID().GetFormatted();
	idleMarker.moveSpeed = 5;
	auto movingMarker = idleMarker;
	movingMarker.objectId = 2;
	movingMarker.moving = true;
	bank.Begin(&worldToken, sceneToken, 0);
	auto* idle = bank.Resolve(asset, idleMarker);
	auto* moving = bank.Resolve(asset, movingMarker);
	if (!check(idle && moving && idle != moving && bank.Size() == 2 && bank.created == 2 && bank.unsupported == 0,
		"two independent summon graph owners share one original asset")) return finish();
	bank.Begin(&worldToken, sceneToken, .4);
	if (!check(bank.Resolve(asset, idleMarker) == idle && bank.Resolve(asset, movingMarker) == moving &&
		idle->LastSampleTime() == .4 && moving->LastSampleTime() == .4,
		"both original creep graphs advance to the same sample time")) return finish();
	const auto mapped = [&](const LinuxAnimationGraphPlayback* player, NDb::EAnimStates state) {
		const int node = player->TargetNode();
		return creepSet->states.size() > state && asset.animationComponent->animGraph && node >= 0 &&
			node < asset.animationComponent->animGraph->macroNodes.size() &&
			asset.animationComponent->animGraph->macroNodes[node].nodeName == creepSet->states[state].commonNode;
	};
	if (!check(mapped(idle, NDb::ANIMSTATES_IDLE) && mapped(moving, NDb::ANIMSTATES_MOVE),
		"summon graph targets use authored creep idle and move aliases")) return finish();
	const auto samePose = [](const std::vector<Matrix43>& a, const std::vector<Matrix43>& b) {
		return !a.empty() && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Matrix43)) == 0;
	};
	if (!check(idle->Matrices().size() == asset.animationSkinningPreview.jointsCount &&
		moving->Matrices().size() == idle->Matrices().size() && !samePose(idle->Matrices(), moving->Matrices()),
		"idle and moving owners publish distinct complete original skeleton poses")) return finish();
	const auto idlePose = idle->Matrices();
	const auto idleUpdates = idle->Updates();
	const auto movingUpdates = moving->Updates();

	// Host buffers/programs are not rebound: reject incompatible contexts before changing GL state.
	GLint viewport[4] = {}, program = 0, drawFramebuffer = 0, readFramebuffer = 0;
	GLint packBuffer = 0, unpackBuffer = 0, textureUnits = 0;
	GLboolean doubleBuffered = GL_FALSE;
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &textureUnits);
	glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
	if (!check(glGetError() == GL_NO_ERROR && viewport[2] >= 256 && viewport[3] >= 256 && doubleBuffered &&
		!program && !drawFramebuffer && !readFramebuffer && !packBuffer && !unpackBuffer &&
		textureUnits > 0 && textureUnits <= 32, "compatible double-buffered GL target without foreign buffer bindings")) return finish();

	{
		LinuxWindowOverlay overlay;
		overlay.ready = overlay.openglReady = true;
		overlay.unitAnimationGraphs = ownedBank;
		LinuxSelectedMapPreview map;
		/** Restore host state and delete only textures owned by this isolated probe overlay. */
		struct GlState
		{
			LinuxWindowOverlay& overlay;
			GLint matrixMode = 0, activeTexture = 0;
			explicit GlState(LinuxWindowOverlay& overlay) : overlay(overlay)
			{
				glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
				glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
				glPushAttrib(GL_ALL_ATTRIB_BITS);
				glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
				glActiveTexture(GL_TEXTURE0);
				glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
				glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
				glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
			}
			~GlState()
			{
				for (const auto& texture : overlay.heroPreviewDiffuseTextureCache)
					glDeleteTextures(1, &texture.second.texture);
				overlay.heroPreviewDiffuseTextureCache.clear();
				glActiveTexture(GL_TEXTURE0);
				glMatrixMode(GL_TEXTURE); glPopMatrix();
				glMatrixMode(GL_PROJECTION); glPopMatrix();
				glMatrixMode(GL_MODELVIEW); glPopMatrix();
				glMatrixMode(matrixMode);
				glPopClientAttrib();
				glPopAttrib();
				glActiveTexture(activeTexture);
			}
		} saved(overlay);
		for (GLint i = 0; i < textureUnits; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glDisable(GL_TEXTURE_1D); glDisable(GL_TEXTURE_2D);
			glDisable(GL_TEXTURE_3D); glDisable(GL_TEXTURE_CUBE_MAP);
		}
		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_TEXTURE_GEN_S); glDisable(GL_TEXTURE_GEN_T);
		glDisable(GL_TEXTURE_GEN_R); glDisable(GL_TEXTURE_GEN_Q);
		glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
		glClearDepth(1); glDepthRange(0, 1);
		glViewport(0, 0, 256, 256);
		glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_PACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
		glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
		glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE); glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
		glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
		glDisable(GL_STENCIL_TEST); glDisable(GL_FOG); glDisable(GL_ALPHA_TEST);
		glDisable(GL_DITHER); glDisable(GL_MULTISAMPLE); glDisable(GL_COLOR_LOGIC_OP);
		glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glClearColor(0, 0, 0, 1);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glShadeModel(GL_SMOOTH);
		glMatrixMode(GL_PROJECTION); glOrtho(-7, 7, -5, 9, -30, 30);
		glMatrixMode(GL_MODELVIEW); glRotatef(35, 1, 0, 0); glRotatef(25, 0, 1, 0);
		LinuxBootstrap::FramePoseCache<LinuxHeroMeshPreviewVertex> poses;
		const auto draw = [&](const NWorld::LinuxDynamicWorldMarker& marker, double time, bool textureRgb = true) {
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, textureRgb ? GL_MODULATE : GL_COMBINE);
			if (!textureRgb)
			{
				// Negative control ignores texture RGB only; original assets, UVs and alpha stay intact.
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
				glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
				glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
				glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
				glTexEnvi(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1);
			}
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			check(DrawLinuxMapUnitMeshReplica(&overlay, &map, &preview, marker, 0, 0, mapScale,
				time, 255, 255, 255, &poses, true),
				"production map-unit draw accepts the original graph-owned summon");
			std::vector<unsigned char> pixels(256 * 256 * 4);
			glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			// Capture's existing helper clears GL errors, so account for draw/read errors first.
			check(glGetError() == GL_NO_ERROR, "map-unit draw and RGBA readback complete without GL errors");
			return pixels;
		};
		const auto capture = [&](const char* suffix) {
			if (capturePath.empty()) return;
			std::string error;
			const std::string path = capturePath + suffix;
			const bool captured = CaptureLinuxOpenGlBackBufferPng(path, 256, 256, &error);
			check(captured, "retain original Demon2 framebuffer through RGBA vertical-flip PNG helper");
			fprintf(stdout, "Summon presentation capture: path=%.512s result=%s error=%.256s\n",
				path.c_str(), captured ? "PASS" : "FAIL", error.c_str());
		};
		const auto idlePixels = draw(idleMarker, .4);
		capture("-summon-idle.png");
		const auto movingPixels = draw(movingMarker, .4);
		capture("-summon-move.png");
		const size_t uploadedTextures = overlay.heroPreviewDiffuseTextureCache.size();
		const auto repeatedPixels = draw(idleMarker, .4);
		const auto withoutTextureRgb = draw(idleMarker, .4, false);
		const auto visible = [](const std::vector<unsigned char>& pixels) {
			size_t count = 0;
			for (size_t i = 0; i < pixels.size(); i += 4) if (pixels[i] || pixels[i + 1] || pixels[i + 2]) ++count;
			return count;
		};
		bool uploaded = uploadedTextures > 0;
		for (size_t i = 0; i < usedTextures.size(); ++i)
			if (usedTextures[i])
			{
				const auto found = overlay.heroPreviewDiffuseTextureCache.find(asset.skinDiffuseTextures[i].sourceFile);
				uploaded &= found != overlay.heroPreviewDiffuseTextureCache.end() && found->second.texture &&
					glIsTexture(found->second.texture);
			}
		check(uploaded, "all referenced original diffuse textures have live GL uploads");
		const size_t idleVisible = visible(idlePixels), movingVisible = visible(movingPixels);
		check(idleVisible > 256 && movingVisible > 256, "both original summon poses produce nonblank framebuffer geometry");
		check(idlePixels != movingPixels && idlePixels == repeatedPixels,
			"independent idle/move pixels differ while warm idle pixels repeat exactly");
		size_t texturedPixels = 0;
		for (size_t i = 0; i < idlePixels.size(); i += 4)
			if (idlePixels[i] != withoutTextureRgb[i] || idlePixels[i + 1] != withoutTextureRgb[i + 1] ||
				idlePixels[i + 2] != withoutTextureRgb[i + 2]) ++texturedPixels;
		check(texturedPixels > 64, "original texture RGB contributes to visible geometry rather than fallback color only");

		bank.Begin(&worldToken, sceneToken, .65);
		movingMarker.moving = false;
		const bool changed = bank.Resolve(asset, movingMarker) == moving;
		check(changed && moving->Updates() > movingUpdates && moving->LastSampleTime() == .65 &&
			mapped(moving, NDb::ANIMSTATES_IDLE), "changing one owner advances only its graph toward authored idle");
		check(idle->Updates() == idleUpdates && idle->LastSampleTime() == .4 && samePose(idlePose, idle->Matrices()),
			"the untouched owner retains its exact sample time update count and matrices");
		const auto changedPixels = draw(movingMarker, .65);
		const auto repeatedChangedPixels = draw(movingMarker, .65);
		check(visible(changedPixels) > 256 && changedPixels == repeatedChangedPixels &&
			idle->Updates() == idleUpdates && idle->LastSampleTime() == .4 && samePose(idlePose, idle->Matrices()),
			"changed owner repeats warm pixels without advancing the untouched owner's graph");
		check(poses.builds == 3 && poses.hits == 3 && overlay.heroPreviewDiffuseTextureCache.size() == uploadedTextures,
			"pose cache separates owners and times while repeated draws reuse original textures");
		fprintf(stdout, "Summon presentation pixels: idle=%zu moving=%zu textured=%zu textures=%zu textured-triangles=%zu owners=%zu pose-builds=%zu pose-hits=%zu\n",
			idleVisible, movingVisible, texturedPixels, uploadedTextures, texturedTriangles, bank.Size(), poses.builds, poses.hits);
		check(glGetError() == GL_NO_ERROR, "summon asset rendering leaves no GL errors");
	}
	GLint restoredViewport[4] = {};
	glGetIntegerv(GL_VIEWPORT, restoredViewport);
	check(std::equal(viewport, viewport + 4, restoredViewport) && glGetError() == GL_NO_ERROR,
		"probe-owned textures retire and host viewport and GL state restore without errors");

	auto unrelatedMarker = idleMarker;
	unrelatedMarker.objectId = 3;
	auto* unrelated = bank.Resolve(unrelatedAsset, unrelatedMarker);
	if (!check(unrelated && unrelated != idle && unrelated != moving && bank.Size() == 3 && bank.CueSize() == 3 &&
		unrelatedAsset.dbid == asset.dbid && unrelatedAsset.animationComponent.GetPtr() == asset.animationComponent.GetPtr(),
		"a separate asset entry sharing original DB resources owns an unrelated graph and cue")) return finish();
	const auto unrelatedPose = unrelated->Matrices();
	const auto unrelatedUpdates = unrelated->Updates();
	const auto createdBeforeEviction = bank.created;
	const auto sampledBeforeEviction = bank.sampled;
	if (!check(bank.ForgetAsset(&asset) == 2 && bank.Size() == 1 && bank.CueSize() == 1 &&
		bank.created == createdBeforeEviction && bank.sampled == sampledBeforeEviction,
		"asset eviction removes both matching owners and cues without changing counters")) return finish();
	// Evicted idle/moving pointers are never dereferenced; allocator address reuse proves nothing.
	if (!check(bank.Resolve(unrelatedAsset, unrelatedMarker) == unrelated && bank.Size() == 1 && bank.CueSize() == 1 &&
		bank.created == createdBeforeEviction && unrelated->Updates() == unrelatedUpdates &&
		unrelated->LastSampleTime() == .65 && samePose(unrelatedPose, unrelated->Matrices()),
		"asset eviction preserves the unrelated graph cue clock and exact pose")) return finish();
	check(bank.ForgetAsset(nullptr) == 0 && bank.ForgetAsset(&asset) == 0 && bank.Size() == 1 && bank.CueSize() == 1,
		"null and already-evicted assets cannot disturb another asset owner");
	check(bank.Resolve(asset, idleMarker) && bank.Resolve(asset, movingMarker) && bank.Size() == 3 && bank.CueSize() == 3 &&
		bank.created == createdBeforeEviction + 2 && bank.unsupported == 0 && bank.failed == 0,
		"both evicted owners recreate independently without reusing stale asset state");
	return finish();
#else
	(void)environment;
	(void)capturePath;
	fprintf(stderr, "Summon presentation probe: FAIL native OpenGL bootstrap is unavailable; no asset proof performed\n");
	return false;
#endif
}
