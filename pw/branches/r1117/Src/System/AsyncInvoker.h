#ifndef THREADWITHTASK_H_4FF430BA_CE8C_4
#define THREADWITHTASK_H_4FF430BA_CE8C_4

#include "SyncPrimitives.h"
#include "Thread.h"
#include "StarForce/StarForce.h"

namespace threading
{
namespace detail 
{
  template< class T >
  struct ResultStorage
  {
    T t;

    ResultStorage(): t() {}
    ResultStorage(const T &t): t(t) {}
    template<class FncT> void Call( const FncT &fn ) { t = fn(); }
    const T &Result() const { return t; }
  };

  template<>
  struct ResultStorage<void>
  {
    template<class FncT> void Call( const FncT &fn ) { fn(); }
    void Result() const {}
  };  
}
  
template< class TaskT, class ResultT = void >
class AsyncInvoker: private ::threading::Thread, private detail::ResultStorage<ResultT>
{
  typedef detail::ResultStorage<ResultT> ResultStorage;

private:
  TaskT m_task; 
  mutable ::threading::Mutex taskMutex; 
  mutable ::LinuxManualEvent_Internal canStartEvent;
  ::LinuxManualEvent_Internal taskCompleteEvent;
  bool needStop; 
  
public:

  AsyncInvoker(): needStop(false), m_task()
  {
    this->::threading::Thread::Resume();
  }
  
  virtual ~AsyncInvoker()
  {
    {
      ::threading::MutexLock lock(this->taskMutex);
      this->needStop = true;  
      this->canStartEvent.Set();
    }
     
    this->::threading::Thread::Wait();     
  }
 
  STARFORCE_FORCE_INLINE void BeginInvoke( const TaskT &task )
  {
    ::threading::MutexLock lock(this->taskMutex);
     
    if( this->canStartEvent.Wait(0) )
    {
      this->ResultStorage::Call(this->m_task);     
    }
    
    this->m_task = task; 
      
    this->canStartEvent.Set();
    this->taskCompleteEvent.Reset();
  }
  
  STARFORCE_FORCE_INLINE ResultT EndInvoke()
  {
    this->taskCompleteEvent.Wait();
    
    ::threading::MutexLock lock(this->taskMutex);
    ResultStorage resultCopy(*this);
  
    return resultCopy.Result();
  }
    
  STARFORCE_FORCE_INLINE void SyncInvoke( const TaskT &task )
  {
    ::threading::MutexLock lock(this->taskMutex);
    
    if( this->canStartEvent.Wait(0) )
    {
      this->ResultStorage::Call(this->m_task);     
    }
     
    this->m_task = task;   
    this->ResultStorage::Call(this->m_task);
        
    this->taskCompleteEvent.Set();
  }
  
  STARFORCE_FORCE_INLINE void FakeInvoke( const ResultStorage &result = ResultStorage() )
  {
    ::threading::MutexLock lock(this->taskMutex);
    
    if( this->canStartEvent.Wait(0) )
    {
      this->ResultStorage::Call(this->m_task);     
    }
    
    static_cast<ResultStorage &>(*this) = result;   
    this->taskCompleteEvent.Set();
  }
  
  STARFORCE_FORCE_INLINE void Sync()
  {
    ::threading::MutexLock lock(this->taskMutex);
    
    if( this->canStartEvent.Wait(0) )
    {
      this->ResultStorage::Call(this->m_task);     
      this->taskCompleteEvent.Set(); 
    } 
  }
  
  STARFORCE_FORCE_INLINE bool IsBusy() const
  {
    if( !this->taskMutex.TryLock() )
    {
      return true;
    }
    
    bool isBusyResult = false;
    
    if( this->canStartEvent.Wait(0) )
    {
      isBusyResult = true;
      this->canStartEvent.Set();
    }
    
    this->taskMutex.Unlock();
    
    return isBusyResult;
  }
    
  void SetPriority( int priority )
  {
    (void) priority;
  }
  
private:

  STARFORCE_EXPORT virtual unsigned Work()
  {
    for(;;)
    {
      this->canStartEvent.Wait();

      {
        ::threading::MutexLock lock(this->taskMutex);
           
        if( this->needStop )
        {
          return 0;  
        }
          
        this->ResultStorage::Call(this->m_task);     
        
        this->taskCompleteEvent.Set();
      }
    }
  }
}; 

template< class TaskT, class ResultT = void >
class FakeAsyncInvoker: private detail::ResultStorage<ResultT>
{
  typedef detail::ResultStorage<ResultT> ResultStorage;

public:
  FakeAsyncInvoker(): m_task() {}

  void BeginInvoke( const TaskT &task )
  {
    m_task = task;   
    ResultStorage::Call(m_task); 
  }

  ResultT EndInvoke()
  {
    return ResultStorage::Result();
  }
  
  void SyncInvoke( const TaskT &task )
  {
    m_task = task;   
    ResultStorage::Call(m_task);
  }
  
  void FakeInvoke( const ResultStorage &result = ResultStorage() )
  {
    static_cast<ResultStorage &>(*this) = result;   
  }

  void Sync() 
  {
  }   
  
  bool IsBusy() const
  {
    return true;
  }
  
  void SetPriority( int priority ) 
  {
    (void) priority;
  }

private:
  TaskT m_task; 
}; 

}

#endif
