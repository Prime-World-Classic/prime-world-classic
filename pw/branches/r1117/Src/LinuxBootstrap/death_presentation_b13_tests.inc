/** Actual world death, strict timer edges and resurrection own the native visual snapshot. */
static void DeathPresentationB13(Checks& check)
{
	const int before=check.count,failed=check.failed;
	for (int flags : {0,int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON),int(PFBaseUnit::UNITDIEFLAGS_DONTPLAYDEATHANIMATION),
		int(PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE),
		int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON | PFBaseUnit::UNITDIEFLAGS_DONTPLAYDEATHANIMATION),
		int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON | PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH),
		int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON | PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE)}) {
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		auto db=DeadCreatureDbB13(f.world);
		const_cast<NDb::UnitDeathParameters*>(db->deathParameters.GetPtr())->fallSpeed=3;
		auto unit=CommonCreepB11(f.world);
		const_cast<NDb::Creature*>(dynamic_cast<const NDb::Creature*>(unit->DbUnitDesc()))->deathParameters=db->deathParameters;
		f.world->Step(0,0);
		check(!unit->GetLinuxDeathPresentation().active,"B13 living creature has no death presentation");
		unit->KillUnit(nullptr,flags|PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const auto cue=unit->GetLinuxDeathPresentation();
		check(cue.active==(flags==0 || flags==PFBaseUnit::UNITDIEFLAGS_UNSUMMON),
			"B13 ordinary death and unsummon admit distinct cues unless presentation is suppressed");
		if (flags==PFBaseUnit::UNITDIEFLAGS_UNSUMMON) {
			check(cue.kind==LinuxDeathPresentation::Kind::Unsummon && cue.sequence==1 &&
				cue.startedAt==f.world->GetTimeElapsed() && cue.fallStartedAt<0 && cue.fallSpeed==0,
				"B13 unsummon captures its exact identity and clock without ordinary corpse falling");
			unit->KillUnit(nullptr,flags | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
			check(unit->GetLinuxDeathPresentation().sequence==cue.sequence &&
				unit->GetLinuxDeathPresentation().kind==cue.kind,
				"B13 duplicate unsummon cannot restart or relabel its visual cue");
			f.world->Step(.5f,.5f);
			check(unit->GetLinuxDeathPresentation().active && !unit->IsObjectDead() &&
				unit->GetLinuxDeathPresentation().fallStartedAt<0 && unit->GetLinuxDeathPresentation().fallSpeed==0,
				"B13 exact unsummon deadline retains its visual owner without scheduling sinking");
			f.world->Step(.125f,.125f);
			check(!unit->GetLinuxDeathPresentation().active && unit->IsObjectDead(),
				"B13 world timer retires unsummon strictly after zero independently of animation completion");
		}
		if (flags) continue;
		check(cue.kind==LinuxDeathPresentation::Kind::Death && cue.sequence==1 &&
			cue.startedAt==f.world->GetTimeElapsed() && cue.fallStartedAt<0 && cue.fallSpeed==3,
			"B13 death captures the original world clock and authored fall speed");
		vector<LinuxDynamicWorldMarker> markers; f.world->GetLinuxDynamicWorldMarkers(markers,10000);
		bool found=false;
		for (const auto& marker : markers) if (marker.objectId==unit->GetObjectId())
			found=marker.dead && marker.death.active && marker.death.sequence==cue.sequence;
		check(found,"B13 marker exports the exact corpse lifecycle snapshot");
		unit->KillUnit(nullptr,PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		check(unit->GetLinuxDeathPresentation().sequence==cue.sequence,"B13 repeated kill cannot restart death presentation");
		f.world->Step(1,1);
		check(unit->GetLinuxDeathPresentation().active && unit->GetLinuxDeathPresentation().fallStartedAt<0,
			"B13 exact fall deadline retains original strict crossing");
		f.world->Step(.125f,.125f);
		check(unit->GetLinuxDeathPresentation().fallStartedAt==f.world->GetTimeElapsed(),"B13 fall starts on actual world timer crossing");
		f.world->Step(.875f,.875f);
		check(unit->GetLinuxDeathPresentation().active,"B13 exact corpse deadline remains visible");
		f.world->Step(.125f,.125f);
		check(!unit->GetLinuxDeathPresentation().active && unit->IsObjectDead(),"B13 corpse retirement cancels visual ownership");
	}
	for (int flags : {0,int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON)}) {
		B8BehaviourForeignWorld f(check); if (!f.world) return;
		auto db=DeadCreatureDbB13(f.world);
		CObj<DeadCreatureB13<PFCreature>> unit=new DeadCreatureB13<PFCreature>(f.world,CVec3(5,5,0),CVec2(1,0),*db);
		unit->KillUnit(nullptr,flags | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		check(unit->GetLinuxDeathPresentation().active,"B13 base creature records an admitted visual death");
		unit->OnRessurect();
		check(!unit->GetLinuxDeathPresentation().active && unit->GetLinuxDeathPresentation().fallStartedAt<0,
			"B13 resurrection invalidates corpse and sinking presentation immediately");
	}
	{
		B8BehaviourForeignWorld f(check); if (!f.world) return;
		auto unit=CommonCreepB11(f.world);
		auto db=DeadCreatureDbB13(f.world);
		const_cast<NDb::Creature*>(dynamic_cast<const NDb::Creature*>(unit->DbUnitDesc()))->deathParameters=db->deathParameters;
		auto damage=DirectDamage(); damage.amount=100000;
		check(unit->OnDamage(damage)>0 && unit->IsDead(),"B13 real lethal damage reaches the death hook after health becomes zero");
		check(unit->GetLinuxDeathPresentation().active && unit->GetLinuxDeathPresentation().sequence==1,
			"B13 actual lethal combat admits death presentation despite zero entry health");
	}
	for (int invalid=0; invalid<3; ++invalid) {
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		auto db=DeadCreatureDbB13(f.world);
		auto* params=const_cast<NDb::UnitDeathParameters*>(db->deathParameters.GetPtr());
		if (invalid==0) params->deathTime=std::numeric_limits<float>::quiet_NaN();
		if (invalid==1) params->fallSpeed=std::numeric_limits<float>::infinity();
		if (invalid==2) params->fallOffset=std::numeric_limits<float>::quiet_NaN();
		CObj<DeadCreatureB13<PFCreature>> unit=new DeadCreatureB13<PFCreature>(f.world,CVec3(5,5,0),CVec2(1,0),*db);
		unit->KillUnit(nullptr,PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const auto cue=unit->GetLinuxDeathPresentation();
		check(cue.active==(invalid!=0) && std::isfinite(cue.fallSpeed),"B13 invalid death timing never enables nonfinite visual transforms");
		if (invalid==2) {
			unit->Step(1.125f);
			check(unit->GetLinuxDeathPresentation().fallStartedAt<0,"B13 invalid fall offset never arms sinking");
		}
	}
	std::printf("Death presentation gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
