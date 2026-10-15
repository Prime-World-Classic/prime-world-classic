/** Direct native casts must reject invalid context before creating dispatch work. */
static void AbilityAdmissionB10(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ability* raw = new NDb::Ability;
	raw->manaCost.sString = "70";
	raw->cooldownTime.sString = "0";
	NDb::Ptr<NDb::Ability> db = raw;
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	data->AddForbid();
	check(!data->ApplyToTarget(Target(receiver.GetPtr())) && sender->GetMana() == 200,
		"direct cast cannot bypass an ability forbid");
	data->RemoveForbid();
	CObj<NCore::IWorldBase> otherOwner = PFWorld::CreatePFWorld();
	PFWorld* other = dynamic_cast<PFWorld*>(otherOwner.GetPtr());
	CObj<PFBaseUnit> foreign = Unit(other, NDb::FACTION_BURN, 20);
	check(!data->IsTargetValid(Target(foreign.GetPtr()), true),
		"allow-dead policy cannot admit a foreign-world target");
	check(!data->ApplyToTarget(Target(foreign.GetPtr())) && sender->GetMana() == 200,
		"foreign direct cast cannot create paid work");
	for (const CVec3 point : {CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0),
		CVec3(5, std::numeric_limits<float>::infinity(), 0), CVec3(5, 5, -std::numeric_limits<float>::infinity())})
	{
		check(!data->IsTargetValid(Target(point)) && !data->ApplyToTarget(Target(point)),
			"nonfinite position is rejected before dispatch construction");
	}
	check(!data->ApplyToTarget(Target()) && data->GetActiveInstancesCount() == 0,
		"empty target cannot register an activated no-op cast");
	check(data->IsTargetValid(Target(receiver.GetPtr())), "same-world living unit remains valid");
	receiver->ChangeHealth(-1000);
	check(!data->IsTargetValid(Target(receiver.GetPtr())) && data->IsTargetValid(Target(receiver.GetPtr()), true),
		"allow-dead remains a liveness override within one world");
	raw->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
	check(data->IsTargetValid(Target(receiver.GetPtr())) && data->IsTargetValid(Target(sender.GetPtr())),
		"authored apply-to-dead flag additionally admits dead targets without excluding living passive owners");
	sender->ChangeHealth(-1000);
	check(!data->ApplyToTarget(Target(receiver.GetPtr())) && sender->GetMana() == 200,
		"dead caster cannot launch a new paid ability");
}
