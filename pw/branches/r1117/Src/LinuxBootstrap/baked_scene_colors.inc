/** Read the original map color asset once, with a bound before allocation. */
static bool LoadLinuxBakedVertexColors(const char* path,
	LinuxBootstrap::BakedVertexColors::Ptr& output)
{
	output.reset();
	if (!path || !*path) return false;
	CObj<Stream> stream = RootFileSystem::OpenFile(path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
	const auto limit = LinuxBootstrap::BakedVertexColorLimits().maxSourceBytes;
	if (!IsValid(stream) || !stream->IsOk() || stream->GetSize() <= 0 ||
		static_cast<size_t>(stream->GetSize()) > limit) return false;
	try {
		std::vector<std::uint8_t> bytes(static_cast<size_t>(stream->GetSize()));
		if (stream->Read(bytes.data(), bytes.size()) != static_cast<int>(bytes.size())) return false;
		LinuxBootstrap::BakedVertexColorError error;
		return LinuxBootstrap::BakedVertexColors::Decode(std::move(bytes), output, error);
	} catch (const std::bad_alloc&) { return false; }
}

/** Match original placed-object identity and the complete common VB before publishing colors.
 * Native DXPtr is borrowed. The aliasing mesh owner must retain both actual allocations
 * through drawing, even after map cache eviction, and destroy its mesh before its buffers.
 */
static bool BindLinuxBakedVertexColors(LinuxStaticMeshStorage& storage, const Render::MeshGeometry& geometry,
	const LinuxBootstrap::BakedVertexColors::Ptr& colors, size_t originalObject,
	std::uint32_t sceneHash, size_t componentIndex)
{
	auto& mesh = storage.mesh;
	mesh.SetVertexColors(AutoPtr<Render::MeshVertexColors>(nullptr), false);
	storage.dayColors.reset(); storage.nightColors.reset();
	if (!colors || geometry.primitives.empty() || !geometry.primitives[0]) return false;
	const auto* first = geometry.primitives[0];
	const auto* common = Get(first->GetVertexBuffer());
	const auto stride = first->GetVBStride();
	if (!common || common->storage.empty() || !stride || common->storage.size() % stride) return false;
	for (int i = 0; i < geometry.primitives.size(); ++i) {
		const auto* primitive = geometry.primitives[i];
		if (!primitive || Get(primitive->GetVertexBuffer()) != common || primitive->GetVBStride() != stride)
			return false;
	}
	Crc32Checksum crc;
	crc.Add(&common->storage[0], common->storage.size());
	LinuxBootstrap::BakedVertexColorView view;
	LinuxBootstrap::BakedVertexColorError error;
	if (!colors->Lookup(originalObject, sceneHash, componentIndex, crc.Get(),
		common->storage.size() / stride, view, error)) return false;
	AutoPtr<Render::MeshVertexColors> buffers(new Render::MeshVertexColors);
	buffers->numColors = static_cast<unsigned>(view.ColorCount());
	std::unique_ptr<IDirect3DVertexBuffer9> day(Render::CreateVB(static_cast<unsigned>(view.ByteCount()), Render::RENDER_POOL_MANAGED).GetPtr());
	std::unique_ptr<IDirect3DVertexBuffer9> night(Render::CreateVB(static_cast<unsigned>(view.ByteCount()), Render::RENDER_POOL_MANAGED).GetPtr());
	buffers->pVB1 = day.get(); buffers->pVB2 = night.get();
	if (!buffers->pVB1 || !buffers->pVB2) return false;
	Render::FillVB(Get(buffers->pVB1), static_cast<unsigned>(view.ByteCount()), view.DayData());
	Render::FillVB(Get(buffers->pVB2), static_cast<unsigned>(view.ByteCount()), view.NightData());
	storage.dayColors = std::move(day); storage.nightColors = std::move(night);
	mesh.SetVertexColors(buffers, false);
	return true;
}
