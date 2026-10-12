#include "../PF_GameLogic/PFCastLimitations.h"

/** Dispel admission inspects hostile removable effects on the caster, as on Windows. */
static void CastDispelB12(Checks& check, PFWorld* world)
{
	for (int mode = 0; mode != 5; ++mode)
	{
		StatusStackFixtureB12 f(world);
		f.other->ChangeFaction(mode == 1 ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
		auto* statusRaw = StatusStackFixtureB12::Db("/Mock/DispelAdmission.xdb", "3");
		statusRaw->dispellPriority = mode == 2 ? NDb::DISPELLPRIORITY_NONDISPELLABLE : NDb::DISPELLPRIORITY_HIGH;
		NDb::Ptr<NDb::StatusApplicator> statusDb = statusRaw;
		CObj<StatusStackProbeB12> status;
		if (mode != 0) status = f.Begin(check, statusDb, 1);
		if (mode == 4) status->Finish();
		auto* raw = new NDb::Talent; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5"; raw->naftaCost = 0;
		auto* limit = new NDb::DispellCastLimitation; raw->castLimitations.push_back(limit);
		auto* dispel = new NDb::DispellApplicator; dispel->maxEffectsToDispell.sString = "1"; raw->applicators.push_back(dispel);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(f.hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		Target target(f.hero.GetPtr()); CastLimitationsCheckParams cp(*talent, target);
		const bool allow = mode == 3;
		check((limit->Check(cp) == talent.GetPtr()) == allow, "dispel factory requires a live hostile dispellable caster status");
		check((CheckDispellLimitation(*limit, cp) == talent.GetPtr()) == allow, "dispel helper preserves original faction and priority filtering");
		check((talent->CheckCastLimitations(target) == nullptr) == allow, "normal cast limitation list reports dispel denial");
		const float before = f.Delta();
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, target);
		check(bool(cast) == allow && f.hero->GetMana() == (allow ? 180 : 200) && talent->GetCurrentCooldown() == (allow ? 5 : 0),
			"dispel admission gates real ability payment and cooldown");
		check(f.Delta() == (allow ? 0 : before) && (!allow || !status->IsAlive()), "successful real dispel removes the status and owned stat child");
		check(!allow || (f.instance[1]->GetActiveApplicatorsCount() == 0 && !limit->Check(cp)), "dispel releases effect ownership and closes repeated admission");
		cp.pAbility = nullptr; check(!limit->Check(cp), "missing dispel ability fails closed");
	}
}
