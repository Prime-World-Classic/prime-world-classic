/** Actual area selection uses authored owner formulas, unit radii and isolated world candidates. */
static void AreaSelectorB12(Checks& check)
{
	B8BehaviourForeignWorld local(check), other(check); if (!local.world || !other.world) return;
	auto params = WeightedParamsB12();
	auto owner = WeightedUnitCreateB12(local.world, params), near = WeightedUnitCreateB12(local.world, params, NDb::FACTION_BURN);
	auto far = WeightedUnitCreateB12(local.world, params, NDb::FACTION_BURN), foreign = WeightedUnitCreateB12(other.world, params, NDb::FACTION_BURN);
	owner->Place(CVec3(5, 5, 0)); near->Place(CVec3(11, 5, 0)); far->Place(CVec3(14, 5, 0)); foreign->Place(CVec3(10, 5, 0));
	near->SetObjectSize(2); far->SetObjectSize(2); foreign->SetObjectSize(2);
	owner->GetVariableVWM("Radius")->SetBaseValue(5);
	auto* rawAbility = new NDb::Ability;
	auto* constants = new NDb::UnitConstantsContainer; auto* radius = new NDb::UnitConstant;
	radius->name = "Radius"; radius->var.sString = "5"; constants->vars.push_back(radius); rawAbility->constants = constants;
	NDb::Ptr<NDb::Ability> abilityDb = rawAbility;
	CObj<PFAbilityData> ability = new PFAbilityData(owner.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
	auto* raw = new NDb::AreaTargetSelector; NDb::Ptr<NDb::AreaTargetSelector> db = raw;
	raw->targetFilter = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_CREEP);
	Target point(CVec3(5, 5, 0));
	const auto enumerate = [&]() {
		vector<PFBaseUnit*> result; PFTargetSelector::TargetsCollector<PFBaseUnit> collect(result);
		CObj<PFTargetSelector> selector = db->Create(local.world);
		PFTargetSelector::RequestParams request(owner.GetPtr(), ability.GetPtr(), point);
		selector->EnumerateTargets(collect, request); return result;
	};
	for (const char* expression : {"5", "2 + 3", "sender.vRadius", "cRadius", "sStrength / 8", "rStrength / 8", "true ? 5 : cMissing"})
	{
		raw->range.sString = expression; const auto selected = enumerate();
		check(selected.size() == 1 && selected[0] == near.GetPtr(), "area range uses checked owner context and excludes distant/foreign candidates");
	}
	for (const char* expression : {"-5", "1 / 0", "1e39", "sqrt(-1)", "cMissing", "nan", "5 +"})
	{
		raw->range.sString = expression;
		check(enumerate().empty(), "invalid or negative radius never selects a fallback area");
	}
	raw->range.sString = ""; raw->range.compiledString = "not-native";
	check(enumerate().empty(), "compiled-only area radius fails closed"); raw->range.compiledString.clear();
	raw->range.sString = "5";
	NDb::TargetCastLimitation limit; limit.targetSelector = db;
	check(limit.Check(CastLimitationsCheckParams(*ability, point)) == ability.GetPtr(), "real area selector can satisfy an authored cast limitation");
	raw->absolutePosition = CVec2(std::numeric_limits<float>::quiet_NaN(), 0);
	check(enumerate().empty(), "invalid authored absolute center cannot fall back to requester");
	raw->absolutePosition = CVec2(0, 0);
	point.SetUnit(foreign.GetPtr());
	check(enumerate().empty(), "foreign requester cannot choose a local area by coincident position");
	point.SetPosition(CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0));
	check(enumerate().empty(), "nonfinite requester geometry cannot select all candidates");
	point.SetPosition(CVec3(5, 5, 0));
	local.world->UnregisterObject(near.GetPtr(), false); local.world->ProcessAddRemove();
	check(enumerate().empty(), "unregistered cached units cannot receive area effects");
}
