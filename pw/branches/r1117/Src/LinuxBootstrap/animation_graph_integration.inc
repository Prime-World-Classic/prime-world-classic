/** Frame-bounded independent unit graphs. A map/world replacement discards old playback.
 * Unsupported assets keep the existing first-clip preview; no graph callback reaches PFWorld.
 */
class LinuxAnimationGraphBank
{
	struct Entry {
		const void* asset=nullptr;
		const void* component=nullptr;
		bool used=false;
		float heading=0;
		bool hasFacing=false;
		unsigned long long deathSequence=0;
		unsigned long long castSequence=0;
		unsigned markerHits=0;
		unsigned abilityMarkerHits=0;
		std::unique_ptr<LinuxAnimationGraphPlayback> player;
	};
	std::map<int,Entry> entries;
	struct Cue { int ability=-1; unsigned long long sequence=0,castSequence=0; double seen=-1; };
	std::map<int,Cue> cues;
	const void* world=nullptr;
	std::weak_ptr<const void> scene;
	double clock=-1;
public:
	size_t created=0,unsupported=0,sampled=0,movingSamples=0,failed=0;
	size_t attackStarts=0,attackSkipped=0,attackFaces=0;
	size_t attackSyncStarts=0,attackMarkers=0;
	size_t abilityStarts=0,abilitySyncStarts=0,abilityMarkers=0;
	size_t abilitySkipped=0,abilityFaces=0;
	size_t deathStarts=0,deathSamples=0;
	/** Release borrowers before an exact scene asset leaves the mesh working set.
	 * Other graph players, cue histories, counters and the current world clock stay intact.
	 * Returns the number of removed owners; null or already-forgotten assets are no-ops.
	 */
	size_t ForgetAsset(const LinuxHeroSceneAssetPreview* asset)
	{
		if (!asset) return 0;
		size_t removed = 0;
		for (auto it = entries.begin(); it != entries.end();) {
			if (it->second.asset != asset) { ++it; continue; }
			cues.erase(it->first);
			it = entries.erase(it);
			++removed;
		}
		return removed;
	}

	/** Retain only the previous visible frame; at most 58 meshes are admitted by the caller. */
	void Begin(const void* nextWorld,const std::shared_ptr<const void>& nextScene,double seconds)
	{
		if (world!=nextWorld || scene.lock()!=nextScene || seconds<clock) { entries.clear(); cues.clear(); }
		world=nextWorld; scene=nextScene; clock=seconds;
		for (auto it=entries.begin(); it!=entries.end();) {
			if (!it->second.used) it=entries.erase(it);
			else { it->second.used=false; ++it; }
		}
		for (auto it=cues.begin(); it!=cues.end();) {
			// A slow frame must not forget the identity of a still-owned attack graph.
			if (seconds-it->second.seen>.25 && entries.find(it->first)==entries.end()) it=cues.erase(it); else ++it;
		}
	}
	/** Resolve the SessionVisualRoot's actual Hero/Creep aliases, never infer clip names. */
	LinuxAnimationGraphPlayback* Resolve(const LinuxHeroSceneAssetPreview& asset,const NWorld::LinuxDynamicWorldMarker& marker,float* heading=nullptr,float* corpseDrop=nullptr)
	{
		if (marker.objectId<0 || !std::isfinite(clock) || clock<0) return nullptr;
		auto it=entries.find(marker.objectId);
		// Only already-visible owners can start a fresh death; never recreate an offscreen corpse.
		if (marker.dead && (!marker.death.active || it==entries.end())) return nullptr;
		if (it==entries.end()) {
			if (entries.size()>=128) { ++failed; return nullptr; }
			it=entries.emplace(marker.objectId,Entry{}).first;
		}
		auto& entry=it->second; entry.used=true;
		if (entry.asset!=&asset || entry.component!=asset.animationComponent.GetPtr()) {
			if (marker.dead) return nullptr;
			entry.asset=&asset; entry.component=asset.animationComponent.GetPtr(); entry.player.reset();
			entry.heading=0; entry.hasFacing=false; entry.deathSequence=0; entry.markerHits=0;
			entry.abilityMarkerHits=0; entry.castSequence=0;
			const auto root=NDb::SessionRoot::GetRoot();
			const int kind=marker.kind==NWorld::LinuxDynamicWorldMarker::KIND_HERO ? NDb::ANIMSETID_HERO : NDb::ANIMSETID_CREEP;
			if (root && root->visualRoot && kind<root->visualRoot->animSets.sets.size())
				entry.player=LinuxAnimationGraphPlayback::Create(asset,root->visualRoot->animSets.sets[kind]);
			if (entry.player) ++created; else ++unsupported;
		}
		if (!entry.player) return nullptr;
		if (marker.dead) {
			const auto& death=marker.death;
			const double age=clock-death.startedAt;
			if (!death.sequence || !std::isfinite(age) || age<0) return nullptr;
			if (entry.deathSequence!=death.sequence) {
				if (age>.25) return nullptr;
				entry.player->EndDeath();
				entry.player->CancelAbility();
				entry.player->CancelAttack();
				if (!entry.player->Advance(clock,false) || !entry.player->BeginDeath()) return nullptr;
				entry.deathSequence=death.sequence; ++deathStarts;
			}
			if (!entry.player->Advance(clock,false)) return nullptr;
			attackMarkers+=entry.player->AttackMarkerHits()-entry.markerHits; entry.markerHits=entry.player->AttackMarkerHits();
			abilityMarkers+=entry.player->AbilityMarkerHits()-entry.abilityMarkerHits; entry.abilityMarkerHits=entry.player->AbilityMarkerHits();
			if (heading) *heading=entry.heading;
			if (corpseDrop) {
				*corpseDrop=0;
				const double completed=entry.player->DeathCompletedAt();
				const double falling=clock-std::max(death.fallStartedAt,completed);
				const double height=double(asset.meshPreview.maxZ)-asset.meshPreview.minZ;
				// Original StartFallThrough caps travel at three authored half-heights.
				if (completed>=0 && death.fallStartedAt>=0 && std::isfinite(falling) && falling>0 &&
					std::isfinite(death.fallSpeed) && death.fallSpeed>0 && std::isfinite(height) && height>0) {
					const double distance=std::min(falling*death.fallSpeed,1.5*height);
					if (std::isfinite(distance) && distance<=std::numeric_limits<float>::max()) *corpseDrop=static_cast<float>(distance);
				}
			}
			++sampled; ++deathSamples; return entry.player.get();
		}
		entry.player->EndDeath(); entry.deathSequence=0;
		entry.player->SetControlState(marker.stunned,marker.frozen);
		// Consume interruptions before catch-up can cross an obsolete fire deadline.
		const auto& attack=marker.attack;
		auto seen=cues.find(marker.objectId);
		if (seen==cues.end() && cues.size()<128) seen=cues.emplace(marker.objectId,Cue{}).first;
		const auto& ability=marker.ability;
		const bool castChanged=seen!=cues.end() && seen->second.castSequence!=ability.sequence;
		if ((castChanged && ability.node[0]) || (ability.cancelled && entry.castSequence==ability.sequence)) entry.player->CancelAbility();
		const double castAge=clock-ability.startedAt;
		const bool castEligible=castChanged && ability.sequence && !ability.cancelled && ability.instanceObjectId>=0 &&
			!marker.moving && !marker.stunned && !marker.frozen && std::isfinite(castAge) && castAge>=0 && castAge<=.25 &&
			std::isfinite(ability.workTime) && ability.workTime>=0 && ability.workTime<=60 &&
			std::isfinite(ability.fireOffset) && ability.fireOffset>=0 && ability.fireOffset<=60;
		if (castEligible && ability.node[0]) entry.player->CancelAttack();
		const bool changed=seen!=cues.end() && (seen->second.ability!=attack.abilityObjectId || seen->second.sequence!=attack.sequence);
		if (changed || attack.cancelled) entry.player->CancelAttack();
		const double age=clock-attack.startedAt;
		const bool eligible=changed && attack.sequence && !attack.cancelled && attack.abilityObjectId>=0 &&
			!(castEligible && ability.node[0]) && !entry.player->Casting() &&
			!marker.moving && !marker.stunned && !marker.frozen && std::isfinite(age) && age>=0 && age<=.25 &&
			std::isfinite(attack.period) && attack.period>0 && attack.period<=60 &&
			std::isfinite(attack.fireOffset) && attack.fireOffset>=0 && attack.fireOffset<=60;
		// A continuously visible owner can place a new cue between its preceding sample and this draw.
		// Fresh, rewound or long-suspended owners cannot reconstruct an offscreen fire event.
		const double previous=entry.player->LastSampleTime();
		const double eventStart=castEligible ? ability.startedAt : attack.startedAt;
		const bool catchUp=castEligible || (eligible && attack.fireOffset>0);
		const double sampleAt=catchUp && previous>=0 && eventStart>=previous && clock-previous<=1 ? eventStart : clock;
		if (corpseDrop) *corpseDrop=0;
		if (!entry.player->SetMoveSpeed(marker.moveSpeed)) { ++failed; return nullptr; }
		if (!entry.player->Advance(sampleAt,marker.moving)) { ++failed; return nullptr; }
		if ((!entry.hasFacing || (marker.moving && !marker.stunned && !marker.frozen)) && marker.hasMoveDirection &&
			LinuxBootstrap::FacingYaw(marker.moveDirX,marker.moveDirY,entry.heading)) entry.hasFacing=true;
		// Retain light cue identities briefly after graph eviction; old offscreen attacks are never queued.
		if (seen!=cues.end()) {
			auto& cue=seen->second;
			cue.castSequence=ability.sequence;
			if (castChanged && ability.sequence && !ability.cancelled) {
				const double elapsed=sampleAt-ability.startedAt;
				const double remainingFire=ability.fireOffset>0 ? ability.fireOffset-elapsed : 0;
				bool synchronized=false;
				if (castEligible && (ability.workTime==0 || elapsed<ability.workTime) && (ability.fireOffset==0 || remainingFire>=.001) &&
					entry.player->BeginAbility(ability.node,ability.marker,ability.workTime>0 ? ability.workTime-elapsed : 0,remainingFire,&synchronized)) {
					entry.castSequence=ability.sequence;
					++abilityStarts; if (synchronized) ++abilitySyncStarts;
				} else if (ability.node[0]) ++abilitySkipped;
				// Empty-node abilities still carry the original facing request, independently of animation support.
				if (castEligible && ability.lookKind!=NWorld::DontLook && ability.hasTarget &&
					LinuxBootstrap::FacingYaw(double(ability.targetX)-marker.x,double(ability.targetY)-marker.y,entry.heading)) {
					entry.hasFacing=true; ++abilityFaces;
				}
			}
			cue.seen=clock; cue.ability=attack.abilityObjectId; cue.sequence=attack.sequence;
			if (changed && attack.sequence && !attack.cancelled) {
				const double elapsed=sampleAt-attack.startedAt;
				const double remainingFire=attack.fireOffset>0 ? attack.fireOffset-elapsed : 0;
				bool synchronized=false;
				if (eligible && (attack.fireOffset==0 || remainingFire>=.001) &&
					entry.player->BeginAttack(attack.period-elapsed,remainingFire,&synchronized)) {
					++attackStarts;
					if (synchronized) ++attackSyncStarts;
					if (attack.hasTarget && LinuxBootstrap::FacingYaw(double(attack.targetX)-marker.x,double(attack.targetY)-marker.y,entry.heading)) {
						entry.hasFacing=true; ++attackFaces;
					}
				}
				else ++attackSkipped;
			}
			if (!entry.player->Advance(clock,marker.moving)) { ++failed; return nullptr; }
		}
		attackMarkers+=entry.player->AttackMarkerHits()-entry.markerHits; entry.markerHits=entry.player->AttackMarkerHits();
		abilityMarkers+=entry.player->AbilityMarkerHits()-entry.abilityMarkerHits; entry.abilityMarkerHits=entry.player->AbilityMarkerHits();
		++sampled; if (marker.moving) ++movingSamples;
		if (heading) *heading=entry.heading;
		return entry.player.get();
	}
	size_t Size() const { return entries.size(); }
	size_t CueSize() const { return cues.size(); }
};
