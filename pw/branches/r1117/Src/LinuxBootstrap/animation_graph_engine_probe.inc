#include "Scene/AGHelpers.h"

/** Shipped graph regression: Plane lists attack1 first, but its authored default is idle. */
static bool RunLinuxAnimationGraphEngineProbe(const LinuxClientEnvironment& environment)
{
	unsigned checks=0,failures=0;
	const auto check=[&](bool good,const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout,"Animation graph probe: %s result=%s\n",label,good ? "PASS" : "FAIL");
	};
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
		auto idle=LinuxAnimationGraphPlayback::Create(asset,set),move=LinuxAnimationGraphPlayback::Create(asset,set);
		const bool ready=idle && move && idle->Advance(0,false) && move->Advance(0,true) &&
			idle->Advance(.4,false) && move->Advance(.4,true);
		check(ready,"two original poses are ready for renderer integration");
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
			const auto draw=[&](const LinuxAnimationGraphPlayback* pose) {
				glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
				check(DrawLinuxHeroMeshPreview(&overlay,&hero,255,255,255,.4,false,&poses,pose),"original skinned mesh accepts graph-owned matrices");
				std::vector<unsigned char> pixels(256*256*4); glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); return pixels;
			};
			const auto first=draw(idle.get()),second=draw(move.get()),repeated=draw(idle.get());
			size_t visible=0; for (size_t i=0; i<first.size(); i+=4) if (first[i] || first[i+1] || first[i+2]) ++visible;
			fprintf(stdout,"Animation graph pixels: visible=%zu different=%d repeated=%d builds=%zu hits=%zu\n",visible,first!=second,first==repeated,poses.builds,poses.hits);
			check(visible>1000,"original Plane geometry produces visible pixels");
			check(first!=second && first==repeated,"independent idle/move graph poses survive actual drawing and warm pose-cache reuse");
			check(poses.builds==2 && poses.hits==1,"pose cache separates shared-model unit graphs at the same timestamp");
			for (const auto& texture : overlay.heroPreviewDiffuseTextureCache) glDeleteTextures(1,&texture.second.texture);
			glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(mode);
			glPopClientAttrib(); glPopAttrib(); glActiveTexture(active);
			check(glGetError()==GL_NO_ERROR,"graph pose rendering leaves no GL errors");
		}
	}
	fprintf(stdout,"Animation graph summary: checks=%u failures=%u\n",checks,failures);
	return failures==0;
}
