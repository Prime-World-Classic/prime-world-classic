#include "stdafx.h"
#include "RSimpleRPCRabbit.auto.h"

DEFINE_RE_FACTORY( test, RSimpleRPCRabbit );

namespace rpc
{

template<>
void RegisterRemoteFactory(test::RSimpleRPCRabbit* factory)
{
  &factory_test_RSimpleRPCRabbit;
}

} // rpc

NI_DEFINE_REFCOUNT( test::RSimpleRPCRabbit )
DEFINE_RE_FACTORY( test, RSimpleRefCountedRPCRabbit );

namespace rpc
{

template<>
void RegisterRemoteFactory(test::RSimpleRefCountedRPCRabbit* factory)
{
  &factory_test_RSimpleRefCountedRPCRabbit;
}

} // rpc

NI_DEFINE_REFCOUNT( test::RSimpleRefCountedRPCRabbit )
