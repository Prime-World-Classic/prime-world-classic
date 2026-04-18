#pragma once
#include "System/Placement.h"
#include "PF_GameLogic/PFChest.h"
#include "PF_GameLogic/PFFlagpole.h"
#include "PF_GameLogic/PFUniTarget.h"
#include "PF_GameLogic/DBAnimations.h"

namespace PF_Core
{
  class BasicEffect;
  class ClientObjectBase;
  struct IEffectEnableConditionCallback;

  class LightningEffect : public CObjectBase
  {
    OBJECT_METHODS(0x6F38B2A1, LightningEffect)
  public:
    LightningEffect() {}
    void Apply(CPtr<ClientObjectBase> const&, CPtr<ClientObjectBase> const&) {}
    void SetEnableCallback(IEffectEnableConditionCallback *) {}
  };
}

namespace NWorld
{
  class PFWorldObjectBase;
  class PFBaseUnit;
  class PFBaseHero;
  class PFBaseMovingUnit;
  class PFBaseSummonedUnit;
  class PFChest;
  class PFLogicObject;
  class Target;
  enum ELookKind : int;
}

namespace NGameX
{

namespace
{
  inline const nstl::string &ClientStubEmptyNodeName()
  {
    static const nstl::string value;
    return value;
  }
}

//////////////////////////////////////////////////////////////////////////
class BaseClientObjectStub : public NWorld::PFClientObjectBase
{
public:
  BaseClientObjectStub() {}
  BaseClientObjectStub(const CPtr<PF_Core::WorldObjectBase> &pWO) : PFClientObjectBase(pWO) {}

  bool IsVisible() const { return true; }
  void SetVisibility(bool) {}
  void OnMove(const CVec3&) {}
  void Update(float) {}
  void OnDamage(float, float) {}
	void OnHide(bool) {}
  void OnAttack(float, CVec3 const&, bool) {}
  void OnAttack(float, CVec2 const&) {}
  void OnMiss() {}
  void OnMiss(CPtr<NWorld::PFBaseUnit> const&) {}
  void OnLevelUp(int level) {}
  void RenderDebug() {}
  void OnBecameLocal(bool) {}
  void OnDeath() {}
  void Show(bool b = true) {}
  void OnResurrect() {}
  void OnUseMagic(int id, NWorld::Target const& target, float timeOffset, unsigned flags) {}
  void OnUseMagic(int id, NWorld::Target const& target, float timeOffset, NWorld::ELookKind lookAtTarget) {}
  void OnAwardedForKill(CPtr<NWorld::PFBaseUnit> const&, const float) {}
	void OnAddGold(CPtr<NWorld::PFBaseUnit> const&, const float) {}
  void PlayAskSound(int) {}
  bool PlayAskSound(int, NWorld::PFBaseHero const*, int) { return false; }
  void SetRotation(CQuat const&) {}
	void Recolor(float) {}
	void Recolor(const Render::HDRColor&) {}
	void ResetColor() {}
  void ModifyColor(const Render::HDRColor&, const Render::HDRColor &) const {}
  void ModifyColor(const Render::HDRColor &) const {}
  void AttachGlowEffect() {}
  void RemoveGlowEffect() {}
  void DropTree(const CVec3&, const CPtr<NWorld::PFBaseUnit> &) {}
  void DropTree(const CVec2&, const CPtr<NWorld::PFBaseUnit> &) {}
  void RestoreTree() {}
  void ModifyOpacity(float) const {}
  void SetNatureType(int) {}
  void OnMoveTo(const CVec3&, bool /*animate*/ = true ) {}
  void OnMoveTo(const CVec2&, bool /*animate*/ = true ) {}
  void OnTeleportTo(CVec3 const&) {}
  void OnTeleportTo(CVec2 const&) {}
  void OnAttachTo(CPtr<NWorld::PFBaseHero> const&) {}
  void OnDetach() {}
  void OnStop() {}
  void SetMoveSpeed(float) {}
  void SetAttackSpeed(float) {}
  void TurnBy(CQuat const&) {}
  void OnKill(CPtr<NWorld::PFBaseUnit> const&, bool) {}
  void OnUseAbility(const char*, const char*, NWorld::Target const&, float, NWorld::ELookKind, bool) {}
  void OnMountSpecial(CPtr<NWorld::PFBaseMovingUnit> const&) {}
  void OnUnmountSpecial() {}
  void OnScrollReceived() {}
  void Perish() {}
  void Resurrect() {}
  void StartFallThrough(float) {}
  void OnEmote(NDb::EEmotion) {}
	void SetTileRegion(const CTRect<int> &) {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientLogicObject : public BaseClientObjectStub
{
public:
  struct CreatePars : public NonCopyable
  {
  public:
    CreatePars(const NDb::Unit &unit, NDb::AnimSet const* pAnimSet, NDb::AnimSet const* pAnimSitSet, NScene::IScene *pS)  {}
    CreatePars(const NDb::Unit &unit, NDb::AnimSet const* pAnimSet, NScene::IScene *pS)  {}
    CreatePars(const NDb::BaseHero &unit, NDb::AnimSet const* pAnimSet, NScene::IScene *pS, NDb::EFaction faction) {}
    CreatePars(const NDb::BaseHero &unit, NDb::AnimSet const* pBaseAnimSet, NDb::AnimSet const* pSitAnimSet, NScene::IScene *pS, NDb::EFaction faction) {}
    CreatePars(NScene::IScene*, const nstl::string&, NDb::Ptr<NDb::DBSceneObject> const&, NDb::Ptr<NDb::CollisionGeometry> const&) {}
    CreatePars(NScene::IScene*, const nstl::string&, NDb::Ptr<NDb::DBSceneObject> const&) {}
    CreatePars(NDb::Ptr<NDb::DBSceneObject> const& sceneObject, NDb::AnimSet const* pAnimSet, NScene::IScene *pS) {}
    CreatePars(NScene::IScene*, NDb::Ptr<NDb::DBSceneObject> const&, NDb::Ptr<NDb::CollisionGeometry> const&){}
    CreatePars(NScene::IScene*, NDb::Ptr<NDb::DBSceneObject> const&) {}
    CreatePars(NScene::IScene*, NDb::Ptr<NDb::GameObject> const&) {}
    CreatePars(NScene::IScene*, const nstl::string&, NDb::Ptr<NDb::GameObject> const&) {}
    CreatePars(NScene::IScene*, const char*, NDb::Ptr<NDb::GameObject> const&) {}
    CreatePars(NScene::IScene*, const nstl::string&, NDb::AdvMapObject const&) {}
    CreatePars(NScene::IScene*, const char*, NDb::AdvMapObject const&) {}
		CreatePars(NScene::IScene*, NDb::AdvMapObject const& ) {}
    CreatePars(const NDb::Unit&, NScene::IScene *pS) {}
  };
  PFClientLogicObject() {}
  PFClientLogicObject(const PF_Core::WorldObjectRef &pWO, const CreatePars &cp) : BaseClientObjectStub(pWO) {}

  //TODO: Fix build audit
  void RevalidateVisibility() {}
  void UpdateVisibility() {}
  void ClearColorModifications() {}
  const Placement& GetPosition() const { return NULLPLACEMENT; }
  float GetTimeDead() const { return 0.0f; }
};

namespace EHappyState
{
  enum Type
  {
    Ready,
    Happy,
    Resting
  };
}

//////////////////////////////////////////////////////////////////////////
class PFClientSingleStateObject : public PFClientLogicObject
{
  CLIENT_OBJECT_METHODS(0xF624CC0, PFClientSingleStateObject, NWorld::PFLogicObject);
public:
  PFClientSingleStateObject(const PF_Core::WorldObjectRef &pWO, const CreatePars &cp) : PFClientLogicObject(pWO, cp) {}
  PFClientSingleStateObject(const PF_Core::WorldObjectRef &pWO, const CreatePars &cp, const NGameX::PFClientLogicObject* unitClientObject ) : PFClientLogicObject(pWO, cp) {}
  void OnMove(const CVec3&) {}
  void RemoveCollision() {}
protected:
  explicit PFClientSingleStateObject() {}
};


//////////////////////////////////////////////////////////////////////////
class PFClientMultiStateObject : public PFClientLogicObject
{
  CLIENT_OBJECT_METHODS(0xF624DC0, PFClientMultiStateObject, NWorld::PFLogicObject);
public:
  PFClientMultiStateObject(const PF_Core::WorldObjectRef &pWO, NScene::IScene* pScene, NDb::AdvMapObject const& advMapObject) : PFClientLogicObject(pWO, CreatePars(pScene, advMapObject)) {}
  PFClientMultiStateObject(const PF_Core::WorldObjectRef &pWO, NScene::IScene* pScene, NDb::AdvMapObject const& advMapObject, int) : PFClientLogicObject(pWO, CreatePars(pScene, advMapObject)) {}
  void OnMove(const CVec3&) {}
  void SetState(int, bool) {}
  void Hide(bool) {}
  void DoFreeze() {}
  void RemoveCollision() {}
protected:
  explicit PFClientMultiStateObject() {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientChest : public PFClientSingleStateObject
{
  CLIENT_OBJECT_WORLD( PFClientChest, NWorld::PFPickupableObjectBase )
public:
  PFClientChest(const PF_Core::WorldObjectRef &, const CreatePars &) {}

  virtual void Update( float timeDiff ) {};
};

//////////////////////////////////////////////////////////////////////////
class PFClientGlyph : public PFClientSingleStateObject
{
  CLIENT_OBJECT_WORLD( PFClientGlyph, NWorld::PFPickupableObjectBase )
public:
  PFClientGlyph(const PF_Core::WorldObjectRef &pWO, const CreatePars& cp, const Render::HDRColor& color) {}
  PFClientGlyph(const PF_Core::WorldObjectRef &, const CreatePars &) {}

  virtual void Update( float timeDiff ) {};
};

//////////////////////////////////////////////////////////////////////////
class PFClientBaseUnit : public PFClientLogicObject
{
  CLIENT_OBJECT_WORLD( PFClientBaseUnit, NWorld::PFLogicObject )
public:
  PFClientBaseUnit() {}
  PFClientBaseUnit(PF_Core::WorldObjectRef pWO, const CreatePars &cp) : PFClientLogicObject(pWO, cp) {}
  EHappyState::Type GetHappyState() const { return EHappyState::Resting; }
  void SetHappyState(EHappyState::Type) {}
  void OnAttack(float, CVec2 const&) {}
  void OnUnitDie() {}
  void OnBecameIdle() {}
  void OnStartedFighting() {}
  void OnFinishedFighting() {}
  void OnAttackDispatchStarted() {}
  void OnStunned(bool) {}
  void OnFreeze(bool) {}
  void OnUnsummon() {}
  void AcknowledgeAuraChange(bool, bool) {}
  void OnSelfAuraChange(bool, bool) {}
  void SetSelfAuras(int, int) {}
  void ForceIdle() {}
  const NDb::DBSceneObject* GetSceneObjectDesc() const { return 0; }
  void SetSceneObject(const NDb::DBSceneObject*) {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientBaseMovingUnit : public PFClientBaseUnit
{
public:
  PFClientBaseMovingUnit() {}
  PFClientBaseMovingUnit(PF_Core::WorldObjectRef pWO, const CreatePars &cp) : PFClientBaseUnit(pWO, cp) {}
  void OnFarTargetChanged( CVec3 const&  ) {}
  void LookTo(CVec2 const&, bool = false, float = 0.0f) {}
  void LookTo(CVec3 const&, bool = false) {}
  void OnMoveFailed() {}
  void OnMoveFailed(const CVec2&) {}
  void OnMoveTo(const CVec2&, bool = true) {}
  void OnTeleportTo(CVec2 const&) {}
};

//////////////////////////////////////////////////////////////////////////
class PFAnimController
{
public:
  PFAnimController() {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientCreature : public PFClientBaseMovingUnit, public PFAnimController
{
  CLIENT_OBJECT_WORLD( PFClientCreature, PF_Core::WorldObjectBase )
public:
  struct CreatePars : public PFClientLogicObject::CreatePars
  {
    CreatePars(NDb::Ptr<NDb::DBSceneObject> const& sceneObject, NDb::AnimSet const* pAnimSet, NScene::IScene *pS, const nstl::string& nodeName, bool startIdleAnimation = false)
      : PFClientLogicObject::CreatePars(sceneObject, pAnimSet, pS)
    {
    }
  };

  PFClientCreature() {}
  PFClientCreature(PF_Core::WorldObjectRef const& pWO, const CreatePars &cp) {}

  void Summon() {}
  void OnAbilityDispatchStarted(const char*) {}
  float ForceAnimation(const string&) { return 0.0f; }
  void StopForcedAnimation() {}
  void Idle(bool = false) {}
  bool IsPartiallyVisible() const { return false; }
  void OnUnsummon() {}
  void ForceIdle() {}
  float GetStateDuration(NDb::EAnimStates, bool upper = false) const { return 0.0f; }
  int  ReplaceAnimSet( NDb::Ptr<NDb::AnimSet> pSet ) { return -1; }
  bool RollbackAnimSet( int setId )                  { return false; }
  bool IsInAttackNode() const { return false; }
  int  ReplaceAnimation( NDb::EAnimStates state, char const* name, char const* marker, bool upper, bool affectAllSets)  { return -1; }
  bool RollbackAnimation( NDb::EAnimStates state, int id, bool upper)                               { return false; }

	void CreateStandaloneEffect() {}
};

//////////////////////////////////////////////////////////////////////////
class PFBaseClientHero : public PFClientCreature
{
  CLIENT_OBJECT_METHODS( 0xF63FCC0, PFBaseClientHero, NWorld::PFLogicObject )
public:
  struct CreatePars : public PFClientCreature::CreatePars
  {
    CreatePars(const NDb::BaseHero &unit, NDb::AnimSet const* pAnimSet, NScene::IScene *pS, NDb::EFaction faction, const nstl::string& nodeName, const nstl::string& skinId)
      : PFClientCreature::CreatePars(SelectSceneObj(unit, faction, skinId), pAnimSet, pS, nodeName)
    {
    }

    static NDb::Ptr<NDb::DBSceneObject> const& SelectSceneObj(const NDb::BaseHero &unit, NDb::EFaction, const nstl::string& skinId)
    {
      if (!skinId.empty())
      {
        vector<NDb::Ptr<NDb::HeroSkin>>::const_iterator it = unit.heroSkins.begin();
        for (; it != unit.heroSkins.end(); ++it)
        {
          if ((*it)->persistentId == skinId)
          {
            return (*it)->sceneObject;
          }
        }
      }

      return unit.sceneObject;
    }
  };

  PFBaseClientHero() {}
  PFBaseClientHero(PF_Core::WorldObjectRef pWO, const CreatePars &cp) : PFClientCreature(pWO, cp) {}
  void UnsubscribeMinigameAfterReconnect() {}
};

class PFClientMaleHero : public PFBaseClientHero
{
  CLIENT_OBJECT_METHODS( 0x2C59BC81, PFClientMaleHero, NWorld::PFLogicObject )
public:
  explicit PFClientMaleHero() {}
  PFClientMaleHero(PF_Core::WorldObjectRef pWO, const CreatePars &cp) : PFBaseClientHero(pWO, cp) {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientFlagpole : public PFClientBaseUnit
{
  CLIENT_OBJECT_METHODS(0xF63FCC0, PFClientFlagpole, PF_Core::WorldObjectBase)
public:
  PFClientFlagpole() {}
  PFClientFlagpole(PF_Core::WorldObjectRef pWO, const NDb::AdvMapObject& mapObject, NScene::IScene* pScene)
    : PFClientBaseUnit(pWO, CreatePars(pScene, mapObject))
  {
  }

  void OnRaiseFlag(int) {}
  void OnStartRaiseFlag() {}
  void OnDropFlag() {}
  void OnAfterReset(bool, int) {}
  void Hide(bool) {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientPriestess : public PFClientCreature
{
  CLIENT_OBJECT_METHODS( 0xC566BC80, PFClientPriestess, NWorld::PFLogicObject )
public:
  PFClientPriestess() {}
  PFClientPriestess(PF_Core::WorldObjectRef pWO, const CreatePars &cp) : PFClientCreature(pWO, cp) {}
  void OnMount( CPtr<NWorld::PFBaseHero>  const& pHero ) {}
  void OnUnmount( ) {}
};

//////////////////////////////////////////////////////////////////////////
class PFBuilding : public PFClientBaseUnit
{
  CLIENT_OBJECT_METHODS( 0xF63FC40, PFBuilding, NWorld::PFLogicObject );
public:
  PFBuilding() {}
  PFBuilding(PF_Core::WorldObjectRef pWO, const NDb::AdvMapObject &mapObject, NScene::IScene* pScene)
    : PFClientBaseUnit(pWO, CreatePars(pScene, ClientStubEmptyNodeName(), mapObject.gameObject)) {}
  PFBuilding(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp) : PFClientBaseUnit(pWO, cp) {}
  PFBuilding(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp, NScene::IScene*, const NDb::Building*) : PFClientBaseUnit(pWO, cp) {}
  PFBuilding(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp, NScene::IScene*, const NDb::GameObject*) : PFClientBaseUnit(pWO, cp) {}
  PFBuilding(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp, NScene::IScene*, const NDb::Fountain*) : PFClientBaseUnit(pWO, cp) {}
  void Hide(bool) {}
  void OnHeal(float, float) {}
  void SetAnimState(const nstl::string&, bool) {}
  void MakeRuined() {}
  void ApplyHealthEffects() {}
  void ShowFragEffect() {}
};

//////////////////////////////////////////////////////////////////////////
class PFBattleBuilding : public PFBuilding
{
  CLIENT_OBJECT_METHODS( 0xF63FC40, PFBattleBuilding, NWorld::PFLogicObject );
public:
  PFBattleBuilding() {}
  PFBattleBuilding(PF_Core::WorldObjectRef pWO, const NDb::AdvMapObject &mapObject, NScene::IScene* pScene) : PFBuilding(pWO, mapObject, pScene) {}
  PFBattleBuilding(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp) : PFBuilding(pWO, cp) {}
  void OnRotationChanged(float, bool = false) {}
};

//////////////////////////////////////////////////////////////////////////
class PFCreep : public PFClientCreature
{
  CLIENT_OBJECT_METHODS( 0xF63FC41, PFCreep, NWorld::PFLogicObject );
public:
  PFCreep() {}
  PFCreep(const PF_Core::WorldObjectRef &pWO, const CreatePars &cp) : PFClientCreature(pWO, cp) {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientSummoned : public PFCreep
{
  CLIENT_OBJECT_METHODS( 0xB199CB00, PFClientSummoned, PF_Core::WorldObjectBase )
public:
  struct CreatePars : public PFCreep::CreatePars
  {
    CreatePars(const NDb::Summoned &unit, NDb::AnimSet const* pAnimSet, NScene::IScene *pS, NDb::EFaction, const nstl::string& nodeName, bool startIdleAnimation, const nstl::string& skinId)
      : PFCreep::CreatePars(SelectSceneObj(unit, skinId), pAnimSet, pS, nodeName, startIdleAnimation)
    {
    }

    static NDb::Ptr<NDb::DBSceneObject> const& SelectSceneObj(const NDb::Summoned &unit, const nstl::string& skinId)
    {
      if (!skinId.empty() && skinId != "default")
      {
        vector<NDb::Ptr<NDb::CreepSkin>>::const_iterator it = unit.creepSkins.begin();
        for (; it != unit.creepSkins.end(); ++it)
        {
          if ((*it)->heroPersistentId == skinId)
          {
            return (*it)->sceneObject;
          }
        }
      }

      return unit.sceneObject;
    }
  };

  PFClientSummoned() {}
  PFClientSummoned(const PF_Core::WorldObjectRef &pWO, const CreatePars &cp) : PFCreep(pWO, cp) {}

  static void PrepareExcludedResourcesList(const NDb::Summoned* pDBPtr, const nstl::string& summonerSkinId, nstl::list<const NDb::DbResource*> &excludes)
  {
    if (pDBPtr && !summonerSkinId.empty() && summonerSkinId != "default")
    {
      vector<NDb::Ptr<NDb::CreepSkin>>::const_iterator it = pDBPtr->creepSkins.begin();
      for (; it != pDBPtr->creepSkins.end(); ++it)
      {
        if ((*it)->heroPersistentId != summonerSkinId)
        {
          excludes.push_back(*it);
        }
      }
    }
  }
};

//////////////////////////////////////////////////////////////////////////
class PFClientTree : public PFClientLogicObject
{
  CLIENT_OBJECT_METHODS( 0xF63FC80, PFClientTree, NWorld::PFLogicObject );
public:
  PFClientTree() {}
  PFClientTree(const PF_Core::WorldObjectRef &pWO, const NDb::AdvMapObject &mapObject, NScene::IScene* pScene_)
    : PFClientLogicObject(pWO, CreatePars(pScene_, ClientStubEmptyNodeName(), mapObject.gameObject)) {}
  PFClientTree(const PF_Core::WorldObjectRef &pWO, const NDb::AdvMapObject &mapObject, NScene::IScene* pScene_, int)
    : PFClientLogicObject(pWO, CreatePars(pScene_, ClientStubEmptyNodeName(), mapObject.gameObject)) {}
  void Switch2StubImmediately() {}
  void SetState(int, bool) {}
  void Hide(bool) {}
  void DropTree(const CVec2&, const CPtr<NWorld::PFBaseUnit> &) {}
};

//////////////////////////////////////////////////////////////////////////
class PFTower : public PFBuilding
{
  CLIENT_OBJECT_METHODS( 0xF63FC81, PFTower, NWorld::PFLogicObject )
public:
  PFTower() {}
  PFTower(PF_Core::WorldObjectRef pWO, const NDb::AdvMapObject &mapObject, NScene::IScene* pScene) : PFBuilding(pWO, mapObject, pScene) {}
  PFTower(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp, NScene::IScene*, const NDb::Tower*) : PFBuilding(pWO, cp) {}

  void OnRotationChanged( float angle, bool forced = false ) {}
  void OnTargetAssigned() {}
  void OnTargetDropped() {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientMainBuilding : public PFBattleBuilding
{
  CLIENT_OBJECT_METHODS(0x2C6CC480, PFClientMainBuilding, PF_Core::WorldObjectBase)
public:
  explicit PFClientMainBuilding() {}
  PFClientMainBuilding(PF_Core::WorldObjectRef pWO, const PFClientLogicObject::CreatePars &cp, NScene::IScene*, const NDb::GameObject*)
    : PFBattleBuilding(pWO, cp)
  {
  }

  void OnActivate(bool) {}
  void OnActivated(bool) {}
  void OnAttack(float, CVec2 const&) {}
  void OnUnitDie() {}
  void Update(float) {}
};

//////////////////////////////////////////////////////////////////////////
//class PFClientMinigamePlace : public PFBuilding
//{
//  CLIENT_OBJECT_METHODS( 0xF63FCC1, PFClientMinigamePlace, NWorld::PFLogicObject )
//public:
//  PFClientMinigamePlace() {}
//  PFClientMinigamePlace(PF_Core::WorldObjectRef pWO, const NDb::AdvMapObject &mapObject, NScene::IScene* pScene) : PFBuilding(pWO, mapObject, pScene) {}
//};

//////////////////////////////////////////////////////////////////////////
class PFClientNatureMap : public NWorld::PFClientObjectBase
{
  CLIENT_OBJECT_METHODS(0xF63FCC2, PFClientNatureMap, NWorld::PFWorldObjectBase )
public:
  explicit PFClientNatureMap() {}
  PFClientNatureMap(const PF_Core::WorldObjectRef &pWO) : PFClientObjectBase(pWO) {}
  void OnCreate(int texSideX, int texSideY) {}
  void OnStep() {}
  void SwitchDebugDrawLines() {}
};

//////////////////////////////////////////////////////////////////////////
class VisibilityMapClient : public NWorld::PFClientObjectBase
{
  CLIENT_OBJECT_METHODS( 0xF63FCC2, VisibilityMapClient, PF_Core::WorldObjectBase )
public:
  VisibilityMapClient() {}
  VisibilityMapClient(const PF_Core::WorldObjectRef &pWO) : PFClientObjectBase(pWO) {}
  void OnCreate(int texSideX, int texSideY, const CVec3 &worldSize, Render::HDRColor &color) {}
  void OnStep() {}
};

//////////////////////////////////////////////////////////////////////////
class PFClientSimpleObject : public NWorld::PFClientObjectBase
{
  CLIENT_OBJECT_METHODS( 0xF640B00, PFClientSimpleObject, PF_Core::WorldObjectBase )
public:
  explicit PFClientSimpleObject() {}
  PFClientSimpleObject(PF_Core::WorldObjectRef pWO, NDb::AdvMapObject const& dbObject, NScene::IScene* pScene) : PFClientObjectBase(pWO) {}
  void Hide(bool) {}
  void DoFreeze() {}
};

//////////////////////////////////////////////////////////////////////////
class PFDispatchUniformLinearMove : public NWorld::PFClientObjectBase
{
  CLIENT_OBJECT_METHODS( 0xF640B01, PFDispatchUniformLinearMove, PF_Core::WorldObjectBase );
public:
  PFDispatchUniformLinearMove() : bStarted(false) {}
  PFDispatchUniformLinearMove(PF_Core::WorldObjectRef pWO, NScene::IScene *pScene, bool bAlignToAxis)
    : NWorld::PFClientObjectBase(pWO), bStarted(false) {}
  PFDispatchUniformLinearMove(PF_Core::WorldObjectRef pWO, NScene::IScene *pScene, bool bAlignToAxis, float startDelay)
    : NWorld::PFClientObjectBase(pWO), bStarted(startDelay <= 0.0f) {}
  virtual void OnWorldObjectDie() { NWorld::PFClientObjectBase::OnWorldObjectDie(); }
  virtual void Update(float timeDelta) {}
  virtual void UpdateAfterScene(float timeDelta) { bStarted = true; }
  void Start() { bStarted = true; }
  bool IsStarted() const { return bStarted; }
  virtual void OnArrive() {}
private:
  bool bStarted;
};

//////////////////////////////////////////////////////////////////////////
class PFDispatchByTime : public NWorld::PFClientObjectBase
{
  CLIENT_OBJECT_METHODS( 0xF640B02, PFDispatchByTime, PF_Core::WorldObjectBase );
public:
  PFDispatchByTime() {}
  PFDispatchByTime(PF_Core::WorldObjectRef pWO, NScene::IScene *pScene) : NWorld::PFClientObjectBase(pWO) {}
  void Start() {}
  virtual void OnWorldObjectDie() { NWorld::PFClientObjectBase::OnWorldObjectDie(); }
};

//////////////////////////////////////////////////////////////////////////
class PFDispatchWithLink : public PFDispatchUniformLinearMove
{
  CLIENT_OBJECT_METHODS( 0xF640B03, PFDispatchWithLink, PF_Core::WorldObjectBase );
public:
  PFDispatchWithLink() {}
  PFDispatchWithLink(PF_Core::WorldObjectRef pWO, NScene::IScene *pScene, bool bAlignToAxis, float startDelay)
    : PFDispatchUniformLinearMove(pWO, pScene, bAlignToAxis, startDelay) {}
};

//////////////////////////////////////////////////////////////////////////
class PFDispatchRockmanMace : public PFDispatchWithLink
{
  CLIENT_OBJECT_METHODS( 0xF640B04, PFDispatchRockmanMace, PF_Core::WorldObjectBase );
public:
  PFDispatchRockmanMace() {}
  PFDispatchRockmanMace(PF_Core::WorldObjectRef pWO, NScene::IScene *pScene, bool bAlignToAxis, float startDelay)
    : PFDispatchWithLink(pWO, pScene, bAlignToAxis, startDelay) {}
};

//////////////////////////////////////////////////////////////////////////
inline void PFDispatchPlayApplyEffect(CPtr<NWorld::PFDispatch>, NWorld::PFWorld*, NWorld::Target const&) {}

inline void CreateEffect(CObj<PF_Core::BasicEffect> &pEffect, const NDb::Ptr<NDb::EffectBase> effect, CPtr<NWorld::PFBaseUnit> pSender, CPtr<NWorld::PFBaseUnit> pTarget) {}
inline void PrepareEffect(CObj<PF_Core::BasicEffect> &, const NDb::Ptr<NDb::EffectBase> &) {}
inline void ApplyEffect(CObj<PF_Core::BasicEffect>, NWorld::PFBaseUnit *, NWorld::PFLogicObject *, NWorld::PFLogicObject *, PF_Core::IEffectEnableConditionCallback * = NULL) {}
inline void RestoreEffect(CObj<PF_Core::BasicEffect>, NWorld::PFBaseUnit *, NWorld::PFLogicObject *, NWorld::PFLogicObject *, const Placement &, PF_Core::IEffectEnableConditionCallback * = NULL) {}
inline void CreateEffect(CObj<PF_Core::BasicEffect> &, const NDb::Ptr<NDb::EffectBase> &, NWorld::PFBaseUnit *, NWorld::PFLogicObject *, NWorld::PFLogicObject *, PF_Core::IEffectEnableConditionCallback * = NULL) {}
inline void PlayEffect(CPtr<NWorld::PFBaseUnit> , const NDb::Ptr<NDb::EffectBase>, CPtr<NWorld::PFBaseUnit> , const char *locatorName = 0 ) {}
inline void PlayEffect(NWorld::PFLogicObject *, const NDb::Ptr<NDb::EffectBase> &, NWorld::PFBaseUnit *, NWorld::PFLogicObject *, PF_Core::IEffectEnableConditionCallback * = NULL) {}
inline void KillEffect(CObj<PF_Core::BasicEffect> &, bool waitTillAnimationFinish = false) {}

inline CObj<PF_Core::LightningEffect> CreateLightningEffect(const NDb::Ptr<NDb::LightningEffect> pEffect)
{
  return pEffect ? CObj<PF_Core::LightningEffect>(new PF_Core::LightningEffect()) : CObj<PF_Core::LightningEffect>(NULL);
}

inline CObj<PF_Core::LightningEffect> CreateLightningEffect(
  const NDb::Ptr<NDb::LightningEffect> pEffect,
  const string& skinId,
  PF_Core::IEffectEnableConditionCallback *pEnableCallback = NULL)
{
  CObj<PF_Core::LightningEffect> effect = CreateLightningEffect(pEffect);
  if (IsValid(effect))
    effect->SetEnableCallback(pEnableCallback);
  return effect;
}

} //namespace NGameX
