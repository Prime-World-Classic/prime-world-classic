/** Native presentation owner for the original animation graph and CPU skeletal blend.
 * Instances are independent; this adapter never sends gameplay events or mutates DB data.
 */
class LinuxAnimationGraphPlayback
{
public:
	/** Admit validated single-skeleton assets before calling the original fixed-capacity graph API.
	 * Dynamic external parameters and upper-body graphs remain unsupported in this adapter.
	 */
	static std::unique_ptr<LinuxAnimationGraphPlayback> Create(const LinuxHeroSceneAssetPreview& asset, const NDb::AnimSet* set)
	{
		if (!asset.animationSkinningPreview.ready || !asset.animationPreview.ready || !asset.skeletonPreview.ready ||
			asset.animatedComponentCount!=1 || !asset.animationComponent || !set ||
			set->states.size()<=NDb::ANIMSTATES_MOVE || asset.animationPreview.sourceFileSize>32u*1024u*1024u) return {};
		const auto& component=*asset.animationComponent;
		if (!Supported(component)) return {};
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
		if (set->states.size()>NDb::ANIMSTATES_ATTACK && mapped(set->states[NDb::ANIMSTATES_ATTACK]))
			result->attack=result->graph->GetNodeIDByNameSlowQuite(set->states[NDb::ANIMSTATES_ATTACK].commonNode.c_str());
		if (result->idle<0 || result->move<0 || static_cast<unsigned>(result->idle)>=result->graph->GetNumNodes() ||
			static_cast<unsigned>(result->move)>=result->graph->GetNumNodes()) return {};
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
		if (seconds==last && moving==wasMoving && !dirty) return true;
		const bool restart=last<0 || seconds<last || seconds-last>1;
		if (restart) {
			if (!blender->Restart(graph->GetDefNode())) return false;
			target=-1; attackUntil=-1;
		}
		if (Attacking() && (moving || seconds>=attackUntil)) CancelAttack();
		const int requested=Attacking() ? attack : moving ? move : idle;
		if (requested!=target) {
			if (!blender->SetMainAGTargetNode(requested)) return false;
			target=requested;
		}
		double remaining=restart ? 0 : seconds-last;
		do {
			const float step=static_cast<float>(std::min(.1,remaining));
			if (!blender->Update(step)) return false;
			remaining-=step;
		} while (remaining>1e-7);
		auto* model=applicator->GetModel(); model->Update(false);
		const auto* pose=model->GetSkeletonWrapper()->GetSkinWorldMatrices();
		if (!pose) return false;
		for (size_t i=0; i<matrices.size(); ++i) {
			const float* values=reinterpret_cast<const float*>(pose+i);
			for (size_t j=0; j<12; ++j) if (!std::isfinite(values[j])) return false;
		}
		std::copy(pose,pose+matrices.size(),matrices.begin());
		last=seconds; wasMoving=moving; dirty=false; ++updates; return true;
	}
	const std::vector<Matrix43>& Matrices() const { return matrices; }
	/** Start one visual attack window; combat remains authoritative and independent. */
	bool BeginAttack(double period)
	{
		if (last<0 || attack<0 || static_cast<unsigned>(attack)>=graph->GetNumNodes() ||
			!std::isfinite(period) || period<=0 || period>60) return false;
		if (!blender->SetMainAGTargetNode(attack)) return false;
		target=attack; attackUntil=last+period; dirty=true; return true;
	}
	/** Cancel the presentation request without sending any simulation event. */
	void CancelAttack() { if (Attacking()) { attackUntil=-1; target=-1; dirty=true; } }
	bool Attacking() const { return attackUntil>=0; }
	int CurrentNode() const { return static_cast<int>(graph->GetCurNodeUnsafe()); }
	int TargetNode() const { return target; }
	unsigned Updates() const { return updates; }
private:
	/** Original names use a 36-byte fixed slot and the original loader accepts at most 60 clips. */
	static bool Supported(const NDb::DBAnimatedSceneComponent& component)
	{
		if (!component.animGraph || component.additionalGraph || component.animations.empty() || component.animations.size()>60 ||
			component.animGraph->macroNodes.empty() || component.animGraph->macroNodes.size()>128) return false;
		std::set<std::string> names,clips;
		for (const auto& animation : component.animations)
			if (animation.name.empty() || animation.name.size()>=36 || animation.animationFileName.empty() ||
				!clips.insert(animation.name.c_str()).second) return false;
		for (const auto& node : component.animGraph->macroNodes)
			if (node.nodeName.empty() || node.nodeName.size()>=36 || !names.insert(node.nodeName.c_str()).second) return false;
		if (!names.count(component.animGraph->defaultNode.c_str())) return false;
		const auto constant=[](const NDb::AnGrFormula& formula) { return formula.coeffs.empty() && std::isfinite(formula.value); };
		const auto unit=[&](const NDb::AnGrFormula& formula) { return constant(formula) && formula.value>=0 && formula.value<=1; };
		const auto markers=[](const nstl::vector<NDb::AnGrMarker>& values) {
			if (values.size()>128) return false;
			for (const auto& value : values) if (value.name.empty() || value.name.size()>128 ||
				!std::isfinite(value.virtPar) || value.virtPar<0 || value.virtPar>1) return false;
			return true;
		};
		size_t edges=0;
		for (const auto& node : component.animGraph->macroNodes) {
			if (node.nodeType<NDb::ANGRNODETYPE_NORMAL || node.nodeType>NDb::ANGRNODETYPE_SUBNODE ||
				!names.count(node.nextNode.c_str()) || !clips.count(node.sequenceName.c_str()) ||
				!unit(node.startPar) || !unit(node.stopPar) || node.startPar.value>node.stopPar.value ||
				!constant(node.speed) || node.speed.value<=0 || node.speed.value>100 ||
				!std::isfinite(node.surfSpeedX) || !std::isfinite(node.surfSpeedY) || !markers(node.markers)) return false;
			std::set<std::string> targets; double probability=0;
			for (const auto& edge : node.simpleTransactions) {
				const auto& p=edge.parameters;
				if (++edges>4096 || !names.count(edge.targetNode.c_str()) || !targets.insert(edge.targetNode.c_str()).second ||
					!std::isfinite(edge.probability) || edge.probability<0 || edge.probability>100 ||
					!unit(p.fromMin) || !unit(p.fromMax) || p.fromMin.value>p.fromMax.value || !unit(p.toParam) ||
					!constant(p.parA) || !constant(p.parB) || !std::isfinite(p.duration) || p.duration<0 || p.duration>60 ||
					p.kind<NDb::ANGRTRANSACTIONKIND_UNIFORM || p.kind>NDb::ANGRTRANSACTIONKIND_RECSPLINE ||
					p.blending<NDb::ANGRTRANSACTIONUPDATE_POSETOPOSE || p.blending>NDb::ANGRTRANSACTIONUPDATE_ANIMTOANIM ||
					!markers(p.markers)) return false;
				probability+=edge.probability;
			}
			if (node.nodeType==NDb::ANGRNODETYPE_DISPATCHER && probability<=0) return false;
		}
		return true;
	}
	struct DeleteGraph { void operator()(DiAnimGraph* graph) const { if (graph) DiAnimGraph::Destroy(graph); } };
	NDb::Ptr<NDb::DBAnimatedSceneComponent> component;
	std::unique_ptr<NScene::AnimGraphApplicator> applicator;
	std::unique_ptr<NScene::AnimGraphBlender> blender;
	std::unique_ptr<DiAnimGraph,DeleteGraph> graph;
	std::unique_ptr<DiAnGrExtPars> parameters;
	std::vector<Matrix43> matrices;
	double last=-1,attackUntil=-1;
	int idle=-1,move=-1,attack=-1,target=-1;
	bool wasMoving=false,dirty=false;
	unsigned updates=0;
};
