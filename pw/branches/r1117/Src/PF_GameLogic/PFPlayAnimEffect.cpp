#pragma once

#include "stdafx.h"
#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "PFCreature.h"
#include "PFClientCreature.h"
#endif
#include "PFPlayAnimEffect.h"

namespace NGameX
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFPlayAnimEffect::Apply(CPtr<PF_Core::ClientObjectBase> const &pUnit)
{
	NI_DATA_VERIFY(GetDBEffect().animGraphNode.size(), 
		             NStr::StrFmt("No animation name in effect %s", GetDBEffect().GetDBID().GetFileName().c_str()), 
								 return; );

#if defined(PW_LINUX_DB_BOOTSTRAP)
  pBootstrapObject = pUnit;
  NI_DATA_VERIFY(IsValid(pBootstrapObject),
                 NStr::StrFmt("Effect %s could be applied on client object", GetDBEffect().GetDBID().GetFileName().c_str()),
                 return; );

  bootstrapApplied = true;
  bootstrapReturned = false;
  bootstrapSceneUpdated = false;

  if (NScene::SceneObject* const pSceneObject = pBootstrapObject->GetSceneObject())
  {
    pSceneObject->UpdateForced(0.1f);
    bootstrapSceneUpdated = true;
  }
#else
	pOwner    = dynamic_cast<PFClientBaseUnit *> (pUnit.GetPtr());
	pAnimated = dynamic_cast<IAnimatedClientObject*>(pUnit.GetPtr());
	NI_DATA_VERIFY(NULL != pAnimated && IsValid(pOwner),
								 NStr::StrFmt("Effect %s could be applied on animated objects only", GetDBEffect().GetDBID().GetFileName().c_str()), 
								 return; );

	// Remember state ID to return
	returnStateId = pAnimated->GetCurrentStateId();

	unsigned int stateId = pAnimated->GetStateIdByName(GetDBEffect().animGraphNode.c_str());

  if ( stateId == DIANGR_NO_SUCH_ELEMENT )
  {
    DebugTrace( NStr::StrFmt("Bad animation name '%s' in effect %s", GetDBEffect().animGraphNode.c_str(), GetDBEffect().GetDBID().GetFileName().c_str()) );
    return;
  }

	pAnimated->SetAnimStateId(stateId);
	
	if (GetDBEffect().marker.size())
		pAnimated->ReachStateMarker(GetDBEffect().marker.c_str(), GetDBEffect().markerReachTime);

	if ( pUnit->GetSceneObject() )
		pUnit->GetSceneObject()->UpdateForced(0.1f, false, pOwner->IsVisible());
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFPlayAnimEffect::Die()
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  bootstrapReturned = bootstrapApplied;
  pBootstrapObject = 0;
  BasicEffect::Die();
#else
	if (!IsValid(pOwner) || !pAnimated )
	{
		BasicEffect::DieImmediate();
		return;
	}

	if (GetDBEffect().goToOtherNodeOnStop && !pOwner->WorldObject()->IsDead() && !IsInterrupted())
	{
		if (GetDBEffect().returnAnimGraphNode.size())
		{
			unsigned int stateId = pAnimated->GetStateIdByName(GetDBEffect().returnAnimGraphNode.c_str());

      if ( stateId == DIANGR_NO_SUCH_ELEMENT )
      {
        DebugTrace( NStr::StrFmt("Bad animation name '%s' in effect %s", GetDBEffect().returnAnimGraphNode.c_str(), GetDBEffect().GetDBID().GetFileName().c_str()) );
        return;
      }

      if ( pAnimated->GetNextStateId() != stateId )
      {
        pAnimated->SetAnimStateId( stateId );
      }
		}
		else
		{
			if (returnStateId != DIANGR_NO_SUCH_ELEMENT)
				pAnimated->SetAnimStateId(returnStateId);
		}
	}
  BasicEffect::Die();
#endif
}

void PFPlayAnimEffect::DieImmediate()
{ 
#if defined(PW_LINUX_DB_BOOTSTRAP)
  pBootstrapObject = 0;
  bootstrapApplied = false;
#else
  pOwner    = 0;
  pAnimated = 0;
#endif

  BasicEffect::DieImmediate();
}
}
