#pragma once

#include "../PF_Core/BasicEffect.h"
#include "../PF_Core/EffectsPool.h"
#include "DBPFEffect.h"

namespace NScene { _interface IScene; }

namespace NGameX
{

class PFClientBaseUnit;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PFAuraEffect : public PF_Core::EffectDBLinker<NDb::AuraEffect>
{
	OBJECT_BASIC_METHODS( PFAuraEffect )

public:
	PFAuraEffect()
#if defined(PW_LINUX_DB_BOOTSTRAP)
		: bootstrapAuraActive(false)
		, bootstrapAuraAlly(false)
		, bootstrapAuraChangeCount(0)
#endif
	{}

	PFAuraEffect(const NDb::EffectBase &dbEffect)
		: EffectBase(dbEffect)
#if defined(PW_LINUX_DB_BOOTSTRAP)
		, bootstrapAuraActive(false)
		, bootstrapAuraAlly(false)
		, bootstrapAuraChangeCount(0)
#endif
	{}

	virtual void Apply(CPtr<PF_Core::ClientObjectBase> const &pObject);

#if defined(PW_LINUX_DB_BOOTSTRAP)
	bool IsBootstrapAuraActive() const { return bootstrapAuraActive; }
	bool IsBootstrapAuraAlly() const { return bootstrapAuraAlly; }
	size_t GetBootstrapAuraChangeCount() const { return bootstrapAuraChangeCount; }
#endif

protected:
	virtual void Die();
  virtual void DieImmediate();

private:
#if defined(PW_LINUX_DB_BOOTSTRAP)
	CPtr<PF_Core::ClientObjectBase> pBootstrapObject;
	bool bootstrapAuraActive;
	bool bootstrapAuraAlly;
	size_t bootstrapAuraChangeCount;
#else
	CPtr<PFClientBaseUnit> pUnit;
#endif
};

}
