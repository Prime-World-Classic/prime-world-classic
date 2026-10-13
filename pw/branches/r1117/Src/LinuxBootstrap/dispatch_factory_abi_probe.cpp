/** Compile with each maintained factory/caller profile, not just the main client flags. */
#include "PF_GameLogic/stdafx.h"
#include "PF_GameLogic/PFAbilityInstance.h"
#include "PF_GameLogic/PFBaseAttackData.h"
#include "PF_GameLogic/PFDispatchFactory.h"
#include <type_traits>

using DispatchResult = decltype(NWorld::CreateDispatch(CObj<NWorld::PFAbilityInstance>(),
	CPtr<NWorld::PFBaseApplicator>(), NWorld::Target(), NWorld::Target(), NDb::Ptr<NDb::Spell>()));
static_assert(std::is_same<DispatchResult, CObj<NWorld::PFDispatch>>::value,
	"Every native factory caller must receive an owning dispatch handle");
static_assert(std::is_same<decltype(&NWorld::PFBaseAttackInstance::ApplyAttack),
	bool (NWorld::PFBaseAttackInstance::*)()>::value,
	"Every native attack caller must observe dispatch admission failure");
