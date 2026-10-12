# nvrhi_probe (этап 0 плана `docs/PLAN_NVRHI.md`)

Вне CMake-графа клиента. Цель — снять три неизвестности до того, как NVRHI
попадёт в дерево клиента: собирается ли им нашим тулчейном, поднимается ли
Vulkan, сколько весит.

```sh
./fetch_vendor.sh          # клонирует nvrhi + Vulkan-Headers + DirectX-Headers (в vendor/, в git не лежит)
./build_probe_wine.sh      # configure + build (cl 14.44 под Wine, тот же префикс ~/.wine-vs)
./build_probe_wine.sh --run
```

## Что доказано на этой машине (2026-10-12)

| Проверка | Итог |
|---|---|
| NVRHI собирается cl 14.44 под Wine, C++17, статика, **`/MT`** | **ДА.** Конфигурирование и сборка без единой ошибки; в командной строке cl видно `-MT` (после `CMAKE_POLICY_DEFAULT_CMP0091=NEW` + `MSVC_RUNTIME_LIBRARY=MultiThreaded`) |
| линкуется с приложением | **ДА**, но см. «Порядок линковки» ниже |
| Vulkan/D3D11 поднимаются | **НЕ ПРОВЕРЕНО здесь.** В этом Wine-префиксе нет адаптера: `D3D11CreateDevice` → `DXGI_ERROR_NOT_FOUND` (0x887A0004), `vkCreateInstance` → `VK_ERROR_EXTENSION_NOT_PRESENT` (winevulkan есть, surface-расширения нет). Runtime-смоук — на tiny10 |

Размеры статики (`/O2`, без PDB): `nvrhi.lib` 4.8 МБ, `nvrhi_d3d11.lib` 5.8 МБ,
`nvrhi_d3d12.lib` 10.5 МБ, `nvrhi_vk.lib` 13.0 МБ; `nvrhi_probe.exe` 0.72 МБ.
Vulkan-бэкенд дорогой по коду из-за шаблонного Vulkan-Hpp.

## Найдено по исходникам NVRHI `6b96fb03` (2026-10-05) и подтверждено сборкой

1. **Swapchain-абстракции нет вообще.** В `include/` нет ни `ISwapchain`, ни
   `createSwapchain`. Приложение само создаёт DXGI swapchain / `VkSwapchainKHR`,
   а бэк-буфер оборачивается через `IDevice::createHandleForNativeTexture`
   (так и написано в `doc/Tutorial.md`).
2. **Occlusion-запросов нет.** Есть только `IEventQuery`
   (`createEventQuery/setEventQuery/pollEventQuery/waitEventQuery/resetEventQuery`)
   и `ITimerQuery`. Это gap: в клиенте живой `OcclusionQueriesBank`
   (`PFRenderWater`). См. §3.x плана.
3. **При СТАТИЧЕСКОЙ сборке NVRHI даёт ЧЕТЫРЕ либы**, а не одну: `nvrhi`
   (common + validation), `nvrhi_d3d11`, `nvrhi_d3d12`, `nvrhi_vk`. Ссылаться
   надо на все нужные — иначе `createDevice` не слинкуется (проверено).
4. **Там же, при статике, NVRHI НЕ инициализирует диспетчер Vulkan-Hpp**:
   `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` и
   `VULKAN_HPP_DEFAULT_DISPATCHER.init(...)` обёрнуты в
   `#if defined(NVRHI_SHARED_LIBRARY_BUILD)`. Хост обязан объявить хранилище
   ровно в одном TU и сам проинициализировать диспетчер — см. `probe.cpp`.
5. **`nvrhi::d3d11::DeviceDesc` принимает только `context`** (устройство
   берётся из него); отдельного поля `device` нет.
6. **NVRHI не создаёт ни VkInstance, ни VkDevice, ни VkSurfaceKHR** —
   бутстрап целиком на приложении. `probe.cpp` — рабочий пример этого бутстрапа
   на чистом C API (Vulkan-Hpp в свежих заголовках местами несовместим с
   примерами из NVRHI-доков: `createInstanceUnique` не раскладывается через
   structured binding, `init(VkInstance)` с сырым хендлом выбирает
   `init<DynamicLoader>`; работает только 3-аргументный
   `init(VkInstance, PFN_vkGetInstanceProcAddr, VkDevice)`).
7. **import-библиотека Vulkan не нужна** (и не доступна): NVRHI разрешает
   функции через динамический диспетчер. Достаточно `LoadLibrary("vulkan-1.dll")`.
   Но `VK_NO_PROTOTYPES` при этом обязателен, иначе линкер требует `vulkan-1.lib`.
8. **Грязь в заголовках:** `vulkan_win32.h` требует `windows.h` ДО себя;
   `VK_USE_PLATFORM_WIN32_KHR` NVRHI включает как `PUBLIC` define на своей цели.

## Что осталось на tiny10

- `vkCreateInstance` с `VK_KHR_surface` + `VK_KHR_win32_surface` на реальном
  драйвере;
- `VkSwapchainKHR` + `createHandleForNativeTexture` + clear + present в окне
  клиента;
- D3D11/D3D12-ветки (для сравнения картинки с D3D9);
- `IEventQuery` как замена `D3DQUERYTYPE_EVENT` в `Renderer::Synchronize()`.
