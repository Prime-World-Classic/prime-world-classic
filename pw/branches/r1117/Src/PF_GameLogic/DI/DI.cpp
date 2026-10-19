#include "stdafx.h"
#include "DI.hpp"

// NOTE: предполагается, что ProviderManager живет в основном потоке.
// на практике так и получается. если что-то изменится, необходимо будет переработать.

namespace
{
  namespace EProviderManagerState
  {
    enum Enum
    {
      Uninitialized,
      Operational,
      Finished,
    };
  }

  // глобальное состояние необходимо из-за специфики работы CObjectBase, а именно того факта, что уничтоженные объекты тут же создаются вновь
  // таким образом, может получиться, что конструктор/деструктор какого-то DI-класса будет вызван после уничтожения ProviderManager
  static EProviderManagerState::Enum g_state = EProviderManagerState::Uninitialized;
}

namespace DI
{
  namespace Private
  {

    ProviderManager::ProviderManager()
      : providers()
    {
      NI_ASSERT(g_state == EProviderManagerState::Uninitialized, "Invariant");

      g_state = EProviderManagerState::Operational;
    }

    ProviderManager::~ProviderManager()
    {
      NI_ASSERT(g_state == EProviderManagerState::Operational, "Invariant");

      g_state = EProviderManagerState::Finished;
    }

    void ProviderManager::Register(const TypeInfo& tag, const ProviderPtr provider)
    {
      if (g_state != EProviderManagerState::Operational)
        return;

      DevTrace("DI: registering '%s'...", tag.Get().name());

      if (!provider)
      {
        Unregister(tag, NULL);
        return;
      }

      // провайдеров с одним тегом может быть несколько: уничтоженный CObjectBase тут же создается вновь
      // (DestroyContents) и регистрирует свою "пустую" оболочку, а она живет, пока на нее есть CPtr
      // (например, статические DI_WEAK). Новый экземпляр (следующий бой в том же процессе, переоткрытие
      // реплея) не должен ею перекрываться - Get выбирает последний живой.
      ProviderList& list = providers[tag];

      for (int i = 0; i < list.size(); ++i)
        if (list[i] == provider)
          return;

      list.push_back(provider);

      DevTrace("DI: registered '%s'", tag.Get().name());
    }

    void ProviderManager::Unregister(const TypeInfo& tag, const ProviderPtr provider)
    {
      if (g_state != EProviderManagerState::Operational)
        return;

      DevTrace("DI: unregistering '%s'...", tag.Get().name());

      ProviderMap::iterator it = providers.find(tag);

      if (it == providers.end())
        return;

      ProviderList& list = it->second;

      if (provider)
      {
        int index = 0;

        while (index < list.size() && list[index] != provider)
          ++index;

        NI_VERIFY(index < list.size(), "DI: trying to unregister foreign provider!", return);

        list.erase(list.begin() + index);
      }
      else
      {
        list.clear();
      }

      if (list.empty())
        providers.erase(it);

      DevTrace("DI: unregistered '%s'", tag.Get().name());
    }

    ProviderPtr ProviderManager::Get(const TypeInfo& tag) const
    {
      if (g_state != EProviderManagerState::Operational)
        return NULL;

      ProviderMap::const_iterator it = providers.find(tag);

      if (it == providers.end() || it->second.empty())
        return NULL;

      const ProviderList& list = it->second;

      for (int i = list.size() - 1; i >= 0; --i)
        if (list[i]->IsAlive())
          return list[i];

      return list.back();
    }

    ProviderManager& ProviderManager::Instance()
    {
      static ProviderManager instance;

      return instance;
    }

  }
}
