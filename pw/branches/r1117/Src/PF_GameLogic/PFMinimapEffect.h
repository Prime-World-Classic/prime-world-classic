#pragma once

#include "../PF_Core/BasicEffect.h"
#include "../PF_Core/EffectsPool.h"
#include "DBPFEffect.h"

namespace NGameX
{
#if !defined(PW_LINUX_DB_BOOTSTRAP)
  class PFClientLogicObject;
#endif

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  class PFMinimapEffect : public PF_Core::EffectDBLinker<NDb::MinimapEffect>
  {
    OBJECT_METHODS( 0xE78B9480, PFMinimapEffect )
  public:
    PFMinimapEffect( const NDb::EffectBase& dbEffect)
      : EffectBase( dbEffect )
      , index(-1)
#if defined(PW_LINUX_DB_BOOTSTRAP)
      , bootstrapActive(false)
      , bootstrapUpdateCount(0)
#endif
    { }

    virtual void Apply(CPtr<PF_Core::ClientObjectBase> const &pObject);
    virtual void Update(float timeDelta);
#if defined(PW_LINUX_DB_BOOTSTRAP)
    bool IsBootstrapActive() const { return bootstrapActive; }
    size_t GetBootstrapUpdateCount() const { return bootstrapUpdateCount; }
#endif
  protected:
    virtual void Die();

  private:
    PFMinimapEffect()
      : index(-1)
#if defined(PW_LINUX_DB_BOOTSTRAP)
      , bootstrapActive(false)
      , bootstrapUpdateCount(0)
#endif
    {};

#if defined(PW_LINUX_DB_BOOTSTRAP)
    CPtr<PF_Core::ClientObjectBase> pObject;
    CVec3 lastPosition;
    bool bootstrapActive;
    size_t bootstrapUpdateCount;
#else
    CPtr<PFClientLogicObject> pObject;
#endif
    int index;
  };

}
