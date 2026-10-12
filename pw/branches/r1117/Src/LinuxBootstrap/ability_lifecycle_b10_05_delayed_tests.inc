#include <cstring>

/** Restore only serialized activation timers; all engine ownership and callbacks stay real.
 * Authored animation graphs are unavailable in this null-render fixture, so the delay is mocked.
 */
struct B10ActivationTimerRestore : IBinSaver
{
	OBJECT_BASIC_METHODS(B10ActivationTimerRestore);
public:
	float delay;
	explicit B10ActivationTimerRestore(float delay = 1) : delay(delay) {}
	bool StartChunk(chunk_id, int, int) override { return false; }
	void FinishChunk() override {}
	void DataChunk(chunk_id id, void* data, int size, int) override
	{
		if ((id == 10 || id == 11) && size == sizeof(float)) std::memcpy(data, &delay, sizeof(float));
	}
	int CountChunks(chunk_id) override { return 0; }
	bool IsReading() override { return true; }
	Stream* GetStream() override { return nullptr; }
	void StoreObject(CObjectBase*) override {}
	CObjectBase* LoadObject() override { return nullptr; }
	void StoreObjectST(BaseObjectST*) override {}
	BaseObjectST* LoadObjectST() override { return nullptr; }
	void StoreObjectMT(BaseObjectMT*) override {}
	BaseObjectMT* LoadObjectMT() override { return nullptr; }
	void DataChunkString(nstl::string&) override {}
	void DataChunkString(nstl::wstring&) override {}
	void DataChunkString(nstl::fixed_string_base<char>&) override {}
	void DataChunkString(nstl::fixed_string_base<wchar_t>&) override {}
};

/** Real delayed activation rechecks context and payment at the dispatch boundary. */
static void AbilityDelayedAdmissionB10(Checks& check, PFWorld* world)
{
	for (int loss = 0; loss < 6; ++loss)
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Talent* raw = new NDb::Talent;
		raw->manaCost.sString = "70";
		raw->cooldownTime.sString = "5";
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFAbilityData> data = new B9TimedTalent(hero.GetPtr(), db);
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		B10ActivationTimerRestore restore(1);
		pending->operator&(restore);
		check(pending->ApplyToTarget() && !pending->IsActivated() && hero->GetMana() == 200,
			"delayed dispatch construction does not pay or apply effects");
		check(!pending->ApplyToTarget(), "pending instance cannot replace its attached dispatch");
		pending->Update(.25f);
		check(!pending->IsActivated() && hero->GetMana() == 200 && receiver->GetHealth() == 1000,
			"predeadline update retains unpaid pending work");
		if (loss == 1) data->AddForbid();
		if (loss == 2) hero->SetEnergy(20);
		if (loss == 3) hero->ChangeHealth(-1000);
		if (loss == 4) receiver->ChangeHealth(-1000);
		if (loss == 5)
		{
			NDb::ConditionCastLimitation* condition = new NDb::ConditionCastLimitation;
			condition->condition.sString = "false";
			raw->castLimitations.push_back(condition);
		}
		pending->Update(.75f);
		if (loss == 0)
		{
			check(pending->IsActivated() && !pending->IsCastFinished() && hero->GetMana() == 130
				&& receiver->GetHealth() == 975 && data->GetCurrentCooldown() == 5,
				"deadline starts real effects, authored cooldown, and exactly one payment");
			pending->Update(2);
			check(pending->IsFinished() && hero->GetMana() == 130 && receiver->GetHealth() == 975,
				"delayed work completes without replaying activation");
		}
		else
		{
			check(pending->IsFinished() && data->GetCurrentCooldown() == 0
				&& hero->GetMana() == (loss == 2 ? 20 : 200)
				&& receiver->GetHealth() == (loss == 4 ? 0 : 1000),
				"lost delayed admission ends work without cooldown, resource debt, or effects");
			pending->Update(2);
			check(pending->IsFinished() && hero->GetMana() == (loss == 2 ? 20 : 200),
				"failed activation cannot revive on a later update");
		}
	}
	for (bool interrupt : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Ability* raw = new NDb::Ability; raw->manaCost.sString = "70";
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		B10ActivationTimerRestore restore(1); pending->operator&(restore);
		check(pending->ApplyToTarget(), "cancellation fixture owns a real pending dispatch");
		if (interrupt) pending->Interrupt(); else pending->Cancel();
		check(pending->IsFinished() && pending->Update(2) && sender->GetMana() == 200,
			"attached pending dispatch terminates without delayed payment");
	}
}
