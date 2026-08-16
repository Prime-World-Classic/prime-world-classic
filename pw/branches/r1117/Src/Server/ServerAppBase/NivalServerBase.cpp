#include "stdafx.h"
#include "NivalServerBase.h"
#include "ServerCfg.h"

#include "Server/LogMonitorAgent/LogMonitorAgent.h"
#include "Server/LogMonitorAgent/LogMonitorConfig.h"

#include "Network/ClusterConfiguration.h"
#include "Network/Initializer.h"
#include "Network/StreamAllocator.h"

#include "Coordinator/ConfigServiceOption.h"
#include "Coordinator/CoordinatorServerIface.h"
#include "Coordinator/ServicesStartInfo.h"

#include "transport/TLTransportModule.h"
#include "transport/TLCfg.h"

#include "System/sleep.h"

#include "RdpTransport/RdpTransport.h"
#include "RdpTransport/RdpFrontendTransport.h"

#include "Network/RUDP/UdpAddr.h"

namespace Transport
{

//TEMP: Used for external services (e.g. 'statistics')
class NullServiceSpawner : public DefaultServiceSpawnerBase
{
  NI_DECLARE_REFCOUNT_CLASS_1( NullServiceSpawner, DefaultServiceSpawnerBase );
public:
  NullServiceSpawner( const TServiceId & _serviceClass, const Coordinator::SInterfacePolicy & _policy ) :
  DefaultServiceSpawnerBase( _serviceClass, 0, _policy )
  {}

  virtual bool ServiceIsExternal() const { return true; }
  virtual IServerRunner * SpawnService() const { return 0; }
};


namespace
{

// In the local (single-process) deployment the coordinator and all services
// live in one process, so the normal RPC handshake (the coordinator tells
// the client which services to start, the client reports them back when up)
// is performed with direct in-process calls instead. This proxy stands in
// for the remote ICoordinatorClientRemote object that the coordinator keeps
// references to: service start/stop are no-ops (the local services are
// already started here, not by the coordinator) and route updates are
// forwarded to the real coordinator client, so the local address
// translators receive the permanent coordinator routes.
class LocalCoordinatorClientProxy :
  public Coordinator::ICoordinatorClientRemote,
  public Coordinator::IRegisterClientCallback,
  public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_3( LocalCoordinatorClientProxy, Coordinator::ICoordinatorClientRemote, Coordinator::IRegisterClientCallback, BaseObjectMT );

public:
  LocalCoordinatorClientProxy( Coordinator::CoordinatorClient * _coordClient ) :
  coordClient( _coordClient ),
  registerResult( -1 ),
  serverId( Coordinator::INVALID_SERVER_ID )
  {}

  // ICoordinatorClientRemote
  virtual void AddRoute( const Transport::TServiceId & _service, Coordinator::SvcNetAddresses const & _addrs )
  {
    if ( StrongMT<Coordinator::CoordinatorClient> cli = coordClient.Lock() )
      cli->LocalAddRoute( _service, _addrs );
  }
  virtual void RemoveRoute( const Transport::TServiceId & _service )
  {
    if ( StrongMT<Coordinator::CoordinatorClient> cli = coordClient.Lock() )
      cli->LocalRemoveRoute( _service );
  }
  virtual void StartService( const Transport::TServiceId & _service ) {} // services are already started locally
  virtual void StopService( const Transport::TServiceId & _service ) {}
  virtual void SoftStopService( const Transport::TServiceId & _service ) {}
  virtual void ReloadConfig( const Transport::TServiceId & _service ) {}
  virtual unsigned int Ping( unsigned int _param ) { return _param; }

  // IRegisterClientCallback
  virtual void OnRegisterClient( int _result, Coordinator::ServerIdT _clientid )
  {
    registerResult = _result;
    serverId = _clientid;
  }

  int RegisterResult() const { return registerResult; }
  Coordinator::ServerIdT ServerId() const { return serverId; }

private:
  WeakMT<Coordinator::CoordinatorClient> coordClient;
  int registerResult;
  Coordinator::ServerIdT serverId;
};


// The coordinator state (client/svc contexts) is only touched from the
// coordinator job thread (Step() + the RPC dispatch), so the local
// register/announce handshake is posted there as a one-shot task.
class LocalCoordinatorAnnounceTask : public Coordinator::CoordinatorServerJob::LocalAnnounceTask
{
public:
  LocalCoordinatorAnnounceTask( StrongMT<LocalCoordinatorClientProxy> const & _proxy, Coordinator::ServerDef const & _serverDef, Coordinator::ServicesStartInfo const & _startInfo ) :
  proxy( _proxy ),
  serverDef( _serverDef ),
  startInfo( _startInfo )
  {}

  virtual void Run( Coordinator::CoordinatorServer * _server )
  {
    _server->RegisterClient( Coordinator::INVALID_SERVER_ID, proxy.Get(), serverDef, proxy.Get() );
    if ( proxy->RegisterResult() >= 0 )
    {
      startInfo.srvid_ = proxy->ServerId();
      _server->ServicesStarted( startInfo );
    }
  }

private:
  StrongMT<LocalCoordinatorClientProxy> proxy;
  Coordinator::ServerDef serverDef;
  Coordinator::ServicesStartInfo startInfo;
};

} // namespace





NivalServerBase::NivalServerBase() :
startCoordinator( false ),
showNetStat( false )
{
  svcpath = HostServer::Cfg::GetSvcPath().c_str();
}



NivalServerBase::~NivalServerBase() 
{
  Shutdown();
}



bool NivalServerBase::Startup( const TStartList & _startList, const TServerCmdLine & _serverCmdLine )
{
  LoadConfigs( _startList );

  StartLogMonitor( _startList );

  // Network initialization
  netDriver = Network::Initialize();
  netDriver->SetTrafficType( Network::EDriverTrafficType::Light ); // ���� Heavy == 750� ������� �� ������ ���������� Connection

  netDriver->SetStreamAllocator( new Network::StreamAllocator() );

  coordinatorClientRunner = new Coordinator::CoordinatorClientRunner( svcpath, netDriver );

  // When running as coordinator, skip connecting client to avoid self-communication deadlock
  if ( startCoordinator )
  {
    if ( !StartTransport( _startList, _serverCmdLine ) )
      return false;
    if ( !StartCoordinatorService() )
      return false;
    // In local mode CoordinatorClientRunner::Open() is skipped, so the explicit
    // route to the coordinator itself (normally added there) is missing and every
    // IfaceRequester targeting "coordinator" fails with an empty address. Add it.
    coordinatorClientRunner->GetClient()->GetInterface()->AddExplicitRoute(
      Transport::ENetInterface::Coordinator,
      Coordinator::SvcNetAddresses( Network::GetCoordinatorAddress(), Network::NetAddress() ) );
    // Start all services locally (no coordinator RPC needed)
    StartLocalServices( _startList, _serverCmdLine );
    return true;
  }

  if ( !StartTransport( _startList, _serverCmdLine ) )
    return false;

  if ( !SpawnServices( _startList, _serverCmdLine ) )
    return false;

  return true;
}



bool NivalServerBase::StartTransport( const TStartList & _startList, const TServerCmdLine & _serverCmdLine )
{
  NI_VERIFY( coordinatorClientRunner, "", return false );

  if ( rdp_transport::RdpTransportEnabled() )
  {
    MessageTrace( "Starting RDP transport" );

    bool hadExternalServices = false;
    for ( TStartList::const_iterator it = _startList.begin(); it != _startList.end(); ++it )
      if ( (*it).spawner->Policy().flags & Coordinator::ESvcFlags::EXTERNAL )
        hadExternalServices = true;

    StrongMT<Coordinator::ICoordinatorClient> coordClient = coordinatorClientRunner->GetClient()->GetInterface();

    ni_udp::NetAddr backendAddr( Network::GetBackendIPAddr().c_str(), Network::GetFirstServerPortBack() );
    backendTransport = new rdp_transport::BackendTransport( Transport::GetGlobalMessageFactory(), coordClient->GetBackendAddressTranslator(), backendAddr, "backend" );

    if ( hadExternalServices )
    {
      ni_udp::NetAddr frontendAddr( Network::GetFrontendIPAddr().c_str(), Network::GetFirstServerPortFront() );
      frontendTransport = new rdp_transport::PrimaryFrontend( Transport::GetGlobalMessageFactory(), coordClient->GetFrontendAddressTranslator(), frontendAddr );
    }
  }
  else
  {
    MessageTrace( "Starting TCP/IP transport" );

    TL::Cfg cfg;
    cfg.firstServerPort = Network::GetFirstServerPortBack();
    cfg.mf_ = Transport::GetGlobalMessageFactory();
    cfg.at_ = coordinatorClientRunner->GetClient()->GetInterface()->GetBackendAddressTranslator();
    cfg.threads_ = TL::GlobalCfg::GetThreads();
    cfg.loglvl_ = TL::GlobalCfg::GetLogLevel();
    cfg.terabit_loglvl_ = TL::GlobalCfg::GetTerabitLogLevel();
    cfg.read_block_size_ = TL::GlobalCfg::GetReadBlockSize();
    cfg.so_rcvbuf_ = TL::GlobalCfg::GetSoRcvbuf();
    cfg.so_sndbuf_ = TL::GlobalCfg::GetSoSndbuf();
    cfg.disableNagleAlgorithm_ = TL::GlobalCfg::GetDisableNagleAlgorithm();
    cfg.so_keepalive_ = TL::GlobalCfg::GetSoKeepalive();
    cfg.mbHeapDumpFreq_ = TL::GlobalCfg::GetMessageBlockHeapDumpFreq();
    cfg.mbHeapPerConnection_ = TL::GlobalCfg::GetMessageBlockHeapPerConnection();
    cfg.mbNotypeUseThreshold_ = TL::GlobalCfg::GetMessageBlockNotypeUseThreshold();
    cfg.mbWriteUseThreshold_ = TL::GlobalCfg::GetMessageBlockWriteUseThreshold();
    cfg.mbReadUseThreshold_ = TL::GlobalCfg::GetMessageBlockReadUseThreshold();
    cfg.logWrittenBytes_ = TL::GlobalCfg::GetLogWrittenBytes();
    cfg.checkActivityTimeout_ = TL::GlobalCfg::GetCheckActivityTimeout();

    aioTransport = TransportLayer::CreateTransportSystem(cfg);
    backendTransport = aioTransport;
  }

  return true;
}



bool NivalServerBase::StartCoordinatorService()
{
  coordinatorSvcJob = new Coordinator::CoordinatorServerJob( BackendTransport(), Network::GetCoordinatorAddress() );

  for ( ServiceSpawners::iterator it = Spawners().begin(); it != Spawners().end(); ++it )
  {
    IServiceSpawner * spn = it->second;
    coordinatorSvcJob->GetServer()->SetServicePolicy( spn->ServiceClass(), spn->Policy() );
  }

  coordinatorSvcThread = new threading::JobThread( coordinatorSvcJob, "CoordSvc" );
  return true;
}



void NivalServerBase::StartLogMonitor( const TStartList & _startList )
{
  set<string> svcClasses;

  for ( TStartList::const_iterator it = _startList.begin(); it != _startList.end(); ++it )
  {
    StrongMT<IServiceSpawner> spawner = it->spawner;
    const TServiceId & serviceClass = spawner->ServiceClass();
    svcClasses.insert( serviceClass.c_str() );
  }

  string svcNames;
  for ( set<string>::const_iterator it = svcClasses.begin(); it != svcClasses.end(); ++it )
  {
    if ( !svcNames.empty() )
      svcNames += "_";
    svcNames += *it;
  }

  //special ugly case
  if ( svcNames.empty() )
    svcNames = "coordinator";

  StrongMT<logMonitor::Agent> agent = new logMonitor::Agent( logMonitor::CreateConfigFromStatics(), svcNames.c_str() );

  logMonitorAgent = new logMonitor::AgentParallelPoller( agent );
}



void NivalServerBase::LoadConfigs( const TStartList & _startList )
{
  set<string> configNames;
  for ( TStartList::const_iterator it = _startList.begin(); it != _startList.end(); ++it )
  {
    const ServiceStartupInfo & ssi = *it;

    if ( ssi.spawner->Policy().flags & Coordinator::ESvcFlags::HAS_NO_CFG )
      continue;

    configNames.insert( ssi.spawner->ServiceClass().c_str() );
  }

  MessageTrace( "%d configs to load", configNames.size() );
  for( set<string>::const_iterator it = configNames.begin(); it != configNames.end(); ++it )
  {
    MessageTrace( "Loading config for service '%s'...", it->c_str() );
    Coordinator::LoadServiceConfig( it->c_str() );
  }
}



bool NivalServerBase::SpawnServices( const TStartList & _startList, const TServerCmdLine & _serverCmdLine )
{
  StrongMT<Coordinator::CoordinatorClient> cli = coordinatorClientRunner->GetClient();

  cli->SetIps( Network::GetBackendIPAddr(), Network::GetFrontendIPAddr() );
  MessageTrace( "IP addresses: backend=%s, frontend=%s", Network::GetBackendIPAddr(), Network::GetFrontendIPAddr() );

  for ( TStartList::const_iterator it = _startList.begin(); it != _startList.end(); ++it )
  {
    StrongMT<IServiceSpawner> spawner = it->spawner;
    const TServiceId & serviceClass = spawner->ServiceClass();

    const unsigned instanceNumber = it->instanceNumber ? it->instanceNumber : spawner->DefaultInstancesNumber();
    for ( unsigned i = 0; i < instanceNumber; ++i )
    {
      StrongMT<IServerRunner> runner = spawner->SpawnService();
      if ( !runner ) {
        ErrorTrace( "Null service spawner. svc_class=%s", serviceClass.c_str() );
        continue;
      }

      ServiceOptions svcOpts;
      svcOpts.commandLine = _serverCmdLine;

      // �������� �����, ����������� � ������� ������� (+���������� �����, ����������� �� ���� ��������)
      const Coordinator::TConfigServiceOptions & configOptions = Coordinator::GetConfigServiceOptions();
      for( int idx = 0; idx < configOptions.size(); ++idx ) {
        const Coordinator::SConfigServiceOption * cfgOpt = configOptions[idx];
        if ( cfgOpt->IsGlobalOption() || ( serviceClass == cfgOpt->serviceId.c_str() ) )
          svcOpts.options.insert( cfgOpt->option );
      }
      
      cli->AddServiceInstance( spawner, runner, svcOpts );
    }
  }

  // Skip coordinator client connection when running as coordinator service
  if ( !startCoordinator )
  {
    coordinatorClientRunner->Open( backendTransport, frontendTransport, Network::GetCoordinatorAddress(), "pvx" );
    coordinatorClientThread = new threading::JobThread( coordinatorClientRunner, "Coordinator Client" );

    while( cli->state() == Coordinator::ClientState::OPENING )
      nival::sleep( 10 );

    if ( cli->state() != Coordinator::ClientState::OPEN )
    {
      LOG_C(0) << "Can't connect to coordinator service OR coordinator refuse client";
      return false;
    }
  }

  return true;
}



void NivalServerBase::StartLocalServices( const TStartList & _startList, const TServerCmdLine & _serverCmdLine )
{
  MessageTrace( "=== StartLocalServices called ===" );
  StrongMT<Coordinator::CoordinatorClient> cli = coordinatorClientRunner->GetClient();
  cli->SetIps( Network::GetBackendIPAddr(), Network::GetFrontendIPAddr() );
  MessageTrace( "IP addresses: backend=%s, frontend=%s", Network::GetBackendIPAddr(), Network::GetFrontendIPAddr() );

  for ( TStartList::const_iterator it = _startList.begin(); it != _startList.end(); ++it )
  {
    StrongMT<IServiceSpawner> spawner = it->spawner;
    const TServiceId & serviceClass = spawner->ServiceClass();

    // Skip coordinator service - already started
    if ( serviceClass == "coordinator" )
      continue;

    const unsigned instanceNumber = it->instanceNumber ? it->instanceNumber : spawner->DefaultInstancesNumber();
    for ( unsigned i = 0; i < instanceNumber; ++i )
    {
      char buf[128];
      snprintf( buf, sizeof(buf), "%s_%02u", serviceClass.c_str(), i );
      Transport::TServiceId serviceId( buf );

      // Manually start service like CoordinatorClient::StartService
      const bool extService = ( spawner->Policy().flags & Coordinator::ESvcFlags::EXTERNAL ) ? true : false;

      ServiceOptions svcOpts;
      svcOpts.commandLine = _serverCmdLine;
      const Coordinator::TConfigServiceOptions & configOptions = Coordinator::GetConfigServiceOptions();
      for( int idx = 0; idx < configOptions.size(); ++idx ) {
        const Coordinator::SConfigServiceOption * cfgOpt = configOptions[idx];
        if ( cfgOpt->IsGlobalOption() || ( serviceClass == cfgOpt->serviceId.c_str() ) )
          svcOpts.options.insert( cfgOpt->option );
      }

      Transport::ServiceParams params( svcOpts );
      params.serviceId = serviceId;
      params.backendTransport = backendTransport;
      params.frontendTransport = extService ? frontendTransport : 0;
      params.driver = netDriver;
      params.coordClient = cli->GetInterface();

      // Spawn and start a new runner directly
      StrongMT<IServerRunner> runner = spawner->SpawnService();
      if ( runner )
      {
        runner->StartInstance( params );
        MessageTrace( "Local service started: %s (ext=%d)", serviceId.c_str(), extService );
        // Store runner to keep it alive for the lifetime of the server
        localRunners.push_back( runner );
      }
    }
  }

  // Register this process with the local coordinator and report the services
  // that are already started, so the service appearance publisher notifies
  // the subscribers (the gamebalancer) exactly as in the multi-process
  // deployment. The handshake is posted as a one-shot task, because the
  // coordinator state may only be touched from the coordinator job thread.
  if ( coordinatorSvcJob && coordinatorSvcJob->GetServer() )
  {
    Coordinator::ServerDef serverDef;
    serverDef.svcPathBase = svcpath;
#if defined( NV_WIN_PLATFORM )
    serverDef.pid = ::GetCurrentProcessId();
#elif defined( NV_LINUX_PLATFORM )
    serverDef.pid = ::getpid();
#endif

    Coordinator::ServicesStartInfo startInfo;

    Transport::IAddressTranslator * backTrans = cli->GetInterface()->GetBackendAddressTranslator();
    Transport::IAddressTranslator * frontTrans = cli->GetInterface()->GetFrontendAddressTranslator();

    for ( TStartList::const_iterator it = _startList.begin(); it != _startList.end(); ++it )
    {
      StrongMT<IServiceSpawner> spawner = it->spawner;
      const TServiceId & serviceClass = spawner->ServiceClass();

      if ( serviceClass == "coordinator" )
        continue;

      const unsigned instanceNumber = it->instanceNumber ? it->instanceNumber : spawner->DefaultInstancesNumber();
      const bool multiple = ( spawner->Policy().type == Coordinator::EServiceInstancing::MULTIPLE );
      const unsigned announcedCount = multiple ? instanceNumber : 1;

      for ( unsigned i = 0; i < announcedCount; ++i )
      {
        char localBuf[128];
        snprintf( localBuf, sizeof( localBuf ), "%s_%02u", serviceClass.c_str(), i );

        // The instance id as the coordinator would assign it
        Transport::TServiceId coordId( serviceClass );
        if ( multiple )
        {
          char coordBuf[128];
          snprintf( coordBuf, sizeof( coordBuf ), "%s/%u", serviceClass.c_str(), i + 1 );
          coordId = coordBuf;
        }

        serverDef.svcdefs.push_back( Coordinator::ServiceDef( serviceClass, Coordinator::defaultServiceRole ) );

        Network::NetAddress backAddr = backTrans ? backTrans->GetSvcAddress( localBuf ) : Network::NetAddress();
        Network::NetAddress frontAddr = frontTrans ? frontTrans->GetSvcAddress( localBuf ) : Network::NetAddress();
        startInfo.serviceDefs.push_back( Coordinator::ServiceStartInfo( coordId, backAddr, frontAddr ) );

        MessageTrace( "Local announce: %s -> %s (backend=%s frontend=%s)", localBuf, coordId.c_str(), backAddr.c_str(), frontAddr.c_str() );
      }
    }

    StrongMT<LocalCoordinatorClientProxy> proxy = new LocalCoordinatorClientProxy( cli.Get() );
    coordinatorSvcJob->PostLocalAnnounceTask( new LocalCoordinatorAnnounceTask( proxy, serverDef, startInfo ) );
    MessageTrace( "Local coordinator announce task posted (%d services)", (int)startInfo.serviceDefs.size() );
  }
}



void NivalServerBase::Shutdown()
{
  spawners.clear();
  localRunners.clear();

  coordinatorClientRunner = 0;
  coordinatorClientThread = 0;

  if (aioTransport)
    aioTransport->Fini();
  aioTransport = 0;

  backendTransport = 0;
  frontendTransport = 0;
}



void NivalServerBase::RegisterService( IServiceSpawner * _spawner )
{
  const Transport::TServiceId & svcCls = _spawner->ServiceClass();
  bool ext = _spawner->ServiceIsExternal();
  int pol = _spawner->Policy().type;

  MessageTrace( "Registering service. svc_class=%s, ext=%d, policy=%d", svcCls.c_str(), ext, pol );

  StrongMT<IServiceSpawner> & spawner = spawners[svcCls];
  NI_ASSERT( !spawner.Valid(), NI_STRFMT( "Duplicate service spawners: svc_class='%s'", svcCls.c_str() ) );
  spawner = _spawner;
}



void NivalServerBase::RegisterExternalService( const TServiceId & _serviceClass, const Coordinator::SInterfacePolicy & _policy )
{
  RegisterService( new NullServiceSpawner( _serviceClass, _policy ) );
}



IServiceSpawner * NivalServerBase::FindSvcSpawner( const TServiceId & id ) const
{
  ServiceSpawners::const_iterator it = spawners.find( id );
  if ( it == spawners.end() )
    return 0;

  IServiceSpawner * spwn = it->second;

  if ( spwn->ServiceIsExternal() )
    return 0;

  return spwn;
}

} // namespace Transport

// Force the linker to keep ALL auto-generated remote-entity factory
// registrations (DEFINE_RE_FACTORY) that are part of the UniServerApp build.
// Under GCC/ELF the archive members holding the static factory objects are
// dropped because nothing references them, so the receiving side cannot
// instantiate the RI entity class (it arrives as NULL and the first use
// crashes). MSVC is not affected. Referencing the
// RegisterRemoteFactory<T> specializations below keeps the members linked,
// so the factories register themselves at process start.

namespace chat { class RIChatClientCallback; }
namespace chat { class RIChatManagement; }
namespace chat { class RIOpenChannelCallback; }
namespace chat { class RIOpenSessionCallback; }
namespace clientCtl { class RIInterface; }
namespace clientCtl { class RILoginSvcAllocationCallback; }
namespace clientCtl { class RIUserPresenceCallback; }
namespace Coordinator { class RICoordinatorClientRemote; }
namespace Coordinator { class RICoordinatorServerRemote; }
namespace Coordinator { class RIRegisterClientCallback; }
namespace Coordinator { class RIServiceAppearancePublisher; }
namespace Coordinator { class RIServiceAppearanceSubscriber; }
namespace GameBalancer { class RIBalancer; }
namespace GameBalancer { class RIBalancerCallback; }
namespace HybridServer { class RIGameServerAllocator; }
namespace HybridServer { class RIGameServerAllocatorNotify; }
namespace HybridServer { class RIGameServerDispenser; }
namespace HybridServer { class RIGameServerDispenserCallback; }
namespace lobby { class RIEntrance; }
namespace lobby { class RILobbyUser; }
namespace lobby { class RIServerInstance; }
namespace lobby { class RISessionHybridLink; }
namespace Login { class RIAddSessionKeyCallback; }
namespace Login { class RISessionKeyRegister; }
namespace MatchMaking { class RIClient; }
namespace MatchMaking { class RISession; }
namespace mmaking { class RILiveMMaking; }
namespace mmaking { class RILiveMMakingClient; }
namespace Monitoring { class RIMonitor; }
namespace NDebug { class RDebugVarReporter; }
namespace Peered { class RIGameClient; }
namespace Peered { class RIGameClientReconnect; }
namespace Peered { class RIGameServer; }
namespace Peered { class RIGameServerInternal; }
namespace Peered { class RIGameServerReconnect; }
namespace rdp_transport { class RIFrontendAgent; }
namespace rdp_transport { class RIFrontendAgentRemote; }
namespace Relay { class RIBalancer; }
namespace Relay { class RIBalancerCallback; }
namespace Relay { class RIIncomingClientNotifySink; }
namespace Relay { class RIOutgoingClientNotifySink; }
namespace roll { class RIBalancer; }
namespace roll { class RIClient; }
namespace roll { class RIInstance; }
namespace socialLobby { class RIDevSocLobby; }
namespace socialLobby { class RINotify; }
namespace socialLobby { class RIPvxAcknowledge; }
namespace socialLobby { class RIPvxInterface; }
namespace socialLobby { class RIPvxSvc; }
namespace socialLobby { class RISocialInterface; }
namespace socialLobby { class RIUserContext; }
namespace UserManager { class RIPrepareUserEnvCallback; }
namespace UserManager { class RIUserManager; }
namespace UserManager { class RIUserNotificationPublisher; }
namespace UserManager { class RIUserNotificationSubscriber; }

namespace rpc
{
template<> void RegisterRemoteFactory( chat::RIChatClientCallback* instance );
template<> void RegisterRemoteFactory( chat::RIChatManagement* instance );
template<> void RegisterRemoteFactory( chat::RIOpenChannelCallback* instance );
template<> void RegisterRemoteFactory( chat::RIOpenSessionCallback* instance );
template<> void RegisterRemoteFactory( clientCtl::RIInterface* instance );
template<> void RegisterRemoteFactory( clientCtl::RILoginSvcAllocationCallback* instance );
template<> void RegisterRemoteFactory( clientCtl::RIUserPresenceCallback* instance );
template<> void RegisterRemoteFactory( Coordinator::RICoordinatorClientRemote* instance );
template<> void RegisterRemoteFactory( Coordinator::RICoordinatorServerRemote* instance );
template<> void RegisterRemoteFactory( Coordinator::RIRegisterClientCallback* instance );
template<> void RegisterRemoteFactory( Coordinator::RIServiceAppearancePublisher* instance );
template<> void RegisterRemoteFactory( Coordinator::RIServiceAppearanceSubscriber* instance );
template<> void RegisterRemoteFactory( GameBalancer::RIBalancer* instance );
template<> void RegisterRemoteFactory( GameBalancer::RIBalancerCallback* instance );
template<> void RegisterRemoteFactory( HybridServer::RIGameServerAllocator* instance );
template<> void RegisterRemoteFactory( HybridServer::RIGameServerAllocatorNotify* instance );
template<> void RegisterRemoteFactory( HybridServer::RIGameServerDispenser* instance );
template<> void RegisterRemoteFactory( HybridServer::RIGameServerDispenserCallback* instance );
template<> void RegisterRemoteFactory( lobby::RIEntrance* instance );
template<> void RegisterRemoteFactory( lobby::RILobbyUser* instance );
template<> void RegisterRemoteFactory( lobby::RIServerInstance* instance );
template<> void RegisterRemoteFactory( lobby::RISessionHybridLink* instance );
template<> void RegisterRemoteFactory( Login::RIAddSessionKeyCallback* instance );
template<> void RegisterRemoteFactory( Login::RISessionKeyRegister* instance );
template<> void RegisterRemoteFactory( MatchMaking::RIClient* instance );
template<> void RegisterRemoteFactory( MatchMaking::RISession* instance );
template<> void RegisterRemoteFactory( mmaking::RILiveMMaking* instance );
template<> void RegisterRemoteFactory( mmaking::RILiveMMakingClient* instance );
template<> void RegisterRemoteFactory( Monitoring::RIMonitor* instance );
template<> void RegisterRemoteFactory( NDebug::RDebugVarReporter* instance );
template<> void RegisterRemoteFactory( Peered::RIGameClient* instance );
template<> void RegisterRemoteFactory( Peered::RIGameClientReconnect* instance );
template<> void RegisterRemoteFactory( Peered::RIGameServer* instance );
template<> void RegisterRemoteFactory( Peered::RIGameServerInternal* instance );
template<> void RegisterRemoteFactory( Peered::RIGameServerReconnect* instance );
template<> void RegisterRemoteFactory( rdp_transport::RIFrontendAgent* instance );
template<> void RegisterRemoteFactory( rdp_transport::RIFrontendAgentRemote* instance );
template<> void RegisterRemoteFactory( Relay::RIBalancer* instance );
template<> void RegisterRemoteFactory( Relay::RIBalancerCallback* instance );
template<> void RegisterRemoteFactory( Relay::RIIncomingClientNotifySink* instance );
template<> void RegisterRemoteFactory( Relay::RIOutgoingClientNotifySink* instance );
template<> void RegisterRemoteFactory( roll::RIBalancer* instance );
template<> void RegisterRemoteFactory( roll::RIClient* instance );
template<> void RegisterRemoteFactory( roll::RIInstance* instance );
template<> void RegisterRemoteFactory( socialLobby::RIDevSocLobby* instance );
template<> void RegisterRemoteFactory( socialLobby::RINotify* instance );
template<> void RegisterRemoteFactory( socialLobby::RIPvxAcknowledge* instance );
template<> void RegisterRemoteFactory( socialLobby::RIPvxInterface* instance );
template<> void RegisterRemoteFactory( socialLobby::RIPvxSvc* instance );
template<> void RegisterRemoteFactory( socialLobby::RISocialInterface* instance );
template<> void RegisterRemoteFactory( socialLobby::RIUserContext* instance );
template<> void RegisterRemoteFactory( UserManager::RIPrepareUserEnvCallback* instance );
template<> void RegisterRemoteFactory( UserManager::RIUserManager* instance );
template<> void RegisterRemoteFactory( UserManager::RIUserNotificationPublisher* instance );
template<> void RegisterRemoteFactory( UserManager::RIUserNotificationSubscriber* instance );
}
namespace
{
  struct RemoteFactoriesLinker
  {
    RemoteFactoriesLinker()
    {
      rpc::RegisterRemoteFactory<chat::RIChatClientCallback> ( 0 );
      rpc::RegisterRemoteFactory<chat::RIChatManagement> ( 0 );
      rpc::RegisterRemoteFactory<chat::RIOpenChannelCallback> ( 0 );
      rpc::RegisterRemoteFactory<chat::RIOpenSessionCallback> ( 0 );
      rpc::RegisterRemoteFactory<clientCtl::RIInterface> ( 0 );
      rpc::RegisterRemoteFactory<clientCtl::RILoginSvcAllocationCallback> ( 0 );
      rpc::RegisterRemoteFactory<clientCtl::RIUserPresenceCallback> ( 0 );
      rpc::RegisterRemoteFactory<Coordinator::RICoordinatorClientRemote> ( 0 );
      rpc::RegisterRemoteFactory<Coordinator::RICoordinatorServerRemote> ( 0 );
      rpc::RegisterRemoteFactory<Coordinator::RIRegisterClientCallback> ( 0 );
      rpc::RegisterRemoteFactory<Coordinator::RIServiceAppearancePublisher> ( 0 );
      rpc::RegisterRemoteFactory<Coordinator::RIServiceAppearanceSubscriber> ( 0 );
      rpc::RegisterRemoteFactory<GameBalancer::RIBalancer> ( 0 );
      rpc::RegisterRemoteFactory<GameBalancer::RIBalancerCallback> ( 0 );
      rpc::RegisterRemoteFactory<HybridServer::RIGameServerAllocator> ( 0 );
      rpc::RegisterRemoteFactory<HybridServer::RIGameServerAllocatorNotify> ( 0 );
      rpc::RegisterRemoteFactory<HybridServer::RIGameServerDispenser> ( 0 );
      rpc::RegisterRemoteFactory<HybridServer::RIGameServerDispenserCallback> ( 0 );
      rpc::RegisterRemoteFactory<lobby::RIEntrance> ( 0 );
      rpc::RegisterRemoteFactory<lobby::RILobbyUser> ( 0 );
      rpc::RegisterRemoteFactory<lobby::RIServerInstance> ( 0 );
      rpc::RegisterRemoteFactory<lobby::RISessionHybridLink> ( 0 );
      rpc::RegisterRemoteFactory<Login::RIAddSessionKeyCallback> ( 0 );
      rpc::RegisterRemoteFactory<Login::RISessionKeyRegister> ( 0 );
      rpc::RegisterRemoteFactory<MatchMaking::RIClient> ( 0 );
      rpc::RegisterRemoteFactory<MatchMaking::RISession> ( 0 );
      rpc::RegisterRemoteFactory<mmaking::RILiveMMaking> ( 0 );
      rpc::RegisterRemoteFactory<mmaking::RILiveMMakingClient> ( 0 );
      rpc::RegisterRemoteFactory<Monitoring::RIMonitor> ( 0 );
      rpc::RegisterRemoteFactory<NDebug::RDebugVarReporter> ( 0 );
      rpc::RegisterRemoteFactory<Peered::RIGameClient> ( 0 );
      rpc::RegisterRemoteFactory<Peered::RIGameClientReconnect> ( 0 );
      rpc::RegisterRemoteFactory<Peered::RIGameServer> ( 0 );
      rpc::RegisterRemoteFactory<Peered::RIGameServerInternal> ( 0 );
      rpc::RegisterRemoteFactory<Peered::RIGameServerReconnect> ( 0 );
      rpc::RegisterRemoteFactory<rdp_transport::RIFrontendAgent> ( 0 );
      rpc::RegisterRemoteFactory<rdp_transport::RIFrontendAgentRemote> ( 0 );
      rpc::RegisterRemoteFactory<Relay::RIBalancer> ( 0 );
      rpc::RegisterRemoteFactory<Relay::RIBalancerCallback> ( 0 );
      rpc::RegisterRemoteFactory<Relay::RIIncomingClientNotifySink> ( 0 );
      rpc::RegisterRemoteFactory<Relay::RIOutgoingClientNotifySink> ( 0 );
      rpc::RegisterRemoteFactory<roll::RIBalancer> ( 0 );
      rpc::RegisterRemoteFactory<roll::RIClient> ( 0 );
      rpc::RegisterRemoteFactory<roll::RIInstance> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RIDevSocLobby> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RINotify> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RIPvxAcknowledge> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RIPvxInterface> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RIPvxSvc> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RISocialInterface> ( 0 );
      rpc::RegisterRemoteFactory<socialLobby::RIUserContext> ( 0 );
      rpc::RegisterRemoteFactory<UserManager::RIPrepareUserEnvCallback> ( 0 );
      rpc::RegisterRemoteFactory<UserManager::RIUserManager> ( 0 );
      rpc::RegisterRemoteFactory<UserManager::RIUserNotificationPublisher> ( 0 );
      rpc::RegisterRemoteFactory<UserManager::RIUserNotificationSubscriber> ( 0 );
    }
  };
  static RemoteFactoriesLinker s_RemoteFactoriesLinker;
}
