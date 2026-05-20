#pragma once

#include "../PF_Core/BasicEffect.h"
#include "../PF_Core/EffectsPool.h"
#include "DBPFEffect.h"

namespace NGameX
{

class PFClientCreature;
class SingleSceneObjectHolder;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PFUnitSceneObjectModify : public PF_Core::EffectDBLinker<NDb::UnitSceneObjectModify>
{
  OBJECT_BASIC_METHODS( PFUnitSceneObjectModify )

public:
  PFUnitSceneObjectModify()
#if defined(PW_LINUX_DB_BOOTSTRAP)
    : bootstrapReplacedTarget(false)
    , bootstrapSceneObjectVisible(false)
#endif
  {}

	PFUnitSceneObjectModify(const NDb::EffectBase &dbEffect)
    : EffectBase(dbEffect)
#if defined(PW_LINUX_DB_BOOTSTRAP)
    , bootstrapReplacedTarget(false)
    , bootstrapSceneObjectVisible(false)
#endif
  {}

	virtual void Apply(CPtr<PF_Core::ClientObjectBase> const &pObject);

protected:
  virtual void Update(float timeDelta);

  virtual void Die();

private:
#if defined(PW_LINUX_DB_BOOTSTRAP)
  AutoPtr<NScene::SceneObject>      pBootstrapSceneObject;
  CPtr<PF_Core::ClientObjectBase>   pBootstrapTarget;
  bool                              bootstrapReplacedTarget;
  bool                              bootstrapSceneObjectVisible;
#else
	AutoPtr<SingleSceneObjectHolder> pSceneObjectHolder;
	CPtr<PFClientCreature>           pCreature;
#endif
};

}
