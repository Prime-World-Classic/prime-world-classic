#ifndef __L_LobbyIServerInstance_H__
#define __L_LobbyIServerInstance_H__

#include <RPC/RPC.h>
#include <RPC/CppWrapper.h>


#include "LobbyIServerInstance.h"

namespace lobby
{


class LIServerInstance : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LIServerInstance, rpc::ILocalEntity, BaseObjectMT);
public:
  LIServerInstance():entityMap(0) {}
  LIServerInstance(lobby::IServerInstance* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LIServerInstance() 
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
        { "lobby::IServerInstance::SetDeveloperSex", 1, false, rpc::GetMethodCode(&lobby::IServerInstance::SetDeveloperSex), 0 },
        { "lobby::IServerInstance::RequestGamesList", 0, false, rpc::GetMethodCode(&lobby::IServerInstance::RequestGamesList), 0 },
        { "lobby::IServerInstance::JoinGame", 1, false, rpc::GetMethodCode(&lobby::IServerInstance::JoinGame), 0 },
        { "lobby::IServerInstance::ReconnectToCustomGame", 3, false, rpc::GetMethodCode(&lobby::IServerInstance::ReconnectToCustomGame), 4 },
        { "lobby::IServerInstance::CreateCustomGame", 4, false, rpc::GetMethodCode(&lobby::IServerInstance::CreateCustomGame), 4 },
        { "lobby::IServerInstance::SpectateCustomGame", 1, false, rpc::GetMethodCode(&lobby::IServerInstance::SpectateCustomGame), 0 },
        { "lobby::IServerInstance::ChangeCustomGameSettings", 3, false, rpc::GetMethodCode(&lobby::IServerInstance::ChangeCustomGameSettings), 4 },
        { "lobby::IServerInstance::SetReady", 1, false, rpc::GetMethodCode(&lobby::IServerInstance::SetReady), 0 },
        { "lobby::IServerInstance::SetDeveloperParty", 1, false, rpc::GetMethodCode(&lobby::IServerInstance::SetDeveloperParty), 0 },
        { "lobby::IServerInstance::JoinSocialGame", 0, false, rpc::GetMethodCode(&lobby::IServerInstance::JoinSocialGame), 0 },
        { "lobby::IServerInstance::ConnectToWebLobby", 1, false, rpc::GetMethodCode(&lobby::IServerInstance::ConnectToWebLobby), 1 },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("lobby::IServerInstance", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xa3d7b3d; }
  static uint GetClassCrcStatic() { return 0xa3d7b3d; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<lobby::IServerInstance> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}



#endif // __L_LobbyIServerInstance_H__