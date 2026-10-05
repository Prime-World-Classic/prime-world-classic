#ifndef __R_SimpleRPCRabbitAck_H__
#define __R_SimpleRPCRabbitAck_H__

#include <RPC/RPC.h>
#include "RSimpleRPCRabbitAck.auto.h"


#include "SimpleRPCRabbitAck.h"

namespace test
{


class RISimpleRPCRabbitAck : public ISimpleRPCRabbitAck, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(RISimpleRPCRabbitAck, ISimpleRPCRabbitAck, BaseObjectMT);
public:
  RPC_INFO("test::ISimpleRPCRabbitAck", 0x1e2e5438);
  
  RISimpleRPCRabbitAck() : handler(0) {}
  RISimpleRPCRabbitAck( rpc::EntityHandler* _handler, rpc::IRemoteEntity* _parent )
  :  handler(_handler)
  ,  parent(_parent)

  {

  }

  ~RISimpleRPCRabbitAck()
  {
    if( handler )
    {
      handler->OnDestruct(*this);
      handler = 0;
    }
  }
  virtual rpc::RemoteEntityInfo GetInfo() const { rpc::RemoteEntityInfo info = { handler->GetId(), { RISimpleRPCRabbitAck::ID(), RISimpleRPCRabbitAck::CRC32}, handler->GetGUID() }; return info; }
  inline bool IsUpdated() const { return handler->IsUpdated(); }
  rpc::EntityHandler* GetHandler() { return handler; }

  void Ack(  int _value )
  {
    handler->Go(handler->Call( 0, _value ));
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

  static uint GetClassCrcStatic() { return 0x1e2e5438; }
protected:
  friend class rpc::Gate;




private:
  StrongMT<rpc::EntityHandler> handler;
  WeakMT<rpc::IRemoteEntity> parent;


};

}



#endif // __R_SimpleRPCRabbitAck_H__
