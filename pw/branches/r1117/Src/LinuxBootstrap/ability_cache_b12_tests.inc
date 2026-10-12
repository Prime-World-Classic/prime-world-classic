namespace
{
/** Restore only the existing serialized timestamp; no alternate cache implementation. */
struct AbilityCacheTimeB12 : B10ActivationTimerRestore
{
	explicit AbilityCacheTimeB12(float value) : B10ActivationTimerRestore(value) {}
	void DataChunk(chunk_id id, void* data, int size, int) override
	{
		if (id == 20 && size == sizeof(float)) std::memcpy(data, &delay, sizeof(float));
	}
};

/** Same-world-time changes exercise real hero/talent caches, admission and resource mutation. */
static void AbilityCacheB12(Checks& check, PFWorld* world)
{
	for (auto mode : {NDb::ABILITYMODMODE_MANACOST, NDb::ABILITYMODMODE_COOLDOWN})
	{
		AbilityModFixtureB12 f(world, mode, ".5", "5");
		// Warm the timestamp before insertion so the first change also collides.
		f.hero->RecacheAbilitiesModifiers();
		CObj<PFTalent> talent = f.Talent();
		check(f.app->Initialize() && !f.app->Run(), "cache fixture starts its checked real ability modifier");
		for (int change = 0; change != 6; ++change)
		{
			if (change == 1) { f.Edit().addValue.sString = "8"; f.app->Tick(0); }
			if (change == 2) f.app->Block();
			if (change == 3) f.app->Unblock();
			if (change == 4) { f.Edit().enabled.sString = "false"; f.app->Tick(0); }
			if (change == 5) { f.Edit().enabled.sString = "true"; f.app->Tick(0); }
			talent->Update(0, true);
			const bool active = change != 2 && change != 4;
			const float expected = active ? (change == 0 ? 5 : 8) + (mode == NDb::ABILITYMODMODE_MANACOST ? 20 : 10)
				: mode == NDb::ABILITYMODMODE_MANACOST ? 40 : 20;
			check(talent->GetModifiedValue(mode == NDb::ABILITYMODMODE_MANACOST ? 40 : 20, mode) == expected,
				"same-tick modifier reads follow activation dynamic Block Unblock and authored enabled transitions");
			if (mode == NDb::ABILITYMODMODE_MANACOST)
				check(talent->GetManaCost() == expected, "full update refreshes existing mana cache within one tick");
			else
			{
				talent->RecalculateAndRestartCooldown();
				check(talent->GetCooldown() == expected && talent->GetCurrentCooldown() == expected,
					"cooldown restart uses the latest same-tick coefficient pair");
			}
		}
		f.app->Finish(); talent->Update(0, true);
		check(talent->GetModifiedValue(40, mode) == 40 && talent->GetManaCost() == 40,
			"same-tick removal clears an existing ability's modifier cache");
	}
	for (int change = 0; change != 5; ++change)
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		f.app->Initialize(); f.app->Run();
		CObj<PFTalent> talent = f.Talent();
		if (change == 0) { f.Edit().addValue.sString = "10"; f.app->Tick(0); }
		if (change == 1) f.app->Block();
		if (change == 2) { f.app->Block(); talent->Update(0, true); f.app->Unblock(); }
		if (change == 3) { f.Edit().multValue.sString = "10"; f.app->Tick(0); }
		if (change == 4) { f.app->Block(); talent->Update(0, true); f.hero->SetEnergy(30); f.app->Unblock(); }
		const float available = f.hero->GetMana();
		const float cost = change == 0 ? 30 : change == 1 ? 40 : change == 3 ? 405 : 25;
		check(talent->IsEnoughMana() == (change != 3), "same-tick affordability sees both lost admission and newly affordable discounts");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(bool(cast) == (change != 3) && f.hero->GetMana() == (change == 3 ? available : available - cost),
			"actual paid cast without an intervening update uses current same-tick cost");
		check(change != 3 || talent->GetCurrentCooldown() == 0, "rejected expensive cast cannot start cooldown");
	}
	for (float timestamp : {0.f, 1e30f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		AbilityCacheTimeB12 restore(timestamp); talent->PFAbilityData::operator&(restore);
		f.Edit().addValue.sString = "8"; f.app->Tick(0); talent->Update(0, true);
		check(talent->GetManaCost() == 28, "restored future nonfinite or same-tick timestamp cannot pin obsolete coefficients");
	}
	for (bool expensive : {false, true})
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(talent.GetPtr(), Target(f.hero.GetPtr()), false);
		B10ActivationTimerRestore timer(1); pending->operator&(timer);
		check(pending->ApplyToTarget() && f.hero->GetMana() == 200, "delayed actual cast starts unpaid");
		f.Edit().addValue.sString = expensive ? "300" : "10"; f.app->Tick(0);
		pending->Update(1);
		check(f.hero->GetMana() == (expensive ? 200 : 170) && talent->GetCurrentCooldown() == (expensive ? 0 : 20),
			"delayed activation refreshes live affordability and pays only a valid same-tick cost");
		pending->Cancel();
	}
	for (auto mode : {NDb::ABILITYMODMODE_MANACOST, NDb::ABILITYMODMODE_COOLDOWN})
		for (bool sum : {false, true})
	{
		AbilityModFixtureB12 f(world, mode, "1", "0");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		f.Edit().multValue.sString = sum ? "1" : "2e38";
		f.Edit().addValue.sString = sum ? "2e38" : "-2e38";
		f.app->Tick(0);
		const float base = sum ? 2e38f : 2;
		check(std::isnan(f.Modified(base)) && std::isnan(talent->GetModifiedValue(base, mode)),
			"final hero and ability arithmetic rejects overflowing product or sum before cancellation");
		NDb::Talent* db = const_cast<NDb::Talent*>(talent->GetTalentDesc().GetPtr());
		if (mode == NDb::ABILITYMODMODE_MANACOST) db->manaCost.sString = sum ? "2e38" : "2";
		else db->cooldownTime.sString = sum ? "2e38" : "2";
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(!cast && f.hero->GetMana() == 200 && talent->GetCurrentCooldown() == 0,
			"same-tick invalid final cost or cooldown fails admission before payment and cooldown mutation");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_COOLDOWN, ".5", "5");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		talent->RestartCooldown(7); f.Edit().addValue.sString = "8"; f.app->Tick(0);
		talent->Update(0, true); talent->IsReady();
		check(talent->GetCooldown() == 7 && talent->GetCurrentCooldown() == 7,
			"cache refresh and readiness queries preserve explicit and already-running cooldown timers");
		talent->Update(2, false);
		check(talent->GetCurrentCooldown() == 5, "cooldown-only update retains elapsed-time policy");
		talent->DropCooldown(true); talent->RecalculateAndRestartCooldown();
		check(talent->GetCooldown() == 18 && talent->GetCurrentCooldown() == 18, "next authored restart uses current coefficients after explicit override");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_COOLDOWN, "2e38", "0");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		check(!talent->IsReady() && talent->GetCurrentCooldown() == 0, "invalid initial modified duration cannot begin a cast");
		f.app->Block();
		check(talent->IsReady(), "removing invalid duration modifier restores admission without rewriting the idle timer");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(cast && f.hero->GetMana() == 160 && talent->GetCurrentCooldown() == 20,
			"recovered duration starts its real cooldown and payment");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		NDb::Talent* db = const_cast<NDb::Talent*>(talent->GetTalentDesc().GetPtr());
		NDb::AbilityModApplicator* effect = new NDb::AbilityModApplicator;
		effect->mode = NDb::ABILITYMODMODE_MANACOST; effect->targetAbilities = NDb::ABILITYIDFLAGS_TALENTS;
		effect->multValue.sString = "4"; effect->addValue.sString = "0";
		effect->enabled.sString = "true"; effect->startCondition.sString = "true"; effect->lifeTime.sString = "-1";
		db->applicators.push_back(effect);
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(cast && f.Modified() == 85 && f.hero->GetMana() == 175,
			"synchronous dispatch changes the next quote without repricing the admitted cast");
		check(talent->IsEnoughMana() && talent->GetManaCost() == 85, "next admission observes the modifier installed by the prior cast");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_STATE, "1", "0");
		f.app->Initialize(); f.app->Run(); CObj<PFTalent> talent = f.Talent();
		check(talent->IsSecondState(), "state modifier selects the original second cooldown slot");
		talent->RestartCooldown(20); f.app->Block();
		check(talent->IsReady(), "same-tick state removal checks the ready first timer rather than stale second timer");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(cast && !talent->IsSecondState() && f.hero->GetMana() == 160 && talent->GetCurrentCooldown() == 20,
			"same-tick state removal admits and restarts the correct real cooldown slot");
	}
}
}
