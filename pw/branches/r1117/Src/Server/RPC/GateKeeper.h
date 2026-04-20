#ifndef GATEKEEPER_H_7868FF9E_2769_4E24
#define GATEKEEPER_H_7868FF9E_2769_4E24

#include "TransportPipe.h"
#include "Base.h"
#include "Network/TransportUtils.h"
#include "System/ThreadPool.h"
#include "PollJob.h"
#include "TransportPipeTrackerFactory.h"
#include "ClassFactory.h"

namespace Transport
{
  _interface IClientTransportSystem;
}

namespace rpc
{
  class TransportPipe;

  /// ������������� ������ �� ���������� �������� GateKeeper
  _interface IGateKeeperCallback : public IBaseInterfaceMT
  {
    NI_DECLARE_CLASS_1( IGateKeeperCallback, IBaseInterfaceMT );

    /// ���������� �� Poll() ��� ��������� ������ ������. ������� � ���������� ����� � ����
    virtual void OnNewNode( Transport::IChannel * channel,  rpc::Node * node ) = 0;

    /// ���������� �� Poll() ��� �������� ������. ������� � ���������� ����� � ����
    virtual void OnChannelClosed( Transport::IChannel * channel, rpc::Node * node ) = 0;

    /// ���������� ��� ����������� ����������� ������� � ������
    virtual void OnCorruptData( Transport::IChannel * channel, rpc::Node * node ) = 0;
  };




  /// ������� ����� ��� GateKeeper � GateKeeperClient
  class GateKeeperBase : public BaseObjectMT, public IPipeProcessor
  {
    NI_DECLARE_REFCOUNT_CLASS_2( GateKeeperBase, BaseObjectMT, IPipeProcessor )

  public:
    GateKeeperBase( const Transport::TServiceId& _serviceId, Transport::TClientId _clientId, IGateKeeperCallback* callback,
       unsigned int nthreads = 1);

    ~GateKeeperBase(){}

    /// ����������� ����������� ������ � ���� ���
    void Poll();

    /// ���������� ���� 
    //TODO: return pointer, NOT ref
    rpc::Gate * GetGate() const
    { 
      return gate;
    }

    void setGateKeeperCallback( IGateKeeperCallback* _callback );
    void attachNotificationCallback( IGateKeeperCallback * _callback );
    void detachNotificationCallback( IGateKeeperCallback * _callback );

    /// ���������� ���� �� serviceId. ���� �� �������, ������ ����� ����.
    StrongMT<rpc::Node> RequestNode( const Transport::TServiceId& destination );

    void setTransportPipeTrackerFactory( TransportPipeTrafficTrackerFactory * tpttf );

    /// ���������� ���� �� serviceId. ���� �� �������, ���������� 0.
    StrongMT<rpc::Node> GetNode( const Transport::TServiceId& destination ) const;

    /// ������� ���� �� serviceId
    void RemoveNode( const Transport::TServiceId& destination );

  protected:
    Transport::TServiceId serviceId;

    GateKeeperBase() : gate(0) {}

    void Init();
    void Destroy();
    void AddReceivedPipe( Transport::IChannel * channel );
    StrongMT<TransportPipe> CreatePipe( Transport::IChannel * chan, const GUID * id =0 );
    void GenerateServiceId( Transport::IChannel* channel, Transport::TServiceId *serviceId );

    virtual StrongMT<Transport::IChannel> OpenChannel( const Transport::Address& address ) = 0;
    virtual void OnPrePoll() {}
    virtual void OnPostPoll() {};

  private:
    typedef hash_map<Transport::TServiceId, StrongMT<TransportPipe>, Transport::ServiceIdHash> TPipes;
    typedef list<StrongMT<TransportPipe> > TPipeList;
    typedef nstl::list<WeakMT<IGateKeeperCallback> > CallbacksT;

    CallbacksT callbacks;

    Transport::TClientId clientId;
    unsigned int pipeCount;

    StrongMT<rpc::Gate> gate;
    TPipes pipes;
    TPipeList closingPipes;

    threading::ThreadPool tp_;
    CloseChannelJobFactory ccjobFactory_;

    //rpc::IPipeProcessor
    virtual void OnCorruptData( rpc::IPacketPipe * _pipe );
    virtual StrongMT<rpc::IPacketPipe> ConnectToPipe( const char* name, const GUID& id);
    virtual const char* GetName() const  { return serviceId.c_str(); }

    void attachNotificationCallback_(IGateKeeperCallback* _callback);

    StrongMT<TransportPipeTrafficTrackerFactory> tpttf;
    typedef map<Transport::TServiceId, StrongMT<TransportPipeTrafficTrackerFactory> > TPipeTrackers;
    TPipeTrackers trackers;
  };

 /// ������ � ����� � ����� �� ����������� ������.
 /// ����� GateKeeper ������������ ��� ������� � ����� � ����� �� ����������� ������. 
 /// �� ������ � ���� ����, ��������� ���, � ����� ������ �� callback-������.
 /// 
 /// ��. ����� ������ Gate � Node
 class GateKeeper : public GateKeeperBase 
 {
   NI_DECLARE_REFCOUNT_CLASS_1( GateKeeper, GateKeeperBase )

 public:
   /// �����������
   /// <param name="_transport">������������ ������ �� ������ �������� ����������</param>
   /// <param name="_serviceId">��� ������� ����������� ������</param>
   /// <param name="_clientId">�������������� ��� (������������ ��� �������� ������), �� ��������� Transport::autoAssignClientId</param>
   /// <param name="callback">������ �� callback-������, �� ��������� 0</param>
   GateKeeper( Transport::ITransportSystem* _transport, const Transport::TServiceId& _serviceId, 
     Transport::TClientId _clientId = Transport::autoAssignClientId, IGateKeeperCallback* callback = 0,
     Network::NetAddress const & listenAddress = nstl::string());

   /// ����������
   ~GateKeeper()
   {
     Destroy();
   }

 private:
   StrongMT<Transport::ITransportSystem> transport;
   StrongMT<Transport::IChannelListener> listener;

   virtual StrongMT<Transport::IChannel> OpenChannel( const Transport::Address& address );
   virtual void OnPostPoll();

 };

 /// ������ � ����� � ����� �� ����������� ������ �������.
 /// ����� GateKeeperClient ���������� ������ GateKeeper, 
 /// �� ���������� ��� �������� ������� ��������� Transport::IClientTransportSystem, 
 /// � �� �������� � ������������ ��������� _clientId.
 class GateKeeperClient : public GateKeeperBase 
 {
   NI_DECLARE_REFCOUNT_CLASS_1( GateKeeperClient, GateKeeperBase )

 public:
   /// �����������
   /// <param name="_transport">������������ ������ �� ������ �������� ����������</param>
   /// <param name="_serviceId">��� ������� ����������� ������</param>
   /// <param name="callback">������ �� callback-������, �� ��������� 0</param>
   GateKeeperClient( Transport::IClientTransportSystem* _transport, const Transport::TServiceId& _serviceId, IGateKeeperCallback* callback = 0 );

   /// ����������
   ~GateKeeperClient()
   {
     Destroy();
   }

   Transport::TClientId GetClientId() const;

 private:
   GateKeeperClient() {}

   StrongMT<Transport::IClientTransportSystem> transport;

   virtual StrongMT<Transport::IChannel> OpenChannel( const Transport::Address& address );
 };


 
class TransportPipe : public IPacketPipe, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( TransportPipe, IPacketPipe, BaseObjectMT );
public:
 TransportPipe( Transport::IChannel * channel, Transport::TServiceId _serviceId, TransportPipeTrafficTracker * _tptt );
 ~TransportPipe();

 bool IsClosed() const;
 StrongMT<rpc::Node> LockNode() { return node.Lock(); }
 void SetNode( rpc::Node * _node ) { node = _node; }

 virtual const char* GetName() const { return serviceId.c_str(); }
 virtual Transport::IChannel * GetPipeChannel() const { return channel; } 
 virtual IPacketPipe::PipeInfo GetInfo() const;
 virtual void Send( const byte* data, int size, bool force );
 virtual bool Recieve(int index, byte** data, int* size, Transport::MessageMiscInfo & _miscInfo );
 virtual bool Recieve(int index, byte** data, int* size );
 virtual void Reset();

private:
  WeakMT<rpc::Node> node; 
  Transport::TServiceId serviceId;
  nstl::vector<byte> dataCache;
  StrongMT<Transport::IChannel> channel;
  StrongMT<TransportPipeTrafficTracker> tptt;
};

}

#endif //#define GATEKEEPER_H_7868FF9E_2769_4E24
