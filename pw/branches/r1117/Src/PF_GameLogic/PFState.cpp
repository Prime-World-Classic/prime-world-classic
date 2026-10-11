#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFState.h"
#include "PFBaseMovingUnit.h"
#include "PFHero.h"
#include "PFMainBuilding.h"
#include "PFPet.h"

namespace NWorld
{

/** Use-state and guard descendants need owner-world identity and weak registration, not world stepping. */
PFBaseUnitState::PFBaseUnitState(PFBaseUnit* unit)
	: TPFState<PFBaseUnit>(unit), PFWorldObjectBase(unit ? unit->GetWorld() : 0, 0) {}
/** Nested production states resolve their world through this base object. */
PFBaseMovingUnitState::PFBaseMovingUnitState(PFBaseMovingUnit* unit) : TPFState<PFBaseMovingUnit>(unit), PFWorldObjectBase(unit ? unit->GetWorld() : 0, 0) {}
/** Hero state parents must share their owner's context with nested moving-unit states. */
PFBaseHeroState::PFBaseHeroState(PFBaseHero* unit)
	: TPFState<PFBaseHero>(unit), PFWorldObjectBase(unit ? unit->GetWorld() : 0, 0) {}
/** Main-building guards keep the same base world context as their initialized building. */
PFMainBuildingState::PFMainBuildingState(PFMainBuilding* unit)
	: TPFState<PFMainBuilding>(unit), PFWorldObjectBase(unit ? unit->GetWorld() : 0, 0) {}
/** Pet escort states retain their world even though their owner is not a hero or ordinary creep. */
PFBasePetUnitState::PFBasePetUnitState(PFBasePetUnit* unit)
	: TPFState<PFBasePetUnit>(unit), PFWorldObjectBase(unit ? unit->GetWorld() : 0, 0) {}

void PFBaseUnitState::DumpState(const char* state) { (void)state; }
void PFBaseMovingUnitState::DumpState(const char* state) { (void)state; }
void PFBaseHeroState::DumpState(const char* state) { (void)state; }
void PFMainBuildingState::DumpState(const char* state) { (void)state; }
void PFBasePetUnitState::DumpState(const char* state) { (void)state; }

void DumpSimpleState(IPFState* state, int depths) { (void)state; (void)depths; }

} // namespace NWorld

BASIC_REGISTER_CLASS(NWorld::IPFState);

REGISTER_WORLD_OBJECT_NM(PFBaseUnitState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBaseMovingUnitState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBaseHeroState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFMainBuildingState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBasePetUnitState, NWorld);

#else

#include "PFState.h"
#include "PFBaseUnit.h"
#include "PFBaseMovingUnit.h"
#include "PFHero.h"
#include "PFMainBuilding.h"
#include "PFPet.h"
#include "PFAIContainer.h"

namespace NWorld
{
PFBaseUnitState::PFBaseUnitState( PFBaseUnit* unit ) : TPFState<PFBaseUnit>( unit ), PFWorldObjectBase( unit->GetWorld(), 0 ) 
{
}

PFBaseMovingUnitState::PFBaseMovingUnitState( PFBaseMovingUnit* unit ) : TPFState<PFBaseMovingUnit>( unit ), PFWorldObjectBase( unit->GetWorld(), 0 ) 
{
}

PFBaseHeroState::PFBaseHeroState( PFBaseHero* unit ) : TPFState<PFBaseHero>( unit ), PFWorldObjectBase( unit->GetWorld(), 0 ) 
{
}

PFMainBuildingState::PFMainBuildingState( PFMainBuilding* unit ) : TPFState<PFMainBuilding>( unit ), PFWorldObjectBase( unit->GetWorld(), 0 ) 
{
}

PFBasePetUnitState::PFBasePetUnitState( PFBasePetUnit* unit ) : TPFState<PFBasePetUnit>( unit ), PFWorldObjectBase( unit->GetWorld(), 0 ) 
{
}

void PFBaseUnitState::DumpState( const char* state )
{
	if( IsValid( pOwner ) && pOwner->IsDumpStates() && state )
		pOwner->GetWorld()->GetIAdventureScreen()->DumpState( GetObjectTypeName(), state );
}

void PFBaseMovingUnitState::DumpState( const char* state )
{
	if( IsValid( pOwner ) && pOwner->IsDumpStates() && state )
		pOwner->GetWorld()->GetIAdventureScreen()->DumpState( GetObjectTypeName(), state );
}

void PFBaseHeroState::DumpState( const char* state )
{
	if( IsValid( pOwner ) && pOwner->IsDumpStates() && state )
		pOwner->GetWorld()->GetIAdventureScreen()->DumpState( GetObjectTypeName(), state );
}

void PFMainBuildingState::DumpState( const char* state )
{
	if( IsValid( pOwner ) && pOwner->IsDumpStates() && state )
		pOwner->GetWorld()->GetIAdventureScreen()->DumpState( GetObjectTypeName(), state );
}

void PFBasePetUnitState::DumpState( const char* state )
{
	if( IsValid( pOwner ) && pOwner->IsDumpStates() && state )
		pOwner->GetWorld()->GetIAdventureScreen()->DumpState( GetObjectTypeName(), state );
}

} // namespace NWorld

BASIC_REGISTER_CLASS(NWorld::IPFState);

REGISTER_WORLD_OBJECT_NM(PFBaseUnitState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBaseMovingUnitState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBaseHeroState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFMainBuildingState, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBasePetUnitState, NWorld);

#endif
