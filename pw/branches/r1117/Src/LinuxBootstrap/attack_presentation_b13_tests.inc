/** Presentation observes accepted attacks, including instances which finish before the next draw. */
static void AttackPresentationB13(Checks& check, PFWorld* world)
{
	const int before=check.count,failed=check.failed;
	for (int kind=0; kind<3; ++kind) {
		CObj<PFBaseUnit> sender=Unit(world,NDb::FACTION_FREEZE,40);
		CObj<PFBaseUnit> receiver=Unit(world,NDb::FACTION_BURN,20);
		CObj<PFBaseAttackData> attack=new PFBaseAttackData(sender.GetPtr(),B11AttackDescriptor(kind));
		sender->ReplaceBaseAttack(attack,false);
		check(attack->GetLinuxPresentation().sequence==0,"B13 no attack cue exists before admission");
		check(!attack->DoAttack(Target()) && attack->GetLinuxPresentation().sequence==0,"B13 rejected attack emits no cue");
		sender->AssignTarget(receiver.GetPtr(),true);
		check(sender->DoAttack(false),"B13 actual unit attack admits presentation fixture");
		const auto cue=attack->GetLinuxPresentation();
		check(cue.sequence==1 && cue.abilityObjectId==attack->GetObjectId() && !cue.cancelled &&
			cue.startedAt==world->GetTimeElapsed() && cue.period>0,"B13 accepted attack records identity, clock and actual cooldown period");
		check(!sender->DoAttack(false) && attack->GetLinuxPresentation().sequence==cue.sequence,"B13 cooldown rejection cannot duplicate presentation");
		attack->Update(3,true);
		check(attack->GetAttackInstances().empty() && attack->GetLinuxPresentation().sequence==cue.sequence,
			"B13 cue survives immediate, timed and travelling instance retirement");
		check(sender->DoAttack(false) && attack->GetLinuxPresentation().sequence==2,"B13 next accepted attack increments the cue once");
		attack->Cancel(); attack->Cancel();
		check(attack->GetLinuxPresentation().cancelled && attack->GetLinuxPresentation().sequence==2,
			"B13 repeated cancellation preserves identity and prevents pending visual replay");
		attack->Update(3,true);
		check(sender->DoAttack(false) && attack->GetLinuxPresentation().sequence==3 && !attack->GetLinuxPresentation().cancelled,
			"B13 a later accepted attack clears cancellation without replaying an old cue");
		attack->Cancel();
	}
	CObj<PFCommonCreep> creep=CommonCreepB11(world);
	CObj<PFBaseUnit> target=Unit(world,NDb::FACTION_BURN,20);
	CObj<PFBaseAttackData> attack=new PFBaseAttackData(creep.GetPtr(),B11AttackDescriptor(0));
	creep->ReplaceBaseAttack(attack,false); creep->AssignTarget(target.GetPtr(),true);
	check(creep->DoAttack(false),"B13 actual world creep accepts an attack cue");
	vector<LinuxDynamicWorldMarker> markers; world->GetLinuxDynamicWorldMarkers(markers,100000);
	bool found=false;
	for (const auto& marker : markers) if (marker.objectId==creep->GetObjectId())
		found=marker.attack.sequence==1 && marker.attack.abilityObjectId==attack->GetObjectId();
	check(found,"B13 live world marker exports the exact accepted ability cue");
	attack->Cancel();
	world->GetLinuxDynamicWorldMarkers(markers,100000); found=false;
	for (const auto& marker : markers) if (marker.objectId==creep->GetObjectId()) found=marker.attack.cancelled;
	check(found,"B13 live world marker exports cancellation without consuming the cue");
	{
		CObj<PFBaseUnit> sender=Unit(world,NDb::FACTION_FREEZE,40);
		CObj<PFBaseUnit> receiver=Unit(world,NDb::FACTION_BURN,20);
		auto* descriptor=B11AttackDescriptor(0);
		auto* immediate=new NDb::DispatchImmediate; immediate->instantApply=true; descriptor->dispatch=immediate;
		CObj<PFBaseAttackData> data=new PFBaseAttackData(sender.GetPtr(),descriptor);
		sender->ReplaceBaseAttack(data,false); sender->AssignTarget(receiver.GetPtr(),true);
		CObj<EventDispatchB7Listener> listener=new EventDispatchB7Listener;
		listener->action=[&](EventDispatchB7Listener&,const PFBaseUnitEvent*) { data->Cancel(); return 0u; };
		receiver->AddEventListener(listener.GetPtr());
		check(sender->DoAttack(false) && listener->calls>0,"B13 immediate damage callback cancels the accepted attack");
		check(data->GetLinuxPresentation().sequence==1 && data->GetLinuxPresentation().cancelled,
			"B13 admission cannot overwrite callback-driven presentation cancellation");
		receiver->RemoveEventListener(listener.GetPtr()); listener->action=nullptr;
	}
	std::printf("Attack presentation gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
