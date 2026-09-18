#ifndef NEWLOGIN_CONFIG_H_INCLUDED
#define NEWLOGIN_CONFIG_H_INCLUDED

#include "ServerAppBase/ConfigProvider.h"


namespace newLogin
{

struct SConfig
{
  float       sessionKeyExpire;
  float       helloWaitTimeout;
  float       processingTimeout;
  float       emptyLinksTimeout;
  unsigned    svcLinksLimit;
  float       loadUpdatePeriod;
  unsigned    rdpLogEvents;
  int         udpSockBufferSize;
  int         threadPriority;

  // Web session registry (the backend): address and shared key. Empty values
  // keep the server_ip.h fallback (local development).
  string      webSessionHost;
  int         webSessionPort;
  string      webSessionKey;

  SConfig();
};



typedef Transport::IConfigProvider<SConfig> IConfigProvider;
typedef IConfigProvider::TConfig Config;

StrongMT<IConfigProvider> CreateConfigFromStatics();

} //namespace newLogin

#endif // NEWLOGIN_CONFIG_H_INCLUDED
