# nvrhi_probe (этап 0 плана `docs/PLAN_NVRHI.md`)

Вне CMake-графа клиента. Цель — снять три неизвестности до того, как NVRHI
попадёт в дерево клиента: собирается ли им нашим тулчейном, поднимается ли
Vulkan, сколько весит.

```sh
./fetch_vendor.sh          # клонирует nvrhi + Vulkan-Headers + DirectX-Headers (в vendor/, в git не лежит)
./build_probe_wine.sh      # Windows-сборка cl 14.44 под Wine (тот же префикс ~/.wine-vs)
./build_probe_wine.sh --run
./build_probe_linux.sh     # нативная Linux-сборка (g++) — ради Vulkan-смоука на хосте
./build_probe_linux.sh --run
```
Артефакты: `build/nvrhi_probe.exe` (Windows), `build-linux/nvrhi_probe` (Linux).
Windows-артефакт запускается на tiny10 так:

```sh
scp build/nvrhi_probe.exe pw-tiny10:C:/tools/nvrhi_probe.exe
ssh pw-tiny10 'cd C:\tools & nvrhi_probe.exe'
```

## Итоги прогонов (2026-10-12)

| Среда | D3D11 | Vulkan | Смоук «RT → clear → copy → readback» |
|---|---|---|---|
| Wine `~/.wine-vs` (домашняя) | FAIL `DXGI_ERROR_NOT_FOUND` (0x887A0004) | FAIL `VK_ERROR_EXTENSION_NOT_PRESENT` (winevulkan есть, surface-расширений нет) | — (среды запуска нет) |
| **tiny10** (`ssh pw-tiny10`) | **PASS** — hardware не дал, взят **WARP**, feature level **11_0** | **FAIL: `vulkan-1.dll` в госте нет** (видео = `Microsoft Basic Display Adapter`, драйвер 10.0.17763.1) | **PASS** на D3D11: пиксель `(0,188,255,255)` |
| **Linux-хост** (g++, нативно) | — | **PASS**: instance → device → `nvrhi::vulkan::createDevice` → `IEventQuery` → смоук | **PASS**: пиксель `(0,188,255,255)`, pitch 64 |

Вывод: **Vulkan сегодня проверяется только на Linux-хосте**, и то на
**llvmpipe** (модули nvidia на хосте не загружены → RTX 3060 в перечне устройств
не появляется, `ComputeQueue/Meshlets/RayTracing/VRS = 0`, производительность не
измеряется). tiny10 годится как D3D11/WARP-эталон и стенд E2E клиента, но **не**
как Vulkan-стенд.

Сборка/линковка: NVRHI `6b96fb03` собирается cl 14.44 под Wine, C++17, статика,
**`/MT`** — без ошибок (`-MT` виден в командной строке cl; нужен
`-DCMAKE_POLICY_DEFAULT_CMP0091=NEW`, иначе `MSVC_RUNTIME_LIBRARY` игнорируется).

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

## Обязательные условия Vulkan-бэкенда (в доках NVRHI их нет; получены ценой abort'ов)

Приложение **обязано** включить при создании `VkDevice`, иначе NVRHI падает:

| Требование | Симптом без него |
|---|---|
| `VK_KHR_synchronization2` (или Vulkan 1.3) — NVRHI зовёт `vkCmdPipelineBarrier2` | assert Vulkan-Hpp: `Function <vkCmdPipelineBarrier2> requires <VK_KHR_synchronization2> or <VK_VERSION_1_3>` |
| timeline-семафоры (Vulkan 1.2 / `VK_KHR_timeline_semaphore`) — `vkGetSemaphoreCounterValue` | assert: `Function <vkGetSemaphoreCounterValue> requires <VK_KHR_timeline_semaphore> or <VK_VERSION_1_2>` |
| у каждой текстуры `initialState` + `keepInitialState` (либо `beginTrackingTextureState`) | SIGSEGV в `CommandListResourceStateTracker::requireTextureState` |

В probe включены: API 1.3; `VkPhysicalDeviceVulkan12Features`
(`timelineSemaphore`, `bufferDeviceAddress`,
`descriptorBindingUniformBufferUpdateAfterBind`); `VkPhysicalDeviceVulkan13Features`
(`synchronization2`, `dynamicRendering`, `subgroupSizeControl`,
`computeFullSubgroups`); расширения устройства `VK_KHR_swapchain` +
`VK_KHR_synchronization2`.

Прочие ловушки:

- **`vkDestroyInstance` — instance-level**: `vkGetInstanceProcAddr(NULL, "vkDestroyInstance")`
  возвращает NULL и на Linux-загрузчике, и на winevulkan (то же для
  `vkEnumeratePhysicalDevices`). До создания инстанса грузится только
  `vkCreateInstance`.
- **порядок статических либ на g++**: бэкенды зависят от core → `nvrhi_vk nvrhi`
  (на MSVC порядок не важен; на g++ иначе undefined reference к
  `CommandListResourceStateTracker::*` и `utils::InvalidEnum`).
- **`vulkan_win32.h` включать ПОСЛЕ `vulkan_core.h`** (иначе C4430/C2146 на
  `VkWin32SurfaceCreateFlagsKHR`), а `windows.h` — до `vulkan.h`.
- **никаких render pass'ов**: `beginRenderPass`/`endRenderPass`/`RenderPassDesc`
  в этой версии **нет** — всё через `setGraphicsState(GraphicsState)` + `draw()`,
  очистка явная (`clearTextureFloat` / `clearTextureUInt` /
  `clearDepthStencilTexture`).

## Что осталось

- `VkSurfaceKHR` + `VkSwapchainKHR` + `createHandleForNativeTexture` + present в
  настоящем окне (для Windows — на реальной машине с Vulkan-драйвером: tiny10 не
  годится, Wine — не даёт surface);
- D3D12-ветка (для сравнения картинки с D3D9);
- замеры на реальном GPU (на llvmpipe бессмысленны);
- `IEventQuery` как замена `D3DQUERYTYPE_EVENT` в `Renderer::Synchronize()` —
  работает, проверено.
