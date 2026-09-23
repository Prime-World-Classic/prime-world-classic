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

  // Shared key for the web-session player-key formula
  // (sha256(str(user_id)+token+key)) — the same key the back-end uses.
  string      webSessionKey;

  SConfig();
};



typedef Transport::IConfigProvider<SConfig> IConfigProvider;
typedef IConfigProvider::TConfig Config;

StrongMT<IConfigProvider> CreateConfigFromStatics();

} //namespace newLogin

#endif // NEWLOGIN_CONFIG_H_INCLUDED
