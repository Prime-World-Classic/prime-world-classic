#include "Scene/AGHelpers.h"
#include "animation_graph_locomotion_probe.inc"

/** Shipped graph regression: Plane lists attack1 first, but its authored default is idle. */
static bool RunLinuxAnimationGraphEngineProbe(const LinuxClientEnvironment& environment)
{
	unsigned checks=0,failures=0;
	const auto check=[&](bool good,const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout,"Animation graph probe: %s result=%s\n",label,good ? "PASS" : "FAIL");
	};
	CheckLinuxGraphLocomotion(check);
	{
		// Existing original test doubles record the exact integer sequence IDs sent to the blender.
		test::AnimationDataHolder data;
		for (const char* name : {"zero","one","two"}) data.AddAnimation(name,1.f);
		auto* creator=new NDb::AnimGraphCreator;
		NDb::Ptr<NDb::AnimGraphCreator> ownedCreator=creator;
		creator->defaultNode="two";
		for (const char* name : {"zero","one","two"}) test::CreateMacroNode(*creator,name,1);
		test::CreateTransaction(creator->macroNodes[2],"one",.5f);
		test::MockAnimGraphBlender recorder(data);
		auto* graph=DiAnimGraph::CreateByData(creator,data.animations,&recorder);
		check(graph!=nullptr,"original mock graph constructs");
		if (graph) {
			graph->StoreDefaultValues(); graph->Restart(graph->GetDefNode(),true); graph->Update(0);
			check(recorder.GetLastFrame().size()==1 && recorder.GetLastFrame().animations[0]==2,"default node preserves sequence index two");
			graph->SetTargetNode(1); graph->Update(.25f);
			const auto& frame=recorder.GetLastFrame();
			check(frame.size()==2 && frame.animations[0]==2 && frame.animations[1]==1,
				"transition passes contiguous int sequence IDs to the original blender");
			check(frame.size()==2 && frame.weights[0]>.1f && frame.weights[1]>.1f,"both transition poses carry nonzero weights");
			DiAnimGraph::Destroy(graph);
		}
	}
	const auto scene=NDb::Get<NDb::DBSceneObject>(NDb::DBID("Heroes/Plane/_.SOBJ"));
	const auto set=NDb::Get<NDb::AnimSet>(NDb::DBID("GameLogic/AnimSets/Hero.ANS"));
	LinuxHeroSceneAssetPreview asset;
	ProbeHeroSceneObjectAsset(environment,scene.GetPtr(),"animation graph regression",&asset);
	check(asset.animationSkinningPreview.ready && asset.animatedComponentCount==1 && asset.animationComponent && set,
		"one original Plane skeleton, animation component and hero state map");
	if (!asset.animationComponent || !set) return false;
	const auto& component=*asset.animationComponent;
	check(component.animations.size()>1 && component.animations[0].name=="attack1" &&
		component.animGraph && component.animGraph->defaultNode=="idle","first listed clip is not the authored graph default");
	auto player=LinuxAnimationGraphPlayback::Create(asset,set);
	check(bool(player),"original graph playback is available");
	if (player) {
		check(player->Advance(0,false),"sample authored idle at initial clock");
		check(player->Matrices().size()==asset.animationSkinningPreview.jointsCount,"graph blend publishes a complete skeleton pose");
		std::vector<Matrix43> firstClip;
		check(ComputeLinuxHeroAnimationSkinMatrices(asset,0,&firstClip) && firstClip.size()==player->Matrices().size(),
			"legacy first-clip reference remains available");
		check(player->CurrentNode()>=0 && player->CurrentNode()<component.animGraph->macroNodes.size() &&
			std::string(component.animGraph->macroNodes[player->CurrentNode()].nodeName.c_str()).find("idle")==0,
			"authored default resolves to an idle node instead of the first attack1 clip");
		const auto initial=player->Matrices(); const auto before=player->Updates();
		check(player->Advance(0,false) && player->Updates()==before,"repeated draw at the same clock cannot advance animation");
		check(player->Advance(.2,true) && player->TargetNode()>=0,"movement uses the original state-map target");
		check(player->Advance(.4,true) && std::memcmp(initial.data(),player->Matrices().data(),initial.size()*sizeof(Matrix43))!=0,
			"authored graph transition produces a changed pose");
		const int moveTarget=player->TargetNode();
		check(player->Advance(.6,false) && player->TargetNode()!=moveTarget,"stop requests the authored idle state");
		auto other=LinuxAnimationGraphPlayback::Create(asset,set);
		check(other && other->Advance(0,false) && other->Updates()==1,"another unit owns an independent graph clock");
		check(player->Advance(0,false),"clock rewind restarts the owned graph");
		const auto held=player->Matrices(); const auto heldUpdates=player->Updates();
		for (double bad : {-1., std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
			check(!player->Advance(bad,true) && player->Updates()==heldUpdates &&
				std::memcmp(held.data(),player->Matrices().data(),held.size()*sizeof(Matrix43))==0,"invalid clock preserves the last good pose");
	}
	check(!LinuxAnimationGraphPlayback::Create(LinuxHeroSceneAssetPreview{},set),"unprepared asset cannot reach original graph code");
	{
		auto normal=LinuxAnimationGraphPlayback::Create(asset,set),fast=LinuxAnimationGraphPlayback::Create(asset,set);
		check(normal && fast && normal->SetMoveSpeed(5) && fast->SetMoveSpeed(10),"independent original Plane graphs admit different world speeds");
		if (normal && fast) {
			for (int i=0; i<=6; ++i) { normal->Advance(i*.1,true); fast->Advance(i*.1,true); }
			check(std::memcmp(normal->Matrices().data(),fast->Matrices().data(),normal->Matrices().size()*sizeof(Matrix43))!=0,
				"original Plane locomotion pose reflects independent movement speeds");
			check(fast->SetMoveSpeed(0) && fast->Advance(.7,true),"zero speed pauses an already-entered locomotion node");
			const auto stopped=fast->Matrices();
			check(fast->Advance(.8,true) && std::memcmp(stopped.data(),fast->Matrices().data(),stopped.size()*sizeof(Matrix43))==0,
				"zero world speed holds the current original locomotion pose");
			check(fast->SetMoveSpeed(5) && fast->Advance(.9,true) &&
				std::memcmp(stopped.data(),fast->Matrices().data(),stopped.size()*sizeof(Matrix43))!=0,
				"original locomotion resumes after a zero speed interval");
		}
	}
	{
		LinuxAnimationGraphBank bank; int world=0; auto map=std::make_shared<int>(1);
		NWorld::LinuxDynamicWorldMarker a; a.objectId=1; a.kind=a.KIND_HERO; a.moving=true; a.moveSpeed=5;
		auto b=a; b.objectId=2; b.moveSpeed=10;
		LinuxAnimationGraphPlayback *first=nullptr,*second=nullptr;
		for (int i=0; i<=6; ++i) { bank.Begin(&world,map,i*.1); first=bank.Resolve(asset,a); second=bank.Resolve(asset,b); }
		check(first && second && first!=second && bank.failed==0 &&
			std::memcmp(first->Matrices().data(),second->Matrices().data(),first->Matrices().size()*sizeof(Matrix43))!=0,
			"live unit cache applies each marker's actual movement speed before sampling");
		const auto before=first ? first->Updates() : 0;
		bank.Resolve(asset,a);
		check(first && first->Updates()==before,"repeated world speed cannot advance a same-clock pose twice");
		a.moveSpeed=std::numeric_limits<float>::infinity();
		check(!bank.Resolve(asset,a) && first && first->Updates()==before,"invalid live movement speed never reaches graph update");
		a.moveSpeed=2.5f; bank.Begin(&world,map,.7);
		check(bank.Resolve(asset,a)==first,"valid later movement speed recovers the same owned graph");
	}
	check(sizeof(DiInt32)==sizeof(int) && sizeof(DiUInt32)==4 && sizeof(DiBool)==4,"native graph keeps the original 32-bit integer contract");
	for (int malformed=0; malformed<5; ++malformed) {
		auto* altered=new NDb::AnimSet(*set);
		NDb::Ptr<NDb::AnimSet> held=altered;
		if (malformed==0) altered->states[NDb::ANIMSTATES_IDLE].commonNode="missing-node";
		if (malformed==1) altered->states[NDb::ANIMSTATES_MOVE].commonNode="";
		if (malformed==2) altered->states[NDb::ANIMSTATES_MOVE].playMode=NDb::ANIMSTATEPLAYMODE_PLAYUPPER;
		if (malformed==3) altered->states[NDb::ANIMSTATES_IDLE].playMode=static_cast<NDb::EAnimStatePlayMode>(99);
		if (malformed==4) altered->states.clear();
		check(!LinuxAnimationGraphPlayback::Create(asset,held),"missing, invalid or upper-only state aliases reject before playback");
	}
	for (int malformed=0; malformed<10; ++malformed) {
		LinuxHeroSceneAssetPreview changed=asset;
		auto* altered=new NDb::DBAnimatedSceneComponent(component); changed.animationComponent=altered;
		auto* graph=new NDb::AnimGraphCreator(*component.animGraph); altered->animGraph=graph;
		if (malformed==0) altered->animations[0].name="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
		if (malformed==1) altered->animations.resize(61);
		if (malformed==2) graph->defaultNode="missing";
		if (malformed==3) graph->macroNodes[0].sequenceName="missing";
		if (malformed==4) graph->macroNodes[0].startPar.value=std::numeric_limits<float>::quiet_NaN();
		if (malformed==5) graph->macroNodes[0].speed.coeffs.resize(1);
		if (malformed==6) graph->macroNodes[0].simpleTransactions[0].targetNode="missing";
		if (malformed==7) changed.animatedComponentCount=2;
		if (malformed==8) changed.animationPreview.sourceFileSize=33u*1024u*1024u;
		if (malformed==9) graph->macroNodes[0].simpleTransactions[0].parameters.markers.push_back(NDb::AnGrMarker{}).virtPar=std::numeric_limits<float>::infinity();
		check(!LinuxAnimationGraphPlayback::Create(changed,set),"unsafe or unsupported graph metadata never reaches the original fixed-capacity loader");
	}
	{
		LinuxAnimationGraphBank bank; int world=0,otherWorld=0;
		auto map=std::make_shared<int>(1),replacement=std::make_shared<int>(2);
		NWorld::LinuxDynamicWorldMarker first; first.objectId=10; first.kind=first.KIND_HERO;
		auto second=first; second.objectId=20; second.moving=true;
		bank.Begin(&world,map,0);
		auto* a=bank.Resolve(asset,first); auto* b=bank.Resolve(asset,second);
		check(a && b && a!=b && bank.created==2 && bank.Size()==2,"same model has independent per-unit graph ownership");
		check(bank.Resolve(asset,first)==a && bank.created==2 && a && a->Updates()==1,"repeated frame consumers reuse the same graph pose");
		bank.Begin(&world,map,.1); bank.Resolve(asset,first);
		bank.Begin(&world,map,.2);
		check(bank.Size()==1,"unvisited unit graphs retire at the next frame boundary");
		bank.Begin(&world,map,.3);
		check(bank.Size()==0,"all old unit graphs leave the bounded visible cache");
		bank.Resolve(asset,first); bank.Begin(&world,replacement,.4);
		check(bank.Size()==0,"map-owner replacement clears old playback");
		bank.Resolve(asset,first); bank.Begin(&otherWorld,replacement,.5);
		check(bank.Size()==0,"world-owner replacement clears old playback");
		bank.Resolve(asset,first); bank.Begin(&otherWorld,replacement,0);
		check(bank.Size()==0,"replay-clock rewind clears per-unit playback");
		first.dead=true;
		check(!bank.Resolve(asset,first) && bank.Size()==0,"dead markers do not resurrect a living animation");
		first.dead=false; first.objectId=-1;
		check(!bank.Resolve(asset,first) && bank.Size()==0,"invalid identity cannot own playback");
		LinuxHeroSceneAssetPreview unsupported;
		first.objectId=10; const auto before=bank.unsupported;
		check(!bank.Resolve(unsupported,first) && !bank.Resolve(unsupported,first) && bank.unsupported==before+1,
			"unsupported assets are admitted once per retained identity, not retried every draw");
		for (int i=0; i<128; ++i) { first.objectId=i; bank.Resolve(unsupported,first); }
		first.objectId=129;
		check(bank.Size()==128 && !bank.Resolve(unsupported,first) && bank.Size()==128,"live animation ownership has a hard capacity bound");
	}
	{
		auto attack=LinuxAnimationGraphPlayback::Create(asset,set);
		check(attack && attack->Advance(0,false),"attack graph has an initial pose");
		if (attack) {
			const int idle=attack->TargetNode();
			check(attack->BeginAttack(1) && attack->Attacking(),"accepted attack requests an authored attack state");
			check(attack->Advance(.2,false) && attack->TargetNode()!=idle,"attack request is not overwritten by stationary idle");
			check(attack->Advance(.5,false) && attack->Attacking(),"attack state survives within its accepted cooldown window");
			check(attack->Advance(1,false) && !attack->Attacking() && attack->TargetNode()==idle,"attack window returns to authored idle");
			check(attack->BeginAttack(1) && attack->Advance(1.1,true) && !attack->Attacking(),"movement interrupts the whole-body attack presentation");
			check(attack->BeginAttack(1),"another accepted attack can start independently");
			attack->CancelAttack();
			check(attack->Advance(1.1,false) && !attack->Attacking() && attack->TargetNode()==idle,"cancellation at the same clock restores locomotion");
			for (double bad : {0.,-1.,61.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
				check(!attack->BeginAttack(bad) && !attack->Attacking(),"invalid attack interval preserves the previous graph state");
		}
		LinuxAnimationGraphBank bank; int world=0; auto map=std::make_shared<int>(1);
		NWorld::LinuxDynamicWorldMarker marker; marker.objectId=1; marker.kind=marker.KIND_HERO;
		marker.attack.sequence=1; marker.attack.abilityObjectId=2; marker.attack.startedAt=0; marker.attack.period=1;
		bank.Begin(&world,map,0); auto* player=bank.Resolve(asset,marker);
		check(player && player->Attacking() && bank.attackStarts==1,"live accepted cue starts a unit graph exactly once");
		bank.Resolve(asset,marker);
		check(bank.attackStarts==1,"repeated draws cannot replay an accepted attack");
		marker.attack.cancelled=true; bank.Resolve(asset,marker);
		check(player && !player->Attacking(),"live cancellation stops the graph at the same world clock");
		marker.attack.cancelled=false; marker.attack.sequence=2; marker.attack.startedAt=.1;
		bank.Begin(&world,map,.1); player=bank.Resolve(asset,marker);
		check(player && player->Attacking() && bank.attackStarts==2,"later accepted cue starts after cancellation");
		bank.Begin(&world,map,.15); bank.Begin(&world,map,.2); player=bank.Resolve(asset,marker);
		check(player && !player->Attacking() && bank.attackStarts==2,"cue identity survives quick offscreen graph eviction");
		bank.Begin(&world,map,.5); bank.Begin(&world,map,.6); player=bank.Resolve(asset,marker);
		check(player && !player->Attacking() && bank.attackStarts==2,"offscreen eviction never replays an old attack on reentry");
		for (int bad=0; bad<5; ++bad) {
			marker.attack.sequence++; marker.attack.startedAt=.6; marker.attack.period=1;
			if (bad==0) marker.attack.startedAt=.7;
			if (bad==1) marker.attack.startedAt=std::numeric_limits<double>::quiet_NaN();
			if (bad==2) marker.attack.period=std::numeric_limits<float>::infinity();
			if (bad==3) marker.attack.period=0;
			if (bad==4) marker.moving=true;
			player=bank.Resolve(asset,marker);
			check(player && !player->Attacking() && bank.attackStarts==2,"future, malformed and moving attack cues never queue a delayed request");
		}
		auto* altered=new NDb::AnimSet(*set); NDb::Ptr<NDb::AnimSet> missing=altered;
		altered->states[NDb::ANIMSTATES_ATTACK].commonNode="missing";
		auto unsupported=LinuxAnimationGraphPlayback::Create(asset,missing);
		check(unsupported && unsupported->Advance(0,false) && !unsupported->BeginAttack(1),"unavailable attack alias preserves supported idle and move playback");
		LinuxAnimationGraphBank bounded;
		marker.moving=false; marker.attack.period=1; marker.attack.startedAt=0;
		for (int i=0; i<129; ++i) {
			marker.objectId=i; bounded.Begin(&world,map,i*.001); bounded.Resolve(asset,marker);
		}
		check(bounded.CueSize()==128 && bounded.Size()<=2 && bounded.attackStarts==128,
			"brief cue receipts remain bounded independently of heavyweight graph eviction");
		bounded.Begin(&world,map,1);
		check(bounded.CueSize()==0,"expired cue receipts release their bounded capacity");
	}
	{
		LinuxAnimationGraphBank bank; int world=0; auto map=std::make_shared<int>(1);
		NWorld::LinuxDynamicWorldMarker marker; marker.objectId=1; marker.kind=marker.KIND_HERO;
		marker.hasMoveDirection=true; marker.moveDirY=1;
		float heading=999;
		bank.Begin(&world,map,0); bank.Resolve(asset,marker,&heading);
		check(heading==0,"new visible unit uses its original forward direction");
		marker.attack.sequence=1; marker.attack.abilityObjectId=2; marker.attack.startedAt=0;
		marker.attack.period=1; marker.attack.hasTarget=true; marker.attack.targetX=10;
		bank.Resolve(asset,marker,&heading);
		check(std::abs(heading-90)<.001f,"accepted attack faces its captured victim position");
		marker.attack.targetX=-10; bank.Resolve(asset,marker,&heading);
		check(std::abs(heading-90)<.001f,"same accepted cue cannot rotate again on repeated draws");
		marker.attack.cancelled=true; bank.Resolve(asset,marker,&heading);
		check(std::abs(heading-90)<.001f,"cancellation keeps the last visual heading while returning to idle");
		marker.moving=true; marker.moveDirX=-1; marker.moveDirY=0;
		bank.Begin(&world,map,.1); bank.Resolve(asset,marker,&heading);
		check(std::abs(heading+90)<.001f,"subsequent movement owns the visible unit heading");
		marker.moving=false; marker.moveDirX=0; marker.moveDirY=1;
		bank.Begin(&world,map,.2); bank.Resolve(asset,marker,&heading);
		check(std::abs(heading+90)<.001f,"idle retains the last heading instead of resuming an old move direction");
		marker.attack.cancelled=false; marker.attack.sequence=2; marker.attack.startedAt=.2;
		marker.attack.targetX=std::numeric_limits<float>::infinity(); bank.Resolve(asset,marker,&heading);
		check(std::isfinite(heading) && std::abs(heading+90)<.001f,"invalid target coordinates cannot poison a live mesh transform");
		marker.attack.sequence=3; marker.attack.targetX=0; marker.attack.targetY=0; bank.Resolve(asset,marker,&heading);
		check(std::abs(heading+90)<.001f,"coincident attacker and victim preserve the previous heading");
		auto other=marker; other.objectId=2; other.attack=NWorld::LinuxAttackPresentation{};
		bank.Resolve(asset,other,&heading);
		check(heading==0,"units sharing geometry keep independent facing");
		bank.Begin(&world,map,.3); bank.Begin(&world,map,.4); bank.Resolve(asset,marker,&heading);
		check(heading==0,"offscreen reentry starts from current world direction, not a stale attack cue");
	}
	{
		auto player=LinuxAnimationGraphPlayback::Create(asset,set);
		check(player && player->Advance(0,false) && player->BeginDeath(),"original graph admits the authored death state");
		if (player) {
			check(player->Dying() && !player->BeginAttack(1),"death overrides later attack requests");
			check(player->Advance(.1,true) && player->Dying(),"movement cannot replace a dying unit pose");
			player->EndDeath();
			check(!player->Dying() && player->Advance(.2,false),"resurrection restarts original living graph state");
		}
		LinuxAnimationGraphBank bank; int world=0; auto map=std::make_shared<int>(1);
		NWorld::LinuxDynamicWorldMarker marker; marker.objectId=100; marker.kind=marker.KIND_HERO;
		marker.hasMoveDirection=true; marker.moveDirX=1;
		float heading=0;
		bank.Begin(&world,map,0); bank.Resolve(asset,marker,&heading);
		marker.dead=true; marker.death.active=true; marker.death.sequence=1; marker.death.startedAt=0;
		const auto* dying=bank.Resolve(asset,marker,&heading);
		check(dying && dying->Dying() && heading==90,"visible death retains its original graph and last facing");
		bank.Begin(&world,map,.1);
		check(bank.Resolve(asset,marker)==dying && dying,"subsequent corpse frames do not restart death");
		marker.death.fallStartedAt=.1; marker.death.fallSpeed=3;
		float drop=99;
		bank.Begin(&world,map,.2); bank.Resolve(asset,marker,&heading,&drop);
		check(drop==0,"early fall timer cannot sink a still-playing death animation");
		double corpseClock=.2;
		while (dying && dying->DeathCompletedAt()<0 && corpseClock<6) {
			corpseClock+=.1; bank.Begin(&world,map,corpseClock); dying=bank.Resolve(asset,marker,&heading,&drop);
		}
		check(dying && dying->DeathCompletedAt()>=0 && drop==0,"original 99 percent callback records completion without catching up an early fall timer");
		corpseClock+=.1; bank.Begin(&world,map,corpseClock); bank.Resolve(asset,marker,&heading,&drop);
		check(std::abs(drop-.3f)<.0001f,"completed corpse sinks at authored speed after both lifecycle gates");
		marker.death.fallSpeed=std::numeric_limits<float>::max(); bank.Resolve(asset,marker,&heading,&drop);
		check(std::isfinite(drop) && std::abs(drop-1.5f*(asset.meshPreview.maxZ-asset.meshPreview.minZ))<.001f,
			"corpse fall distance is bounded by three authored half-heights");
		marker.death.fallSpeed=std::numeric_limits<float>::infinity(); bank.Resolve(asset,marker,&heading,&drop);
		check(drop==0,"invalid sinking metadata leaves the mesh on its original ground plane");
		marker.death.active=false;
		check(!bank.Resolve(asset,marker),"world corpse removal immediately stops drawing");
		marker.dead=false;
		const auto* living=bank.Resolve(asset,marker);
		check(living && !living->Dying(),"same world identity returns to living animation after resurrection");
		marker.dead=true; marker.death.active=true;
		bank.Begin(&world,map,corpseClock+.1); bank.Begin(&world,map,corpseClock+.2);
		check(!bank.Resolve(asset,marker),"offscreen corpse never replays an old death on reentry");
		marker.dead=false; bank.Resolve(asset,marker);
		marker.dead=true; marker.death.startedAt=corpseClock+1;
		check(!bank.Resolve(asset,marker),"future death timestamps cannot start a corpse graph");
		marker.death.startedAt=0;
		check(!bank.Resolve(asset,marker),"stale death timestamps cannot queue a delayed death");
		marker.death.startedAt=corpseClock+.2; marker.death.sequence=2;
		check(bank.Resolve(asset,marker)!=nullptr,"a fresh subsequent death can use the retained living owner");
		bank.Begin(&world,map,corpseClock+2);
		check(!bank.Resolve(asset,marker),"long presentation gaps hide a corpse instead of restarting its death clip");
	}
	{
		auto idle=LinuxAnimationGraphPlayback::Create(asset,set),move=LinuxAnimationGraphPlayback::Create(asset,set),attack=LinuxAnimationGraphPlayback::Create(asset,set),death=LinuxAnimationGraphPlayback::Create(asset,set);
		auto fast=LinuxAnimationGraphPlayback::Create(asset,set);
		const bool ready=idle && move && fast && attack && death && fast->SetMoveSpeed(10) && fast->Advance(0,true) && fast->Advance(.4,true) &&
			move->SetMoveSpeed(5) && death->Advance(0,false) && death->BeginDeath() && death->Advance(.4,false) && idle->Advance(0,false) && move->Advance(0,true) &&
			attack->Advance(0,false) && attack->BeginAttack(1) && attack->Advance(.4,false) &&
			idle->Advance(.4,false) && move->Advance(.4,true);
		check(ready,"original idle movement scaled movement attack and death poses are ready for rendering");
		if (ready) {
			glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
			GLint mode=0,active=0; glGetIntegerv(GL_MATRIX_MODE,&mode); glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
			glActiveTexture(GL_TEXTURE0); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
			glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK); glClearDepth(1); glDepthRange(0,1);
			glViewport(0,0,256,256); glPixelStorei(GL_PACK_ALIGNMENT,1);
			glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
			glDisable(GL_STENCIL_TEST); glDisable(GL_FOG);
			glDisable(GL_ALPHA_TEST); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
			glColorMask(1,1,1,1); glClearColor(0,0,0,1);
			glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-7,7,-5,9,-30,30);
			glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glRotatef(35,1,0,0); glRotatef(25,0,1,0);
			LinuxWindowOverlay overlay; overlay.ready=overlay.openglReady=true;
			LinuxSelectedHeroDbPreview hero; hero.sceneAsset=asset;
			LinuxBootstrap::FramePoseCache<LinuxHeroMeshPreviewVertex> poses;
			const auto draw=[&](const LinuxAnimationGraphPlayback* pose,float heading=0) {
				glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
				glPushMatrix(); glRotatef(heading,0,1,0);
				check(DrawLinuxHeroMeshPreview(&overlay,&hero,255,255,255,.4,false,&poses,pose),"original skinned mesh accepts graph-owned matrices");
				glPopMatrix();
				std::vector<unsigned char> pixels(256*256*4); glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); return pixels;
			};
			const auto first=draw(idle.get()),second=draw(move.get()),faster=draw(fast.get()),attacking=draw(attack.get()),turned=draw(attack.get(),90),dying=draw(death.get()),repeated=draw(idle.get());
			size_t visible=0; for (size_t i=0; i<first.size(); i+=4) if (first[i] || first[i+1] || first[i+2]) ++visible;
			fprintf(stdout,"Animation graph pixels: visible=%zu different=%d repeated=%d builds=%zu hits=%zu\n",visible,first!=second,first==repeated,poses.builds,poses.hits);
			check(visible>1000,"original Plane geometry produces visible pixels");
			check(first!=second && first==repeated,"independent idle/move graph poses survive actual drawing and warm pose-cache reuse");
			check(faster!=second && faster!=first,"different actual movement rates produce distinct original Plane pixels");
			check(attacking!=first && attacking!=second,"accepted attack produces a distinct original skinned pose on screen");
			check(turned!=attacking,"changed heading rotates original attack geometry without changing its cached pose");
			check(dying!=first && dying!=second && dying!=attacking,"authored death produces a distinct original skinned pose");
			check(poses.builds==5 && poses.hits==2,"pose cache separates independently scaled living and death graphs and reuses facing changes");
			for (const auto& texture : overlay.heroPreviewDiffuseTextureCache) glDeleteTextures(1,&texture.second.texture);
			glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(mode);
			glPopClientAttrib(); glPopAttrib(); glActiveTexture(active);
			check(glGetError()==GL_NO_ERROR,"graph pose rendering leaves no GL errors");
		}
	}
	fprintf(stdout,"Animation graph summary: checks=%u failures=%u\n",checks,failures);
	return failures==0;
}
