#pragma once

#include "GameBalancer/GameBalancerIface.h"
#include "HybridServer/GameServerAllocatorIface.h"
#include "GameBalancer/GameSvcHostContext.h"
#include <RPC/GateKeeper.h>
#include <Coordinator/ServiceAppearanceNotifierIface.h>
#include <Coordinator/RServiceAppearanceNotifierIface.auto.h>
#include <System/HPTimer.h>

namespace GameBalancer
{
  class SvcContext;
  class SvcAllocContext;

  class BalancerImpl : public IBalancer, public Coordinator::IServiceAppearanceSubscriber, 
    public rpc::IfaceRequesterCallback<Coordinator::RIServiceAppearancePublisher>, public BaseObjectMT
  {
    NI_DECLARE_REFCOUNT_CLASS_4( BalancerImpl, IBalancer, Coordinator::IServiceAppearanceSubscriber, 
      rpc::IfaceRequesterCallback<Coordinator::RIServiceAppearancePublisher>, BaseObjectMT );

    typedef nstl::list<StrongMT<SvcContext> > SvcContextListT;
    typedef nstl::list<StrongMT<HostContext> > HostContextListT;
    typedef nstl::map<Transport::TServiceId, HostContextListT> HostsByPathT;

  public:
    BalancerImpl(uint _softlimit, uint _hardlimit, rpc::GateKeeper * _gk, Transport::IAddressTranslator* _addrResolver);
    ~BalancerImpl();

    //  IBalancer
    virtual int AllocateGameSvc(Transport::TServiceId const & svcpath, Peered::TSessionId userctx, IBalancerCallback* cb);

    //  IGameSvcAppearanceNotifier
    virtual void AddService( Transport::TServiceId const & svcid );
    virtual void RemoveService( Transport::TServiceId const & svcid );

    //  IServiceAppearanceSubscriber
    virtual void OnRegisterSubscriber(Coordinator::SubcriberIdT _id, Coordinator::ClusterInfo const & _cluster);
    virtual void OnRegisterService(Transport::TServiceId const & _svcid);
    virtual void OnUnregisterService(Transport::TServiceId const & _svcid);
    virtual void OnStartService(Coordinator::SvcInfo const & _si);
    virtual void OnStopService(Transport::TServiceId const & _svcid);
    virtual void OnChangeServiceStatus(Transport::TServiceId const & _svcid, /*Coordinator::ServiceStatus::Enum*/int _status);

    //  
    int Step();

  private:
    StrongMT<SvcContext> registerService_(Transport::TServiceId const & svcid);
    StrongMT<SvcContext> unregisterService_(Transport::TServiceId const & svcid);

    int registerInitializedContext_( SvcContext * svctx );
    StrongMT<HostContext> findHostCtx_(HostContextListT const & hostlst, Network::NetAddress const & netaddr);

    StrongMT<SvcContext> findSvc_(Transport::TServiceId const & svcpath, Peered::TSessionId userctx, 
      unsigned int softlimit, unsigned int hardlimit);

    //  rpc::IfaceRequesterCallback
    virtual void onChangeState(rpc::IfaceRequesterState::Enum _st, 
      StrongMT<rpc::IfaceRequester<Coordinator::RIServiceAppearancePublisher> > const & _ifacereq);

  private:
    uint softlimit_;
    uint hardlimit_;
    int limitPerSvc;
    StrongMT<rpc::GateKeeper> gk;
    HostsByPathT hostByPath_;
    SvcContextListT registeredSvcCtxs;
    SvcContextListT initSvcCtxs;
    StrongMT<Transport::IAddressTranslator> addrResolver_;

    StrongMT<rpc::IfaceRequester<Coordinator::RIServiceAppearancePublisher> > svcPublisherIface_;
    Coordinator::SubcriberIdT subscriberId_;

    // Startup-race recovery (single-process deployment): the initial cluster
    // snapshot (OnRegisterSubscriber) may be taken before the local services
    // are announced to the coordinator, leaving no started gamesvc; the later
    // OnStartService notifications can be lost as well (pipe still settling).
    // While no service is registered, resubscribe periodically — the
    // publisher resends the current cluster info on re-subscription.
    bool resubscribePending_;
    NHPTimer::FTime lastResubscribeTime_;
    int resubscribeRetries_;
    static constexpr NHPTimer::FTime kResubscribeInterval = 1.0; // seconds
    static constexpr int kMaxResubscribeRetries = 60;
  };
}
