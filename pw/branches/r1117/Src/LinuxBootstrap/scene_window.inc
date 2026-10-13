/** Bounded native presentation catalog. Discovery retains original placement identity;
 * selection and asset admission precede any geometry load or diffuse decode. The
 * original resource manager owns process-lifetime pools, so this is not an LRU.
 */
static LinuxBootstrap::SceneAssetLedger& LinuxMapSceneAssetLedger()
{
	static LinuxBootstrap::SceneAssetLedger ledger;
	return ledger;
}

/// Enumerate every diffuse fallback that the existing preview material can load.
static std::vector<std::string> LinuxMapMaterialTextureFiles(const NDb::Material* material)
{
	if (const auto* terrain = dynamic_cast<const NDb::TerrainMaterial*>(material))
		return {ExtractLinuxSamplerExTextureFileName(terrain->N_DiffuseMap),
			ExtractLinuxSamplerExTextureFileName(terrain->A_DiffuseMap),
			ExtractLinuxSamplerExTextureFileName(terrain->B_DiffuseMap)};
	return {ExtractLinuxMaterialDiffuseTextureFileName(material)};
}

/// Transform the authored full box, not just its origin; large overlapping rocks matter.
static LinuxBootstrap::SceneWindowBounds LinuxMapSceneBounds(const NDb::AABB& db,
	const Placement& placement, LinuxSelectedMapPreview& map)
{
	Render::AABB local, world;
	local.Set(db);
	Matrix43 matrix;
	placement.GetMatrix(&matrix);
	world.Transform(matrix, local);
	LinuxBootstrap::SceneWindowBounds result{world.center.x - world.halfSize.x, world.center.y - world.halfSize.y,
		world.center.x + world.halfSize.x, world.center.y + world.halfSize.y};
	if (local.IsValid() && world.IsValid() && result.Valid() &&
		(result.maxX > result.minX || result.maxY > result.minY)) return result;
	++map.sceneCatalog->fallbackBounds;
	// Missing authored bounds cannot justify culling. Keep them conservatively visible.
	return {map.tactical.minX, map.tactical.minY, map.tactical.maxX, map.tactical.maxY};
}

static void FinalizeLinuxMapSceneCatalog(const LinuxClientEnvironment& environment, LinuxSelectedMapPreview* map)
{
	if (!map || !map->sceneCatalog) return;
	auto& catalog = *map->sceneCatalog;
	const auto registerMaterials = [&](const auto& materials) {
		for (const auto& material : materials)
			for (const auto& file : LinuxMapMaterialTextureFiles(material.GetPtr()))
				if (!file.empty()) RecordLinuxMapStaticDiffuseTexture(environment, file, map);
	};
	for (const auto& payload : map->staticGeometryPayloads) registerMaterials(payload.materialReferences);
	for (const auto& payload : map->terrainElementPayloads) registerMaterials(payload.materialReferences);
	for (const auto& payload : map->animatedGeometryPayloads)
		for (const auto& skin : payload.skinGeometryPayloads) registerMaterials(skin.materialReferences);
	catalog.statics = std::move(map->staticGeometryPayloads);
	catalog.terrain = std::move(map->terrainElementPayloads);
	catalog.animated = std::move(map->animatedGeometryPayloads);
	catalog.textures = std::move(map->staticDiffuseTextureFiles);
	catalog.terrainLayers = map->terrainHeightmap.terrainLayers;
	map->staticDiffuseTextures.clear();
	const auto add = [&](const auto& payloads, LinuxBootstrap::SceneWindowKind kind) {
		for (size_t i = 0; i < payloads.size(); ++i) {
			const auto& payload = payloads[i];
			catalog.candidates.push_back({kind, i, payload.originalObjectIndex, payload.sceneWindowComponentIndex,
				LinuxMapSceneBounds(payload.localBounds, payload.placement, *map)});
		}
	};
	add(catalog.statics, LinuxBootstrap::SceneWindowKind::Static);
	for (auto& candidate : catalog.candidates) {
		const auto& materials=catalog.statics[candidate.payloadIndex].materialReferences;
		if (!materials.empty() && std::all_of(materials.begin(),materials.end(),[](const auto& material) {
			return dynamic_cast<const NDb::SkyMaterial*>(material.GetPtr())!=nullptr;
		})) candidate.kind=LinuxBootstrap::SceneWindowKind::Sky;
	}
	add(catalog.terrain, LinuxBootstrap::SceneWindowKind::Terrain);
	add(catalog.animated, LinuxBootstrap::SceneWindowKind::Animated);
	catalog.discovering = false;
	std::cout << "Native scene catalog: objects=" << map->staticSceneObjectScanCount
		<< " candidates=" << catalog.candidates.size() << " static=" << catalog.statics.size()
		<< " terrain=" << catalog.terrain.size() << " animated=" << catalog.animated.size()
		<< " textures=" << catalog.textures.size() << " metadataDrops=" << catalog.metadataDrops
		<< " fallbackBounds=" << catalog.fallbackBounds << std::endl;
}

/// Size the existing DDS format before its decoder or engine sampler sees the file.
static bool ReadLinuxMapSceneAssetCost(const fs::path& path, LinuxBootstrap::SceneAssetKind kind,
	const char* geometryType, LinuxBootstrap::SceneAssetCost& cost)
{
	std::error_code error;
	const auto bytes = fs::file_size(path, error);
	if (error || bytes == 0 || bytes > 64U * 1024U * 1024U) return false;
	cost = {kind, std::string(geometryType) + path.lexically_normal().string(), static_cast<size_t>(bytes), 0};
	if (kind == LinuxBootstrap::SceneAssetKind::Geometry) return true;
	SDDSFileHeader header;
	static_assert(sizeof(header) == 128, "Original DDS header size");
	std::ifstream stream(path, std::ios::binary);
	if (!stream.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
		header.dwSignature != SDDSFileHeader::SIGNATURE || header.header.dwSize != sizeof(SDDSHeader) ||
		header.header.ddspf.dwSize != sizeof(SDDSPixelFormat) || header.header.dwDepth > 1 ||
		header.header.dwCubemapFlags != 0 || header.header.dwWidth == 0 || header.header.dwHeight == 0 ||
		header.header.dwWidth > 4096 || header.header.dwHeight > 4096) return false;
	cost.decodedBytes = static_cast<size_t>(header.header.dwWidth) * header.header.dwHeight * 4;
	std::shared_ptr<const std::vector<unsigned char>> mipSource;
	LinuxBootstrap::DdsMipChain mips;
	const auto status=ReadLinuxDdsMipSource(path,mipSource,mips);
	if (status==LinuxBootstrap::DdsMipStatus::Invalid) return false;
	if (status==LinuxBootstrap::DdsMipStatus::Valid) cost.decodedBytes=mips.rgbaBytes;
	return true;
}

/** Publish only between frames. Clear indexed owners before replacing even a same-sized
 * list. Stable guard-band frames do no scans, file queries, decoding or cache rebuilding.
 */
static bool UpdateLinuxMapSceneWindow(const LinuxClientEnvironment& environment,
	const LinuxBootstrap::SceneWindowRequest& request, LinuxSelectedMapPreview* map, LinuxWindowOverlay* overlay)
{
	using namespace LinuxBootstrap;
	if (!map || !overlay || !map->sceneCatalog || map->sceneCatalog->discovering ||
		!map->sceneWindow.NeedsUpdate(request)) return false;
	const auto& catalog = *map->sceneCatalog;
	SceneWindowMetrics metrics;
	const auto ranked = RankSceneWindow(catalog.candidates, request, metrics);
	SceneActiveBudget budget;
	std::vector<size_t> selected, textureIds;
	std::map<size_t, size_t> textureSlots;
	std::map<std::string, std::pair<bool, SceneAssetCost>> measured;
	auto& ledger = LinuxMapSceneAssetLedger();
	const auto costOf = [&](const fs::path& path, SceneAssetKind kind, const char* type,
		std::vector<SceneAssetCost>& costs) {
		const auto key = std::string(type) + path.string();
		auto found = measured.find(key);
		if (found == measured.end()) {
			SceneAssetCost cost;
			const bool valid = !path.empty() && ReadLinuxMapSceneAssetCost(path, kind, type, cost);
			found = measured.emplace(key, std::make_pair(valid, std::move(cost))).first;
		}
		if (found->second.first) costs.push_back(found->second.second);
		return found->second.first;
	};
	const auto texturesFor = [&](const auto& materials, std::set<size_t>& ids) {
		for (const auto& material : materials)
			for (const auto& file : LinuxMapMaterialTextureFiles(material.GetPtr())) {
				if (file.empty()) continue;
				const auto found = catalog.textureIndices.find(file);
				if (found == catalog.textureIndices.end()) return false;
				ids.insert(found->second);
			}
		return true;
	};
	const auto admit = [&](const std::set<size_t>& ids, std::vector<SceneAssetCost>& costs) {
		size_t newTextures = 0;
		for (const auto id : ids) if (!textureSlots.count(id)) ++newTextures;
		if (newTextures > SceneWindowLimits::textureCount - textureIds.size()) {
			++metrics.textureBudgetDrops; return false;
		}
		for (const auto id : ids)
			if (id >= catalog.textures.size() || !costOf(ResolveDataRefPath(environment, catalog.textures[id], 0),
				SceneAssetKind::Texture, "texture:", costs)) {
				++metrics.unavailableDrops; return false;
			}
		if (ledger.Admit(costs) != SceneAssetAdmission::Accepted) {
			++metrics.cumulativeDrops; return false;
		}
		for (const auto id : ids) if (!textureSlots.count(id)) {
			textureSlots.emplace(id, textureIds.size()); textureIds.push_back(id);
		}
		return true;
	};
	// The full heightfield uses these layer colors/textures in every window.
	for (const auto& layer : catalog.terrainLayers) {
		if (layer.diffuseTextureIndex == kLinuxHeroPreviewNoDiffuseTexture) continue;
		std::vector<SceneAssetCost> costs;
		admit({layer.diffuseTextureIndex}, costs);
	}
	for (const auto index : ranked) {
		const auto& candidate = catalog.candidates[index];
		if (candidate.kind != SceneWindowKind::Animated && candidate.kind != SceneWindowKind::Sky) {
			const auto& materials = candidate.kind == SceneWindowKind::Static
				? catalog.statics[candidate.payloadIndex].materialReferences : catalog.terrain[candidate.payloadIndex].materialReferences;
			// Mixed sky/non-sky components cannot use either ordinary fallback or the sky pass.
			if (std::any_of(materials.begin(), materials.end(), [](const auto& material) {
				return dynamic_cast<const NDb::SkyMaterial*>(material.GetPtr()) != nullptr;
			})) { ++metrics.unsupportedDrops; continue; }
		}
		if (!budget.HasRoom(candidate.kind)) { ++metrics.activeBudgetDrops; continue; }
		std::vector<SceneAssetCost> costs;
		std::set<size_t> ids;
		bool available = true;
		if (candidate.kind == SceneWindowKind::Animated) {
			const auto& payload = catalog.animated[candidate.payloadIndex];
			available = costOf(payload.skeletonFile, SceneAssetKind::Geometry, "skeleton:", costs);
			if (!payload.animationFile.empty())
				available = costOf(payload.animationFile, SceneAssetKind::Geometry, "animation:", costs) && available;
			for (const auto& skin : payload.skinGeometryPayloads)
				available = costOf(skin.geometryFile, SceneAssetKind::Geometry, "skin:", costs)
					&& texturesFor(skin.materialReferences, ids) && available;
		} else {
			const auto& payload = candidate.kind == SceneWindowKind::Terrain
				? catalog.terrain[candidate.payloadIndex] : catalog.statics[candidate.payloadIndex];
			available = costOf(payload.geometryFile, SceneAssetKind::Geometry, "static:", costs)
				&& texturesFor(payload.materialReferences, ids);
		}
		if (!available) { ++metrics.unavailableDrops; continue; }
		if (!admit(ids, costs)) continue;
		budget.Admit(candidate.kind);
		selected.push_back(index);
	}
	metrics.admittedStatic = budget.counts[0]; metrics.admittedTerrain = budget.counts[1];
	metrics.admittedAnimated = budget.counts[2];
	metrics.admittedSky = budget.counts[3];
	++map->sceneWindowBuilds;
	if (selected == map->sceneWindow.Selected() && map->sceneWindow.Revision() != 0) {
		map->sceneWindow.Publish(request, selected);
		map->sceneWindowMetrics = metrics;
		return false;
	}
	std::vector<LinuxMapStaticGeometryPayloadPreview> statics, terrain, sky;
	std::vector<LinuxMapAnimatedGeometryPayloadPreview> animated;
	const auto remap = [&](auto& indices) {
		for (auto& index : indices) {
			const auto found = textureSlots.find(index);
			index = found == textureSlots.end() ? kLinuxHeroPreviewNoDiffuseTexture : found->second;
		}
	};
	for (const auto index : selected) {
		const auto& candidate = catalog.candidates[index];
		if (candidate.kind == SceneWindowKind::Animated) {
			animated.push_back(catalog.animated[candidate.payloadIndex]);
			for (auto& skin : animated.back().skinGeometryPayloads) remap(skin.materialDiffuseTextureIndices);
		} else {
			auto& target = candidate.kind == SceneWindowKind::Sky ? sky : candidate.kind == SceneWindowKind::Static ? statics : terrain;
			target.push_back(candidate.kind == SceneWindowKind::Terrain ? catalog.terrain[candidate.payloadIndex]
				: catalog.statics[candidate.payloadIndex]);
			remap(target.back().materialDiffuseTextureIndices);
			for (auto& faction : target.back().terrainTextureIndices) remap(faction);
		}
	}
	std::vector<std::string> files;
	std::vector<LinuxTextureAssetPreview> textures;
	for (const auto id : textureIds) {
		const auto& file = catalog.textures[id];
		files.push_back(file);
		const auto old = std::find(map->staticDiffuseTextureFiles.begin(), map->staticDiffuseTextureFiles.end(), file);
		if (old != map->staticDiffuseTextureFiles.end()) {
			textures.push_back(map->staticDiffuseTextures[old - map->staticDiffuseTextureFiles.begin()]);
			++metrics.textureReuses;
		} else {
			textures.emplace_back();
			ProbeTexturePayloadFile(environment, file, &textures.back());
			auto& texture=textures.back();
			if (texture.artworkLoaded && ReadLinuxDdsMipSource(texture.sourceFile,texture.ddsMipSource,texture.ddsMips)==
				LinuxBootstrap::DdsMipStatus::Invalid) {
				texture.artworkLoaded=false;
				texture.warnings.push_back("DDS mip source changed or failed after scene admission");
			}
			if (texture.ddsMipSource) {
				const auto admitted=measured.find("texture:"+texture.sourceFile);
				if (admitted==measured.end() || !admitted->second.first ||
					texture.ddsMipSource->size()!=admitted->second.second.sourceBytes ||
					texture.ddsMips.rgbaBytes!=admitted->second.second.decodedBytes) {
					texture.ddsMipSource.reset(); texture.ddsMips={}; texture.artworkLoaded=false;
					texture.warnings.push_back("DDS mip source differs from admitted source/decoded extent");
				}
			}
			++metrics.textureLoads;
		}
	}
	auto layers = catalog.terrainLayers;
	size_t layersLoaded = 0;
	for (auto& layer : layers) {
		const auto found = textureSlots.find(layer.diffuseTextureIndex);
		layer.diffuseTextureIndex = found == textureSlots.end() ? kLinuxHeroPreviewNoDiffuseTexture : found->second;
		layer.textureLoaded = found != textureSlots.end() && textures[found->second].artworkLoaded;
		if (layer.textureLoaded) {
			++layersLoaded;
			ComputeLinuxTextureAverageColor(textures[found->second], &layer.red, &layer.green, &layer.blue);
		}
	}
	overlay->animatedMapResources.Clear();
	for (auto& cache : overlay->staticMapResources) cache.Clear();
	map->staticGeometryPayloads.swap(statics); map->terrainElementPayloads.swap(terrain);
	map->skyGeometryPayloads.swap(sky);
	map->animatedGeometryPayloads.swap(animated);
	map->staticDiffuseTextures.swap(textures); map->staticDiffuseTextureFiles.swap(files);
	map->terrainHeightmap.terrainLayers.swap(layers);
	map->terrainHeightmap.terrainLayerTextureLoadedCount = layersLoaded;
	map->sceneWindow.Publish(request, std::move(selected));
	map->sceneWindowMetrics = metrics;
	size_t maxObject = 0;
	for (const auto index : map->sceneWindow.Selected())
		if (catalog.candidates[index].kind != SceneWindowKind::Terrain)
			maxObject = std::max(maxObject, catalog.candidates[index].objectIndex);
	std::cout << "Native scene window: revision=" << map->sceneWindow.Revision() << " focus=" << request.focusX << ',' << request.focusY
		<< " scanned=" << metrics.scanned << " outside=" << metrics.outsideWindow
		<< " selected=" << metrics.admittedStatic << '/' << metrics.admittedTerrain << '/' << metrics.admittedAnimated
		<< " sky=" << metrics.admittedSky
		<< " textures=" << textureIds.size() << " loads=" << metrics.textureLoads << " reuses=" << metrics.textureReuses
		<< " maxOriginalObject=" << maxObject << " drops=" << metrics.activeBudgetDrops << '/' << metrics.textureBudgetDrops
		<< '/' << metrics.cumulativeDrops << '/' << metrics.unavailableDrops
		<< " unsupported=" << metrics.unsupportedDrops
		<< " cumulative=" << ledger.GeometryCount() << '/' << ledger.TextureCount()
		<< " bytes=" << ledger.GeometryBytes() << '/' << ledger.TextureBytes() << '/' << ledger.TextureDecodedBytes() << std::endl;
	return true;
}
