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
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>

#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif
#include <vulkan/vulkan.hpp>

#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <nvrhi/utils.h>
#include "b0_shader_spv.h"

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

// ---------------------------------------------------------------- B0: SPIR-V пайплайн
//
// Спайк B0 (PLAN_NVRHI §3.x): нарисовать ОДИН шейдер, собранный из диалекта
// клиента (tools/spirv_probe: прослойка shim.py + DXC -> SPIR-V), и сверить
// пиксели с эталоном, посчитанным на CPU. Спайк A измерил компилируемость
// (67/70); этот шаг проверяет СЕМАНТИКУ перевода, которую тот не проверял:
// конвенцию mul(vector, matrix) (строковые векторы D3D9), константы из
// register(cN) -> uniform-буфер, ориентацию V при выборке, clip() -> discard,
// выход PS.
//
// Раскладка binding'ов взята из разбора SPIR-V (gen_b0_shader.py):
//   set 0: binding 0 = сэмплер, binding 1 = $Globals (константы), binding 2 = текстура;
//   location'ы вершин: 0 = POSITION, 1 = TEXCOORD0.
// На Vulkan NVRHI нумерует location'ы по порядку описания атрибутов и имена не
// использует (Device::createInputLayout, src/vulkan/vulkan-shader.cpp), т.е.
// порядок массива атрибутов и есть договор с шейдером.

namespace {

constexpr int kB0Size = 64;      // сторона цели
constexpr int kB0TexSize = 8;    // сторона тестовой текстуры

struct B0Vertex { float x, y, z, u, v; };
struct B0Consts { float world[16]; float tint[4]; };

unsigned char g_b0Texel[kB0TexSize * kB0TexSize * 4];

void FillB0Texture()
{
  for (int y = 0; y < kB0TexSize; ++y)
    for (int x = 0; x < kB0TexSize; ++x)
    {
      unsigned char* p = &g_b0Texel[(y * kB0TexSize + x) * 4];
      p[0] = static_cast<unsigned char>(32 + x * 28);   // растёт по x
      p[1] = static_cast<unsigned char>(32 + y * 28);   // растёт по y — ловит переворот V
      p[2] = 128;
      p[3] = (x < 2) ? 32 : 255;                        // a=0.125 -> clip() отбрасывает
    }
}

} // namespace

// Прямой вызов vkCreateGraphicsPipelines (чистый C, без Vulkan-Hpp): NVRHI при
// отказе молчит (CHECK_VK_FAIL без сообщения), а код результата решает всё.
static void RawPipelineTest(VkInstance instance, VkDevice vkd, const void* vsCode,
                            size_t vsSize, const void* psCode, size_t psSize,
                            const char* vsEntry, const char* psEntry)
{
  PFN_vkCreateShaderModule pCreateShaderModule =
      (PFN_vkCreateShaderModule)vkp::GetInstanceProcAddr(instance, "vkCreateShaderModule");
  PFN_vkCreatePipelineLayout pCreatePipelineLayout =
      (PFN_vkCreatePipelineLayout)vkp::GetInstanceProcAddr(instance, "vkCreatePipelineLayout");
  PFN_vkCreateGraphicsPipelines pCreateGraphicsPipelines =
      (PFN_vkCreateGraphicsPipelines)vkp::GetInstanceProcAddr(instance, "vkCreateGraphicsPipelines");
  PFN_vkDestroyShaderModule pDestroyShaderModule =
      (PFN_vkDestroyShaderModule)vkp::GetInstanceProcAddr(instance, "vkDestroyShaderModule");
  PFN_vkDestroyPipeline pDestroyPipeline =
      (PFN_vkDestroyPipeline)vkp::GetInstanceProcAddr(instance, "vkDestroyPipeline");
  PFN_vkDestroyPipelineLayout pDestroyPipelineLayout =
      (PFN_vkDestroyPipelineLayout)vkp::GetInstanceProcAddr(instance, "vkDestroyPipelineLayout");
  if (!pCreateShaderModule || !pCreatePipelineLayout || !pCreateGraphicsPipelines)
  {
    Print("b0: raw: device-level точки не загружены", false);
    return;
  }

  VkResult r;
  VkShaderModuleCreateInfo smci = {};
  smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smci.codeSize = vsSize;
  smci.pCode = (const uint32_t*)vsCode;
  VkShaderModule vsMod = VK_NULL_HANDLE;
  r = pCreateShaderModule(vkd, &smci, nullptr, &vsMod);
  if (r != VK_SUCCESS) { Print("b0: raw vkCreateShaderModule(VS)", false, vkResultToString(r)); return; }

  smci.codeSize = psSize;
  smci.pCode = (const uint32_t*)psCode;
  VkShaderModule psMod = VK_NULL_HANDLE;
  r = pCreateShaderModule(vkd, &smci, nullptr, &psMod);
  if (r != VK_SUCCESS)
  {
    Print("b0: raw vkCreateShaderModule(PS)", false, vkResultToString(r));
    pDestroyShaderModule(vkd, vsMod, nullptr);
    return;
  }

  VkPipelineLayoutCreateInfo plci = {};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  r = pCreatePipelineLayout(vkd, &plci, nullptr, &layout);
  if (r != VK_SUCCESS)
  {
    Print("b0: raw vkCreatePipelineLayout", false, vkResultToString(r));
    pDestroyShaderModule(vkd, vsMod, nullptr);
    pDestroyShaderModule(vkd, psMod, nullptr);
    return;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vsMod;
  stages[0].pName = vsEntry;
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = psMod;
  stages[1].pName = psEntry;

  VkPipelineVertexInputStateCreateInfo vis = {};
  vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ias = {};
  ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vps = {};
  vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vps.viewportCount = 1;
  vps.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs = {};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.lineWidth = 1.f;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  VkPipelineMultisampleStateCreateInfo ms = {};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo dss = {};
  dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState cba = {};
  cba.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cbs = {};
  cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cbs.attachmentCount = 1;
  cbs.pAttachments = &cba;
  VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dyns = {};
  dyns.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dyns.dynamicStateCount = 2;
  dyns.pDynamicStates = dyn;

  VkGraphicsPipelineCreateInfo pci = {};
  pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vis;
  pci.pInputAssemblyState = &ias;
  pci.pViewportState = &vps;
  pci.pRasterizationState = &rs;
  pci.pMultisampleState = &ms;
  pci.pDepthStencilState = &dss;
  pci.pColorBlendState = &cbs;
  pci.pDynamicState = &dyns;
  pci.layout = layout;
  // renderPass = VK_NULL_HANDLE: рендер-пассов в NVRHI нет, всё держится на
  // динамическом рендеринге (ядро Vulkan 1.3).
  pci.renderPass = VK_NULL_HANDLE;

  VkPipeline pipe = VK_NULL_HANDLE;
  r = pCreateGraphicsPipelines(vkd, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
  char rc[64];
  snprintf(rc, sizeof(rc), "VkResult=%d (%s)", (int)r, vkResultToString(r));
  Print("b0: raw vkCreateGraphicsPipelines (renderPass=null)", r == VK_SUCCESS, rc);

  if (pipe != VK_NULL_HANDLE) pDestroyPipeline(vkd, pipe, nullptr);
  pDestroyPipelineLayout(vkd, layout, nullptr);
  pDestroyShaderModule(vkd, vsMod, nullptr);
  pDestroyShaderModule(vkd, psMod, nullptr);
}

// b0bare: тот же RT/тот же framebuffer, но шейдеры без вершинных входов и без
// ресурсов (треугольник из SV_VertexID, сплошной красный). Разделяет «путь
// отрисовки/чтения сломан» и «проблема в вершинных данных/UV».
static void SmokeBareDraw(nvrhi::IDevice* dev, nvrhi::FramebufferHandle fb, nvrhi::TextureHandle rt)
{
  nvrhi::ShaderHandle vs = dev->createShader(
    nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("bare_vs"),
    g_b0BareVsSpv, g_b0BareVsSpv_size);
  nvrhi::ShaderHandle ps = dev->createShader(
    nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("bare_ps"),
    g_b0BarePsSpv, g_b0BarePsSpv_size);
  if (!vs || !ps) { Print("b0bare: createShader", false); return; }

  nvrhi::GraphicsPipelineDesc pd;
  pd.setPrimType(nvrhi::PrimitiveType::TriangleList).setVertexShader(vs).setPixelShader(ps);
  // cullMode=None: с флипом Y (NVRHI отдаёт viewport с отрицательной высотой,
  // VKViewportWithDXCoords) намотка D3D9 становится тыльной; проверяем без
  // отсечения, чтобы отделить «нет растеризации» от «нет лицевой стороны».
  pd.renderState.rasterState.setCullNone();
  nvrhi::GraphicsPipelineHandle pipe = dev->createGraphicsPipeline(pd, fb->getFramebufferInfo());
  if (!pipe) { Print("b0bare: createGraphicsPipeline", false); return; }

  nvrhi::StagingTextureHandle staging = dev->createStagingTexture(
    nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(kB0Size).setHeight(kB0Size).setFormat(nvrhi::Format::RGBA8_UNORM)
      .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
    nvrhi::CpuAccessMode::Read);
  if (!staging) { Print("b0bare: createStagingTexture", false); return; }

  nvrhi::CommandListHandle cl = dev->createCommandList();
  cl->open();
  // пред-заливка ~7/255: по ней видно «до не дорисовано»
  cl->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(),
                        nvrhi::Color(7.f / 255.f, 7.f / 255.f, 7.f / 255.f, 7.f / 255.f));
  nvrhi::GraphicsState gs;
  gs.setPipeline(pipe).setFramebuffer(fb)
    .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
      nvrhi::Viewport(float(kB0Size), float(kB0Size))));
  cl->setGraphicsState(gs);
  cl->draw(nvrhi::DrawArguments().setVertexCount(3));
  cl->copyTexture(staging, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
  cl->close();
  dev->executeCommandList(cl);
  dev->waitForIdle();

  size_t pitch = 0;
  void* px = dev->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch);
  if (!px) { Print("b0bare: mapStagingTexture", false); return; }
  const unsigned char* c = static_cast<const unsigned char*>(px) +
                           (kB0Size / 2) * pitch + (kB0Size / 2) * 4;
  char d[128];
  snprintf(d, sizeof(d), "центр кадра = %u,%u,%u,%u (красный = нарисовано, 7 = кадр не отрисован)",
           c[0], c[1], c[2], c[3]);
  Print("b0bare: треугольник из SV_VertexID", c[0] > 200 && c[1] < 40 && c[2] < 40, d);
  dev->unmapStagingTexture(staging);
}


// Карта кадра блоками 8x8 (средние R,G) — по ней видно, КУДА именно легла
// картинка (сдвиг/переворот/отсечение), а не только «совпало/не совпало».
static void PrintBlockMap(const std::vector<unsigned char>& px, size_t pitch, const char* tag)
{
  printf("b0: %s блоками 8x8 (средние R,G):\n", tag);
  for (int by = 0; by < 8; ++by)
  {
    printf("       ");
    for (int bx = 0; bx < 8; ++bx)
    {
      unsigned int sr = 0, sg = 0;
      for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
        {
          const unsigned char* q = &px[size_t(by * 8 + yy) * pitch + size_t(bx * 8 + xx) * 4];
          sr += q[0]; sg += q[1];
        }
      printf("%4u,%4u", sr / 64, sg / 64);
    }
    printf("\n");
  }
}

// Отрисовка 6 вершин в rt и чтение кадра в RAM (pitch = kB0Size*4).
static bool RenderAndReadback(nvrhi::IDevice* dev, nvrhi::FramebufferHandle fb, nvrhi::TextureHandle rt,
                              nvrhi::GraphicsPipelineHandle pipe, nvrhi::BufferHandle vbuf,
                              nvrhi::BindingSetHandle bset, float clearLevel,
                              std::vector<unsigned char>& out, size_t& outPitch)
{
  nvrhi::StagingTextureHandle staging = dev->createStagingTexture(
    nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(kB0Size).setHeight(kB0Size).setFormat(nvrhi::Format::RGBA8_UNORM)
      .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
    nvrhi::CpuAccessMode::Read);
  if (!staging) return false;

  nvrhi::CommandListHandle cl = dev->createCommandList();
  cl->open();
  cl->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(),
                        nvrhi::Color(clearLevel, clearLevel, clearLevel, clearLevel));
  nvrhi::GraphicsState gs;
  gs.setPipeline(pipe).setFramebuffer(fb)
    .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vbuf).setSlot(0).setOffset(0));
  if (bset) gs.addBindingSet(bset);
  gs.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
    nvrhi::Viewport(float(kB0Size), float(kB0Size))));
  cl->setGraphicsState(gs);
  cl->draw(nvrhi::DrawArguments().setVertexCount(6));
  cl->copyTexture(staging, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
  cl->close();
  dev->executeCommandList(cl);
  dev->waitForIdle();

  size_t pitch = 0;
  void* px = dev->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch);
  if (!px) return false;
  out.assign(size_t(kB0Size) * kB0Size * 4, 0);
  for (int y = 0; y < kB0Size; ++y)
    memcpy(&out[size_t(y) * kB0Size * 4], static_cast<const unsigned char*>(px) + size_t(y) * pitch,
           size_t(kB0Size) * 4);
  outPitch = size_t(kB0Size) * 4;
  dev->unmapStagingTexture(staging);
  return true;
}

// Эталон для WorldXf = diag(0.5,0.5,1) со сдвигом (+0.5,+0.5) в ЧЕТВЁРТОЙ СТРОКЕ
// (строковый вектор, конвенция D3D9/HLSL mul(v, M)). Квадрат тогда занимает
// NDC [0,1]^2 -> экран x∈[32,64), y∈[0,32). При обратной (столбцовой) трактовке
// сдвиг пропал бы и квадрат встал бы по центру — кейс его различает.
static void WantScaled(int px, int py, int out[4])
{
  if (px < 32 || py >= 32) { out[0] = out[1] = out[2] = out[3] = 7; return; }
  const int tx = int(((px + 0.5f) / 32.f - 1.f) * 8.f);
  const int ty = int(((py + 0.5f) / 32.f) * 8.f);
  if (tx < 2) { out[0] = out[1] = out[2] = out[3] = 7; return; }   // clip()
  out[0] = 32 + tx * 28; out[1] = 32 + ty * 28; out[2] = 128; out[3] = 255;
}

static bool SmokeShaderPipeline(nvrhi::IDevice* dev, VkInstance instance, VkDevice vkd)
{
  FillB0Texture();

  nvrhi::TextureHandle rt = dev->createTexture(nvrhi::TextureDesc()
                                                 .setDimension(nvrhi::TextureDimension::Texture2D)
                                                 .setWidth(kB0Size).setHeight(kB0Size)
                                                 .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                 .setIsRenderTarget(true)
                                                 .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                                 .setKeepInitialState(true));
  if (!rt) { Print("b0: createTexture(RT)", false); return false; }

  nvrhi::FramebufferHandle fb = dev->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(rt));
  if (!fb) { Print("b0: createFramebuffer", false); return false; }

  nvrhi::ShaderHandle vs = dev->createShader(
    nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("MainPass_VSMain"),
    g_b0VsSpv, g_b0VsSpv_size);
  nvrhi::ShaderHandle ps = dev->createShader(
    nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("MainPass_PSMain"),
    g_b0PsSpv, g_b0PsSpv_size);
  if (!vs || !ps) { Print("b0: createShader(SPIR-V)", false); return false; }
  Print("b0: createShader(SPIR-V)", true, "VS+PS из b0_probe.hlsl (прослойка + DXC)");

  nvrhi::VertexAttributeDesc attrs[2] = {
    nvrhi::VertexAttributeDesc().setName("POSITION")
      .setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(0)
      .setElementStride(static_cast<uint32_t>(sizeof(B0Vertex))),
    nvrhi::VertexAttributeDesc().setName("TEXCOORD0")
      .setFormat(nvrhi::Format::RG32_FLOAT).setOffset(12)
      .setElementStride(static_cast<uint32_t>(sizeof(B0Vertex))),
  };
  nvrhi::InputLayoutHandle inputLayout = dev->createInputLayout(attrs, 2, vs);
  if (!inputLayout) { Print("b0: createInputLayout", false); return false; }

  // Квадрат двумя треугольниками. UV привязан к пиксельным координатам цели:
  // пиксель (px,py) -> UV ((px+0.5)/64, (py+0.5)/64), т.е. эталон на CPU —
  // просто texel = (px/8, py/8) при POINT-фильтре и Clamp.
  const B0Vertex verts[6] = {
    { -1.f, -1.f, 0.f, 0.f, 1.f },
    {  1.f, -1.f, 0.f, 1.f, 1.f },
    { -1.f,  1.f, 0.f, 0.f, 0.f },
    {  1.f, -1.f, 0.f, 1.f, 1.f },
    {  1.f,  1.f, 0.f, 1.f, 0.f },
    { -1.f,  1.f, 0.f, 0.f, 0.f },
  };
  nvrhi::BufferHandle vbuf = dev->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(sizeof(verts))
                                                 .setIsVertexBuffer(true)
                                                 .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                 .setKeepInitialState(true));
  if (!vbuf) { Print("b0: createBuffer(VB)", false); return false; }

  B0Consts consts;
  static const float kIdentity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
  memcpy(consts.world, kIdentity, sizeof(kIdentity));
  consts.tint[0] = consts.tint[1] = consts.tint[2] = consts.tint[3] = 1.f;

  nvrhi::BufferHandle cbuf = dev->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(sizeof(consts))
                                                 .setIsConstantBuffer(true)
                                                 .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                                 .setKeepInitialState(true));
  if (!cbuf) { Print("b0: createBuffer(CB)", false); return false; }

  nvrhi::TextureHandle tex = dev->createTexture(nvrhi::TextureDesc()
                                                  .setDimension(nvrhi::TextureDimension::Texture2D)
                                                  .setWidth(kB0TexSize).setHeight(kB0TexSize)
                                                  .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                  .setMipLevels(1)
                                                  .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                  .setKeepInitialState(true));
  if (!tex) { Print("b0: createTexture", false); return false; }

  // Загрузка — отдельным командным списком ДО проверок и до основного рисуем:
  // в этой версии NVRHI writeBuffer/writeTexture — методы ICommandList.
  {
    nvrhi::CommandListHandle up = dev->createCommandList();
    up->open();
    up->writeBuffer(vbuf, verts, sizeof(verts));
    up->writeBuffer(cbuf, &consts, sizeof(consts));
    up->writeTexture(tex, 0, 0, g_b0Texel, kB0TexSize * 4);
    up->close();
    dev->executeCommandList(up);
    dev->waitForIdle();
  }

  nvrhi::SamplerHandle sampler = dev->createSampler(nvrhi::SamplerDesc()
                                                      .setAllFilters(false)   // POINT
                                                      .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
  if (!sampler) { Print("b0: createSampler", false); return false; }

  nvrhi::BindingSetDesc bsDesc;
  bsDesc.bindings.push_back(nvrhi::BindingSetItem::Sampler(0, sampler));
  bsDesc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(1, cbuf));
  bsDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(2, tex));
  // Раскладка создаётся вручную, а не через utils::CreateBindingSetAndLayout:
  // у NVRHI по умолчанию VulkanBindingOffsets = {shaderResource 0, sampler 128,
  // constantBuffer 256, UAV 384} — т.е. слоты s* уходят в binding 128+, b* в
  // 256+. DXC в нашей сборке раздаёт binding'и сам (0,1,2), поэтому смещения
  // обнулены. Ссовпадения слотов с binding'ами шейдера без этого нет —
  // выборка возвращает нули (проверено: с дефолтными смещениями кадр чёрный).
  nvrhi::BindingLayoutDesc ldesc;
  ldesc.visibility = nvrhi::ShaderType::AllGraphics;
  ldesc.registerSpace = 0;
  ldesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
    .setShaderResourceOffset(0).setSamplerOffset(0)
    .setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
  ldesc.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(0));
  ldesc.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(1));
  ldesc.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(2));
  nvrhi::BindingLayoutHandle bindingLayout = dev->createBindingLayout(ldesc);
  nvrhi::BindingSetHandle bindingSet = bindingLayout ? dev->createBindingSet(bsDesc, bindingLayout)
                                                     : nvrhi::BindingSetHandle();
  if (!bindingLayout || !bindingSet)
  {
    Print("b0: createBindingLayout/createBindingSet", false);
    return false;
  }
  Print("b0: binding layout (смещения 0)", true);

  nvrhi::GraphicsPipelineDesc pd;
  pd.setPrimType(nvrhi::PrimitiveType::TriangleList)
    .setInputLayout(inputLayout)
    .setVertexShader(vs)
    .setPixelShader(ps)
    .addBindingLayout(bindingLayout);
  pd.renderState.rasterState.setCullNone();
  nvrhi::GraphicsPipelineHandle pipe = dev->createGraphicsPipeline(pd, fb->getFramebufferInfo());
  if (!pipe)
  {
    // NVRHI молчит (CHECK_VK_FAIL без сообщения), поэтому сужаем причину
    // перебором: без binding-раскладки / без input layout.
    nvrhi::GraphicsPipelineDesc pd2;
    pd2.setPrimType(nvrhi::PrimitiveType::TriangleList).setVertexShader(vs).setPixelShader(ps);
    Print("b0: variant без binding-раскладок", bool(dev->createGraphicsPipeline(pd2, fb->getFramebufferInfo())));

    nvrhi::GraphicsPipelineDesc pd3;
    pd3.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(inputLayout)
       .setVertexShader(vs).setPixelShader(ps);
    Print("b0: variant без binding-раскладок, с input layout",
          bool(dev->createGraphicsPipeline(pd3, fb->getFramebufferInfo())));

    nvrhi::GraphicsPipelineDesc pd4;
    pd4.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(inputLayout)
       .setVertexShader(vs).setPixelShader(ps).addBindingLayout(bindingLayout);
    Print("b0: variant с input layout + раскладкой (повтор)",
          bool(dev->createGraphicsPipeline(pd4, fb->getFramebufferInfo())));

    // Лестница бисекции: голые шейдеры (ни ресурсов, ни вершинных входов).
    nvrhi::ShaderHandle bvs = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("bare_vs"),
      g_b0BareVsSpv, g_b0BareVsSpv_size);
    nvrhi::ShaderHandle bps = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("bare_ps"),
      g_b0BarePsSpv, g_b0BarePsSpv_size);
    Print("b0: bare createShader", bool(bvs && bps));
    RawPipelineTest(instance, vkd, g_b0BareVsSpv, g_b0BareVsSpv_size,
                    g_b0BarePsSpv, g_b0BarePsSpv_size, "bare_vs", "bare_ps");
    nvrhi::GraphicsPipelineDesc pdb;
    pdb.setPrimType(nvrhi::PrimitiveType::TriangleList).setVertexShader(bvs).setPixelShader(bps);
    Print("b0: bare пайплайн (без ресурсов и input layout)",
          bool(dev->createGraphicsPipeline(pdb, fb->getFramebufferInfo())));

    // Чистый SM6 (без прослойки), те же ресурсы и та же раскладка: отделяет
    // «перевод диалекта» от «нашего вызова NVRHI».
    nvrhi::ShaderHandle tvs = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("trivial_vs"),
      g_b0TVsSpv, g_b0TVsSpv_size);
    nvrhi::ShaderHandle tps = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("trivial_ps"),
      g_b0TPsSpv, g_b0TPsSpv_size);
    nvrhi::GraphicsPipelineDesc pd5;
    pd5.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(inputLayout)
       .setVertexShader(tvs).setPixelShader(tps).addBindingLayout(bindingLayout);
    Print("b0: variant чистый SM6 (без прослойки)",
          bool(dev->createGraphicsPipeline(pd5, fb->getFramebufferInfo())));
    Print("b0: createGraphicsPipeline", false);
    return false;
  }
  Print("b0: createGraphicsPipeline", true);
  SmokeBareDraw(dev, fb, rt);

  // (0) вершинные данные без ресурсов: PS возвращает (0,1,uv.x,1). По каналу B
  // видно, дошло ли UV до пиксельного шейдера (центр ~128 = 0.5).
  {
    nvrhi::ShaderHandle vvs = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("vb_vs"),
      g_b0VbVsSpv, g_b0VbVsSpv_size);
    nvrhi::ShaderHandle vps = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("vb_ps"),
      g_b0VbPsSpv, g_b0VbPsSpv_size);
    nvrhi::GraphicsPipelineDesc pdv;
    pdv.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(inputLayout)
       .setVertexShader(vvs).setPixelShader(vps);
    pdv.renderState.rasterState.setCullNone();
    nvrhi::GraphicsPipelineHandle pv = dev->createGraphicsPipeline(pdv, fb->getFramebufferInfo());
    if (!pv) { Print("b0vb: createGraphicsPipeline", false); }
    else
    {
      nvrhi::StagingTextureHandle st = dev->createStagingTexture(
        nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
          .setWidth(kB0Size).setHeight(kB0Size).setFormat(nvrhi::Format::RGBA8_UNORM)
          .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
        nvrhi::CpuAccessMode::Read);
      nvrhi::CommandListHandle c4 = dev->createCommandList();
      c4->open();
      c4->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(),
                            nvrhi::Color(7.f / 255.f, 7.f / 255.f, 7.f / 255.f, 7.f / 255.f));
      nvrhi::GraphicsState gs;
      gs.setPipeline(pv).setFramebuffer(fb)
        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vbuf).setSlot(0).setOffset(0))
        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
          nvrhi::Viewport(float(kB0Size), float(kB0Size))));
      c4->setGraphicsState(gs);
      c4->draw(nvrhi::DrawArguments().setVertexCount(6));
      c4->copyTexture(st, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
      c4->close();
      dev->executeCommandList(c4);
      dev->waitForIdle();
      size_t p4 = 0;
      void* px4 = dev->mapStagingTexture(st, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &p4);
      if (px4)
      {
        const unsigned char* c = static_cast<const unsigned char*>(px4) +
                                 (kB0Size / 2) * p4 + (kB0Size / 2) * 4;
        char d[128];
        snprintf(d, sizeof(d), "центр = %u,%u,%u,%u (ожидалось ~0,255,128,255)", c[0], c[1], c[2], c[3]);
        Print("b0vb: вершинные данные без ресурсов", c[1] > 200, d);
        dev->unmapStagingTexture(st);
      }
    }
  }

  // (1) проверка загрузки текстуры: копирование обратно и чтение текселей.
  {
    nvrhi::StagingTextureHandle st = dev->createStagingTexture(
      nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(kB0TexSize).setHeight(kB0TexSize).setFormat(nvrhi::Format::RGBA8_UNORM)
        .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
      nvrhi::CpuAccessMode::Read);
    if (st)
    {
      nvrhi::CommandListHandle c2 = dev->createCommandList();
      c2->open();
      c2->copyTexture(st, nvrhi::TextureSlice(), tex, nvrhi::TextureSlice());
      c2->close();
      dev->executeCommandList(c2);
      dev->waitForIdle();
      size_t p2 = 0;
      void* px2 = dev->mapStagingTexture(st, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &p2);
      if (px2)
      {
        const unsigned char* a = static_cast<const unsigned char*>(px2);
        const unsigned char* b = a + (kB0TexSize - 1) * p2 + (kB0TexSize - 1) * 4;
        char d[128];
        snprintf(d, sizeof(d), "texel(0,0)=%u,%u,%u,%u texel(7,7)=%u,%u,%u,%u (ожидалось 32,32,128,32 и 228,228,128,255)",
                 a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
        Print("b0tex: writeTexture -> readback", a[0] == 32 && b[0] == 228, d);
        dev->unmapStagingTexture(st);
      }
    }
  }

  // (2) тот же binding set, но тривиальный SM6-шейдер (без прослойки):
  // разделяет «проблема в биндингах» и «проблема в переведённом шейдере».
  {
    nvrhi::ShaderHandle tvs = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("trivial_vs"),
      g_b0TVsSpv, g_b0TVsSpv_size);
    nvrhi::ShaderHandle tps = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("trivial_ps"),
      g_b0TPsSpv, g_b0TPsSpv_size);
    nvrhi::GraphicsPipelineDesc pdt;
    pdt.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(inputLayout)
       .setVertexShader(tvs).setPixelShader(tps).addBindingLayout(bindingLayout);
    pdt.renderState.rasterState.setCullNone();
    nvrhi::GraphicsPipelineHandle pt = dev->createGraphicsPipeline(pdt, fb->getFramebufferInfo());
    if (!pt) { Print("b0triv: createGraphicsPipeline", false); }
    else
    {
      nvrhi::StagingTextureHandle st = dev->createStagingTexture(
        nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
          .setWidth(kB0Size).setHeight(kB0Size).setFormat(nvrhi::Format::RGBA8_UNORM)
          .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
        nvrhi::CpuAccessMode::Read);
      nvrhi::CommandListHandle c3 = dev->createCommandList();
      c3->open();
      c3->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(),
                            nvrhi::Color(7.f / 255.f, 7.f / 255.f, 7.f / 255.f, 7.f / 255.f));
      nvrhi::GraphicsState gs;
      gs.setPipeline(pt).setFramebuffer(fb)
        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vbuf).setSlot(0).setOffset(0))
        .addBindingSet(bindingSet)
        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
          nvrhi::Viewport(float(kB0Size), float(kB0Size))));
      c3->setGraphicsState(gs);
      c3->draw(nvrhi::DrawArguments().setVertexCount(6));
      c3->copyTexture(st, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
      c3->close();
      dev->executeCommandList(c3);
      dev->waitForIdle();
      size_t p3 = 0;
      void* px3 = dev->mapStagingTexture(st, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &p3);
      if (px3)
      {
        const unsigned char* c = static_cast<const unsigned char*>(px3) +
                                 (kB0Size / 2) * p3 + (kB0Size / 2) * 4;
        char d[128];
        snprintf(d, sizeof(d), "центр = %u,%u,%u,%u (7 = не нарисовано; ~116..144 = выборка текстуры)",
                 c[0], c[1], c[2], c[3]);
        Print("b0triv: тривиальный SM6 с теми же биндингами", c[0] > 40, d);
        dev->unmapStagingTexture(st);
      }
    }
  }

  nvrhi::StagingTextureHandle staging = dev->createStagingTexture(
    nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(kB0Size).setHeight(kB0Size).setFormat(nvrhi::Format::RGBA8_UNORM)
      .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
    nvrhi::CpuAccessMode::Read);
  if (!staging) { Print("b0: createStagingTexture", false); return false; }

  nvrhi::CommandListHandle cl = dev->createCommandList();
  cl->open();
  cl->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(), nvrhi::Color(0.f, 0.f, 0.f, 0.f));
  nvrhi::GraphicsState gs;
  gs.setPipeline(pipe)
    .setFramebuffer(fb)
    .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vbuf).setSlot(0).setOffset(0))
    .addBindingSet(bindingSet)
    .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
      nvrhi::Viewport(float(kB0Size), float(kB0Size))));
  cl->setGraphicsState(gs);
  cl->draw(nvrhi::DrawArguments().setVertexCount(6));
  cl->copyTexture(staging, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
  cl->close();
  dev->executeCommandList(cl);
  dev->waitForIdle();

  size_t rowPitch = 0;
  void* pixels = dev->mapStagingTexture(staging, nvrhi::TextureSlice(),
                                        nvrhi::CpuAccessMode::Read, &rowPitch);
  if (!pixels) { Print("b0: mapStagingTexture", false); return false; }

  int bad = 0, firstX = -1, firstY = -1;
  int got[4] = {}, want[4] = {};
  for (int py = 0; py < kB0Size; ++py)
    for (int px = 0; px < kB0Size; ++px)
    {
      const int tx = px / (kB0Size / kB0TexSize);
      const int ty = py / (kB0Size / kB0TexSize);
      int w[4];
      if (tx < 2) { w[0] = w[1] = w[2] = w[3] = 0; }   // clip() -> discard
      else { w[0] = 32 + tx * 28; w[1] = 32 + ty * 28; w[2] = 128; w[3] = 255; }

      const unsigned char* g = static_cast<const unsigned char*>(pixels) + py * rowPitch + px * 4;
      bool diff = false;
      for (int c = 0; c < 4; ++c) { int d = int(g[c]) - w[c]; if (d < -2 || d > 2) diff = true; }
      if (diff)
      {
        ++bad;
        if (firstX < 0)
        {
          firstX = px; firstY = py;
          for (int c = 0; c < 4; ++c) { got[c] = g[c]; want[c] = w[c]; }
        }
      }
    }
  dev->unmapStagingTexture(staging);

  // Карта кадра блоками 8x8 (средние R,G) — по ней видно, СДВИНУТА ли картинка,
  // перевёрнута ли, или вся отброшена.
  printf("b0: кадр блоками 8x8 (средние R,G; блок = texel эталона):\n");
  for (int by = 0; by < 8; ++by)
  {
    printf("       ");
    for (int bx = 0; bx < 8; ++bx)
    {
      unsigned int sr = 0, sg = 0;
      for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
        {
          const unsigned char* q = static_cast<const unsigned char*>(pixels) +
                                   (by * 8 + yy) * rowPitch + (bx * 8 + xx) * 4;
          sr += q[0]; sg += q[1];
        }
      printf("%4u,%4u", sr / 64, sg / 64);
    }
    printf("\n");
  }

  char detail[256];
  if (bad == 0)
    snprintf(detail, sizeof(detail), "все %d пикселей совпали с CPU-эталоном", kB0Size * kB0Size);
  else
    snprintf(detail, sizeof(detail), "не совпало %d/%d; первый (%d,%d): получено %d,%d,%d,%d, ожидалось %d,%d,%d,%d",
             bad, kB0Size * kB0Size, firstX, firstY, got[0], got[1], got[2], got[3],
             want[0], want[1], want[2], want[3]);
  Print("b0: SPIR-V пайплайн vs CPU-эталон", bad == 0, detail);
  const bool identityOk = (bad == 0);

  // --- b0xfm: тот же шейдер, но WorldXf = масштаб 0.5 + перенос из 4-й строки.
  bool xfmOk = false;
  {
    B0Consts c2;
    static const float kHalfShift[16] = { 0.5f,0,0,0,  0,0.5f,0,0,  0,0,1,0,  0.5f,0.5f,0,1 };
    memcpy(c2.world, kHalfShift, sizeof(kHalfShift));
    c2.tint[0] = c2.tint[1] = c2.tint[2] = c2.tint[3] = 1.f;
    nvrhi::CommandListHandle up = dev->createCommandList();
    up->open();
    up->writeBuffer(cbuf, &c2, sizeof(c2));
    up->close();
    dev->executeCommandList(up);
    dev->waitForIdle();

    std::vector<unsigned char> px;
    size_t pitch = 0;
    if (!RenderAndReadback(dev, fb, rt, pipe, vbuf, bindingSet, 7.f / 255.f, px, pitch))
      Print("b0xfm: render/readback", false);
    else
    {
      int bad2 = 0, fx = -1, fy = -1, got[4] = {}, want[4] = {};
      for (int py = 0; py < kB0Size; ++py)
        for (int pxx = 0; pxx < kB0Size; ++pxx)
        {
          WantScaled(pxx, py, want);
          const unsigned char* g = &px[size_t(py) * pitch + size_t(pxx) * 4];
          bool diff = false;
          for (int c = 0; c < 4; ++c) { int d = int(g[c]) - want[c]; if (d < -2 || d > 2) diff = true; }
          if (diff)
          {
            ++bad2;
            if (fx < 0) { fx = pxx; fy = py; for (int c = 0; c < 4; ++c) got[c] = g[c]; }
          }
        }
      char d2[200];
      if (bad2 == 0) snprintf(d2, sizeof(d2), "все %d пикселей совпали", kB0Size * kB0Size);
      else snprintf(d2, sizeof(d2), "не совпало %d/%d; первый (%d,%d): %d,%d,%d,%d, ожидалось %d,%d,%d,%d",
                    bad2, kB0Size * kB0Size, fx, fy, got[0], got[1], got[2], got[3],
                    want[0], want[1], want[2], want[3]);
      xfmOk = (bad2 == 0);
      Print("b0xfm: mul(v, M) со сдвигом в 4-й строке", xfmOk, d2);
      if (!xfmOk) PrintBlockMap(px, pitch, "b0xfm кадр");
    }

    // вернуть единичную матрицу — дальше сравнения считаются от неё
    B0Consts c1;
    static const float kIdent[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    memcpy(c1.world, kIdent, sizeof(kIdent));
    c1.tint[0] = c1.tint[1] = c1.tint[2] = c1.tint[3] = 1.f;
    nvrhi::CommandListHandle up2 = dev->createCommandList();
    up2->open();
    up2->writeBuffer(cbuf, &c1, sizeof(c1));
    up2->close();
    dev->executeCommandList(up2);
    dev->waitForIdle();
  }

  // --- b0cull: намотка. NVRHI по умолчанию cullMode=Back,
  // frontCounterClockwise=false. Квадрат задан в намотке D3D9; смотрим, при
  // каком значении он не отсекается (иначе Stage 3 придётся переворачивать
  // намотку геометрии или индексации).
  int drawn[2] = { -1, -1 };
  for (int variant = 0; variant < 2; ++variant)
  {
    nvrhi::GraphicsPipelineDesc pdc;
    pdc.setPrimType(nvrhi::PrimitiveType::TriangleList)
      .setInputLayout(inputLayout).setVertexShader(vs).setPixelShader(ps)
      .addBindingLayout(bindingLayout);
    pdc.renderState.rasterState.setFrontCounterClockwise(variant == 1);
    nvrhi::GraphicsPipelineHandle pc = dev->createGraphicsPipeline(pdc, fb->getFramebufferInfo());
    if (!pc) continue;
    std::vector<unsigned char> px;
    size_t pitch = 0;
    if (!RenderAndReadback(dev, fb, rt, pc, vbuf, bindingSet, 0.f, px, pitch)) continue;
    int n = 0;
    for (size_t i = 0; i < px.size(); i += 4)
      if (px[i] != 0 || px[i + 1] != 0 || px[i + 2] != 0) ++n;
    drawn[variant] = n;
  }
  // Замер (не шивот): NVRHI на Vulkan отдаёт viewport с отрицательной высотой
  // (VKViewportWithDXCoords), т.е. Y развёрнут как в D3D9, и из-за этого
  // треугольники с намоткой D3D9 оказываются тыльными. Вывод для этапа 3:
  // во всех пайплайнах нужен frontCounterClockwise=true (или иная смена
  // намотки), иначе при cullMode=Back не рисуется ничего.
  const bool cullOk = (drawn[0] == 0 && drawn[1] > 2000);
  char d4[160];
  snprintf(d4, sizeof(d4), "frontCounterClockwise=false -> %d пикселей, true -> %d (из %d)",
           drawn[0], drawn[1], kB0Size * kB0Size);
  Print("b0cull: D3D9-намотка требует frontCounterClockwise=true", cullOk, d4);

  Print("b0: итог спайка B0", identityOk && xfmOk && cullOk,
        "семантика перевода HLSL->SPIR-V сверена с CPU-эталоном");
  return identityOk && xfmOk && cullOk;
}

// ------------------------------------ .shd v2: наш SPIR-V внутри Vulkan (шаг 12)
//
// Спайк отвечает на два вопроса, которые нельзя снять из Python-инструмента
// (tools/shader_budget считает бюджет по SPIR-V, но не говорит, принимает ли его
// драйвер):
//   1. создаются ли из блобов нашего писателя (tools/shader_build/mkshd2.py:
//      DXC + прослойка SM3->SM6 + PW_MODERN_ARRAYS=1) шейдеры и пайплайны
//      NVRHI/Vulkan и что именно сообщает NVRHI при отказе (messageCallback
//      подключён в ProbeVulkan);
//   2. в какие наборы (descriptor set) и binding'ы реально кладёт ресурсы DXC —
//      макет §3.z п.1 обязан совпадать с фактом, иначе пайплайн не слинкуется
//      (на спайке B0 с дефолтными VulkanBindingOffsets кадр был чёрный).
// Рендерить эти пайплайны пока нечем (нет ни блока констант, ни текстур),
// поэтому проверка заканчивается созданием пайплайна.
// Запуск: PW_SHD2=<путь к .shd> [PW_SHD2_MAX=N, 0 = все записи].

static unsigned Ld32(const unsigned char* p)
{
  return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

struct Shd2Sec { unsigned tag, off, cnt, end; };

struct Shd2Spike
{
  std::vector<unsigned char> data;
  unsigned v1Size = 0, v2hdr = 0;
  std::vector<Shd2Sec> secs;

  bool open(const char* path)
  {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = n > 32 && (long)data.size() + n >= 0;
    data.resize((size_t)n);
    ok = ok && fread(data.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) return false;
    const unsigned char* t = data.data() + (size_t)n - 16;
    if (memcmp(t, "SHD2", 4) || memcmp(t + 12, "SHD2", 4)) return false;   // чистый v1
    v2hdr = Ld32(t + 4);
    const unsigned char* h = data.data() + v2hdr;
    if (v2hdr + 16 > (unsigned)n || memcmp(h, "V2H\0", 4) || Ld32(h + 4) != 1) return false;
    v1Size = Ld32(h + 8);
    const unsigned tc = Ld32(h + 12);
    for (unsigned i = 0; i < tc; ++i)
    {
      Shd2Sec s;
      s.tag = Ld32(h + 16 + 12 * i);
      s.off = Ld32(h + 20 + 12 * i);
      s.cnt = Ld32(h + 24 + 12 * i);
      s.end = (i + 1 < tc) ? Ld32(h + 20 + 12 * (i + 1)) : v2hdr;
      if (s.off + 8 + 20 * s.cnt > s.end) return false;
      secs.push_back(s);
    }
    return !secs.empty();
  }

  const Shd2Sec* find(unsigned tag) const
  {
    for (size_t i = 0; i < secs.size(); ++i)
      if (secs[i].tag == tag) return &secs[i];
    return nullptr;
  }
};

// Разбор SPIR-V: какие ресурсы у модуля и в каких сетах/binding'ах они лежат.
// Декорации: OpDecorate = 71, Binding = 33, DescriptorSet = 34.
// Класс ресурса берётся из типа переменной: OpVariable = 59 (слово 1 = тип,
// слово 2 = storage class), OpTypePointer = 32 (слово 3 = базовый тип),
// OpTypeImage = 26, OpTypeSampler = 27, OpTypeSampledImage = 28, OpTypeStruct = 31.
struct SpvRes
{
  enum Kind { kSampler, kTexture, kCbuf, kSsbo, kOther };
  struct Item { unsigned set, binding; Kind kind; unsigned storage, baseOp; };
  std::vector<Item> items;
  unsigned maxSet = 0, maxBinding = 0;
  // BuiltIn-декорации (OpDecorate, decoration=11). Для VS важно наличие
  // Position (BuiltIn 0): 28 материалов из 49 отдают клиповую позицию
  // out-параметром с семантикой POSITION, и без подмены прослойки DXC
  // пишет gl_Position в неопределённость (см. tools/spirv_probe/shim.py).
  unsigned builtIns = 0, biPosition = 0, biFragCoord = 0, biInstanceIndex = 0;
  // Что реально читает шейдер из uniform-буфера: (binding, смещение члена в
  // байтах, является ли член матрицей). Снято с OpAccessChain с постоянным
  // индексом + OpMemberDecorate Offset(35)/MatrixStride(7). Нужно затем, что
  // имена членов DXC из SPIR-V выбрасывает (OpMemberName нет), а у $Globals
  // смещения задаёт register() в _common.hlsl — угадать их по номеру члена
  // нельзя (замер WaterDummy: читаются члены 10 и 11, а не 0 и 1).
  struct Acc { unsigned binding, offset; bool matrix; };
  std::vector<Acc> accesses;
  bool ok = false;
};

static SpvRes ScanSpv(const unsigned char* p, size_t bytes)
{
  SpvRes r;
  if (bytes < 20 || Ld32(p) != 0x07230203u) return r;
  const size_t words = bytes / 4;

  std::vector<unsigned> ptrBase;    // id типа указателя -> id базового типа
  std::vector<unsigned char> ptrStore;  // id типа указателя -> storage class
  std::vector<unsigned> varType;    // id переменной -> id типа указателя
  std::vector<unsigned> decBind;    // id -> Binding (0xffffffff = нет)
  std::vector<unsigned> decSet;     // id -> DescriptorSet
  std::vector<unsigned> opOfId;     // id результата -> opcode, объявивший этот id
  std::vector<unsigned> elemOfId;   // id массива -> id элемента (OpTypeArray/RuntimeArray)
  // Декорации структур: Block(2) = uniform-буфер, BufferBlock(3) = SSBO. ИМЕННО
  // они, а не storage class, различают cbuffer и StructuredBuffer: DXC кладёт
  // StructuredBuffer в storage class Uniform(2), поэтому проверка «st==12»
  // относила все SSBO к константным буферам (первый прогон давал ssbo=0).
  std::vector<unsigned char> structBlock;       // id структуры -> Block
  std::vector<unsigned char> structBufferBlock; // id структуры -> BufferBlock
  std::vector<unsigned> constVal;               // id -> значение OpConstant (int)
  std::vector<unsigned> memOffset;              // id*64+член -> Offset в байтах
  std::vector<unsigned char> memMatrix;         // id*64+член -> есть MatrixStride
  auto memKey = [](unsigned structId, unsigned member) { return structId * 64u + member; };

  auto growU = [](std::vector<unsigned>& v, unsigned id, unsigned init)
  { if (id + 1 > v.size()) v.resize(id + 1, init); };
  auto growB = [](std::vector<unsigned char>& v, unsigned id, unsigned char init)
  { if (id + 1 > v.size()) v.resize(id + 1, init); };

  for (size_t i = 5; i < words; )
  {
    const unsigned w = Ld32(p + i * 4);
    const unsigned opcode = w & 0xffffu, wc = w >> 16;
    if (!wc || i + wc > words) break;
    // номера opcode по SPIR-V: OpTypeImage=25, OpTypeSampler=26,
    // OpTypeSampledImage=27, OpTypeArray=28, OpTypeRuntimeArray=29,
    // OpTypeStruct=30, OpTypePointer=32; OpVariable=59, OpDecorate=71.
    // opOfId заполняется ТОЛЬКО объявлениями типов: у инструкций-значений
    // (OpUndef, OpAccessChain, OpVariable) первое слово — Result Type, и
    // первая версия сканера записывала в opOfId мусор именно оттуда.
    const bool isTypeDecl = (opcode >= 19 && opcode <= 33) || opcode == 44;
    if (isTypeDecl && wc >= 2)
    {
      const unsigned res = Ld32(p + i * 4 + 4);
      growU(opOfId, res, 0);
      if (!opOfId[res]) opOfId[res] = opcode;
      if ((opcode == 28 || opcode == 29) && wc >= 3)
      {
        growU(elemOfId, res, 0xffffffffu);
        elemOfId[res] = Ld32(p + i * 4 + 8);
      }
    }
    if (opcode == 32 && wc >= 4)          // OpTypePointer: result, storage, base
    {
      const unsigned res = Ld32(p + i * 4 + 4);
      growU(ptrBase, res, 0xffffffffu);
      ptrBase[res] = Ld32(p + i * 4 + 12);
      growB(ptrStore, res, 255);
      ptrStore[res] = (unsigned char)Ld32(p + i * 4 + 8);
    }
    else if (opcode == 59 && wc >= 4)     // OpVariable: Result Type (указатель), Result <id>, Storage
    {
      // порядок слов именно такой: у OpVariable (в отличие от OpTypePointer)
      // первым идёт Result Type, и это НЕ id переменной — на этом первый
      // прогон сканера и дал пустой список ресурсов
      const unsigned ptrType = Ld32(p + i * 4 + 4);
      const unsigned res = Ld32(p + i * 4 + 8);
      growU(varType, res, 0xffffffffu);
      varType[res] = ptrType;
    }
    else if (opcode == 71 && wc >= 3)     // OpDecorate: target, decoration[, operand]
    {
      // ВАЖНО: у декораций без операнда (Block=2, BufferBlock=3, Restrict, ...)
      // word count = 3, поэтому условие «wc >= 4» их молча пропускало — и первый
      // исправленный сканер всё равно давал ssbo=0.
      const unsigned target = Ld32(p + i * 4 + 4), dec = Ld32(p + i * 4 + 8);
      const unsigned val = (wc >= 4) ? Ld32(p + i * 4 + 12) : 0;
      if (dec == 33) { growU(decBind, target, 0xffffffffu); decBind[target] = val; }
      else if (dec == 34) { growU(decSet, target, 0xffffffffu); decSet[target] = val; }
      else if (dec == 2) { growB(structBlock, target, 0); structBlock[target] = 1; }
      else if (dec == 3) { growB(structBufferBlock, target, 0); structBufferBlock[target] = 1; }
      else if (dec == 11 && wc >= 4)     // BuiltIn
      {
        ++r.builtIns;
        if (val == 0) ++r.biPosition;             // Position (gl_Position)
        else if (val == 15) ++r.biFragCoord;      // FragCoord (SV_Position в PS)
        else if (val == 42 || val == 43) ++r.biInstanceIndex;  // InstanceIndex
      }
    }
    else if (opcode == 43 && wc >= 4)    // OpConstant: Result Type, Result <id>, значение
                                         // (номер именно 43; 50 — другая инструкция,
                                         // на ней первый вариант сканера и не нашёл
                                         // ни одного доступа к членам $Globals)
    {
      const unsigned res = Ld32(p + i * 4 + 8);
      growU(constVal, res, 0xffffffffu);
      constVal[res] = Ld32(p + i * 4 + 12);
    }
    else if (opcode == 72 && wc >= 4)    // OpMemberDecorate: struct, member, decoration[, operand]
    {
      const unsigned st = Ld32(p + i * 4 + 4), mem = Ld32(p + i * 4 + 8);
      const unsigned dec = Ld32(p + i * 4 + 12);
      const unsigned val = (wc >= 5) ? Ld32(p + i * 4 + 16) : 0;
      if (dec == 35) { growU(memOffset, memKey(st, mem), 0xffffffffu); memOffset[memKey(st, mem)] = val; }
      else if (dec == 7) { growB(memMatrix, memKey(st, mem), 0); memMatrix[memKey(st, mem)] = 1; }
    }
    else if ((opcode == 65 || opcode == 66) && wc >= 5)  // OpAccessChain / OpInBoundsAccessChain
    {
      // слова: Result Type, Result <id>, Base <id>, Index <id>...
      const unsigned base = Ld32(p + i * 4 + 12);
      if (base >= varType.size() || varType[base] == 0xffffffffu) { i += wc; continue; }
      const unsigned pt = varType[base];
      if (pt >= ptrBase.size() || ptrBase[pt] == 0xffffffffu) { i += wc; continue; }
      const unsigned structId = ptrBase[pt];
      if (structId >= structBlock.size() || !structBlock[structId]) { i += wc; continue; }
      const unsigned idxId = Ld32(p + i * 4 + 16);
      if (idxId >= constVal.size() || constVal[idxId] == 0xffffffffu) { i += wc; continue; }
      const unsigned member = constVal[idxId];
      const unsigned key = memKey(structId, member);
      SpvRes::Acc a;
      a.binding = (base < decBind.size()) ? decBind[base] : 0xffffffffu;
      a.offset = (key < memOffset.size()) ? memOffset[key] : 0xffffffffu;
      a.matrix = (key < memMatrix.size() && memMatrix[key]) ? true : false;
      if (a.offset != 0xffffffffu) r.accesses.push_back(a);
    }
    i += wc;
  }

  for (size_t id = 0; id < varType.size(); ++id)
  {
    if (id >= decBind.size() || id >= decSet.size()) continue;
    if (decBind[id] == 0xffffffffu || decSet[id] == 0xffffffffu) continue;
    const unsigned pt = varType[id];
    if (pt == 0xffffffffu || pt >= ptrStore.size() || pt >= ptrBase.size()) continue;
    const unsigned st = ptrStore[pt];
    // только дескрипторные классы: UniformConstant(0), Uniform(2), StorageBuffer(12)
    if (st != 0 && st != 2 && st != 12) continue;
    unsigned base = ptrBase[pt];
    unsigned baseOp = (base < opOfId.size()) ? opOfId[base] : 0;
    if ((baseOp == 28 || baseOp == 29) && base < elemOfId.size() && elemOfId[base] != 0xffffffffu)
    {
      const unsigned el = elemOfId[base];
      baseOp = (el < opOfId.size()) ? opOfId[el] : 0;   // массив изображений = массив текстур
    }
    SpvRes::Kind k = SpvRes::kOther;
    if (baseOp == 26) k = SpvRes::kSampler;             // OpTypeSampler
    else if (baseOp == 25 || baseOp == 27) k = SpvRes::kTexture;  // Image / SampledImage
    else if (baseOp == 30)                             // OpTypeStruct: cbuffer или SSBO
    {
      const bool bb = (base < structBufferBlock.size()) && structBufferBlock[base];
      const bool bl = (base < structBlock.size()) && structBlock[base];
      if (bb) k = SpvRes::kSsbo;
      else if (bl) k = SpvRes::kCbuf;
      else k = (st == 12) ? SpvRes::kSsbo : SpvRes::kCbuf;
    }
    else if (st == 12) k = SpvRes::kSsbo;
    else if (st == 2) k = SpvRes::kCbuf;
    SpvRes::Item it;
    it.set = decSet[id];
    it.binding = decBind[id];
    it.kind = k;
    it.storage = st;
    it.baseOp = baseOp;
    r.items.push_back(it);
    if (it.set > r.maxSet) r.maxSet = it.set;
    if (it.binding > r.maxBinding) r.maxBinding = it.binding;
  }
  r.ok = true;
  return r;
}

static const char* SpvKindName(SpvRes::Kind k)
{
  switch (k)
  {
  case SpvRes::kSampler: return "smp";
  case SpvRes::kTexture: return "tex";
  case SpvRes::kCbuf:    return "ubo";
  case SpvRes::kSsbo:    return "ssbo";
  default:               return "?";
  }
}

// Одиночный DIP реальным материалом из контейнера v2 (шаг 12). Берётся
// первая запись, у которой VS и PS не требуют ничего, кроме uniform-буферов
// (в живом корпусе под это подходит WaterDummyMaterial: VS читает только
// $Globals, PS возвращает w). Создание пайплайна этого НЕ проверяет:
// раскладку матриц в $Globals (-Zpr = строчная память), семантику входа,
// макет сетов и намотку. Квадрат — левая половина клипа: ждём красной ровно
// левую половину кадра.
static void ProbeShd2Draw(nvrhi::IDevice* dev, const Shd2Spike& f, const Shd2Sec& sec)
{
  const unsigned W = 256, H = 256;
  const unsigned uboSize = getenv("PW_SHD2_UBO") ? unsigned(atoi(getenv("PW_SHD2_UBO"))) : 4096u;
  const int mat0 = getenv("PW_SHD2_MAT0") ? atoi(getenv("PW_SHD2_MAT0")) : 0;
  const int mat1 = getenv("PW_SHD2_MAT1") ? atoi(getenv("PW_SHD2_MAT1")) : 64;

  nvrhi::TextureHandle rt = dev->createTexture(nvrhi::TextureDesc()
                                                 .setDimension(nvrhi::TextureDimension::Texture2D)
                                                 .setWidth(W).setHeight(H)
                                                 .setFormat(nvrhi::Format::BGRA8_UNORM)
                                                 .setIsRenderTarget(true)
                                                 .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                                 .setKeepInitialState(true)
                                                 .setDebugName("draw rt"));
  nvrhi::TextureHandle ds = dev->createTexture(nvrhi::TextureDesc()
                                                 .setDimension(nvrhi::TextureDimension::Texture2D)
                                                 .setWidth(W).setHeight(H)
                                                 .setFormat(nvrhi::Format::D24S8)
                                                 .setIsRenderTarget(true)
                                                 // без initialState+keepInitialState NVRHI не знает
                                                 // предыдущего состояния и рвёт командный список:
                                                 // "Unknown prior state of texture draw ds"
                                                 .setInitialState(nvrhi::ResourceStates::DepthWrite)
                                                 .setKeepInitialState(true)
                                                 .setDebugName("draw ds"));
  nvrhi::FramebufferHandle fb = dev->createFramebuffer(
      nvrhi::FramebufferDesc().addColorAttachment(rt).setDepthAttachment(ds));
  if (!rt || !ds || !fb) { Print("draw: RT/depth/framebuffer", false); return; }

  unsigned usedEntry = 0xFFFFFFFFu, vsOff = 0, vsSize = 0, psOff = 0, psSize = 0;
  SpvRes vs, ps;
  for (unsigned j = 0; j < sec.cnt; ++j)
  {
    const unsigned char* e = f.data.data() + sec.off + 8 + 20 * j;
    const unsigned o0 = Ld32(e + 4), s0 = Ld32(e + 8), o1 = Ld32(e + 12), s1 = Ld32(e + 16);
    if (!s0 || !s1 || o0 + s0 > sec.end || o1 + s1 > sec.end) continue;
    SpvRes a = ScanSpv(f.data.data() + o0, s0);
    SpvRes b = ScanSpv(f.data.data() + o1, s1);
    // SSBO намеренно исключены: их содержимое (матрицы экземпляров, кости)
    // нулями не подменить — геометрия схлопнется, и проба перестанет что-либо
    // утверждать. UBO патчится по SPIR-V, текстуры/сэмплеры — пустышки.
    bool okRes = a.biPosition != 0;
    for (const SpvRes::Item& it : a.items)
      if (it.kind == SpvRes::kSsbo || it.kind == SpvRes::kOther) okRes = false;
    for (const SpvRes::Item& it : b.items)
      if (it.kind == SpvRes::kSsbo || it.kind == SpvRes::kOther) okRes = false;
    if (!okRes) continue;
    usedEntry = Ld32(e); vsOff = o0; vsSize = s0; psOff = o1; psSize = s1;
    vs = a; ps = b;
    break;
  }
  if (usedEntry == 0xFFFFFFFFu)
  {
    Print("draw: нет записи без SSBO/прочих ресурсов", false);
    return;
  }
  printf("draw: запись #%u, vs %u Б, ps %u Б, наборы: vs", usedEntry, vsSize, psSize);
  for (const SpvRes::Item& it : vs.items) printf(" %u:%u=%s", it.set, it.binding, SpvKindName(it.kind));
  printf(", ps");
  for (const SpvRes::Item& it : ps.items) printf(" %u:%u=%s", it.set, it.binding, SpvKindName(it.kind));
  printf("\n");

  nvrhi::ShaderHandle shaderVs = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex)
                         .setEntryName("MainPass_VSMain").setDebugName("draw vs"),
      f.data.data() + vsOff, vsSize);
  nvrhi::ShaderHandle shaderPs = dev->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel)
                         .setEntryName("MainPass_PSMain").setDebugName("draw ps"),
      f.data.data() + psOff, psSize);
  if (!shaderVs || !shaderPs) { Print("draw: createShader", false); return; }

  // WaterDummyMaterial: struct VS_INPUT { float3 Position : POSITION; }.
  // NVRHI на Vulkan сопоставляет атрибуты с SPIR-V по именам (in.var.POSITION).
  const nvrhi::VertexAttributeDesc attr =
      nvrhi::VertexAttributeDesc().setName("POSITION")
        .setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(0).setElementStride(12);
  nvrhi::InputLayoutHandle il = dev->createInputLayout(&attr, 1, shaderVs);
  if (!il) { Print("draw: createInputLayout (POSITION)", false); return; }

  // левая половина клип-пространства, два треугольника
  const float verts[18] = {
    -1.f, -1.f, 0.f,   0.f, -1.f, 0.f,  -1.f, 1.f, 0.f,
     0.f, -1.f, 0.f,   0.f,  1.f, 0.f,  -1.f, 1.f, 0.f,
  };
  // $Globals: что именно писать, берём из SPIR-V (см. SpvRes::accesses) —
  // смещения членов задаёт register() в _common.hlsl, имён в SPIR-V нет.
  // Матрице — единичная (строчная память, -Zpr), скаляру/вектору — 1.0.
  // PW_SHD2_MAT0/PW_SHD2_MAT1 задают ручную раскладку (диагностика).
  std::vector<unsigned char> consts(uboSize, 0);
  std::vector<SpvRes::Acc> acc;
  for (const SpvRes::Acc& a : vs.accesses) acc.push_back(a);
  for (const SpvRes::Acc& a : ps.accesses) acc.push_back(a);
  std::vector<unsigned> patched;
  for (const SpvRes::Acc& a : acc)
  {
    if (a.offset + (a.matrix ? 64u : 16u) > uboSize) continue;
    bool seen = false;
    for (unsigned o : patched) if (o == a.offset) seen = true;
    if (seen) continue;
    patched.push_back(a.offset);
    float* dst = reinterpret_cast<float*>(&consts[size_t(a.offset)]);
    if (a.matrix)
      for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) dst[r * 4 + c] = (r == c) ? 1.f : 0.f;
    else
      for (int c = 0; c < 4; ++c) dst[c] = 1.f;
  }
  if (getenv("PW_SHD2_MAT0") || getenv("PW_SHD2_MAT1"))
  {
    patched.clear();
    const int mofs[2] = { mat0, mat1 };
    for (int m = 0; m < 2; ++m)
    {
      if (size_t(mofs[m]) + 64 > uboSize) continue;
      patched.push_back(unsigned(mofs[m]));
      float* dst = reinterpret_cast<float*>(&consts[size_t(mofs[m])]);
      for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
        dst[r * 4 + c] = (r == c) ? 1.f : 0.f;
    }
  }
  printf("       draw: $Globals %u Б, патч по %u членам:", uboSize, (unsigned)patched.size());
  for (unsigned o : patched) printf(" %u%s", o, "");
  printf("\n");
  if (patched.empty()) { Print("draw: в шейдере нет доступов к членам $Globals", false); return; }

  nvrhi::BufferHandle vbuf = dev->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(sizeof(verts))
                                                 .setIsVertexBuffer(true)
                                                 .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                 .setKeepInitialState(true));
  if (!vbuf) { Print("draw: createBuffer(VB)", false); return; }

  // по одному BindingLayout/BindingSet на набор (механизм §3.z п.1)
  const unsigned setCount = (vs.maxSet > ps.maxSet ? vs.maxSet : ps.maxSet) + 1;
  std::vector<nvrhi::BufferHandle> cbufs;
  std::vector<nvrhi::TextureHandle> dummyTex;
  std::vector<nvrhi::SamplerHandle> dummySmp;
  std::vector<nvrhi::BindingLayoutHandle> layouts;
  std::vector<nvrhi::BindingSetHandle> bsets;
  struct Bound { unsigned set, binding; };
  std::vector<Bound> bound;
  for (unsigned s = 0; s < setCount; ++s)
  {
    nvrhi::BindingLayoutDesc ld;
    ld.visibility = nvrhi::ShaderType::AllGraphics;
    ld.setRegisterSpaceAndDescriptorSet(s);
    ld.bindingOffsets = nvrhi::VulkanBindingOffsets()
      .setShaderResourceOffset(0).setSamplerOffset(0)
      .setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
    nvrhi::BindingSetDesc bsd;
    bool unsupported = false;
    for (unsigned side = 0; side < 2; ++side)
    {
      const SpvRes& st = side ? ps : vs;
      for (const SpvRes::Item& it : st.items)
      {
        if (it.set != s) continue;
        bool already = false;
        for (const Bound& bnd : bound) if (bnd.set == it.set && bnd.binding == it.binding) already = true;
        if (already) continue;   // VS и PS делят $Globals — один слот, один ресурс
        bound.push_back({ it.set, it.binding });
        if (it.kind == SpvRes::kCbuf)
        {
          nvrhi::BufferHandle cb = dev->createBuffer(nvrhi::BufferDesc()
                                                       .setByteSize(uboSize)
                                                       .setIsConstantBuffer(true)
                                                       .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                                       .setKeepInitialState(true));
          if (!cb) { Print("draw: createBuffer(CB)", false); return; }
          cbufs.push_back(cb);
          ld.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(it.binding));
          bsd.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(it.binding, cb));
        }
        else if (it.kind == SpvRes::kTexture)
        {
          // пустышка 4x4: содержимое не инициализируется — проба проверяет
          // геометрию, а не выборку
          nvrhi::TextureHandle tx = dev->createTexture(nvrhi::TextureDesc()
                                                         .setDimension(nvrhi::TextureDimension::Texture2D)
                                                         .setWidth(4).setHeight(4)
                                                         .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                         .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                         .setKeepInitialState(true)
                                                         .setDebugName("draw dummy tex"));
          if (!tx) { Print("draw: createTexture(dummy)", false); return; }
          dummyTex.push_back(tx);
          ld.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(it.binding));
          bsd.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(it.binding, tx));
        }
        else if (it.kind == SpvRes::kSampler)
        {
          nvrhi::SamplerHandle sp = dev->createSampler(nvrhi::SamplerDesc().setAllFilters(false)
                                                        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
          if (!sp) { Print("draw: createSampler(dummy)", false); return; }
          dummySmp.push_back(sp);
          ld.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(it.binding));
          bsd.bindings.push_back(nvrhi::BindingSetItem::Sampler(it.binding, sp));
        }
        else unsupported = true;
      }
    }
    if (unsupported) { Print("draw: набор с SSBO/прочими ресурсами — пропуск пробы", false); return; }
    if (ld.bindings.empty()) continue;
    nvrhi::BindingLayoutHandle lh = dev->createBindingLayout(ld);
    nvrhi::BindingSetHandle bs = lh ? dev->createBindingSet(bsd, lh) : nvrhi::BindingSetHandle();
    if (!lh || !bs) { Print("draw: createBindingLayout/createBindingSet", false); return; }
    layouts.push_back(lh);
    bsets.push_back(bs);
  }

  nvrhi::GraphicsPipelineDesc pd;
  pd.setPrimType(nvrhi::PrimitiveType::TriangleList)
    .setInputLayout(il).setVertexShader(shaderVs).setPixelShader(shaderPs);
  // намотка: у NVRHI viewport с отрицательной высотой, без этого флага
  // геометрия вырезается (замер b0cull: 0 против 3072 пикселей)
  pd.renderState.rasterState.setCullNone().setFrontCounterClockwise(true);
  for (const nvrhi::BindingLayoutHandle& lh : layouts) pd.addBindingLayout(lh);
  nvrhi::GraphicsPipelineHandle pipe = dev->createGraphicsPipeline(pd, fb->getFramebufferInfo());
  if (!pipe) { Print("draw: createGraphicsPipeline", false); return; }

  nvrhi::StagingTextureHandle staging = dev->createStagingTexture(
    nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
      .setWidth(W).setHeight(H).setFormat(nvrhi::Format::BGRA8_UNORM)
      .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true),
    nvrhi::CpuAccessMode::Read);
  if (!staging) { Print("draw: createStagingTexture", false); return; }

  nvrhi::CommandListHandle up = dev->createCommandList();
  up->open();
  up->writeBuffer(vbuf, verts, sizeof(verts));
  for (const nvrhi::BufferHandle& cb : cbufs) up->writeBuffer(cb, consts.data(), consts.size());
  up->close();
  dev->executeCommandList(up);

  nvrhi::CommandListHandle cl = dev->createCommandList();
  cl->open();
  cl->clearTextureFloat(rt, nvrhi::TextureSubresourceSet(), nvrhi::Color(0.f, 1.f, 0.f, 1.f));
  // глубина обязана быть очищена: у WaterDummy PS возвращает w, у единичной
  // ViewProjection это 1.0, а дефолтный depthFunc = Less и нечищеный буфер
  // (0) вырезает весь кадр молча
  cl->clearDepthStencilTexture(ds, nvrhi::TextureSubresourceSet(), true, 1.f, true, 0);
  nvrhi::GraphicsState gs;
  gs.setPipeline(pipe).setFramebuffer(fb)
    .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vbuf).setSlot(0).setOffset(0));
  for (const nvrhi::BindingSetHandle& bs : bsets) gs.addBindingSet(bs);
  gs.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(float(W), float(H))));
  cl->setGraphicsState(gs);
  cl->draw(nvrhi::DrawArguments().setVertexCount(6));
  cl->copyTexture(staging, nvrhi::TextureSlice(), rt, nvrhi::TextureSlice());
  cl->close();
  dev->executeCommandList(cl);
  dev->waitForIdle();

  size_t pitch = 0;
  void* px = dev->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch);
  if (!px) { Print("draw: mapStagingTexture", false); return; }
  unsigned redLeft = 0, redRight = 0, green = 0, other = 0, chLeft = 0, chRight = 0;
  for (unsigned y = 0; y < H; ++y)
  {
    const unsigned char* row = static_cast<const unsigned char*>(px) + size_t(y) * pitch;
    for (unsigned x = 0; x < W; ++x)
    {
      const unsigned char b = row[x * 4 + 0], g = row[x * 4 + 1], r = row[x * 4 + 2];
      if (r > 200 && g < 60 && b < 60) { if (x < W / 2) ++redLeft; else ++redRight; }
      else if (g > 200 && r < 60) ++green;
      else ++other;
      // фон — зелёный clear; всё остальное — результат рисования (цвет PS
      // зависит от констант, поэтому для «не WaterDummy» проверяется только
      // геометрия: пиксели есть слева и их нет справа)
      if (!(g > 200 && r < 60 && b < 60)) { if (x < W / 2) ++chLeft; else ++chRight; }
    }
  }
  dev->unmapStagingTexture(staging);
  const unsigned half = (W / 2) * H;
  const bool exact = redLeft == half && redRight == 0 && other == 0;
  const bool geom = chLeft > 0 && chRight == 0;
  printf("       draw: красных слева %u/%u, справа %u, зелёных %u, прочих %u | нефон слева %u, справа %u\n",
         redLeft, half, redRight, green, other, chLeft, chRight);
  char detail[160];
  snprintf(detail, sizeof(detail),
           "%s; патч %u членов $Globals по SPIR-V, наборов %u, пустышек: tex %u smp %u",
           exact ? "квадрат ровно в левой половине" : "геометрия только в левой половине",
           (unsigned)patched.size(), (unsigned)layouts.size(),
           (unsigned)dummyTex.size(), (unsigned)dummySmp.size());
  Print("draw: DIP материалом из .shd v2", exact || geom, detail);
}

static void ProbeShd2(nvrhi::IDevice* dev)
{
  const char* path = getenv("PW_SHD2");
  if (!path) return;
  const int maxEntries = getenv("PW_SHD2_MAX") ? atoi(getenv("PW_SHD2_MAX")) : 0;
  const int verbose = getenv("PW_SHD2_VERBOSE") ? atoi(getenv("PW_SHD2_VERBOSE")) : 0;

  Shd2Spike f;
  if (!f.open(path))
  {
    Print("shd2: файл не разбирается как v2", false, path);
    return;
  }
  const Shd2Sec* sec = f.find(2 /*stSpirv*/);
  if (!sec)
  {
    Print("shd2: секции SPIR-V нет", false, path);
    return;
  }
  printf("shd2: %s\n       v1 %u Б, целей %d, SPIR-V: записей %u, блоки [%u,%u)\n",
         path, f.v1Size, (int)f.secs.size(), sec->cnt, sec->off + 8 + 20 * sec->cnt, sec->end);

  nvrhi::TextureHandle rt = dev->createTexture(nvrhi::TextureDesc()
                                                 .setDimension(nvrhi::TextureDimension::Texture2D)
                                                 .setWidth(1024).setHeight(768)
                                                 .setFormat(nvrhi::Format::BGRA8_UNORM)   // = VK_FORMAT_B8G8R8A8_UNORM, тот же, что у swapchain
                                                 .setIsRenderTarget(true)
                                                 .setDebugName("shd2 rt"));
  nvrhi::TextureHandle ds = dev->createTexture(nvrhi::TextureDesc()
                                                 .setDimension(nvrhi::TextureDimension::Texture2D)
                                                 .setWidth(1024).setHeight(768)
                                                 .setFormat(nvrhi::Format::D24S8)
                                                 .setIsRenderTarget(true)   // глубина у NVRHI определяется форматом
                                                 .setDebugName("shd2 ds"));
  nvrhi::FramebufferHandle fb = dev->createFramebuffer(
      nvrhi::FramebufferDesc().addColorAttachment(rt).setDepthAttachment(ds));
  if (!rt || !ds || !fb)
  {
    Print("shd2: RT/depth/framebuffer", false);
    return;
  }
  Print("shd2: RT BGRA8_UNORM + depth D24S8 + framebuffer", true);

  unsigned createdShaders = 0, createdPipes = 0, tried = 0, withLayout = 0, withLayoutOk = 0;
  unsigned globalMaxSet = 0, globalMaxBinding = 0, kindCount[5] = { 0, 0, 0, 0, 0 };
  unsigned vsNoPosition = 0, psNoFragCoord = 0, vsWithInstanceIndex = 0;

  for (unsigned j = 0; j < sec->cnt; ++j)
  {
    if (maxEntries && (int)j >= maxEntries) break;
    const unsigned char* e = f.data.data() + sec->off + 8 + 20 * j;
    const unsigned index = Ld32(e), vsOff = Ld32(e + 4), vsSize = Ld32(e + 8);
    const unsigned psOff = Ld32(e + 12), psSize = Ld32(e + 16);
    if (!vsSize || !psSize || vsOff + vsSize > sec->end || psOff + psSize > sec->end)
    {
      Print("shd2: запись вне блоб-области", false, path);
      continue;
    }
    const SpvRes vs = ScanSpv(f.data.data() + vsOff, vsSize);
    const SpvRes ps = ScanSpv(f.data.data() + psOff, psSize);
    for (const SpvRes::Item& it : vs.items) { kindCount[it.kind]++; }
    if (verbose)
    {
      printf("       #%u vs[%u Б]:", index, vsSize);
      for (const SpvRes::Item& it : vs.items)
        printf(" %u:%u=%s", it.set, it.binding, SpvKindName(it.kind)),
              (it.kind == SpvRes::kOther ? printf("(st=%u,op=%u)", it.storage, it.baseOp) : 0);
      printf(" ps[%u Б]:", psSize);
      for (const SpvRes::Item& it : ps.items)
        printf(" %u:%u=%s", it.set, it.binding, SpvKindName(it.kind)),
              (it.kind == SpvRes::kOther ? printf("(st=%u,op=%u)", it.storage, it.baseOp) : 0);
      printf("  | bi: vs pos=%u inst=%u, ps frag=%u\n",
             vs.biPosition, vs.biInstanceIndex, ps.biFragCoord);
    }
    for (const SpvRes::Item& it : ps.items) { kindCount[it.kind]++; }
    if (!vs.biPosition) ++vsNoPosition;
    if (!ps.biFragCoord) ++psNoFragCoord;
    if (vs.biInstanceIndex) ++vsWithInstanceIndex;
    if (vs.maxSet > globalMaxSet) globalMaxSet = vs.maxSet;
    if (ps.maxSet > globalMaxSet) globalMaxSet = ps.maxSet;
    if (vs.maxBinding > globalMaxBinding) globalMaxBinding = vs.maxBinding;
    if (ps.maxBinding > globalMaxBinding) globalMaxBinding = ps.maxBinding;

    nvrhi::ShaderHandle shaderVs = dev->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex)
                           .setEntryName("MainPass_VSMain")
                           .setDebugName("shd2 vs"),
        f.data.data() + vsOff, vsSize);
    nvrhi::ShaderHandle shaderPs = dev->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel)
                           .setEntryName("MainPass_PSMain")
                           .setDebugName("shd2 ps"),
        f.data.data() + psOff, psSize);
    ++tried;
    if (shaderVs) ++createdShaders;
    if (shaderPs) ++createdShaders;
    if (!shaderVs || !shaderPs)
    {
      printf("       #%u vs %u Б / ps %u Б -> vs=%d ps=%d\n", index, vsSize, psSize,
             int(bool(shaderVs)), int(bool(shaderPs)));
      continue;
    }

    nvrhi::GraphicsPipelineDesc pd;
    pd.setPrimType(nvrhi::PrimitiveType::TriangleList)
      .setVertexShader(shaderVs).setPixelShader(shaderPs);
    pd.renderState.rasterState.setCullNone().setFrontCounterClockwise(true);
    nvrhi::GraphicsPipelineHandle pipe = dev->createGraphicsPipeline(pd, fb->getFramebufferInfo());
    if (pipe) ++createdPipes;
    else
    {
      printf("       #%u: пайплайн без раскладок НЕ собрался\n", index);
      continue;
    }

    // Вторая ступень: раскладка, собранная по фактическим сетами/binding'ам
    // модуля (по одному BindingLayout на набор — NVRHI умеет
    // setRegisterSpaceAndDescriptorSet, см. vulkan-resource-bindings.cpp:1031).
    std::vector<nvrhi::BindingLayoutHandle> layouts;
    bool layoutBuilt = true;
    const unsigned setCount = (vs.maxSet > ps.maxSet ? vs.maxSet : ps.maxSet) + 1;
    for (unsigned s = 0; s < setCount; ++s)
    {
      nvrhi::BindingLayoutDesc ld;
      ld.visibility = nvrhi::ShaderType::AllGraphics;
      ld.setRegisterSpaceAndDescriptorSet(s);
      ld.bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0).setSamplerOffset(0)
        .setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
      auto addFor = [&](const SpvRes& r)
      {
        for (const SpvRes::Item& it : r.items)
        {
          if (it.set != s) continue;
          switch (it.kind)
          {
          case SpvRes::kSampler: ld.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(it.binding)); break;
          case SpvRes::kTexture: ld.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(it.binding)); break;
          case SpvRes::kCbuf:    ld.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(it.binding)); break;
          case SpvRes::kSsbo:    ld.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(it.binding)); break;
          default: layoutBuilt = false; break;
          }
        }
      };
      addFor(vs); addFor(ps);
      if (!layoutBuilt || ld.bindings.empty()) continue;
      nvrhi::BindingLayoutHandle h = dev->createBindingLayout(ld);
      if (!h) { layoutBuilt = false; break; }
      layouts.push_back(h);
    }
    if (layoutBuilt && !layouts.empty())
    {
      ++withLayout;
      nvrhi::GraphicsPipelineDesc pd2 = pd;
      for (const nvrhi::BindingLayoutHandle& h : layouts) pd2.addBindingLayout(h);
      if (dev->createGraphicsPipeline(pd2, fb->getFramebufferInfo())) ++withLayoutOk;
      else printf("       #%u: с раскладкой по фактическим сетам НЕ слинковался\n", index);
    }
  }

  printf("       итог: пар %u, шейдеров создано %u/%u, пайплайнов без раскладок %u, "
         "с раскладкой %u/%u\n", tried, createdShaders, tried * 2, createdPipes,
         withLayoutOk, withLayout);
  printf("       ресурсы SPIR-V: наборов %u, binding макс %u; "
         "smp=%d tex=%d ubo=%d ssbo=%d прочее=%d\n",
         globalMaxSet + 1, globalMaxBinding, kindCount[SpvRes::kSampler],
         kindCount[SpvRes::kTexture], kindCount[SpvRes::kCbuf], kindCount[SpvRes::kSsbo],
         kindCount[SpvRes::kOther]);
  // Без записи gl_Position вершина уходит в неопределённую позицию — это не
  // ошибка создания пайплайна, поэтому считается отдельно (ловушка шага 13).
  printf("       BuiltIn: VS без Position %u, PS без FragCoord %u, VS с InstanceIndex %u\n",
         vsNoPosition, psNoFragCoord, vsWithInstanceIndex);

  if (getenv("PW_SHD2_DRAW")) ProbeShd2Draw(dev, f, *sec);
}

#ifdef PW_WITH_SDL3
#include <SDL3/SDL.h>

// SDL3 как поставщик окна (шаг 12 очереди, план §3.z п.6). Проверяется ровно
// то, чего не может дать WndProc-путь: поднимает ли vendored SDL3 видео под
// Wine, и годится ли его HWND для VkSurfaceKHR (дальше NvrhiBackend строит
// swapchain по тому же коду, что у win32-спайка). Ввод здесь НЕ проверяется —
// он остаётся на подклассе WndProc клиента.
static bool ProbeSdl3Window(nvrhi::IDevice* dev, VkInstance instance, VkPhysicalDevice phys,
                            VkDevice device, VkQueue queue, uint32_t queueFamily)
{
  if (!SDL_Init(SDL_INIT_VIDEO))
  {
    Print("sdl3: SDL_Init(SDL_INIT_VIDEO)", false, SDL_GetError());
    return false;
  }
  Print("sdl3: SDL_Init", true, SDL_GetCurrentVideoDriver());

  SDL_Window* win = SDL_CreateWindow("nvrhi_probe sdl3", 160, 120, 0);
  if (!win)
  {
    Print("sdl3: SDL_CreateWindow", false, SDL_GetError());
    SDL_Quit();
    return false;
  }
  int bw = 0, bh = 0;
  SDL_GetWindowSize(win, &bw, &bh);
  char info[128];
  snprintf(info, sizeof(info), "%dx%d", bw, bh);
  Print("sdl3: SDL_CreateWindow", true, info);

  HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  if (!hwnd)
  {
    Print("sdl3: HWND из окна SDL", false, SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return false;
  }
  Print("sdl3: SDL_PROP_WINDOW_WIN32_HWND_POINTER", true);

  bool ok = false;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  do
  {
    VkWin32SurfaceCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleA(nullptr);
    sci.hwnd = hwnd;
    if (vkp::CreateWin32SurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS)
    {
      Print("sdl3: vkCreateWin32SurfaceKHR(HWND SDL)", false);
      break;
    }
    Print("sdl3: VkSurfaceKHR из HWND SDL", true);

    VkBool32 supported = VK_FALSE;
    vkp::GetPhysicalDeviceSurfaceSupportKHR(phys, queueFamily, surface, &supported);
    if (!supported)
    {
      Print("sdl3: surface supported", false);
      break;
    }
    Print("sdl3: vkGetPhysicalDeviceSurfaceSupportKHR", true);

    VkSurfaceCapabilitiesKHR caps = {};
    vkp::GetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    uint32_t fmtCount = 0;
    vkp::GetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    VkSurfaceFormatKHR fmts[16] = {};
    if (fmtCount > 16) fmtCount = 16;
    vkp::GetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts);
    if (!fmtCount)
    {
      Print("sdl3: surface formats", false);
      break;
    }
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fmtCount; ++i)
      if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
          fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      {
        chosen = fmts[i];
        break;
      }

    uint32_t width = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent.width : (uint32_t)bw;
    uint32_t height = caps.currentExtent.height != 0xFFFFFFFFu ? caps.currentExtent.height : (uint32_t)bh;
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swci = {};
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface;
    swci.minImageCount = imageCount;
    swci.imageFormat = chosen.format;
    swci.imageColorSpace = chosen.colorSpace;
    swci.imageExtent = { width, height };
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;

    if (vkp::CreateSwapchainKHR(device, &swci, nullptr, &swapchain) != VK_SUCCESS)
    {
      Print("sdl3: vkCreateSwapchainKHR", false);
      break;
    }
    uint32_t imgCount = 0;
    vkp::GetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
    snprintf(info, sizeof(info), "%ux%u, картинок %u", width, height, imgCount);
    Print("sdl3: VkSwapchainKHR", true, info);

    std::vector<VkImage> images(imgCount);
    vkp::GetSwapchainImagesKHR(device, swapchain, &imgCount, images.data());
    nvrhi::TextureHandle back = dev->createHandleForNativeTexture(
        nvrhi::ObjectTypes::VK_Image, nvrhi::Object(images[0]),
        nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setFormat(nvrhi::Format::SBGRA8_UNORM)
            .setWidth(width).setHeight(height)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::RenderTarget)
            .setKeepInitialState(true)
            .setDebugName("sdl3 back"));
    if (!back)
    {
      Print("sdl3: createHandleForNativeTexture(бэк-буфер SDL)", false);
      break;
    }
    Print("sdl3: NVRHI принял бэк-буфер swapchain'а", true);
    ok = true;
  } while (false);

  if (swapchain != VK_NULL_HANDLE) vkp::DestroySwapchainKHR(device, swapchain, nullptr);
  if (surface != VK_NULL_HANDLE) vkp::DestroySurfaceKHR(instance, surface, nullptr);
  SDL_DestroyWindow(win);
  SDL_PumpEvents();
  SDL_Quit();
  return ok;
}
#endif

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

  // VK_KHR_dynamic_rendering: NVRHI создаёт пайплайны с renderPass =
// VK_NULL_HANDLE (рендер-пассов в его API нет). Одной только фичи
// Vulkan13.dynamicRendering недостаточно — на Mesa (llvmpipe/lavapipe)
// vkCreateGraphicsPipelines с null-renderPass без включённого расширения
// возвращает VK_ERROR_UNKNOWN (-13).
  const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME };
  VkDeviceCreateInfo dci = {};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = &f12;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 3;
  dci.ppEnabledExtensionNames = devExts;

  VkDevice device = VK_NULL_HANDLE;
  r = vkp::CreateDevice(phys, &dci, nullptr, &device);
  if (r != VK_SUCCESS)
  {
    Print("vulkan: vkCreateDevice", false, vkResultToString(r));
    vkp::DestroyInstance(instance, nullptr);
    return false;
  }
  Print("vulkan: vkCreateDevice", true, "ext: swapchain + synchronization2 + dynamic_rendering");

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

  // Сообщения NVRHI (в т.ч. причины отказа createGraphicsPipeline) — в вывод,
  // иначе отказ объясняется молча.
  struct ProbeMsg : nvrhi::IMessageCallback
  {
    void message(nvrhi::MessageSeverity severity, const char* text) override
    {
      if (severity != nvrhi::MessageSeverity::Info)
        Print("nvrhi>", false, text);
    }
  };
  static ProbeMsg g_probeMsg;

  nvrhi::vulkan::DeviceDesc dd;
  dd.errorCB = &g_probeMsg;
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
    SmokeShaderPipeline(nvrhiDevice, instance, device);
    ProbeShd2(nvrhiDevice);
#ifdef _WIN32
    ProbeWin32Swapchain(nvrhiDevice, instance, phys, device, queue, graphicsFamily);
#ifdef PW_WITH_SDL3
    ProbeSdl3Window(nvrhiDevice, instance, phys, device, queue, graphicsFamily);
#endif
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
