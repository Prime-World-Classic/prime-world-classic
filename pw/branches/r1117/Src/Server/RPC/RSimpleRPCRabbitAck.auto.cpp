#include "stdafx.h"
#include "RSimpleRPCRabbitAck.auto.h"

DEFINE_RE_FACTORY( test, RISimpleRPCRabbitAck );

namespace rpc
{

template<>
void RegisterRemoteFactory(test::RISimpleRPCRabbitAck* factory)
{
  &factory_test_RISimpleRPCRabbitAck;
}

} // rpc

NI_DEFINE_REFCOUNT( test::RISimpleRPCRabbitAck )
