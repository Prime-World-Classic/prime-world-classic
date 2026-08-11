// Stub for Linux (no STREAMS support)
#ifndef ACE_SPIPE_STREAM_H
#define ACE_SPIPE_STREAM_H
#include /**/ "ace/pre.h"
#include "ace/sockaddr.h"
#include "ace/SOCK_IO.h"
#include "ace/OS_NS_stropts.h"
#if !defined (ACE_LACKS_PRAGMA_ONCE)
# pragma once
#endif
ACE_BEGIN_VERSIONED_NAMESPACE_DECL
class ACE_Export ACE_SPIPE_Addr : public ACE_Addr {
public:
  ACE_SPIPE_Addr (void) {}
  int dump (void) const { return 0; }
};
class ACE_Export ACE_SPIPE_Stream : public ACE_SOCK_IO {
public:
  ACE_SPIPE_Stream (void) {}
  ACE_SPIPE_Stream (int h) : ACE_SOCK_IO (h) {}
  int send_handle (const ACE_HANDLE &) const { return -1; }
  int recv_handle (ACE_HANDLE &) const { return -1; }
  int send (const void *, size_t) { return -1; }
  int send_n (const void *, size_t) { return -1; }
  int recv (void *, size_t) { return -1; }
  int recv_n (void *, size_t) { return -1; }
  int recv_msg (ACE_Str_Buf &) { return -1; }
  int send_msg (const ACE_Str_Buf &) { return -1; }
};
ACE_END_VERSIONED_NAMESPACE_DECL
#include /**/ "ace/post.h"
#endif
