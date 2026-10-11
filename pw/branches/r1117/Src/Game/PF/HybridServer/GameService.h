#ifndef GAMESERVICE_H_INCLUDED
#define GAMESERVICE_H_INCLUDED

#include "HybridServerDispencer.h"
#include "ServerAppBase/NivalService.h"
#include "Server/Roll/RRollBalancer.auto.h"
#include "System/BlockData/BlockData.h"
#include "RPC/IfaceRequester.h"


namespace StatisticService
{
  class GameStatClient;
}


namespace HybridServer
{

class IServicePollCallback : public IBaseInterfaceMT
{
  NI_DECLARE_CLASS_1( IServicePollCallback, IBaseInterfaceMT );
public:
  virtual void PollCallback() = 0;
  // Suggested poll interval (same units as game_polling_interval). 0 (default)
  // means "use the default interval" (REPORT_server_profiling.md, B2).
  virtual int PollIntervalHint() { return 0; }
};


class GameService : public Transport::BaseService, public IServicePollCallback
{
  NI_DECLARE_REFCOUNT_CLASS_2( GameService, Transport::BaseService, IServicePollCallback );

public:
  GameService( const Transport::ServiceParams & _svcParams, const Transport::CustomServiceParams & _customParams );

  HybridServerDispencer * GetServer() const { return server; }

private:
  ~GameService();

  Transport::TServiceId DefineReconnectSvcId();
  void CreateMagicBlockDataWriter();
  void CleanupMagicBlockDataWriter();

  //Transport::BaseService
  virtual void ThreadMain( volatile bool & _isRunning );
  virtual void OnConfigReload();

  //IServicePollCallback
  virtual void PollCallback();
  virtual int PollIntervalHint();

  int _RegisterDebugVarCounter(const wchar_t* name, const char* alias, bool toCumulate);

  StrongMT<HybridServerDispencer>   server;
  NLogg::CChannelLogger*            logStream;
  StrongMT<NLogg::BasicTextFileDumper> logDumper;
  StrongMT<rpc::IfaceRequester<roll::RIBalancer> >  rollBalancer;
  StrongMT<StatisticService::GameStatClient> statistics;

  nvl::CPtr<nvl::INodeManager>      cpNodeManager;
  nvl::CPtr<nvl::ITerminator>       cpReplaysTerminator;
};

} //namespace HybridServer


namespace Peered
{

FORCE_INIT_FACTORY( ClientInfo );
FORCE_INIT_FACTORY( ClientStartInfo );
FORCE_INIT_FACTORY( SAuxUserData );
FORCE_INIT_FACTORY( SAuxData );
FORCE_INIT_FACTORY( SClientStatistics );

}

#endif //GAMESERVICE_H_INCLUDED
