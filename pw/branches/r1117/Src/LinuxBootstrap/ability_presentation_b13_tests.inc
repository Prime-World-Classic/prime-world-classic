/** Observe original admission events, including synchronous cancellation and nested acceptance. */
struct B13CastPresentationListener : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(B13CastPresentationListener);
public:
	int starts=0;
	std::function<void(PFAbilityInstance*)> onStart;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (const auto* start=dynamic_cast<const PFBaseUnitAbilityStartEvent*>(event)) {
			++starts; if (onStart) onStart(const_cast<PFAbilityInstance*>(start->GetAbility()));
		}
		return 0;
	}
};

/** Exercise all original OnUseAbility sites without substituting a mock cast implementation. */
static void AbilityPresentationB13(Checks& check)
{
	const auto before=check.count, failed=check.failed;
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto timing=std::make_shared<B13TimingSource>(); f.world->SetLinuxAbilityAnimationTiming(timing);
	for (int mode=0; mode<6; ++mode) {
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		auto* raw=new NDb::Talent; NDb::Ptr<NDb::Talent> db=raw;
		NDb::Ptr<NDb::Ability> abilityDb=raw;
		raw->node="ability12x3"; raw->marker="cast"; raw->naftaCost=0;
		raw->manaCost.sString="10"; raw->cooldownTime.sString="4";
		raw->flags=NDb::ABILITYFLAGS_FOCUSONTARGET;
		CObj<B13CastPresentationListener> listener=new B13CastPresentationListener;
		hero->AddEventListener(listener.GetPtr());
		CObj<PFAbilityInstance> instance;
		const Target target(CVec3(5,15,0));
		if (mode==0) {
			CObj<PFAbilityData> data=new PFAbilityData(hero.GetPtr(),abilityDb,NDb::ABILITYTYPEID_ABILITY1);
			hero->SetAbility(1,data); instance=hero->UseAbility(1,target);
		} else if (mode==2) {
			auto* item=new NDb::Consumable; NDb::Ptr<NDb::Consumable> owned=item;
			item->node=raw->node; item->marker=raw->marker; item->flags=raw->flags; item->maxQuantity=2;
			check(hero->TakeConsumable(owned,2),"B13 cast fixture installs a real consumable stack");
			instance=hero->UseConsumable(0,target,false);
		} else if (mode==3) {
			instance=hero->UseExternalAbility(abilityDb,target);
		} else {
			CObj<PFTalent> talent=new PFTalent(hero.GetPtr(),db,1,0,0,false); talent->Activate();
			if (mode==4) listener->onStart=[](PFAbilityInstance* cast) { cast->Cancel(); };
			if (mode==5) listener->onStart=[&](PFAbilityInstance*) {
				hero->UseExternalAbility(abilityDb,Target(CVec3(15,5,0)));
			};
			instance=hero->UseTalent(talent,target);
		}
		const auto cue=hero->GetLinuxAbilityPresentation();
		check(instance && cue.sequence && std::string(cue.node)=="ability12x3" && std::string(cue.marker)=="cast",
			"B13 unit, talent, consumable and external entry points publish original DB animation names");
		check(listener->starts==(mode==3 ? 0 : 1),"B13 original admission event routing is preserved for each entry point");
		check(cue.cancelled==(mode==4),"B13 synchronous cancellation cannot publish a live cast afterwards");
		if (mode==5) {
			check(cue.instanceObjectId!=instance->GetObjectId() && cue.targetX==15 && cue.targetY==5,
				"B13 nested accepted cast cannot be overwritten by its older returning caller");
			instance->Cancel();
			check(!hero->GetLinuxAbilityPresentation().cancelled,"B13 cancelling outer instance preserves nested cast presentation");
		} else if (mode!=3 && mode!=4) {
			check(std::fabs(hero->GetMoveDirection().x)<1e-6f && hero->GetMoveDirection().y>.99f,
				"B13 original admitted cast restores the simulation facing update");
		}
		if (instance) instance->Cancel();
		hero->RemoveEventListener(listener.GetPtr()); hero->Cleanup();
	}
	{
		auto owner=Unit(f.world,NDb::FACTION_FREEZE,40);
		auto* raw=new NDb::Ability; NDb::Ptr<NDb::Ability> db=raw;
		raw->node="ability12x3"; raw->marker="cast";
		CObj<PFAbilityData> data=new PFAbilityData(owner.GetPtr(),db,NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> passive=new PFAbilityInstance(data,Target(owner.GetPtr()),true);
		passive->PublishLinuxPresentation(true,true);
		CObj<PFAbilityInstance> unaccepted=new PFAbilityInstance(data,Target(owner.GetPtr()),false);
		unaccepted->PublishLinuxPresentation(true,true);
		check(owner->GetLinuxAbilityPresentation().sequence==0,"B13 passive and unsubmitted instances cannot forge accepted visual cues");
		auto accepted=data->ApplyToTarget(Target(owner.GetPtr()));
		check(accepted && owner->GetLinuxAbilityPresentation().sequence==0,"B13 generic ApplyToTarget does not impersonate an OnUseAbility call");
		raw->node="mutated"; raw->marker="changed"; timing->fire=.5f;
		accepted->PublishLinuxPresentation(true,true);
		const auto cue=owner->GetLinuxAbilityPresentation();
		check(std::string(cue.node)=="ability12x3" && std::string(cue.marker)=="cast" &&
			std::fabs(cue.fireOffset-.26f)<1e-6f && cue.lookKind==DontLook,
			"B13 admitted instance metadata survives later DB/timing changes and self-target keeps DontLook");
		accepted->PublishLinuxPresentation(false,false);
		check(owner->GetLinuxAbilityPresentation().sequence==cue.sequence && owner->GetLinuxAbilityPresentation().syncVisual,
			"B13 duplicate publication is immutable");
		accepted->Cancel();
	}
	{
		auto owner=Unit(f.world,NDb::FACTION_FREEZE,40);
		auto* raw=new NDb::Ability; NDb::Ptr<NDb::Ability> db=raw;
		raw->node="ability12x3"; raw->marker="cast";
		auto instance=owner->UseExternalAbility(db,Target(owner.GetPtr()));
		check(instance && !instance->IsCastFinished(),"B13 cleanup fixture begins real unfinished cast work");
		if (instance) {
			instance->Update(.7f);
			check(instance->IsCastFinished() && instance->GetLinuxActivationReceipt().activated,
				"B13 cleanup fixture reaches successful activation and work completion");
			instance->Cancel();
			check(!owner->GetLinuxAbilityPresentation().cancelled,
				"B13 normal state cleanup cannot retroactively cancel successfully completed cast presentation");
		}
	}
	std::printf("Ability presentation gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
