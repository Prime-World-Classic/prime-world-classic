//////////////////////////////////////////////////////////////////////////
// Vulkan-бэкенд поверх NVRHI — шаг 12a очереди этапа 3 (docs/PLAN_NVRHI.md §3.y, §3.z).
//
// Что здесь реально работает: VkInstance -> VkDevice (обязательные extensions
// swapchain + synchronization2 + dynamic_rendering) -> VkSurfaceKHR из HWND
// seam'а -> swapchain -> кадр (clear бэк-буфера + present). Всё остальное из
// IRenderBackend — заглушки с учётом вызовов (NVRHI_STUB_LIST): их счётчик и
// есть work-list шага 13, снятый по фактическим вызовам клиента.
//
// Почему отдельный TU и почему pimpl: NVRHI и Vulkan-Hpp требуют C++17, а TU
// проекта Render компилируются без /std: (дефолт cl 14.44 = C++14; замер FLAGS
// в Tools/ClientCMake/build/x64-wine/build.ninja). Поэтому /std:c++17 /EHsc
// выдаётся пофайлово только этому файлу (Tools/ClientCMake/gen_cmake.py,
// X64_EXTRA_OPTS), а типы NVRHI/Vulkan наружу не выпускаются (nvrhibackend.h
// не включает ни vulkan.h, ни nvrhi.h).
//
// Диспетчер Vulkan: в статической сборке NVRHI хранилище динамического
// диспетчера и его инициализация обёрнуты в #if defined(NVRHI_SHARED_LIBRARY_BUILD)
// (Vendor/NVRHI/src/vulkan/vulkan-device.cpp:31 и :46), т.е. и то и другое —
// обязанность приложения. Собственно поэтому здесь есть VULKAN_HPP_DEFAULT_
// DISPATCH_LOADER_DYNAMIC_STORAGE и ручной LoadLibrary("vulkan-1.dll"):
// импорт-библиотека vulkan-1.lib тулчейну не нужна (проверено на спайке
// tools/nvrhi_probe — в импортах exe нет vulkan-1.dll).
//
#include "stdafx.h"
#include "nvrhibackend.h"

// windows.h (через stdafx.h) приносит макросы min/max, которые ломают шаблонные
// выражения Vulkan-Hpp и NVRHI.
#undef min
#undef max

#define VK_NO_PROTOTYPES 1
#define VK_USE_PLATFORM_WIN32_KHR 1
// NVRHI включает динамический диспетчер сам (Vendor/NVRHI/src/vulkan/
// vulkan-backend.h:33); здесь то же значение должно быть на момент включения
// заголовков, иначе объявится статический диспетчер и ссылки разъедутся.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <vulkan/vulkan.hpp>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Render
{

//////////////////////////////////////////////////////////////////////////
// Учёт незакрытых методов. Порядок списка = порядок, в котором методы идут в
// seam'е; имена попадают в DebugTrace, так что дамп читается как план работ.
#define NVRHI_STUB_LIST(X) \
  X(BeginScene) X(EndScene) X(Clear) \
  X(SetStreamSource) X(SetIndices) X(SetInputLayout) X(SetVertexShader) X(SetPixelShader) \
  X(SetTexture) X(SetRenderTarget) X(SetDepthStencil) X(GetDepthStencil) X(SetViewport) \
  X(DrawIndexed) X(Draw) X(DrawIndexedUP) X(DrawUP) \
  X(SetInstanceCount) X(SetInstanceSource) X(IsStreamBufferBound) \
  X(CreateVertexShader) X(CreatePixelShader) X(CreateInputLayout) \
  X(CreateTexture2D) X(CreateTextureCube) X(CreateSurface) X(UpdateTexture) \
  X(CreateVertexBuffer) X(CreateIndexBuffer) \
  X(LockVertexBuffer) X(UnlockVertexBuffer) X(LockIndexBuffer) X(UnlockIndexBuffer) \
  X(GetRenderTargetData) X(ColorFill) X(StretchRect) X(ReadSurface) X(GetRenderTarget)

enum EStubId
{
#define NVRHI_STUB_ENUM(n) st_##n,
  NVRHI_STUB_LIST(NVRHI_STUB_ENUM)
#undef NVRHI_STUB_ENUM
    st_count
};

static const char* const kStubNames[st_count] = {
#define NVRHI_STUB_NAME(n) #n,
  NVRHI_STUB_LIST(NVRHI_STUB_NAME)
#undef NVRHI_STUB_NAME
};

//////////////////////////////////////////////////////////////////////////
// Поверхности отдаются клиенту ручкой HSurface. В D3D9Backend это указатель на
// COM-объект; здесь — индекс в таблице (0xffffffff = «нет»), потому что
// nvrhi::TextureHandle — счётный умный указатель и хранить его в ручке нельзя.
struct SurfaceObj
{
  nvrhi::TextureHandle tex;
  bool backBuffer;
  SurfaceObj() : backBuffer(false) {}
};

struct NvrhiBackendImpl
{
  // Vulkan-уровень
  HMODULE hVulkan;
  PFN_vkGetInstanceProcAddr gipa;
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  uint32_t queueFamily;
  VkSurfaceKHR surface;
  HWND window;                    // окно для сверки размера (Present может нести 0)
  VkSwapchainKHR swapchain;
  VkFormat swapFormat;
  VkColorSpaceKHR swapSpace;
  VkPresentModeKHR presentMode;
  unsigned width, height;
  VkSemaphore acquired;
  unsigned imageIndex;

  nvrhi::DeviceHandle nvrhiDevice;
  nvrhi::CommandListHandle commandList;

  std::vector<nvrhi::TextureHandle> backTextures;   // по одному на картинку swapchain'а
  std::vector<SurfaceObj> surfaces;                 // индекс + 1 = значение HSurface.p
  unsigned currentSurface;                          // поверхность текущего бэк-буфера

  VkPhysicalDeviceProperties props;
  char deviceName[128];

  unsigned stubCalls[st_count];
  unsigned presents;
  unsigned swapRecreates;         // сколько раз пересоздали swapchain

  NvrhiBackendImpl()
    : hVulkan(0), gipa(0), instance(0), physical(0), device(0), queue(0), queueFamily(0),
      surface(0), window(0), swapchain(0), swapFormat(VK_FORMAT_UNDEFINED),
      swapSpace(VkColorSpaceKHR(0)), presentMode(VK_PRESENT_MODE_FIFO_KHR),
      width(0), height(0), acquired(0), imageIndex(0), currentSurface(0), presents(0),
      swapRecreates(0)
  {
    deviceName[0] = 0;
    memset(stubCalls, 0, sizeof(stubCalls));
  }
};

//////////////////////////////////////////////////////////////////////////
// Вспомогательные вывод/ошибки

static void VkNote(const char* what, bool ok, const char* detail)
{
  if (ok)
    DebugTrace("NvrhiBackend: %s — OK%s%s", what, detail ? " " : "", detail ? detail : "");
  else
  {
    DebugTrace("NvrhiBackend: %s — НЕ УДАЛОСЬ%s%s", what, detail ? " " : "", detail ? detail : "");
    systemLog(NLogg::LEVEL_WARNING) << "NvrhiBackend: " << what << (detail ? detail : "") << endl;
  }
}

static const char* VkResultName(VkResult r)
{
  return nvrhi::vulkan::resultToString(r);
}

//////////////////////////////////////////////////////////////////////////

NvrhiBackend::NvrhiBackend()
  : m_pImpl(new NvrhiBackendImpl), m_unsupported(0), m_scissorValid(false), m_clipPlaneEnabled(false),
    m_instanceCount(0)
{
  m_viewport.x = m_viewport.y = 0;
  m_viewport.width = m_viewport.height = 0;
  m_viewport.minZ = 0.0f;
  m_viewport.maxZ = 1.0f;
  memset(&m_scissor, 0, sizeof(m_scissor));
  memset(m_clipPlane, 0, sizeof(m_clipPlane));
  for (int i = 0; i < csConstStageCount; ++i)
    m_constShadow[i] = new float[SHADER_CONST_REGISTERS * 4];
}

NvrhiBackend::~NvrhiBackend()
{
  Shutdown();
  for (int i = 0; i < csConstStageCount; ++i)
    delete[] m_constShadow[i];
  delete m_pImpl;
}

NvrhiBackend* NvrhiBackend::Create(NativeWindow window, unsigned width, unsigned height)
{
  NvrhiBackend* pSelf = new NvrhiBackend();
  if (!pSelf->Init(window, width, height))
  {
    delete pSelf;
    return 0;
  }
  return pSelf;
}

//////////////////////////////////////////////////////////////////////////
// Загрузка Vulkan и создание устройства. Последовательность повторяет
// проверенный спайк tools/nvrhi_probe (ProbeVulkan).

bool NvrhiBackend::Init(NativeWindow window, unsigned width, unsigned height)
{
  NvrhiBackendImpl& d = *m_pImpl;
  d.window = (HWND)window.p;   // нужен Present'у для сверки размера окна

  d.hVulkan = LoadLibraryA("vulkan-1.dll");
  if (!d.hVulkan)
  {
    VkNote("LoadLibrary(vulkan-1.dll)", false, 0);
    return false;
  }
  d.gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(d.hVulkan, "vkGetInstanceProcAddr");
  if (!d.gipa)
  {
    VkNote("vkGetInstanceProcAddr", false, 0);
    return false;
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(d.gipa);

  VkApplicationInfo app;
  memset(&app, 0, sizeof(app));
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "PW_Game";
  app.apiVersion = VK_API_VERSION_1_3;

  const char* instanceExts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
  VkInstanceCreateInfo ici;
  memset(&ici, 0, sizeof(ici));
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = 2;
  ici.ppEnabledExtensionNames = instanceExts;

  VkResult r = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateInstance(&ici, 0, &d.instance);
  if (r != VK_SUCCESS)
  {
    // Различить «Vulkan нет вообще» и «Vulkan есть, но нет surface-расширений»
    // (под Wine без X-сервера бывает и так).
    VkInstanceCreateInfo bare;
    memset(&bare, 0, sizeof(bare));
    bare.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    bare.pApplicationInfo = &app;
    VkInstance probe = 0;
    const VkResult r2 = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateInstance(&bare, 0, &probe);
    if (probe)
      VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyInstance(probe, 0);
    VkNote("vkCreateInstance", false,
           r2 == VK_SUCCESS ? "драйвер есть, но нет surface-расширений" : VkResultName(r2));
    return false;
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(d.instance, d.gipa, VK_NULL_HANDLE);

  uint32_t devCount = 0;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkEnumeratePhysicalDevices(d.instance, &devCount, 0);
  if (!devCount)
  {
    VkNote("vkEnumeratePhysicalDevices", false, "нет устройств");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(devCount);
  VULKAN_HPP_DEFAULT_DISPATCHER.vkEnumeratePhysicalDevices(d.instance, &devCount, &devices[0]);

  d.physical = devices[0];
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceProperties(d.physical, &d.props);
  for (uint32_t i = 0; i < devCount; ++i)
  {
    VkPhysicalDeviceProperties p;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceProperties(devices[i], &p);
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
      d.physical = devices[i];
      d.props = p;
      break;
    }
  }
  strncpy(d.deviceName, d.props.deviceName, sizeof(d.deviceName) - 1);
  d.deviceName[sizeof(d.deviceName) - 1] = 0;
  DebugTrace("NvrhiBackend: устройство %d шт., выбрано %s (api %u.%u.%u)", (int)devCount,
             d.deviceName, VK_API_VERSION_MAJOR(d.props.apiVersion),
             VK_API_VERSION_MINOR(d.props.apiVersion), VK_API_VERSION_PATCH(d.props.apiVersion));

  // Очередь графики.
  uint32_t famCount = 0;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceQueueFamilyProperties(d.physical, &famCount, 0);
  std::vector<VkQueueFamilyProperties> fams(famCount);
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceQueueFamilyProperties(d.physical, &famCount, &fams[0]);
  bool haveQueue = false;
  for (uint32_t i = 0; i < famCount; ++i)
    if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      d.queueFamily = i;
      haveQueue = true;
      break;
    }
  if (!haveQueue)
  {
    VkNote("graphics queue family", false, 0);
    return false;
  }

  // Surface на окне клиента.
  VkWin32SurfaceCreateInfoKHR sci;
  memset(&sci, 0, sizeof(sci));
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  sci.hinstance = GetModuleHandleA(0);
  sci.hwnd = (HWND)window.p;
  if (!sci.hwnd)
  {
    VkNote("VkWin32SurfaceCreateInfoKHR", false, "окно клиента не передано");
    return false;
  }
  r = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateWin32SurfaceKHR(d.instance, &sci, 0, &d.surface);
  if (r != VK_SUCCESS)
  {
    VkNote("vkCreateWin32SurfaceKHR", false, VkResultName(r));
    return false;
  }

  VkBool32 supported = VK_FALSE;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfaceSupportKHR(d.physical, d.queueFamily,
                                                                    d.surface, &supported);
  if (!supported)
  {
    VkNote("present на очереди графики", false, 0);
    return false;
  }

  // Устройство. Три extensions обязательны: без dynamic_rendering NVRHI
  // ругается, без synchronization2 он не может делать переходы состояний
  // (замеры спаяка, §3.x).
  const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                               VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                               VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME };
  VkPhysicalDeviceVulkan13Features feats13;
  memset(&feats13, 0, sizeof(feats13));
  feats13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  feats13.synchronization2 = VK_TRUE;
  feats13.dynamicRendering = VK_TRUE;

  VkPhysicalDeviceVulkan12Features feats12;
  memset(&feats12, 0, sizeof(feats12));
  feats12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  feats12.pNext = &feats13;
  feats12.timelineSemaphore = VK_TRUE;
  feats12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci;
  memset(&qci, 0, sizeof(qci));
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = d.queueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  VkDeviceCreateInfo dci;
  memset(&dci, 0, sizeof(dci));
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = &feats12;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 3;
  dci.ppEnabledExtensionNames = deviceExts;

  r = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDevice(d.physical, &dci, 0, &d.device);
  if (r != VK_SUCCESS)
  {
    VkNote("vkCreateDevice", false, VkResultName(r));
    return false;
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(d.instance, d.gipa, d.device);
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceQueue(d.device, d.queueFamily, 0, &d.queue);

  nvrhi::vulkan::DeviceDesc dd;
  dd.instance = d.instance;
  dd.physicalDevice = d.physical;
  dd.device = d.device;
  dd.graphicsQueue = d.queue;
  dd.graphicsQueueIndex = (int)d.queueFamily;
  d.nvrhiDevice = nvrhi::vulkan::createDevice(dd);
  if (!d.nvrhiDevice)
  {
    VkNote("nvrhi::vulkan::createDevice", false, 0);
    return false;
  }

  if (!CreateSwapchain(width, height))
    return false;

  d.commandList = d.nvrhiDevice->createCommandList();

  VkNote("Vulkan-бэкенд поднят", true, d.deviceName);
  return true;
}

//////////////////////////////////////////////////////////////////////////
// Swapchain. Порядок и набор параметров — как в проверенном спайке
// (tools/nvrhi_probe/probe.cpp, ProbeWin32Swapchain): без TRANSFER_DST у
// lavapipe present падает на VK_ERROR_MEMORY_MAP_FAILED.

bool NvrhiBackend::CreateSwapchain(unsigned width, unsigned height)
{
  NvrhiBackendImpl& d = *m_pImpl;

  VkSurfaceCapabilitiesKHR caps;
  if (VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d.physical, d.surface,
                                                                             &caps) != VK_SUCCESS)
  {
    VkNote("surface capabilities", false, 0);
    return false;
  }

  uint32_t fmtCount = 0;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfaceFormatsKHR(d.physical, d.surface, &fmtCount, 0);
  std::vector<VkSurfaceFormatKHR> formats(fmtCount);
  if (fmtCount)
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfaceFormatsKHR(d.physical, d.surface, &fmtCount,
                                                                       &formats[0]);
  bool chosen = false;
  for (uint32_t i = 0; i < fmtCount; ++i)
    if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
        formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
      d.swapFormat = formats[i].format;
      d.swapSpace = formats[i].colorSpace;
      chosen = true;
      break;
    }
  if (!chosen && fmtCount)
  {
    d.swapFormat = formats[0].format;
    d.swapSpace = formats[0].colorSpace;
  }
  if (d.swapFormat == VK_FORMAT_UNDEFINED)
  {
    VkNote("формат swapchain'а", false, 0);
    return false;
  }

  uint32_t modeCount = 0;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfacePresentModesKHR(d.physical, d.surface,
                                                                         &modeCount, 0);
  std::vector<VkPresentModeKHR> modes(modeCount);
  if (modeCount)
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfacePresentModesKHR(d.physical, d.surface,
                                                                            &modeCount, &modes[0]);
  d.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // гарантированно есть
  for (uint32_t i = 0; i < modeCount; ++i)
    if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
    {
      d.presentMode = modes[i];
      break;
    }

  if (caps.currentExtent.width != 0xFFFFFFFFu)
  {
    d.width = caps.currentExtent.width;
    d.height = caps.currentExtent.height;
  }
  else
  {
    d.width = width ? width : 128;
    d.height = height ? height : 128;
    if (d.width < caps.minImageExtent.width) d.width = caps.minImageExtent.width;
    if (d.height < caps.minImageExtent.height) d.height = caps.minImageExtent.height;
    if (caps.maxImageExtent.width && d.width > caps.maxImageExtent.width) d.width = caps.maxImageExtent.width;
    if (caps.maxImageExtent.height && d.height > caps.maxImageExtent.height) d.height = caps.maxImageExtent.height;
  }

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
  if (imageCount < 1) imageCount = 1;

  VkSwapchainCreateInfoKHR swci;
  memset(&swci, 0, sizeof(swci));
  swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swci.surface = d.surface;
  swci.minImageCount = imageCount;
  swci.imageFormat = d.swapFormat;
  swci.imageColorSpace = d.swapSpace;
  swci.imageExtent.width = d.width;
  swci.imageExtent.height = d.height;
  swci.imageArrayLayers = 1;
  swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swci.preTransform = caps.currentTransform;
  swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swci.presentMode = d.presentMode;
  swci.clipped = VK_TRUE;

  VkResult r = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateSwapchainKHR(d.device, &swci, 0, &d.swapchain);
  if (r != VK_SUCCESS)
  {
    VkNote("vkCreateSwapchainKHR", false, VkResultName(r));
    return false;
  }

  uint32_t imgCount = 0;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetSwapchainImagesKHR(d.device, d.swapchain, &imgCount, 0);
  std::vector<VkImage> images(imgCount);
  if (imgCount)
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetSwapchainImagesKHR(d.device, d.swapchain, &imgCount, &images[0]);
  if (!imgCount)
  {
    VkNote("vkGetSwapchainImagesKHR", false, 0);
    return false;
  }

  // NVRHI не знает, в каком состоянии находятся картинки чужого swapchain'а,
  // поэтому начальное состояние выставляется вручную (замер спайка).
  nvrhi::Format nf = (d.swapFormat == VK_FORMAT_B8G8R8A8_UNORM) ? nvrhi::Format::BGRA8_UNORM
                                                                : nvrhi::Format::RGBA8_UNORM;
  d.backTextures.clear();
  for (uint32_t i = 0; i < imgCount; ++i)
  {
    nvrhi::TextureHandle h = d.nvrhiDevice->createHandleForNativeTexture(
        nvrhi::ObjectTypes::VK_Image, nvrhi::Object(images[i]),
        nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setFormat(nf)
            .setWidth(d.width).setHeight(d.height)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::RenderTarget)
            .setKeepInitialState(true)
            .setDebugName("PW backbuffer"));
    d.backTextures.push_back(h);
    SurfaceObj so;
    so.tex = h;
    so.backBuffer = true;
    d.surfaces.push_back(so);
  }

  VkSemaphoreCreateInfo semci;
  memset(&semci, 0, sizeof(semci));
  semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateSemaphore(d.device, &semci, 0, &d.acquired) != VK_SUCCESS)
  {
    VkNote("vkCreateSemaphore", false, 0);
    return false;
  }

  DebugTrace("NvrhiBackend: swapchain %ux%u, картинок %u, формат %d, режим present %d",
             d.width, d.height, (unsigned)imgCount, (int)d.swapFormat, (int)d.presentMode);
  return true;
}

void NvrhiBackend::DestroySwapchain()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (d.nvrhiDevice)
    d.nvrhiDevice->waitForIdle();
  // Картинки принадлежат swapchain'у: NVRHI-обёртки и SurfaceObj отпускаем ДО
  // vkDestroySwapchainKHR. Индексы surfaces при пересоздании восстанавливаются
  // те же (то же число картинок, тот же порядок), поэтому выданные GetBackBuffer
  // указатели (index+1) остаются осмысленными.
  d.backTextures.clear();
  d.surfaces.clear();
  d.currentSurface = 0;
  if (d.acquired) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySemaphore(d.device, d.acquired, 0); d.acquired = 0; }
  if (d.swapchain) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySwapchainKHR(d.device, d.swapchain, 0); d.swapchain = 0; }
}

bool NvrhiBackend::RecreateSwapchain()
{
  NvrhiBackendImpl& d = *m_pImpl;
  ++d.swapRecreates;
  // Лог дозируем: при перетаскивании окна пересоздание происходит на каждый кадр.
  if (d.swapRecreates <= 4 || d.swapRecreates % 120 == 0)
    DebugTrace("NvrhiBackend: пересоздание swapchain'а #%u", d.swapRecreates);
  DestroySwapchain();
  // size 0,0: у Win32-surface currentExtent всегда равен размеру окна, так что
  // CreateSwapchain сам возьмёт актуальные размеры.
  if (!CreateSwapchain(0, 0))
  {
    VkNote("пересоздание swapchain'а", false, 0);
    return false;
  }
  return true;
}

void NvrhiBackend::Shutdown()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.device)
  {
    // Порядок важен: nvrhi-объекты должны умереть раньше VkDevice.
    if (d.hVulkan) { FreeLibrary(d.hVulkan); d.hVulkan = 0; }
    return;
  }
  if (d.nvrhiDevice)
    d.nvrhiDevice->waitForIdle();
  d.commandList = nullptr;
  d.backTextures.clear();
  d.surfaces.clear();
  if (d.acquired) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySemaphore(d.device, d.acquired, 0); d.acquired = 0; }
  if (d.swapchain) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySwapchainKHR(d.device, d.swapchain, 0); d.swapchain = 0; }
  d.nvrhiDevice = nullptr;
  if (d.device) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDevice(d.device, 0); d.device = 0; }
  if (d.surface) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySurfaceKHR(d.instance, d.surface, 0); d.surface = 0; }
  if (d.instance) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyInstance(d.instance, 0); d.instance = 0; }
  if (d.hVulkan) { FreeLibrary(d.hVulkan); d.hVulkan = 0; }
}

//////////////////////////////////////////////////////////////////////////
// Диагностика

void NvrhiBackend::DumpStats(const char* pTag) const
{
  const NvrhiBackendImpl& d = *m_pImpl;
  DebugTrace("VkStats%s: presents=%u recreates=%u unsupported=%u swap=%ux%u device=%s",
             pTag ? pTag : "", d.presents, d.swapRecreates, m_unsupported, d.width, d.height, d.deviceName);
  for (int i = 0; i < st_count; ++i)
    if (d.stubCalls[i])
      DebugTrace("VkStub: %s вызван %u раз", kStubNames[i], d.stubCalls[i]);
}

const char* NvrhiBackend::DeviceName() const { return m_pImpl->deviceName; }
unsigned NvrhiBackend::SwapchainWidth() const { return m_pImpl->width; }
unsigned NvrhiBackend::SwapchainHeight() const { return m_pImpl->height; }

HRESULT NvrhiBackend::Unsupported(int which) const
{
  ++m_unsupported;
  if (which >= 0 && which < st_count)
    ++m_pImpl->stubCalls[which];
  return E_NOTIMPL;
}

//////////////////////////////////////////////////////////////////////////
// Сведения об адаптере — заполняются по-настоящему (шаг 4, §2.5).

HRESULT NvrhiBackend::GetSystemInfo(BackendSystemInfo& out)
{
  const NvrhiBackendImpl& d = *m_pImpl;
  memset(&out, 0, sizeof(out));
  out.vendorId = d.props.vendorID;
  out.deviceId = d.props.deviceID;
  // VkPhysicalDeviceProperties::driverVersion — упакованное число (для NVIDIA
  // старшие 22 бита = major). Как есть, без разбора: у D3D9-пути это тоже
  // упакованное значение, матчер config.txt его не использует.
  out.driverVersion = (unsigned long long)d.props.driverVersion;
  out.description = d.deviceName;
  // Vulkan не сообщает «число константных регистров» — их вообще нет. Для
  // клиента контракт остаётся 256 (§2.5 п.3): весь корпус компилируется под
  // этот лимит, и ParticleFX читает именно это поле.
  out.maxVertexShaderConst = SHADER_CONST_REGISTERS;
  out.isNullRef = false;

  // Флаги глубины. На Vulkan-устройстве они почти всегда истинны — в отличие
  // от E2E-стенда на tiny10 (Microsoft Basic Render Driver, caps=0x0), где
  // путь аппаратных теней не исполнялся никогда (§2.15 п.9). Пока этот
  // бэкенд не подменяет D3D9, на картинку не влияет.
  unsigned features = 0;
  const VkFormat depthFormats[3] = { VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT,
                                     VK_FORMAT_D32_SFLOAT_S8_UINT };
  const unsigned depthBits[3] = { hasD16T, hasD24S, hasD24S };
  for (int i = 0; i < 3; ++i)
  {
    VkFormatProperties fp;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFormatProperties(d.physical, depthFormats[i], &fp);
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
      features |= depthBits[i];
  }
  out.features = features;
  return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// Кадр

HRESULT NvrhiBackend::BeginScene() { return Unsupported(st_BeginScene); }
HRESULT NvrhiBackend::EndScene() { return Unsupported(st_EndScene); }

HRESULT NvrhiBackend::Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect)
{
  (void)pSrcRect; (void)pDstRect;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.swapchain || !d.nvrhiDevice)
    return E_FAIL;

  if (window.p)
    d.window = (HWND)window.p;

  // Размер спрашиваем у ОКНА, а не у surface'а: currentExtent в swapchain'е
  // записан тот, что уже создан, а клиент меняет разрешение после CreateDevice
  // (при входе в лобби). Замер 2026-10-15, прогон 2026.10.15-16.46.36: без этой
  // сверки каждый кадр даёт VK_ERROR_OUT_OF_DATE_KHR (1378 строк за прогон,
  // VkStats shows presents=1).
  RECT rc;
  if (d.window && GetClientRect(d.window, &rc))
  {
    const unsigned w = (unsigned)(rc.right - rc.left);
    const unsigned h = (unsigned)(rc.bottom - rc.top);
    if (w && h && (w != d.width || h != d.height))
      return RecreateSwapchain() ? S_OK : E_FAIL;   // кадр пропускаем, следующий — по новому размеру
  }

  // 12a: единственный «проход» — очистка бэк-буфера. Цель — доказать, что
  // клиентский exe управляет Vulkan'ом в клиентском окне; перевод настоящих
  // проходов (BeginScene/Draw/Present через seam) — шаг 13.
  const VkResult ar = VULKAN_HPP_DEFAULT_DISPATCHER.vkAcquireNextImageKHR(
      d.device, d.swapchain, 0xFFFFFFFFFFFFFFFFull, d.acquired, VK_NULL_HANDLE, &d.imageIndex);
  if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR)
  {
    // OUT_OF_DATE/SURFACE_LOST — штатный повод пересоздать swapchain (Wine
    // поднимает его и на смене размера, и на сворачивании окна).
    if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_ERROR_SURFACE_LOST_KHR)
      return RecreateSwapchain() ? S_OK : E_FAIL;
    VkNote("vkAcquireNextImageKHR", false, VkResultName(ar));
    return E_FAIL;
  }

  if (d.imageIndex >= d.backTextures.size())
    return E_FAIL;
  SurfaceObj& so = d.surfaces[d.imageIndex];

  d.commandList->open();
  // Цвет намеренно не тот, что у D3D9-кадра: по нему в скриншоте видно, какой
  // именно вывод лёг последним.
  d.commandList->clearTextureFloat(so.tex, nvrhi::TextureSubresourceSet(),
                                   nvrhi::Color(0.10f, 0.35f, 0.65f, 1.0f));
  d.commandList->close();
  d.nvrhiDevice->executeCommandList(d.commandList);

  d.nvrhiDevice->waitForIdle();

  VkPresentInfoKHR pi;
  memset(&pi, 0, sizeof(pi));
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &d.acquired;
  pi.swapchainCount = 1;
  pi.pSwapchains = &d.swapchain;
  pi.pImageIndices = &d.imageIndex;
  const VkResult pr = VULKAN_HPP_DEFAULT_DISPATCHER.vkQueuePresentKHR(d.queue, &pi);
  if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR)
  {
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_ERROR_SURFACE_LOST_KHR)
      return RecreateSwapchain() ? S_OK : E_FAIL;
    VkNote("vkQueuePresentKHR", false, VkResultName(pr));
    return E_FAIL;
  }
  ++d.presents;
  return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// Область вывода, отсечение, клип-плоскость — ведём тень (её читает клиент).

void NvrhiBackend::GetViewport(BackendViewport& vp) { vp = m_viewport; }

HRESULT NvrhiBackend::SetViewport(const BackendViewport& vp)
{
  m_viewport = vp;
  return Unsupported(st_SetViewport);
}

void NvrhiBackend::SetScissorRect(const BackendRect* pRect)
{
  if (pRect) { m_scissor = *pRect; m_scissorValid = true; }
  else m_scissorValid = false;
}

void NvrhiBackend::GetScissorRect(BackendRect* pOut)
{
  if (!pOut) return;
  if (m_scissorValid) *pOut = m_scissor;
  else { pOut->left = pOut->top = 0; pOut->right = (long)m_viewport.width; pOut->bottom = (long)m_viewport.height; }
}

void NvrhiBackend::SetClipPlane(bool enable, const float* pPlaneEquation)
{
  m_clipPlaneEnabled = enable;
  if (pPlaneEquation) memcpy(m_clipPlane, pPlaneEquation, sizeof(m_clipPlane));
}

HRESULT NvrhiBackend::Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil)
{
  (void)clearFlags; (void)colorRGBA; (void)z; (void)stencil;
  return Unsupported(st_Clear);
}

//////////////////////////////////////////////////////////////////////////
// Константы шейдеров: тень стадии. Реальная выкладка в uniform-буфер — шаг 13
// (раскладка $Globals = порядок объявления, §3.z п.2). GetShaderConstants
// отвечает из тени, поэтому сверка с устройством (шаг 7) на этом бэкенде
// тривиальна — она имеет смысл только для D3D9, где читается настоящее
// состояние устройства.

HRESULT NvrhiBackend::SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData)
{
  if (!pData || !registerCount || startRegister + registerCount > (unsigned)SHADER_CONST_REGISTERS)
    return D3DERR_INVALIDCALL;
  memcpy(m_constShadow[csVertex] + startRegister * 4, pData, (size_t)registerCount * 16);
  return S_OK;
}

HRESULT NvrhiBackend::SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData)
{
  if (!pData || !registerCount || startRegister + registerCount > (unsigned)SHADER_CONST_REGISTERS)
    return D3DERR_INVALIDCALL;
  memcpy(m_constShadow[csPixel] + startRegister * 4, pData, (size_t)registerCount * 16);
  return S_OK;
}

HRESULT NvrhiBackend::GetShaderConstants(EShaderConstStage stage, unsigned startRegister,
                                         unsigned registerCount, void* pData)
{
  if (!pData || !registerCount || stage < 0 || stage >= csConstStageCount ||
      startRegister + registerCount > (unsigned)SHADER_CONST_REGISTERS)
    return D3DERR_INVALIDCALL;
  memcpy(pData, m_constShadow[stage] + startRegister * 4, (size_t)registerCount * 16);
  return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// Биндинги и вывод — ещё не реализованы (шаг 13).

HRESULT NvrhiBackend::SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride)
{ (void)slot; (void)vertexBuffer; (void)byteOffset; (void)stride; return Unsupported(st_SetStreamSource); }
HRESULT NvrhiBackend::SetIndices(HBuffer indexBuffer) { (void)indexBuffer; return Unsupported(st_SetIndices); }
HRESULT NvrhiBackend::SetInputLayout(HInputLayout layout) { (void)layout; return Unsupported(st_SetInputLayout); }
HRESULT NvrhiBackend::SetVertexShader(HShader shader) { (void)shader; return Unsupported(st_SetVertexShader); }
HRESULT NvrhiBackend::SetPixelShader(HShader shader) { (void)shader; return Unsupported(st_SetPixelShader); }
HRESULT NvrhiBackend::SetTexture(unsigned samplerIndex, HTexture texture)
{ (void)samplerIndex; (void)texture; return Unsupported(st_SetTexture); }
HRESULT NvrhiBackend::SetRenderTarget(unsigned index, HSurface surface)
{ (void)index; (void)surface; return Unsupported(st_SetRenderTarget); }
HRESULT NvrhiBackend::SetDepthStencil(HSurface surface) { (void)surface; return Unsupported(st_SetDepthStencil); }
HRESULT NvrhiBackend::GetDepthStencil(HSurface* pOut) { (void)pOut; return Unsupported(st_GetDepthStencil); }

HRESULT NvrhiBackend::DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                                  unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount)
{ (void)type; (void)baseVertexIndex; (void)minIndex; (void)indexedVertexCount; (void)startIndex;
  (void)primitiveCount; return Unsupported(st_DrawIndexed); }

HRESULT NvrhiBackend::Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount)
{ (void)type; (void)startVertex; (void)primitiveCount; return Unsupported(st_Draw); }

HRESULT NvrhiBackend::DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                    unsigned vertexCount, const void* pIndexData, bool index16,
                                    const void* pVertexData, unsigned vertexStride)
{ (void)type; (void)minIndexValue; (void)indexCount; (void)vertexCount; (void)pIndexData; (void)index16;
  (void)pVertexData; (void)vertexStride; return Unsupported(st_DrawIndexedUP); }

HRESULT NvrhiBackend::DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                             const void* pVertexData, unsigned vertexStride)
{ (void)type; (void)primitiveCount; (void)pVertexData; (void)vertexStride; return Unsupported(st_DrawUP); }

HRESULT NvrhiBackend::SetInstanceCount(unsigned count)
{ m_instanceCount = count; return Unsupported(st_SetInstanceCount); }

HRESULT NvrhiBackend::SetInstanceSource(HBuffer perInstanceData, unsigned stride, unsigned offset)
{ (void)perInstanceData; (void)stride; (void)offset; return Unsupported(st_SetInstanceSource); }

bool NvrhiBackend::IsStreamBufferBound(const void* pVertexBuffer) const
{ (void)pVertexBuffer; Unsupported(st_IsStreamBufferBound); return false; }

void NvrhiBackend::InvalidateBoundState()
{
  // Таблицы биндингов ещё нет, поэтому сбрасывать нечего; счётчик инстансинга
  // сбрасывается, чтобы не переживать за него при пересоздании устройства.
  m_instanceCount = 0;
}

//////////////////////////////////////////////////////////////////////////
// Создание ресурсов — шаг 13.

HRESULT NvrhiBackend::CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut)
{ (void)pShaderCode; (void)codeSize; (void)pOut; return Unsupported(st_CreateVertexShader); }
HRESULT NvrhiBackend::CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut)
{ (void)pShaderCode; (void)codeSize; (void)pOut; return Unsupported(st_CreatePixelShader); }
HRESULT NvrhiBackend::CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut)
{ (void)descr; (void)pOut; return Unsupported(st_CreateInputLayout); }

HRESULT NvrhiBackend::CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                      EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                      HTexture* pOut)
{ (void)width; (void)height; (void)mipLevels; (void)usage; (void)format; (void)pool; (void)pOut;
  return Unsupported(st_CreateTexture2D); }

HRESULT NvrhiBackend::CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                        EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                        HTexture* pOut)
{ (void)edgeLen; (void)mipLevels; (void)usage; (void)format; (void)pool; (void)pOut;
  return Unsupported(st_CreateTextureCube); }

HRESULT NvrhiBackend::CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                    EBackendFormat format, EBackendPool pool, HSurface* pOut)
{ (void)kind; (void)width; (void)height; (void)format; (void)pool; (void)pOut;
  return Unsupported(st_CreateSurface); }

HRESULT NvrhiBackend::UpdateTexture(HTexture srcTexture, HTexture dstTexture)
{ (void)srcTexture; (void)dstTexture; return Unsupported(st_UpdateTexture); }

HRESULT NvrhiBackend::CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut)
{ (void)size; (void)pool; (void)pOut; return Unsupported(st_CreateVertexBuffer); }
HRESULT NvrhiBackend::CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut)
{ (void)size; (void)index32; (void)pool; (void)pOut; return Unsupported(st_CreateIndexBuffer); }

HRESULT NvrhiBackend::LockVertexBuffer(HBuffer vb, unsigned offset, unsigned size,
                                       ERenderLockType mode, void** ppData)
{ (void)vb; (void)offset; (void)size; (void)mode; (void)ppData; return Unsupported(st_LockVertexBuffer); }
HRESULT NvrhiBackend::UnlockVertexBuffer(HBuffer vb) { (void)vb; return Unsupported(st_UnlockVertexBuffer); }
HRESULT NvrhiBackend::LockIndexBuffer(HBuffer ib, unsigned offset, unsigned size,
                                      ERenderLockType mode, void** ppData)
{ (void)ib; (void)offset; (void)size; (void)mode; (void)ppData; return Unsupported(st_LockIndexBuffer); }
HRESULT NvrhiBackend::UnlockIndexBuffer(HBuffer ib) { (void)ib; return Unsupported(st_UnlockIndexBuffer); }

//////////////////////////////////////////////////////////////////////////
// Поверхности

HRESULT NvrhiBackend::GetRenderTargetData(HSurface srcSurface, HSurface dstSurface)
{ (void)srcSurface; (void)dstSurface; return Unsupported(st_GetRenderTargetData); }
HRESULT NvrhiBackend::ColorFill(HSurface surface, unsigned colorRGBA)
{ (void)surface; (void)colorRGBA; return Unsupported(st_ColorFill); }
HRESULT NvrhiBackend::StretchRect(HSurface src, const BackendRect* pSrcRect, HSurface dst,
                                  const BackendRect* pDstRect, EBackendFilter filter)
{ (void)src; (void)pSrcRect; (void)dst; (void)pDstRect; (void)filter; return Unsupported(st_StretchRect); }
HRESULT NvrhiBackend::ReadSurface(HSurface srcSurface, const BackendRect* pCropRect, void* pDstBuffer,
                                  unsigned dstRowPitch, EBackendPixelLayout* pOutLayout)
{ (void)srcSurface; (void)pCropRect; (void)pDstBuffer; (void)dstRowPitch; (void)pOutLayout;
  return Unsupported(st_ReadSurface); }

HRESULT NvrhiBackend::GetBackBuffer(HSurface* pOut)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!pOut || d.imageIndex >= d.surfaces.size())
    return E_FAIL;
  pOut->p = (void*)(size_t)(d.imageIndex + 1);
  return S_OK;
}

HRESULT NvrhiBackend::GetRenderTarget(unsigned index, HSurface* pOut)
{ (void)index; (void)pOut; return Unsupported(st_GetRenderTarget); }

void NvrhiBackend::EvictManagedResources()
{
  // Своих managed-ресурсов пока нет; вызов допустим в любой момент.
}

} // namespace Render
