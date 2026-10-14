/** Read only the original fixed-size clip header. No skeleton, samples, GPU state or unbounded allocation.
 * Track-table bounds establish that the duration belongs to a structurally plausible clip;
 * sample payload validation remains the renderer's responsibility.
 */
static bool LinuxAnimationClipDuration(Stream& stream, float& duration)
{
	struct Header { char signature[4]; float minTime, maxTime; uint32_t count, offset; } header;
	static_assert(sizeof(Header) == sizeof(SkeletalAnimationData), "original clip header layout");
	static_assert(offsetof(Header, count) == offsetof(SkeletalAnimationData, tracks), "relative track origin");
	const int size = stream.GetSize();
	if (!stream.IsOk() || size < static_cast<int>(sizeof(header)) || size > 32*1024*1024 ||
		stream.Read(&header, sizeof(header)) != sizeof(header) || !stream.IsOk()) return false;
	const uint64_t begin = offsetof(Header, count) + static_cast<uint64_t>(header.offset);
	const uint64_t end = begin + static_cast<uint64_t>(header.count)*sizeof(TBoneTracks);
	const float value = header.maxTime-header.minTime;
	if (!std::isfinite(header.minTime) || !std::isfinite(header.maxTime) || !std::isfinite(value) || value < 0 || value > 3600 ||
		header.count == 0 || header.count > 512 || begin < sizeof(header) || end > static_cast<uint64_t>(size)) return false;
	duration = value; return true;
}

/** Metadata-only blender for original graph construction. It never plays, samples or publishes a pose. */
class LinuxTimingBlender : public NScene::IAnimGraphBlender
{
public:
	std::vector<float> durations;
	NScene::AnimationVector const& GetAnimationVector() const override { return animations; }
	NScene::Time GetAnimDuration(const int& index) const override { return durations.at(index); }
	void SetVirtualParameterByMix(int, float) override {}
	bool RegisterSequencesToMix(int, int*, float*) override { return false; }
	DiAnimGraph* GetMainGraph() override { return nullptr; }
	const DiAnimGraph* GetMainGraph() const override { return nullptr; }
	DiAnimGraph* GetAdditionalGraph() override { return nullptr; }
	const DiAnimGraph* GetAdditionalGraph() const override { return nullptr; }
	bool Update(float) override { return false; }
	bool SetMainAGTargetNode(int) override { return false; }
	bool SetAdditionalAGTargetNode(int) override { return false; }
private:
	NScene::AnimationVector animations;
};

/** Preserve the original first eligible dispatcher-neighbour rule, not weighted/random selection. */
static bool LinuxQueryAnimationTime(DiAnimGraph& graph, const char* name, const char* marker, float& seconds)
{
	if (!name || !*name || std::strlen(name) >= 36 || (marker && (!*marker || std::strlen(marker) > 128))) return false;
	const auto id = graph.GetNodeIDByNameSlowQuite(name);
	if (id >= graph.GetNumNodes()) return false;
	struct Query : INeiFunctor {
		DiAnimGraph& graph; const char* marker; bool found = false; float value = 0;
		Query(DiAnimGraph& g, const char* m) : graph(g), marker(m) {}
		void Read(DiUInt32 id) {
			auto* node = graph.GetNodeData(id);
			if (!node) return;
			if (marker) {
				auto* mark = graph.FindMarkerByNameInNode(marker, id);
				if (!mark) return;
				found = true;
				value = node->GetSpeed() > 0 ? mark->GetVirtPar()/node->GetSpeed() : -1;
			} else { found = true; value = graph.GetNodeDuration(id); }
		}
		void operator()(DiUInt32 id) override {
			if (!found && graph.GetNodeData(id)->IsSubNode()) Read(id);
		}
	} query(graph, marker);
	if (graph.GetNodeData(id)->IsSwitcher()) graph.ForAllNeighbours(id, &query); else query.Read(id);
	if (!query.found || !std::isfinite(query.value) || query.value < 0 || query.value > 3600) return false;
	seconds = query.value; return true;
}

/** Per-world bounded cache of immutable original graphs and DB ownership, independent of visible units.
 * Uses the original offscreen scene selection: Unit.sceneObject, then BaseHero.heroSceneObjects[0].
 * Dynamic external parameters, additional graphs and runtime skin/graph replacements are not supported.
 */
class LinuxAuthoredAbilityTiming : public NWorld::LinuxAbilityAnimationTiming
{
public:
	~LinuxAuthoredAbilityTiming() override
	{
		std::printf("Authored ability timing: queries=%llu matched=%llu positive=%llu graphs=%llu\n",
			static_cast<unsigned long long>(queries), static_cast<unsigned long long>(matched),
			static_cast<unsigned long long>(positive), static_cast<unsigned long long>(graphs));
	}
	bool Query(const NDb::Unit& unit, const char* node, const char* marker, float& seconds) override
	{
		++queries;
		const NDb::DBSceneObject* scene = unit.sceneObject.GetPtr();
		if (!scene) if (const auto* hero = dynamic_cast<const NDb::BaseHero*>(&unit))
			if (!hero->heroSceneObjects.empty()) scene = hero->heroSceneObjects[0].GetPtr();
		if (!scene) return false;
		auto found = entries.find(scene);
		if (found == entries.end()) {
			// Eviction changes only cost, never query results or gameplay state.
			if (entries.size() >= 256) entries.clear();
			auto entry = std::unique_ptr<Entry>(new Entry); entry->scene = scene;
			unsigned visited = 0;
			for (const auto& attached : scene->attached) {
				entry->component = FindComponent(attached.component.GetPtr(), 0, visited);
				if (entry->component || visited > 256) break;
			}
			if (entry->component && LinuxAnimationGraphSupported(*entry->component)) {
				bool valid = true;
				for (const auto& animation : entry->component->animations) {
					CObj<Stream> file = RootFileSystem::OpenFile(animation.animationFileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
					float duration = 0;
					if (!file || !LinuxAnimationClipDuration(*file, duration)) { valid = false; break; }
					entry->blender.durations.push_back(duration);
				}
				if (valid) {
					entry->graph.reset(DiAnimGraph::CreateByData(entry->component->animGraph, entry->component->animations, &entry->blender));
					if (entry->graph) {
						++graphs;
						DiAnGrExtPars parameters(entry->component->animGraph, entry->graph.get());
						parameters.applyParamsToGraph(); entry->graph->StoreDefaultValues();
					}
				}
			}
			found = entries.emplace(scene, std::move(entry)).first;
		}
		const bool good = found->second->graph && LinuxQueryAnimationTime(*found->second->graph, node, marker, seconds);
		if (good) { ++matched; if (seconds > 0) ++positive; }
		return good;
	}
private:
	/** Bounded preorder matches original first animated component; cycles cannot recurse indefinitely. */
	static const NDb::DBAnimatedSceneComponent* FindComponent(const NDb::DBSceneComponent* component, unsigned depth, unsigned& visited)
	{
		if (!component || depth > 16 || ++visited > 256) return nullptr;
		if (const auto* animated = dynamic_cast<const NDb::DBAnimatedSceneComponent*>(component))
			if (animated->animGraph) return animated;
		for (const auto& attached : component->attached) {
			if (auto* found = FindComponent(attached.component.GetPtr(), depth+1, visited)) return found;
			if (visited > 256) break;
		}
		return nullptr;
	}
	struct DeleteGraph { void operator()(DiAnimGraph* graph) const { if (graph) DiAnimGraph::Destroy(graph); } };
	struct Entry {
		NDb::Ptr<NDb::DBSceneObject> scene;
		NDb::Ptr<NDb::DBAnimatedSceneComponent> component;
		LinuxTimingBlender blender;
		std::unique_ptr<DiAnimGraph, DeleteGraph> graph;
	};
	std::map<const NDb::DBSceneObject*, std::unique_ptr<Entry>> entries;
	uint64_t queries = 0, matched = 0, positive = 0, graphs = 0;
};
