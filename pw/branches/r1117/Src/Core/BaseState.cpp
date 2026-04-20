#include "stdafx.h"
#include "BaseState.h"

namespace NCore
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CEmptyObject 
	: public NCore::IBaseFSMState
	, public CObjectBase
{
	OBJECT_METHODS( 0x104B4D00, CEmptyObject )

public:
	virtual void Init() {}
	virtual NCore::IBaseFSMState *Step(float) { return 0; }
};

} // namespace NCore

template<>
NCore::IBaseFSMState *GetInvalid<NCore::IBaseFSMState>()
{
	static NCore::CEmptyObject emptyObj;
	return (NCore::IBaseFSMState*)(void*)__builtin_addressof(emptyObj);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
