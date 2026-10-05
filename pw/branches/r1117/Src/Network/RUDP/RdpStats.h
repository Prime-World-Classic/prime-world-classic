#ifndef RUDP_RDPSTATS_H_INCLUDED
#define RUDP_RDPSTATS_H_INCLUDED

namespace ni_udp
{

class RdpStats : public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_1( RdpStats, BaseObjectMT );

public:
  RdpStats() :
  bytesWritten( 0 ), bytesRecieved( 0 ), bytesDelivered( 0 ),
  packetsWritten( 0 ), packetsRecieved( 0 ),
  datagramsDelivered( 0 ), datagramsQueued( 0 ), datagramsRecieved( 0 ),
  rawDatagramsSent( 0 ), rawDatagramsRecieved( 0 ),
  errors( 0 ), warnings( 0 ), retransmits( 0 )
  {}

  volatile long long     bytesWritten, bytesRecieved;
  volatile long long     bytesDelivered;
  volatile long          packetsWritten, packetsRecieved;
  volatile long          datagramsDelivered, datagramsQueued, datagramsRecieved;
  volatile long          rawDatagramsSent, rawDatagramsRecieved;
  volatile long          errors;
  volatile long          warnings;
  volatile long          retransmits;

#if defined( NV_WIN_PLATFORM )
  void Inc( volatile LONG RdpStats::*_field, int _inc = 1 )
  {
    InterlockedExchangeAdd( &(this->*_field), _inc );
  }

  void Inc( volatile LONGLONG RdpStats::*_field, int _inc = 1 )
  {
    NiInterlockedExchangeAdd64( &(this->*_field), _inc );
  }
#elif defined( NV_LINUX_PLATFORM )
  // Linux: atomic increment (stats are updated from socket worker and logic threads)
  void Inc( volatile long RdpStats::*_field, int _inc = 1 )
  {
    volatile long &f = this->*_field;
    __atomic_fetch_add( &f, _inc, __ATOMIC_RELAXED );
  }

  void Inc( volatile long long RdpStats::*_field, int _inc = 1 )
  {
    volatile long long &f = this->*_field;
    __atomic_fetch_add( &f, _inc, __ATOMIC_RELAXED );
  }
#endif
};



class RdpConnStats : public RdpStats
{
  NI_DECLARE_REFCOUNT_CLASS_1( RdpConnStats, RdpStats );

public:
  explicit RdpConnStats( RdpStats * _global ) :
  globalStats( _global )
  {}

  template <class T>
  void Inc( volatile T RdpStats::*_field, int _inc = 1 )
  {
    RdpStats::Inc( _field, _inc );
    globalStats->RdpStats::Inc( _field, _inc );
  }

private:
  StrongMT<RdpStats>  globalStats;
};

} //namespace ni_udp

#endif //RUDP_RDPSTATS_H_INCLUDED
