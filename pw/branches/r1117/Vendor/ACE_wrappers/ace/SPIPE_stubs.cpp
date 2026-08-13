#include "ace/SPIPE_Addr.h"
#include "ace/SPIPE.h"
#include "ace/SPIPE_Acceptor.h"
ACE_BEGIN_VERSIONED_NAMESPACE_DECL
ACE_SPIPE_Addr::ACE_SPIPE_Addr(void) {}
ACE_SPIPE_Addr::ACE_SPIPE_Addr(const ACE_SPIPE_Addr &sa) {}
ACE_SPIPE_Addr::ACE_SPIPE_Addr(const ACE_TCHAR *rp, gid_t, uid_t) {}
void ACE_SPIPE_Addr::set_addr(void *addr, int len) {}
int ACE_SPIPE_Addr::addr_to_string(ACE_TCHAR *, size_t) const { return -1; }
int ACE_SPIPE_Addr::string_to_addr(const ACE_TCHAR *) { return -1; }
void ACE_SPIPE_Addr::dump(void) const {}
int ACE_SPIPE_Addr::set(const ACE_SPIPE_Addr &) { return -1; }
void *ACE_SPIPE_Addr::get_addr(void) const { return 0; }
ACE_SPIPE::ACE_SPIPE() {}
int ACE_SPIPE::close() { return -1; }
ACE_SPIPE_Acceptor::ACE_SPIPE_Acceptor() {}
int ACE_SPIPE_Acceptor::open(const ACE_SPIPE_Addr &, int, int, int, int) { return -1; }
int ACE_SPIPE_Acceptor::close() { return -1; }
int ACE_SPIPE_Acceptor::accept(ACE_SPIPE_Stream &, ACE_SPIPE_Addr *, ACE_Time_Value *, bool, bool) { return -1; }
ACE_END_VERSIONED_NAMESPACE_DECL
