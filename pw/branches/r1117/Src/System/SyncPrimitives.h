#ifndef SYNCPRIMITIVES_H_INCLUDED
#define SYNCPRIMITIVES_H_INCLUDED

#include "System/config.h"
#include "noncopyable.h"

#if defined( NV_LINUX_PLATFORM )
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

struct LinuxManualEvent_Internal
{
private:
  mutable pthread_mutex_t m_mutex;
  mutable pthread_cond_t  m_cond;
  mutable bool            m_manualReset;
  mutable bool            m_state;

public:
  enum { NV_INFINITE = -1U };

  LinuxManualEvent_Internal( bool manualReset = false, bool initialState = false, const char * name = NULL )
    : m_manualReset(manualReset), m_state(initialState)
  {
    pthread_mutex_init(&m_mutex, NULL);
    pthread_cond_init(&m_cond, NULL);
  }

  ~LinuxManualEvent_Internal()
  {
    pthread_cond_destroy(&m_cond);
    pthread_mutex_destroy(&m_mutex);
  }

  void Reset()
  {
    pthread_mutex_lock(&m_mutex);
    m_state = false;
    pthread_mutex_unlock(&m_mutex);
  }

  void Set()
  {
    pthread_mutex_lock(&m_mutex);
    m_state = true;
    if (m_manualReset)
      pthread_cond_broadcast(&m_cond);
    else
      pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
  }

  bool Wait( unsigned time = NV_INFINITE ) const
  {
    pthread_mutex_lock(&m_mutex);
    bool success = true;
    if (!m_state)
    {
      if (time == NV_INFINITE)
      {
        while (!m_state)
          pthread_cond_wait(&m_cond, &m_mutex);
      }
      else
      {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += time / 1000;
        ts.tv_nsec += (time % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000)
        {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000;
        }
        while (!m_state && success)
        {
          if (pthread_cond_timedwait(&m_cond, &m_mutex, &ts) != 0)
            success = false;
        }
      }
    }
    if (success && !m_manualReset)
      m_state = false;
    pthread_mutex_unlock(&m_mutex);
    return success;
  }
};
#endif

namespace threading
{

#if defined( NV_WIN_PLATFORM )


class Mutex : public NonCopyable
{
  mutable CRITICAL_SECTION section;
public:

  Mutex()               { InitializeCriticalSection( &section ); }
  explicit Mutex( int ) { InitializeCriticalSection( &section ); }
  ~Mutex()              { DeleteCriticalSection( &section ); }

  void Lock() const     { EnterCriticalSection( &section ); }
  void Unlock() const   { LeaveCriticalSection( &section ); }

  bool TryLock() const  { return TryEnterCriticalSection( &section ) != 0; }

};


class Semaphore : public NonCopyable
{
  HANDLE handle;
public:

  Semaphore( unsigned int initialCount ) : handle( 0 )
  {
    handle = CreateSemaphoreA( 0, initialCount, 0x7FFFFFFF, 0 );
  }

  ~Semaphore()
  {
    if ( handle )
      CloseHandle( handle );
  }

  void Signal() { ReleaseSemaphore( handle, 1, 0 ); }

  bool Wait( unsigned int nTime = INFINITE ) const
  {
    return ( WaitForSingleObject( handle, (DWORD)nTime ) == WAIT_OBJECT_0 );
  }

  void Reset() { while ( Wait( 0 ) ) {} }

};


class WaitEvent_Win : public NonCopyable
{
private:

  HANDLE handle;

public:

  WaitEvent_Win( bool manualReset = false, bool initialState = false, const char * name = NULL ) : handle( 0 )
  {
    handle = CreateEventA( 0, manualReset ? TRUE : FALSE, initialState ? TRUE : FALSE, name );
  }

  ~WaitEvent_Win()
  {
    if ( handle )
      CloseHandle( handle );
  }

  void Reset() { ResetEvent( handle ); }

  void Set() { SetEvent( handle ); }

  enum { NV_INFINITE = -1U };

  bool Wait( unsigned time = NV_INFINITE ) const
  {
    return ( WaitForSingleObject( handle, (DWORD)time ) == WAIT_OBJECT_0 );
  }

  HANDLE GetHandle() const { return handle; }

};

typedef WaitEvent_Win Event;
typedef WaitEvent_Win LinuxManualEvent_Internal;

#elif defined( NV_LINUX_PLATFORM )


class Mutex : public NonCopyable
{
  mutable pthread_mutex_t m_mutex;
  mutable pthread_mutexattr_t m_attr;
public:

  Mutex()
  {
    ::pthread_mutexattr_init( &m_attr );
    ::pthread_mutexattr_settype( &m_attr, PTHREAD_MUTEX_RECURSIVE );
    ::pthread_mutex_init( &m_mutex, &m_attr );
  }

  explicit Mutex( int )
  {
    ::pthread_mutexattr_init( &m_attr );
    ::pthread_mutexattr_settype( &m_attr, PTHREAD_MUTEX_RECURSIVE );
    ::pthread_mutex_init( &m_mutex, &m_attr );
  }

  ~Mutex()
  {
    ::pthread_mutex_destroy( &m_mutex );
    ::pthread_mutexattr_destroy( &m_attr );
  }

  void Lock() const
  {
    ::pthread_mutex_lock( &m_mutex );
  }

  void Unlock() const
  {
    ::pthread_mutex_unlock( &m_mutex );
  }

  bool TryLock() const
  {
    return ::pthread_mutex_trylock( &m_mutex ) == 0;
  }

};


class Semaphore : public NonCopyable
{
  mutable sem_t m_semaphore;
public:

  Semaphore( unsigned int initialCount )
  {
    sem_init( &m_semaphore, 0, initialCount );
  }

  void Signal()
  {
    sem_post( &m_semaphore );
  }

  void Signal( int count )
  {
    for( int i = 0; i < count; ++i )
      sem_post( &m_semaphore );
  }

  bool Wait( unsigned int nTime = -1U ) const
  {
    if ( nTime == -1U )
    {
      return sem_wait( &m_semaphore ) == 0;
    }

    if ( nTime == 0 )
    {
      return sem_trywait( &m_semaphore ) == 0;
    }

    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    ts.tv_sec += nTime / 1000;
    ts.tv_nsec += ( nTime % 1000 ) * 1000000;
    if ( ts.tv_nsec >= 1000000000 )
    {
      ++ts.tv_sec;
      ts.tv_nsec -= 1000000000;
    }

    return sem_timedwait( &m_semaphore, &ts ) == 0;
  }

  void Reset() const
  {
    while ( Wait( 0 ) ) {}
  }

  ~Semaphore()
  {
    sem_destroy( &m_semaphore );
  }

};

typedef ::LinuxManualEvent_Internal Event;

#endif  // #elif defined( NV_LINUX_PLATFORM )


struct MutexLock : public NonCopyable
{
  const ::threading::Mutex * mutex;

public:
  MutexLock( const ::threading::Mutex & _mutex ) : mutex( &_mutex ) { mutex->Lock(); }

  ~MutexLock() { mutex->Unlock(); }
};




struct MutexUnlock : public NonCopyable
{
  const ::threading::Mutex * mutex;

public:
  MutexUnlock( const ::threading::Mutex & _mutex ) : mutex( &_mutex ) {}

  ~MutexUnlock() { mutex->Unlock(); }
};



} //namespace threading

#endif //SYNCPRIMITIVES_H_INCLUDED
