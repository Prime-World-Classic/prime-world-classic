#pragma once

#include "stdafx.h"
#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "PFClientBaseUnit.h"
#endif
#include "PFAuraEffect.h"

namespace NGameX
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAuraEffect::Apply(CPtr<PF_Core::ClientObjectBase> const &pObject)
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
	pBootstrapObject = pObject;
	NI_DATA_VERIFY(IsValid(pBootstrapObject),
								 NStr::StrFmt("Effect %s could be applied on client object", GetDBEffect().GetDBID().GetFileName().c_str()),
								 return; );

	bootstrapAuraAlly = GetDBEffect().type == NDb::AURATYPE_ALLY;
	bootstrapAuraActive = true;
	++bootstrapAuraChangeCount;
#else
	pUnit = dynamic_cast<PFClientBaseUnit*>(pObject.GetPtr());
	NI_DATA_VERIFY(pUnit,
								 NStr::StrFmt("Effect %s could be applied on base unit onlu", GetDBEffect().GetDBID().GetFileName().c_str()), 
								 return; );
	pUnit->AcknowledgeAuraChange(GetDBEffect().type == NDb::AURATYPE_ALLY, true);
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAuraEffect::Die()
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
	if (bootstrapAuraActive)
	{
		bootstrapAuraActive = false;
		++bootstrapAuraChangeCount;
	}

	pBootstrapObject = 0;
  BasicEffect::Die();
#else
	if ( IsValid( pUnit ) )
	{
		pUnit->AcknowledgeAuraChange(GetDBEffect().type == NDb::AURATYPE_ALLY, false);
	}

  BasicEffect::Die();
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAuraEffect::DieImmediate()
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  pBootstrapObject = 0;
	bootstrapAuraActive = false;
#else
  pUnit = 0;
#endif

  BasicEffect::DieImmediate();
}

}
