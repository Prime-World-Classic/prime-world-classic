#ifndef __L_SimpleRPCRabbitAck_H__
#define __L_SimpleRPCRabbitAck_H__

#include <RPC/RPC.h>
#include <RPC/CppWrapper.h>


#include "SimpleRPCRabbitAck.h"

namespace test
{


class LISimpleRPCRabbitAck : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LISimpleRPCRabbitAck, rpc::ILocalEntity, BaseObjectMT);
public:
  LISimpleRPCRabbitAck():entityMap(0) {}
  LISimpleRPCRabbitAck(test::ISimpleRPCRabbitAck* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LISimpleRPCRabbitAck() 
  {
    if (factory)
    {
      factory->Destroy(this);
    }
  }
  
  virtual rpc::CallResult::Enum Call(const rpc::MethodCallHeader& call, rpc::MethodCall& resultCall, rpc::Arguments& args, const byte* paramsData, int _paramsSize)
  {
    entityId = resultCall.info.header.entityId;
    
    static const rpc::MethodInfo methods[] = 
    {
        { "test::ISimpleRPCRabbitAck::Ack", 1, false, rpc::GetMethodCode(&test::ISimpleRPCRabbitAck::Ack) },
    };
    if (call.id >= sizeof(methods)/sizeof(rpc::MethodInfo) || call.id < 0)
    {
      return rpc::CallResult::WrongMethodId;
    }
    rpc::MethodCallStack stack = rpc::FillStack(call, args, paramsData, _paramsSize, methods[call.id]);
    if (true || stack.methodCode == methods[call.id].methodCode)
    {
      if ( !stack.isValid ) return rpc::CallResult::DataCorruption;
      return vcall(call.id, resultCall, stack) ? rpc::CallResult::OK : rpc::CallResult::StackCorruption;
    }
    else
    {
      return rpc::CallResult::WrongMethodCode;
    }
  }

  virtual void Publish()
  {
    StrongMT<rpc::IEntityMap> sentityMap = entityMap.Lock();
    if (sentityMap)
    {
      sentityMap->Publish( entityId, this );
    }
  }

  bool vcall( byte method_id, rpc::MethodCall& call, rpc::MethodCallStack& stack );
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("test::ISimpleRPCRabbitAck", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0x1e2e5438; }
  static uint GetClassCrcStatic() { return 0x1e2e5438; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<test::ISimpleRPCRabbitAck> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}



#endif // __L_SimpleRPCRabbitAck_H__