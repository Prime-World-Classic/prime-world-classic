/** Original shipped ability node and marker, independent of payment/damage and attack aliases. */
template<class Check>
static void CheckLinuxAbilityGraphs(Check& check, const LinuxHeroSceneAssetPreview& asset, const NDb::AnimSet* set)
{
	auto player=LinuxAnimationGraphPlayback::Create(asset,set);
	check(player && player->Advance(0,false),"ability fixture owns a sampled original graph");
	if (!player) return;
	const auto idle=player->TargetNode();
	bool synced=false;
	check(player->BeginAbility("ability1","doCast1",.6,.2,&synced) && synced && player->Casting(),
		"named ability uses original graph node and reach-marker synchronization");
	const auto cast=player->TargetNode();
	check(cast!=idle && player->Advance(.1,false) && player->Casting() && player->AbilityMarkerHits()==0,
		"cast owns its pose before the fire deadline");
	check(player->Advance(.3,false) && player->AbilityMarkerHits()==1 && player->GraphSpeed()==1,
		"original cast marker restores speed without simulation callbacks");
	check(player->Advance(.7,false) && !player->Casting() && player->TargetNode()==idle,
		"authored work window releases cast presentation independently of cooldown");
	for (int stop=0; stop<5; ++stop) {
		player->SetControlState(false,false); player->EndDeath(); player->Advance(1+stop,false);
		check(player->BeginAbility("ability1","doCast1",.6,.2),"later named cast starts after previous work");
		const auto hits=player->AbilityMarkerHits();
		if (stop==0) player->CancelAbility();
		if (stop==1) player->SetControlState(true,false);
		if (stop==2) player->SetControlState(true,true);
		if (stop==3) player->BeginDeath();
		check(player->Advance(1.3+stop,stop==4) && !player->Casting() && player->AbilityMarkerHits()==hits,
			"cancel, stun, freeze, death and movement suppress obsolete cast markers");
	}
	player->SetControlState(false,false); player->EndDeath(); player->Advance(10,false);
	check(!player->BeginAbility("missing","doCast1",.6,.2) && !player->Casting(),"unknown DB node does not substitute an ability clip");
	check(player->BeginAbility("ability1","missing",.6,.2,&synced) && !synced && player->Casting(),
		"unsupported cast marker retains normal-rate named-node playback");
	player->CancelAbility();
	for (double bad : {-1.,61.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
		check(!player->BeginAbility("ability1","doCast1",bad,.2) && !player->Casting(),"invalid cast work cannot mutate the graph");
	check(player->BeginAbility("ability1","",0,0) && player->Casting(),"zero metadata timing still admits the original named animation");
	player->CancelAbility();
	check(player->BeginAttack(1,.2) && player->BeginAbility("ability1","doCast1",.6,.2) && !player->Attacking(),
		"accepted ordinary ability replaces the prior attack presentation");
	check(!player->BeginAttack(1,.2) && player->Casting(),"old attack snapshot cannot override active cast work");
	{
		LinuxAnimationGraphBank bank; int world=0; auto scene=std::make_shared<int>(1);
		NWorld::LinuxDynamicWorldMarker marker; marker.objectId=1; marker.kind=NWorld::LinuxDynamicWorldMarker::KIND_HERO;
		marker.moveSpeed=5;
		bank.Begin(&world,scene,0); auto* live=bank.Resolve(asset,marker);
		marker.ability.sequence=1; marker.ability.instanceObjectId=3; marker.ability.startedAt=.1;
		marker.ability.workTime=.6f; marker.ability.fireOffset=.2f;
		std::strcpy(marker.ability.node,"ability1"); std::strcpy(marker.ability.marker,"doCast1");
		bank.Begin(&world,scene,.3); live=bank.Resolve(asset,marker);
		check(live && live->Casting() && bank.abilityStarts==1 && bank.abilitySyncStarts==1,
			"visible cast receipt catches up from accepted start rather than delaying to draw time");
		bank.Begin(&world,scene,.5); live=bank.Resolve(asset,marker);
		check(live && bank.abilityStarts==1 && bank.abilityMarkers==1,"slow frames preserve cast identity and complete one marker");
		marker.ability.cancelled=true;
		bank.Begin(&world,scene,.5); live=bank.Resolve(asset,marker);
		check(live && !live->Casting() && bank.abilityStarts==1,"current cast cancellation takes effect without advancing time");
		// Original SetTargetNode rejects restarting a non-looped node while still inside it.
		bank.Begin(&world,scene,1); live=bank.Resolve(asset,marker);
		marker.ability.cancelled=false; marker.ability.sequence=2; marker.ability.startedAt=1.1;
		bank.Begin(&world,scene,1.1); live=bank.Resolve(asset,marker);
		check(live && live->Casting() && bank.abilityStarts==2,"next cast starts after the original non-looped node exits");
		marker.ability.cancelled=true;
		bank.Begin(&world,scene,1.4); live=bank.Resolve(asset,marker);
		check(live && !live->Casting() && bank.abilityMarkers==1,"cancellation precedes catch-up across an obsolete cast deadline");
		bank.Begin(&world,scene,1.5); bank.Begin(&world,scene,1.6); live=bank.Resolve(asset,marker);
		check(live && !live->Casting() && bank.abilityStarts==2,"evicted cast owner never replays an old receipt");
	}
	{
		LinuxAnimationGraphBank bank; int world=0; auto scene=std::make_shared<int>(1);
		NWorld::LinuxDynamicWorldMarker marker; marker.objectId=1; marker.kind=NWorld::LinuxDynamicWorldMarker::KIND_HERO; marker.moveSpeed=5;
		marker.ability.sequence=1; marker.ability.instanceObjectId=3; marker.ability.startedAt=0; marker.ability.workTime=.1f;
		std::strcpy(marker.ability.node,"ability1");
		bank.Begin(&world,scene,double(marker.ability.workTime)); auto* live=bank.Resolve(asset,marker);
		check(live && !live->Casting() && bank.abilityStarts==0,"fresh owner cannot turn an expired work window into default-duration playback");
		marker.ability.sequence=2; marker.ability.startedAt=.2; marker.ability.workTime=.6f;
		bank.Begin(&world,scene,.2); live=bank.Resolve(asset,marker);
		marker.ability.sequence=3; marker.ability.startedAt=.3; marker.ability.node[0]=0;
		bank.Begin(&world,scene,.3); live=bank.Resolve(asset,marker);
		check(live && live->Casting() && bank.abilityStarts==1,"empty-node ability preserves the original graph instead of cancelling active cast work");
	}
}
