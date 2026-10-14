/** Real stacked flag applicators own the effective native presentation state. */
static void ControlPresentationB13(Checks& check)
{
	const int before=check.count,failed=check.failed;
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto unit=CommonCreepB11(f.world);
	const auto snapshot=[&](bool stun,bool frozen) {
		vector<LinuxDynamicWorldMarker> markers; f.world->GetLinuxDynamicWorldMarkers(markers,10000);
		bool found=false;
		for (const auto& marker : markers) if (marker.objectId==unit->GetObjectId()) found=marker.stunned==stun && marker.frozen==frozen;
		check(found,"B13 marker reflects currently owned stun/freeze effects");
		check(unit->CheckFlagType(NDb::UNITFLAGTYPE_STUN)==stun,"B13 presentation reads never alter original control flags");
	};
	auto* raw=new NDb::FlagsApplicator; NDb::Ptr<NDb::FlagsApplicator> db=raw;
	raw->flag=NDb::UNITFLAG_STUN;
	auto a=FlagsB12(f.world,unit,db),b=FlagsB12(f.world,unit,db);
	snapshot(false,false);
	check(a->Begin() && b->Begin(),"B13 stacked real stun applicators start"); snapshot(true,false);
	auto* iceRaw=new NDb::FlagsApplicator; NDb::Ptr<NDb::FlagsApplicator> iceDb=iceRaw;
	iceRaw->flag=static_cast<NDb::EUnitFlag>(NDb::UNITFLAG_STUN|NDb::UNITFLAG_FREEZE);
	auto ice=FlagsB12(f.world,unit,iceDb);
	check(ice->Begin(),"B13 real freeze effect overlays existing stun ownership"); snapshot(true,true);
	a->Block(); b->Finish(); snapshot(true,true);
	ice->Block(); snapshot(false,false);
	a->Unblock(); snapshot(true,false);
	ice->Unblock(); snapshot(true,true);
	ice->Finish(); snapshot(true,false);
	a->Finish(); snapshot(false,false);
	unit->AddFlag(NDb::UNITFLAG_FREEZE); snapshot(false,false);
	unit->RemoveFlag(NDb::UNITFLAG_FREEZE);
	std::printf("Control presentation gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
