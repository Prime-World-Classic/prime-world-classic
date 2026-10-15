#pragma once

#include "PFApplicator.h"

namespace NWorld
{

int SendSpellToSingleTarget(PFBaseApplicator *pAppl, NDb::Ptr<NDb::Spell> pSpell, Target &targ, bool justCount = false );
int SendSpell2Targets(PFBaseApplicator *pAppl, NDb::Ptr<NDb::Spell> pSpell, NDb::Ptr<NDb::TargetSelector> pSelector, bool justCount = false);
int SendSpell2Targets(PFBaseApplicator *pAppl, NDb::Ptr<NDb::Spell> pSpell, PFTargetSelector* pSelector, bool justCount = false);
#if defined(PW_LINUX_NULL_RENDER)
/** Opt into active-parent cancellation for periodic responses; ordinary stop/cancel spells retain original admission. */
int SendSpell2Targets(PFBaseApplicator* pAppl, NDb::Ptr<NDb::Spell> pSpell,
	PFTargetSelector* pSelector, bool justCount, bool requireActiveParent);
#endif

enum CompareType
{
  ByTypeID,
  ByDBID
};

bool IsApplicatorInList( const NDb::BaseApplicator* dbApplicator, const NDb::ApplicatorsList* pApplicatorsList, CompareType compareType = ByDBID );

}
