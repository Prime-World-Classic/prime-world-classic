#include "stdafx.h"

#if defined(PW_LINUX_DB_BOOTSTRAP)

#include "../PF_Core/BasicEffect.h"
#include "../PF_Core/EffectsPool.h"
#include "../PF_Core/SpectatorEffectsPool.h"
#include "../Scene/SceneObject.h"
#include "PFClientApplicators.h"
#include "PFBaseUnit.h"
#include "PFClientObjectBase.h"
#include "PFLogicObject.h"
#include "DBPFEffect.h"

namespace NGameX
{

namespace
{
const string& GetBootstrapSkinId(const NWorld::PFBaseUnit* pSender)
{
  static const string emptySkinId;
  return pSender ? pSender->GetSkinId() : emptySkinId;
}

PF_Core::ClientObjectBase* GetBootstrapClient(NWorld::PFLogicObject* pObject)
{
  if (!pObject || !pObject->ClientObject())
    return 0;

  // The bootstrap path keeps PFClientLogicObject forward-declared to avoid the VISUAL_CUTTED audit stubs.
  NWorld::PFClientObjectBase* pClient = reinterpret_cast<NWorld::PFClientObjectBase*>(pObject->ClientObject());
  return static_cast<PF_Core::ClientObjectBase*>(pClient);
}

NScene::IScene* GetBootstrapScene(NWorld::PFLogicObject* pObject)
{
  PF_Core::ClientObjectBase* pClient = GetBootstrapClient(pObject);
  NScene::SceneObject* pSceneObject = pClient ? pClient->GetSceneObject() : 0;
  return pSceneObject ? pSceneObject->GetScene() : 0;
}

const NDb::EffectBase* SelectBootstrapStandaloneEffect(
  const NDb::EffectBase* pDispatchEffect,
  const NWorld::PFBaseUnit* pSender,
  NDb::Ptr<NDb::EffectBase>* pVisible,
  NDb::Ptr<NDb::EffectBase>* pInvisible,
  int* pCurrent
)
{
  if (!pDispatchEffect || pDispatchEffect->GetObjectTypeID() != NDb::EffectSwitcher::typeId)
  {
    return pDispatchEffect;
  }

  const NDb::EffectSwitcher* pSwitcher = static_cast<const NDb::EffectSwitcher*>(pDispatchEffect);
  if (pVisible)
    *pVisible = pSwitcher->effectOnVisible;
  if (pInvisible)
    *pInvisible = pSwitcher->effectOnInvisible;

  const bool preferVisible =
    !pSender ||
    pSender->GetFaction() == NDb::FACTION_FREEZE ||
    !IsValid(pSwitcher->effectOnInvisible);
  const NDb::Ptr<NDb::EffectBase>& selected =
    (preferVisible && IsValid(pSwitcher->effectOnVisible)) ? pSwitcher->effectOnVisible : pSwitcher->effectOnInvisible;
  if (pCurrent)
    *pCurrent = (&selected == &pSwitcher->effectOnVisible) ? 0 : 1;

  return selected.GetPtr();
}

bool PrepareStandaloneBootstrapEffect(
  CObj<PF_Core::BasicEffectStandalone>& pEffect,
  const NDb::EffectBase* pDispatchEffect,
  const NWorld::PFBaseUnit* pSender,
  PF_Core::IEffectEnableConditionCallback* pEnableCallback
)
{
  if (!pDispatchEffect)
    return false;

  NDb::Ptr<NDb::EffectBase> effectPtr(pDispatchEffect);
  effectPtr.ChangeState(GetBootstrapSkinId(pSender));

  PF_Core::EffectsPool* pEffectsPool = PF_Core::EffectsPool::Get();
  if (!pEffectsPool)
    return false;

  pEffect = pEffectsPool->RetrieveKnownEffect<PF_Core::BasicEffectStandalone>(effectPtr);
  if (pEffect)
    pEffect->SetEnableCallback(pEnableCallback);

  return pEffect != 0;
}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CreateEffect(CObj<PF_Core::BasicEffect>& pEffect, const NDb::Ptr<NDb::EffectBase>& effect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pTarget, NWorld::PFLogicObject* pOrigin, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  PrepareEffect(pEffect, effect);
  ApplyEffect(pEffect, pSender, pTarget, pOrigin, pEnableCallback);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PrepareEffect( CObj<PF_Core::BasicEffect>& pEffect, const NDb::Ptr<NDb::EffectBase>& effect )
{
  pEffect = 0;
  PF_Core::EffectsPool* pEffectsPool = PF_Core::EffectsPool::Get();
  if (!pEffectsPool || !effect)
    return;

  pEffect = pEffectsPool->Retrieve(effect);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ApplyEffect( CObj<PF_Core::BasicEffect> pEffect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pTarget, NWorld::PFLogicObject* pOrigin, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  PF_Core::ClientObjectBase* pTargetClient = GetBootstrapClient(pTarget);
  if (!pEffect || !pTargetClient)
    return;

  pEffect->SetEnableCallback(pEnableCallback);

  PF_Core::LightningEffect* pLightning = dynamic_cast<PF_Core::LightningEffect*>(pEffect.GetPtr());
  if (pLightning)
  {
    PF_Core::ClientObjectBase* pOriginClient = GetBootstrapClient(pOrigin);
    pLightning->Apply(
      CPtr<PF_Core::ClientObjectBase>(pOriginClient ? pOriginClient : pTargetClient),
      CPtr<PF_Core::ClientObjectBase>(pTargetClient)
    );
    return;
  }

  pEffect->SetActiveStateName(GetBootstrapSkinId(pSender));
  pEffect->Apply(CPtr<PF_Core::ClientObjectBase>(pTargetClient));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void RestoreEffect(CObj<PF_Core::BasicEffect> pEffect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pTarget, NWorld::PFLogicObject* pOrigin, const Placement &placementOnStart, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  if (!pEffect)
    return;

  pEffect->SetEnableCallback(pEnableCallback);

  PF_Core::LightningEffect* pLightning = dynamic_cast<PF_Core::LightningEffect*>(pEffect.GetPtr());
  if (pLightning)
  {
    PF_Core::ClientObjectBase* pOriginClient = GetBootstrapClient(pOrigin);
    PF_Core::ClientObjectBase* pTargetClient = GetBootstrapClient(pTarget);
    if (pTargetClient)
    {
      pLightning->Apply(
        CPtr<PF_Core::ClientObjectBase>(pOriginClient ? pOriginClient : pTargetClient),
        CPtr<PF_Core::ClientObjectBase>(pTargetClient)
      );
    }
  }
  else if (PF_Core::BasicEffectStandalone* pStandalone = dynamic_cast<PF_Core::BasicEffectStandalone*>(pEffect.GetPtr()))
  {
    pStandalone->SetPosition(placementOnStart);
    pStandalone->AddToScene(GetBootstrapScene(pTarget));
  }
  else
  {
    ApplyEffect(pEffect, pSender, pTarget, pOrigin, pEnableCallback);
  }

  pEffect->Update(100.0f);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void KillEffect(CObj<PF_Core::BasicEffect> &pEffect, bool waitTillAnimationFinish)
{
  if (!IsValid(pEffect))
    return;

  if (waitTillAnimationFinish)
  {
    pEffect->SetDeathType(NDb::EFFECTDEATHTYPE_ANIM);
    return;
  }

  pEffect->Die();
  pEffect = 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PlayEffect( NWorld::PFLogicObject* pUnit, const NDb::Ptr<NDb::EffectBase>& effect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pOrigin, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  CObj<PF_Core::BasicEffect> pEffect;
  CreateEffect(pEffect, effect, pSender, pUnit, pOrigin, pEnableCallback);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CObj<PF_Core::LightningEffect> CreateLightningEffect( const NDb::Ptr<NDb::LightningEffect>& pEffect, const string& skinId, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  CObj<PF_Core::LightningEffect> pLightningEffect;
  PF_Core::EffectsPool* pEffectsPool = PF_Core::EffectsPool::Get();
  if (!pEffectsPool || !pEffect)
    return pLightningEffect;

  pEffect.ChangeState(skinId);
  pLightningEffect = pEffectsPool->Retrieve<PF_Core::LightningEffect>(pEffect);
  if (pLightningEffect)
    pLightningEffect->SetEnableCallback(pEnableCallback);

  return pLightningEffect;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void RetrieveStandaloneEffectCollection(StandaloneEffectsVector *effectList, NDb::EffectBase const *_dispatchEffect, NWorld::PFBaseUnit const* pSender, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  if (!effectList || !_dispatchEffect)
    return;

  if (_dispatchEffect->GetObjectTypeID() == NDb::EffectList::typeId)
  {
    const NDb::EffectList* pEffectList = static_cast<const NDb::EffectList*>(_dispatchEffect);
    for (int i = 0; i < pEffectList->effects.size(); ++i)
    {
      RetrieveStandaloneEffect(effectList, pEffectList->effects[i], pSender, pEnableCallback);
    }
    return;
  }

  RetrieveStandaloneEffect(effectList, _dispatchEffect, pSender, pEnableCallback);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CObj<PF_Core::BasicEffectStandalone> RetrieveStandaloneEffect( NDb::EffectBase const *_dispatchEffect, NWorld::PFBaseUnit const* pSender, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  CObj<PF_Core::BasicEffectStandalone> effect;
  if (!_dispatchEffect)
    return effect;

  NDb::Ptr<NDb::EffectBase> visibleEffect;
  NDb::Ptr<NDb::EffectBase> invisibleEffect;
  int currentEffect = 0;
  const NDb::EffectBase* pSelectedEffect =
    SelectBootstrapStandaloneEffect(_dispatchEffect, pSender, &visibleEffect, &invisibleEffect, &currentEffect);

  if (!PrepareStandaloneBootstrapEffect(effect, pSelectedEffect, pSender, pEnableCallback))
    return effect;

  PF_Core::SpectatorEffectsPool* pSpectatorEffectsPool = PF_Core::SpectatorEffectsPool::Get();
  if (pSpectatorEffectsPool && _dispatchEffect->GetObjectTypeID() == NDb::EffectSwitcher::typeId)
  {
    pSpectatorEffectsPool->RegisterBasicEffectStandalone(
      effect.GetPtr(),
      visibleEffect,
      invisibleEffect,
      currentEffect
    );
  }

  return effect;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void RetrieveStandaloneEffect(StandaloneEffectsVector* effectVector, NDb::EffectBase const *_dispatchEffect, NWorld::PFBaseUnit const* pSender, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  if (!effectVector || !_dispatchEffect)
    return;

  if (_dispatchEffect->GetObjectTypeID() == NDb::EffectList::typeId)
  {
    RetrieveStandaloneEffectCollection(effectVector, _dispatchEffect, pSender, pEnableCallback);
    return;
  }

  CObj<PF_Core::BasicEffectStandalone> effect =
    RetrieveStandaloneEffect(_dispatchEffect, pSender, pEnableCallback);
  if (effect)
  {
    CObj<PF_Core::BasicEffectStandalone>& slot = effectVector->push_back();
    slot = effect;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

#else

#include "../PF_Core/EffectsPool.h"
#include "../PF_Core/SpectatorEffectsPool.h"
#include "PFClientLogicObject.h"
#include "PFClientBaseUnit.h"

#include "PFClientApplicators.h"
#include "AdventureScreen.h"
#include "PFEffectSwitcher.h"

namespace NGameX
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CreateEffect(CObj<PF_Core::BasicEffect>& pEffect, const NDb::Ptr<NDb::EffectBase>& effect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pTarget, NWorld::PFLogicObject* pOrigin, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  PrepareEffect( pEffect, effect );
  ApplyEffect(pEffect, pSender, pTarget, pOrigin, pEnableCallback );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PrepareEffect( CObj<PF_Core::BasicEffect>& pEffect, const NDb::Ptr<NDb::EffectBase>& effect )
{
  if ( !effect )
  {
    return;
  }

  pEffect = PF_Core::EffectsPool::Get()->Retrieve( effect );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool CanApplyEffect(CObj<PF_Core::BasicEffect> pEffect, NWorld::PFLogicObject* pOrigin, NWorld::PFLogicObject* pTarget )
{
  if (!pEffect)
    return false;
  if (!pEffect->GetDBDesc())
    return true;

  const NDb::EffectBase* const desc= pEffect->GetDBDesc();

  // TODO: почему именно эти эффекты?
  switch (desc->GetObjectTypeID())
  {
  case NDb::PlayAnimationEffect::typeId:
  case NDb::ScaleColorEffect::typeId:
    return true;
  default:
    break;
  }

  const bool visible =
    (pTarget->IsVisibleForFaction(AdventureScreen::Instance()->GetPlayerFaction())) ||
    (pOrigin && pOrigin->IsVisibleForFaction(AdventureScreen::Instance()->GetPlayerFaction()));

  if (visible)
    return true;

  switch (desc->GetObjectTypeID())
  {
  case NDb::EffectSwitcher::typeId:
    {
      NDb::Ptr<NDb::EffectSwitcher> native = static_cast<const NDb::EffectSwitcher*>(desc);

      if (native->effectOnInvisible && (native->effectOnInvisible->GetObjectTypeID() == NDb::BasicEffectAttached::typeId))
        return true;

      return native->isVisibleUnderWarfog;
    }
    break;
  case NDb::BasicEffectStandalone::typeId:
    {
      NDb::Ptr<NDb::BasicEffectStandalone> native = dynamic_cast<const NDb::BasicEffectStandalone*>(desc);

      return native->isVisibleUnderWarfog;
    }
    break;
  case NDb::LightningEffect::typeId:
    {
      NDb::Ptr<NDb::LightningEffect> native = static_cast<const NDb::LightningEffect*>(desc);

      return native->controlledVisibility;
    }
    break;
  case NDb::ChangeMaterialEffect::typeId:
  case NDb::EffectList::typeId:
  case NDb::EnableSCEffect::typeId:
    return true;
  default:
    if (PF_Core::BasicEffectAttached* effAtt = dynamic_cast<PF_Core::BasicEffectAttached*>(pEffect.GetPtr()))
      return true;
    break;
  }

  return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ApplyEffect( CObj<PF_Core::BasicEffect> pEffect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pTarget, NWorld::PFLogicObject* pOrigin, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  if ( !AdventureScreen::Instance()->IsSpectator() )
  {
    if( !CanApplyEffect(pEffect, pOrigin, pTarget) )
    {
      return;
    }
  }
  else
  {
    if ( !pEffect )
      return;
  }

  pEffect->SetEnableCallback(pEnableCallback);

  PF_Core::LightningEffect* pLightning = dynamic_cast<PF_Core::LightningEffect*>( pEffect.GetPtr() );
  if ( pLightning )
  {
    pLightning->Apply( pOrigin->ClientObject(), pTarget->ClientObject() );
  }
  else
  {
    pEffect->SetActiveStateName( pSender->GetSkinId() );
    pEffect->Apply( pTarget->ClientObject() );
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RestoreEffect(CObj<PF_Core::BasicEffect> pEffect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pTarget, NWorld::PFLogicObject* pOrigin, const Placement &placementOnStart, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  if( !CanApplyEffect(pEffect, pOrigin, pTarget) )
    return;

  pEffect->SetEnableCallback(pEnableCallback);

  typedef PF_Core::LightningEffect LghtEff;
  typedef PF_Core::BasicEffectStandalone StndEff;

  if ( LghtEff* pLght = dynamic_cast<LghtEff *>( pEffect.GetPtr() ) )
  {
    pLght->Apply( pOrigin->ClientObject(), pTarget->ClientObject() );
  }
  else if( StndEff* pStnd = dynamic_cast<StndEff *>( pEffect.GetPtr() ) )
  {
    pStnd->SetPosition( placementOnStart );
    pStnd->AddToScene( pTarget->GetWorld()->GetScene() );
  }
  else
  {
    pEffect->Apply( pTarget->ClientObject() );
  }

  // skip all animated changes
  pEffect->Update( 100 );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void KillEffect(CObj<PF_Core::BasicEffect> &pEffect, bool waitTillAnimationFinish)
{
	if (IsValid(pEffect))
	{
		if (waitTillAnimationFinish)
    {
      pEffect->SetDeathType(NDb::EFFECTDEATHTYPE_ANIM);
    }
    else
    {
      pEffect->Die();
      pEffect = 0;
    }
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PlayEffect( NWorld::PFLogicObject* pUnit, const NDb::Ptr<NDb::EffectBase>& effect, NWorld::PFBaseUnit* pSender, NWorld::PFLogicObject* pOrigin, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
	CObj<PF_Core::BasicEffect> pEffect;
	CreateEffect(pEffect, effect, pSender, pUnit, pOrigin, pEnableCallback);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CObj<PF_Core::LightningEffect> CreateLightningEffect( const NDb::Ptr<NDb::LightningEffect>& pEffect, const string& skinId, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  pEffect.ChangeState(skinId);
  CObj<PF_Core::LightningEffect> lightningEffect = PF_Core::EffectsPool::Get()->Retrieve<PF_Core::LightningEffect>(pEffect);

  lightningEffect->SetEnableCallback(pEnableCallback);

  return lightningEffect;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void RetrieveStandaloneEffectCollection(StandaloneEffectsVector *effectList, NDb::EffectBase const *_dispatchEffect, NWorld::PFBaseUnit const* pSender, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  NI_VERIFY( _dispatchEffect, NStr::StrFmt( "Null dbptr in RetrieveStandaloneEffect" ) , return; );

  if (_dispatchEffect->GetObjectTypeID() == NDb::EffectList::typeId)
  {
    const NDb::EffectList * _effectList = static_cast<const NDb::EffectList *>(_dispatchEffect);
    nstl::vector<NDb::Ptr<NDb::EffectBase>>::const_iterator iter = _effectList->effects.begin();

    for(;iter !=  _effectList->effects.end();++iter)
    {
      RetrieveStandaloneEffect(effectList, *iter, pSender, pEnableCallback);
    }
  }
  else
  {
    RetrieveStandaloneEffect(effectList, _dispatchEffect, pSender, pEnableCallback);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CObj<PF_Core::BasicEffectStandalone> RetrieveStandaloneEffect( NDb::EffectBase const *_dispatchEffect, NWorld::PFBaseUnit const* pSender, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  bool needRegister = false;
  int curEffect = 0;
  NDb::Ptr<NDb::EffectBase> eff1;
  NDb::Ptr<NDb::EffectBase> eff2;
  const NDb::EffectBase* dispatchEffect = _dispatchEffect;

  CObj<PF_Core::BasicEffectStandalone> effect;

  NI_VERIFY( dispatchEffect, NStr::StrFmt( "Null dbptr in RetrieveStandaloneEffect" ) , return NULL );

  if (dispatchEffect->GetObjectTypeID() == NDb::EffectSwitcher::typeId)
  {
    needRegister = true;
    NDb::Ptr<NDb::EffectSwitcher> pEffSwitcher = static_cast<NDb::EffectSwitcher const*>(dispatchEffect);
    const NDb::Ptr<NDb::EffectBase> *ppSubEffect;
    // Logic of visibility selection

    eff1 = pEffSwitcher->effectOnVisible;
    eff2 = pEffSwitcher->effectOnInvisible;

    if (AdventureScreen::Instance()->GetPlayerFaction() == pSender->GetFaction())
    {
      ppSubEffect = &pEffSwitcher->effectOnVisible;
      curEffect = 0;
    }
    else
    {
      ppSubEffect = &pEffSwitcher->effectOnInvisible;
      curEffect = 1;
    }

    NI_VERIFY( ppSubEffect && *ppSubEffect, NStr::StrFmt( "Cannot find sub-effect in switch effect \"%s\"", pEffSwitcher->GetDBID().GetId().c_str() ), return NULL );

    ppSubEffect->ChangeState(pSender->GetSkinId());
    dispatchEffect = ppSubEffect->GetPtr();
    effect = PF_Core::EffectsPool::Get()->RetrieveKnownEffect<PF_Core::BasicEffectStandalone>( dispatchEffect );
  }
  else
  {
    const NDb::Ptr<NDb::EffectBase> eff = dispatchEffect;
    eff.ChangeState(pSender->GetSkinId());
    effect = PF_Core::EffectsPool::Get()->RetrieveKnownEffect<PF_Core::BasicEffectStandalone>( eff );
  }

  if ( AdventureScreen::Instance()->IsSpectator() )
  {
    if ( needRegister )
    {
      PF_Core::SpectatorEffectsPool::Get()->RegisterBasicEffectStandalone( effect.GetPtr(), eff1, eff2, curEffect );
    }
  }

  NI_VERIFY( effect, NStr::StrFmt( "Cannot retrieve standalone effect for \"%s\"", dispatchEffect->GetDBID().GetId().c_str() ), return NULL );

  effect->SetEnableCallback(pEnableCallback);

  return effect;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void RetrieveStandaloneEffect(StandaloneEffectsVector* effectVector, NDb::EffectBase const *_dispatchEffect, NWorld::PFBaseUnit const* pSender, PF_Core::IEffectEnableConditionCallback *pEnableCallback /*= NULL*/ )
{
  if(!effectVector) return;

  bool needRegister = false;
  int curEffect = 0;
  NDb::Ptr<NDb::EffectBase> eff1;
  NDb::Ptr<NDb::EffectBase> eff2;
  const NDb::EffectBase* dispatchEffect = _dispatchEffect;

  NI_VERIFY( dispatchEffect, NStr::StrFmt( "Null dbptr in RetrieveStandaloneEffect" ) , return ; );

  if (dispatchEffect->GetObjectTypeID() == NDb::EffectSwitcher::typeId)
	{
    needRegister = true;
    NDb::Ptr<NDb::EffectSwitcher> pEffSwitcher = static_cast<NDb::EffectSwitcher const*>(dispatchEffect);
		const NDb::Ptr<NDb::EffectBase> *ppSubEffect;
		// Logic of visibility selection

    eff1 = pEffSwitcher->effectOnVisible;
    eff2 = pEffSwitcher->effectOnInvisible;

    if (AdventureScreen::Instance()->GetPlayerFaction() == pSender->GetFaction())
		{
			ppSubEffect = &pEffSwitcher->effectOnVisible;
      curEffect = 0;
		}
		else
		{
			ppSubEffect = &pEffSwitcher->effectOnInvisible;
      curEffect = 1;
		}
    
    NI_VERIFY( ppSubEffect && *ppSubEffect, NStr::StrFmt( "Cannot find sub-effect in switch effect \"%s\"", pEffSwitcher->GetDBID().GetId().c_str() ), return );

    ppSubEffect->ChangeState(pSender->GetSkinId());
    dispatchEffect = ppSubEffect->GetPtr();

    if(dispatchEffect->GetObjectTypeID() == NDb::EffectList::typeId)
    {
      const NDb::EffectList * _effectList = static_cast<const NDb::EffectList *>(dispatchEffect);
      nstl::vector<NDb::Ptr<NDb::EffectBase>>::const_iterator iter = _effectList->effects.begin();

      for(;iter !=  _effectList->effects.end();++iter)
      {
        CObj<PF_Core::BasicEffectStandalone> &effect = effectVector->push_back();
        effect = PF_Core::EffectsPool::Get()->RetrieveKnownEffect<PF_Core::BasicEffectStandalone>( *iter );
        effect->SetEnableCallback(pEnableCallback);

        NI_VERIFY( effect, NStr::StrFmt( "Cannot retrieve standalone effect for \"%s\"", dispatchEffect->GetDBID().GetId().c_str() ), return );
      }
    }
    else
    {
      CObj<PF_Core::BasicEffectStandalone> &effect = effectVector->push_back();
      effect = PF_Core::EffectsPool::Get()->RetrieveKnownEffect<PF_Core::BasicEffectStandalone>( dispatchEffect );
      effect->SetEnableCallback(pEnableCallback);

      NI_VERIFY( effect, NStr::StrFmt( "Cannot retrieve standalone effect for \"%s\"", dispatchEffect->GetDBID().GetId().c_str() ), return );

      if ( AdventureScreen::Instance()->IsSpectator() )
      {
        if ( needRegister )
        {
          PF_Core::SpectatorEffectsPool::Get()->RegisterBasicEffectStandalone( effect.GetPtr(), eff1, eff2, curEffect );
        }
      }
    }
	}
  else
  {
    const NDb::Ptr<NDb::EffectBase> eff = dispatchEffect;
    eff.ChangeState(pSender->GetSkinId());

    CObj<PF_Core::BasicEffectStandalone> &effect = effectVector->push_back();
    effect = PF_Core::EffectsPool::Get()->RetrieveKnownEffect<PF_Core::BasicEffectStandalone>( eff );
    effect->SetEnableCallback(pEnableCallback);

    NI_VERIFY( effect, NStr::StrFmt( "Cannot retrieve standalone effect for \"%s\"", dispatchEffect->GetDBID().GetId().c_str() ), return );
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

#endif
