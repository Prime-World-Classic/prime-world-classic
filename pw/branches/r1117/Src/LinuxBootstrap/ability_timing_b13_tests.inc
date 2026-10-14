/** Only metadata is mocked: original ability instances, dispatches, unit ticks and damage stay real. */
struct B13TimingSource : LinuxAbilityAnimationTiming
{
	float fire = .26f, work = .6f, duration = .8f;
	bool available = true;
	std::string lastNode, lastMarker;
	bool Query(const NDb::Unit&, const char* node, const char* marker, float& seconds) override
	{
		lastNode = node; lastMarker = marker ? marker : "<duration>";
		seconds = !marker ? duration : lastMarker == "workTime123" ? work : fire;
		return available;
	}
};

#include "ability_presentation_b13_tests.inc"

/** Offscreen casts and attacks use world-owned timing, including quantization and terminal cancellation. */
static void AbilityTimingB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld f(check), other(check); if (!f.world || !other.world) return;
	auto source = std::make_shared<B13TimingSource>(); f.world->SetLinuxAbilityAnimationTiming(source);
	auto owner = Unit(f.world, NDb::FACTION_FREEZE, 40);
	auto* raw = new NDb::Ability; NDb::Ptr<NDb::Ability> db = raw;
	raw->node = "ability12x3"; raw->marker = "cast";
	CObj<PFAbilityData> data = new PFAbilityData(owner.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	check(std::fabs(data->GetWorkTime() - .6f) < 1e-6f && source->lastMarker == "workTime123",
		"B13 ordinary work marker appends all authored node digits");
	check(std::fabs(data->GetSpeed() - 1.f/.6f) < 1e-6f, "B13 ordinary ability speed is reciprocal authored work time");
	check(std::fabs(data->GetTimeOffset(true) - .26f) < 1e-6f, "B13 raw fire marker stays unquantized without a rendered owner");
	const float step = f.world->GetStepLengthInSeconds();
	check(step > 0 && std::fabs(data->GetTimeOffset() - .3f) < 1e-6f, "B13 fire time rounds to the nearest original world step");
	CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(owner.GetPtr()), false);
	cast->Update(.2f);
	check(!cast->IsCastFinished(), "B13 real cast does not finish before authored work time");
	cast->Update(.4f);
	check(cast->IsCastFinished(), "B13 real cast finishes at authored work duration");
	for (float bad : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1e30f}) {
		source->fire = bad; float held = 73;
		check(data->GetTimeOffset(true) == 0 && data->GetTimeOffset() == 0 &&
			!f.world->GetLinuxAbilityAnimationTime(*owner->DbUnitDesc(), "attack", "doFire", held) && held == 73,
			"B13 malformed timing never reaches float-to-int conversion or mutates output");
	}
	source->fire = .26f; source->work = 0;
	check(data->GetTimeOffset(true) == 0 && data->GetSpeed() == 0, "B13 missing work time suppresses an ordinary fire marker");
	source->work = .6f;
	auto otherOwner = Unit(other.world, NDb::FACTION_FREEZE, 40);
	CObj<PFAbilityData> otherData = new PFAbilityData(otherOwner.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	check(otherData->GetWorkTime() == 0 && otherData->GetTimeOffset() == 0, "B13 timing sources do not leak across worlds");
	for (int kind : {0, 2}) for (int cancel = 0; cancel < 2; ++cancel) {
		auto sender = Unit(f.world, NDb::FACTION_FREEZE, 40);
		auto receiver = B8BehaviourMoving(f.world, NDb::FACTION_BURN);
		check(receiver->TeleportTo(CVec2(15, 5)), "B13 windup projectile fixture uses an actual ten-unit flight");
		CObj<PFBaseAttackData> attack = new PFBaseAttackData(sender.GetPtr(), B11AttackDescriptor(kind));
		check(std::fabs(attack->GetWorkTime() - .8f) < 1e-6f && source->lastNode == "attack" && source->lastMarker == "<duration>",
			"B13 base attack work uses original node duration without time-offset recursion");
		check(std::fabs(attack->GetTimeOffset(true) - .26f) < 1e-6f && source->lastNode == "attack" && source->lastMarker == "doFire",
			"B13 exact BaseAttack type uses original attack/doFire names");
		sender->ReplaceBaseAttack(attack, false); sender->AssignTarget(receiver.GetPtr(), true);
		check(sender->DoAttack(false), "B13 authored delayed attack admitted by real unit");
		check(std::fabs(attack->GetLinuxPresentation().fireOffset-.26f) < 1e-6f,
			"B13 accepted visual cue preserves raw fire timing independently of rounded dispatch scheduling");
		sender->AddFlag(NDb::UNITFLAG_FORBIDATTACK); sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		sender->Step(.1f); sender->Step(.1f);
		check(receiver->GetHealth() == 1000, "B13 actual dispatch cannot deliver before the rounded fire marker");
		if (cancel) attack->Cancel();
		sender->Step(.1f); sender->Step(.1f);
		check(receiver->GetHealth() == (cancel || kind == 2 ? 1000 : 975), "B13 fire deadline delivers only immediate attacks; cancelled and travelling payloads do no damage");
		if (!cancel && kind == 2 && !attack->GetAttackInstances().empty()) {
			auto dispatch = B11FindDispatch(f.world, attack->GetAttackInstances()[0]);
			auto* projectile = dynamic_cast<PFDispatchUniformLinearMove*>(dispatch.GetPtr());
			check(projectile && projectile->GetCurPos() == CVec2(5.5f, 5), "B13 projectile travel starts after windup, not at attack admission");
		}
		sender->Step(2);
		check(receiver->GetHealth() == (cancel ? 1000 : 975) && attack->GetAttackInstances().empty(),
			"B13 terminal delayed attack is removed without replaying damage");
	}
	for (int cancel = 0; cancel < 2; ++cancel) {
		auto sender = Unit(f.world, NDb::FACTION_FREEZE, 40), receiver = Unit(f.world, NDb::FACTION_BURN, 20);
		auto* spell = new NDb::Ability; NDb::Ptr<NDb::Ability> owned = spell;
		spell->node = "ability12x3"; spell->marker = "cast"; spell->manaCost.sString = "10";
		spell->cooldownTime.sString = "10";
		auto* immediate = new NDb::DispatchImmediate; immediate->instantApply = true; spell->dispatch = immediate;
		auto* damage = new NDb::DamageApplicator; damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; spell->applicators.push_back(damage);
		CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), owned, NDb::ABILITYTYPEID_SPECIAL);
		auto instance = ability->ApplyToTarget(Target(receiver.GetPtr()));
		check(instance && !instance->GetLinuxActivationReceipt().activated && sender->GetMana() == 200 &&
			ability->GetCurrentCooldown() == 0 && receiver->GetHealth() == 1000,
			"B13 accepted delayed cast has no premature payment, cooldown, receipt or payload");
		if (!instance) continue;
		instance->Update(.2f);
		check(!instance->GetLinuxActivationReceipt().activated && sender->GetMana() == 200,
			"B13 pre-marker instance update cannot publish an activation receipt");
		if (cancel) instance->Cancel();
		instance->Update(.1f);
		const auto receipt = instance->GetLinuxActivationReceipt();
		check(cancel ? !receipt.activated && !receipt.paymentObserved && sender->GetMana() == 200 && receiver->GetHealth() == 1000 :
			receipt.activated && receipt.paymentObserved && !receipt.spentLife && receipt.cooldown == 10 &&
			receipt.cost == 10 && receipt.before == 200 && receipt.after == 190 && receiver->GetHealth() == 975,
			"B13 actual activation records exact cooldown/payment; cancellation cannot impersonate activation");
		sender->TakeMana(7); ability->Update(1, true); instance->Update(1);
		check(instance->GetLinuxActivationReceipt().after == receipt.after && instance->GetLinuxActivationReceipt().cooldown == receipt.cooldown,
			"B13 immutable receipt survives later pool changes and cooldown ticks");
	}
	source->available = false;
	{
		source->available = true;
		auto sender = B8BehaviourMoving(f.world, NDb::FACTION_FREEZE);
		auto* spell = new NDb::Ability; NDb::Ptr<NDb::Ability> owned = spell;
		spell->node = "ability12x3"; spell->marker = "cast";
		spell->flags = NDb::ABILITYFLAGS_FOCUSONTARGET | NDb::ABILITYFLAGS_FOCUSINSTANTLY;
		auto* immediate = new NDb::DispatchImmediate; immediate->instantApply = true; spell->dispatch = immediate;
		check(sender->GetLinuxAbilityPresentation().sequence == 0, "B13 no speculative cast snapshot before admission");
		auto first = sender->UseExternalAbility(owned, Target(CVec3(10, 15, 0)));
		const auto cue = sender->GetLinuxAbilityPresentation();
		check(first && cue.sequence && cue.instanceObjectId == first->GetObjectId() && !cue.cancelled &&
			std::string(cue.node) == "ability12x3" && std::string(cue.marker) == "cast" &&
			std::fabs(cue.workTime-.6f)<1e-6f && std::fabs(cue.fireOffset-.26f)<1e-6f,
			"B13 accepted external cast snapshots original node, marker and instance timing before activation");
		check(cue.hasTarget && cue.targetX == 10 && cue.targetY == 15 && cue.lookKind == TurnInstantly,
			"B13 original focus policy and target are copied without retaining the target");
		if (first) first->Cancel();
		check(sender->GetLinuxAbilityPresentation().cancelled && sender->GetLinuxAbilityPresentation().sequence == cue.sequence,
			"B13 cancellation preserves accepted cue identity");
		auto second = sender->UseExternalAbility(owned, Target(CVec3(20, 25, 0)));
		const auto next = sender->GetLinuxAbilityPresentation();
		if (first) first->Interrupt();
		check(second && next.sequence > cue.sequence && !sender->GetLinuxAbilityPresentation().cancelled &&
			sender->GetLinuxAbilityPresentation().targetX == 20,
			"B13 older instance interruption cannot cancel the newer accepted cast");
		if (second) second->Interrupt();
		check(sender->GetLinuxAbilityPresentation().cancelled, "B13 current instance interruption cancels its presentation");
		spell->manaCost.sString = "999999";
		check(!sender->UseExternalAbility(owned, Target(sender.GetPtr())) &&
			sender->GetLinuxAbilityPresentation().sequence == next.sequence,
			"B13 rejected admission cannot overwrite the last accepted cast");
		source->available = false;
	}
	check(data->GetWorkTime() == 0 && data->GetTimeOffset() == 0, "B13 unavailable metadata retains explicit legacy fallback");
	f.world->SetLinuxAbilityAnimationTiming({}); std::weak_ptr<B13TimingSource> weak = source; source.reset();
	check(weak.expired(), "B13 replacing world source releases its owned metadata cache");
	std::printf("Ability timing gameplay: checks=%d failures=%d\n", check.count-before, check.failed-failed);
	AbilityPresentationB13(check);
}
