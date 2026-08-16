#include "stdafx.h"
#include "ClientControlService.h"
#include "ClientControlLogic.h"
#include "ClientControlTypes.h"
#include "LClientControlInterface.auto.h"
#include "Network/LSessionKeyRegisterIface.auto.h"
#include "System/InlineProfiler.h"
#include "ClientControlLog.h"


namespace clientCtl
{

static NDebug::DebugVar<int> s_CcuCounter( "ActiveUsers", "UserManager" );


InstanceSvc::InstanceSvc( const Transport::ServiceParams & _svcParams, const Transport::CustomServiceParams & _customParams ) :
Transport::BaseService( _svcParams, _customParams ),
prevReportedCcu( 0 )
{
  RegisterBackendAttach<IInterface, LIInterface>();
  RegisterBackendAttach<Login::ISessionKeyRegister, Login::LISessionKeyRegister>();
  RegisterBackendAttach<Login::IAddSessionKeyCallback, Login::LIAddSessionKeyCallback>();
  config = CreateConfigFromStatics();

  logic = new Logic( config, Now() );

  RegisterFrontendObject<IInterface>( logic, serviceIds::Gate );

  RegisterPerfCounter( "CCU", &s_CcuCounter );
}



InstanceSvc::~InstanceSvc()
{
  UnregisterPerfCounter( "CCU" );
}



void InstanceSvc::Poll( timer::Time _now )
{
  unsigned ccu = 0;
  logic->Poll( _now, ccu );

  if ( ccu != prevReportedCcu )
  {
    prevReportedCcu = ccu;
    s_CcuCounter.SetValue( (int)ccu );
  }
}



void InstanceSvc::OnConfigReload()
{
  config->ReloadConfig();
}

} //namespace clientCtl

// The auto-generated remote factories for the callback interfaces
// (clientCtl::RIUserPresenceCallback, clientCtl::RILoginSvcAllocationCallback)
// live in RClientControlRemote.auto.cpp, which is archived into the static
// libClientControl-st. Nothing else references that object, so the linker
// drops it and the factory static initializers (which register the factories
// in the global remote factory container) never run. Without the factory the
// receiving side cannot build the RI stub for a callback passed by reference
// (e.g. newlogin -> clientctrl UserEnters), and the call is silently dropped.
// Referencing the RegisterRemoteFactory specializations below forces the
// object to be linked in. (The FORCE_INIT_FACTORY macro does not work on GCC:
// the unused 'static int initX = dummyX;' gets optimized away.)
namespace clientCtl { class RIUserPresenceCallback; class RILoginSvcAllocationCallback; }
namespace rpc
{
template<> void RegisterRemoteFactory( clientCtl::RIUserPresenceCallback* instance );
template<> void RegisterRemoteFactory( clientCtl::RILoginSvcAllocationCallback* instance );
}
namespace
{
  struct ClientCtlRemoteFactoryLinker
  {
    ClientCtlRemoteFactoryLinker()
    {
      rpc::RegisterRemoteFactory<clientCtl::RIUserPresenceCallback>( 0 );
      rpc::RegisterRemoteFactory<clientCtl::RILoginSvcAllocationCallback>( 0 );
    }
  };
  static ClientCtlRemoteFactoryLinker s_ClientCtlRemoteFactoryLinker;
}

