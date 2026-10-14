/** Exercise admission against original graphs before their fixed path buffers and integer time conversion. */
template<class Check>
static void CheckLinuxAttackMarkerBounds(Check& check)
{
	for (int mode=0; mode<11; ++mode) {
		test::AnimationDataHolder data;
		auto* raw=new NDb::AnimGraphCreator; NDb::Ptr<NDb::AnimGraphCreator> creator=raw;
		const int count=mode==1 ? 11 : 2;
		for (int i=0; i<count; ++i) {
			const auto name=std::string("node")+std::to_string(i);
			data.AddAnimation(name.c_str(),1); test::CreateMacroNode(*raw,name.c_str(),1);
		}
		raw->defaultNode="node0";
		for (int i=0; i<count-1; ++i) if (mode!=9)
			test::CreateTransaction(raw->macroNodes[i],raw->macroNodes[i+1].nodeName,.01f);
		NDb::AnGrMarker marker; marker.name="fire"; marker.virtPar=.5f;
		if (mode==4) raw->macroNodes[0].simpleTransactions[0].parameters.markers.push_back(marker);
		else raw->macroNodes[count-1].markers.push_back(marker);
		if (mode==3) for (int i=1; i<15; ++i) {
			marker.name=(std::string("extra")+std::to_string(i)).c_str();
			raw->macroNodes[0].markers.push_back(marker);
		}
		test::MockAnimGraphBlender blender(data);
		auto* graph=DiAnimGraph::CreateByData(creator,data.animations,&blender);
		check(graph!=nullptr,"bounded marker fixture creates an original graph");
		if (!graph) continue;
		graph->StoreDefaultValues(); graph->Restart(0,true); graph->Update(0);
		if (mode==5) { graph->GetNodeData(1)->SetSubNode(true); graph->GetNodeData(1)->SetDispatcherNodeID(999); }
		LinuxAnimationGraphAttack timing;
		const bool initialized=timing.Initialize(*graph,*raw,mode==2 ? "missing" : "fire");
		if (mode>=2 && mode<=5) {
			check(!initialized && !timing.Reach(.4),"missing, excessive, transition and orphan-subnode markers reject safely");
		} else {
			check(initialized,"supported original node marker registers once");
			if (mode==6) graph->GetNodeData(1)->SetSpeed(0);
			if (mode==7) graph->GetNodeData(1)->SetSpeed(1e-9f);
			const bool reached=timing.Reach(mode==8 ? .001 : .4);
			if (mode==0) {
				check(reached && graph->Update(.41f) && timing.Hits()==1 && graph->GetGraphSpeedCoeff()==1,
					"original marker callback fires once and restores the normal rate");
			} else if (mode==10) {
				graph->GetNodeData(0)->SetSpeed(100);
				check(reached && !timing.BoundedRate(),"later locomotion changes cannot bypass the combined graph-rate bound");
			} else check(!reached && timing.Hits()==0,"long, unreachable, zero-speed, overflowing-time and excessive-rate paths reject");
		}
		timing.Cancel(); graph->SetTargetNode(0);
		check(graph->GetGraphSpeedCoeff()==1,"original target selection restores rate after accepted or rejected reach");
		DiAnimGraph::Destroy(graph);
	}
}

/** Observe original marker callbacks only; these graphs cannot send any gameplay damage or commands. */
template<class Check>
static void CheckLinuxAttackMarkers(Check& check, const LinuxHeroSceneAssetPreview& asset, const NDb::AnimSet* set)
{
	auto fast = LinuxAnimationGraphPlayback::Create(asset,set), slow = LinuxAnimationGraphPlayback::Create(asset,set);
	check(fast && slow && fast->Advance(0,false) && slow->Advance(0,false), "fire-marker owners initialize independently");
	if (!fast || !slow) return;
	check(fast->BeginAttack(1,.2) && slow->BeginAttack(1,.6), "original graph admits independent requested fire deadlines");
	check(fast->Advance(.1,false) && slow->Advance(.1,false) && fast->AttackMarkerHits()==0 && slow->AttackMarkerHits()==0,
		"no original fire marker is observed before either requested deadline");
	check(fast->Advance(.25,false) && slow->Advance(.25,false) && fast->AttackMarkerHits()==1 && slow->AttackMarkerHits()==0,
		"fast owner reaches its marker while independent slow owner is still winding up");
	check(std::memcmp(fast->Matrices().data(),slow->Matrices().data(),fast->Matrices().size()*sizeof(Matrix43))!=0,
		"different marker deadlines produce different real original skeletal poses");
	check(fast->GraphSpeed()==1 && slow->Advance(.65,false) && slow->AttackMarkerHits()==1 && slow->GraphSpeed()==1,
		"original marker callbacks restore each graph's ordinary rate");
	check(slow->Advance(.65,false) && slow->AttackMarkerHits()==1, "same-clock draws cannot emit another marker");
	check(fast->Advance(1.1,false) && fast->BeginAttack(1,.2) && fast->Advance(1.35,false) && fast->AttackMarkerHits()==2,
		"a later accepted attack can synchronize another original fire marker");
	for (int stop=0; stop<4; ++stop) {
		auto player=LinuxAnimationGraphPlayback::Create(asset,set);
		check(player && player->Advance(0,false) && player->BeginAttack(1,.4) && player->Advance(.05,false),
			"marker cancellation fixture starts original windup");
		if (!player) continue;
		if (stop==0) player->CancelAttack();
		if (stop==1) player->SetControlState(true,false);
		if (stop==2) player->SetControlState(true,true);
		if (stop==3) check(player->BeginDeath(), "death overrides a pending original fire-marker request");
		check(player->Advance(.6,false) && player->AttackMarkerHits()==0 && !player->Attacking(),
			"cancel, stun, freeze and death cannot deliver a cancelled visual marker");
		if (stop!=2) check(player->GraphSpeed()==1, "interrupted marker request restores original graph rate");
	}
	for (double bad : {-1., .0001, 61., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
		auto player=LinuxAnimationGraphPlayback::Create(asset,set); player->Advance(0,false);
		const auto pose=player->Matrices();
		check(!player->BeginAttack(1,bad) && !player->Attacking() && player->GraphSpeed()==1 &&
			std::memcmp(pose.data(),player->Matrices().data(),pose.size()*sizeof(Matrix43))==0,
			"invalid fire deadline preserves pose and cannot reach original integer conversion");
	}
	LinuxAnimationGraphBank bank; int world=0; auto scene=std::make_shared<int>(1);
	NWorld::LinuxDynamicWorldMarker marker; marker.objectId=1; marker.kind=marker.KIND_HERO; marker.moveSpeed=5;
	marker.attack.sequence=1; marker.attack.abilityObjectId=2; marker.attack.startedAt=0; marker.attack.period=1; marker.attack.fireOffset=.2f;
	bank.Begin(&world,scene,.1); auto* player=bank.Resolve(asset,marker);
	check(player && player->Attacking(), "visible cue requests only its remaining fire time");
	bank.Begin(&world,scene,.22); player=bank.Resolve(asset,marker);
	check(player && player->AttackMarkerHits()==1 && bank.attackSyncStarts==1 && bank.attackMarkers==1,
		"cue age is subtracted rather than adding a second windup, with observed marker diagnostics");
	marker.attack.sequence=2; marker.attack.startedAt=0;
	bank.Begin(&world,scene,.23); player=bank.Resolve(asset,marker);
	check(player && !player->Attacking() && bank.attackStarts==1, "already-fired cue is consumed without replaying a late attack");
	marker.attack.sequence=3; marker.attack.startedAt=.23; marker.attack.fireOffset=std::numeric_limits<float>::quiet_NaN();
	player=bank.Resolve(asset,marker);
	check(player && !player->Attacking() && bank.attackStarts==1, "malformed raw cue time cannot enter a graph request");
	for (int stop=0; stop<2; ++stop) {
		LinuxAnimationGraphBank stopped;
		marker.dead=false; marker.attack.cancelled=false; marker.attack.sequence=1;
		marker.attack.startedAt=0; marker.attack.fireOffset=.4f;
		stopped.Begin(&world,scene,0); player=stopped.Resolve(asset,marker);
		check(player && player->Attacking(),"independent bank starts the cancellation windup");
		if (stop==0) marker.attack.cancelled=true;
		else { marker.dead=true; marker.death.active=true; marker.death.sequence=1; marker.death.startedAt=.6; }
		stopped.Begin(&world,scene,.6); player=stopped.Resolve(asset,marker);
		check(player && !player->Attacking() && stopped.attackMarkers==0,
			"current cancellation or death precedes graph catch-up across the old fire deadline");
	}
	LinuxAnimationGraphBank lagged;
	marker.dead=false; marker.attack.cancelled=false; marker.attack.startedAt=0; marker.attack.fireOffset=.4f;
	lagged.Begin(&world,scene,0); player=lagged.Resolve(asset,marker);
	check(player && player->Attacking(),"visible owner starts before a slow presentation frame");
	lagged.Begin(&world,scene,.6); player=lagged.Resolve(asset,marker);
	check(player && player->Attacking() && lagged.attackStarts==1 && lagged.attackMarkers==1,
		"visible cue identity survives a frame gap beyond offscreen receipt expiry without cancellation or replay");
	LinuxAnimationGraphBank visible;
	marker.attack.sequence=0;
	visible.Begin(&world,scene,0); player=visible.Resolve(asset,marker);
	marker.attack.sequence=1; marker.attack.startedAt=.1; marker.attack.fireOffset=.2f;
	visible.Begin(&world,scene,.34); player=visible.Resolve(asset,marker);
	check(player && player->Attacking() && visible.attackStarts==1 && visible.attackMarkers==1,
		"already-visible owner catches up a new attack from its recorded start when first draw follows fire");
	LinuxAnimationGraphBank unseen;
	unseen.Begin(&world,scene,.34); player=unseen.Resolve(asset,marker);
	check(player && !player->Attacking() && unseen.attackStarts==0 && unseen.attackMarkers==0,
		"newly visible owner cannot reconstruct a fire event that happened offscreen");
	NDb::AnimSet missing; missing.Assign(*set);
	missing.states[NDb::ANIMSTATES_ATTACK].commonMarker="unsupported-fire";
	auto fallback=LinuxAnimationGraphPlayback::Create(asset,&missing);
	bool synced=true;
	check(fallback && fallback->Advance(0,false) && fallback->BeginAttack(1,.2,&synced) &&
		!synced && fallback->Attacking() && fallback->GraphSpeed()==1 && fallback->Advance(.3,false) && fallback->AttackMarkerHits()==0,
		"unsupported marker preserves prior attack-state playback without claiming synchronization");
}
