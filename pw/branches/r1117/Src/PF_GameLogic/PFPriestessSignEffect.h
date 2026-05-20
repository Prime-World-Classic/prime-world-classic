#pragma once

#include "../PF_Core/ControlledStatusEffect.h"
#include "DBPFEffect.h"

#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "PFCreature.h"
#include "PFClientCreature.h"
#endif

namespace NGameX
{

class PriestessSignEffect : public PF_Core::EffectDBLinker<NDb::PriestessSignEffect, PF_Core::BasicEffectStandalone>
{
  OBJECT_METHODS( 0xA06CEC01, PriestessSignEffect )
public:
  enum State
  {
    STATE_IDLE,
    STATE_WAITDEATHEFFECT,
    STATE_FLYIN,
    STATE_FLYOUT
  };

  PriestessSignEffect(const NDb::EffectBase &dbEffect);
  PriestessSignEffect()
    : state(STATE_IDLE)
    , countdown(0.0f)
#if defined(PW_LINUX_DB_BOOTSTRAP)
    , bootstrapInitialized(false)
    , bootstrapApplied(false)
    , bootstrapTargetInScene(false)
    , bootstrapSoulEffectDbReady(false)
    , bootstrapSoulStarted(false)
    , bootstrapFlyInStarted(false)
    , bootstrapFlyOutStarted(false)
    , bootstrapCompleted(false)
    , bootstrapUpdateCount(0)
#endif
  {}

  virtual void Init();
  virtual void Apply(CPtr<PF_Core::ClientObjectBase> const &pUnit);
  virtual void Update(float timeDelta);
  virtual void DieImmediate();
  virtual bool Ready2Die();

#if defined(PW_LINUX_DB_BOOTSTRAP)
  bool WasBootstrapInitialized() const { return bootstrapInitialized; }
  bool WasBootstrapApplied() const { return bootstrapApplied; }
  bool WasBootstrapTargetInScene() const { return bootstrapTargetInScene; }
  bool WasBootstrapSoulEffectDbReady() const { return bootstrapSoulEffectDbReady; }
  bool WasBootstrapSoulStarted() const { return bootstrapSoulStarted; }
  bool WasBootstrapFlyInStarted() const { return bootstrapFlyInStarted; }
  bool WasBootstrapFlyOutStarted() const { return bootstrapFlyOutStarted; }
  bool WasBootstrapCompleted() const { return bootstrapCompleted; }
  size_t GetBootstrapUpdateCount() const { return bootstrapUpdateCount; }
#endif

protected:
#if !defined(PW_LINUX_DB_BOOTSTRAP)
  bool HasTargetCreatureStartedDeathEffect();
  CVec3 GetRelativeSoulSphereDestinationPos();
  CVec3 GetRelativeSoulSpherePos();
  CVec3 PlaceSoulSphereIntoPos(CVec3 const &pos);
  void InitFlyIn();
#endif


  State  state;
  float  countdown;

#if defined(PW_LINUX_DB_BOOTSTRAP)
  CPtr<PF_Core::ClientObjectBase> pBootstrapTarget;
  bool  bootstrapInitialized;
  bool  bootstrapApplied;
  bool  bootstrapTargetInScene;
  bool  bootstrapSoulEffectDbReady;
  bool  bootstrapSoulStarted;
  bool  bootstrapFlyInStarted;
  bool  bootstrapFlyOutStarted;
  bool  bootstrapCompleted;
  size_t bootstrapUpdateCount;
#else
  CPtr<NGameX::PFClientCreature> pTargetCreature;
  CObj<PF_Core::BasicEffectStandalone> pSoulEffect;
  NScene::AnimatedPlacement  flyInPos;
  Placement scPosBackup;
  Placement flyInAdj;
#endif
};

}
