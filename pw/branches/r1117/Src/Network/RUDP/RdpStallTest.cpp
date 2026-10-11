// Standalone regression test for the ni_udp (RUDP) send path.
//
// Production incident (2026-10-05): a client's lobby channel opened fine, the
// first datagrams reached the server, and then the channel did nothing but
// re-send the same two datagrams for 40 seconds until the retransmit limit
// killed it. The pending IServerInstance::ConnectToWebLobby RPC sat in the
// outgoing queue and never went out, so the lobby never created the game and
// the client hung on the lobby screen.
//
// What the server side of that session shows: the client's datagrams arrive
// (the server's receive window advances), the server ACKs every one of them,
// and none of those ACKs (nor the pings) ever reach the client. The transport
// reproduced that situation below: data flows one way, DatagramAck packets are
// swallowed.
//
// Build (linux): see RdpStallTest.application, then
//   ./build_linux/RdpStallTest
// Exit code 0 = all cases pass.

#include "stdafx.h"
#include "Rdp.h"
#include "SockSrvLocal.h"
#include "RdpProto.h"
#include "System/NiTimer.h"
#include "System/RandomInterface.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <list>
#include <vector>


namespace
{

int g_acksSent = 0;
int g_acksDropped = 0;



class TestRnd : public ni_rnd::IGenerator, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( TestRnd, ni_rnd::IGenerator, BaseObjectST );
public:
  explicit TestRnd( unsigned _seed ) : state( _seed ? _seed : 1 ) {}

  virtual unsigned Next()
  {
    state = state * 1103515245u + 12345u;
    return ( state >> 8 ) & 0x7fffffffu;
  }

private:
  unsigned state;
};



// Wraps a local socket and swallows DatagramAck packets on the way out: from
// the transport's point of view that is a path where data gets through but
// acknowledgements do not. _dropEveryNthAck = 1 drops every ACK, 3 every third.
class AckSinkSocket : public ni_udp::ISocket, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( AckSinkSocket, ni_udp::ISocket, BaseObjectMT );
public:
  AckSinkSocket( ni_udp::ISocket * _sock, int _dropEveryNthAck ) :
  socket( _sock ), dropEveryNthAck( _dropEveryNthAck ) {}

  virtual ni_udp::ESocketStatus::Enum Status() { return socket->Status(); }
  virtual ni_udp::TAuxData            AuxData() const { return socket->AuxData(); }
  virtual const ni_udp::NetAddr &     LocalAddr() const { return socket->LocalAddr(); }
  virtual void                        Close() { socket->Close(); }

  virtual void SendDatagram( const ni_udp::NetAddr & _destAddr, const void * _data, size_t _size )
  {
    if ( _size >= sizeof( ni_udp::proto::Header ) )
    {
      const ni_udp::proto::Header * hdr = (const ni_udp::proto::Header *)_data;
      if ( (ni_udp::proto::EPktType::Enum)hdr->type == ni_udp::proto::EPktType::DatagramAck )
      {
        ++g_acksSent;
        if ( dropEveryNthAck && ( ( g_acksSent % dropEveryNthAck ) == 0 ) )
        {
          ++g_acksDropped;
          return;
        }
      }
    }

    socket->SendDatagram( _destAddr, _data, _size );
  }

private:
  StrongMT<ni_udp::ISocket> socket;
  const int                 dropEveryNthAck;
};



class TestSocketFactory : public ni_udp::IRdpSocketFactory, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( TestSocketFactory, ni_udp::IRdpSocketFactory, BaseObjectMT );
public:
  TestSocketFactory( ni_udp::LocalSocketServer * _srv, const char * _addr, unsigned _port, int _dropEveryNthAck ) :
  sockSrv( _srv ), addr( _addr ), port( _port ), dropEveryNthAck( _dropEveryNthAck ) {}

  virtual StrongMT<ni_udp::ISocket> OpenSocket( ni_udp::ISocketCallback * _cb )
  {
    StrongMT<ni_udp::ISocket> sock = sockSrv->Open( _cb, ni_udp::NetAddr( addr.c_str(), port ), 0 );
    if ( dropEveryNthAck )
      return new AckSinkSocket( sock, dropEveryNthAck );
    return sock;
  }

private:
  StrongMT<ni_udp::LocalSocketServer> sockSrv;
  const std::string                   addr;
  const unsigned                      port;
  const int                           dropEveryNthAck;
};



class ConnCallback : public ni_udp::IRdpConnectionCallback, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( ConnCallback, ni_udp::IRdpConnectionCallback, BaseObjectMT );
public:
  virtual void OnConnectionEstablished( ni_udp::IRdpConnection * _conn ) { ++established; }
  virtual void OnConnectionClosed( ni_udp::IRdpConnection * _conn ) { ++closed; }
  virtual void OnConnectionFailed( ni_udp::IRdpConnection * _conn ) { ++failed; }

  virtual void OnDatagram( ni_udp::IRdpConnection * _conn, const void * _data, size_t _size, timer::Time _absRecvTime )
  {
    datagrams.push_back( std::string( (const char *)_data, _size ) );
  }

  int established = 0;
  int closed = 0;
  int failed = 0;
  std::list<std::string> datagrams;
};



class ListenCallback : public ni_udp::IRdpListenContextCallback, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( ListenCallback, ni_udp::IRdpListenContextCallback, BaseObjectMT );
public:
  explicit ListenCallback( ConnCallback * _cb ) : connCallback( _cb ) {}

  virtual ni_udp::IRdpConnectionCallback * OnConnectionEstablished( ni_udp::IRdpConnection * _conn, const ni_udp::NetAddr & _remoteAddr )
  {
    accepted.push_back( _conn );
    return connCallback;
  }

  StrongMT<ConnCallback> connCallback;
  std::vector<StrongMT<ni_udp::IRdpConnection> > accepted;
};



struct Harness
{
  StrongMT<timer::FixedTimer>             clock;
  StrongMT<ni_udp::LocalSocketServer>     sockSrv;
  StrongMT<ni_udp::Rdp>                   server, client;
  StrongMT<ConnCallback>                  serverConnCb, clientConnCb;
  StrongMT<ListenCallback>                listenCb;
  StrongMT<ni_udp::IRdpListenContext>     listenCtx;
  StrongMT<ni_udp::IRdpConnection>        clientConn;

  void Start( int _dropEveryNthAck, unsigned _initWindowSize )
  {
    clock = new timer::FixedTimer;
    sockSrv = new ni_udp::LocalSocketServer( clock );

    ni_udp::RdpOptions opt;
    opt.logEvents = 0;
    if ( _initWindowSize )
      opt.cc.initWindowSize = _initWindowSize;

    Strong<ni_rnd::IGenerator> rnd = new TestRnd( 201 );

    server = new ni_udp::Rdp( true );
    client = new ni_udp::Rdp( true );

    StrongMT<ni_udp::IRdpSocketFactory> srvFact = new TestSocketFactory( sockSrv, "192.168.0.1", 27000, _dropEveryNthAck );
    StrongMT<ni_udp::IRdpSocketFactory> cliFact = new TestSocketFactory( sockSrv, "192.168.0.2", 27000, 0 );

    server->Init( srvFact, opt, rnd, clock );
    client->Init( cliFact, opt, rnd, clock );

    serverConnCb = new ConnCallback;
    listenCb = new ListenCallback( serverConnCb );
    listenCtx = server->Listen( 100, listenCb );

    clientConnCb = new ConnCallback;
    clientConn = client->Connect( ni_udp::NetAddr( "192.168.0.1", 27000 ), 100, clientConnCb );

    poll( 0.2f, 4 );
  }

  void poll( timer::Time _dt, int _steps )
  {
    for ( int i = 0; i < _steps; ++i )
    {
      clock->Set( clock->Now() + _dt, 0 );
      sockSrv->Poll();
      server->UnitTestPoll();
      client->UnitTestPoll();
      sockSrv->Poll();
    }
  }

  bool Ready() const { return clientConn->Status() == ni_udp::EConnStatus::Ready; }
};



int g_failures = 0;

void Check( bool _ok, const char * _what )
{
  printf( "  %s %s\n", _ok ? "ok  " : "FAIL", _what );
  if ( !_ok )
    ++g_failures;
}



// Baseline: a healthy path must keep working after the congestion control
// changes - everything is delivered in order and the connection stays up.
void test_no_loss_smoke()
{
  printf( "test_no_loss_smoke\n" );

  Harness h;
  h.Start( 0, 0 );
  Check( h.Ready(), "connection established" );

  const int count = 20;
  for ( int i = 0; i < count; ++i )
  {
    char buf[16];
    sprintf( buf, "msg%02d", i );
    h.clientConn->SendDatagram( buf, strlen( buf ) );
  }

  h.poll( 0.2f, 40 );

  Check( (int)h.serverConnCb->datagrams.size() == count, "all 20 datagrams delivered" );

  bool inOrder = h.serverConnCb->datagrams.size() == (size_t)count;
  int idx = 0;
  for ( std::list<std::string>::const_iterator it = h.serverConnCb->datagrams.begin(); it != h.serverConnCb->datagrams.end(); ++it, ++idx )
  {
    char buf[16];
    sprintf( buf, "msg%02d", idx );
    if ( *it != buf )
      inOrder = false;
  }
  Check( inOrder, "delivered in order" );
  Check( h.clientConn->Status() == ni_udp::EConnStatus::Ready, "connection still ready" );
  Check( h.clientConnCb->failed == 0 && h.serverConnCb->failed == 0, "no connection failure" );
}



// The production scenario: the lobby channel sends a handful of datagrams
// (attach, iface query, RequestServerInstance, ConnectToWebLobby), the server
// receives them but none of its ACKs get back. The channel must keep carrying
// new application data instead of wedging on retransmissions.
void test_ack_loss_does_not_wedge_the_channel()
{
  printf( "test_ack_loss_does_not_wedge_the_channel\n" );

  Harness h;
  h.Start( 1, 0 );
  Check( h.Ready(), "connection established" );

  h.clientConn->SendDatagram( "attach", 6 );
  h.clientConn->SendDatagram( "iface", 5 );
  h.clientConn->SendDatagram( "inst", 4 );
  h.clientConn->SendDatagram( "join", 4 );

  h.poll( 0.2f, 10 );

  Check( g_acksDropped > 0, "ACKs were actually swallowed" );

  const char * expected[] = { "attach", "iface", "inst", "join" };
  int delivered = 0;
  for ( int i = 0; i < 4; ++i )
  {
    bool found = false;
    for ( std::list<std::string>::const_iterator it = h.serverConnCb->datagrams.begin(); it != h.serverConnCb->datagrams.end(); ++it )
      if ( *it == expected[i] )
        found = true;
    if ( found )
      ++delivered;
  }

  printf( "       delivered %d/4, acks sent %d / dropped %d\n", delivered, g_acksSent, g_acksDropped );
  Check( delivered == 4, "every datagram reaches the peer even though no ACK comes back" );
}



// Partial ACK loss must not break delivery either.
void test_partial_ack_loss_recovers()
{
  printf( "test_partial_ack_loss_recovers\n" );

  Harness h;
  h.Start( 3, 0 );
  Check( h.Ready(), "connection established" );

  const int count = 10;
  for ( int i = 0; i < count; ++i )
  {
    char buf[16];
    sprintf( buf, "pkt%02d", i );
    h.clientConn->SendDatagram( buf, strlen( buf ) );
  }

  h.poll( 0.2f, 40 );

  int delivered = 0;
  for ( int i = 0; i < count; ++i )
  {
    char buf[16];
    sprintf( buf, "pkt%02d", i );
    for ( std::list<std::string>::const_iterator it = h.serverConnCb->datagrams.begin(); it != h.serverConnCb->datagrams.end(); ++it )
      if ( *it == buf )
      {
        ++delivered;
        break;
      }
  }

  printf( "       delivered %d/%d, acks sent %d / dropped %d\n", delivered, count, g_acksSent, g_acksDropped );
  Check( delivered == count, "all datagrams delivered with every third ACK lost" );
  Check( h.clientConnCb->failed == 0, "connection not failed" );
}

} //namespace



int main()
{
  test_no_loss_smoke();
  test_ack_loss_does_not_wedge_the_channel();
  test_partial_ack_loss_recovers();

  printf( "\n%s (%d failure(s))\n", g_failures ? "FAILED" : "PASSED", g_failures );
  return g_failures ? 1 : 0;
}
