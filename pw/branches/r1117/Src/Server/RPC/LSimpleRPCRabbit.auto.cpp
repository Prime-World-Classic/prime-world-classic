#include "stdafx.h"
#include "LSimpleRPCRabbit.auto.h"

#pragma warning( push )
#pragma warning( disable : 4065 )

namespace test
{


	typedef void (test::SimpleRPCRabbit::*TprocessSnapshot)( const test::InitialSnapshot* _msg);
	typedef void (test::SimpleRPCRabbit::*TRoll)( const test::SPreGameData* _preGame, const test::SPostGameData* _postGame, NI_LPTR test::ISimpleRPCRabbitAck* _ack);


    bool LSimpleRPCRabbit::vcall( byte method_id, rpc::MethodCall& call, rpc::MethodCallStack& stack )
    {
        bool popResult = true;
        switch ( method_id )
        {
			case 0: { NI_PROFILE_BLOCK("test::SimpleRPCRabbit::process/0");rpc::VCall( stack, localObject.Get(), &test::SimpleRPCRabbit::process, popResult); } break;
			case 1: { NI_PROFILE_BLOCK("test::SimpleRPCRabbit::processIntWithReturnValueIntAsync/1");
			{
				int result = rpc::VCall( stack, localObject.Get(), &test::SimpleRPCRabbit::processIntWithReturnValueIntAsync, popResult); 
				call.Prepare(1).Push(result);
			}
			} break;
			case 2: { NI_PROFILE_BLOCK("test::SimpleRPCRabbit::processSnapshot/2");rpc::VCall( stack, localObject.Get(), TprocessSnapshot(&test::SimpleRPCRabbit::processSnapshot), popResult); } break;
			case 3: { NI_PROFILE_BLOCK("test::SimpleRPCRabbit::GetServerDef/3");
			{
				const InitialSnapshot& result = rpc::VCall( stack, localObject.Get(), &test::SimpleRPCRabbit::GetServerDef, popResult); 
				call.Prepare(3).Push(result);
			}
			} break;
			case 4: { NI_PROFILE_BLOCK("test::SimpleRPCRabbit::Roll/4");rpc::VCall( stack, localObject.Get(), TRoll(&test::SimpleRPCRabbit::Roll), popResult); } break;
			case 5: { NI_PROFILE_BLOCK("test::SimpleRPCRabbit::probeEnumU64/5");rpc::VCall( stack, localObject.Get(), &test::SimpleRPCRabbit::probeEnumU64, popResult); } break;

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
rpc::ILocalEntity* CreateLocalEntity(test::SimpleRPCRabbit* api, rpc::IEntityMap* entityMap)
{
  return new test::LSimpleRPCRabbit(api, entityMap);
}

} // rpc

#pragma warning( pop )

NI_DEFINE_REFCOUNT( test::LSimpleRPCRabbit )


#pragma warning( push )
#pragma warning( disable : 4065 )

namespace test
{


	typedef void (test::SimpleRefCountedRPCRabbit::*TprocessWithRecieveTime)( const NHPTimer::STime* __recieveTime__);


    bool LSimpleRefCountedRPCRabbit::vcall( byte method_id, rpc::MethodCall& call, rpc::MethodCallStack& stack )
    {
        bool popResult = true;
        switch ( method_id )
        {
			case 0: { NI_PROFILE_BLOCK("test::SimpleRefCountedRPCRabbit::process/0");rpc::VCall( stack, localObject.Get(), &test::SimpleRefCountedRPCRabbit::process, popResult); } break;
			case 1: { NI_PROFILE_BLOCK("test::SimpleRefCountedRPCRabbit::processWithRecieveTime/1");rpc::VCall( stack, localObject.Get(), TprocessWithRecieveTime(&test::SimpleRefCountedRPCRabbit::processWithRecieveTime), popResult); } break;

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
rpc::ILocalEntity* CreateLocalEntity(test::SimpleRefCountedRPCRabbit* api, rpc::IEntityMap* entityMap)
{
  return new test::LSimpleRefCountedRPCRabbit(api, entityMap);
}

} // rpc

#pragma warning( pop )

NI_DEFINE_REFCOUNT( test::LSimpleRefCountedRPCRabbit )


