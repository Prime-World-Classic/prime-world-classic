/** Real Plane graph control transitions, not a substitute animation implementation. */
template<class Check>
static void CheckLinuxGraphControls(Check& check,const LinuxHeroSceneAssetPreview& asset,const NDb::AnimSet* set)
{
	auto player=LinuxAnimationGraphPlayback::Create(asset,set);
	check(player && player->SetMoveSpeed(5) && player->Advance(0,true) && player->Advance(.4,true),"control graph starts with actual moving Plane pose");
	if (!player) return;
	const auto moving=player->Matrices(); const auto updates=player->Updates();
	player->SetControlState(true,true);
	check(player->Frozen() && player->Advance(.8,true) && player->Matrices().size()==moving.size() &&
		std::memcmp(moving.data(),player->Matrices().data(),moving.size()*sizeof(Matrix43))==0,"freeze holds the exact original pose despite movement requests");
	check(player->Advance(10,true) && player->Updates()==updates &&
		std::memcmp(moving.data(),player->Matrices().data(),moving.size()*sizeof(Matrix43))==0,"long frozen intervals consume world time without catch-up or graph restart");
	check(!player->BeginAttack(1),"freeze cannot queue a new accepted attack animation");
	player->SetControlState(false,false);
	check(!player->Frozen() && player->Advance(10.1,false) && !player->Attacking(),"thaw restarts original idle without replaying attacks");
	player->SetControlState(true,false);
	check(player->Stunned() && player->Advance(10.2,true) && player->TargetNode()>=0 &&
		player->TargetNode()<asset.animationComponent->animGraph->macroNodes.size() &&
		asset.animationComponent->animGraph->macroNodes[player->TargetNode()].nodeName==set->states[NDb::ANIMSTATES_STUN].commonNode,
		"stun selects the original session alias even when movement is requested");
	check(!player->BeginAttack(1),"stun blocks attack presentation without changing combat");
	const auto stunned=player->Matrices();
	check(player->Advance(10.5,false) && std::memcmp(stunned.data(),player->Matrices().data(),stunned.size()*sizeof(Matrix43))!=0,
		"stun animation continues playing rather than freezing its skeleton");
	player->SetControlState(true,true); player->Advance(10.6,false);
	check(player->BeginDeath() && !player->Frozen() && player->Advance(10.8,false) && player->Dying(),"death explicitly unfreezes the original graph and overrides controls");
	player->EndDeath(); player->SetControlState(false,false);
	check(player->Advance(11,false) && !player->Stunned() && !player->Frozen(),"resurrection cannot retain old graph control state");
	auto initial=LinuxAnimationGraphPlayback::Create(asset,set);
	if (initial) {
		initial->SetControlState(true,true);
		check(initial->Advance(0,false) && !initial->Matrices().empty(),"new frozen owner initializes a complete original pose before freezing");
		const auto held=initial->Matrices();
		check(initial->Advance(.5,false) && std::memcmp(held.data(),initial->Matrices().data(),held.size()*sizeof(Matrix43))==0,
			"initially frozen owner holds its established pose");
		check(!initial->Advance(std::numeric_limits<double>::quiet_NaN(),false) && initial->Frozen(),"invalid frozen clocks preserve control ownership");
		check(initial->Advance(.1,false) && initial->Frozen(),"rewound frozen clock initializes a new valid pose without thawing");
		const auto rewind=initial->Matrices();
		check(initial->Advance(.2,false) && std::memcmp(rewind.data(),initial->Matrices().data(),rewind.size()*sizeof(Matrix43))==0,
			"rewound frozen owner holds the reinitialized pose");
	}
	LinuxAnimationGraphBank bank; int world=0; auto map=std::make_shared<int>(1);
	NWorld::LinuxDynamicWorldMarker marker; marker.objectId=1; marker.kind=marker.KIND_HERO; marker.moveSpeed=5;
	bank.Begin(&world,map,0); auto* live=bank.Resolve(asset,marker);
	marker.stunned=true; bank.Begin(&world,map,.1); live=bank.Resolve(asset,marker);
	check(live && live->Stunned(),"live marker exports stun into its independent graph owner");
	marker.frozen=true; bank.Begin(&world,map,.2); live=bank.Resolve(asset,marker);
	check(live && live->Frozen(),"live stacked freeze reaches original graph playback");
	auto other=marker; other.objectId=2; other.stunned=other.frozen=false;
	auto* independent=bank.Resolve(asset,other);
	check(independent && !independent->Frozen() && !independent->Stunned(),"units sharing one asset retain independent control state");
	marker.attack.sequence=1; marker.attack.abilityObjectId=2; marker.attack.startedAt=.2; marker.attack.period=1;
	bank.Resolve(asset,marker);
	check(live && !live->Attacking() && bank.attackStarts==0,"attack cue received while controlled is consumed without playback");
	marker.stunned=marker.frozen=false; bank.Begin(&world,map,.3); live=bank.Resolve(asset,marker);
	check(live && !live->Attacking() && bank.attackStarts==0,"thaw cannot replay a previously observed controlled attack cue");
	auto* changed=new NDb::AnimSet(*set); NDb::Ptr<NDb::AnimSet> missing=changed;
	changed->states[NDb::ANIMSTATES_STUN].commonNode="missing";
	auto fallback=LinuxAnimationGraphPlayback::Create(asset,missing);
	check(bool(fallback),"missing optional stun alias keeps ordinary playback supported");
	if (fallback) {
		fallback->SetControlState(true,false);
		check(fallback->Advance(0,true) && !fallback->BeginAttack(1),"unsupported stun uses idle while still suppressing attack presentation");
	}
}
