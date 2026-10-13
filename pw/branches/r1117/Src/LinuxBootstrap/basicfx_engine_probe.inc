bool CaptureLinuxOpenGlBackBufferPng(const std::string& path, int width, int height, std::string* error);

/** Exercise authored geometry/texture through StaticMesh -> Batch -> SmartRenderer. */
bool RunLinuxBasicFxEngineProbe(const std::string& capturePath)
{
	const auto scene = NDb::Get<NDb::DBSceneObject>(NDb::DBID("Buildings/B/Main/_crash.SOBJ"));
	NDb::Ptr<NDb::DBStaticSceneComponent> component;
	if (scene)
		for (int i = 0; i < scene->attached.size(); ++i)
		{
			const auto* candidate = dynamic_cast<const NDb::DBStaticSceneComponent*>(scene->attached[i].component.GetPtr());
			if (candidate && candidate->geometryFileName == "/Buildings/B/Main/_CrashStones.stat")
				{ component = candidate; break; }
		}
	if (!component || component->materialsReferences.empty())
	{
		fprintf(stderr, "BasicFX engine probe: missing shipped Stones component.\n");
		return false;
	}
	const auto* authored = dynamic_cast<const NDb::BasicFXMaterial*>(component->materialsReferences[0].GetPtr());
	const auto* geometry = Render::RenderResourceManager::LoadStaticMeshGeometry(
		nstl::string(NormalizeRootFileSystemPath(component->geometryFileName.c_str()).c_str()), false);
	if (!authored || !geometry || geometry->primitives.empty())
	{
		fprintf(stderr, "BasicFX engine probe: missing original material or geometry.\n");
		return false;
	}
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	glViewport(0, 0, 256, 256);
	glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE); glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-7, 7, -7, 7, -30, 30);
	glMatrixMode(GL_MODELVIEW); glPushMatrix();
	Matrix43 world; Identity(&world);
	world._14 = .3f; world._24 = -.2f;
	std::vector<unsigned char> firstMask(256 * 256);
	bool passed = true;
	for (int stage = 0; stage < 4; ++stage)
	{
		// A private DB copy exercises constants without changing the shared asset/cache.
		auto* mutableMaterial = new NDb::BasicFXMaterial(*authored);
		NDb::Ptr<NDb::BasicFXMaterial> material(mutableMaterial);
		if (stage)
		{
			mutableMaterial->DiffuseMul = decltype(mutableMaterial->DiffuseMul)(Render::HDRColor(0, 0, 0, 0));
			mutableMaterial->DiffuseAdd = decltype(mutableMaterial->DiffuseAdd)(Render::HDRColor(.8f, .1f, .2f, 1));
		}
		if (stage == 3)
			mutableMaterial->uOffset = decltype(mutableMaterial->uOffset)(std::numeric_limits<float>::quiet_NaN());
		vector<Render::BaseMaterial*> materials;
		for (int i = 0; i < geometry->primitives.size(); ++i)
			materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material.GetPtr()));
		Render::StaticMesh mesh;
		mesh.Initialize(world, geometry, materials);
		Render::BatchQueueSorter sorter(16, true);
		Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32);
		mesh.RenderToQueue(queue);
		glMatrixMode(GL_MODELVIEW); glLoadIdentity();
		glRotatef(60, 1, 0, 0); glRotatef(stage == 2 ? 65 : 20, 0, 1, 0);
		glClearColor(0, 0, 0, 1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0, 0, 0, 1);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
		const auto before = Render::LinuxBasicFx::GetStatistics();
		unsigned batches = 0;
		for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
			for (Render::Batch* batch = queue.GetBatches(priority); batch; batch = batch->pNextBatch)
			{
				batch->Prepare(); batch->Draw(); ++batches;
			}
		const auto after = Render::LinuxBasicFx::GetStatistics();
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		std::vector<unsigned char> pixels(256 * 256 * 4);
		glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		unsigned covered = 0, incorrect = 0, changed = 0;
		for (size_t i = 0; i < firstMask.size(); ++i)
		{
			const auto* p = &pixels[i * 4];
			const bool visible = p[0] || p[1] || p[2];
			if (visible) ++covered;
			if (visible && stage && (std::abs(int(p[0]) - 204) > 1 ||
				std::abs(int(p[1]) - 26) > 1 || std::abs(int(p[2]) - 51) > 1)) ++incorrect;
			if (stage == 0) firstMask[i] = visible;
			else if (firstMask[i] != visible) ++changed;
		}
		const bool good = batches && glGetError() == GL_NO_ERROR &&
			(stage == 3 ? !covered && after.draws == before.draws && after.rejected > before.rejected :
				after.draws - before.draws == batches && covered > 100 && !incorrect &&
				(stage != 1 || changed == 0) && (stage != 2 || changed > 100));
		fprintf(stdout, "BasicFX engine probe: stage=%d batches=%u shaderDraws=%llu pixels=%u wrong=%u changed=%u result=%s\n",
			stage, batches, static_cast<unsigned long long>(after.draws - before.draws),
			covered, incorrect, changed, good ? "PASS" : "FAIL");
		passed = passed && good;
		if (!capturePath.empty() && (stage == 0 || stage == 2))
		{
			std::string error;
			const bool captured = CaptureLinuxOpenGlBackBufferPng(
				stage == 0 ? capturePath : capturePath + "-rotated.png", 256, 256, &error);
			if (!captured) fprintf(stderr, "BasicFX probe capture failed: %s\n", error.c_str());
			passed = captured && passed;
		}
	}
	Render::LinuxBasicFx::Select(authored, nullptr);
	for (int sorted = 0; sorted < 2; ++sorted)
		for (int unsupported = 0; unsupported < 2; ++unsupported)
		{
			Render::StaticMesh meshes[2];
			NDb::Ptr<NDb::BasicFXMaterial> held[2];
			Render::BatchQueueSorter sorter(16, true);
			Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32);
			for (int side = 0; side < 2; ++side)
			{
				auto* material = new NDb::BasicFXMaterial(*authored);
				held[side] = material;
				material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0, 0, 0, 0));
				material->DiffuseAdd = decltype(material->DiffuseAdd)(
					side ? Render::HDRColor(0, 1, 0, 1) : Render::HDRColor(1, 0, 0, 1));
				if (side && unsupported) material->SkeletalMeshPinValue = NDb::BOOLEANPIN_PRESENT;
				Matrix43 placement; Identity(&placement);
				placement._11 = placement._22 = placement._33 = .45f;
				placement._14 = side ? 3 : -3;
				vector<Render::BaseMaterial*> materials;
				for (int i = 0; i < geometry->primitives.size(); ++i)
					materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material));
				meshes[side].Initialize(placement, geometry, materials);
				meshes[side].RenderToQueue(queue);
			}
			unsigned count = 0;
			bool collision = false;
			for (int priority = 0; priority < NDb::MATERIALPRIORITY_COUNT; ++priority)
			{
				queue.SetSorting(priority, sorted != 0);
				for (auto* batch = queue.GetBatches(priority); batch; batch = batch->pNextBatch)
				{
					++count;
					if (batch->pNextBatch) collision = batch->GetSortId() == batch->pNextBatch->GetSortId();
				}
			}
			glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glRotatef(90, 1, 0, 0);
			glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glColor4f(1, 1, 1, 1);
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
			const auto before = Render::LinuxBasicFx::GetStatistics();
			queue.Render();
			const auto after = Render::LinuxBasicFx::GetStatistics();
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
			std::vector<unsigned char> pixels(256 * 256 * 4);
			glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			unsigned red = 0, green = 0, wrong = 0;
			for (size_t i = 0; i < 256 * 256; ++i)
			{
				const auto* p = &pixels[i * 4];
				if (!p[0] && !p[1] && !p[2]) continue;
				if (i % 256 < 128)
				{
					if (p[0] > 250 && p[1] < 2 && p[2] < 2) ++red; else ++wrong;
				}
				else if (!unsupported)
				{
					if (p[1] > 250 && p[0] < 2 && p[2] < 2) ++green; else ++wrong;
				}
			}
			const bool good = collision && count == 2 && !wrong && red > 100 &&
				(unsupported || green > 100) && after.draws - before.draws == static_cast<unsigned>(unsupported ? 1 : 2) &&
				glGetError() == GL_NO_ERROR;
			fprintf(stdout, "BasicFX queue probe: sorted=%d unsupported=%d collision=%d draws=%llu red=%u green=%u wrong=%u result=%s\n",
				sorted, unsupported, collision, static_cast<unsigned long long>(after.draws - before.draws),
				red, green, wrong, good ? "PASS" : "FAIL");
			passed = good && passed;
		}
	Render::LinuxBasicFx::Select(authored, nullptr);
	Render::Batch empty;
	empty.pMaterial = nullptr;
	empty.Prepare();
	passed = !Render::LinuxBasicFx::IsSelected() && passed;
	glMatrixMode(GL_MODELVIEW); glPopMatrix();
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glPopClientAttrib(); glPopAttrib();
	return passed && glGetError() == GL_NO_ERROR;
}
