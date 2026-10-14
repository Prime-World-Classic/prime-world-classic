#pragma once
#ifndef _REALLOCATE_H_
#define _REALLOCATE_H_

// Реестр «отпусти/пересоздай то, что привязано к размеру и формату кадра».
// Бывш. DeviceLostHandler: в D3D9 у устройства было состояние lost (alt-tab,
// смена режима), и DEFAULT-пул требовалось отпускать до Reset() и пересоздавать
// после. У Vulkan/DX12 ничего подобного нет — остаётся только один повод для той
// же пары: пересоздание бэк-буфера (ресайз, смена монитора/режима). Отсюда
// лексика: OnResourcesRelease() / OnResourcesReallocate(), а не lost/reset.
// Инварианты:
//  - парность: после NotifyResourcesRelease() обязан последовать
//    NotifyResourcesReallocate() (или уничтожение устройства);
//  - вызовы идемпотентны: путь восстановления D3D9 зовёт release повторно,
//    реализациям нельзя считать, что вызов ровно один;
//  - порядок по приоритетам (HIGH → SUPER_LOW) сохраняется и на release, и на
//    reallocate — зависимости между обработчиками (поверхности кадра раньше
//    того, что в них рисуется) держатся на нём.
// Отдельно живёт DeviceDeleteHandler (ниже): это про сам объект устройства
// (CreateDevice/DeleteDevice), а не про размер кадра; он переименован не будет.
// (docs/PLAN_NVRHI.md §2.3 — пулы и device-lost удаляются, а не переносятся.)

#include "../System/noncopyable.h"

#ifdef _DEBUG
  #define _CHECK_DLH_WRAPPED_
#endif // _DEBUG

#ifdef _CHECK_DLH_WRAPPED_
  #define SELF_REGISTER ReallocateHandler::isWrapped = true; ReallocateHandler::SelfRegister();
  #define DLH_CHECK(arg) arg
#else
  #define SELF_REGISTER ReallocateHandler::SelfRegister();
  #define DLH_CHECK(arg)
#endif // _CHECK_DLH_WRAPPED_

#define COMMA ,


namespace Render {

struct ReallocateHandler : NonCopyable
{
  enum HandlerPriority { HANDLERPRIORITY_HIGH, HANDLERPRIORITY_NORMAL, HANDLERPRIORITY_LOW, HANDLERPRIORITY_SUPER_LOW, HANDLERPRIORITY_COUNT };

  virtual void OnResourcesRelease() = 0;
  virtual void OnResourcesReallocate() = 0;

  virtual ~ReallocateHandler() {
    DLH_CHECK( NI_ASSERT(isWrapped, "All ReallocateHandler descendants MUST be wrapped with ReallocateWrapper!") )
  }

protected:
  explicit ReallocateHandler(HandlerPriority p = HANDLERPRIORITY_NORMAL) : priority(p) DLH_CHECK( COMMA isWrapped(false) ) {}

private:
  seDECLARE_RING(ReallocateHandler, ringPart, Ring);

  static Ring& GetHandlers(int _priority);

  friend void NotifyResourcesRelease();
  friend void NotifyResourcesReallocate();
  template<class T> friend struct ReallocateWrapper;

  void SelfRegister();
  void UnRegister();

  HandlerPriority priority;

  DLH_CHECK( bool isWrapped; )

  static bool InitReallocateHandler();
};


// specialized version for objects, that are loaded in other thread
struct ReallocateHandlerLoadable : ReallocateHandler
{
  class LoaderCS : NonCopyable // Auto-locker (critical section)
  {
    volatile bool& loadFinished;

  public:
    LoaderCS(volatile bool& _loadFinished); // should be called before any D3DPOOL_DEFAULT resources use at load
    ~LoaderCS();   // should be called after load finalization
  };

protected:
  volatile bool loadFinished;

  explicit ReallocateHandlerLoadable(HandlerPriority p = HANDLERPRIORITY_NORMAL) : ReallocateHandler(p), loadFinished(false) {}

  friend class LoaderCS;
};


template<class T>
struct ReallocateWrapper : public T
{
  ReallocateWrapper() { SELF_REGISTER }

  template<class T1>
  ReallocateWrapper(T1 *_arg1) : T(_arg1) { SELF_REGISTER }

  template<class T1>
  ReallocateWrapper(const T1& _arg1) : T(_arg1) { SELF_REGISTER }

  template<class T1, class T2>
  ReallocateWrapper(T1 *_arg1, const T2& _arg2) : T(_arg1, _arg2) { SELF_REGISTER }

  template<class T1, class T2>
  ReallocateWrapper(const T1& _arg1, const T2& _arg2) : T(_arg1, _arg2) { SELF_REGISTER }

  template<class T1, class T2, class T3>
  ReallocateWrapper(const T1& _arg1, const T2& _arg2, const T3& _arg3) : T(_arg1, _arg2, _arg3) { SELF_REGISTER }

  virtual ~ReallocateWrapper() { ReallocateHandler::UnRegister(); }
};


typedef char (&DLH_tag)[2];
DLH_tag ClassSelectorFunction(ReallocateHandler*);

void NotifyResourcesRelease();
void NotifyResourcesReallocate();

////////////////////////////////////////////////////////////////////////////////////////////////////////
struct DeviceDeleteHandler : NonCopyable
{
  enum HandlerPriority { HANDLERPRIORITY_HIGH, HANDLERPRIORITY_NORMAL, HANDLERPRIORITY_LOW, HANDLERPRIORITY_SUPER_LOW, HANDLERPRIORITY_COUNT };

  explicit DeviceDeleteHandler(HandlerPriority p = HANDLERPRIORITY_NORMAL);
  virtual ~DeviceDeleteHandler();

  virtual void OnDeviceDelete() = 0;
  virtual void OnDeviceCreate() {} // As most resources need special care to create them, it is acceptable to leave this method without override

  seDECLARE_RING(DeviceDeleteHandler, ringPart, Ring);

  // WARNING - unlike ReallocateHandler, DeviceDeleteHandler processes priorities in FILO order
  static void NotifyDeviceDelete();
  static void NotifyDeviceCreate();

private:
  HandlerPriority priority;
};

} // namespace Render

template<class T> struct CreateHelper<sizeof(Render::DLH_tag), T> : CreateHelpersHelper< Render::ReallocateWrapper<T> > {};

#endif /* _DEVICELOST_H_ */
