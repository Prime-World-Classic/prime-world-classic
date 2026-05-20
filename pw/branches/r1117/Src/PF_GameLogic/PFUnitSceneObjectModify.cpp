#pragma once

#include "stdafx.h"
#include "../Scene/SceneObject.h"
#include "../Scene/SceneComponent.h"
#include "../Scene/DBScene.h"
#include "PFUnitSceneObjectModify.h"
#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "PFClientCreature.h"
#include "PFWorldNatureMap.h"
#include "AdventureScreen.h"
#include "PFClientVisibilityMap.h"
#endif

namespace NGameX
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFUnitSceneObjectModify::Apply(CPtr<PF_Core::ClientObjectBase> const &pObject)
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  pBootstrapTarget = pObject;

  NScene::SceneObject* const pTargetSceneObject =
    IsValid(pObject) ? pObject->GetSceneObject() : 0;
  NI_DATA_VERIFY(pTargetSceneObject,
                 NStr::StrFmt("Effect %s could be applied on scene object", GetDBEffect().GetDBID().GetFileName().c_str()),
                 return; );

  NScene::IScene* const pScene = pTargetSceneObject->GetScene();
  NI_VERIFY(pScene, NStr::StrFmt("Target scene object should present in scene", GetDBEffect().GetDBID().GetFileName().c_str()), return; );

  NDb::Ptr<NDb::DBSceneObject> pDBSceneObject;
  for (int i = 0; i < GetDBEffect().sceneObjects.size(); ++i)
  {
    if (GetDBEffect().sceneObjects[i])
    {
      pDBSceneObject = GetDBEffect().sceneObjects[i];
      break;
    }
  }

  NI_DATA_VERIFY(pDBSceneObject,
                 NStr::StrFmt("Effect %s should contain scene object at least in Neutral state", GetDBEffect().GetDBID().GetFileName().c_str()),
                 return; );

  Reset(pBootstrapSceneObject, new NScene::SceneObject(pScene, pDBSceneObject.GetPtr()));

  CObj<NScene::SceneComponent> pRootComponent(new NScene::SceneComponent());
  pRootComponent->Init();
  pBootstrapSceneObject->Add(pRootComponent);
  pBootstrapSceneObject->SetOwnerID(pTargetSceneObject->GetOwnerID());
  pBootstrapSceneObject->SetPlacement(pTargetSceneObject->GetPosition());
  pBootstrapSceneObject->UpdateForced();
  pBootstrapSceneObject->AddToScene(pScene);
  bootstrapSceneObjectVisible = true;

  bootstrapReplacedTarget = GetDBEffect().mode != NDb::UNITSCENEOBJECTMODIFYMODE_APPEND;
  if (bootstrapReplacedTarget)
  {
    pTargetSceneObject->EnableRender(false);
  }

  Update(0.0f);
#else
	pCreature = dynamic_cast<PFClientCreature*>(pObject.GetPtr());
	NI_DATA_VERIFY(pCreature,
								 NStr::StrFmt("Effect %s could be applied on creature", GetDBEffect().GetDBID().GetFileName().c_str()), 
								 return; );

	NScene::SceneObject *pSO = pCreature->GetSceneObject();
	NI_VERIFY(pSO, NStr::StrFmt("Creature should have Scene Object", GetDBEffect().GetDBID().GetFileName().c_str()), return; );

	CVec3 const &pos = pCreature->GetPosition().pos;
  const NWorld::PFLogicObject *pLO = pCreature->WorldObject();
	int natureType = pLO->GetWorld()->GetNatureMap()->GetNatureInPoint(pos.x, pos.y);
	NI_VERIFY(0 <= natureType && natureType < 3, "Invalid nature type", return; )
	
	NDb::Ptr<NDb::DBSceneObject> pDBSceneObject = GetDBEffect().sceneObjects[natureType];
	if (!pDBSceneObject)
		pDBSceneObject = GetDBEffect().sceneObjects[0];
	NI_DATA_VERIFY(pDBSceneObject, 
		             NStr::StrFmt("Effect %s should contain scene object at least in Neutral state", GetDBEffect().GetDBID().GetFileName().c_str()), 
								 return; );

	NScene::IScene *pScene = pSO->GetScene();
	NI_ASSERT(pScene, "Scene object should present in scene")

	Reset(pSceneObjectHolder, new SingleSceneObjectHolder(pScene, pCreature->WorldObject()->GetObjectId(), pSO->GetPosition(), pDBSceneObject, pDBSceneObject->collisionGeometry, pCreature->GetNodeName()));

	if (GetDBEffect().mode == NDb::UNITSCENEOBJECTMODIFYMODE_REPLACESTATIC)
	{
		//pCreature->Show(false);
   // pLO->UpdateHiddenState(false);
    pCreature->SetVisibility(false);
	}

  Update(0.0f);
#endif
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PFUnitSceneObjectModify::Update(float timeDelta)
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  BasicEffect::Update(timeDelta);

  if (!pBootstrapSceneObject)
    return;

  const bool visible = GetDBEffect().visibilityMode == NDb::SCENEOBJECTVISIBILITYMODE_ASBUILDING || IsValid(pBootstrapTarget);
  pBootstrapSceneObject->EnableRender(visible);
  bootstrapSceneObjectVisible = visible;
#else
  NI_VERIFY(IsValid(pCreature), "Creature is invalid", return);

  bool bVisible = false;

  BasicEffect::Update(timeDelta);
  
  if (const NWorld::PFLogicObject *pLO = pCreature->WorldObject())
  {
     NDb::EFaction playerFaction = AdventureScreen::Instance()->GetPlayerFaction();
   
     bVisible = playerFaction == pLO->GetFaction() ? true : AdventureScreen::Instance()->GetClientVisibilityMap()->IsPointVisible(pLO->GetPosition().AsVec2D());
  }

  if (GetDBEffect().visibilityMode == NDb::SCENEOBJECTVISIBILITYMODE_ASOBJECT)
  {
    pSceneObjectHolder->SetVisibility(bVisible);
  }
  else
  {
    pSceneObjectHolder->SetVisibility(true);
  }
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFUnitSceneObjectModify::Die()
{
#if defined(PW_LINUX_DB_BOOTSTRAP)
  if (bootstrapReplacedTarget && IsValid(pBootstrapTarget))
  {
    if (NScene::SceneObject* const pTargetSceneObject = pBootstrapTarget->GetSceneObject())
    {
      pTargetSceneObject->EnableRender(true);
    }
  }

  if (pBootstrapSceneObject)
  {
    pBootstrapSceneObject->RemoveFromScene();
  }
  Reset(pBootstrapSceneObject);
  pBootstrapTarget = 0;
  bootstrapReplacedTarget = false;
  bootstrapSceneObjectVisible = false;

  BasicEffect::Die();
#else
	if (GetDBEffect().mode == NDb::UNITSCENEOBJECTMODIFYMODE_REPLACESTATIC)
	{
		//pCreature->Show(true);
    pCreature->SetVisibility(true);
	}
	Reset(pSceneObjectHolder);
	pCreature = 0;

  BasicEffect::Die();
#endif
}


}
