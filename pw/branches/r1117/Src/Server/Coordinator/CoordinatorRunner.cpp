#include "stdafx.h"
#include "CoordinatorServer.h"
#include "LCoordinatorServerIface.auto.h"

//#include "transport/TLTransportModule.h"
//#include "transport/TLCfg.h"
//#include "transport/TLGlobalCfg.h"
#include "CoordinatorRunner.h"
#include "System/sleep.h"

namespace Coordinator
{

CoordinatorServerJob::CoordinatorServerJob( Transport::ITransportSystem * _transport, const string & coordinatorAddress ) : pendingAnnounceTask( 0 )
{
  coordinatorServer = new Coordinator::CoordinatorServer;

  gateKeeper = new rpc::GateKeeper( _transport, Transport::ENetInterface::Coordinator, Transport::autoAssignClientId, coordinatorServer, coordinatorAddress);

  int rc = coordinatorServer->Init( coordinatorAddress, gateKeeper );
  NI_ASSERT(rc >= 0, "Coordinator initialization is FAILED");
}



CoordinatorServerJob::~CoordinatorServerJob()
{
  LocalAnnounceTask * task = 0;
  { threading::MutexLock lock( announceTaskMutex ); task = pendingAnnounceTask; pendingAnnounceTask = 0; }
  if ( task )
    delete task;

  if ( coordinatorServer )
    gateKeeper->GetGate()->UnregisterObject( coordinatorServer );

  gateKeeper = 0;
  coordinatorServer = 0;
}



void CoordinatorServerJob::PostLocalAnnounceTask( LocalAnnounceTask * _task )
{
  threading::MutexLock lock( announceTaskMutex );
  NI_ASSERT( pendingAnnounceTask == 0, "Duplicate local announce task" );
  pendingAnnounceTask = _task;
}



void CoordinatorServerJob::Work( volatile bool & isRunning )
{
  while ( isRunning )
  {
    LocalAnnounceTask * task = 0;
    { threading::MutexLock lock( announceTaskMutex ); task = pendingAnnounceTask; pendingAnnounceTask = 0; }
    if ( task )
    {
      task->Run( coordinatorServer.Get() );
      delete task;
    }

    coordinatorServer->Step();
    gateKeeper->Poll();
    nival::sleep( 10 );
  }
}

} //namespace Coordinator
