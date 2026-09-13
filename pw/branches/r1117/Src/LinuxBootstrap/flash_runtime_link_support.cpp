#include "../UI/stdafx.h"

#include "../UI/FlashContainer2.h"

// These specializations normally come from FlashContainer2.cpp. Keep the
// executable link gate strict until that complete UI control is admitted.
namespace ni_detail
{
template<>
WeakPointerProxyST* AcquireWeakProxyImpl<WeakPointerProxyST, UI::FlashContainer2>(UI::FlashContainer2* value, void*)
{
  return value->AcquireWeakProxy();
}

template<>
UI::FlashContainer2* GetDerivedObjectImpl2<UI::FlashContainer2>(BasicType* value, UI::FlashContainer2*, void*)
{
  SerializableBase* serializable = static_cast<SerializableBase*>(value);
  IBaseInterfaceST* interfaceObject = static_cast<IBaseInterfaceST*>(serializable);
  BaseObjectST* baseObject = interfaceObject->CastToBaseObject();
  UI::Window* window = static_cast<UI::Window*>(baseObject);
  UI::NameMappedWindow* nameMappedWindow = static_cast<UI::NameMappedWindow*>(window);
  return static_cast<UI::FlashContainer2*>(nameMappedWindow);
}

template<>
WeakPointerProxyST* AcquireWeakProxyImpl<WeakPointerProxyST, flash::IStageFocusHandler>(flash::IStageFocusHandler* value, void*)
{
  return value->AcquireWeakProxy();
}

template<>
flash::IStageFocusHandler* GetDerivedObjectImpl2<flash::IStageFocusHandler>(BasicType* value, flash::IStageFocusHandler*, void*)
{
  return GetDerivedObjectImpl(value, static_cast<flash::IStageFocusHandler*>(0));
}
}
