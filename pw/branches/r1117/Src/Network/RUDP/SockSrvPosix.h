#ifndef RUDP_SOCKSRVPOSIX_H_INCLUDED
#define RUDP_SOCKSRVPOSIX_H_INCLUDED

#include "ISockSrv.h"
#include "System/JobThread.h"
#include "UdpSocket.h"


namespace ni_udp
{

class BlockingUdpSocketServerPosix : public ISocketServer, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2( BlockingUdpSocketServerPosix, ISocketServer, BaseObjectMT );

public:
  BlockingUdpSocketServerPosix( int _threadPriority, int _bufferSize );

  virtual StrongMT<ISocket> Open( ISocketCallback * _cb, const NetAddr & _bindAddr, TAuxData _auxData );

private:
  const int threadPriority;
  const int bufferSize;
};



class BlockingUdpSocketPosix : public ISocket, public UdpSocket
{
  NI_DECLARE_REFCOUNT_CLASS_2( BlockingUdpSocketPosix, ISocket, UdpSocket );

public:
  BlockingUdpSocketPosix( ISocketCallback * _cb, const NetAddr & _bindAddr, SOCKET _s, TAuxData _auxData, unsigned _priority );
  ~BlockingUdpSocketPosix();

  virtual ESocketStatus::Enum   Status();
  virtual TAuxData              AuxData() const { return auxData; }
  virtual const NetAddr &       LocalAddr() const { return UdpSocket::BindAddr(); }
  virtual void                  SendDatagram( const NetAddr & _destAddr, const void * _data, size_t _size );
  virtual void                  Close();

  bool ListenSocket();

private:
  WeakMT<ISocketCallback>         callback;
  const TAuxData                  auxData;
  StrongMT<threading::JobThread>  workerThread;
};

} //namespace ni_udp

#endif //RUDP_SOCKSRVPOSIX_H_INCLUDED
