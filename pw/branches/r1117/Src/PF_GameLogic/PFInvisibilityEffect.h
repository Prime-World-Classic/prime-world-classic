#pragma once

#include "DBPFEffect.h"
#include "../PF_Core/ScaleColorEffect.h"

namespace PF_Core
{
  class ClientObjectBase;
  class ColorModificationChannel;
}

namespace NGameX
{

class PFClientLogicObject;

class InvisibilityEffect : public PF_Core::EffectDBLinker<NDb::InvisibilityEffect, PF_Core::ScaleColorEffect>
{
  OBJECT_METHODS(0xB7323C1, InvisibilityEffect);
public:
  typedef PF_Core::EffectDBLinker<NDb::InvisibilityEffect, PF_Core::ScaleColorEffect> EffectLinkerBase;

  InvisibilityEffect(const NDb::EffectBase &dbEffect)
    : EffectLinkerBase(dbEffect)
  {}
  InvisibilityEffect() {};

  virtual void Apply(CPtr<PF_Core::ClientObjectBase> const &pObject);
protected:
  virtual void Apply(float t, bool);

#if defined(PW_LINUX_DB_BOOTSTRAP)
  CPtr<PF_Core::ClientObjectBase> pClientLogicObject;
#else
  CPtr<NGameX::PFClientLogicObject> pClientLogicObject;
#endif
};

}
