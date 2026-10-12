#include "../libdb/IDbTypesRegistrator.h"

/** Use the real DB factory to mark distinct in-memory descriptors loaded, avoiding disk reload. */
static NDb::Talent* FlagTalentB12(const char* id)
{
	auto* db = static_cast<NDb::Talent*>(NDb::GetDbTypesRegistrator().Create(NDb::DBID(id), "Talent", true));
	if (db) { db->naftaCost = 0; db->manaCost.sString = "0"; }
	return db;
}

/** Expose lifecycle only; all flag, ability and notification behavior remains production code. */
struct FlagBuffB12 : PFApplFlags
{
	explicit FlagBuffB12(const PFApplCreatePars& pars) : PFApplFlags(pars) {}
	bool Begin() { return Init() && !Start(); }
	void Finish() { Stop(); }
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
};

static CObj<FlagBuffB12> FlagsB12(PFWorld* world, PFBaseUnit* unit, const NDb::FlagsApplicator* db)
{
	PFApplCreatePars pars{CObj<PFAbilityInstance>(), Target(unit)};
	pars.pWorld = world; pars.pOwner = unit; pars.pDBAppl = db;
	return new FlagBuffB12(pars);
}

/** Exercise original selected-talent and selected-consumable restrictions, including stacked owners. */
static void FlagsBuffB12(Checks& check, PFWorld* world)
{
	for (bool exceptions : {false, true})
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		NDb::Ptr<NDb::Talent> selected = FlagTalentB12("/Mock/FlagSelected.xdb");
		NDb::Ptr<NDb::Talent> other = FlagTalentB12("/Mock/FlagOther.xdb");
		check(selected && other, "mock talent DB factory returns loaded distinct resources");
		if (!selected || !other) continue;
		hero->InstallTalent(selected); hero->InstallPortal(selected);
		PFTalentsSet::SetInfo info;
		info[0][0].pTalentDesc = selected; info[0][0].refineRate = 1;
		info[0][1].pTalentDesc = other; info[0][1].refineRate = 1;
		hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
		hero->GetTalent(0, 0)->Activate(); hero->GetTalent(0, 1)->Activate();
		NDb::FlagsApplicator* raw = new NDb::FlagsApplicator;
		raw->flag = NDb::UNITFLAG_FORBIDUSETALENTS;
		raw->talents.push_back(selected);
		raw->useListAs = exceptions ? NDb::USEMODE_LISTASEXCEPTION : NDb::USEMODE_LISTASRULE;
		NDb::Ptr<NDb::FlagsApplicator> db = raw;
		CObj<FlagBuffB12> a = FlagsB12(world, hero, db), b = FlagsB12(world, hero, db);
		check(a->Begin() && b->Begin(), "two real flag buffs start on a hero");
		check(hero->GetTalent(0, 0)->IsForbidded() == !exceptions &&
			hero->GetTalent(0, 1)->IsForbidded() == exceptions && hero->GetPortal()->IsForbidded() == !exceptions,
			"authored rule and exception lists select talents and portal by DB identity");
		PFTalent* restricted = hero->GetTalent(0, exceptions ? 1 : 0);
		check(!hero->UseTalent(restricted, Target(hero.GetPtr())) && hero->GetMana() == 200,
			"selected talent is rejected by real casting before payment");
		a->Block();
		check(restricted->IsForbidded() && hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDUSETALENTS),
			"blocking one effect retains the other talent and unit forbid");
		b->Finish(); b->Finish();
		check(!restricted->IsForbidded() && !hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDUSETALENTS),
			"final active owner releases restrictions once");
		a->Unblock(); a->Reset();
		check(restricted->IsForbidded(), "unblock restores one restriction and Reset does not duplicate it");
		a->Finish();
		check(!restricted->IsForbidded() && !hero->GetPortal()->IsForbidded(), "final Stop releases talent and portal ownership");
		hero->Cleanup();
	}
	for (bool listed : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		NDb::Consumable* rawItem = new NDb::Consumable;
		rawItem->manaCost.sString = "0"; rawItem->maxQuantity = 5;
		NDb::Ptr<NDb::Consumable> item = rawItem;
		NDb::Ptr<NDb::Consumable> other = new NDb::Consumable;
		CObj<PFConsumableAbilityData> chosen = hero->GetConsumableAbility(item);
		CObj<PFConsumableAbilityData> untouched = hero->GetConsumableAbility(other);
		NDb::FlagsApplicator* raw = new NDb::FlagsApplicator;
		raw->flag = NDb::UNITFLAG_FORBIDUSECONSUMABLES;
		if (listed) raw->consumables.push_back(item);
		NDb::Ptr<NDb::FlagsApplicator> db = raw;
		CObj<FlagBuffB12> a = FlagsB12(world, hero, db), b = FlagsB12(world, hero, db);
		check(a->Begin() && b->Begin(), "stacked consumable restrictions start");
		check(hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDUSECONSUMABLES) == !listed &&
			chosen->IsForbidded() == listed && !untouched->IsForbidded(),
			"listed consumables restrict only their ability, empty lists retain the global flag");
		a->Finish();
		check(chosen->IsForbidded() == listed && hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDUSECONSUMABLES) == !listed,
			"first removal preserves the second consumable restriction");
		b->Finish();
		check(!chosen->IsForbidded() && !hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDUSECONSUMABLES),
			"last removal releases selected and global consumable restrictions");
	}
	{
		CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
		NDb::FlagsApplicator* raw = new NDb::FlagsApplicator;
		raw->flag = static_cast<NDb::EUnitFlag>(NDb::UNITFLAG_FORBIDMOVE | NDb::UNITFLAG_FORBIDATTACK |
			NDb::UNITFLAG_FORBID_ABILITY1 | NDb::UNITFLAG_FORBID_ABILITY4 | NDb::UNITFLAG_FORBIDINVISIBILITY);
		NDb::Ptr<NDb::FlagsApplicator> db = raw;
		CObj<FlagBuffB12> app = FlagsB12(world, unit, db);
		std::vector<int> events;
		CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			events.push_back(event->GetType());
			const auto* payload = dynamic_cast<const PFBaseUnitApplicatorEvent*>(event);
			check(payload && payload->GetAppl() == app.GetPtr(), "flag notification carries the actual applicator");
			return 0u;
		};
		unit->AddEventListener(listener.GetPtr());
		check(app->Begin(), "nonhero flag notification fixture starts");
		check(events == std::vector<int>({NDb::BASEUNITEVENT_FORBIDMOVE, NDb::BASEUNITEVENT_FORBIDATTACK,
			NDb::BASEUNITEVENT_FORBIDCAST, NDb::BASEUNITEVENT_CANCELINVISIBILITY}),
			"flag enable sends original movement attack cast and invisibility events in order");
		app->Reset(); app->Finish(); app->Finish();
		check(events.size() == 4 && !unit->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE),
			"Reset and cleanup neither replay enable notifications nor retain movement flags");
		unit->RemoveEventListener(listener.GetPtr());
	}
}
