#include "stdafx.h"
#include "SockSrvPosix.h"
#include "System/InlineProfiler.h"
#include <atomic>


namespace ni_udp
{

class BlockingUdpSocketServerWorkerPosix : public threading::IThreadJob, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( BlockingUdpSocketServerWorkerPosix, threading::IThreadJob, BaseObjectMT );

public:
  BlockingUdpSocketServerWorkerPosix( BlockingUdpSocketPosix * _sock ) :
  owner( _sock )
  {
  }

private:
  //threading::IThreadJob
  virtual void Work( volatile bool & isRunning )
  {
    NI_PROFILE_THREAD;

    while ( isRunning )
    {
      NI_PROFILE_BLOCK( "Worker Loop" );

      if ( !owner->ListenSocket() )
        break;
    }
  }

  BlockingUdpSocketPosix * owner;
};



BlockingUdpSocketServerPosix::BlockingUdpSocketServerPosix( int _threadPriority, int _bufferSize ) :
threadPriority( _threadPriority ),
bufferSize( _bufferSize )
{
}



StrongMT<ISocket> BlockingUdpSocketServerPosix::Open( ISocketCallback * _cb, const NetAddr & _bindAddr, TAuxData _auxData )
{
  SOCKET s = UdpSocket::CreateSocket( _bindAddr, UdpSocket::Options( UdpSocket::Options::BlockingMode, bufferSize, bufferSize ) );
  if ( s == INVALID_SOCKET )
    return 0;

  StrongMT<BlockingUdpSocketPosix> sock = new BlockingUdpSocketPosix( _cb, _bindAddr, s, _auxData, threadPriority );

  MessageTrace( "Socket opened. addr=%s, sock=%d", _bindAddr, s );

  return sock.Get();
}



BlockingUdpSocketPosix::BlockingUdpSocketPosix( ISocketCallback * _cb, const NetAddr & _bindAddr, SOCKET _s, TAuxData _auxData, unsigned _priority ) :
UdpSocket( _s, _bindAddr),
callback( _cb ),
auxData( _auxData )
{
  workerThread = new threading::JobThread( new BlockingUdpSocketServerWorkerPosix( this ), "UdpWorkerBlocking" );
  workerThread->SetPriority( _priority );
}



BlockingUdpSocketPosix::~BlockingUdpSocketPosix()
{
  workerThread = 0;
}



ESocketStatus::Enum BlockingUdpSocketPosix::Status()
{
  return UdpSocket::SocketIsValid() ? ESocketStatus::Ready : ESocketStatus::Failure;
}



void BlockingUdpSocketPosix::SendDatagram( const NetAddr & _destAddr, const void * _data, size_t _size )
{
  UdpSocket::SendDatagram( _destAddr, _data, _size );
}



void BlockingUdpSocketPosix::Close()
{
  workerThread->AsyncStop();
  workerThread->Wait( 3000 );

  //We DO NOT close socket object. It can be used by some parallel thread
}



bool BlockingUdpSocketPosix::ListenSocket()
{
  NI_PROFILE_FUNCTION;

  timer::Time now = timer::Now();

  const int BufSz = 65536;
  char buf[BufSz];

  sockaddr from;
  socklen_t fromLen = sizeof( from );
  memset( &from, 0, fromLen );

  int recvRes = 0;
  {
    NI_PROFILE_BLOCK( "recvfrom" );
    recvRes = ::recvfrom( AccessHandle(), buf, BufSz, 0, &from, &fromLen );
  }

  timer::Time dbgRecvEnd = timer::Now();

  if ( ( recvRes != 0 ) && ( recvRes != -1 ) && ( fromLen == sizeof( sockaddr_in ) ) )
  {
    NI_PROFILE_BLOCK( "OnDatagram callback" );
    sockaddr_in & remoteAddr = *(sockaddr_in *)&from;

    StrongMT<ISocketCallback> cb = callback.Lock();
    if ( cb )
      cb->OnDatagram( this, NetAddr( remoteAddr ), buf, recvRes, dbgRecvEnd );
  }

  return true;
}

} //namespace ni_udp
