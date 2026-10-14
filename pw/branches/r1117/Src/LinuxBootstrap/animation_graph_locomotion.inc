/** Per-owner original locomotion coefficients; the graph outlives this adapter. */
class LinuxAnimationGraphLocomotion
{
	struct Base { DiAnimNode* node; double speed,x,y; };
	std::vector<Base> nodes;
	double surface=0,last=-1;
	bool ready=false;
public:
	/** Capture once after external constants are applied, before any rate mutation. */
	bool Initialize(DiAnimGraph& graph, int move)
	{
		if (ready || move<0 || static_cast<unsigned>(move)>=graph.GetNumNodes() || graph.GetNumNodes()>128) return false;
		const auto velocity=graph.GetNodeSurfSpeed(move);
		const double length=std::hypot(double(velocity.x),double(velocity.y));
		if (!std::isfinite(length)) return false;
		std::vector<Base> bases;
		for (unsigned i=0; i<graph.GetNumNodes(); ++i) {
			auto* node=graph.GetNodeData(i); if (!node) return false;
			const auto v=node->GetSurfSpeed();
			if (!std::isfinite(v.x) || !std::isfinite(v.y)) return false;
			if (v.x==0 && v.y==0) continue;
			if (!std::isfinite(node->GetSpeed()) || node->GetSpeed()<0 || node->GetSpeed()>100) return false;
			bases.push_back({node,node->GetSpeed(),v.x,v.y});
		}
		nodes=std::move(bases); surface=length; ready=true; return true;
	}
	/** Match PFAnimController's base-coefficient rule, including zero followed by resume.
	 * Preflight all nodes atomically. Cap normalized playback at100 cycles/second
	 * (ten per existing0.1-second update); graphs without authored move velocity keep their rates.
	 */
	bool Apply(double speed)
	{
		if (!ready || !std::isfinite(speed) || speed<0) return false;
		if (speed==last || surface==0) return true;
		const double ratio=speed/surface;
		if (!std::isfinite(ratio)) return false;
		const auto fits=[](double value) { return std::isfinite(value) && std::abs(value)<=std::numeric_limits<float>::max(); };
		for (const auto& base : nodes)
			if (!fits(base.x*ratio) || !fits(base.y*ratio) || !fits(base.speed*ratio) || base.speed*ratio>100) return false;
		for (const auto& base : nodes) {
			base.node->SetSpeed(static_cast<float>(base.speed*ratio));
			base.node->SetSurfSpeed(DiVec2(static_cast<float>(base.x*ratio),static_cast<float>(base.y*ratio)));
		}
		last=speed; return true;
	}
};
