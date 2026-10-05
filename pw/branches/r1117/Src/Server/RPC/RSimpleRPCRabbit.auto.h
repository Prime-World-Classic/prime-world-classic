#ifndef __R_SimpleRPCRabbit_H__
#define __R_SimpleRPCRabbit_H__

#include <RPC/RPC.h>
#include "RPCMixin.h"
#include "SimpleRPCRabbitTypes.h"
#include "RSimpleRPCRabbitAck.auto.h"




namespace test
{


class RSimpleRPCRabbit : public rpc::IRemoteEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(RSimpleRPCRabbit, rpc::IRemoteEntity, BaseObjectMT);
public:
  RPC_INFO("test::SimpleRPCRabbit", 0xad0d87dc);
  
  RSimpleRPCRabbit() : handler(0) {}
  RSimpleRPCRabbit( rpc::EntityHandler* _handler, rpc::IRemoteEntity* _parent )
  :  handler(_handler)
  ,  parent(_parent)

  {

  }

  ~RSimpleRPCRabbit()
  {
    if( handler )
    {
      handler->OnDestruct(*this);
      handler = 0;
    }
  }
  virtual rpc::RemoteEntityInfo GetInfo() const { rpc::RemoteEntityInfo info = { handler->GetId(), { RSimpleRPCRabbit::ID(), RSimpleRPCRabbit::CRC32}, handler->GetGUID() }; return info; }
  inline bool IsUpdated() const { return handler->IsUpdated(); }
  rpc::EntityHandler* GetHandler() { return handler; }

  void process( )
  {
    handler->Go(handler->Call( 0 ));
  }

  template <typename T>
  rpc::ECallResult::Enum processIntWithReturnValueIntAsync( int value, T* object, void (T::*func)(int result) )
  {           
    rpc::Transaction* transaction = handler->Call( 1, value );
    if (transaction)
    {
      transaction->RegisterAsyncCall( transaction->GetInfo(), new rpc::FunctorNoContext<T, int>(object, func) );
      return handler->Go(transaction);
    }
    return rpc::ECallResult::NoTransaction;
  }
  

  template <typename T, typename C>
  rpc::ECallResult::Enum processIntWithReturnValueIntAsync( int value, T* object, void (T::*func)(int result, C context, rpc::CallStatus status), const C& context, float timeout=0.f)
  {           
    rpc::Transaction* transaction = handler->Call( 1, value );
    if (transaction)
    {
      transaction->RegisterAsyncCall( transaction->GetInfo(), new rpc::FunctorContext<T, int, C>(object, func, context), timeout );
      return handler->Go(transaction);
    }
    return rpc::ECallResult::NoTransaction;
  }
  
  void processSnapshot(  const test::InitialSnapshot& _msg )
  {
    handler->Go(handler->Call( 2, _msg ));
  }

  template <typename T>
  rpc::ECallResult::Enum GetServerDef( T* object, void (T::*func)(const InitialSnapshot& result) )
  {           
    rpc::Transaction* transaction = handler->Call( 3 );
    if (transaction)
    {
      transaction->RegisterAsyncCall( transaction->GetInfo(), new rpc::RefFunctorNoContext<T, const InitialSnapshot>(object, func) );
      return handler->Go(transaction);
    }
    return rpc::ECallResult::NoTransaction;
  }
  

  template <typename T, typename C>
  rpc::ECallResult::Enum GetServerDef( T* object, void (T::*func)(const InitialSnapshot& result, C context, rpc::CallStatus status), const C& context, float timeout=0.f)
  {           
    rpc::Transaction* transaction = handler->Call( 3 );
    if (transaction)
    {
      transaction->RegisterAsyncCall( transaction->GetInfo(), new rpc::RefFunctorContext<T, const InitialSnapshot, C>(object, func, context), timeout );
      return handler->Go(transaction);
    }
    return rpc::ECallResult::NoTransaction;
  }
  
  void Roll(  const test::SPreGameData& _preGame, const test::SPostGameData& _postGame, NI_LPTR test::ISimpleRPCRabbitAck* _ack )
  {
    handler->Go(handler->Call( 4, _preGame, _postGame, RemotePtr<test::RISimpleRPCRabbitAck>(_ack) ));
  }
  void probeEnumU64(  EGameFinishClientState::Enum _e, unsigned long long _v )
  {
    handler->Go(handler->Call( 5, _e, _v ));
  }



  bool Update(rpc::IUpdateCallback* callback=0)
  {
    return handler->Update(this, callback);
  }

  bool SetUpdateCallback(rpc::IUpdateCallback* callback=0)
  {
    return handler->SetUpdateCallback(callback);
  }

  void ReadOnly( bool val )
  {
    handler->ReadOnly( val );
  }

  void Publish()
  {
    handler->Publish();
  }
 
  StrongMT<rpc::INode> GetNode(int index) { return GetHandler()->GetNode(index); }
  StrongMT<rpc::INode> GetNode(const char* name) { return GetHandler()->GetNode(name); }
  virtual rpc::IUpdateCallback* GetUpdateCallback() { StrongMT<rpc::IRemoteEntity> _parent = parent.Lock(); return handler->GetUpdateCallback(_parent); }
  virtual void SetParent(rpc::IRemoteEntity* _parent) { parent = _parent; }
  virtual rpc::Status GetStatus() { return handler->GetStatus(); }

  static uint GetClassCrcStatic() { return 0xad0d87dc; }
protected:
  friend class rpc::Gate;




private:
  StrongMT<rpc::EntityHandler> handler;
  WeakMT<rpc::IRemoteEntity> parent;


};

}



namespace test
{


class RSimpleRefCountedRPCRabbit : public rpc::IRemoteEntity, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(RSimpleRefCountedRPCRabbit, rpc::IRemoteEntity, BaseObjectMT);
public:
  RPC_INFO("test::SimpleRefCountedRPCRabbit", 0xf36250ef);
  
  RSimpleRefCountedRPCRabbit() : handler(0) {}
  RSimpleRefCountedRPCRabbit( rpc::EntityHandler* _handler, rpc::IRemoteEntity* _parent )
  :  handler(_handler)
  ,  parent(_parent)

  {

  }

  ~RSimpleRefCountedRPCRabbit()
  {
    if( handler )
    {
      handler->OnDestruct(*this);
      handler = 0;
    }
  }
  virtual rpc::RemoteEntityInfo GetInfo() const { rpc::RemoteEntityInfo info = { handler->GetId(), { RSimpleRefCountedRPCRabbit::ID(), RSimpleRefCountedRPCRabbit::CRC32}, handler->GetGUID() }; return info; }
  inline bool IsUpdated() const { return handler->IsUpdated(); }
  rpc::EntityHandler* GetHandler() { return handler; }

  void process( )
  {
    handler->Go(handler->Call( 0 ));
  }
  void processWithRecieveTime(  const NHPTimer::STime& __recieveTime__ )
  {
    handler->Go(handler->Call( 1 ));
  }



  bool Update(rpc::IUpdateCallback* callback=0)
  {
    return handler->Update(this, callback);
  }

  bool SetUpdateCallback(rpc::IUpdateCallback* callback=0)
  {
    return handler->SetUpdateCallback(callback);
  }

  void ReadOnly( bool val )
  {
    handler->ReadOnly( val );
  }

  void Publish()
  {
    handler->Publish();
  }
 
  StrongMT<rpc::INode> GetNode(int index) { return GetHandler()->GetNode(index); }
  StrongMT<rpc::INode> GetNode(const char* name) { return GetHandler()->GetNode(name); }
  virtual rpc::IUpdateCallback* GetUpdateCallback() { StrongMT<rpc::IRemoteEntity> _parent = parent.Lock(); return handler->GetUpdateCallback(_parent); }
  virtual void SetParent(rpc::IRemoteEntity* _parent) { parent = _parent; }
  virtual rpc::Status GetStatus() { return handler->GetStatus(); }

  static uint GetClassCrcStatic() { return 0xf36250ef; }
protected:
  friend class rpc::Gate;




private:
  StrongMT<rpc::EntityHandler> handler;
  WeakMT<rpc::IRemoteEntity> parent;


};

}



#endif // __R_SimpleRPCRabbit_H__
