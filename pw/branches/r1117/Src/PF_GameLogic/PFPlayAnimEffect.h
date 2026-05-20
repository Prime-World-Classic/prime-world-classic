#pragma once

#include "../PF_Core/BasicEffect.h"
#include "../PF_Core/EffectsPool.h"
#include "DBPFEffect.h"

namespace NScene { _interface IScene; }

namespace NGameX
{

#if !defined(PW_LINUX_DB_BOOTSTRAP)
struct IAnimatedClientObject;
class PFClientBaseUnit;
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PFPlayAnimEffect : public PF_Core::EffectDBLinker<NDb::PlayAnimationEffect>
{
  OBJECT_BASIC_METHODS( PFPlayAnimEffect )

public:
	PFPlayAnimEffect()
#if defined(PW_LINUX_DB_BOOTSTRAP)
    : bootstrapApplied(false)
    , bootstrapReturned(false)
    , bootstrapSceneUpdated(false)
#else
    : pOwner(0)
    , pAnimated(0)
    , returnStateId(0)
#endif
  {}

	PFPlayAnimEffect(const NDb::EffectBase &dbEffect)
    : EffectBase(dbEffect)
#if defined(PW_LINUX_DB_BOOTSTRAP)
    , bootstrapApplied(false)
    , bootstrapReturned(false)
    , bootstrapSceneUpdated(false)
#else
    , pOwner(0)
    , pAnimated(0)
    , returnStateId(0)
#endif
  {}
	void Init() { EffectBase::Init(); }

	virtual void Apply(CPtr<PF_Core::ClientObjectBase> const &pUnit);

#if defined(PW_LINUX_DB_BOOTSTRAP)
  bool WasBootstrapApplied() const { return bootstrapApplied; }
  bool WasBootstrapReturned() const { return bootstrapReturned; }
  bool WasBootstrapSceneUpdated() const { return bootstrapSceneUpdated; }
#endif

protected:
	virtual void Die();
  virtual void DieImmediate();

private:
#if defined(PW_LINUX_DB_BOOTSTRAP)
  CPtr<PF_Core::ClientObjectBase> pBootstrapObject;
  bool bootstrapApplied;
  bool bootstrapReturned;
  bool bootstrapSceneUpdated;
#else
  CPtr<PFClientBaseUnit>       pOwner;
	IAnimatedClientObject        *pAnimated;
	unsigned int                 returnStateId;
#endif
};

}
