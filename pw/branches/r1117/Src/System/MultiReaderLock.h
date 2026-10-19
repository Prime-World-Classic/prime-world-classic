#pragma once

#include <memory>
#include "noncopyable.h"
#include "auto_ptr_compat.h"

namespace threading
{

  class MultiReaderLock : private NonCopyable
{ 
public: 
  MultiReaderLock();
  ~MultiReaderLock();

  void LockForRead() const;
  void UnlockAfterRead() const;
  void LockForWrite() const;
  void UnlockAfterWrite() const;

private:
  class MultiReaderLockImpl;
  const NV_AUTO_PTR( MultiReaderLockImpl ) impl_;
};



class ReadLockGuard : public NonCopyable
{
  const MultiReaderLock & lock;

public:
  ReadLockGuard( const MultiReaderLock & _lock ) : lock( _lock ) { lock.LockForRead(); }
  ~ReadLockGuard() { lock.UnlockAfterRead(); }
};



class WriteLockGuard : public NonCopyable
{
  const MultiReaderLock & lock;

public:
  WriteLockGuard( const MultiReaderLock & _lock ) : lock( _lock ) { lock.LockForWrite(); }
  ~WriteLockGuard() { lock.UnlockAfterWrite(); }
};

} //namespace threading
