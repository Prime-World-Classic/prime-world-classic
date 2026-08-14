#ifndef RUDP_RDPPROTO_H_INCLUDED
#define RUDP_RDPPROTO_H_INCLUDED

#include "System/EnumToString.h"
#include <stdint.h>

namespace ni_udp
{

namespace proto
{

typedef unsigned char Byte;
typedef int Int32;
typedef long long Int64;
typedef unsigned int UInt32;
typedef unsigned long long UInt64;

//static const Int32 VERSION = 1;


namespace EPktType
{
  enum Enum
  {
    HandshakeInit         = 0,
    HandshakeInitAck      = 1,
    HandshakeAck          = 2,
    HandshakeRefused      = 3,
    RetryHandshake        = 4,
    Datagram              = 5,
    DatagramChunk         = 6,
    DatagramAck           = 7,
    DatagramRaw           = 8,
    Shutdown              = 9,
    ShutdownAck           = 10,
    Ping                  = 11,
    Pong                  = 12
  };

  NI_ENUM_DECL_STD;
}

static UInt32 Version = 1;
static Int32 SeqIndexClamp = 32768; //1024 is just for debugging

inline Int32 ClampSeqIndex( Int32 _idx ) { return _idx % SeqIndexClamp; }
inline Int32 NextSeqIndex( Int32 _idx ) { return ClampSeqIndex( _idx + 1 ); }

Int32 SeqIdxOffsetInWindow( Int32 _wndStart, Int32 _wndSize, Int32 _idx, Int32 _seqIdxClamp );

Int32 SeqIdxShortestDist( Int32 _from, Int32 _to, Int32 _seqIdxClamp ); // to - from


#pragma pack(push, 1)

// Explicit types instead of bit-fields to guarantee identical wire format
// across MSVC (Windows client) and GCC (Linux server). Bit-field layout is
// compiler-dependent and caused cross-platform connection failures.
struct Header
{
  uint8_t   type;       // 1 byte
  uint16_t  index;      // 2 bytes (little-endian)
  uint16_t  sourceMux;  // 2 bytes (little-endian)
  uint16_t  destMux;    // 2 bytes (little-endian)
  // Total: 7 bytes, deterministic layout on all compilers

  Header( EPktType::Enum _type, UInt32 _srcMux, UInt32 _destMux, UInt32 _index ) :
  type( (uint8_t)_type ),
  index( (uint16_t)_index ),
  sourceMux( (uint16_t)_srcMux ),
  destMux( (uint16_t)_destMux )
  {}
};

#pragma pack(pop)

} // namespace proto

inline const char * PktTypeToString( proto::EPktType::Enum _t ) { return proto::EPktType::ToString( _t ); }
inline const char * PktTypeToString( int _t ) { return proto::EPktType::ToString( (proto::EPktType::Enum)_t ); }

} //namespace ni_udp

#endif //RUDP_RDPPROTO_H_INCLUDED
