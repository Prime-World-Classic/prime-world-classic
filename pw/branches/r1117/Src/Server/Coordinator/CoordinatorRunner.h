#pragma once
#include "Network/Network.h"
#include "Network/Transport.h"
#include "Network/TransportInitializer.h"
 
#include "Coordinator/CoordinatorClient.h"
#include "Coordinator/CoordinatorServer.h"
#include "ServerAppBase/ServerRunner.h"
#include "System/JobThread.h"
#include "System/SyncPrimitives.h"

namespace Coordinator
{

class CoordinatorServerJob : public threading::IThreadJob, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( CoordinatorServerJob, threading::IThreadJob, BaseObjectMT );

public:
  CoordinatorServerJob( Transport::ITransportSystem * _transport, const string & _coordinatorAddress );
  ~CoordinatorServerJob();

  Coordinator::CoordinatorServer * GetServer() const { return coordinatorServer; }

  // Local (single-process) deployment: a one-shot task executed on the
  // coordinator job thread (the same thread running Step() and the RPC
  // dispatch), so the coordinator state is touched only from its owning
  // thread. Ownership of the task transfers to the job.
  struct LocalAnnounceTask
  {
    virtual ~LocalAnnounceTask() {}
    virtual void Run( CoordinatorServer * _server ) = 0;
  };
  void PostLocalAnnounceTask( LocalAnnounceTask * _task );

private:
  virtual void Work( volatile bool & isRunning );

private:
  StrongMT<rpc::GateKeeper>                 gateKeeper;
  StrongMT<Coordinator::CoordinatorServer>  coordinatorServer;
  threading::Mutex                          announceTaskMutex;
  LocalAnnounceTask *                       pendingAnnounceTask;
};
}
