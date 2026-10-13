/** Frame-bounded independent unit graphs. A map/world replacement discards old playback.
 * Unsupported assets keep the existing first-clip preview; no graph callback reaches PFWorld.
 */
class LinuxAnimationGraphBank
{
	struct Entry {
		const void* asset=nullptr;
		const void* component=nullptr;
		bool used=false;
		std::unique_ptr<LinuxAnimationGraphPlayback> player;
	};
	std::map<int,Entry> entries;
	const void* world=nullptr;
	std::weak_ptr<const void> scene;
	double clock=-1;
public:
	size_t created=0,unsupported=0,sampled=0,movingSamples=0,failed=0;
	/** Retain only the previous visible frame; at most 58 meshes are admitted by the caller. */
	void Begin(const void* nextWorld,const std::shared_ptr<const void>& nextScene,double seconds)
	{
		if (world!=nextWorld || scene.lock()!=nextScene || seconds<clock) entries.clear();
		world=nextWorld; scene=nextScene; clock=seconds;
		for (auto it=entries.begin(); it!=entries.end();) {
			if (!it->second.used) it=entries.erase(it);
			else { it->second.used=false; ++it; }
		}
	}
	/** Resolve the SessionVisualRoot's actual Hero/Creep aliases, never infer clip names. */
	LinuxAnimationGraphPlayback* Resolve(const LinuxHeroSceneAssetPreview& asset,const NWorld::LinuxDynamicWorldMarker& marker)
	{
		if (marker.objectId<0 || marker.dead || !std::isfinite(clock) || clock<0) return nullptr;
		auto it=entries.find(marker.objectId);
		if (it==entries.end()) {
			if (entries.size()>=128) { ++failed; return nullptr; }
			it=entries.emplace(marker.objectId,Entry{}).first;
		}
		auto& entry=it->second; entry.used=true;
		if (entry.asset!=&asset || entry.component!=asset.animationComponent.GetPtr()) {
			entry.asset=&asset; entry.component=asset.animationComponent.GetPtr(); entry.player.reset();
			const auto root=NDb::SessionRoot::GetRoot();
			const int kind=marker.kind==NWorld::LinuxDynamicWorldMarker::KIND_HERO ? NDb::ANIMSETID_HERO : NDb::ANIMSETID_CREEP;
			if (root && root->visualRoot && kind<root->visualRoot->animSets.sets.size())
				entry.player=LinuxAnimationGraphPlayback::Create(asset,root->visualRoot->animSets.sets[kind]);
			if (entry.player) ++created; else ++unsupported;
		}
		if (!entry.player) return nullptr;
		if (!entry.player->Advance(clock,marker.moving)) { ++failed; return nullptr; }
		++sampled; if (marker.moving) ++movingSamples;
		return entry.player.get();
	}
	size_t Size() const { return entries.size(); }
};
