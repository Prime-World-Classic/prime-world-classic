/** Checked presentation-only adapter for the original reach-marker algorithm.
 * The graph owns its callbacks and must be destroyed before this adapter. No callback reaches gameplay.
 */
class LinuxAnimationGraphAttack
{
public:
	/** Only node markers fit the original supported reach-marker path; its global search requires <15 markers. */
	bool Initialize(DiAnimGraph& value, const NDb::AnimGraphCreator& description, const nstl::string& name)
	{
		if (graph || name.empty() || name.size()>128 || value.GetNumMarkers()<=0 || value.GetNumMarkers()>=15) return false;
		std::vector<unsigned> found;
		for (const auto& authored : description.macroNodes) {
			for (const auto& edge : authored.simpleTransactions)
				for (const auto& event : edge.parameters.markers) if (event.name==name) return false;
			for (const auto& event : authored.markers) if (event.name==name) {
				const auto id=value.GetNodeIDByNameSlowQuite(authored.nodeName.c_str());
				if (id>=value.GetNumNodes() || !value.FindMarkerByNameInNode(name,id)) return false;
				auto* node=value.GetNodeData(id);
				if (node->IsSubNode() && (node->GetDispatcherNodeID()>=value.GetNumNodes() ||
					!value.GetNodeData(node->GetDispatcherNodeID())->IsSwitcher())) return false;
				found.push_back(id);
			}
		}
		if (found.empty() || !value.RegisterCallbackForAllSameMarkers(name,Reached,this)) return false;
		graph=&value; marker=name; targets=std::move(found); return true;
	}
	/** Validate paths before the original fixed 10-node buffers and float-to-int time arithmetic.
	 * At most eight path nodes leave room for the current transition's extra path entry.
	 */
	bool Reach(double seconds)
	{
		if (!graph || !std::isfinite(seconds) || seconds<.001 || seconds>60 || graph->GetNumNodes()>128) return false;
		for (unsigned i=0; i<graph->GetNumNodes(); ++i) {
			const double speed=graph->GetNodeData(i)->GetSpeed();
			if (!std::isfinite(speed) || speed<=0 || speed>100 || 1/speed>3600) return false;
		}
		bool source=false;
		for (const auto from : {graph->GetCurNodeUnsafe(),graph->GetNextNode()}) {
			if (from>=graph->GetNumNodes()) continue;
			source=true;
			for (const auto to : targets) if (from!=to) {
				DiUInt32 length=0;
				if (!graph->FindShortestWay(from,to,length,graph->GetDBFileName()) || length>8) return false;
			}
		}
		if (!source) return false;
		armed=true;
		if (!graph->ReachMarkerByTime(marker,static_cast<int>(seconds*1000),true,true) || !BoundedRate()) {
			armed=false; return false;
		}
		return true;
	}
	/** The caller also selects a normal graph target, using the original speed/callback cancellation path. */
	void Cancel() { armed=false; }
	/** Prevent graph-wide reach acceleration from defeating the existing per-node playback work bound. */
	bool BoundedRate() const
	{
		if (!graph) return true;
		const double scale=graph->GetGraphSpeedCoeff();
		if (!std::isfinite(scale) || scale<=0) return false;
		for (unsigned i=0; i<graph->GetNumNodes(); ++i) {
			const double rate=scale*graph->GetNodeData(i)->GetSpeed();
			if (!std::isfinite(rate) || rate<0 || rate>100) return false;
		}
		return true;
	}
	unsigned Hits() const { return hits; }
private:
	/** Count at most one visual marker per admitted request, even if a clip repeats. */
	static DiInt32 Reached(const DiAnGrCallbackParams& event)
	{
		auto* owner=static_cast<LinuxAnimationGraphAttack*>(event.vpUserData);
		if (owner && owner->armed) { ++owner->hits; owner->armed=false; }
		return DIOBJMSG_CONTINUE;
	}
	DiAnimGraph* graph=nullptr;
	nstl::string marker;
	std::vector<unsigned> targets;
	bool armed=false;
	unsigned hits=0;
};
