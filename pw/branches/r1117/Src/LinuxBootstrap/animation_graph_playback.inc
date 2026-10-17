#include "animation_graph_admission.inc"
#include "animation_graph_locomotion.inc"
#include "animation_graph_attack.inc"

/** Native presentation owner for the original animation graph and CPU skeletal blend.
 * Instances are independent; this adapter never sends gameplay events or mutates DB data.
 */
class LinuxAnimationGraphPlayback
{
public:
	using Kind=NWorld::LinuxDeathPresentation::Kind;
	/** Admit validated single-skeleton assets before calling the original fixed-capacity graph API.
	 * Dynamic external parameters and upper-body graphs remain unsupported in this adapter.
	 */
	static std::unique_ptr<LinuxAnimationGraphPlayback> Create(const LinuxHeroSceneAssetPreview& asset, const NDb::AnimSet* set)
	{
		if (!asset.animationSkinningPreview.ready || !asset.animationPreview.ready || !asset.skeletonPreview.ready ||
			asset.animatedComponentCount!=1 || !asset.animationComponent || !set ||
			set->states.size()<=NDb::ANIMSTATES_MOVE || asset.animationPreview.sourceFileSize>32u*1024u*1024u) return {};
		const auto& component=*asset.animationComponent;
		if (!LinuxAnimationGraphSupported(component)) return {};
		const auto idle=set->states[NDb::ANIMSTATES_IDLE],move=set->states[NDb::ANIMSTATES_MOVE];
		const auto mapped=[&](const NDb::AnimStateData& state) {
			return (state.playMode==NDb::ANIMSTATEPLAYMODE_PLAYNORMAL || state.playMode==NDb::ANIMSTATEPLAYMODE_PLAYBOTH) &&
				!state.commonNode.empty() && std::any_of(component.animGraph->macroNodes.begin(),component.animGraph->macroNodes.end(),
					[&](const auto& node) { return node.nodeName==state.commonNode; });
		};
		if (!mapped(idle) || !mapped(move)) return {};
		// Resource-manager ownership outlives the presentation. Validate every sample before construction.
		const auto* skeleton=Render::RenderResourceManager::LoadSkeletonData(component.skeletonFileName);
		if (!skeleton || !skeleton->GetData() || skeleton->GetData()->jointsCount!=asset.skeletonPreview.jointsCount) return {};
		for (const auto& animation : component.animations) {
			const auto* data=Render::RenderResourceManager::LoadSkeletalAnimation(animation.animationFileName);
			if (!data || !data->GetData() || data->GetData()->tracks.size()!=skeleton->GetData()->jointsCount) return {};
		}
		auto result=std::unique_ptr<LinuxAnimationGraphPlayback>(new LinuxAnimationGraphPlayback);
		result->component=asset.animationComponent;
		result->applicator.reset(new NScene::AnimGraphApplicator(&component,new Render::SkeletalMesh));
		result->blender.reset(new NScene::AnimGraphBlender(result->applicator.get()));
		result->graph.reset(DiAnimGraph::CreateByData(component.animGraph,component.animations,result->blender.get()));
		if (!result->graph) return {};
		result->blender->SetAnimGraphs(result->graph.get(),nullptr);
		result->parameters.reset(new DiAnGrExtPars(component.animGraph,result->graph.get()));
		result->parameters->applyParamsToGraph(); result->graph->StoreDefaultValues();
		result->idle=result->graph->GetNodeIDByNameSlowQuite(idle.commonNode.c_str());
		result->move=result->graph->GetNodeIDByNameSlowQuite(move.commonNode.c_str());
		if (set->states.size()>NDb::ANIMSTATES_ATTACK && mapped(set->states[NDb::ANIMSTATES_ATTACK])) {
			result->attack=result->graph->GetNodeIDByNameSlowQuite(set->states[NDb::ANIMSTATES_ATTACK].commonNode.c_str());
			result->attackTiming.Initialize(*result->graph,*component.animGraph,set->states[NDb::ANIMSTATES_ATTACK].commonMarker);
		}
		if (set->states.size()>NDb::ANIMSTATES_DEATH && mapped(set->states[NDb::ANIMSTATES_DEATH]))
			result->death=result->graph->GetNodeIDByNameSlowQuite(set->states[NDb::ANIMSTATES_DEATH].commonNode.c_str());
		if (set->states.size()>NDb::ANIMSTATES_UNSUMMON && mapped(set->states[NDb::ANIMSTATES_UNSUMMON]))
			result->unsummon=result->graph->GetNodeIDByNameSlowQuite(set->states[NDb::ANIMSTATES_UNSUMMON].commonNode.c_str());
		if (set->states.size()>NDb::ANIMSTATES_STUN && mapped(set->states[NDb::ANIMSTATES_STUN]))
			result->stun=result->graph->GetNodeIDByNameSlowQuite(set->states[NDb::ANIMSTATES_STUN].commonNode.c_str());
		// Match original terminal events once per mapped node, including shared death/unsummon aliases.
		const auto registerTerminal=[&](int node) {
			return node>=0 && static_cast<unsigned>(node)<result->graph->GetNumNodes() &&
				result->graph->RegisterCallback(DIANGR_NODE,node,DeathComplete,DIANGR_CF_EXACT_PARAM,result.get(),.99f);
		};
		const int authoredDeath=result->death;
		if (!registerTerminal(result->death)) result->death=-1;
		if (result->unsummon==authoredDeath) result->unsummon=result->death;
		else if (!registerTerminal(result->unsummon)) result->unsummon=-1;
		if (result->idle<0 || result->move<0 || static_cast<unsigned>(result->idle)>=result->graph->GetNumNodes() ||
			static_cast<unsigned>(result->move)>=result->graph->GetNumNodes()) return {};
		if (!result->locomotion.Initialize(*result->graph,result->move)) return {};
		result->matrices.resize(skeleton->GetData()->jointsCount);
		result->applicator->GetModel()->GetSkeletonWrapper()->SetActiveBones(result->matrices.size());
		return result;
	}
	/** Advance once per timestamp, with bounded catch-up; rewind/long suspension restarts the graph.
	 * The supplied clock cannot fire gameplay damage/cast callbacks; live units use world time.
	 */
	bool Advance(double seconds, bool moving)
	{
		if (!std::isfinite(seconds) || seconds<0 || seconds>1e9) return false;
		if (dying || stunned || frozen) moving=false;
		if (frozen && last>=0 && seconds>=last) {
			// Consume world time while holding the established pose, including long frozen gaps.
			last=seconds; wasMoving=false; dirty=false; return true;
		}
		// A first/rewound frozen owner needs one valid original pose before its graph is frozen.
		if (frozen) graph->UnFreeze();
		if (seconds==last && moving==wasMoving && !dirty) return true;
		const bool restart=last<0 || seconds<last || seconds-last>1;
		// A suspended corpse is hidden instead of replaying death from its first frame.
		if (restart && dying) return false;
		if (restart) {
			CancelAbility();
			CancelAttack();
			if (!blender->Restart(graph->GetDefNode())) return false;
			target=-1; attackUntil=-1;
		}
		if (Attacking() && (moving || seconds>=attackUntil || !attackTiming.BoundedRate())) CancelAttack();
		if (Casting() && (moving || (abilityTiming && !abilityTiming->BoundedRate()))) CancelAbility();
		const int requested=dying ? terminalNode : stunned && !frozen ? (stun>=0 ? stun : idle) : Casting() ? abilityNode : Attacking() ? attack : moving ? move : idle;
		if (requested!=target) {
			if (!blender->SetMainAGTargetNode(requested)) return false;
			target=requested;
		}
		double remaining=restart ? 0 : seconds-last;
		do {
			const double at=seconds-remaining;
			if (Casting() && at>=abilityUntil) CancelAbility();
			const float step=static_cast<float>(std::min(std::min(.1,remaining),Casting() ? abilityUntil-at : remaining));
			if (!blender->Update(step)) return false;
			remaining-=step;
			// Sample a fire marker crossed in this frame before releasing the authored work window.
			if (Casting() && seconds-remaining>=abilityUntil-1e-7) CancelAbility();
		} while (remaining>1e-7);
		auto* model=applicator->GetModel(); model->Update(false);
		const auto* pose=model->GetSkeletonWrapper()->GetSkinWorldMatrices();
		if (!pose) return false;
		for (size_t i=0; i<matrices.size(); ++i) {
			const float* values=reinterpret_cast<const float*>(pose+i);
			for (size_t j=0; j<12; ++j) if (!std::isfinite(values[j])) return false;
		}
		std::copy(pose,pose+matrices.size(),matrices.begin());
		if (deathFinished && deathCompletedAt<0) deathCompletedAt=seconds;
		if (frozen) graph->Freeze();
		last=seconds; wasMoving=moving; dirty=false; ++updates; return true;
	}
	const std::vector<Matrix43>& Matrices() const { return matrices; }
	/** Apply world movement speed to original locomotion coefficients only. */
	bool SetMoveSpeed(double speed) { return locomotion.Apply(speed); }
	/** Select original stun or freeze presentation without changing world flags. */
	void SetControlState(bool nextStunned, bool nextFrozen)
	{
		if (dying || (stunned==nextStunned && frozen==nextFrozen)) return;
		const bool thaw=frozen && !nextFrozen;
		stunned=nextStunned; frozen=nextFrozen; CancelAbility(); CancelAttack(); dirty=true;
		if (frozen) graph->Freeze(); else { graph->UnFreeze(); target=-1; }
		// Original OnFreeze(false) forces idle; do not catch up the frozen interval.
		if (thaw) last=-1;
	}
	bool Stunned() const { return stunned; }
	bool Frozen() const { return frozen; }
	/** Select the authored terminal state without fallback or simulation callbacks.
	 * Repeated requests are idempotent only for the same kind; EndDeath releases the old identity.
	 */
	bool BeginDeath(Kind kind=Kind::Death)
	{
		const int node=kind==Kind::Death ? death : kind==Kind::Unsummon ? unsummon : -1;
		if (last<0 || node<0 || static_cast<unsigned>(node)>=graph->GetNumNodes()) return false;
		if (dying) return terminalKind==kind && terminalNode==node;
		graph->UnFreeze();
		if (!blender->SetMainAGTargetNode(node)) { if (frozen) graph->Freeze(); return false; }
		attackTiming.Cancel();
		if (abilityTiming) abilityTiming->Cancel();
		abilityUntil=-1; abilityTiming=nullptr;
		stunned=frozen=false;
		terminalKind=kind; terminalNode=node;
		dying=true; deathFinished=false; deathCompletedAt=-1; attackUntil=-1; target=node; dirty=true; return true;
	}
	/** Resurrection discards the previous terminal identity, pose and clock. */
	void EndDeath() { if (dying) { dying=false; terminalKind=Kind::Death; terminalNode=-1; deathFinished=false; deathCompletedAt=-1; last=-1; target=-1; attackUntil=-1; dirty=true; } }
	bool Dying() const { return dying; }
	/** World-clock frame of the selected terminal state's original 99% callback, or -1. */
	double DeathCompletedAt() const { return deathCompletedAt; }
	/** Reach the session fire marker when supported; otherwise preserve normal-rate attack-state playback.
	 * Successful calls report whether synchronization was admitted, independently of visual fallback.
	 */
	bool BeginAttack(double period, double fireDelay=0, bool* synchronized=nullptr)
	{
		if (dying || stunned || frozen || Casting() || last<0 || attack<0 || static_cast<unsigned>(attack)>=graph->GetNumNodes() ||
			!std::isfinite(period) || period<=0 || period>60 || !std::isfinite(fireDelay) || fireDelay<0 ||
			fireDelay>60 || (fireDelay>0 && fireDelay<.001)) return false;
		if (!blender->SetMainAGTargetNode(attack)) return false;
		attackTiming.Cancel();
		const bool synced=fireDelay>0 && attackTiming.Reach(fireDelay);
		if (fireDelay>0 && !synced) {
			// SetTargetNode owns restoration of the original temporary speed and marker callback.
			if (!blender->SetMainAGTargetNode(attack)) { attackUntil=-1; target=-1; dirty=true; return false; }
		}
		if (synchronized) *synchronized=synced;
		target=attack; attackUntil=last+period; dirty=true; return true;
	}
	/** Cancel the presentation request without sending any simulation event. */
	void CancelAttack()
	{
		attackTiming.Cancel();
		if (Attacking()) { blender->SetMainAGTargetNode(idle); attackUntil=-1; target=-1; dirty=true; }
	}
	bool Attacking() const { return attackUntil>=0; }
	/** Select the original normal graph by DB name. Unsupported marker timing keeps normal-rate playback.
	 * Work time, not cooldown, owns the presentation window; absent metadata uses the original node duration.
	 */
	bool BeginAbility(const char* name,const char* marker,double work,double fire,bool* synchronized=nullptr)
	{
		if (dying || stunned || frozen || last<0 || !name || !marker || !name[0] ||
			strnlen(name,129)>128 || strnlen(marker,129)>128 || !std::isfinite(work) || work<0 || work>60 ||
			!std::isfinite(fire) || fire<0 || fire>60 || (fire>0 && fire<.001)) return false;
		const auto node=graph->GetNodeIDByNameSlowQuite(name);
		if (node>=graph->GetNumNodes()) return false;
		const double duration=work>0 ? work : graph->GetNodeDuration(node);
		if (!std::isfinite(duration) || duration<=0 || duration>60) return false;
		CancelAbility(); CancelAttack();
		if (!blender->SetMainAGTargetNode(node)) return false;
		bool synced=false;
		if (fire>0 && marker[0]) {
			auto it=abilityTimings.find(marker);
			if (it==abilityTimings.end() && abilityTimings.size()<16) {
				auto timing=std::unique_ptr<LinuxAnimationGraphAttack>(new LinuxAnimationGraphAttack);
				timing->Initialize(*graph,*component->animGraph,nstl::string(marker));
				it=abilityTimings.emplace(marker,std::move(timing)).first;
			}
			if (it!=abilityTimings.end()) { abilityTiming=it->second.get(); synced=abilityTiming->Reach(fire); }
			if (!synced && !blender->SetMainAGTargetNode(node)) { target=-1; dirty=true; return false; }
		}
		if (synchronized) *synchronized=synced;
		abilityNode=node; target=node; abilityUntil=last+duration; dirty=true; return true;
	}
	/** Original target selection restores any temporary marker speed; it cannot cancel world work. */
	void CancelAbility()
	{
		if (abilityTiming) abilityTiming->Cancel();
		abilityTiming=nullptr;
		if (Casting()) { blender->SetMainAGTargetNode(idle); abilityUntil=-1; target=idle; dirty=true; }
	}
	bool Casting() const { return abilityUntil>=0; }
	unsigned AbilityMarkerHits() const
	{
		unsigned hits=0; for (const auto& timing : abilityTimings) hits+=timing.second->Hits(); return hits;
	}
	/** Last sampled world time, or -1 after creation/reset; bounds visible-only event catch-up. */
	double LastSampleTime() const { return last; }
	unsigned AttackMarkerHits() const { return attackTiming.Hits(); }
	double GraphSpeed() const { return graph->GetGraphSpeedCoeff(); }
	int CurrentNode() const { return static_cast<int>(graph->GetCurNodeUnsafe()); }
	int TargetNode() const { return target; }
	unsigned Updates() const { return updates; }
private:
	/** Only the selected terminal node can complete presentation; outgoing-node callbacks are stale.
	 * Callback storage is graph-owned and destroyed while its presentation owner is still alive.
	 */
	static DiInt32 DeathComplete(const DiAnGrCallbackParams& event)
	{
		auto* owner=static_cast<LinuxAnimationGraphPlayback*>(event.vpUserData);
		if (!owner || !owner->dying || owner->terminalNode<0 ||
			static_cast<unsigned>(owner->terminalNode)>=owner->graph->GetNumNodes()) return DIOBJMSG_CONTINUE;
		const DiAnimNode* node=event.geElement==DIANGR_NODE ? event.anpNode :
			event.geElement==DIANGR_MACRONODE ? static_cast<const DiAnimNode*>(event.anmpMacronode) : nullptr;
		if (node && node==owner->graph->GetNodeData(owner->terminalNode)) owner->deathFinished=true;
		return DIOBJMSG_CONTINUE;
	}
	struct DeleteGraph { void operator()(DiAnimGraph* graph) const { if (graph) DiAnimGraph::Destroy(graph); } };
	NDb::Ptr<NDb::DBAnimatedSceneComponent> component;
	std::unique_ptr<NScene::AnimGraphApplicator> applicator;
	std::unique_ptr<NScene::AnimGraphBlender> blender;
	LinuxAnimationGraphAttack attackTiming;
	std::map<std::string,std::unique_ptr<LinuxAnimationGraphAttack>> abilityTimings;
	LinuxAnimationGraphAttack* abilityTiming=nullptr;
	std::unique_ptr<DiAnimGraph,DeleteGraph> graph;
	std::unique_ptr<DiAnGrExtPars> parameters;
	LinuxAnimationGraphLocomotion locomotion;
	std::vector<Matrix43> matrices;
	double last=-1,attackUntil=-1,deathCompletedAt=-1;
	double abilityUntil=-1;
	int abilityNode=-1;
	int idle=-1,move=-1,attack=-1,death=-1,unsummon=-1,stun=-1,target=-1;
	int terminalNode=-1;
	Kind terminalKind=Kind::Death;
	bool wasMoving=false,dirty=false,dying=false,deathFinished=false;
	bool stunned=false,frozen=false;
	unsigned updates=0;
};
