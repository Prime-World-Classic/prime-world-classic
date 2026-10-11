#include "stdafx.h"
#include "LSimpleRPCRabbitAck.auto.h"

#pragma warning( push )
#pragma warning( disable : 4065 )

namespace test
{




    bool LISimpleRPCRabbitAck::vcall( byte method_id, rpc::MethodCall& call, rpc::MethodCallStack& stack )
    {
        bool popResult = true;
        switch ( method_id )
        {
			case 0: { NI_PROFILE_BLOCK("test::ISimpleRPCRabbitAck::Ack/0");rpc::VCall( stack, localObject.Get(), &test::ISimpleRPCRabbitAck::Ack, popResult); } break;

        default:
            popResult = false;
            break;
        }
        return popResult;
    }

}


namespace rpc
{
template<>
rpc::ILocalEntity* CreateLocalEntity(test::ISimpleRPCRabbitAck* api, rpc::IEntityMap* entityMap)
{
  return new test::LISimpleRPCRabbitAck(api, entityMap);
}

} // rpc

#pragma warning( pop )

NI_DEFINE_REFCOUNT( test::LISimpleRPCRabbitAck )


