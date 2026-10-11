#include "stdafx.h"
#include "TransportBase.h"
#include "Network/AddressTranslator.h"
#include "System/InlineProfiler.h"
#include <map>
#include <string>

NI_DEFINE_REFCOUNT( Transport::IChannel );
NI_DEFINE_REFCOUNT( Transport::IFrontendTransportAuth );


namespace Transport
{

TransportSystemBase::TransportSystemBase()
{
}



TransportSystemBase::~TransportSystemBase()
{
}



// Diagnostics (untagged, reaches the main log on Linux): per-target failure
// counters to trace why inter-service channels fail to open.
static std::map<std::string, unsigned> s_openChannelFails;

StrongMT<IChannel> TransportSystemBase::OpenChannel( const Address & address, const TLoginData & loginData, unsigned int pingperiod, unsigned int to )
{
  NI_PROFILE_FUNCTION;

  if ( !addressTranslator )
  {
    if ( ++s_openChannelFails["<no-translator>"] % 1000 == 1 )
      ErrorTrace( "OpenChannel FAILED: no address translator (target=%s fails=%u)", address.target.c_str(), s_openChannelFails["<no-translator>"] );
    return 0;
  }

  Transport::Address addr(address);
  addr.target = addressTranslator->GetLastServiceInstance( address.target );
  Network::NetAddress naddr = addressTranslator->GetSvcAddress( addr.target );

  StrongMT<IChannel> chan = OpenChannelDirect( addr, loginData, naddr );
  if ( !chan )
  {
    unsigned & fails = s_openChannelFails[address.target.c_str()];
    if ( fails % 1000 == 0 )
      ErrorTrace( "OpenChannel FAILED: target=%s instance=%s naddr=%s fails=%u",
        address.target.c_str(), addr.target.c_str(), naddr.c_str(), ++fails );
  }
  else
  {
    s_openChannelFails.erase( address.target.c_str() );
    MessageTrace( "OpenChannel ok: target=%s instance=%s naddr=%s", address.target.c_str(), addr.target.c_str(), naddr.c_str() );
  }
  return chan;
}



StrongMT<IChannelListener> TransportSystemBase::CreateChannelListener( TServiceId interfaceId )
{
  NI_PROFILE_FUNCTION;

  if ( !addressTranslator )
    return 0;

  Network::NetAddress naddr = addressTranslator->GetSvcAddress( interfaceId );
  if ( naddr.empty() )
  {
    naddr = AllocateServerAddress( addressTranslator->GetServerIp() );

    addressTranslator->DefineRoute( interfaceId, naddr );
    naddr = addressTranslator->GetSvcAddress( interfaceId );
  }

  DebugTrace( "Creating service '%s' listener on ip=%s", interfaceId.c_str(), naddr );
  if ( naddr.empty() )
    return 0;

  return CreateChannelListenerDirect( interfaceId, naddr );
}

} //namespace Transport
