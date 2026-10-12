// nvrhi_probe — этап 0 плана docs/PLAN_NVRHI.md. Вне графа клиента.
//
// Что проверяется (каждый шаг независимо, чтобы один сбой не прятал остальные):
//   1. NVRHI собирается нашим тулчейном (cl 14.44 под Wine, C++17, статика, /MT);
//   2. D3D11-бэкенд: nvrhi::d3d11::createDevice поверх D3D11CreateDevice;
//   3. Vulkan-бэкенд: бутстрап руками (NVRHI устройство НЕ создаёт) +
//      nvrhi::vulkan::createDevice.
//
// Проверено по исходникам NVRHI 6b96fb03 и подтверждено этим прогоном:
//   - swapchain-абстракции нет вообще: бэк-буфер оборачивается через
//     IDevice::createHandleForNativeTexture;
//   - occlusion-запросов нет (только IEventQuery / ITimerQuery);
//   - d3d11::DeviceDesc принимает ТОЛЬКО context (устройство берётся из него),
//     отдельного поля device нет;
//   - при СТАТИЧЕСКОЙ сборке NVRHI сам НЕ инициализирует Vulkan-Hpp диспетчер
//     (VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE обёрнут в
//     #if defined(NVRHI_SHARED_LIBRARY_BUILD)) -> это обязанность приложения,
//     см. initVulkanDispatcher();
//   - Vulkan-Hpp в свежих Vulkan-Headers местами несовместим с тем, что пишет
//     NVRHI-документация, поэтому бутстрап здесь на чистом C API.
//
// Vulkan намеренно грузится динамически (LoadLibrary vulkan-1.dll): Vulkan SDK
// в этом окружении нет, import-библиотеки нет, а под Wine нужен winevulkan.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
// import-либы Vulkan SDK в этом окружении нет -> прототипы не объявляем,
// все точки входа берём из vulkan-1.dll через vkGetInstanceProcAddr.
#define VK_NO_PROTOTYPES 1

// windows.h ДО vulkan_win32.h (нужны HINSTANCE/HWND/HANDLE).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <vulkan/vulkan.hpp>

#include <nvrhi/nvrhi.h>
#include <nvrhi/d3d11.h>
#include <nvrhi/vulkan.h>

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
  case VK_SUCCESS:                        return "VK_SUCCESS";
  case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  default:                                return "other VkResult";
  }
}

// ---------------------------------------------------------------- D3D11

static bool ProbeD3D11()
{
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL got = {};
  const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };

  const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                       levels, 2, D3D11_SDK_VERSION, &device, &got, &ctx);
  if (FAILED(hr))
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "hr=0x%08X", (unsigned)hr);
    Print("d3d11: D3D11CreateDevice", false, buf);
    return false;
  }

  nvrhi::d3d11::DeviceDesc dd;
  dd.context = ctx;                       // поля device нет — берётся из context
  nvrhi::DeviceHandle nvrhiDevice = nvrhi::d3d11::createDevice(dd);

  const bool ok = bool(nvrhiDevice);
  Print("d3d11: nvrhi::d3d11::createDevice", ok);
  if (ok)
    printf("       feature level 0x%X\n", (unsigned)got);

  nvrhiDevice = static_cast<nvrhi::DeviceHandle>(nullptr);
  device->Release();
  ctx->Release();
  return ok;
}

// ---------------------------------------------------------------- Vulkan

// NVRHI при статической сборке не трогает диспетчер Vulkan-Hpp, а прототипы
// vulkan-1 мы не линкуем -> грузим сами. Это же делает и приложение-хост:
// в клиенте будет ровно этот шаг.
// Минимальный набор точек входа Vulkan, который нужен бутстрапу. Всё, что
// дальше делает NVRHI, resolves через Vulkan-Hpp диспетчер.
namespace vkp
{
  PFN_vkGetInstanceProcAddr  GetInstanceProcAddr  = nullptr;
  PFN_vkCreateInstance       CreateInstance       = nullptr;
  PFN_vkDestroyInstance      DestroyInstance      = nullptr;
  PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
  PFN_vkCreateDevice         CreateDevice         = nullptr;
  PFN_vkDestroyDevice        DestroyDevice        = nullptr;
  PFN_vkGetDeviceQueue       GetDeviceQueue       = nullptr;
}

static bool initVulkanDispatcher(VkInstance instance, VkDevice device)
{
  static HMODULE mod = nullptr;
  if (!mod)
  {
    mod = LoadLibraryA("vulkan-1.dll");
    if (!mod)
    {
      Print("vulkan: LoadLibrary(vulkan-1.dll)", false, "нет winevulkan / драйвера");
      return false;
    }
    vkp::GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(mod, "vkGetInstanceProcAddr");
    if (!vkp::GetInstanceProcAddr)
    {
      Print("vulkan: vkGetInstanceProcAddr", false);
      return false;
    }
    // ВАЖНО (проверено на winevulkan): vkGetInstanceProcAddr(NULL, ...)
    // возвращает NULL для instance-level функций (vkEnumeratePhysicalDevices
    // и т.п.). До создания инстанса грузим только loader-level точки.
#define LOAD_LOADER(name) vkp::name = (PFN_vk##name)vkp::GetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name)
    LOAD_LOADER(CreateInstance);
    LOAD_LOADER(DestroyInstance);
#undef LOAD_LOADER
    if (!vkp::CreateInstance || !vkp::DestroyInstance)
    {
      Print("vulkan: загрузка точек входа", false, "нет vkCreateInstance/vkDestroyInstance");
      return false;
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkp::GetInstanceProcAddr);
  }
  if (instance != VK_NULL_HANDLE)
  {
    // 3-аргументная форма: единственный перегруженный вариант, который
    // принимает сырые VkInstance/VkDevice (остальные требуют vk::Instance /
    // vk::Device и с сырыми хендлами выбирают шаблон init<DynamicLoader>).
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, vkp::GetInstanceProcAddr, device);
  }
  else
  {
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkp::GetInstanceProcAddr);
  }
  return true;
}

static const char* kInstanceExts[] = {
  VK_KHR_SURFACE_EXTENSION_NAME,
  VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};

static bool ProbeVulkan()
{
  if (!initVulkanDispatcher(VK_NULL_HANDLE, VK_NULL_HANDLE))
    return false;

  VkApplicationInfo app = {};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "nvrhi_probe";
  app.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo ici = {};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = 2;
  ici.ppEnabledExtensionNames = kInstanceExts;

  VkInstance instance = VK_NULL_HANDLE;
  VkResult r = vkp::CreateInstance(&ici, nullptr, &instance);
  if (r != VK_SUCCESS)
  {
    // Различить «нет драйвера Vulkan вообще» и «драйвер есть, но нет
    // surface-расширения» (под Wine без окна/видеодрайвера — второе).
    VkInstanceCreateInfo bare = {};
    bare.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    bare.pApplicationInfo = &app;
    VkInstance probe = VK_NULL_HANDLE;
    const VkResult r2 = vkp::CreateInstance(&bare, nullptr, &probe);
    if (r2 == VK_SUCCESS)
    {
      vkp::DestroyInstance(probe, nullptr);
      Print("vulkan: vkCreateInstance", false,
            "драйвер ЕСТЬ, но нет VK_KHR_surface/win32_surface — "
            "нужен нативный Windows (tiny10) либо winevulkan с окном");
    }
    else
    {
      Print("vulkan: vkCreateInstance", false, vkResultToString(r2));
    }
    return false;
  }
  Print("vulkan: vkCreateInstance", true, "ext: surface + win32_surface");

  if (!initVulkanDispatcher(instance, VK_NULL_HANDLE))
    return false;

  // instance-level точки — только через инстанс
#define LOAD_INST(name) vkp::name = (PFN_vk##name)vkp::GetInstanceProcAddr(instance, "vk" #name)
  LOAD_INST(EnumeratePhysicalDevices);
  LOAD_INST(GetPhysicalDeviceProperties);
  LOAD_INST(CreateDevice);
  LOAD_INST(DestroyDevice);
  LOAD_INST(GetDeviceQueue);
#undef LOAD_INST
  if (!vkp::EnumeratePhysicalDevices || !vkp::CreateDevice || !vkp::GetPhysicalDeviceProperties)
  {
    Print("vulkan: instance-level точки входа", false);
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
  count = count > 16 ? 16 : count;
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

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci = {};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = 0;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;

  const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkDeviceCreateInfo dci = {};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = devExts;

  VkDevice device = VK_NULL_HANDLE;
  r = vkp::CreateDevice(phys, &dci, nullptr, &device);
  if (r != VK_SUCCESS)
  {
    Print("vulkan: vkCreateDevice", false, vkResultToString(r));
    vkp::DestroyInstance(instance, nullptr);
    return false;
  }
  Print("vulkan: vkCreateDevice", true, "ext: VK_KHR_swapchain");

  if (!initVulkanDispatcher(instance, device))
    return false;

  VkQueue queue = VK_NULL_HANDLE;
  vkp::GetDeviceQueue(device, 0, 0, &queue);

  nvrhi::vulkan::DeviceDesc dd;
  dd.instance = instance;
  dd.physicalDevice = phys;
  dd.device = device;
  dd.graphicsQueue = queue;
  dd.graphicsQueueIndex = 0;
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

    // Смоук командного списка: создать и закрыть без содержимого.
    nvrhi::CommandListHandle cl = nvrhiDevice->createCommandList();
    cl->open();
    cl->close();
    nvrhiDevice->executeCommandList(cl);
    nvrhiDevice->waitForIdle();
    Print("vulkan: empty command list + waitForIdle", true);
  }

  nvrhiDevice = static_cast<nvrhi::vulkan::DeviceHandle>(nullptr);
  vkp::DestroyDevice(device, nullptr);
  vkp::DestroyInstance(instance, nullptr);
  return ok;
}

// ---------------------------------------------------------------- main

int main()
{
  printf("nvrhi_probe: NVRHI 6b96fb03 (vendor/nvrhi/SOURCE_COMMIT.txt)\n\n");

  const bool d3d11 = ProbeD3D11();
  const bool vulkan = ProbeVulkan();

  printf("\nитог: d3d11=%d vulkan=%d\n", (int)d3d11, (int)vulkan);
  return (d3d11 && vulkan) ? 0 : 1;
}
