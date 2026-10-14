/** Original names use a 36-byte fixed slot and the original loader accepts at most 60 clips. */
static bool LinuxAnimationGraphSupported(const NDb::DBAnimatedSceneComponent& component)
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
