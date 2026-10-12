// nvrhi_probe — этап 0 плана docs/PLAN_NVRHI.md. Вне графа клиента.
//
// Проверяется по порядку, каждый шаг независимо (один сбой не прячет остальные):
//   1. NVRHI собирается нашим тулчейном (Windows: cl 14.44 под Wine, C++17,
//      статика, /MT; Linux: g++, тот же код — чтобы Vulkan-смоук можно было
//      снять на хосте с реальным GPU);
//   2. поднимается ли бэкенд (Windows: D3D11 + Vulkan; Linux: Vulkan);
//   3. работает ли конвейер «аллокация RT -> clear -> copy в staging -> readback»
//      (SmokeRender) — это минимальный конец-в-конец без шейдеров и без окна.
//
// Проверено по исходникам NVRHI 6b96fb03 и подтверждено сборкой/запуском:
//   - swapchain-абстракции нет вообще: бэк-буфер оборачивается через
//     IDevice::createHandleForNativeTexture;
//   - RENDER PASS'ОВ НЕТ: вместо beginRenderPass/endRenderPass —
//     setGraphicsState(GraphicsState) + draw(), а очистка явная
//     (clearTextureFloat / clearTextureUInt / clearDepthStencilTexture);
//   - occlusion-запросов нет (только IEventQuery / ITimerQuery);
//   - d3d11::DeviceDesc принимает ТОЛЬКО context (устройство берётся из него);
//   - при СТАТИЧЕСКОЙ сборке NVRHI даёт четыре либы (nvrhi, nvrhi_d3d11,
//     nvrhi_d3d12, nvrhi_vk) и сам НЕ инициализирует Vulkan-Hpp диспетчер
//     (VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE обёрнут в
//     #if defined(NVRHI_SHARED_LIBRARY_BUILD)) -> это обязанность приложения,
//     см. initVulkanDispatcher();
//   - vkGetInstanceProcAddr(NULL, "vkEnumeratePhysicalDevices") на winevulkan
//     возвращает NULL — instance-level точки грузить только через инстанс.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
// import-либы Vulkan SDK нет нигде -> прототипы не объявляем, все точки входа
// берём из vulkan-1.dll / libvulkan.so через vkGetInstanceProcAddr.
#define VK_NO_PROTOTYPES 1

#ifdef _WIN32
// windows.h ДО vulkan.h (vulkan_win32.h требует HINSTANCE/HWND/HANDLE),
// а сам vulkan_win32.h — ПОСЛЕ vulkan_core.h (иначе VK_DEFINE_HANDLE/VKAPI_PTR
// ещё не объявлены -> пачка C4430/C2146 в vulkan_win32.h).
#define VK_USE_PLATFORM_WIN32_KHR 1
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <stdio.h>
#include <string.h>

#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif
#include <vulkan/vulkan.hpp>

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#ifdef _WIN32
#include <nvrhi/d3d11.h>
#endif

// Хранилище динамического диспетчера Vulkan-Hpp — ровно в одном TU программы
// (при статической NVRHI сам его не объявляет).
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// ---------------------------------------------------------------- utilities

static void Print(const char* what, bool ok, const char* detail = "")
{
  printf("[%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail && *detail ? detail : "");
  fflush(stdout);
}

static const char* vkResultToString(VkResult r)
{
  switch (r)
  {
  case VK_SUCCESS:                     return "VK_SUCCESS";
  case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER:   return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_OUT_OF_HOST_MEMORY:    return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:  return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  default:                             return "other VkResult";
  }
}

// ---------------------------------------------------------------- Vulkan

namespace vkp
{
  PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
  PFN_vkCreateInstance CreateInstance = nullptr;
  PFN_vkDestroyInstance DestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkCreateDevice CreateDevice = nullptr;
  PFN_vkDestroyDevice DestroyDevice = nullptr;
  PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
}

static bool loadVulkanLoader()
{
#ifdef _WIN32
  static HMODULE mod = nullptr;
  if (mod)
    return true;
  mod = LoadLibraryA("vulkan-1.dll");
  if (!mod)
  {
    Print("vulkan: LoadLibrary(vulkan-1.dll)", false, "нет winevulkan / драйвера");
    return false;
  }
  vkp::GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(mod, "vkGetInstanceProcAddr");
#else
  static void* mod = nullptr;
  if (mod)
    return true;
  mod = dlopen("libvulkan.so.1", RTLD_NOW);
  if (!mod)
  {
    Print("vulkan: dlopen(libvulkan.so.1)", false);
    return false;
  }
  vkp::GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(mod, "vkGetInstanceProcAddr");
#endif
  if (!vkp::GetInstanceProcAddr)
  {
    Print("vulkan: vkGetInstanceProcAddr", false);
    return false;
  }

#define LOAD_LOADER(name) vkp::name = (PFN_vk##name)vkp::GetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name)
  // ВАЖНО (проверено на winevulkan): vkGetInstanceProcAddr(NULL, ...) возвращает
  // NULL для instance-level функций. До создания инстанса грузим только
  // loader-level точки.
  // По спецификации с instance == VK_NULL_HANDLE загрузчик отдаёт ТОЛЬКО
  // vkGetInstanceProcAddr, vkEnumerateInstanceExtensionProperties/... и
  // vkCreateInstance. vkDestroyInstance требует диспетчер инстанса -> его
  // грузим уже после создания инстанса (проверено и на Linux-загрузчике, и на
  // winevulkan; на winevulkan NULL возвращается и для
  // vkEnumeratePhysicalDevices — тоже instance-level).
  LOAD_LOADER(CreateInstance);
#undef LOAD_LOADER

  if (!vkp::CreateInstance)
  {
    char d[128];
    snprintf(d, sizeof(d), "gipa=%p CreateInstance=%p DestroyInstance=%p",
             (void*)vkp::GetInstanceProcAddr, (void*)vkp::CreateInstance,
             (void*)vkp::DestroyInstance);
    Print("vulkan: загрузка loader-level точек", false, d);
    return false;
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkp::GetInstanceProcAddr);
  return true;
}

static bool loadInstanceLevel(VkInstance instance)
{
#define LOAD_INST(name) vkp::name = (PFN_vk##name)vkp::GetInstanceProcAddr(instance, "vk" #name)
  LOAD_INST(DestroyInstance);
  LOAD_INST(EnumeratePhysicalDevices);
  LOAD_INST(GetPhysicalDeviceProperties);
  LOAD_INST(GetPhysicalDeviceQueueFamilyProperties);
  LOAD_INST(CreateDevice);
  LOAD_INST(DestroyDevice);
  LOAD_INST(GetDeviceQueue);
#undef LOAD_INST
  if (!vkp::DestroyInstance || !vkp::EnumeratePhysicalDevices || !vkp::CreateDevice ||
      !vkp::GetDeviceQueue)
  {
    Print("vulkan: instance-level точки входа", false);
    return false;
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, vkp::GetInstanceProcAddr, VK_NULL_HANDLE);
  return true;
}

// ---------------------------------------------------------------- smoke render

// Минимальный конец-в-конец без шейдеров и без окна: аллоцировать RT, залить
// его, скопировать в staging (CPU-read), отмапить и проверить пиксель.
// Заодно это первый реальный тест раскладок/транзишенов Vulkan-бэкенда.
static bool SmokeRender(nvrhi::IDevice* dev)
{
  const uint32_t w = 16, h = 16;

  nvrhi::TextureHandle rt = dev->createTexture(nvrhi::TextureDesc()
                                                 .setDimension(nvrhi::TextureDimension::Texture2D)
                                                 .setFormat(nvrhi::Format::SRGBA8_UNORM)
                                                 .setWidth(w).setHeight(h)
                                                 .setIsRenderTarget(true)
                                                 // Без initialState+keepInitialState трекер состояний
                                                 // не знает предыдущее состояние и падает в
                                                 // requireTextureState (проверено).
                                                 .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                                 .setKeepInitialState(true)
                                                 .setDebugName("probe-rt"));
  if (!rt)
  {
    Print("smoke: createTexture(RT)", false);
    return false;
  }

  nvrhi::StagingTextureHandle staging = dev->createStagingTexture(
      nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
                          .setFormat(nvrhi::Format::SRGBA8_UNORM)
                          .setWidth(w).setHeight(h)
                          .setInitialState(nvrhi::ResourceStates::CopyDest)
                          .setKeepInitialState(true),
      nvrhi::CpuAccessMode::Read);
  if (!staging)
  {
    Print("smoke: createStagingTexture", false);
    return false;
  }

  nvrhi::CommandListHandle cl = dev->createCommandList();
  cl->open();
  // RENDER PASS'ОВ В ЭТОЙ ВЕРСИИ NVRHI НЕТ — очистка явная.
  cl->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(), nvrhi::Color(0.f, 0.5f, 1.f, 1.f));
  cl->copyTexture(staging, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
  cl->close();
  dev->executeCommandList(cl);
  dev->waitForIdle();

  size_t rowPitch = 0;
  void* pixels = dev->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
  if (!pixels)
  {
    Print("smoke: mapStagingTexture", false);
    return false;
  }

  // SRGB8_UNORM: ожидаем нелинейные байты; сравниваем по каналам с допуском.
  const uint8_t* px = static_cast<const uint8_t*>(pixels);
  const uint8_t r = px[0], g = px[1], b = px[2], a = px[3];
  dev->unmapStagingTexture(staging);

  char detail[128];
  snprintf(detail, sizeof(detail), "пиксель=(%u,%u,%u,%u), pitch=%lu",
           (unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a, (unsigned long)rowPitch);
  const bool ok = (r < 40) && (g > 100 && g < 190) && (b > 200) && (a > 200);
  Print("smoke: clear -> copy -> readback", ok, detail);
  return ok;
}

// ---------------------------------------------------------------- Vulkan probe

static bool ProbeVulkan()
{
  if (!loadVulkanLoader())
    return false;

  VkApplicationInfo app = {};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "nvrhi_probe";
  app.apiVersion = VK_API_VERSION_1_3;

  // Surface-расширения просим только там, где они точно есть; headless-смоук
  // работает и без них.
#ifdef _WIN32
  const char* exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
  const uint32_t extCount = 2;
#else
  const char* exts[] = { VK_KHR_SURFACE_EXTENSION_NAME };
  const uint32_t extCount = 1;
#endif

  VkInstanceCreateInfo ici = {};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = extCount;
  ici.ppEnabledExtensionNames = exts;

  VkInstance instance = VK_NULL_HANDLE;
  VkResult r = vkp::CreateInstance(&ici, nullptr, &instance);
  if (r != VK_SUCCESS)
  {
    // Различить «нет драйвера вообще» и «драйвер есть, но нет surface-расширения».
    VkInstanceCreateInfo bare = {};
    bare.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    bare.pApplicationInfo = &app;
    VkInstance probe = VK_NULL_HANDLE;
    const VkResult r2 = vkp::CreateInstance(&bare, nullptr, &probe);
    if (r2 == VK_SUCCESS)
    {
      vkp::DestroyInstance(probe, nullptr);
      Print("vulkan: vkCreateInstance", false,
            "драйвер ЕСТЬ, но нет surface-расширений — смоук без окна");
      instance = VK_NULL_HANDLE;
      ici.enabledExtensionCount = 0;
      ici.ppEnabledExtensionNames = nullptr;
      if (vkp::CreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
      {
        Print("vulkan: vkCreateInstance (без surface)", false);
        return false;
      }
    }
    else
    {
      Print("vulkan: vkCreateInstance", false, vkResultToString(r2));
      return false;
    }
  }
  else
  {
    Print("vulkan: vkCreateInstance", true, "со surface-расширениями");
  }

  if (!loadInstanceLevel(instance))
  {
    vkp::DestroyInstance(instance, nullptr);
    return false;
  }

  uint32_t count = 0;
  vkp::EnumeratePhysicalDevices(instance, &count, nullptr);
  if (count == 0)
  {
    Print("vulkan: vkEnumeratePhysicalDevices", false, "нет устройств");
    vkp::DestroyInstance(instance, nullptr);
    return false;
  }

  VkPhysicalDevice devices[16];
  if (count > 16)
    count = 16;
  vkp::EnumeratePhysicalDevices(instance, &count, devices);

  VkPhysicalDevice phys = devices[0];
  VkPhysicalDeviceProperties props = {};
  vkp::GetPhysicalDeviceProperties(phys, &props);
  for (uint32_t i = 0; i < count; ++i)
  {
    VkPhysicalDeviceProperties p = {};
    vkp::GetPhysicalDeviceProperties(devices[i], &p);
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
      phys = devices[i];
      props = p;
      break;
    }
  }
  printf("       %d устройств(а); выбрано: %s (api %u.%u.%u)\n", (int)count, props.deviceName,
         VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
         VK_API_VERSION_PATCH(props.apiVersion));

  uint32_t qCount = 0;
  vkp::GetPhysicalDeviceQueueFamilyProperties(phys, &qCount, nullptr);
  uint32_t graphicsFamily = 0;
  bool found = false;
  for (uint32_t i = 0; i < qCount && i < 16; ++i)
  {
    VkQueueFamilyProperties fam[16] = {};
    uint32_t n = qCount > 16 ? 16 : qCount;
    vkp::GetPhysicalDeviceQueueFamilyProperties(phys, &n, fam);
    for (uint32_t j = 0; j < n; ++j)
      if (fam[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
        graphicsFamily = j;
        found = true;
        break;
      }
    if (found)
      break;
  }
  if (!found)
  {
    Print("vulkan: graphics queue family", false);
    vkp::DestroyInstance(instance, nullptr);
    return false;
  }

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci = {};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = graphicsFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;

  // NVRHI вызывает vkCmdPipelineBarrier2 — значит на VkDevice ОБЯЗАНЫ быть
  // включены VK_KHR_synchronization2 (или Vulkan 1.3). В доках NVRHI это не
  // написано; без него падает на assert'е Vulkan-Hpp:
  //   "Function <vkCmdPipelineBarrier2> requires <VK_KHR_synchronization2> or <VK_VERSION_1_3>"
  // (проверено на llvmpipe 1.4.335 — сам драйвер sync2 поддерживает, но
  // включение расширения/фичи обязано сделать приложение).
  // NVRHI требует как минимум Vulkan 1.2 + VK_KHR_synchronization2:
  //   vkCmdPipelineBarrier2 -> assert "requires <VK_KHR_synchronization2> or <VK_VERSION_1_3>"
  //   vkGetSemaphoreCounterValue -> assert "requires <VK_KHR_timeline_semaphore> or <VK_VERSION_1_2>"
  // (проверено на llvmpipe 1.4.335; в доках NVRHI нигде не написано).
  VkPhysicalDeviceVulkan12Features f12 = {};
  f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  f12.timelineSemaphore = VK_TRUE;
  f12.bufferDeviceAddress = VK_TRUE;
  f12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;

  VkPhysicalDeviceVulkan13Features f13 = {};
  f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  f13.synchronization2 = VK_TRUE;
  f13.dynamicRendering = VK_TRUE;
  f13.subgroupSizeControl = VK_TRUE;
  f13.computeFullSubgroups = VK_TRUE;
  f12.pNext = &f13;

  const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME };
  VkDeviceCreateInfo dci = {};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = &f12;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 2;
  dci.ppEnabledExtensionNames = devExts;

  VkDevice device = VK_NULL_HANDLE;
  r = vkp::CreateDevice(phys, &dci, nullptr, &device);
  if (r != VK_SUCCESS)
  {
    Print("vulkan: vkCreateDevice", false, vkResultToString(r));
    vkp::DestroyInstance(instance, nullptr);
    return false;
  }
  Print("vulkan: vkCreateDevice", true, "ext: swapchain + synchronization2");

  VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, vkp::GetInstanceProcAddr, device);

  VkQueue queue = VK_NULL_HANDLE;
  vkp::GetDeviceQueue(device, graphicsFamily, 0, &queue);

  nvrhi::vulkan::DeviceDesc dd;
  dd.instance = instance;
  dd.physicalDevice = phys;
  dd.device = device;
  dd.graphicsQueue = queue;
  dd.graphicsQueueIndex = (int)graphicsFamily;
  nvrhi::vulkan::DeviceHandle nvrhiDevice = nvrhi::vulkan::createDevice(dd);

  const bool ok = bool(nvrhiDevice);
  Print("vulkan: nvrhi::vulkan::createDevice", ok);

  if (ok)
  {
    printf("       caps: ComputeQueue=%d Meshlets=%d RayTracingPipeline=%d VRS=%d\n",
           (int)nvrhiDevice->queryFeatureSupport(nvrhi::Feature::ComputeQueue),
           (int)nvrhiDevice->queryFeatureSupport(nvrhi::Feature::Meshlets),
           (int)nvrhiDevice->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline),
           (int)nvrhiDevice->queryFeatureSupport(nvrhi::Feature::VariableRateShading));

    // Единственная штатная замена D3DQUERYTYPE_EVENT (Renderer::Synchronize).
    nvrhi::EventQueryHandle ev = nvrhiDevice->createEventQuery();
    nvrhiDevice->setEventQuery(ev, nvrhi::CommandQueue::Graphics);
    nvrhiDevice->waitEventQuery(ev);
    nvrhiDevice->resetEventQuery(ev);
    Print("vulkan: IEventQuery (замена D3DQUERYTYPE_EVENT)", true);

    SmokeRender(nvrhiDevice);
  }

  nvrhiDevice = nullptr;
  vkp::DestroyDevice(device, nullptr);
  vkp::DestroyInstance(instance, nullptr);
  return ok;
}

// ---------------------------------------------------------------- D3D11 (Windows)

#ifdef _WIN32
static bool ProbeD3D11()
{
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL got = {};
  const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };

  // WARP обязателен как второй заход: на ВМ без GPU (tiny10 = Microsoft Basic
  // Display Adapter) hardware-устройство не даётся, а WARP даёт D3D11 полностью.
  const D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
  HRESULT hr = E_FAIL;
  for (D3D_DRIVER_TYPE t : types)
  {
    hr = D3D11CreateDevice(nullptr, t, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                           &device, &got, &ctx);
    if (SUCCEEDED(hr))
    {
      printf("       D3D11 driver type = %d\n", (int)t);
      break;
    }
  }
  if (FAILED(hr))
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "hr=0x%08X", (unsigned)hr);
    Print("d3d11: D3D11CreateDevice", false, buf);
    return false;
  }

  nvrhi::d3d11::DeviceDesc dd;
  dd.context = ctx; // поля device нет — берётся из context
  nvrhi::DeviceHandle nvrhiDevice = nvrhi::d3d11::createDevice(dd);

  const bool ok = bool(nvrhiDevice);
  Print("d3d11: nvrhi::d3d11::createDevice", ok);
  if (ok)
  {
    printf("       feature level 0x%X\n", (unsigned)got);
    SmokeRender(nvrhiDevice);
  }

  nvrhiDevice = nullptr;
  device->Release();
  ctx->Release();
  return ok;
}
#endif

// ---------------------------------------------------------------- main

int main()
{
  printf("nvrhi_probe: NVRHI 6b96fb03 (vendor/nvrhi/SOURCE_COMMIT.txt)\n\n");

#ifdef _WIN32
  const bool d3d11 = ProbeD3D11();
#else
  const bool d3d11 = true; // не Windows
#endif
  const bool vulkan = ProbeVulkan();

  printf("\nитог: d3d11=%d vulkan=%d\n", (int)d3d11, (int)vulkan);
  return (d3d11 && vulkan) ? 0 : 1;
}
