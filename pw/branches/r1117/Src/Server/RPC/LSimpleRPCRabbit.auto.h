#ifndef __L_SimpleRPCRabbit_H__
#define __L_SimpleRPCRabbit_H__

#include <RPC/RPC.h>
#include <RPC/CppWrapper.h>


#include "SimpleRPCRabbit.h"

namespace test
{


class LSimpleRPCRabbit : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LSimpleRPCRabbit, rpc::ILocalEntity, BaseObjectMT);
public:
  LSimpleRPCRabbit():entityMap(0) {}
  LSimpleRPCRabbit(test::SimpleRPCRabbit* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LSimpleRPCRabbit() 
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
        { "test::SimpleRPCRabbit::process", 0, false, rpc::GetMethodCode(&test::SimpleRPCRabbit::process) },
        { "test::SimpleRPCRabbit::processIntWithReturnValueIntAsync", 1, false, rpc::GetMethodCode(&test::SimpleRPCRabbit::processIntWithReturnValueIntAsync) },
        { "test::SimpleRPCRabbit::processSnapshot", 1, false, rpc::GetMethodCode(&test::SimpleRPCRabbit::processSnapshot) },
        { "test::SimpleRPCRabbit::GetServerDef", 0, false, rpc::GetMethodCode(&test::SimpleRPCRabbit::GetServerDef) },
        { "test::SimpleRPCRabbit::Roll", 3, false, rpc::GetMethodCode(&test::SimpleRPCRabbit::Roll) },
        { "test::SimpleRPCRabbit::probeEnumU64", 2, false, rpc::GetMethodCode(&test::SimpleRPCRabbit::probeEnumU64) },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("test::SimpleRPCRabbit", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xad0d87dc; }
  static uint GetClassCrcStatic() { return 0xad0d87dc; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<test::SimpleRPCRabbit> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}

namespace test
{


class LSimpleRefCountedRPCRabbit : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LSimpleRefCountedRPCRabbit, rpc::ILocalEntity, BaseObjectMT);
public:
  LSimpleRefCountedRPCRabbit():entityMap(0) {}
  LSimpleRefCountedRPCRabbit(test::SimpleRefCountedRPCRabbit* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LSimpleRefCountedRPCRabbit() 
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
        { "test::SimpleRefCountedRPCRabbit::process", 0, false, rpc::GetMethodCode(&test::SimpleRefCountedRPCRabbit::process) },
        { "test::SimpleRefCountedRPCRabbit::processWithRecieveTime", 1, false, rpc::GetMethodCode(&test::SimpleRefCountedRPCRabbit::processWithRecieveTime) },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("test::SimpleRefCountedRPCRabbit", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xf36250ef; }
  static uint GetClassCrcStatic() { return 0xf36250ef; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<test::SimpleRefCountedRPCRabbit> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}



#endif // __L_SimpleRPCRabbit_H__