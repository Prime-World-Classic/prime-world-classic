#include "../PF_GameLogic/PFBaseUnitEvent.h"
#include <cstring>

/** Mock only the serialized channel activation delay; leave dispatch and callbacks untouched. */
struct B10ChannelTimerRestore : IBinSaver
{
	OBJECT_BASIC_METHODS(B10ChannelTimerRestore);
public:
	bool StartChunk(chunk_id, int, int) override { return false; }
	void FinishChunk() override {}
	void DataChunk(chunk_id id, void* data, int size, int) override
	{
		const float delay = 1;
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

/** Observe creation-channel event identity and count, without implementing payment. */
struct B10ChannelEvents : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(B10ChannelEvents);
public:
	int casts = 0;
	const PFAbilityInstance* last = nullptr;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_CASTMAGIC) { ++casts; last = event->GetAbility(); }
		return 0;
	}
};

/** Synchronous ability-end callbacks must not be overwritten by the channel-wait seed. */
static void AbilityChannelCompletionB10(Checks& check, PFWorld* world)
{
	for (bool delayed : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		raw->flags = NDb::ABILITYFLAGS_WAITFORCHANNELING;
		raw->applicators.push_back(new NDb::AbilityEndApplicator);
		CObj<PFAbilityData> data = new PFAbilityData(hero.GetPtr(), raw, NDb::ABILITYTYPEID_ABILITY1);
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(hero.GetPtr()), false);
		if (delayed) { B10ChannelTimerRestore restore; cast->operator&(restore); }
		check(cast->ApplyToTarget(), "channel-end fixture admits a real dispatch");
		if (delayed) cast->Update(1);
		check(cast->IsFinished() && !hero->IsInChannelling() && hero->GetMana() == 130,
			"real synchronous ability-end keeps channel progress cleared after activation");
		cast->Update(1);
		check(!hero->IsInChannelling() && hero->GetMana() == 130,
			"completed channel cannot be resurrected by instance updates");
	}
	for (bool cancel : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		raw->castAllowRange.sString = "1";
		raw->flags = static_cast<NDb::EAbilityFlags>(NDb::ABILITYFLAGS_WAITFORCHANNELING | NDb::ABILITYFLAGS_CHANNELINGCREATE);
		NDb::ChannellingApplicator* channel = new NDb::ChannellingApplicator;
		channel->lifeTime.sString = "1";
		channel->channelingType = NDb::CHANNELINGTYPE_CREATION;
		channel->cancelOnInterrupt = true;
		raw->applicators.push_back(channel);
		CObj<PFAbilityData> data = new PFAbilityData(hero.GetPtr(), raw, NDb::ABILITYTYPEID_ABILITY1);
		CObj<B10ChannelEvents> events = new B10ChannelEvents;
		hero->AddEventListener(events.GetPtr());
		CObj<PFAbilityInstance> cast = hero->CreateAbilityInstance(data, Target(hero.GetPtr()));
		check(cast && cast->GetActiveApplicatorsCount() == 1 && !cast->IsFinished()
			&& hero->IsInChannelling() && hero->GetMana() == 200 && events->casts == 0,
			"real creation channel retains effects and defers payment and cast event");
		if (cancel) hero->CancelChannelling();
		hero->Simulate(1);
		check(cast && cast->IsFinished() && !hero->IsInChannelling()
			&& hero->GetMana() == (cancel ? 200 : 130) && events->casts == (cancel ? 0 : 1),
			"channel completion pays once while real channel cancellation does not pay");
		check(cancel || events->last == cast.GetPtr(), "successful channel emits its real instance identity");
		hero->RemoveEventListener(events.GetPtr());
	}
}
