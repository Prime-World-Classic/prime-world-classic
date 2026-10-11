#pragma once

#include "Network/TransportTypes.h"
#include "Network/Address.h"
#include "Network/TransportDefaults.h"
#include "Network/LoginTypes.h"
#include "Server/NewLogin/NewLoginTypes.h"

struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;

namespace Transport
{
  class MessageFactory;
  _interface IChannel;
  _interface IChannelListener;

  namespace EStatus {
    enum Enum {
      NONE = 0,
      OK,
      FAIL,
      CRITICAL_FAIL,
    };
  }
  _interface IClientTransportSystem : public IBaseInterfaceMT
  {
    NI_DECLARE_CLASS_1( IClientTransportSystem, IBaseInterfaceMT );
    virtual int GetUserId() const = 0;

    virtual StrongMT<Transport::IChannel> OpenChannel( Transport::TServiceId interfaceId, 
      unsigned int pingperiod = Defaults::GetPingPeriod(), unsigned int to = Defaults::GetOpenChannelTimeout()) = 0;
    virtual void GetNewAcceptedChannels(vector< StrongMT< Transport::IChannel > > & _chnls) { _chnls; }

    // playerKey: web-session player key (sha256(user_id+sessionToken+api_key),
    // from the launcher URL). Empty = legacy login by nickname. The session
    // data is delivered by the server in GetLoginReply().webSession.
    virtual void Login( const Network::NetAddress& loginServerAddress, const nstl::string& login, const nstl::string& _password, const nstl::string& sessionKey="", const nstl::string& playerKey="", Login::LoginType::Enum _loginType = Login::LoginType::ORDINARY ) = 0;
    virtual void Logout() = 0;
    virtual Login::ELoginResult::Enum GetLoginResult() const = 0;
    // Full login reply (webSession is empty unless the server sent it).
    virtual newLogin::LoginReply GetLoginReply() const { return newLogin::LoginReply(); }
    virtual EStatus::Enum GetStatus() = 0; // � ���������, �� CRITICAL_FAIL ����� ���������
    virtual TServiceId GetSessionPath() const { return TServiceId(); };
    virtual Network::NetAddress GetRelayAddress() const = 0;
    virtual Network::NetAddress GetSecondaryRelayAddress() const = 0;
    virtual void Step() = 0;
  };

}
