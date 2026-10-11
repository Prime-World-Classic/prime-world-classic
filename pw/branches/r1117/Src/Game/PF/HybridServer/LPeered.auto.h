#ifndef __L_Peered_H__
#define __L_Peered_H__

#include <RPC/RPC.h>
#include <RPC/CppWrapper.h>
#include "RPeered.auto.h"


#include "Peered.h"

namespace Peered
{


class LIGameClient : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LIGameClient, rpc::ILocalEntity, BaseObjectMT);
public:
  LIGameClient():entityMap(0) {}
  LIGameClient(Peered::IGameClient* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LIGameClient() 
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
        { "Peered::IGameClient::Start", 2, false, rpc::GetMethodCode(&Peered::IGameClient::Start), 3 },
        { "Peered::IGameClient::Step", 6, true, rpc::GetMethodCode(&Peered::IGameClient::Step), 60 },
        { "Peered::IGameClient::StepLoading", 5, false, rpc::GetMethodCode(&Peered::IGameClient::StepLoading), 28 },
        { "Peered::IGameClient::StepBatch", 2, false, rpc::GetMethodCode(&Peered::IGameClient::StepBatch), 2 },
        { "Peered::IGameClient::OnAsync", 2, false, rpc::GetMethodCode(&Peered::IGameClient::OnAsync), 2 },
        { "Peered::IGameClient::OnStart", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnStart), 0 },
        { "Peered::IGameClient::OnTimeScaleChanged", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnTimeScaleChanged), 0 },
        { "Peered::IGameClient::OnChangeClientStatus", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnChangeClientStatus), 1 },
        { "Peered::IGameClient::OnDisconnect", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnDisconnect), 0 },
        { "Peered::IGameClient::OnRejoin", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnRejoin), 0 },
        { "Peered::IGameClient::RequestWorldData", 1, false, rpc::GetMethodCode(&Peered::IGameClient::RequestWorldData), 1 },
        { "Peered::IGameClient::RequestCrcData", 1, false, rpc::GetMethodCode(&Peered::IGameClient::RequestCrcData), 0 },
        { "Peered::IGameClient::Award", 1, false, rpc::GetMethodCode(&Peered::IGameClient::Award), 1 },
        { "Peered::IGameClient::OnSpectate", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnSpectate), 1 },
        { "Peered::IGameClient::OnPlaySameTeamDecision", 2, false, rpc::GetMethodCode(&Peered::IGameClient::OnPlaySameTeamDecision), 0 },
        { "Peered::IGameClient::OnBadBehaviourComplaint", 3, false, rpc::GetMethodCode(&Peered::IGameClient::OnBadBehaviourComplaint), 0 },
        { "Peered::IGameClient::OnProtectionAsync", 1, false, rpc::GetMethodCode(&Peered::IGameClient::OnProtectionAsync), 0 },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("Peered::IGameClient", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xcf716401; }
  static uint GetClassCrcStatic() { return 0xcf716401; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<Peered::IGameClient> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}

namespace Peered
{


class LIGameClientReconnect : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LIGameClientReconnect, rpc::ILocalEntity, BaseObjectMT);
public:
  LIGameClientReconnect():entityMap(0) {}
  LIGameClientReconnect(Peered::IGameClientReconnect* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LIGameClientReconnect() 
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
        { "Peered::IGameClientReconnect::ApplyWorldDataInfo", 1, false, rpc::GetMethodCode(&Peered::IGameClientReconnect::ApplyWorldDataInfo), 1 },
        { "Peered::IGameClientReconnect::ApplyWorldDataPart", 1, false, rpc::GetMethodCode(&Peered::IGameClientReconnect::ApplyWorldDataPart), 1 },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("Peered::IGameClientReconnect", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xc334a3a4; }
  static uint GetClassCrcStatic() { return 0xc334a3a4; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<Peered::IGameClientReconnect> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}

namespace Peered
{


class LIGameServer : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LIGameServer, rpc::ILocalEntity, BaseObjectMT);
public:
  LIGameServer():entityMap(0) {}
  LIGameServer(Peered::IGameServer* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LIGameServer() 
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
        { "Peered::IGameServer::AddClient", 4, false, rpc::GetMethodCode(&Peered::IGameServer::AddClient), 13 },
        { "Peered::IGameServer::AddClientFast", 4, false, rpc::GetMethodCode(&Peered::IGameServer::AddClientFast), 4 },
        { "Peered::IGameServer::SendCommand", 3, false, rpc::GetMethodCode(&Peered::IGameServer::SendCommand), 2 },
        { "Peered::IGameServer::OnFinishStep", 3, false, rpc::GetMethodCode(&Peered::IGameServer::OnFinishStep), 0 },
        { "Peered::IGameServer::OnClientReady", 1, false, rpc::GetMethodCode(&Peered::IGameServer::OnClientReady), 0 },
        { "Peered::IGameServer::SetTimeScale", 2, false, rpc::GetMethodCode(&Peered::IGameServer::SetTimeScale), 0 },
        { "Peered::IGameServer::Ping", 1, false, rpc::GetMethodCode(&Peered::IGameServer::Ping), 1 },
        { "Peered::IGameServer::OnGameFinish", 3, false, rpc::GetMethodCode(&Peered::IGameServer::OnGameFinish), 4 },
        { "Peered::IGameServer::RequestWorldData", 1, false, rpc::GetMethodCode(&Peered::IGameServer::RequestWorldData), 0 },
        { "Peered::IGameServer::OnGameLeave", 1, false, rpc::GetMethodCode(&Peered::IGameServer::OnGameLeave), 0 },
        { "Peered::IGameServer::SendCrcData", 3, false, rpc::GetMethodCode(&Peered::IGameServer::SendCrcData), 4 },
        { "Peered::IGameServer::OnDisconnectAck", 1, false, rpc::GetMethodCode(&Peered::IGameServer::OnDisconnectAck), 0 },
        { "Peered::IGameServer::DispatchPlaySameTeamDecision", 2, false, rpc::GetMethodCode(&Peered::IGameServer::DispatchPlaySameTeamDecision), 0 },
        { "Peered::IGameServer::DispatchBadBehaviourComplaint", 3, false, rpc::GetMethodCode(&Peered::IGameServer::DispatchBadBehaviourComplaint), 0 },
        { "Peered::IGameServer::OnProtectionError", 1, false, rpc::GetMethodCode(&Peered::IGameServer::OnProtectionError), 0 },
        { "Peered::IGameServer::SendProtectionMagic", 3, false, rpc::GetMethodCode(&Peered::IGameServer::SendProtectionMagic), 0 },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("Peered::IGameServer", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xeb8e44be; }
  static uint GetClassCrcStatic() { return 0xeb8e44be; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<Peered::IGameServer> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}

namespace Peered
{


class LIGameServerInternal : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LIGameServerInternal, rpc::ILocalEntity, BaseObjectMT);
public:
  LIGameServerInternal():entityMap(0) {}
  LIGameServerInternal(Peered::IGameServerInternal* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LIGameServerInternal() 
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
        { "Peered::IGameServerInternal::OnRejoinClient", 1, false, rpc::GetMethodCode(&Peered::IGameServerInternal::OnRejoinClient), 0 },
        { "Peered::IGameServerInternal::AddSpectator", 2, false, rpc::GetMethodCode(&Peered::IGameServerInternal::AddSpectator), 2 },
        { "Peered::IGameServerInternal::UserLeftTheGameInCastle", 1, false, rpc::GetMethodCode(&Peered::IGameServerInternal::UserLeftTheGameInCastle), 0 },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("Peered::IGameServerInternal", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0xec13ccd2; }
  static uint GetClassCrcStatic() { return 0xec13ccd2; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<Peered::IGameServerInternal> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}

namespace Peered
{


class LIGameServerReconnect : public rpc::ILocalEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(LIGameServerReconnect, rpc::ILocalEntity, BaseObjectMT);
public:
  LIGameServerReconnect():entityMap(0) {}
  LIGameServerReconnect(Peered::IGameServerReconnect* _localObject, rpc::IEntityMap* _entityMap, rpc::ILocalEntityFactory* _factory=0)
  :   factory(_factory)
  ,   entityMap(_entityMap)
  ,   localObject(_localObject) 
  {
    localObject->SetOwner( this );
  }

  ~LIGameServerReconnect() 
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
        { "Peered::IGameServerReconnect::SendWorldDataInfo", 2, false, rpc::GetMethodCode(&Peered::IGameServerReconnect::SendWorldDataInfo), 2 },
        { "Peered::IGameServerReconnect::SendWorldDataPart", 2, false, rpc::GetMethodCode(&Peered::IGameServerReconnect::SendWorldDataPart), 2 },
        { "Peered::IGameServerReconnect::SetGameClientReconnect", 2, false, rpc::GetMethodCode(&Peered::IGameServerReconnect::SetGameClientReconnect), 2 },
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
  virtual void* _Get( uint classId ) { return (classId == rpc::_GetId("Peered::IGameServerReconnect", rpc::GeneratedType) ) ? localObject : 0; }
  virtual uint GetClassCrc() const { return 0x3bd049b8; }
  static uint GetClassCrcStatic() { return 0x3bd049b8; }

  virtual int GetMemberIndex( const void* ) { return -1; }


private:
  StrongMT<Peered::IGameServerReconnect> localObject;
  rpc::ILocalEntityFactory* factory;
  WeakMT<rpc::IEntityMap> entityMap;
  rpc::EntityId entityId;
};

}



#endif // __L_Peered_H__