#ifndef RUDP_UDPADDR_H_INCLUDED
#define RUDP_UDPADDR_H_INCLUDED

#if defined( NV_WIN_PLATFORM )
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined( NV_LINUX_PLATFORM )
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "System/LogIpMode.h"

namespace ni_udp
{

typedef unsigned IpAddr;


struct NetAddr : public sockaddr_in
{
  NetAddr();
  NetAddr( const char * _addr, unsigned _port );
  NetAddr( const sockaddr_in & _addrin ) : sockaddr_in( _addrin ) {}
  NetAddr( const char * _combined );

  bool Set( const char * _addr, unsigned _port ); // pass _addr=0 for INADDR_ANY
  bool Set( const char * _combined );
  void SetPort( unsigned _port );
  
  bool operator < ( const NetAddr & that ) const;
  bool operator == ( const NetAddr & that ) const;

  IpAddr Address() const { return sin_addr.s_addr; }
  unsigned Port() const;
  void Clear();

  void Str( char * _buf, size_t _bufSz ) const;
  void StrIp( char * _buf, size_t _bufSz ) const;
  // Masked-log renderings (System/LogIpMode.h): hex IPv4 without a port
  // (0a4e0001 == 10.78.0.1, same byte order as Str()/StrIp() and as the launcher
  // protocol hex block) and port only, with the address left out.
  void StrHex( char * _buf, size_t _bufSz ) const;
  void StrPort( char * _buf, size_t _bufSz ) const;
};


// Log rendering of a textual address "a.b.c.d[:port][@mux]" into _buf. In builds
// that do not mask the string is copied unchanged; otherwise the address part is
// dropped and the remainder ("port[@mux]") is kept. Returns _buf.
const char * MaskAddrString( char * _buf, size_t _bufSz, const char * _addr );

} //namespace ni_udp



//Log formatting stuff
namespace text
{

template<>
class TArg<ni_udp::NetAddr> : public BasicArg
{
public:
  TArg( const ni_udp::NetAddr & _a ) : addr( _a ) {}

  virtual void Format( IBuffer * buffer, const SFormatSpecs & specs ) const
  {
    char buf[32];
    // Masked builds: the address is left out of the log, the port is kept.
    if ( NGlobal::GetLogIpMode() )
      addr.StrPort( buf, 32 );
    else
      addr.Str( buf, 32 );

    FormatString( buffer, buf, specs );
  }

  virtual bool AsInt( int & _intValue ) const { return false; }

private:
  const ni_udp::NetAddr & addr;
};

} //namespace text

#endif //RUDP_UDPADDR_H_INCLUDED
