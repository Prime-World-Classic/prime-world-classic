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

./build_loader_wine.sh     # свой vulkan-1.dll (Khronos Vulkan-Loader, /MT) — в tiny10
                           # системный vulkan-1.dll вырезан, а в Mesa-сборке его нет
./setup_tiny10_vulkan.sh   # раскладка Mesa lavapipe + загрузка в tiny10 + запуск пробы
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
| **tiny10, как есть** | **PASS** — hardware не дал, взят **WARP**, feature level **11_0** | **FAIL: `vulkan-1.dll` в госте нет** (видео = `Microsoft Basic Display Adapter`, драйвер 10.0.17763.1) | **PASS** на D3D11: пиксель `(0,188,255,255)` |
| **tiny10 + Mesa lavapipe** (`setup_tiny10_vulkan.sh`) | PASS (WARP) | **PASS полностью**: instance **со surface** → device → `createDevice` → `IEventQuery` → смоук → **окно → `VkSurfaceKHR` → `VkSwapchainKHR` (120x89, 2 картинки, B8G8R8A8) → `createHandleForNativeTexture` → clear → `vkQueuePresentKHR`** | **PASS**, пиксель тот же |
| **Linux-хост** (g++, нативно) | — | **PASS**: instance → device → `nvrhi::vulkan::createDevice` → `IEventQuery` → смоук | **PASS**: пиксель `(0,188,255,255)`, pitch 64 |

Вывод: **Vulkan проверяется на tiny10** — том же стенде, где гоняется E2E
клиента — через software-стек: `vulkan-1.dll` (свой, `build_loader_wine.sh`) +
ICD Mesa **lavapipe** (`vendor/mesa-win/vulkan_lvp.dll`, Mesa 26.2.4
release-msvc). На хосте Vulkan тоже работает, но там доступен только llvmpipe
(модули nvidia не загружены → RTX 3060 в перечне не появляется).
**Производительность по-прежнему негде мерить** — и там и там растер CPU;
для замеров нужно реальное GPU-железо (см. план, риски п.5).

### Два обязательных условия Vulkan в tiny10 (без них — непонятные отказы)

1. **ICD регистрируется в реестре**, а не через `VK_ICD_FILENAMES`:
   `reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v
   "C:\tools\vulkan\lvp_icd.x86_64.json" /t REG_DWORD /d 0 /f`.
   Причина: проба из-под SSH идёт с правами администратора, и загрузчик печатает
   «Loader is running with elevated permissions» **игнорируя** переменные
   окружения; без реестра ICD не находится вовсе, а `vkCreateInstance` отвечает
   `VK_ERROR_INCOMPATIBLE_DRIVER` (что выглядит как «драйвер несовместим», хотя
   драйвера просто нет).
2. **Запускать в интерактивной сессии** (задача `schtasks` без `/ru`, приём как с
   `PwE2E`). Из сессии 0 (чистый SSH) окно, surface и swapchain создаются, но
   `vkQueuePresentKHR` падает в `VK_ERROR_MEMORY_MAP_FAILED`: lavapipe делает
   present через GDI DIB, а в неинтерактивной сессии его нет.

Прочее по раскладке: все файлы в одном каталоге (`vulkan-1.dll`,
`vulkan_lvp.dll`, `lvp_icd.x86_64.json`, `nvrhi_probe.exe`) — в ICD json
`library_path": ".\vulkan_lvp.dll"`, т.е. относительно каталога. `vulkan_lvp.dll`
(56 МБ) импортирует только системные DLL — VC++ runtime не нужен.
`swci.imageUsage` для swapchain стоит ставить с `TRANSFER_SRC|TRANSFER_DST`.

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

## Лимиты, замеренные пробой (lavapipe Mesa 26, tiny10)

`maxPerStageDescriptorSampledImages` = `...Samplers` = `...UniformBuffers`
≈ 1 015 808 (фактически без лимита), `maxImageDimension2D` = 16384,
`maxColorAttachments` = 8, `maxVertexInputAttributes` = 32,
**`maxBoundDescriptorSets` = 8** ← единственное реальное ограничение, оно
проектирующее для макета `IBindingLayout` клиента (16 сэмплеров + 261 указатель
текстур + CB виртуальных регистров `c0..c255`). На реальном GPU бывает 7.

## Что осталось

- `VkSurfaceKHR` + `VkSwapchainKHR` + `createHandleForNativeTexture` + present в
  настоящем окне (для Windows — на реальной машине с Vulkan-драйвером: tiny10 не
  годится, Wine — не даёт surface);
- D3D12-ветка (для сравнения картинки с D3D9);
- замеры на реальном GPU (на llvmpipe бессмысленны);
- `IEventQuery` как замена `D3DQUERYTYPE_EVENT` в `Renderer::Synchronize()` —
  работает, проверено.

## Спайк B0: SPIR-V-пайплайн и сверка пикселей с CPU-эталоном

`probe.cpp::SmokeShaderPipeline` строит полноценный пайплайн из SPIR-V,
сгенерированного `../spirv_probe/gen_b0_shader.py` (материал в диалекте
клиента: `register(cN)`, `sampler2D`, `tex2D`, `clip()`, `mul(v, M)`):

* RT 64x64 RGBA8 + framebuffer;
* VS/PS из SPIR-V (`setEntryName` — **обязательно**, DXC оставляет имя HLSL-
  функции, NVRHI ищет `main` и молча возвращает `nullptr`);
* input layout (порядок атрибутов = location'ы; имена Vulkan не использует);
* VB (2 треугольника), uniform (`float4x4` + `float4`), текстура 8x8 с
  асимметричным содержимым, сэмплер POINT/Clamp;
* binding layout **с обнулёнными `VulkanBindingOffsets`** — дефолтные
  `{t:0, s:128, b:256}` не совпадают с тем, что раздаёт DXC, и тогда выборка
  возвращает нули при полностью успешных вызовах (чёрный кадр без ошибок);
* отрисовка, readback, сравнение с эталоном, посчитанным на CPU.

Дополнительные кейсы: `b0bare` (треугольник из `SV_VertexID`, без ресурсов),
`b0vb` (вершинные данные без ресурсов — по каналу B видно долетевшее UV),
`b0tex` (загрузка текстуры через readback), `b0triv` (чистый SM6 с теми же
биндингами), `b0xfm` (ненулевая `WorldXf`: перенос в 4-й строке — проверяет
конвенцию `mul(v, M)` и `-Zpr`), `b0cull` (замер намотки).

Итог: **4096/4096 пикселей совпали** на host (llvmpipe) и на tiny10
(lavapipe). Замеренные обязательности этапа 3: `VK_KHR_dynamic_rendering` при
создании устройства (одной фичи 1.3 на Mesa мало), `setEntryName`,
согласование `VulkanBindingOffsets`, `frontCounterClockwise=true` (с
viewport'ом NVRHI геометрия D3D9-намотки оказывается тыльной — при
`cullMode=Back` не рисуется ничего).

Запуск:

    ./build_probe_linux.sh && ./build-linux/nvrhi_probe       # хост
    ./build_probe_wine.sh && ./setup_tiny10_vulkan.sh         # tiny10
