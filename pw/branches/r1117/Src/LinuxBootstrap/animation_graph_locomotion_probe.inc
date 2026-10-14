/** Original graph mocks verify exact coefficients independently from sampled poses. */
template<class Check>
static void CheckLinuxGraphLocomotion(Check& check)
{
	test::AnimationDataHolder data;
	for (const char* name : {"idle","move","strafe","attack","death"}) data.AddAnimation(name,1.f);
	auto* creator=new NDb::AnimGraphCreator;
	NDb::Ptr<NDb::AnimGraphCreator> owned=creator;
	creator->defaultNode="idle";
	for (const char* name : {"idle","move","strafe","attack","death"}) test::CreateMacroNode(*creator,name,1);
	creator->macroNodes[1].surfSpeedX=3; creator->macroNodes[1].surfSpeedY=4;
	creator->macroNodes[2].surfSpeedX=-10;
	creator->macroNodes[2].speed.value=10;
	test::MockAnimGraphBlender recorder(data);
	auto* graph=DiAnimGraph::CreateByData(creator,data.animations,&recorder);
	check(graph!=nullptr,"locomotion original mock graph constructs");
	if (!graph) return;
	graph->StoreDefaultValues();
	LinuxAnimationGraphLocomotion scaling;
	check(scaling.Initialize(*graph,1),"locomotion snapshots original coefficients");
	const auto move=graph->GetNodeData(1)->GetSpeed(),strafe=graph->GetNodeData(2)->GetSpeed();
	const auto idle=graph->GetNodeData(0)->GetSpeed(),attack=graph->GetNodeData(3)->GetSpeed(),death=graph->GetNodeData(4)->GetSpeed();
	for (double speed : {10.,2.5,0.,5.,10.,10.}) {
		check(scaling.Apply(speed),"finite movement speed applies including zero and resume");
		const double ratio=speed/5.;
		check(std::abs(graph->GetNodeData(1)->GetSpeed()-move*ratio)<1e-5 &&
			std::abs(graph->GetNodeData(2)->GetSpeed()-strafe*ratio)<1e-5,
			"every locomotion node scales from immutable base speed without compounding");
		const auto a=graph->GetNodeSurfSpeed(1),b=graph->GetNodeSurfSpeed(2);
		check(std::abs(a.x-3*ratio)<1e-5 && std::abs(a.y-4*ratio)<1e-5 && std::abs(b.x+10*ratio)<1e-5 && b.y==0,
			"signed authored surface velocity uses the same absolute speed ratio");
		check(graph->GetNodeData(0)->GetSpeed()==idle && graph->GetNodeData(3)->GetSpeed()==attack && graph->GetNodeData(4)->GetSpeed()==death,
			"zero-surface idle attack and death node rates remain unchanged");
	}
	for (double bad : {-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::max(),100.,1000000.})
		check(!scaling.Apply(bad) && graph->GetNodeData(1)->GetSpeed()==move*2 && graph->GetNodeSurfSpeed(2).x==-20,
			"invalid or unbounded rates reject atomically without changing coefficients");
	check(!scaling.Initialize(*graph,1),"a configured adapter cannot capture already-scaled bases again");
	LinuxAnimationGraphLocomotion invalid;
	check(!invalid.Initialize(*graph,-1) && !invalid.Apply(5),"invalid node identity cannot configure scaling");
	LinuxAnimationGraphLocomotion zero;
	check(zero.Initialize(*graph,0) && zero.Apply(5) && graph->GetNodeData(1)->GetSpeed()==move*2,
		"zero authored move speed leaves graph rates untouched without division");
	LinuxAnimationGraphLocomotion malformed;
	graph->GetNodeData(2)->SetSurfSpeed(DiVec2(std::numeric_limits<float>::infinity(),0));
	check(!malformed.Initialize(*graph,1),"nonfinite secondary surface velocity rejects before capturing any bases");
	graph->GetNodeData(2)->SetSurfSpeed(DiVec2(-20,0));
	graph->GetNodeData(2)->SetSpeed(std::numeric_limits<float>::quiet_NaN());
	check(!malformed.Initialize(*graph,1),"nonfinite secondary node rate rejects initialization atomically");
	graph->GetNodeData(2)->SetSpeed(strafe*2);
	check(malformed.Initialize(*graph,1) && malformed.Apply(10),"failed initialization can retry without stale partial coefficients");
	check(creator->macroNodes[1].surfSpeedX==3 && creator->macroNodes[2].surfSpeedX==-10 && creator->macroNodes[2].speed.value==10,
		"presentation scaling never changes shared authored graph metadata");
	DiAnimGraph::Destroy(graph);
}
