/** Load the exact descriptor-ordered baked stream once, never from a draw callback. */
static bool LoadLinuxMapBakedTerrain(const LinuxClientEnvironment& environment,
	const NDb::Terrain& terrain, LinuxSelectedMapPreview& map)
{
	map.bakedTerrain.reset(); map.bakedTerrainFile.clear();
	std::string path;
	if (!ResolveLinuxDataPayloadFile(environment, ToStdString(terrain.geometry.bakedTerrainFileName), &path)) return false;
	try {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		const auto size = file.tellg();
		if (!file || size <= 0 || size > static_cast<std::streamoff>(LinuxBootstrap::BakedTerrainLimits{}.sourceBytes)) return false;
		std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
		file.seekg(0);
		if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
		LinuxBootstrap::BakedTerrainError error;
		if (!LinuxBootstrap::BakedTerrain::Decode(std::move(bytes), terrain.geometry.terrainElements.size(), map.bakedTerrain, error)) {
			fprintf(stderr, "Original baked terrain rejected: code=%d offset=%zu\n", int(error.code), error.offset);
			return false;
		}
		map.bakedTerrainFile = path;
		fprintf(stdout, "Original baked terrain: tiles=%zu vertices=%zu triangles=%zu bytes=%zu\n",
			map.bakedTerrain->TileCount(), map.bakedTerrain->VertexCount(), map.bakedTerrain->TriangleCount(), map.bakedTerrain->SourceBytes());
		return true;
	} catch (const std::exception&) { map.bakedTerrain.reset(); return false; }
}

/** Baked vertices include editor placement and deformation. Bounds and transform are world-space. */
static void RecordLinuxMapBakedTerrainElement(const LinuxClientEnvironment& environment,
	const NDb::TerrainElementInstance& element, size_t index, LinuxSelectedMapPreview& map)
{
	++map.terrainHeightmap.terrainElementPayloadReferenceCount;
	const auto* tile = map.bakedTerrain->GetTile(index);
	if (!tile || !IsValid(element.material) || !ReserveLinuxMapSceneMetadata(&map, map.bakedTerrainFile.size() * 2)) return;
	if (!map.sceneCatalog && map.terrainElementPayloads.size() >= kLinuxMapTerrainElementPreviewMaxPayloadFiles) {
		map.terrainHeightmap.terrainElementPayloadLimitHit = true; return;
	}
	LinuxMapStaticGeometryPayloadPreview payload;
	payload.bakedTerrain = true; payload.originalObjectIndex = index;
	payload.geometryFile = payload.rootGeometryFile = map.bakedTerrainFile;
	payload.placement = Placement();
	payload.localBounds.minX = tile->minimum[0]; payload.localBounds.maxX = tile->maximum[0];
	payload.localBounds.minY = tile->minimum[1]; payload.localBounds.maxY = tile->maximum[1];
	payload.localBounds.minZ = tile->minimum[2]; payload.localBounds.maxZ = tile->maximum[2];
	payload.materialReferences.push_back(element.material);
	payload.materialDiffuseTextureIndices.push_back(RecordLinuxMapStaticDiffuseTexture(environment,
		ExtractLinuxMaterialDiffuseTextureFileName(element.material.GetPtr()), &map));
	map.terrainElementPayloads.push_back(std::move(payload));
	++map.terrainHeightmap.terrainElementPayloadResolvedCount;
}

/** Convert admitted bytes to owned native backing buffers, not the process-lifetime mesh pool.
 * Keep authored normals/UV/three colour streams for the subsequent terrain-material port.
 * This uses the current preview material; it does not yet implement terrain-layer shading.
 */
static const Render::MeshGeometry* CreateLinuxBakedTerrainGeometry(LinuxStaticMeshStorage& storage,
	const LinuxBootstrap::BakedTerrain::Ptr& terrain, size_t index)
{
	const auto* tile = terrain ? terrain->GetTile(index) : nullptr;
	const std::uint32_t endian = 1;
	if (!tile || *reinterpret_cast<const unsigned char*>(&endian) != 1) return nullptr;
	// The native DXPtr is borrowed. Adopt the helpers' bounded copies explicitly.
	std::unique_ptr<IDirect3DVertexBuffer9> vertices(Get(Render::CreateVB(
		static_cast<int>(tile->vertexCount * 44), Render::RENDER_POOL_MANAGED, terrain->VertexData(index))));
	std::unique_ptr<IDirect3DIndexBuffer9> indices(Get(Render::CreateIB(
		static_cast<int>(tile->indexCount * 4), Render::RENDER_POOL_MANAGED,
		reinterpret_cast<const UINT*>(terrain->IndexData(index)))));
	auto geometry = std::make_unique<Render::MeshGeometry>();
	auto primitive = std::make_unique<Render::Primitive>();
	primitive->SetVertexStream(vertices.get(), 44);
	primitive->SetIndexBuffer(indices.get());
	Render::VertexFormatDescriptor declaration;
	declaration.AddVertexElement({0, 0, Render::VERTEXELEMENTTYPE_FLOAT3, Render::VERETEXELEMENTUSAGE_POSITION, 0});
	declaration.AddVertexElement({0, 12, Render::VERTEXELEMENTTYPE_FLOAT3, Render::VERETEXELEMENTUSAGE_NORMAL, 0});
	declaration.AddVertexElement({0, 24, Render::VERTEXELEMENTTYPE_FLOAT2, Render::VERETEXELEMENTUSAGE_TEXCOORD, 0});
	for (unsigned channel = 0; channel < 3; ++channel)
		declaration.AddVertexElement({0, 32 + channel * 4, Render::VERTEXELEMENTTYPE_D3DCOLOR, Render::VERETEXELEMENTUSAGE_COLOR, channel});
	primitive->SetVertexDeclaration(Render::SmartRenderer::GetVertexFormatDeclaration(declaration));
	auto& draw = primitive->GetDipDescriptor();
	draw.primitiveType = Render::RENDERPRIMITIVE_TRIANGLELIST;
	draw.baseVertexIndex = 0; draw.startIndex = 0;
	draw.numVertices = static_cast<unsigned>(tile->vertexCount);
	draw.primitiveCount = static_cast<unsigned>(tile->triangleCount);
	geometry->materialCount = geometry->fragmentCount = 1;
	geometry->materialID[0] = 0; geometry->triangleCount = draw.primitiveCount;
	geometry->primitives.push_back(primitive.get());
	primitive.release();
	storage.terrainVertices = std::move(vertices); storage.terrainIndices = std::move(indices);
	storage.terrainGeometry = std::move(geometry);
	return storage.terrainGeometry.get();
}
