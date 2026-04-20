#include "stdafx.h"
#include "UdpSocket.h"
#include "System/InlineProfiler.h"

#ifdef NI_PLATF_LINUX
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define closesocket close
#define WSAGetLastError() errno
#endif

namespace ni_udp
{

void UdpSocket::GlobalInit()
{
#ifndef NI_PLATF_LINUX
  WSADATA data;
  memset( &data, 0, sizeof( data ) );
  WSAStartup( MAKEWORD( 2, 2 ), &data );
#endif
}

void UdpSocket::GlobalShutdown()
{
#ifndef NI_PLATF_LINUX
  WSACleanup();
#endif
}

inline bool SetSockOptTimeval( SOCKET s, int opt )
{
  int to = 300;
  return ::setsockopt( s, SOL_SOCKET, opt, (char *)&to, sizeof( to ) ) == 0;
}

SOCKET UdpSocket::CreateSocket( const NetAddr & _bindAddr, const Options & _options )
{
  SOCKET s = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

  if ( s == INVALID_SOCKET )
  {
    ErrorTrace( "Failed to create socket. code=%d", WSAGetLastError() );
    return INVALID_SOCKET;
  }

  int on = ( _options.flags & Options::ReUseAddr ) ? 1 : 0;
  if ( ::setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof( on ) ) != 0 )
  {
    ErrorTrace( "Failed to set SO_REUSEADDR. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
    closesocket( s );
    return INVALID_SOCKET;
  }

  if ( _options.flags & Options::BlockingMode )
  {
    if ( !SetSockOptTimeval( s, SO_RCVTIMEO ) )
    {
      ErrorTrace( "Failed to set SO_RCVTIMEO. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
      closesocket( s );
      return INVALID_SOCKET;
    }
  
    if ( !SetSockOptTimeval( s, SO_SNDTIMEO ) )
    {
      ErrorTrace( "Failed to set SO_SNDTIMEO. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
      closesocket( s );
      return INVALID_SOCKET;
    }
  }

  if ( _options.sendBuffer )
  {
    int opt = 0;
    socklen_t len = sizeof( int );
    if ( ::getsockopt( s, SOL_SOCKET, SO_SNDBUF, (char *)&opt, &len ) == 0 )
      DebugTrace( "Previous SO_SNDBUF=%d", opt );
    opt = _options.sendBuffer;
    if ( ::setsockopt( s, SOL_SOCKET, SO_SNDBUF, (const char *)&opt, sizeof( int ) ) != 0 )
    {
      ErrorTrace( "Failed to set SO_SNDBUF. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
      closesocket( s );
      return INVALID_SOCKET;
    }
    opt = 0;
    len = sizeof( int );
    if ( ::getsockopt( s, SOL_SOCKET, SO_SNDBUF, (char *)&opt, &len ) == 0 )
      DebugTrace( "New SO_SNDBUF=%d", opt );
  }

  if ( _options.recvBuffer )
  {
    int opt = 0;
    socklen_t len = sizeof( int );
    if ( ::getsockopt( s, SOL_SOCKET, SO_RCVBUF, (char *)&opt, &len ) == 0 )
      DebugTrace( "Previous SO_RCVBUF=%d", opt );
    opt = _options.recvBuffer;
    if ( ::setsockopt( s, SOL_SOCKET, SO_RCVBUF, (const char *)&opt, sizeof( int ) ) != 0 )
    {
      ErrorTrace( "Failed to set SO_RCVBUF. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
      closesocket( s );
      return INVALID_SOCKET;
    }
    opt = 0;
    len = sizeof( int );
    if ( ::getsockopt( s, SOL_SOCKET, SO_RCVBUF, (char *)&opt, &len ) == 0 )
      DebugTrace( "New SO_RCVBUF=%d", opt );
  }

#ifndef NI_PLATF_LINUX
  unsigned long nbon = ( _options.flags & Options::BlockingMode ) ? 0 : 1;
  if ( ::ioctlsocket( s, FIONBIO, &nbon ) != 0 )
  {
    ErrorTrace( "Failed to set FIONBIO. code=%d, value=%d, addr=%s", WSAGetLastError(), nbon, _bindAddr );
    closesocket( s );
    return INVALID_SOCKET;
  }
#else
  int flags = fcntl(s, F_GETFL, 0);
  if (!(_options.flags & Options::BlockingMode)) {
      flags |= O_NONBLOCK;
  } else {
      flags &= ~O_NONBLOCK;
  }
  if ( fcntl( s, F_SETFL, flags ) != 0 )
  {
    ErrorTrace( "Failed to set FIONBIO. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
    closesocket( s );
    return INVALID_SOCKET;
  }
#endif

  if ( ::bind( s, (const sockaddr *)&_bindAddr, sizeof( _bindAddr ) ) != 0 )
  {
    WarningTrace( "Failed to bind socket. code=%d, addr=%s", WSAGetLastError(), _bindAddr );
    closesocket( s );
    return INVALID_SOCKET;
  }

  return s;
}

UdpSocket::UdpSocket( SOCKET _s, const NetAddr & _bindAddr ) :
bindAddr( _bindAddr ),
sock( _s )
{
}

UdpSocket::~UdpSocket()
{
  Close();
}

void UdpSocket::Close()
{
  if ( sock != INVALID_SOCKET )
  {
    closesocket( sock );
    sock = INVALID_SOCKET;
  }
}

bool UdpSocket::SocketIsValid() const
{
  return ( sock != INVALID_SOCKET );
}

void UdpSocket::SendDatagram( const NetAddr & _destAddr, const void * _data, size_t _size )
{
  NI_PROFILE_HEAVY_FUNCTION;

  if ( sock == INVALID_SOCKET )
    return;

  if ( ::sendto( sock, (const char *)_data, _size, 0, (const sockaddr *)&_destAddr, sizeof( _destAddr ) ) != (int)_size )
  {
    int code = WSAGetLastError();
    ErrorTrace( "Failed to send datagram. sock=%d, code=%d, size=%d, addr=%s", sock, code, _size, _destAddr );
    return;
  }
}

} //namespace ni_udp