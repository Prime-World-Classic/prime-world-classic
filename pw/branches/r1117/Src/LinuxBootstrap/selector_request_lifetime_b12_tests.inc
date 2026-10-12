/** A selector consumes the borrowed requester during its callback, as real relative selectors do. */
struct RequestEchoDbB12 : NDb::SingleTargetSelector
{
	PFTargetSelector* Create(PFWorld* world) const override;
};
struct RequestEchoB12 : PFSingleTargetSelector
{
	RequestEchoB12(const RequestEchoDbB12& db, PFWorld* world) : PFSingleTargetSelector(db, world) {}
	bool FindTarget(const RequestParams& request, Target& output) override
	{
		if (!request.pRequester || !request.pRequester->IsUnit()) return false;
		output = *request.pRequester;
		return output.IsUnitValid();
	}
};
PFTargetSelector* RequestEchoDbB12::Create(PFWorld* world) const { return new RequestEchoB12(*this, world); }

/** ASan also checks that the caller has not ended the request Target's stack lifetime. */
static void SelectorRequestLifetimeB12(Checks& check)
{
	MicroFixtureB12 f(check); if (!f.micro) return;
	auto* db = new NDb::TargetSelectorMicroAI; db->condition.sString = "true"; db->targetSelector = new RequestEchoDbB12;
	NDb::Ptr<NDb::TargetSelectorMicroAI> held = db;
	PFMicroAICreateParams cp(db, f.ability);
	CObj<PFMicroAI> micro = db->Create(cp);
	for (int i = 0; i < 8; ++i)
	{
		Target result;
		check(micro->GetTarget(result) && result.IsUnit() && result.GetUnit() == f.owner,
			"single MicroAI requester remains alive for the entire selector callback");
	}
}
