/** Explicit instantiation exposes only the private method pointer for this test.
 * No production access, class layout, method body, or client notification is changed.
 */
struct UnitFlagsB12StunTag
{
	using Pointer = bool (PFBaseUnit::*)() const;
	friend Pointer UnitFlagsB12StunMember(UnitFlagsB12StunTag);
};

template<UnitFlagsB12StunTag::Pointer member>
struct UnitFlagsB12StunAccess
{
	friend UnitFlagsB12StunTag::Pointer UnitFlagsB12StunMember(UnitFlagsB12StunTag) { return member; }
};
template struct UnitFlagsB12StunAccess<&PFBaseUnit::IsStunned>;

static bool UnitFlagsB12Stunned(const PFBaseUnit* unit)
{
	return (unit->*UnitFlagsB12StunMember(UnitFlagsB12StunTag()))();
}

/** Inspect observer ownership only; flag queries and fog updates use the real unit. */
struct UnitFlagsB12Probe : ProbeUnit
{
	UnitFlagsB12Probe(PFWorld* world, const NDb::Unit* db)
		: ProbeUnit(world, db, NDb::FACTION_FREEZE) {}
	using PFBaseUnit::UpdateLinuxWarFogPosition;
	bool HasObserver() const
	{
		return visUnitData[NDb::FACTION_FREEZE].warFogObjectID != WAR_FOG_BAD_ID;
	}
};

/** Exhaust the authored flag bits, including the highest valid and unsupported bits. */
static void UnitFlagsB12Masks(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	unit->RemoveFlag(NDb::UNITFLAG_FORBIDREWARDS);
	const PFLogicObject* query = unit.GetPtr();
	const unsigned int unsupported = 1U << 31;
	const int count = NDb::KnownEnum<NDb::EUnitFlagType>::SizeOf();
	check(count == 31, "flag DB has 31 real bits, not the 32 enum entries including ISOLATED");
	check(unit->CheckFlag(0) && query->GetFlag(0), "empty flag mask succeeds on an empty unit");
	check(!unit->CheckFlag(~0U) && !unit->CheckFlag(unsupported), "empty unit rejects full and unsupported masks");
	for (int i = 0; i < count; ++i)
	{
		const unsigned int bit = 1U << i;
		const unsigned int other = 1U << ((i + 1) % count);
		unit->AddFlag(bit);
		check(unit->CheckFlag(bit) && query->GetFlag(bit) &&
			unit->CheckFlagType(static_cast<NDb::EUnitFlagType>(i)), "each authored bit is visible through all query APIs");
		check(!unit->CheckFlag(other), "an absent single bit does not match a different active flag");
		check(!unit->CheckFlag(bit | other) && !query->GetFlag(bit | other), "a composite requires its missing bit through virtual GetFlag too");
		check(!unit->CheckFlag(bit | unsupported), "an active real bit does not satisfy an unsupported requested bit");
		check(unit->CheckFlag(0), "empty mask remains true with an active flag");
		check(UnitFlagsB12Stunned(unit) == (bit == NDb::UNITFLAG_STUN), "only STUN controls IsStunned, not FREEZE or movement forbids");
		unit->AddFlag(other);
		check(unit->CheckFlag(bit | other) && query->GetFlag(bit | other), "a complete composite matches");
		unit->RemoveFlag(bit);
		check(!unit->CheckFlag(bit | other) && unit->CheckFlag(other), "removing either component invalidates only the composite");
		unit->RemoveFlag(other);
	}
	unit->AddFlag(unsupported);
	check(!unit->CheckFlag(unsupported) && unit->CheckFlag(0), "adding an unknown bit cannot create a stored flag");
	unit->AddFlag(~0U);
	check(unit->CheckFlag(0x7fffffffU) && !unit->CheckFlag(~0U), "all real flags still do not satisfy an unknown high bit");
	unit->RemoveFlag(~0U);
	check(unit->CheckFlag(0) && !unit->CheckFlag(0x7fffffffU) && !UnitFlagsB12Stunned(unit), "removing all flags restores empty-mask and stun state");
	check(!unit->CheckFlagType(static_cast<NDb::EUnitFlagType>(-1)) &&
		!unit->CheckFlagType(static_cast<NDb::EUnitFlagType>(count)), "typed query bounds remain unchanged");
}

/** Queries must follow reference counts and copies without changing resource pools. */
static void UnitFlagsB12Ownership(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_BURN, 20);
	const unsigned int pair = NDb::UNITFLAG_STUN | NDb::UNITFLAG_FORBIDMOVE;
	unit->SetHealth(123); unit->TakeMana(37);
	const float health = unit->GetHealth(), mana = unit->GetMana();
	unit->AddFlag(pair); unit->AddFlag(NDb::UNITFLAG_STUN);
	check(UnitFlagsB12Stunned(unit) && unit->CheckFlag(pair), "stacked stun owns the full stun movement pair");
	unit->RemoveFlag(pair);
	check(UnitFlagsB12Stunned(unit) && !unit->CheckFlag(pair) && !unit->CheckFlag(NDb::UNITFLAG_FORBIDMOVE),
		"one remaining stun owner does not retain the removed movement component");
	unit->AddFlag(NDb::UNITFLAG_FREEZE);
	unit->RemoveFlag(NDb::UNITFLAG_STUN);
	check(!UnitFlagsB12Stunned(unit) && unit->CheckFlag(NDb::UNITFLAG_FREEZE), "freeze alone does not imply stun after the last stun removal");
	unit->RemoveFlag(NDb::UNITFLAG_STUN);
	check(!UnitFlagsB12Stunned(unit), "extra native removal does not introduce a stun");
	unit->RemoveFlag(NDb::UNITFLAG_FREEZE);
	unit->AddFlag(0); unit->RemoveFlag(0);
	check(unit->CheckFlag(0) && !UnitFlagsB12Stunned(unit), "zero-mask mutation is a no-op");
	source->AddFlag(pair); source->AddFlag(NDb::UNITFLAG_STUN);
	unit->CopyFlagsFrom(source, false);
	check(UnitFlagsB12Stunned(unit) && unit->CheckFlag(pair), "replacement copies active stun and composite flags");
	unit->RemoveFlag(NDb::UNITFLAG_STUN);
	check(UnitFlagsB12Stunned(unit) && unit->CheckFlag(pair), "replacement preserves the source stun count, not just its presence");
	unit->RemoveFlag(NDb::UNITFLAG_STUN);
	check(!UnitFlagsB12Stunned(unit) && !unit->CheckFlag(pair) && UnitFlagsB12Stunned(source), "copied counters are independent of source ownership");
	unit->CopyFlagsFrom(source, true);
	unit->RemoveFlag(pair);
	check(UnitFlagsB12Stunned(unit) && unit->CheckFlag(pair), "additive copy retains independently stacked counters");
	unit->RemoveFlag(pair);
	check(!UnitFlagsB12Stunned(unit) && !unit->CheckFlag(pair), "final copied owners release both components");
	check(unit->GetHealth() == health && unit->GetMana() == mana, "flag mutations and queries leave current resources unchanged");
	check(unit->IsVulnerable() && !unit->IsFlying(), "single-bit vulnerability and flight callers start clear");
	unit->AddFlag(NDb::UNITFLAG_FORBIDTAKEDAMAGE | NDb::UNITFLAG_FLYING);
	check(!unit->IsVulnerable() && unit->IsFlying(), "single-bit vulnerability and flight callers retain their meaning");
	unit->RemoveFlag(NDb::UNITFLAG_FORBIDTAKEDAMAGE);
	check(unit->IsVulnerable() && unit->IsFlying(), "one single-bit caller is independent of the other flag");
	unit->RemoveFlag(NDb::UNITFLAG_FLYING);
}

/** Real flag applicators retain stun and all-bit queries across lifecycle transitions. */
static void UnitFlagsB12Buffs(Checks& check, PFWorld* world)
{
	CObj<B10CommandHero> hero = CommandHeroB10(world);
	NDb::FlagsApplicator* raw = new NDb::FlagsApplicator;
	const unsigned int pair = NDb::UNITFLAG_STUN | NDb::UNITFLAG_FORBIDMOVE;
	raw->flag = static_cast<NDb::EUnitFlag>(pair);
	NDb::Ptr<NDb::FlagsApplicator> db = raw;
	CObj<FlagBuffB12> a = FlagsB12(world, hero, db), b = FlagsB12(world, hero, db);
	check(a->Begin() && b->Begin(), "two real stun flag buffs start on a real hero");
	check(UnitFlagsB12Stunned(hero) && hero->CheckFlag(pair), "real flag buff enable is visible to the private stun query");
	a->Block();
	check(UnitFlagsB12Stunned(hero) && hero->CheckFlag(pair), "blocking one buff preserves the second owner");
	b->Finish(); b->Finish();
	check(!UnitFlagsB12Stunned(hero) && !hero->CheckFlag(pair), "stopping the final active buff clears stun and compound queries once");
	a->Unblock(); a->Reset();
	check(UnitFlagsB12Stunned(hero) && hero->CheckFlag(pair), "unblock restores stun and Reset does not add an owner");
	a->Finish();
	check(!UnitFlagsB12Stunned(hero) && !hero->CheckFlag(pair), "one stop after Reset releases the restored owner");
	hero->Cleanup();
}

/** Fog callers require full ISOLATED, not an arbitrary bit or invisibility alone. */
static void UnitFlagsB12Isolation(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> seed = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<UnitFlagsB12Probe> unit = new UnitFlagsB12Probe(world, seed->DbUnitDesc());
	const unsigned int isolated = NDb::UNITFLAG_ISOLATED;
	const unsigned int hidden = isolated | NDb::UNITFLAG_INVISIBLE;
	unit->CloseWarFog(true);
	for (unsigned int missing : {unsigned(NDb::UNITFLAG_FORBIDMOVE), unsigned(NDb::UNITFLAG_FORBIDUSETALENTS)})
	{
		unit->AddFlag(isolated & ~missing);
		check(!unit->CheckFlag(isolated), "isolation missing its first or last constituent is incomplete");
		unit->OpenWarFog(NDb::FACTION_FREEZE, -1, 6);
		check(unit->HasObserver(), "partial isolation still permits a real fog observer");
		unit->UpdateLinuxWarFogPosition();
		check(unit->HasObserver(), "position update preserves fog for partial isolation");
		unit->AddFlag(missing);
		check(unit->CheckFlag(isolated) && !unit->CheckFlag(hidden), "full isolation is not the hidden composite without invisibility");
		unit->UpdateLinuxWarFogPosition();
		check(!unit->HasObserver(), "full isolation closes the real fog observer");
		unit->OpenWarFog(NDb::FACTION_FREEZE, -1, 6);
		check(!unit->HasObserver(), "full isolation blocks opening a fog observer");
		unit->AddFlag(NDb::UNITFLAG_INVISIBLE);
		check(unit->CheckFlag(hidden), "full isolation plus invisibility satisfies the exact hidden composite");
		unit->RemoveFlag(hidden);
	}
	unit->AddFlag(NDb::UNITFLAG_INVISIBLE);
	check(!unit->CheckFlag(hidden) && !unit->CheckFlag(isolated), "invisibility alone is not isolation or the hidden composite");
	unit->OpenWarFog(NDb::FACTION_FREEZE, -1, 6);
	check(unit->HasObserver(), "invisibility alone still permits observer ownership");
	unit->RemoveFlag(NDb::UNITFLAG_INVISIBLE);
	unit->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	unit->Hide(true);
	check(unit->CheckFlag(hidden) && !unit->HasObserver(), "native Hide installs the complete composite and closes fog");
	unit->Hide(false);
	check(unit->CheckFlag(NDb::UNITFLAG_FORBIDMOVE) && !unit->CheckFlag(isolated) && !unit->CheckFlag(hidden),
		"native unhide retains independent movement ownership without reporting isolation");
	unit->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
	unit->CloseWarFog(true);
}

/** Each group uses a fresh loaded mock world; no shared probe world is stepped. */
static void UnitFlagsB12(Checks& check)
{
	for (auto test : {UnitFlagsB12Masks, UnitFlagsB12Ownership, UnitFlagsB12Buffs, UnitFlagsB12Isolation})
	{
		B8BehaviourForeignWorld fixture(check);
		if (fixture.world) test(check, fixture.world);
	}
}
