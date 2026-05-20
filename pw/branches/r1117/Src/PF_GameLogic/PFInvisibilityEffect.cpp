#include "stdafx.h"

#include "PFInvisibilityEffect.h"

#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "PFClientLogicObject.h"
#endif

#if defined(PW_LINUX_DB_BOOTSTRAP)
#include "ClientVisibilityHelper_linuxbootstrap.h"
#else
#include "ClientVisibilityHelper.h"
#endif

namespace NGameX
{

void InvisibilityEffect::Apply(CPtr<PF_Core::ClientObjectBase> const &pObject)
{
  PF_Core::ScaleColorEffect::Apply(pObject);

  NI_VERIFY(IsValid(pObject), "Invalid object", return);

#if defined(PW_LINUX_DB_BOOTSTRAP)
  pClientLogicObject = pObject;
#else
  pClientLogicObject = dynamic_cast<NGameX::PFClientLogicObject*>(pObject.GetPtr());
#endif

  NI_VERIFY(IsValid(pClientLogicObject), "Invalid object type", return);
}

void InvisibilityEffect::Apply(float t, bool)
{
  if (!IsValid(this->pChannel))
    return;
  if (!IsValid(pClientLogicObject))
    return;

  float opacity = 1.f - t;

  if (!ClientVisibilityHelper::IsVisibleForPlayer(pClientLogicObject.GetPtr()))
    opacity = 0.f;

  this->pChannel->SetOpacity( opacity );
}

}
