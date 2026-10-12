/** Exercise production sector, capsule and nearest selectors with exact authored shape inputs. */
static void DirectionalSelectorB12(Checks& check)
{
	B8BehaviourForeignWorld local(check), other(check); if (!local.world || !other.world) return;
	auto params = WeightedParamsB12(); auto owner = WeightedUnitCreateB12(local.world, params);
	auto forward = WeightedUnitCreateB12(local.world, params, NDb::FACTION_BURN), side = WeightedUnitCreateB12(local.world, params, NDb::FACTION_BURN);
	auto foreign = WeightedUnitCreateB12(other.world, params, NDb::FACTION_BURN);
	owner->Place(CVec3(5, 5, 0)); forward->Place(CVec3(11, 5, 0)); side->Place(CVec3(5, 11, 0)); foreign->Place(CVec3(8, 5, 0));
	forward->SetObjectSize(0); side->SetObjectSize(0); foreign->SetObjectSize(0);
	owner->GetVariableVWM("Radius")->SetBaseValue(7);
	for (int shape = 0; shape < 3; ++shape)
	{
		NDb::Ptr<NDb::TargetSelector> db;
		NDb::MultipleTargetSelectorFiltered* filtered;
		ExecutableFloatString* range;
		NDb::SectorTargetSelector* sector = nullptr; NDb::CapsuleTargetSelector* capsule = nullptr;
		if (shape == 0)
		{
			sector = new NDb::SectorTargetSelector; db = sector; filtered = sector; range = &sector->range;
			sector->segmentBegin = NDb::TARGETSELECTORPOINT_ABILITYOWNER; sector->segmentEnd = NDb::TARGETSELECTORPOINT_CURRENTPOSITION;
			sector->angle = 20; sector->centerIsSegmentEnd = false;
		}
		else if (shape == 1)
		{
			capsule = new NDb::CapsuleTargetSelector; db = capsule; filtered = capsule; range = &capsule->range;
			capsule->segmentBegin = NDb::TARGETSELECTORPOINT_ABILITYOWNER; capsule->segmentEnd = NDb::TARGETSELECTORPOINT_CURRENTPOSITION;
			capsule->rangeFromOwner.sString = "3 + 4"; capsule->cutoffFirstSegment = false;
		}
		else { auto* nearest = new NDb::NearestInAreaTargetSelector; db = nearest; filtered = nearest; range = &nearest->range; }
		filtered->targetFilter = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_CREEP);
		Target requestPoint(shape == 2 ? CVec3(5, 5, 0) : CVec3(12, 5, 0));
		const auto enumerate = [&]() {
			vector<PFBaseUnit*> result; PFTargetSelector::TargetsCollector<PFBaseUnit> collect(result);
			CObj<PFTargetSelector> selector = db->Create(local.world);
			PFTargetSelector::RequestParams request(owner.GetPtr(), nullptr, requestPoint);
			selector->EnumerateTargets(collect, request); return result;
		};
		for (const char* expression : {"sender.vRadius", "3 + 4", "7"})
		{
			range->sString = shape == 1 ? "(2 + 2) / 4" : expression;
			const auto targets = enumerate();
			check(targets.size() == 1 && (targets[0] == forward.GetPtr() || (shape == 2 && targets[0] == side.GetPtr())),
				"directional and nearest selectors use checked shape formulas with local eligible units");
		}
		for (const char* bad : {"-7", "1e39", "sqrt(-1)", "1/0", "cMissing"})
		{
			range->sString = bad; check(enumerate().empty(), "invalid directional radius cannot become a fallback or unbounded shape");
		}
		range->sString = ""; range->compiledString = "not-native";
		check(enumerate().empty(), "compiled-only directional radius is rejected"); range->compiledString.clear();
		range->sString = "7"; requestPoint.SetPosition(CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0));
		check(enumerate().empty(), "invalid shape endpoints cannot select a default direction");
		requestPoint.SetPosition(shape == 2 ? CVec3(5, 5, 0) : CVec3(12, 5, 0));
		if (sector) { sector->angle = std::numeric_limits<float>::quiet_NaN(); check(enumerate().empty(), "nonfinite sector angle is rejected"); }
		if (capsule) { capsule->rangeFromOwner.sString = "cMissing"; check(enumerate().empty(), "invalid capsule owner range cannot disable its clipping"); }
	}
}
