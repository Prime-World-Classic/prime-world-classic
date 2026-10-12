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
#include <vector>

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
#ifdef _WIN32
  PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR = nullptr;
  PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR = nullptr;
  PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
  PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
  PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
  PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
  PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
  PFN_vkCreateSemaphore CreateSemaphore = nullptr;
  PFN_vkDestroySemaphore DestroySemaphore = nullptr;
#endif
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
#ifdef _WIN32
  LOAD_INST(DestroySurfaceKHR);
  LOAD_INST(GetPhysicalDeviceSurfaceSupportKHR);
  LOAD_INST(GetPhysicalDeviceSurfaceCapabilitiesKHR);
  LOAD_INST(GetPhysicalDeviceSurfaceFormatsKHR);
  LOAD_INST(GetPhysicalDeviceSurfacePresentModesKHR);
  LOAD_INST(CreateSwapchainKHR);
  LOAD_INST(DestroySwapchainKHR);
  LOAD_INST(GetSwapchainImagesKHR);
  LOAD_INST(AcquireNextImageKHR);
  LOAD_INST(QueuePresentKHR);
  LOAD_INST(CreateSemaphore);
  LOAD_INST(DestroySemaphore);
  // surface-функции — instance-level (через инстанс), swapchain — device-level
  // (через устройство): vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR")
  // отдаёт trampoline, этого достаточно.
  vkp::CreateWin32SurfaceKHR =
      (PFN_vkCreateWin32SurfaceKHR)vkp::GetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
#endif
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

#ifdef _WIN32
static bool ProbeWin32Swapchain(nvrhi::vulkan::DeviceHandle nvrhiDevice, VkInstance instance,
                                VkPhysicalDevice phys, VkDevice device, VkQueue queue,
                                uint32_t queueFamily);
#endif


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

  // Лимиты, которые определяют форму будущего NVRHI-бэкенда клиента:
  // клиент использует до 16 PS-сэмплеров и массив из 261 указателя текстур
  // (Render/sampler.h, GlobalMasks.h), плюс 2 цвета + depth в RT.
  printf("       limits: sampledImages/stage=%u samplers/stage=%u uniformBuffers/stage=%u\n"
         "               boundDescriptorSets=%u maxTex2D=%u colorAttachments=%u\n"
         "               vertexInputAttributes=%u maxSamplerArray=%u\n",
         props.limits.maxPerStageDescriptorSampledImages,
         props.limits.maxPerStageDescriptorSamplers,
         props.limits.maxPerStageDescriptorUniformBuffers,
         props.limits.maxBoundDescriptorSets, props.limits.maxImageDimension2D,
         props.limits.maxColorAttachments, props.limits.maxVertexInputAttributes,
         props.limits.maxDescriptorSetSampledImages);

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
#ifdef _WIN32
    ProbeWin32Swapchain(nvrhiDevice, instance, phys, device, queue, graphicsFamily);
#endif
  }

  nvrhiDevice = nullptr;
  vkp::DestroyDevice(device, nullptr);
  vkp::DestroyInstance(instance, nullptr);
  return ok;
}

#ifdef _WIN32
// ------------------------------------------------- окно + surface + swapchain

// Единственное место пробы, где приходится писать нативный Vulkan/Win32 в обход
// NVRHI: в NVRHI 6b96fb03 нет ни surface, ни swapchain (см. §3.x плана).
// Проверяет ровно то, что понадобится клиенту: окно -> VkSurfaceKHR ->
// VkSwapchainKHR -> обёртка бэк-буфера -> clear -> present.
static LRESULT CALLBACK ProbeWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  return DefWindowProcA(h, m, w, l);
}

static bool ProbeWin32Swapchain(nvrhi::vulkan::DeviceHandle nvrhiDevice, VkInstance instance,
                                VkPhysicalDevice phys, VkDevice device, VkQueue queue,
                                uint32_t queueFamily)
{
  if (!vkp::CreateWin32SurfaceKHR || !vkp::CreateSwapchainKHR)
  {
    Print("win32: surface/swapchain точки", false);
    return false;
  }

  const char clsName[] = "nvrhi_probe_wnd";
  WNDCLASSEXA wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = ProbeWndProc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = clsName;
  RegisterClassExA(&wc);
  HWND hwnd = CreateWindowExA(0, clsName, "nvrhi_probe", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                              nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd)
  {
    Print("win32: CreateWindowExA", false);
    return false;
  }
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);
  Print("win32: окно 128x128", true);

  VkWin32SurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  sci.hinstance = wc.hInstance;
  sci.hwnd = hwnd;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (vkp::CreateWin32SurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS)
  {
    Print("win32: vkCreateWin32SurfaceKHR", false);
    DestroyWindow(hwnd);
    return false;
  }
  Print("win32: VkSurfaceKHR", true);

  VkBool32 supported = VK_FALSE;
  vkp::GetPhysicalDeviceSurfaceSupportKHR(phys, queueFamily, surface, &supported);
  if (!supported)
  {
    Print("win32: surface supported", false);
    goto done_surface;
  }
  Print("win32: vkGetPhysicalDeviceSurfaceSupportKHR", true);

  {
    VkSurfaceCapabilitiesKHR caps = {};
    vkp::GetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkp::GetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    VkSurfaceFormatKHR fmts[16] = {};
    if (fmtCount > 16)
      fmtCount = 16;
    vkp::GetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts);
    if (fmtCount == 0)
    {
      Print("win32: surface formats", false);
      goto done_surface;
    }
    // Предпочтение: BGRA8 UNORM sRGB (то, что даёт nvrhi::Format::SBGRA8_UNORM).
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fmtCount; ++i)
      if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
          fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      {
        chosen = fmts[i];
        break;
      }

    uint32_t pmCount = 0;
    vkp::GetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, nullptr);
    VkPresentModeKHR pms[16] = {};
    if (pmCount > 16)
      pmCount = 16;
    vkp::GetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmCount, pms);
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR; // обязан быть по спецификации
    bool pmFound = false;
    for (uint32_t i = 0; i < pmCount; ++i)
      if (pms[i] == VK_PRESENT_MODE_FIFO_KHR)
        pmFound = true;
    if (!pmFound && pmCount)
      pm = pms[0];

    uint32_t width = caps.currentExtent.width != 0xFFFFFFFFu
                         ? caps.currentExtent.width
                         : 128;
    uint32_t height = caps.currentExtent.height != 0xFFFFFFFFu
                          ? caps.currentExtent.height
                          : 128;
    if (width < caps.minImageExtent.width) width = caps.minImageExtent.width;
    if (height < caps.minImageExtent.height) height = caps.minImageExtent.height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
      imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swci = {};
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface;
    swci.minImageCount = imageCount;
    swci.imageFormat = chosen.format;
    swci.imageColorSpace = chosen.colorSpace;
    swci.imageExtent = { width, height };
    swci.imageArrayLayers = 1;
    // TRANSFER_SRC|DST не просто так: lavapipe на Windows реализует swapchain
    // через собственный host-буфер и блит на present, и без transfer-флага
    // present падает в VK_ERROR_MEMORY_MAP_FAILED (проверено на tiny10).
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = pm;
    swci.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult r = vkp::CreateSwapchainKHR(device, &swci, nullptr, &swapchain);
    if (r != VK_SUCCESS)
    {
      Print("win32: vkCreateSwapchainKHR", false, nvrhi::vulkan::resultToString(r));
      goto done_surface;
    }
    char d[96];
    snprintf(d, sizeof(d), "%ux%u, картинок %u, формат %d", width, height, imageCount,
             (int)chosen.format);
    Print("win32: VkSwapchainKHR", true, d);

    {
      uint32_t imgCount = 0;
      vkp::GetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
      std::vector<VkImage> images(imgCount);
      vkp::GetSwapchainImagesKHR(device, swapchain, &imgCount, images.data());

      nvrhi::Format nf = (chosen.format == VK_FORMAT_B8G8R8A8_UNORM)
                             ? nvrhi::Format::SBGRA8_UNORM
                             : nvrhi::Format::SRGBA8_UNORM;
      nvrhi::TextureHandle back = nvrhiDevice->createHandleForNativeTexture(
          nvrhi::ObjectTypes::VK_Image, nvrhi::Object(images[0]),
          nvrhi::TextureDesc()
              .setDimension(nvrhi::TextureDimension::Texture2D)
              .setFormat(nf)
              .setWidth(width).setHeight(height)
              .setIsRenderTarget(true)
              .setInitialState(nvrhi::ResourceStates::RenderTarget)
              .setKeepInitialState(true)
              .setDebugName("probe-swapchain-image"));
      if (!back)
      {
        Print("win32: createHandleForNativeTexture(бэк-буфер)", false);
      }
      else
      {
        Print("win32: createHandleForNativeTexture(бэк-буфер)", true);

        nvrhi::FramebufferHandle fb = nvrhiDevice->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(back));
        nvrhi::CommandListHandle cl = nvrhiDevice->createCommandList();
        cl->open();
        cl->clearTextureFloat(back, nvrhi::TextureSubresourceSet(), nvrhi::Color(1.f, 0.f, 0.f, 1.f));
        cl->close();
        nvrhiDevice->executeCommandList(cl);

        VkSemaphore acquired = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo semci = {};
        semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkp::CreateSemaphore(device, &semci, nullptr, &acquired);
        uint32_t imageIndex = 0;
        r = vkp::AcquireNextImageKHR(device, swapchain, ~0ull, acquired, VK_NULL_HANDLE, &imageIndex);
        if (r == VK_SUCCESS)
        {
          // Порядок для Vulkan: бэк-буфер после acquire лежит в Undefined;
          // NVRHI-трек состояний об этом не знает, поэтому переход делает
          // clear+sync2 (в probe initialState выставлен вручную).
          nvrhiDevice->waitForIdle();
          VkPresentInfoKHR pi = {};
          pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
          pi.waitSemaphoreCount = 1;
          pi.pWaitSemaphores = &acquired;
          pi.swapchainCount = 1;
          pi.pSwapchains = &swapchain;
          pi.pImageIndices = &imageIndex;
          r = vkp::QueuePresentKHR(queue, &pi);
          Print("win32: vkQueuePresentKHR (clear красный)", r == VK_SUCCESS,
                r == VK_SUCCESS ? "" : nvrhi::vulkan::resultToString(r));
        }
        else
        {
          Print("win32: vkAcquireNextImageKHR", false, nvrhi::vulkan::resultToString(r));
        }
        if (acquired)
          vkp::DestroySemaphore(device, acquired, nullptr);
        vkp::DestroySwapchainKHR(device, swapchain, nullptr);
      }
    }
  }

done_surface:
  vkp::DestroySurfaceKHR(instance, surface, nullptr);
  DestroyWindow(hwnd);
  return true;
}

#endif // _WIN32 (окно/swapchain)

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
