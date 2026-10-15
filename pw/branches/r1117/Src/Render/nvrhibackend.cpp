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
#include "shadercontainer.h"                              // формат .shd v2
#include "System/FileSystem/FileStream.h"                 // Stream для чтения контейнера
#include "System/FileSystem/FileSystem.h"                 // RootFileSystem::OpenFile

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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Render
{

//////////////////////////////////////////////////////////////////////////
// Учёт незакрытых методов. Порядок списка = порядок, в котором методы идут в
// seam'е; имена попадают в DebugTrace, так что дамп читается как план работ.
// Шаг 12b снял из списка всё, что реализовано (ресурсы, биндинги, вывод,
// ReadSurface); ниже остались только действительно незакрытые.
#define NVRHI_STUB_LIST(X) \
  X(SetTexture) X(DrawIndexedUP) X(DrawUP) \
  X(CreateTexture2D) X(CreateTextureCube) X(UpdateTexture) \
  X(GetRenderTargetData) X(ColorFill) X(StretchRect) X(GetRenderTarget)

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
// Ручки seam'а = индекс+1 в таблицах Impl (0 = «нет»). 8 потоков вершин —
// столько клиент реально использует (замер шага 9: MapStream поднимает логический
// номер при инстансном режиме, физический слот инстанса — отдельный).
static const unsigned kNoRes = 0xffffffffu;
static const unsigned kMaxVertexStreams = 8;

static unsigned Hnd2Idx(const void* p) { return p ? (unsigned)(uintptr_t)p - 1u : kNoRes; }
static void* Idx2Hnd(unsigned i) { return i == kNoRes ? 0 : (void*)(uintptr_t)(i + 1u); }

// Формат вершинного атрибута: клиент описывает его своим enum'ом (FVF-наследие).
static nvrhi::Format VtxFormat(EVertexElementType t)
{
  switch (t)
  {
  case VERTEXELEMENTTYPE_FLOAT1:    return nvrhi::Format::R32_FLOAT;
  case VERTEXELEMENTTYPE_FLOAT2:    return nvrhi::Format::RG32_FLOAT;
  case VERTEXELEMENTTYPE_FLOAT3:    return nvrhi::Format::RGB32_FLOAT;
  case VERTEXELEMENTTYPE_FLOAT4:    return nvrhi::Format::RGBA32_FLOAT;
  case VERTEXELEMENTTYPE_D3DCOLOR:  return nvrhi::Format::BGRA8_UNORM;
  case VERTEXELEMENTTYPE_UBYTE4N:   return nvrhi::Format::RGBA8_UNORM;
  case VERTEXELEMENTTYPE_UBYTE4:    return nvrhi::Format::RGBA8_UINT;
  case VERTEXELEMENTTYPE_SHORT2:    return nvrhi::Format::RG16_SINT;
  case VERTEXELEMENTTYPE_SHORT4:    return nvrhi::Format::RGBA16_SINT;
  case VERTEXELEMENTTYPE_SHORT2N:   return nvrhi::Format::RG16_SNORM;
  case VERTEXELEMENTTYPE_SHORT4N:   return nvrhi::Format::RGBA16_SNORM;
  case VERTEXELEMENTTYPE_USHORT2N:  return nvrhi::Format::RG16_UNORM;
  case VERTEXELEMENTTYPE_USHORT4N:  return nvrhi::Format::RGBA16_UNORM;
  default:                          return nvrhi::Format::UNKNOWN;
  }
}

// usage+index -> имя семантики HLSL. DXC оставляет семантику в SPIR-V
// (OpName "in.var.POSITION"), а NVRHI на Vulkan назначает location по порядку
// атрибутов, поэтому имя нужно как ключ к тому, что назначил компилятор.
// В живом корпусе семантики такие (замер по Src/Render/Shaders/*.hlsl):
// POSITION, TEXCOORDn, COLOR/COLORn, NORMAL, BLENDWEIGHTn, BLENDINDICESn,
// TANGENT, BINORMAL.
static void SemanticName(EVertexElementUsage usage, unsigned idx, char* pOut, size_t outSize)
{
  const char* base = "?";
  bool numbered = false;
  switch (usage)
  {
  case VERETEXELEMENTUSAGE_POSITION:     base = "POSITION"; break;
  case VERETEXELEMENTUSAGE_POSITIONT:    base = "POSITION"; break;
  case VERETEXELEMENTUSAGE_NORMAL:       base = "NORMAL"; break;
  case VERETEXELEMENTUSAGE_TANGENT:      base = "TANGENT"; break;
  case VERETEXELEMENTUSAGE_BINORMAL:     base = "BINORMAL"; break;
  case VERETEXELEMENTUSAGE_TEXCOORD:     base = "TEXCOORD"; numbered = true; break;
  case VERETEXELEMENTUSAGE_COLOR:        base = "COLOR"; numbered = true; break;
  case VERETEXELEMENTUSAGE_BLENDWEIGHT:  base = "BLENDWEIGHT"; numbered = true; break;
  case VERETEXELEMENTUSAGE_BLENDINDICES: base = "BLENDINDICES"; numbered = true; break;
  case VERETEXELEMENTUSAGE_PSIZE:        base = "PSIZE"; break;
  case VERETEXELEMENTUSAGE_FOG:          base = "FOG"; break;
  case VERETEXELEMENTUSAGE_DEPTH:        base = "DEPTH"; break;
  case VERETEXELEMENTUSAGE_SAMPLE:       base = "SAMPLE"; break;
  case VERETEXELEMENTUSAGE_TESSFACTOR:   base = "TESSFACTOR"; break;
  default: break;
  }
  if (numbered) snprintf(pOut, outSize, "%s%u", base, idx);
  else          snprintf(pOut, outSize, "%s", base);
}

static unsigned PrimVertexCount(ERenderPrimitiveType type, unsigned primitiveCount)
{
  switch (type)
  {
  case RENDERPRIMITIVE_POINTLIST:     return primitiveCount;
  case RENDERPRIMITIVE_LINELIST:      return primitiveCount * 2u;
  case RENDERPRIMITIVE_LINESTRIP:     return primitiveCount + 1u;
  case RENDERPRIMITIVE_TRIANGLELIST:  return primitiveCount * 3u;
  case RENDERPRIMITIVE_TRIANGLESTRIP: return primitiveCount + 2u;
  default:                            return 0;   // TRIANGLEFAN в Vulkan нет
  }
}

static nvrhi::PrimitiveType PrimType(ERenderPrimitiveType type)
{
  switch (type)
  {
  case RENDERPRIMITIVE_POINTLIST:     return nvrhi::PrimitiveType::PointList;
  case RENDERPRIMITIVE_LINELIST:      return nvrhi::PrimitiveType::LineList;
  case RENDERPRIMITIVE_LINESTRIP:     return nvrhi::PrimitiveType::LineStrip;
  case RENDERPRIMITIVE_TRIANGLELIST:  return nvrhi::PrimitiveType::TriangleList;
  case RENDERPRIMITIVE_TRIANGLESTRIP: return nvrhi::PrimitiveType::TriangleStrip;
  default:                            return nvrhi::PrimitiveType::TriangleList;
  }
}

static nvrhi::Format SurfaceFormat(EBackendFormat f)
{
  switch (f)
  {
  case bfA8R8G8B8:      return nvrhi::Format::BGRA8_UNORM;
  case bfX8R8G8B8:      return nvrhi::Format::BGRX8_UNORM;
  case bfA8:            return nvrhi::Format::R8_UNORM;
  case bfL8:            return nvrhi::Format::R8_UNORM;
  case bfL16:           return nvrhi::Format::R16_UNORM;
  case bfR5G6B5:        return nvrhi::Format::B5G6R5_UNORM;
  case bfR16F:          return nvrhi::Format::R16_FLOAT;
  case bfA16B16G16R16F: return nvrhi::Format::RGBA16_FLOAT;
  case bfR32F:          return nvrhi::Format::R32_FLOAT;
  case bfG32R32F:       return nvrhi::Format::RG32_FLOAT;
  case bfA32B32G32R32F: return nvrhi::Format::RGBA32_FLOAT;
  case bfDXT1:          return nvrhi::Format::BC1_UNORM;
  case bfDXT3:          return nvrhi::Format::BC2_UNORM;
  case bfDXT5:          return nvrhi::Format::BC3_UNORM;
  case bfD24S8:         return nvrhi::Format::D24S8;
  default:              return nvrhi::Format::UNKNOWN;
  }
}

//////////////////////////////////////////////////////////////////////////
// Поверхности отдаются клиенту ручкой HSurface. В D3D9Backend это указатель на
// COM-объект; здесь — индекс в таблице (0xffffffff = «нет»), потому что
// nvrhi::TextureHandle — счётный умный указатель и хранить его в ручке нельзя.
struct SurfaceObj
{
  nvrhi::TextureHandle tex;
  nvrhi::StagingTextureHandle staging;  // для ReadSurface (создаётся лениво)
  bool backBuffer;
  SurfaceObj() : backBuffer(false) {}
};

// NVRHI пишет сюда ошибки трекера состояний, несовпадений наборов и т.п. Без
// errorCB они уходят в OutputDebugStringA, который под Wine не виден вовсе
// (замер 2026-10-15: прогон 12b с пустым wine-логом и 0 нарисованными
// пикселями не показал ни одной причины). Дозируется: 40 сообщений.
class VkMsgLog : public nvrhi::IMessageCallback
{
public:
  unsigned total;
  VkMsgLog() : total(0) {}
  virtual void message(nvrhi::MessageSeverity severity, const char* messageText)
  {
    if (total < 40u)
      DebugTrace("VkMsg[%d]: %s", (int)severity, messageText ? messageText : "");
    ++total;
  }
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
  VkMsgLog msgLog;
  nvrhi::CommandListHandle commandList;

  std::vector<nvrhi::TextureHandle> backTextures;   // по одному на картинку swapchain'а
  std::vector<SurfaceObj> surfaces;                 // индекс + 1 = значение HSurface.p
  unsigned currentSurface;                          // поверхность текущего бэк-буфера

  VkPhysicalDeviceProperties props;
  char deviceName[128];

  unsigned stubCalls[st_count];
  unsigned presents;
  unsigned swapRecreates;         // сколько раз пересоздали swapchain
  unsigned surfaceRecreates;      // из них — с пересозданием VkSurfaceKHR (окно пересоздано)

  //////////////////////////////////////////////////////////////////////
  // Шаг 12b: ресурсы, состояние вывода, пайплайны.
  //
  // Ручки seam'а (HBuffer/HShader/HInputLayout/HSurface) = индекс+1 в этих
  // таблицах: nvrhi::Handle — умный указатель со счётчиком, в void* его не
  // кладут.
  struct BufferObj
  {
    nvrhi::BufferHandle buf;
    std::vector<unsigned char> cpu;   // зеркало для Lock/Unlock
    bool isIndex;
    bool index32;
    bool locked;
    BufferObj() : isIndex(false), index32(false), locked(false) {}
  };
  struct ShaderObj
  {
    nvrhi::ShaderHandle sh;
    std::string entry;                // точка входа из OpEntryPoint
    // входы VS: имя семантики из SPIR-V (in.var.POSITION) -> location
    std::vector<std::pair<std::string, unsigned> > inputs;
  };
  struct LayoutObj
  {
    nvrhi::InputLayoutHandle il;
    // копия описания клиента — раскладку собираем позже, чем CreateInputLayout:
    // шаг потока известен только на момент вывода
    std::vector<VertexElementDescriptor> elems;
  };
  struct PipeObj
  {
    nvrhi::GraphicsPipelineHandle pipe;
    nvrhi::FramebufferHandle fb;
    unsigned vs, ps, layout, rt, ds, prim;
  };
  std::vector<BufferObj> buffers;
  std::vector<ShaderObj> shaderObjs;
  std::vector<LayoutObj> layoutObjs;
  std::vector<PipeObj> pipes;

  nvrhi::BufferHandle ubo[2];         // канонический c-банк: VS -> b0, PS -> b1 (set 0)
  bool uboDirty[2];
  nvrhi::BindingLayoutHandle set0Layout;
  nvrhi::BindingSetHandle set0Set;

  // Реестр «имя семантики -> location» по всем созданным VS. NVRHI на Vulkan
  // назначает location по порядку атрибутов (vulkan-shader.cpp:
  // createInputLayout игнорирует шейдер), поэтому порядок обязан совпадать с
  // тем, что назначил DXC, — иначе атрибуты разъедутся молча.
  std::vector<std::pair<std::string, unsigned> > nameLoc;

  unsigned curVs, curPs, curLayout, curIb, curRt, curDs;
  unsigned vb[kMaxVertexStreams], vbStride[kMaxVertexStreams], vbOffset[kMaxVertexStreams];
  unsigned instSrc, instSlot, instStride, instOffset;
  bool recording;
  unsigned draws, pipesCreated, pipeHits, uploads;

  NvrhiBackendImpl()
    : hVulkan(0), gipa(0), instance(0), physical(0), device(0), queue(0), queueFamily(0),
      surface(0), window(0), swapchain(0), swapFormat(VK_FORMAT_UNDEFINED),
      swapSpace(VkColorSpaceKHR(0)), presentMode(VK_PRESENT_MODE_FIFO_KHR),
      width(0), height(0), acquired(0), imageIndex(0), currentSurface(0), presents(0),
      swapRecreates(0), surfaceRecreates(0),
      uboDirty{ false, false }, curVs(kNoRes), curPs(kNoRes), curLayout(kNoRes),
      curIb(kNoRes), curRt(kNoRes), curDs(kNoRes),
      instSrc(kNoRes), instSlot(kMaxVertexStreams), instStride(0), instOffset(0),
      recording(false), draws(0), pipesCreated(0), pipeHits(0), uploads(0)
  {
    deviceName[0] = 0;
    memset(stubCalls, 0, sizeof(stubCalls));
    for (int i = 0; i < kMaxVertexStreams; ++i)
    {
      vb[i] = kNoRes; vbStride[i] = 0; vbOffset[i] = 0;
    }
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
    systemLog(NLogg::LEVEL_WARNING) << "NvrhiBackend: " << what << " — "
                                    << (detail ? detail : "") << endl;
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
  dd.errorCB = &d.msgLog;
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

  // Канонический c-банк (§3.z п.2, шаг 12b): uniform-буферы равны тени
  // констант стадии — 256 регистров × 16 Б, смещение члена = 16*номер
  // регистра. Писатель tools/shader_build/cbank.py раскладывает
  // `cbuffer pwCVS`/`pwCPS` ровно так же через packoffset, поэтому таблица
  // «const-регистр -> байт» не нужна вовсе: загрузка = срез тени.
  // VS -> binding 0, PS -> binding 1 в одном наборе (set 0): у D3D9 у стадий
  // раздельные банки c, а слоты выбраны так, чтобы стадии не спорили за binding
  // (замер DXC: два блока с одним binding в одном прогоне = битый SPIR-V).
  for (int s = 0; s < csConstStageCount; ++s)
  {
    d.ubo[s] = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize((size_t)SHADER_CONST_REGISTERS * 16u)
                                             .setIsConstantBuffer(true)
                                             .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                             .setKeepInitialState(true)
                                             .setDebugName(s == csPixel ? "seam cbank ps" : "seam cbank vs"));
    if (!d.ubo[s]) { VkNote("Init: createBuffer(cbank)", false, 0); return false; }
  }
  {
    nvrhi::BindingLayoutDesc ld;
    ld.visibility = nvrhi::ShaderType::AllGraphics;
    ld.setRegisterSpaceAndDescriptorSet(0);
    ld.bindingOffsets = nvrhi::VulkanBindingOffsets()
      .setShaderResourceOffset(0).setSamplerOffset(0)
      .setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
    ld.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    ld.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(1));
    d.set0Layout = d.nvrhiDevice->createBindingLayout(ld);

    nvrhi::BindingSetDesc bsd;
    bsd.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(0, d.ubo[0]));
    bsd.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(1, d.ubo[1]));
    d.set0Set = d.set0Layout ? d.nvrhiDevice->createBindingSet(bsd, d.set0Layout)
                             : nvrhi::BindingSetHandle();
    if (!d.set0Layout || !d.set0Set)
    {
      VkNote("Init: binding layout/set (набор 0)", false, 0);
      return false;
    }
  }

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

bool NvrhiBackend::RecreateSurface()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (d.surface)
  {
    VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySurfaceKHR(d.instance, d.surface, 0);
    d.surface = 0;
  }
  if (!d.window)
  {
    VkNote("surface: нет окна", false, 0);
    return false;
  }
  VkWin32SurfaceCreateInfoKHR sci;
  memset(&sci, 0, sizeof(sci));
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  sci.hinstance = GetModuleHandleA(0);
  sci.hwnd = d.window;
  VkResult r = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateWin32SurfaceKHR(d.instance, &sci, 0, &d.surface);
  if (r != VK_SUCCESS)
  {
    VkNote("vkCreateWin32SurfaceKHR (пересоздание)", false, VkResultName(r));
    return false;
  }
  VkBool32 supported = VK_FALSE;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceSurfaceSupportKHR(d.physical, d.queueFamily,
                                                                    d.surface, &supported);
  if (!supported)
  {
    VkNote("present после пересоздания surface", false, 0);
    return false;
  }
  DebugTrace("NvrhiBackend: surface пересоздан на окне %p", (void*)d.window);
  return true;
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
    // Surface привязан к HWND. Клиент при смене разрешения пересоздаёт окно:
    // замер 2026-10-15 (стенд Wine+DXVK) — после входа в бой
    // vkGetPhysicalDeviceSurfaceCapabilitiesKHR отдаёт VK_ERROR_SURFACE_LOST_KHR
    // и у нас, и у DXVK, т.е. окно другое, а не только размер. Тогда заводим
    // surface заново на актуальном окне.
    if (!RecreateSurface() || !CreateSwapchain(0, 0))
    {
      VkNote("пересоздание swapchain'а", false, 0);
      return false;
    }
    ++d.surfaceRecreates;
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
  DebugTrace("VkStats%s: presents=%u recreates=%u (surface %u) unsupported=%u swap=%ux%u device=%s",
             pTag ? pTag : "", d.presents, d.swapRecreates, d.surfaceRecreates, m_unsupported,
             d.width, d.height, d.deviceName);
  DebugTrace("VkRes%s: буферы=%u шейдеры=%u раскладки=%u пайплайны=%u/%u (попаданий %u) вывод=%u загруз=%u",
             pTag ? pTag : "", (unsigned)d.buffers.size(), (unsigned)d.shaderObjs.size(),
             (unsigned)d.layoutObjs.size(), d.pipesCreated, (unsigned)d.pipes.size(), d.pipeHits,
             d.draws, d.uploads);
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

// Шаг 12b: BeginScene/EndScene у Vulkan-бэкенда — no-op: командный список
// открывается лениво (EnsureRecording), а «конец сцены» в D3D9 ничего не
// гарантирует сам по себе.
HRESULT NvrhiBackend::BeginScene() { return S_OK; }
HRESULT NvrhiBackend::EndScene() { return S_OK; }

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

  // 12b: если этим кадром уже записаны вызовы (Clear/Draw/writeBuffer), они
  // уходят ОТДЕЛЬНЫМ списком до очистки бэк-буфера: в 12a заливка и была всем
  // кадром, с реальными проходами порядок «сначала сцена, потом бэк-буфер» не
  // меняется, но чистить бэк-буфер поверх нарисованного нельзя.
  FlushRecording();

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
  return S_OK;
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
  NvrhiBackendImpl& d = *m_pImpl;
  if (d.curRt == kNoRes || d.curRt >= d.surfaces.size()) return D3DERR_INVALIDCALL;
  if (!EnsureRecording()) return E_FAIL;
  if (clearFlags & bcTarget)
  {
    const unsigned a = (colorRGBA >> 24) & 0xffu, r = (colorRGBA >> 16) & 0xffu,
                   g = (colorRGBA >> 8) & 0xffu, b = colorRGBA & 0xffu;
    // цвет логический (r,g,b,a): VK_FORMAT_B8G8R8A8_UNORM сам раскладывает
    // каналы, байты в памяти получаются B,G,R,A — как у D3DFMT_A8R8G8B8
    d.commandList->clearTextureFloat(d.surfaces[d.curRt].tex, nvrhi::TextureSubresourceSet(),
                                     nvrhi::Color(r / 255.f, g / 255.f, b / 255.f, a / 255.f));
  }
  if ((clearFlags & (bcZBuffer | bcStencil)) && d.curDs != kNoRes && d.curDs < d.surfaces.size())
  {
    d.commandList->clearDepthStencilTexture(d.surfaces[d.curDs].tex, nvrhi::TextureSubresourceSet(),
                                            (clearFlags & bcZBuffer) != 0, z,
                                            (clearFlags & bcStencil) != 0, stencil);
  }
  return S_OK;
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
  m_pImpl->uboDirty[csVertex] = true;   // банк = образ тени, байт = 16*регистр
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
// Биндинги и вывод (шаг 12b).
//
// Командный список один на бэкенд: EnsureRecording открывает его лениво,
// FlushRecording закрывает и исполняет. D3D9-модель «каждый вызов сразу в
// контекст» не воспроизводится — очередь решает бэкенд, а точку ставит Present
// или там, где вызывающему нужен результат (ReadSurface, тест 12b).

bool NvrhiBackend::EnsureRecording()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.commandList) return false;
  if (!d.recording) { d.commandList->open(); d.recording = true; }
  return true;
}

void NvrhiBackend::FlushRecording()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.recording || !d.nvrhiDevice) return;
  d.commandList->close();
  d.nvrhiDevice->executeCommandList(d.commandList);
  d.recording = false;
}

HRESULT NvrhiBackend::SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (slot >= kMaxVertexStreams) return D3DERR_INVALIDCALL;
  const unsigned idx = Hnd2Idx(vertexBuffer.p);
  if (idx != kNoRes && idx >= d.buffers.size()) return D3DERR_INVALIDCALL;
  d.vb[slot] = idx;
  d.vbOffset[slot] = byteOffset;
  d.vbStride[slot] = (idx == kNoRes) ? 0 : stride;
  return S_OK;
}

HRESULT NvrhiBackend::SetIndices(HBuffer indexBuffer)
{
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(indexBuffer.p);
  if (idx != kNoRes && idx >= d.buffers.size()) return D3DERR_INVALIDCALL;
  d.curIb = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetInputLayout(HInputLayout layout)
{
  const unsigned idx = Hnd2Idx(layout.p);
  if (idx != kNoRes && idx >= m_pImpl->layoutObjs.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curLayout = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetVertexShader(HShader shader)
{
  const unsigned idx = Hnd2Idx(shader.p);
  if (idx != kNoRes && idx >= m_pImpl->shaderObjs.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curVs = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetPixelShader(HShader shader)
{
  const unsigned idx = Hnd2Idx(shader.p);
  if (idx != kNoRes && idx >= m_pImpl->shaderObjs.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curPs = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetRenderTarget(unsigned index, HSurface surface)
{
  (void)index;
  const unsigned idx = Hnd2Idx(surface.p);
  if (idx != kNoRes && idx >= m_pImpl->surfaces.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curRt = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetDepthStencil(HSurface surface)
{
  const unsigned idx = Hnd2Idx(surface.p);
  if (idx != kNoRes && idx >= m_pImpl->surfaces.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curDs = idx;
  return S_OK;
}

HRESULT NvrhiBackend::GetDepthStencil(HSurface* pOut)
{
  if (!pOut) return D3DERR_INVALIDCALL;
  pOut->p = Idx2Hnd(m_pImpl->curDs);
  return S_OK;
}

HRESULT NvrhiBackend::SetInstanceSource(HBuffer perInstanceData, unsigned stride, unsigned offset)
{
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(perInstanceData.p);
  if (idx == kNoRes) { d.instSrc = kNoRes; d.instStride = 0; d.instOffset = 0; return S_OK; }
  if (idx >= d.buffers.size()) return D3DERR_INVALIDCALL;
  d.instSrc = idx;
  d.instStride = stride;
  d.instOffset = offset;
  return S_OK;
}

bool NvrhiBackend::IsStreamBufferBound(const void* pVertexBuffer) const
{
  const NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(pVertexBuffer);
  if (idx == kNoRes) return false;
  for (unsigned i = 0; i < kMaxVertexStreams; ++i)
    if (d.vb[i] == idx) return true;
  return d.instSrc == idx;
}

void NvrhiBackend::InvalidateBoundState()
{
  NvrhiBackendImpl& d = *m_pImpl;
  m_instanceCount = 0;
  d.curVs = d.curPs = d.curLayout = d.curIb = kNoRes;
  for (unsigned i = 0; i < kMaxVertexStreams; ++i)
  {
    d.vb[i] = kNoRes; d.vbStride[i] = 0; d.vbOffset[i] = 0;
  }
  d.instSrc = kNoRes; d.instStride = 0; d.instOffset = 0;
}

//////////////////////////////////////////////////////////////////////////
// Пайплайн. Ключ — (vs, ps, layout, rt, ds, примитив): состояние растеризации
// сейчас фиксировано (cull none, front CCW — намотка NVRHI с отрицательной
// высотой вьюпорта), поэтому ключ шага 8 (хэш состояний/сэмплов/трафарета) в
// него ещё не входит — это шаг 13, там же появятся текстуры/сэмплеры/SSBO.

// Физический слот потока: kInstanceStream — отдельный слот за обычными
// (так же, как kInstanceSlot = 1 у D3D9Backend, только здесь слоты 0..7 заняты
// клиентскими потоками).
static unsigned SlotOfStream(NvrhiBackendImpl& d, unsigned logical)
{
  return (logical == kInstanceStream) ? d.instSlot : logical;
}

// Невиртуальные помощники — свободные функции (в nvrhibackend.h типы nvrhi
// просачиваться не должны: пофайловый /std:c++17 только у nvrhibackend.cpp).
bool BuildInputLayout(NvrhiBackendImpl& d, unsigned layoutIdx)
{
  if (layoutIdx == kNoRes || layoutIdx >= d.layoutObjs.size()) return false;
  NvrhiBackendImpl::LayoutObj& lo = d.layoutObjs[layoutIdx];
  if (lo.il) return true;
  if (lo.elems.empty()) return false;

  struct Attr
  {
    unsigned loc;
    nvrhi::VertexAttributeDesc vd;
  };
  std::vector<Attr> attrs;
  unsigned nextFree = 0;
  for (size_t i = 0; i < lo.elems.size(); ++i)
  {
    const VertexElementDescriptor& e = lo.elems[i];
    char name[32];
    SemanticName((EVertexElementUsage)e.usage, e.usageIndex, name, sizeof(name));
    const nvrhi::Format fmt = VtxFormat((EVertexElementType)e.type);
    if (fmt == nvrhi::Format::UNKNOWN)
    {
      char det[96];
      snprintf(det, sizeof(det), "%s (тип %d)", name, (int)e.type);
      VkNote("CreateInputLayout: формат атрибута не отображается", false, det);
      return false;
    }
    // location берём из SPIR-V (что назначил DXC), а не из порядка объявления:
    // NVRHI назначает location по порядку массива, и при расхождении атрибуты
    // разъезжаются молча (замер vulkan-shader.cpp: createInputLayout игнорирует
    // переданный шейдер).
    unsigned loc = kNoRes;
    for (size_t k = 0; k < d.nameLoc.size(); ++k)
      if (d.nameLoc[k].first == name) { loc = d.nameLoc[k].second; break; }
    if (loc == kNoRes) { loc = nextFree; }
    if (loc + 1 > nextFree) nextFree = loc + 1;

    const unsigned logical = e.stream;
    const unsigned slot = SlotOfStream(d, logical);
    const unsigned stride = (logical == kInstanceStream) ? d.instStride : d.vbStride[slot];
    if (!stride)
    {
      char det[128];
      snprintf(det, sizeof(det), "%s: поток %u без шага (SetStreamSource раньше CreateInputLayout?)",
               name, logical);
      VkNote("CreateInputLayout", false, det);
      return false;
    }
    Attr a;
    a.loc = loc;
    a.vd.setName(name).setFormat(fmt).setOffset(e.offset).setElementStride(stride)
       .setBufferIndex(slot).setIsInstanced(logical == kInstanceStream);
    attrs.push_back(a);
  }
  std::sort(attrs.begin(), attrs.end(), [](const Attr& a, const Attr& b) { return a.loc < b.loc; });

  std::vector<nvrhi::VertexAttributeDesc> vads;
  for (size_t i = 0; i < attrs.size(); ++i) vads.push_back(attrs[i].vd);
  lo.il = d.nvrhiDevice->createInputLayout(vads.data(), (uint32_t)vads.size(),
                                           d.curVs != kNoRes ? d.shaderObjs[d.curVs].sh : nullptr);
  if (!lo.il)
  {
    VkNote("CreateInputLayout: createInputLayout", false, 0);
    return false;
  }
  return true;
}

nvrhi::GraphicsPipelineHandle FindOrCreatePipeline(NvrhiBackendImpl& d, ERenderPrimitiveType type,
                                                   nvrhi::IFramebuffer** ppFb)
{
  for (size_t i = 0; i < d.pipes.size(); ++i)
  {
    const NvrhiBackendImpl::PipeObj& p = d.pipes[i];
    if (p.vs == d.curVs && p.ps == d.curPs && p.layout == d.curLayout &&
        p.rt == d.curRt && p.ds == d.curDs && p.prim == (unsigned)type)
    {
      ++d.pipeHits;
      if (ppFb) *ppFb = p.fb;   // RefCountPtr::operator& возвращает T**, поэтому
                                // наружу отдаётся сырой указатель (владеет PipeObj)
      return p.pipe;
    }
  }
  if (d.curVs == kNoRes || d.curPs == kNoRes || d.curLayout == kNoRes || d.curRt == kNoRes)
    return nvrhi::GraphicsPipelineHandle();

  nvrhi::InputLayoutHandle il;
  if (!BuildInputLayout(d, d.curLayout)) return nvrhi::GraphicsPipelineHandle();
  // раскладка живёт в LayoutObj (её кэшируем), в пайплайн её обязан получить
  // именно оттуда: у Vulkan-бэкенда `if (inputLayout)` (vulkan-graphics.cpp:300)
  // и при нулевом указателе пайплайн создаётся БЕСШТРИХОВЫМ — вершинные
  // атрибуты читаются нулями, геометрия вырождается молча (замер 2026-10-15:
  // 0 пикселей при пайплайне 1/1 и выводе 1)
  il = d.layoutObjs[d.curLayout].il;
  if (!il) return nvrhi::GraphicsPipelineHandle();

  if (d.curRt >= d.surfaces.size()) return nvrhi::GraphicsPipelineHandle();
  nvrhi::FramebufferDesc fbd;
  fbd.addColorAttachment(d.surfaces[d.curRt].tex);
  if (d.curDs != kNoRes && d.curDs < d.surfaces.size())
    fbd.setDepthAttachment(d.surfaces[d.curDs].tex);
  nvrhi::FramebufferHandle fb = d.nvrhiDevice->createFramebuffer(fbd);
  if (!fb) { VkNote("Пайплайн: createFramebuffer", false, 0); return nvrhi::GraphicsPipelineHandle(); }

  nvrhi::GraphicsPipelineDesc pd;
  pd.setPrimType(PrimType(type)).setInputLayout(il)
    .setVertexShader(d.shaderObjs[d.curVs].sh).setPixelShader(d.shaderObjs[d.curPs].sh);
  // Намотка: у NVRHI вьюпорт с отрицательной высотой, без этого флага геометрия
  // вырезается (замер спайка b0cull: 0 против 3072 пикселей).
  pd.renderState.rasterState.setCullNone().setFrontCounterClockwise(true);
  if (d.set0Layout) pd.addBindingLayout(d.set0Layout);

  nvrhi::GraphicsPipelineHandle pipe = d.nvrhiDevice->createGraphicsPipeline(pd, fb->getFramebufferInfo());
  if (!pipe)
  {
    char det[160];
    snprintf(det, sizeof(det), "vs=%s ps=%s", d.shaderObjs[d.curVs].entry.c_str(),
             d.shaderObjs[d.curPs].entry.c_str());
    VkNote("Пайплайн: createGraphicsPipeline", false, det);
    return nvrhi::GraphicsPipelineHandle();
  }
  NvrhiBackendImpl::PipeObj po;
  po.pipe = pipe; po.fb = fb;
  po.vs = d.curVs; po.ps = d.curPs; po.layout = d.curLayout;
  po.rt = d.curRt; po.ds = d.curDs; po.prim = (unsigned)type;
  d.pipes.push_back(po);
  ++d.pipesCreated;
  if (ppFb) *ppFb = fb;
  return pipe;
}

HRESULT NvrhiBackend::DrawImpl(ERenderPrimitiveType type, bool indexed, unsigned startIndexLocation,
                               unsigned startVertexLocation, unsigned primitiveCount)
{
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned vtx = PrimVertexCount(type, primitiveCount);
  if (!vtx)
  {
    VkNote("Вывод: примитив вне Vulkan (TRIANGLEFAN)", false, 0);
    return E_FAIL;
  }
  if (d.curVs == kNoRes || d.curPs == kNoRes || d.curRt == kNoRes)
    return E_FAIL;
  if (!EnsureRecording()) return E_FAIL;

  // Канонический c-банк (§3.z, шаг 13.0): тень констант — это и есть образ
  // UBO, байт = 16*регистр. Пока грузится весь банк (4 КБ на стадию);
  // диапазон из ConstBlock::TakeDirty() — шаг 13.
  for (int s = 0; s < csConstStageCount; ++s)
  {
    if (d.uboDirty[s] && d.ubo[s])
    {
      d.commandList->writeBuffer(d.ubo[s], m_constShadow[s],
                                 (size_t)SHADER_CONST_REGISTERS * 16u);
      d.uboDirty[s] = false;
      ++d.uploads;
    }
  }

  nvrhi::IFramebuffer* fbRaw = 0;
  nvrhi::GraphicsPipelineHandle pipe = FindOrCreatePipeline(d, type, &fbRaw);
  if (!pipe) return E_FAIL;

  nvrhi::GraphicsState gs;
  gs.setPipeline(pipe).setFramebuffer(fbRaw);
  for (unsigned i = 0; i < kMaxVertexStreams; ++i)
  {
    if (d.vb[i] != kNoRes && d.vb[i] < d.buffers.size())
      gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(d.buffers[d.vb[i]].buf)
                         .setSlot(i).setOffset(d.vbOffset[i]));
  }
  if (d.instSrc != kNoRes && d.instSrc < d.buffers.size())
    gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(d.buffers[d.instSrc].buf)
                       .setSlot(d.instSlot).setOffset(d.instOffset));
  if (d.curIb != kNoRes && d.curIb < d.buffers.size())
    gs.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(d.buffers[d.curIb].buf)
                      .setFormat(d.buffers[d.curIb].index32 ? nvrhi::Format::R32_UINT
                                                            : nvrhi::Format::R16_UINT));
  if (d.set0Set) gs.addBindingSet(d.set0Set);

  nvrhi::ViewportState vps;
  nvrhi::Viewport vport((float)m_viewport.x, (float)(m_viewport.x + m_viewport.width),
                        (float)m_viewport.y, (float)(m_viewport.y + m_viewport.height),
                        m_viewport.minZ, m_viewport.maxZ);
  if (m_scissorValid)
    vps.addViewport(vport).addScissorRect(nvrhi::Rect((int)m_scissor.left, (int)m_scissor.right,
                                                      (int)m_scissor.top, (int)m_scissor.bottom));
  else
    vps.addViewportAndScissorRect(vport);
  gs.setViewport(vps);
  d.commandList->setGraphicsState(gs);

  const unsigned inst = m_instanceCount ? m_instanceCount : 1u;
  if (indexed)
  {
    // у NVRHI в drawIndexed поле vertexCount — это ЧИСЛО ИНДЕКСОВ (замер
    // vulkan-graphics.cpp:695 — cmdBuf.drawIndexed(args.vertexCount, ...))
    d.commandList->drawIndexed(nvrhi::DrawArguments()
                               .setVertexCount(vtx).setStartIndexLocation(startIndexLocation)
                               .setStartVertexLocation(startVertexLocation).setInstanceCount(inst));
  }
  else
  {
    d.commandList->draw(nvrhi::DrawArguments().setVertexCount(vtx)
                        .setStartVertexLocation(startVertexLocation).setInstanceCount(inst));
  }
  ++d.draws;
  return S_OK;
}

HRESULT NvrhiBackend::DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                                  unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount)
{
  (void)minIndex; (void)indexedVertexCount;
  return DrawImpl(type, true, startIndex, baseVertexIndex, primitiveCount);
}

HRESULT NvrhiBackend::Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount)
{
  return DrawImpl(type, false, 0, startVertex, primitiveCount);
}

HRESULT NvrhiBackend::SetInstanceCount(unsigned count)
{
  m_instanceCount = count;
  return S_OK;
}

// Ещё не закрыто (шаг 13): текстуры в наборе 2/3 и вывод из пользовательских
// буферов (UP-пути в Vulkan требуют staging-копии на каждый вызов).
HRESULT NvrhiBackend::SetTexture(unsigned samplerIndex, HTexture texture)
{ (void)samplerIndex; (void)texture; return Unsupported(st_SetTexture); }

HRESULT NvrhiBackend::DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                    unsigned vertexCount, const void* pIndexData, bool index16,
                                    const void* pVertexData, unsigned vertexStride)
{ (void)type; (void)minIndexValue; (void)indexCount; (void)vertexCount; (void)pIndexData; (void)index16;
  (void)pVertexData; (void)vertexStride; return Unsupported(st_DrawIndexedUP); }

HRESULT NvrhiBackend::DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                             const void* pVertexData, unsigned vertexStride)
{ (void)type; (void)primitiveCount; (void)pVertexData; (void)vertexStride; return Unsupported(st_DrawUP); }

//////////////////////////////////////////////////////////////////////////
// Создание ресурсов (шаг 12b).
//
// Точку входа и имена/номера входов VS берём из самого SPIR-V: seam их не
// передаёт, а DXC пишет имя HLSL-функции (не "main") и назначает location сам
// (замер спайка: WaterDummy — 1 вход location 0, BasicMaterial — 9 входов).
// Кодировка SPIR-V здесь читается напрямую (вендорский NVRHI рефлексии SPIR-V
// не имеет) — тот же мини-сканер, что в tools/nvrhi_probe.

// OpEntryPoint = 15, OpName = 5, OpDecorate = 71, OpVariable = 59;
// модель исполнения Vertex = 0, Fragment = 4; декорация Location = 30;
// класс хранения Input = 1.
static bool SpvScan(const void* pCode, unsigned codeSize, bool fragment,
                    std::string* pEntry,
                    std::vector<std::pair<std::string, unsigned> >* pInputs)
{
  const unsigned* w = static_cast<const unsigned*>(pCode);
  const unsigned n = codeSize / 4u;
  if (n < 5u || w[0] != 0x07230203u) return false;

  std::vector<unsigned> inputIds;
  std::map<unsigned, std::string> names;
  std::map<unsigned, unsigned> locs;
  // заголовок SPIR-V — 5 слов (magic, version, generator, bound, schema);
  // с 4-го слова начинается schema (обычно 0) и разбор обрывался на wc==0
  for (unsigned i = 5u; i < n; )
  {
    const unsigned op = w[i], wc = op >> 16;
    if (wc == 0u || i + wc > n) return false;
    const unsigned code = op & 0xffffu;
    if (code == 15u && wc >= 4u)                 // OpEntryPoint
    {
      const unsigned model = w[i + 1];
      if (model == (fragment ? 4u : 0u))
      {
        const char* s = reinterpret_cast<const char*>(w + i + 3);
        const unsigned avail = (n - (i + 3u)) * 4u;
        unsigned len = 0;
        while (len < avail && s[len]) ++len;
        pEntry->assign(s, len);
      }
    }
    else if (code == 59u && wc >= 4u)            // OpVariable: тип, id, класс
    {
      if (w[i + 3] == 1u) inputIds.push_back(w[i + 2]);
    }
    else if (code == 5u && wc >= 3u)             // OpName
    {
      const char* s = reinterpret_cast<const char*>(w + i + 2);
      const unsigned avail = (n - (i + 2u)) * 4u;
      unsigned len = 0;
      while (len < avail && s[len]) ++len;
      names[w[i + 1]] = std::string(s, len);
    }
    else if (code == 71u && wc >= 3u)            // OpDecorate
    {
      if (w[i + 2] == 30u && wc >= 4u) locs[w[i + 1]] = w[i + 3];
    }
    i += wc;
  }
  for (size_t k = 0; k < inputIds.size(); ++k)
  {
    std::map<unsigned, std::string>::const_iterator itN = names.find(inputIds[k]);
    std::map<unsigned, unsigned>::const_iterator itL = locs.find(inputIds[k]);
    if (itN == names.end() || itL == locs.end()) continue;
    std::string nm = itN->second;
    const std::string pfx = "in.var.";
    if (nm.compare(0, pfx.size(), pfx) == 0) nm.erase(0, pfx.size());
    else continue;
    pInputs->push_back(std::make_pair(nm, itL->second));
  }
  return !pEntry->empty();
}

HRESULT NvrhiBackend::CreateShaderImpl(const void* pCode, unsigned codeSize, bool fragment, HShader* pOut)
{
  if (!pCode || !codeSize || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;

  std::string entry;
  std::vector<std::pair<std::string, unsigned> > inputs;
  if (!SpvScan(pCode, codeSize, fragment, &entry, &inputs))
  {
    VkNote("CreateShader: SPIR-V не разобран (нет точки входа)", false, 0);
    return E_FAIL;
  }
  nvrhi::ShaderHandle sh = d.nvrhiDevice->createShader(
      nvrhi::ShaderDesc().setShaderType(fragment ? nvrhi::ShaderType::Pixel : nvrhi::ShaderType::Vertex)
                         .setEntryName(entry),
      pCode, codeSize);
  if (!sh)
  {
    char det[160];
    snprintf(det, sizeof(det), "точка входа %s, %u Б", entry.c_str(), codeSize);
    VkNote("CreateShader: createShader", false, det);
    return E_FAIL;
  }
  NvrhiBackendImpl::ShaderObj so;
  so.sh = sh;
  so.entry = entry;
  so.inputs = inputs;
  d.shaderObjs.push_back(so);
  pOut->p = Idx2Hnd((unsigned)d.shaderObjs.size() - 1u);

  // имена входов — в общий реестр «семантика -> location»: по ним потом
  // сопоставляется раскладка атрибутов
  for (size_t k = 0; k < inputs.size(); ++k)
  {
    bool found = false;
    for (size_t e = 0; e < d.nameLoc.size() && !found; ++e)
      if (d.nameLoc[e].first == inputs[k].first) found = true;
    if (!found) d.nameLoc.push_back(inputs[k]);
  }
  if (d.shaderObjs.size() <= 4u || (d.shaderObjs.size() % 50u) == 0u)
  {
    char det[160];
    snprintf(det, sizeof(det), "входов %u, всего %u",
             (unsigned)inputs.size(), (unsigned)d.shaderObjs.size());
    VkNote((std::string("CreateShader: ") + entry).c_str(), true, det);
  }
  return S_OK;
}

HRESULT NvrhiBackend::CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut)
{
  return CreateShaderImpl(pShaderCode, codeSize, false, pOut);
}

HRESULT NvrhiBackend::CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut)
{
  return CreateShaderImpl(pShaderCode, codeSize, true, pOut);
}

HRESULT NvrhiBackend::CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut)
{
  if (!pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  NvrhiBackendImpl::LayoutObj lo;
  const unsigned n = descr.GetVertexElementsCount();
  for (unsigned i = 0; i < n; ++i) lo.elems.push_back(descr.GetVertexElement(i));
  d.layoutObjs.push_back(lo);
  pOut->p = Idx2Hnd((unsigned)d.layoutObjs.size() - 1u);
  return S_OK;
}

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
{
  if (!width || !height || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;
  (void)pool;

  nvrhi::TextureDesc td;
  td.setDimension(nvrhi::TextureDimension::Texture2D).setWidth(width).setHeight(height);
  bool depth = false;
  switch (format)
  {
    case bfD24S8:  td.setFormat(nvrhi::Format::D24S8); depth = true; break;
    // у NVRHI нет формата B8G8R8A8 — BGRA8_UNORM даёт в памяти те же байты
    // B,G,R,A, что D3D9 A8R8G8B8/X8R8G8B8 (замер спайка: RT BGRA8_UNORM +
    // readback пиксель-в-пиксель); X8 у нас читается как A, потерь в коде нет
    case bfA8R8G8B8:
    case bfX8R8G8B8: td.setFormat(nvrhi::Format::BGRA8_UNORM); break;
    default:
    {
      char det[96];
      snprintf(det, sizeof(det), "формат %d не переведён", (int)format);
      VkNote("CreateSurface", false, det);
      return E_FAIL;
    }
  }
  // глубина у NVRHI определяется форматом (setIsDepthStencil в этой версии
  // нет); без initialState+keepInitialState трекер состояний рвёт командный
  // список: «Unknown prior state of texture draw ds» (замер спайка)
  if (depth)
    td.setIsRenderTarget(true).setInitialState(nvrhi::ResourceStates::DepthWrite)
      .setKeepInitialState(true);
  else
    td.setIsRenderTarget(true).setInitialState(nvrhi::ResourceStates::RenderTarget)
      .setKeepInitialState(true);

  nvrhi::TextureHandle tex = d.nvrhiDevice->createTexture(td);
  if (!tex)
  {
    char det[96];
    snprintf(det, sizeof(det), "%ux%u kind=%d", width, height, (int)kind);
    VkNote("CreateSurface: createTexture", false, det);
    return E_FAIL;
  }
  SurfaceObj so;
  so.tex = tex;
  so.backBuffer = false;
  d.surfaces.push_back(so);
  pOut->p = Idx2Hnd((unsigned)d.surfaces.size() - 1u);
  return S_OK;
}

HRESULT NvrhiBackend::UpdateTexture(HTexture srcTexture, HTexture dstTexture)
{ (void)srcTexture; (void)dstTexture; return Unsupported(st_UpdateTexture); }

HRESULT NvrhiBackend::CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut)
{
  if (!size || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;
  (void)pool;
  NvrhiBackendImpl::BufferObj bo;
  bo.cpu.resize(size);
  bo.buf = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                         .setByteSize(size)
                                         .setIsVertexBuffer(true)
                                         .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                         .setKeepInitialState(true)
                                         .setDebugName("seam vb"));
  if (!bo.buf) { VkNote("CreateVertexBuffer", false, 0); return E_FAIL; }
  d.buffers.push_back(bo);
  pOut->p = Idx2Hnd((unsigned)d.buffers.size() - 1u);
  return S_OK;
}

HRESULT NvrhiBackend::CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut)
{
  if (!size || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;
  (void)pool;
  NvrhiBackendImpl::BufferObj bo;
  bo.cpu.resize(size);
  bo.index32 = index32;
  bo.buf = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                         .setByteSize(size)
                                         .setIsVertexBuffer(true)
                                         .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                         .setKeepInitialState(true)
                                         .setDebugName("seam ib"));
  if (!bo.buf) { VkNote("CreateIndexBuffer", false, 0); return E_FAIL; }
  d.buffers.push_back(bo);
  pOut->p = Idx2Hnd((unsigned)d.buffers.size() - 1u);
  return S_OK;
}

// Блокировка отдаёт CPU-зеркало буфера; в Vulkan загрузка — на Unlock
// (целиком: для DIP-проходов это один буфер на материал, кольцевой upload
// появится вместе с боевыми проходами).
HRESULT NvrhiBackend::LockBuffer(HBuffer h, unsigned offset, unsigned size, void** ppData)
{
  if (!ppData) return D3DERR_INVALIDCALL;
  const unsigned idx = Hnd2Idx(h.p);
  if (idx == kNoRes || idx >= m_pImpl->buffers.size()) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::BufferObj& bo = m_pImpl->buffers[idx];
  if (offset + size > bo.cpu.size()) return D3DERR_INVALIDCALL;
  *ppData = &bo.cpu[offset];
  bo.locked = true;
  return S_OK;
}

HRESULT NvrhiBackend::LockVertexBuffer(HBuffer vb, unsigned offset, unsigned size,
                                       ERenderLockType mode, void** ppData)
{
  (void)mode;
  return LockBuffer(vb, offset, size, ppData);
}

HRESULT NvrhiBackend::LockIndexBuffer(HBuffer ib, unsigned offset, unsigned size,
                                      ERenderLockType mode, void** ppData)
{
  (void)mode;
  return LockBuffer(ib, offset, size, ppData);
}

HRESULT NvrhiBackend::UnlockBuffer(HBuffer h)
{
  const unsigned idx = Hnd2Idx(h.p);
  if (idx == kNoRes || idx >= m_pImpl->buffers.size()) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::BufferObj& bo = m_pImpl->buffers[idx];
  if (!bo.locked) return S_OK;
  bo.locked = false;
  if (!bo.buf || !m_pImpl->nvrhiDevice) return E_FAIL;
  if (!EnsureRecording()) return E_FAIL;
  m_pImpl->commandList->writeBuffer(bo.buf, bo.cpu.data(), bo.cpu.size());
  ++m_pImpl->uploads;
  return S_OK;
}

HRESULT NvrhiBackend::UnlockVertexBuffer(HBuffer vb) { return UnlockBuffer(vb); }
HRESULT NvrhiBackend::UnlockIndexBuffer(HBuffer ib) { return UnlockBuffer(ib); }

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
{
  if (!pDstBuffer) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(srcSurface.p);
  if (idx == kNoRes || idx >= d.surfaces.size() || !d.surfaces[idx].tex) return D3DERR_INVALIDCALL;
  if (!d.nvrhiDevice) return E_FAIL;
  SurfaceObj& so = d.surfaces[idx];
  const nvrhi::TextureDesc td = so.tex->getDesc();

  if (!so.staging)
  {
    so.staging = d.nvrhiDevice->createStagingTexture(
        nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
                            .setWidth(td.width).setHeight(td.height).setFormat(td.format)
                            .setInitialState(nvrhi::ResourceStates::CopyDest)
                            .setKeepInitialState(true)
                            .setDebugName("seam readback"),
        nvrhi::CpuAccessMode::Read);
    if (!so.staging) { VkNote("ReadSurface: createStagingTexture", false, 0); return E_FAIL; }
  }

  long x0 = 0, y0 = 0, x1 = (long)td.width, y1 = (long)td.height;
  if (pCropRect)
  {
    x0 = pCropRect->left; y0 = pCropRect->top;
    x1 = pCropRect->right; y1 = pCropRect->bottom;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > (long)td.width) x1 = (long)td.width;
    if (y1 > (long)td.height) y1 = (long)td.height;
    if (x1 <= x0 || y1 <= y0) return D3DERR_INVALIDCALL;
  }

  if (!EnsureRecording()) return E_FAIL;
  d.commandList->copyTexture(so.staging, nvrhi::TextureSlice(), so.tex, nvrhi::TextureSlice());
  FlushRecording();
  d.nvrhiDevice->waitForIdle();

  size_t srcPitch = 0;
  void* px = d.nvrhiDevice->mapStagingTexture(so.staging, nvrhi::TextureSlice(),
                                              nvrhi::CpuAccessMode::Read, &srcPitch);
  if (!px) { VkNote("ReadSurface: mapStagingTexture", false, 0); return E_FAIL; }
  const size_t rowBytes = (size_t)(x1 - x0) * 4u;
  unsigned row = 0;
  for (long y = y0; y < y1; ++y, ++row)
    memcpy(static_cast<unsigned char*>(pDstBuffer) + (size_t)row * dstRowPitch,
           static_cast<unsigned char*>(px) + (size_t)y * srcPitch + (size_t)x0 * 4u, rowBytes);
  d.nvrhiDevice->unmapStagingTexture(so.staging);

  // формат поверхности — BGRA (см. CreateSurface/swapchain), т.е. байты
  // B,G,R,A — как у D3D9 A8R8G8B8; коды перестановку не делают (п. 12 §2.15)
  if (pOutLayout) *pOutLayout = plBGRA;
  return S_OK;
}

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

//////////////////////////////////////////////////////////////////////////
// Тест шага 12b (env PW_VK_DIP): один настоящий DIP через seam.
//
// Берётся РЕАЛЬНЫЙ контейнер .shd v2 из пака клиента (тот же путь и тот же
// читатель, что у Materials::MultiShader::OpenFile), из него достаётся
// SPIR-V-цель, создаются шейдеры, раскладка, offscreen RT+DS, рисуется
// треугольник и пиксели читаются обратно. Проверяется весь механизм, а не
// отдельный спайк: pile -> SPIR-V -> createShader -> input layout (location
// берётся из SPIR-V) -> пайплайн -> draw -> readback.
//
// WaterDummyMaterial выбран потому, что его PS пишет в R интерполированную
// глубину: градиент виден без настоящих текстур. Банк здесь — тень констант
// клиента (не авто-патч спайка), поэтому матрицы выставляем сами: WORLD (c0)
// и VIEWPROJECTION (c8) по GlobalMasks.h = единичные, т.е. вершины идут в
// клипово пространство как есть.
bool NvrhiBackend::RunSeamDipTest()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice || !d.commandList) return false;

  const unsigned W = 256, H = 256;
  char path[256];
  snprintf(path, sizeof(path), "%s.shd", "Shaders/UberShaders/WaterDummyMaterial");
  CObj<Stream> f = RootFileSystem::OpenFile(path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
  if (!f || !f->IsOk()) { VkNote("DIP: контейнер не открыт", false, path); return false; }
  const unsigned char* base = (const unsigned char*)f->GetBuffer();
  const unsigned size = (unsigned)f->GetSize();

  unsigned hdrOffset = 0, version = 0, v1Size = 0;
  nstl::vector<Materials::Shd2Section> secs;
  if (!Materials::Shd2FindTrailer(base, size, hdrOffset, version) ||
      !Materials::Shd2ParseHeader(base, size, hdrOffset, v1Size, secs))
  {
    VkNote("DIP: заголовок v2 не разобран", false, path);
    return false;
  }
  const Materials::Shd2Section* spv = Materials::Shd2FindSection(secs, (unsigned)Materials::stSpirv);
  if (!spv) { VkNote("DIP: секция SPIR-V не найдена", false, path); return false; }

  const unsigned index = Materials::Shd2U32(base + spv->offset + 8);  // первая запись
  unsigned vsOff = 0, vsSize = 0, psOff = 0, psSize = 0;
  if (!Materials::Shd2Lookup(base, size, *spv, index, vsOff, vsSize, psOff, psSize) ||
      !vsSize || !psSize)
  {
    VkNote("DIP: первая запись SPIR-V-секции не читается", false, path);
    return false;
  }

  static const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  SetVertexShaderConstants(0, 4, kIdentity);      // WORLD  (GlobalMasks.h c0)
  SetVertexShaderConstants(8, 4, kIdentity);      // VIEWPROJECTION (c8)

  HShader vs, ps;
  if (FAILED(CreateVertexShader(base + vsOff, vsSize, &vs))) { VkNote("DIP: VS не создан", false, path); return false; }
  if (FAILED(CreatePixelShader(base + psOff, psSize, &ps))) { VkNote("DIP: PS не создан", false, path); return false; }
  SetVertexShader(vs);
  SetPixelShader(ps);

  HSurface rt, dsf;
  if (FAILED(CreateSurface(ssRenderTarget, W, H, bfA8R8G8B8, BACKEND_POOL_DEFAULT, &rt)))
  { VkNote("DIP: RT не создан", false, 0); return false; }
  if (FAILED(CreateSurface(ssDepthStencil, W, H, bfD24S8, BACKEND_POOL_DEFAULT, &dsf)))
  { VkNote("DIP: DS не создан", false, 0); return false; }
  if (FAILED(SetRenderTarget(0, rt))) { VkNote("DIP: SetRenderTarget", false, 0); return false; }
  if (FAILED(SetDepthStencil(dsf))) { VkNote("DIP: SetDepthStencil", false, 0); return false; }

  BackendViewport vp;
  vp.x = 0; vp.y = 0; vp.width = W; vp.height = H; vp.minZ = 0.f; vp.maxZ = 1.f;
  SetViewport(vp);
  Clear(bcTarget | bcZBuffer, 0xFF000000u, 1.f, 0);

  // треугольник занимает левую половину кадра; Z по вершинам 0.5 — её и пишет
  // PS в красный канал (R=127). Глубина 0.5 заведомо проходит depthFunc=Less
  // после очистки в 1.0; с z=1.0 у дальней вершины часть кадра отсекалась бы,
  // и тест терял бы часть площади молча
  static const float kTri[] = {
    -1.f, -1.f, 0.5f,
      0.f, -1.f, 0.5f,
     -1.f,  1.f, 0.5f,
  };
  HBuffer vb;
  if (FAILED(CreateVertexBuffer((unsigned)sizeof(kTri), BACKEND_POOL_DYNAMIC, &vb)))
  { VkNote("DIP: VB не создан", false, 0); return false; }
  void* pV = 0;
  if (FAILED(LockVertexBuffer(vb, 0, (unsigned)sizeof(kTri), LOCK_DISCARD, &pV)) || !pV)
  { VkNote("DIP: LockVertexBuffer", false, 0); return false; }
  memcpy(pV, kTri, sizeof(kTri));
  UnlockVertexBuffer(vb);

  VertexFormatDescriptor fd;
  fd.AddVertexElement(VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT3,
                                              VERETEXELEMENTUSAGE_POSITION, 0));
  HInputLayout il;
  if (FAILED(CreateInputLayout(fd, &il))) { VkNote("DIP: раскладка не создана", false, 0); return false; }
  if (FAILED(SetInputLayout(il))) { VkNote("DIP: SetInputLayout", false, 0); return false; }
  if (FAILED(SetStreamSource(0, vb, 0, (unsigned)sizeof(float) * 3)))
  { VkNote("DIP: SetStreamSource", false, 0); return false; }
  SetInstanceCount(0);

  const HRESULT hrDraw = Draw(RENDERPRIMITIVE_TRIANGLELIST, 0, 1);
  if (FAILED(hrDraw))
  {
    char det[64];
    snprintf(det, sizeof(det), "hr=0x%08X", (unsigned)hrDraw);
    VkNote("DIP: Draw", false, det);
    return false;
  }

  FlushRecording();
  d.nvrhiDevice->waitForIdle();

  nstl::vector<unsigned char> px((size_t)H * W * 4, 0);
  EBackendPixelLayout lay = plBGRA;
  const HRESULT hrRead = ReadSurface(rt, 0, &px[0], W * 4, &lay);
  if (FAILED(hrRead))
  {
    char det[64];
    snprintf(det, sizeof(det), "hr=0x%08X", (unsigned)hrRead);
    VkNote("DIP: ReadSurface", false, det);
    return false;
  }

  // порядок байтов — B,G,R,A (см. ReadSurface): PS пишет (Z,0,0,1), значит в
  // нарисованных пикселях R>0, а B и G нулевые
  unsigned painted = 0, clean = 0, maxR = 0, paintedRight = 0;
  for (unsigned y = 0; y < H; ++y)
    for (unsigned x = 0; x < W; ++x)
    {
      const unsigned char* p = &px[(size_t)y * W * 4 + (size_t)x * 4];
      if (p[2])
      {
        ++painted;
        if (x >= W / 2u) ++paintedRight;   // правая половина заведомо пуста
        if (p[2] > maxR) maxR = p[2];
        if (!p[0] && !p[1]) ++clean;
      }
    }
  const unsigned char* s = &px[((size_t)(H / 2)) * W * 4 + (size_t)(W / 4) * 4];
  char det[224];
  snprintf(det, sizeof(det), "комбинация #%u, блобы VS %u Б / PS %u Б; нарисовано %u/%u пикселей, "
                           "каналы чистые %u, справа %u, макс R=%u, выборка BGRA=%u,%u,%u,%u",
           index, vsSize, psSize, painted, W * H, clean, paintedRight, maxR, s[0], s[1], s[2], s[3]);
  // площадь треугольника (-1,-1)-(0,-1)-(-1,1) = ровно 1/4 кадра; допуск 2*W
  // пикселей на правила растеризации краёв. Прежний порог `painted > W*H/4`
  // отсекал точное попадание (замер 2026-10-15: 16384 при пороге >16384)
  const unsigned expect = W * H / 4u, tol = 2u * W;
  const bool ok = (painted + tol > expect) && (painted < expect + tol) &&
                  paintedRight == 0 && clean == painted && maxR > 0;
  VkNote(ok ? "DIP: пиксели на месте" : "DIP: пикселей нет", ok, det);
  return ok;
}

} // namespace Render
