#include "stdafx.h"
#include "MultiReaderLock.h"


namespace threading
{

#ifdef WIN32
class MultiReaderLock::MultiReaderLockImpl
{ 
public: 
  MultiReaderLockImpl() : readers( 0 ),
                          noReaders( NULL )
  {
    ::InitializeCriticalSection( &writeSection );
    ::InitializeCriticalSection( &readerCountSection );
    noReaders = ::CreateEvent( NULL, TRUE, TRUE, NULL );
  }

  ~MultiReaderLockImpl() throw()
  {
    ::CloseHandle( noReaders );
    ::DeleteCriticalSection( &writeSection );
    ::DeleteCriticalSection( &readerCountSection );
  }

  void LockForRead() const
  {
    ::EnterCriticalSection( &writeSection );
    ::EnterCriticalSection( &readerCountSection );
    if ( ++readers == 1 )
    {
      ::ResetEvent( noReaders );
    }
    ::LeaveCriticalSection( &readerCountSection );
    ::LeaveCriticalSection( &writeSection );
  }

  void UnlockAfterRead() const
  {
    ::EnterCriticalSection( &readerCountSection );
    assert( readers > 0 );
    if ( !( --readers ) )
    {
      ::SetEvent( noReaders );
    }
    ::LeaveCriticalSection( &readerCountSection );
  }

  void LockForWrite() const
  {
    ::EnterCriticalSection( &writeSection );
    ::WaitForSingleObject( noReaders, INFINITE );
  }

  void UnlockAfterWrite() const
  {
    ::LeaveCriticalSection( &writeSection );
  }

private:
  mutable CRITICAL_SECTION  writeSection;
  mutable CRITICAL_SECTION  readerCountSection;
  mutable long              readers;
  mutable HANDLE            noReaders;
};
#else
class MultiReaderLock::MultiReaderLockImpl
{ 
public: 
  MultiReaderLockImpl()
  {
    pthread_rwlock_init(&rwlock, NULL);
  }

  ~MultiReaderLockImpl() throw()
  {
    pthread_rwlock_destroy(&rwlock);
  }

  void LockForRead() const
  {
    pthread_rwlock_rdlock(&rwlock);
  }

  void UnlockAfterRead() const
  {
    pthread_rwlock_unlock(&rwlock);
  }

  void LockForWrite() const
  {
    pthread_rwlock_wrlock(&rwlock);
  }

  void UnlockAfterWrite() const
  {
    pthread_rwlock_unlock(&rwlock);
  }

private:
  mutable pthread_rwlock_t rwlock;
};
#endif


MultiReaderLock::MultiReaderLock() : impl_( new MultiReaderLockImpl )
{
  ;;
}



MultiReaderLock::~MultiReaderLock()
{
  ;;
}



void MultiReaderLock::LockForRead() const
{
  impl_ -> LockForRead();
}



void MultiReaderLock::UnlockAfterRead() const
{
  impl_ -> UnlockAfterRead();
}



void MultiReaderLock::LockForWrite() const
{
  impl_ -> LockForWrite();
}



void MultiReaderLock::UnlockAfterWrite() const
{
  impl_ -> UnlockAfterWrite();
}

} //namespace threading
