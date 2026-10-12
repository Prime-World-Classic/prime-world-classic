/** Compare complete modifier records, including IDs that must remain destination-local. */
static bool CopyStatSameB12(const ValueWithModifiers& first, const ValueWithModifiers& second)
{
	const auto& a = first.GetModifiers();
	const auto& b = second.GetModifiers();
	if (a.size() != b.size()) return false;
	for (int index = 0; index < a.size(); ++index)
	{
		const auto& x = a[index]; const auto& y = b[index];
		if (x.id != y.id || x.provider != y.provider || x.mulValue != y.mulValue ||
			x.addValue != y.addValue || x.active != y.active || x.top != y.top || x.constraint != y.constraint)
			return false;
	}
	return true;
}

/** Assert the original engine's transfer semantics independently of the native unit hook. */
static void CopyStatRetrieveB12(Checks& check)
{
	ValueWithModifiers source(100), destination(10);
	const int disabled = source.AddModifier(2, 3, PF_Core::WORLD_ID, true, NDb::STATCONSTRAINT_UPPERLIMIT);
	source.EnableModifier(disabled, false);
	source.AddModifier(5, 7, 117);
	destination.AddModifier(1, 4, 900);
	source.Retrieve(&destination, PF_Core::WORLD_ID);
	const auto& copied = destination.GetModifiers()[1];
	check(copied.mulValue == 2 && copied.addValue == 3 && copied.provider == PF_Core::WORLD_ID && copied.top,
		"original Retrieve keeps coefficients, provider and top while filtering providers");
	check(copied.active && copied.constraint == NDb::STATCONSTRAINT_NONE && copied.id == 1,
		"original Retrieve enables copies, clears constraints and allocates destination-local IDs");
	check(source.GetBaseValue() == 100 && destination.GetBaseValue() == 10 && destination.GetValue() == 31,
		"original Retrieve neither replaces base values nor applies source activation state");
}

/** Exercise real stat and core layers, existing entries, resource pools and copy independence. */
static void CopyStatLayersB12(Checks& check, PFWorld* world)
{
	ValueWithModifiers sourceCore(200), destinationCore(10);
	sourceCore.AddModifier(1, 99, PF_Core::WORLD_ID);
	destinationCore.AddModifier(1, 1, 901);
	const ValueWithModifiers savedSourceCore = sourceCore, savedDestinationCore = destinationCore;
	CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> destination = Unit(world, NDb::FACTION_BURN, 15);
	StatValueWithModifiers* src = source->GetStat(NDb::STAT_STRENGTH);
	StatValueWithModifiers* dst = destination->GetStat(NDb::STAT_STRENGTH);
	const int sourceInitialCount = src->GetModifiers().size(), destinationInitialCount = dst->GetModifiers().size();
	src->SetCore(&sourceCore); dst->SetCore(&destinationCore);
#ifndef _SHIPPING
	src->SetCheatModifier(2); dst->SetCheatModifier(3);
#endif
	src->AddModifier(2, 5, PF_Core::WORLD_ID);
	const int disabled = src->AddModifier(3, 7, PF_Core::WORLD_ID, true);
	src->EnableModifier(disabled, false);
	src->AddModifier(0, 20, PF_Core::WORLD_ID, false, NDb::STATCONSTRAINT_LOWERLIMIT);
	src->AddModifier(0, 80, PF_Core::WORLD_ID, false, NDb::STATCONSTRAINT_UPPERLIMIT);
	src->AddModifier(9, 99, 501);
	src->AddModifier(1, 4, PF_Core::INVALID_OBJECT_ID);
	dst->AddModifier(1, 2, 901);
	const int removed = dst->AddModifier(1, 0, 901); dst->RemoveModifier(removed);
	const int existingDisabled = dst->AddModifier(8, 20, 901, true, NDb::STATCONSTRAINT_LOWERLIMIT);
	dst->EnableModifier(existingDisabled, false);
	const StatValueWithModifiers savedSource = *src;
	StatValueWithModifiers expected = *dst;
	src->Retrieve(&expected, PF_Core::WORLD_ID);
	source->GetVariableVWM("CopyStatPrivate")->AddModifier(2, 7, PF_Core::WORLD_ID);
	destination->GetVariableVWM("CopyStatPrivate")->SetBaseValue(11);
	source->SetHealth(600); destination->SetHealth(700);
	source->TakeMana(57); destination->TakeMana(31);
	const float oldSourceValue = src->GetValue(), oldBase = dst->GetBaseValue();
	const float oldCore = dst->GetCoreValue(), oldUnmodified = dst->GetUnmodifiedValue();
	(void)dst->GetValue();
	destination->CopyStatModifiersFrom(source.GetPtr(), PF_Core::WORLD_ID);
	check(CopyStatSameB12(*dst, expected) && dst->GetValue() == expected.GetValue(),
		"native filtered copy exactly matches engine Retrieve including cache invalidation and existing entries");
	check(dst->GetModifiers().size() == destinationInitialCount + 6 && dst->Retrieve(existingDisabled + 1) &&
		dst->Retrieve(existingDisabled + 4) && !dst->Retrieve(removed),
		"copy appends fresh destination IDs without resurrecting a removed ID");
	check(CopyStatSameB12(*src, savedSource) && src->GetValue() == oldSourceValue,
		"source modifiers, activation, constraints and evaluated value remain unchanged");
	check(dst->GetBaseValue() == oldBase && dst->GetCore() == &destinationCore &&
		dst->GetCoreValue() == oldCore && dst->GetUnmodifiedValue() == oldUnmodified,
		"destination base, core binding, unmodified value and cheat layer are retained");
	check(src->GetBaseValue() == 40 && src->GetCore() == &sourceCore &&
		CopyStatSameB12(sourceCore, savedSourceCore) && CopyStatSameB12(destinationCore, savedDestinationCore) &&
		sourceCore.GetBaseValue() == 200 && destinationCore.GetBaseValue() == 10,
		"copy leaves both core layers intact and never exports core modifiers");
	check(source->GetHealth() == 600 && source->GetMana() == 143 && destination->GetHealth() == 700 && destination->GetMana() == 169,
		"stat copying does not alter either current resource pool");
	check(destination->GetVariableValue("CopyStatPrivate") == 11 &&
		!destination->GetVariableVWM("CopyStatPrivate")->HasModifiers(), "unit variables are outside the stat-copy contract");
	src->Retrieve(&expected, PF_Core::WORLD_ID);
	destination->CopyStatModifiersFrom(source.GetPtr(), PF_Core::WORLD_ID);
	check(CopyStatSameB12(*dst, expected), "repeated external copy appends again instead of deduplicating a provider");
	src->EnableModifier(disabled, true); src->RemoveModifiers(PF_Core::WORLD_ID);
	check(CopyStatSameB12(*dst, expected), "later source activation and removal do not own or remove destination copies");
	dst->RemoveModifiers(PF_Core::WORLD_ID);
	check(dst->GetModifiers().size() == destinationInitialCount + 2 && dst->Retrieve(existingDisabled) && !dst->Retrieve(existingDisabled)->active,
		"ordinary destination provider removal preserves its unrelated inactive constrained modifier");
	check(src->GetModifiers().size() == sourceInitialCount + 2, "destination cleanup never mutates remaining source modifiers");
	// Core storage belongs to this fixture, not to either unit.
	src->SetCore(nullptr); dst->SetCore(nullptr);
}

/** Every authored stat is visited, including resource maxima, without assigning current pools. */
static void CopyStatAllB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> destination = Unit(world, NDb::FACTION_BURN, 15);
	vector<StatValueWithModifiers> saved, expected;
	const int count = NDb::KnownEnum<NDb::EStat>::sizeOf;
	for (int index = 0; index < count; ++index)
	{
		StatValueWithModifiers* src = source->GetStat(NDb::EStat(index));
		StatValueWithModifiers* dst = destination->GetStat(NDb::EStat(index));
		src->AddModifier(1.25f, float(index + 1), PF_Core::WORLD_ID, index % 2 != 0);
		src->AddModifier(2, float(index + 3), -27);
		src->AddModifier(1, 4, PF_Core::INVALID_OBJECT_ID);
		dst->AddModifier(1, 2, 901);
		saved.push_back(*src); expected.push_back(*dst);
		src->Retrieve(&expected.back(), PF_Core::WORLD_ID);
	}
	source->SetHealth(600); destination->SetHealth(700);
	source->TakeMana(57); destination->TakeMana(31);
	destination->CopyStatModifiersFrom(source.GetPtr(), PF_Core::WORLD_ID);
	for (int index = 0; index < count; ++index)
	{
		StatValueWithModifiers* src = source->GetStat(NDb::EStat(index));
		StatValueWithModifiers* dst = destination->GetStat(NDb::EStat(index));
		check(CopyStatSameB12(*dst, expected[index]) && dst->GetValue() == expected[index].GetValue(),
			"each native stat matches the original provider-filtered transfer");
		check(CopyStatSameB12(*src, saved[index]) && src->GetBaseValue() == saved[index].GetBaseValue(),
			"copying any stat leaves its source base and modifier records unchanged");
		src->Retrieve(&expected[index], PF_Core::INVALID_OBJECT_ID);
	}
	destination->CopyStatModifiersFrom(source.GetPtr());
	for (int index = 0; index < count; ++index)
		check(CopyStatSameB12(*destination->GetStat(NDb::EStat(index)), expected[index]),
			"default provider copies all records including negative and invalid-object providers");
	check(source->GetHealth() == 600 && source->GetMana() == 143 && destination->GetHealth() == 700 && destination->GetMana() == 169,
		"changed maximum-life and energy modifiers never refill or rescale current resources");
	StatValueWithModifiers expectedNegative = *destination->GetStat(NDb::STAT_STRENGTH);
	source->GetStat(NDb::STAT_STRENGTH)->Retrieve(&expectedNegative, -27);
	destination->CopyStatModifiersFrom(source.GetPtr(), -27);
	check(CopyStatSameB12(*destination->GetStat(NDb::STAT_STRENGTH), expectedNegative),
		"a negative provider other than INVALID_OBJECT_ID remains an exact provider filter");
}

/** Source health is not ownership; engine-invalid references and self-copy must be harmless. */
static void CopyStatGuardsB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> destination = Unit(world, NDb::FACTION_BURN, 15);
	StatValueWithModifiers* dst = destination->GetStat(NDb::STAT_STRENGTH);
	dst->AddModifier(2, 3, PF_Core::WORLD_ID);
	const StatValueWithModifiers original = *dst;
	destination->CopyStatModifiersFrom(nullptr);
	check(CopyStatSameB12(*dst, original), "null source is a no-op");
	destination->CopyStatModifiersFrom(destination.GetPtr());
	destination->CopyStatModifiersFrom(destination.GetPtr(), PF_Core::WORLD_ID);
	check(CopyStatSameB12(*dst, original), "self-copy is a bounded no-op rather than mutating Retrieve's own vector");
	destination->CopyStatModifiersFrom(source.GetPtr(), 123456);
	check(CopyStatSameB12(*dst, original), "unmatched provider neither clears nor appends modifiers");
	CObj<PFBaseUnit> temporary = Unit(nullptr, NDb::FACTION_FREEZE, 40);
	CPtr<PFBaseUnit> expired = temporary.GetPtr();
	temporary = nullptr;
	check(expired.GetPtr() && !IsValid(expired), "engine weak reference fixture is invalidated without a dangling allocation");
	destination->CopyStatModifiersFrom(expired.GetPtr());
	check(CopyStatSameB12(*dst, original), "engine-invalid source cannot import reconstructed default contents");
	source->GetStat(NDb::STAT_STRENGTH)->AddModifier(1, 9, PF_Core::WORLD_ID);
	source->ChangeHealth(-source->GetHealth());
	StatValueWithModifiers expected = *dst;
	source->GetStat(NDb::STAT_STRENGTH)->Retrieve(&expected, PF_Core::WORLD_ID);
	destination->CopyStatModifiersFrom(source.GetPtr(), PF_Core::WORLD_ID);
	check(source->IsDead() && IsValid(source) && CopyStatSameB12(*dst, expected),
		"a retained valid source with zero health still copies data as Windows does");
}

/** Reject only a selected malformed stat batch; unrelated providers and stats remain usable. */
static void CopyStatNumericB12(Checks& check, PFWorld* world)
{
	for (float malformed : {std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	for (bool multiplier : {false, true})
	{
		CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> destination = Unit(world, NDb::FACTION_BURN, 15);
		StatValueWithModifiers* src = source->GetStat(NDb::STAT_STRENGTH);
		StatValueWithModifiers* dst = destination->GetStat(NDb::STAT_STRENGTH);
		const int initialCount = src->GetModifiers().size();
		src->AddModifier(1, 5, PF_Core::WORLD_ID);
		const int bad = src->AddModifier(multiplier ? malformed : 1, multiplier ? 0 : malformed, PF_Core::WORLD_ID);
		if (multiplier) src->EnableModifier(bad, false);
		src->AddModifier(2, 3, 117);
		dst->AddModifier(1, 4, 901);
		const StatValueWithModifiers before = *dst;
		source->GetStat(NDb::STAT_INTELLECT)->AddModifier(1, 9, PF_Core::WORLD_ID);
		destination->CopyStatModifiersFrom(source.GetPtr(), PF_Core::WORLD_ID);
		check(CopyStatSameB12(*dst, before), "selected NaN or infinity rejects the whole affected stat before any append");
		check(destination->GetStatValue(NDb::STAT_INTELLECT) == 9,
			"a malformed selected coefficient does not suppress another valid stat");
		StatValueWithModifiers expected = *dst; src->Retrieve(&expected, 117);
		destination->CopyStatModifiersFrom(source.GetPtr(), 117);
		check(CopyStatSameB12(*dst, expected), "non-finite coefficients of an unselected provider do not block the selected provider");
		destination->CopyStatModifiersFrom(source.GetPtr());
		check(CopyStatSameB12(*dst, expected), "all-provider copy also rejects a malformed stat without partial append");
		check(src->GetModifiers().size() == initialCount + 3 &&
			!std::isfinite(multiplier ? src->GetModifiers()[initialCount + 1].mulValue : src->GetModifiers()[initialCount + 1].addValue) &&
			(!multiplier || !src->Retrieve(bad)->active),
			"numeric guard does not sanitize or delete source records");
	}
	CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> destination = Unit(world, NDb::FACTION_BURN, 15);
	StatValueWithModifiers* src = source->GetStat(NDb::STAT_STRENGTH);
	StatValueWithModifiers* dst = destination->GetStat(NDb::STAT_STRENGTH);
	src->AddModifier(0, -5, PF_Core::WORLD_ID, true);
	src->AddModifier(-2, 0, PF_Core::WORLD_ID);
	src->AddModifier(std::numeric_limits<float>::max(), 0, 117);
	StatValueWithModifiers expected = *dst; src->Retrieve(&expected, PF_Core::WORLD_ID);
	destination->CopyStatModifiersFrom(source.GetPtr(), PF_Core::WORLD_ID);
	check(CopyStatSameB12(*dst, expected) && dst->GetValue() == expected.GetValue(),
		"finite zero and negative coefficients retain engine semantics without arbitrary clamping");
	src->Retrieve(&expected, 117); destination->CopyStatModifiersFrom(source.GetPtr(), 117);
	check(CopyStatSameB12(*dst, expected), "finite large coefficients are preserved as records, not bounded by unrelated stat policy");
}

/** Additive entrypoint: real initialized units and unmodified engine modifier operations. */
static void CopyStatB12(Checks& check, PFWorld* world)
{
	CopyStatRetrieveB12(check);
	CopyStatLayersB12(check, world);
	CopyStatAllB12(check, world);
	CopyStatGuardsB12(check, world);
	CopyStatNumericB12(check, world);
}
