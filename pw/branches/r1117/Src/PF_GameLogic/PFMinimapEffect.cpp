#pragma once

#include "stdafx.h"
#include "PFMinimapEffect.h"
#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "AdventureScreen.h"
#include "Minimap.h"
#include "PFClientLogicObject.h"
#endif

namespace NGameX
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFMinimapEffect::Apply(CPtr<PF_Core::ClientObjectBase> const &pObject_)
{
  SetDeathType( NDb::EFFECTDEATHTYPE_MANUAL, 0.0f );
#if defined(PW_LINUX_DB_BOOTSTRAP)
  pObject = pObject_;
  NI_DATA_VERIFY(IsValid(pObject),
    NStr::StrFmt("Effect %s could not be applied on client object", GetDBEffect().GetDBID().GetFileName().c_str()),
    return; );

  index = static_cast<int>(GetDBEffect().effect);
  bootstrapActive = true;
  bootstrapUpdateCount = 0;
  if (NScene::SceneObject* const pSceneObject = pObject->GetSceneObject())
  {
    lastPosition = pSceneObject->GetPosition().pos;
  }
#else
  pObject = dynamic_cast<PFClientLogicObject*>(pObject_.GetPtr());
  NI_DATA_VERIFY(IsValid(pObject),
    NStr::StrFmt("Effect %s could not be applied on logic object", GetDBEffect().GetDBID().GetFileName().c_str()), 
    return; );

  index = AdventureScreen::Instance()->GetMinimap()->AddMinimapEffect( GetDBEffect().effect );
#endif
}

void PFMinimapEffect::Update( float timeDelta )
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  BasicEffect::Update(timeDelta);
  if (bootstrapActive && IsValid(pObject))
  {
    if (NScene::SceneObject* const pSceneObject = pObject->GetSceneObject())
    {
      lastPosition = pSceneObject->GetPosition().pos;
    }
    ++bootstrapUpdateCount;
  }
#else
  if ( IsValid( pObject ) )
    AdventureScreen::Instance()->GetMinimap()->UpdateMinimapEffect( index, pObject->GetPosition().pos );
#endif
}

void PFMinimapEffect::Die()
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  bootstrapActive = false;
  pObject = 0;
  index = -1;
  BasicEffect::Die();
#else
  AdventureScreen::Instance()->GetMinimap()->SetMinimapEffect( index, (NDb::EMinimapEffects)(-1) );
  DieImmediate();
#endif
}

}
