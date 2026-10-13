bool CaptureLinuxOpenGlBackBufferPng(const std::string& path, int width, int height, std::string* error);

size_t DrawLinuxHeroRendererStaticMeshPayloads(LinuxWindowOverlay*, const LinuxSelectedHeroDbPreview*,
	const Render::SkeletonWrapper*, LinuxRendererMaterialSlotStats*, size_t*, size_t*, const pwshader::PreviewTransform&);

#include "transparent_engine_probe.inc"
#include "opacity_engine_probe.inc"

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
	// The cutout texture is shipped content; placing it on Stones is a test fixture,
	// not a claim that the original animated TrapFlower asset is supported here.
	const auto cutout = NDb::Get<NDb::BasicFXMaterial>(NDb::DBID("Effects/Debuffs/Roots/TrapFlower.MTRL"));
	unsigned opaqueCoverage = 0, cutoutCoverage = 0;
	for (int raster = 0; raster < 13; ++raster)
	{
		auto* material = new NDb::BasicFXMaterial(*authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0, 0, 0, 0));
		material->DiffuseAdd = decltype(material->DiffuseAdd)(Render::HDRColor(.8f, .1f, .2f,
			raster == 1 ? 126.f / 255 : raster == 3 ? 128.f / 255 : raster == 4 ? 0 : 127.f / 255));
		if (raster >= 1 && raster <= 3) material->renderState.alphaTest = NDb::ONOFFSTATE_ON;
		if (raster == 5) material->renderState.culling = NDb::ONOFFSTATE_OFF;
		if (raster == 6) material->renderState.alphaTest = static_cast<NDb::OnOffState>(99);
		if (raster == 7) material->renderState.culling = static_cast<NDb::OnOffState>(99);
		if (raster == 8) material->renderState.alphaTestRef = -1;
		if (raster == 9) material->renderState.alphaTestRef = 256;
		if (raster >= 11 && cutout)
		{
			material->DiffuseMap = cutout->DiffuseMap;
			material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0, 0, 0, 1));
			material->DiffuseAdd = decltype(material->DiffuseAdd)(Render::HDRColor(.8f, .1f, .2f, 0));
			material->renderState.alphaTest = raster == 11 ? NDb::ONOFFSTATE_ON : NDb::ONOFFSTATE_OFF;
			material->renderState.culling = NDb::ONOFFSTATE_OFF;
		}
		vector<Render::BaseMaterial*> materials;
		for (int i = 0; i < geometry->primitives.size(); ++i)
			materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material));
		Render::StaticMesh mesh; mesh.Initialize(world, geometry, materials);
		Render::BatchQueueSorter sorter(16, false);
		Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32);
		mesh.RenderToQueue(queue);
		glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glRotatef(60, 1, 0, 0); glRotatef(20, 0, 1, 0);
		glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_NEVER, .731f);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
		const auto before = Render::LinuxBasicFx::GetStatistics();
		const auto depthPin = Render::GetRuntimePins().DoDepthOutValue;
		if (raster == 10) Render::GetRuntimePins().DoDepthOutValue = NDb::BOOLPIN_TRUE;
		queue.Render();
		Render::GetRuntimePins().DoDepthOutValue = depthPin;
		const auto after = Render::LinuxBasicFx::GetStatistics();
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		GLint alphaFunction; GLfloat alphaReference;
		glGetIntegerv(GL_ALPHA_TEST_FUNC, &alphaFunction); glGetFloatv(GL_ALPHA_TEST_REF, &alphaReference);
		const bool restored = glIsEnabled(GL_ALPHA_TEST) && alphaFunction == GL_NEVER && alphaReference == .731f;
		glDisable(GL_ALPHA_TEST);
		std::vector<unsigned char> pixels(256 * 256 * 4);
		glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		unsigned covered = 0, wrong = 0;
		for (size_t i = 0; i < 256 * 256; ++i)
		{
			const auto* p = &pixels[i * 4];
			if (!p[0] && !p[1] && !p[2]) continue;
			++covered;
			if (std::abs(int(p[0]) - 204) > 1 || std::abs(int(p[1]) - 26) > 1 || std::abs(int(p[2]) - 51) > 1)
				++wrong;
		}
		if (raster == 0) opaqueCoverage = covered;
		if (raster == 11) cutoutCoverage = covered;
		const bool invalid = raster >= 6 && raster <= 10;
		const bool good = restored && !wrong && cutout && glGetError() == GL_NO_ERROR &&
			(invalid ? !covered && after.draws == before.draws && after.rejected > before.rejected :
				after.draws > before.draws && (raster == 1 ? !covered : covered > 100)) &&
			((raster != 2 && raster != 3 && raster != 4) || covered == opaqueCoverage) &&
			(raster != 5 || covered >= opaqueCoverage) &&
			(raster != 12 || (cutoutCoverage > 100 && cutoutCoverage < covered));
		fprintf(stdout, "BasicFX raster probe: stage=%d draws=%llu pixels=%u wrong=%u restored=%d result=%s\n",
			raster, static_cast<unsigned long long>(after.draws - before.draws), covered, wrong, restored, good ? "PASS" : "FAIL");
		passed = good && passed;
		if (!capturePath.empty() && raster >= 11)
		{
			std::string error;
			passed = CaptureLinuxOpenGlBackBufferPng(capturePath + (raster == 11 ? "-cutout.png" : "-opaque.png"),
				256, 256, &error) && passed;
		}
	}
	// A one-sided mock quad makes the DB-to-backend culling mapping observable.
	Render::MeshGeometry quad;
	const float quadVertices[][5] = {{-3,-3,0,0,0}, {3,-3,0,1,0}, {3,3,0,1,1}, {-3,3,0,0,1}};
	const WORD quadIndices[] = {0,1,2,0,2,3};
	Render::VertexFormatDescriptor format;
	format.AddVertexElement(Render::VertexElementDescriptor(0, 0, Render::VERTEXELEMENTTYPE_FLOAT3,
		Render::VERETEXELEMENTUSAGE_POSITION, 0));
	format.AddVertexElement(Render::VertexElementDescriptor(0, 12, Render::VERTEXELEMENTTYPE_FLOAT2,
		Render::VERETEXELEMENTUSAGE_TEXCOORD, 0));
	auto* primitive = new Render::Primitive;
	quad.primitives.push_back(primitive);
	quad.materialCount = quad.fragmentCount = 1; quad.triangleCount = 2; quad.materialID[0] = 0;
	primitive->SetVertexDeclaration(Render::SmartRenderer::GetVertexFormatDeclaration(format));
	primitive->SetVertexStream(Render::CreateVB(sizeof(quadVertices), Render::RENDER_POOL_MANAGED, quadVertices), sizeof(quadVertices[0]));
	primitive->SetIndexBuffer(Render::CreateIB16(sizeof(quadIndices), Render::RENDER_POOL_MANAGED, quadIndices));
	primitive->GetDipDescriptor() = Render::DipDescriptor(Render::RENDERPRIMITIVE_TRIANGLELIST, 0, 0, 4, 0, 2);
	for (int mirrored = 0; mirrored < 2; ++mirrored) for (int culling = 0; culling < 2; ++culling)
	{
		auto* material = new NDb::BasicFXMaterial(*authored);
		NDb::Ptr<NDb::BasicFXMaterial> held(material);
		material->renderState.culling = culling ? NDb::ONOFFSTATE_ON : NDb::ONOFFSTATE_OFF;
		material->DiffuseMul = decltype(material->DiffuseMul)(Render::HDRColor(0, 0, 0, 0));
		material->DiffuseAdd = decltype(material->DiffuseAdd)(Render::HDRColor(0, 1, 0, 1));
		Matrix43 placement; Identity(&placement); if (mirrored) placement._11 = -1;
		vector<Render::BaseMaterial*> materials;
		materials.push_back(Render::CreateLinuxStaticMeshPreviewMaterial(material));
		Render::StaticMesh mesh; mesh.Initialize(placement, &quad, materials);
		Render::BatchQueueSorter sorter(16, false);
		Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN, sorter, 32); mesh.RenderToQueue(queue);
		glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glRotatef(-90, 1, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
		const auto before = Render::LinuxBasicFx::GetStatistics(); queue.Render();
		const auto after = Render::LinuxBasicFx::GetStatistics();
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		unsigned char pixel[4]{}; glReadPixels(128,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
		const bool visible = !culling || !mirrored;
		const bool good = after.draws == before.draws + 1 && !pixel[0] && !pixel[2] &&
			pixel[1] == (visible ? 255 : 0) && glGetError() == GL_NO_ERROR;
		fprintf(stdout, "BasicFX sided probe: mirrored=%d culling=%d green=%u result=%s\n",
			mirrored, culling, pixel[1], good ? "PASS" : "FAIL");
		passed = good && passed;
	}
	passed = RunLinuxBasicFxTransparentProbe(*authored, quad, *component) && passed;
	passed = RunLinuxBasicFxOpacityProbe(*authored, quad, capturePath) && passed;
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
