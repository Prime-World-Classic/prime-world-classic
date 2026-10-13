/** Original static sky geometry, after opaque units and before transparent scenery.
 * No independent texture loads or unbounded resources: all inputs use the scene ledger.
 * A shared map clock survives camera-window changes; original per-component time controllers
 * are not yet hosted here. The shipped MOBA sky uses identity component time/placement.
 */
static size_t DrawLinuxMapSky(LinuxWindowOverlay& overlay, const LinuxSelectedMapPreview& map,
	const pwshader::PreviewTransform& preview, double elapsed)
{
	if (!map.sceneCatalog || !std::isfinite(elapsed) || elapsed<0 ||
		map.skyGeometryPayloads.size()>LinuxBootstrap::SceneWindowLimits::skyCount) return 0;
	if (overlay.skySceneOwner.lock()!=map.sceneCatalog || elapsed<overlay.skyLastSeconds) {
		overlay.skySceneOwner=map.sceneCatalog; overlay.skyStartSeconds=elapsed;
	}
	overlay.skyLastSeconds=elapsed;
	const double time=elapsed-overlay.skyStartSeconds;
	if (time>std::numeric_limits<float>::max()) return 0;
	Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(preview.centerX,preview.centerY,preview.minZ,preview.scale);
	Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
	struct Reset {
		~Reset() {
			Render::LinuxBasicFx::ClearSelection();
			Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		}
	} reset;
	size_t draws=0;
	for (size_t i=0; i<map.skyGeometryPayloads.size(); ++i) {
		const auto& payload=map.skyGeometryPayloads[i];
		auto* resource=overlay.staticMapResources[2].Get(i,map.skyGeometryPayloads.size(),[&]() {
			const auto* geometry=Render::RenderResourceManager::LoadStaticMeshGeometry(nstl::string(payload.rootGeometryFile.c_str()),false);
			if (!geometry || geometry->primitives.empty() || geometry->primitives.size()>16 ||
				size_t(geometry->primitives.size())!=payload.materialReferences.size()) return std::unique_ptr<LinuxStaticMapResource>();
			auto result=std::make_unique<LinuxStaticMapResource>();
			payload.placement.GetMatrix(&result->world);
			result->mesh=std::make_shared<Render::StaticMesh>();
			// Null materials only provide queue ownership. The sky pass supplies the original
			// DB material explicitly, preventing the preview factory from loading textures.
			result->mesh->Initialize(result->world,geometry,nstl::vector<Render::BaseMaterial*>());
			return result;
		});
		if (!resource) { ++overlay.skyFailures; continue; }
		Render::BatchQueueSorter sorter(16,true);
		Render::BatchQueue queue(Render::BatchQueue::INDEX_MAIN,sorter,16);
		resource->mesh->RenderToQueue(queue);
		for (int priority=0; priority<NDb::MATERIALPRIORITY_COUNT; ++priority)
			for (auto* batch=queue.GetBatches(priority); batch; batch=batch->pNextBatch) {
				if (batch->elementNumber>=payload.materialReferences.size()) { ++overlay.skyFailures; continue; }
				const auto* material=dynamic_cast<const NDb::SkyMaterial*>(payload.materialReferences[batch->elementNumber].GetPtr());
				const auto index=ResolveLinuxMapRendererMaterialTextureIndex(payload.materialDiffuseTextureIndices,batch->elementNumber);
				const GLuint texture=ResolveLinuxMapPreviewDiffuseTexture(&overlay,&map,index);
				if (!material || !texture || index>=overlay.mapPreviewDiffuseTextures.size()) { ++overlay.skyFailures; continue; }
				batch->Prepare();
				Render::LinuxBasicFx::SelectSky(*material,texture,overlay.mapPreviewDiffuseTextures[index].mipLevels,float(time));
				const auto before=Render::LinuxBasicFx::GetStatistics();
				batch->Draw();
				if (Render::LinuxBasicFx::GetStatistics().skyDraws>before.skyDraws) { ++draws; ++overlay.skyDraws; }
				else ++overlay.skyFailures;
			}
	}
	return draws;
}
