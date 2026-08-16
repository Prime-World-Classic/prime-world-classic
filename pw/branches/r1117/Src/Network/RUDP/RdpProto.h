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

// Explicit types to guarantee an identical, deterministic wire format on all
// compilers (MSVC/GCC). The layout MUST match the MSVC bit-field layout of
// the original struct from the `main` branch (the shipped Windows client was
// built from it):
//   UInt32 type:8;        -> bits  0-7  of 4-byte unit 1
//   UInt32 index:16;      -> bits  8-23 of unit 1
//                            bits 24-31 of unit 1 = padding (unused)
//   UInt32 sourceMux:16;  -> bits  0-15 of 4-byte unit 2
//   UInt32 destMux:16;    -> bits 16-31 of unit 2
// MSVC allocates same-base-type bit-fields in units of the base type, so
// sizeof(Header) == 8 there. The previous 7-byte explicit layout was rejected
// by the client's size check ("Datagram is too small. size=7") and, besides
// that, had sourceMux/destMux off by one byte.
struct Header
{
  uint8_t   type;       // 1 byte
  uint16_t  index;      // 2 bytes (little-endian)
  uint8_t   pad;        // 1 byte (bit-field padding in the MSVC layout, always 0)
  uint16_t  sourceMux;  // 2 bytes (little-endian)
  uint16_t  destMux;    // 2 bytes (little-endian)
  // Total: 8 bytes, deterministic layout on all compilers

  Header( EPktType::Enum _type, UInt32 _srcMux, UInt32 _destMux, UInt32 _index ) :
  type( (uint8_t)_type ),
  index( (uint16_t)_index ),
  pad( 0 ),
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
