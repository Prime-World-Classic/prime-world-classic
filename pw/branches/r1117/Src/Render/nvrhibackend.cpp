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
// Шаг 13.1 снял текстуры (создание 2D/cube, привязка к слоту сэмплера,
// UpdateTexture). Остались UP-пути (в Vulkan на каждый вызов нужен staging-
// буфер) и вспомогательные операции поверх поверхностей.
#define NVRHI_STUB_LIST(X) \
  X(DrawIndexedUP) X(DrawUP) \
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

// вид ресурса из SPIR-V (SpvResScan) — тот же набор, что у сканера спайка
enum EResKind { rkSampler = 0, rkTexture, rkUbo, rkSsbo, rkOther };
// слотов сэмплера у D3D9 16 (GlobalMasks.h использует s0..s12)
static const unsigned kMaxSamplerSlots = 16;

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
  // для GetSurfaceInfo/LockSurface (шаг 13.2): размеры и seam-формат нужны
  // клиенту, а COM-описания у Vulkan-поверхности нет
  nvrhi::Format fmt;
  unsigned w, h;
  EBackendFormat seamFormat;
  EBackendPool pool;
  EBackendUsage usage;
  bool depth;
  // CPU-зеркало уровня 0 — как у текстур: у Vulkan нет приведённой памяти
  std::vector<unsigned char> mirror;
  bool locked;
  long lockX, lockY, lockW, lockH;
  SurfaceObj() : backBuffer(false), fmt(nvrhi::Format::UNKNOWN), w(0), h(0),
                 seamFormat(bfUnknown), pool(BACKEND_POOL_DEFAULT), usage(usageNone),
                 depth(false), locked(false),
                 lockX(0), lockY(0), lockW(0), lockH(0) {}
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
  // Индексы картинок swapchain'а в d.surfaces. Отдельный список нужен потому,
  // что пересоздание swapchain'а НЕ имеет права чистить d.surfaces целиком:
  // там же поверхности, созданные клиентом (RT/DS), а их ручки — индексы.
  std::vector<unsigned> backSurfaceIdx;
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
  // Ресурс, объявленный шейдером: набор, binding и вид. binding для текстур и
  // сэмплеров = слот D3D9 (писатель tools/spirv_probe/shim.py размечает
  // `register(tN, space2)`/`register(sN, space3)` именно по номеру из
  // исходника; замер корпуса: RoadMap s0 -> set 2 binding 0), поэтому
  // SetTexture(N, tex) кладёт текстуру прямо в binding N.
  struct ResItem { unsigned set, binding, kind; };
  struct ShaderObj
  {
    nvrhi::ShaderHandle sh;
    std::string entry;                // точка входа из OpEntryPoint
    // входы VS: имя семантики из SPIR-V (in.var.POSITION) -> location
    std::vector<std::pair<std::string, unsigned> > inputs;
    std::vector<ResItem> res;         // ресурсы этой стадии (шаг 13.1)
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

  ////////////////////////////////////////////////////////////////////
  // Шаг 13.1: текстуры и наборы ресурсов.
  //
  // Набор биндингов зависит от ПАРЫ шейдеров (какие слоты объявлены), поэтому
  // layout кэшируется по (vs,ps), а сам binding set — по подписке привязанных
  // текстур: IBindingSet в этой версии NVRHI неизменяем (у IBindingSet нет
  // setTexture, nvrhi.h:2519), набор пересоздаётся при смене текстур.
  struct TextureObj
  {
    nvrhi::TextureHandle tex;
    nvrhi::Format fmt;
    unsigned w, h, mipLevels;
    bool cube;
    // seam-атрибуты (шаг 13.2): клиент читает описание текстуры через
    // GetTextureInfo, а COM-описания у Vulkan-текстуры нет
    EBackendFormat seamFormat;
    EBackendUsage usage;
    EBackendPool pool;
    // CPU-зеркало уровня 0: у Vulkan нет «приведённой» памяти, поэтому
    // LockTexture2D отдаёт указатель сюда, а UnlockTexture2D грузит её целиком
    // (зеркало авторитетно, поэтому частичная блокировка не затирает остальное)
    std::vector<unsigned char> mirror;
    bool locked;
    long lockX, lockY, lockW, lockH;
    TextureObj() : fmt(nvrhi::Format::UNKNOWN), w(0), h(0), mipLevels(1), cube(false),
                   seamFormat(bfUnknown), usage(usageNone), pool(BACKEND_POOL_DEFAULT),
                   locked(false), lockX(0), lockY(0), lockW(0), lockH(0) {}
  };
  struct ResMapObj
  {
    // по одному layout на набор: setRegisterSpaceAndDescriptorSet задаёт ОДИН
    // registerSpace на layout, а NVRHI рвёт «Can't have multiple binding sets
    // with the same registerSpace» (vulkan-resource-bindings.cpp:1031)
    nvrhi::BindingLayoutHandle layoutTex, layoutSmp, layoutSsbo;
    nvrhi::BindingSetHandle setSmp;   // сэмплеры статичны (один сэмплер по умолчанию)
    std::vector<ResItem> items;
    unsigned vs, ps;
    ResMapObj() : vs(kNoRes), ps(kNoRes) {}
  };
  std::vector<TextureObj> textures;
  std::vector<ResMapObj> resMaps;
  // подпись (набор привязок текстур) -> готовый набор; линейный поиск: за кадр
  // уникальных подписей мало (замер шага 8: 85 активных ключей на кадр)
  std::vector<std::pair<unsigned long long, nvrhi::BindingSetHandle> > setCache;
  nvrhi::SamplerHandle defSampler;    // состояние сэмплера через seam НЕ приходит
  nvrhi::TextureHandle dummyTex;      // в несвязанный слот кладётся 1x1
  unsigned texBound[16];              // слот сэмплера D3D9 -> индекс текстуры
  unsigned setsCreated, texMissing, resMapsCreated;

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
      recording(false), draws(0), pipesCreated(0), pipeHits(0), uploads(0),
      setsCreated(0), texMissing(0), resMapsCreated(0)
  {
    deviceName[0] = 0;
    memset(stubCalls, 0, sizeof(stubCalls));
    for (int i = 0; i < 16; ++i) texBound[i] = kNoRes;
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

  // Шаг 13.1. Состояние сэмплера в seam НЕ описано: RenderStatesManager пишет
  // напрямую в D3D9 (`GetDevice()->SetSamplerState`, renderstatesmanager.cpp:218),
  // поэтому на Vulkan у всех слотов пока один сэмплер — линейная фильтрация +
  // wrap (что игра использует чаще всего). Перевод состояний в seam — шаг 13.2
  // (там же ключ пайплайна из шага 8, где GetSamplerShadowHash уже есть).
  d.defSampler = d.nvrhiDevice->createSampler(nvrhi::SamplerDesc()
                                              .setAllFilters(true)
                                              .setAddressU(nvrhi::SamplerAddressMode::Wrap)
                                              .setAddressV(nvrhi::SamplerAddressMode::Wrap)
                                              .setAddressW(nvrhi::SamplerAddressMode::Wrap));
  // 1x1 чёрная: в слот, который шейдер объявляет, а клиент не привязал, обязан
  // лежать валидный дескриптор — иначе валидация рвёт кадр молча
  d.dummyTex = d.nvrhiDevice->createTexture(nvrhi::TextureDesc()
                                            .setDimension(nvrhi::TextureDimension::Texture2D)
                                            .setWidth(1).setHeight(1)
                                            .setFormat(nvrhi::Format::BGRA8_UNORM)
                                            .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                            .setKeepInitialState(true)
                                            .setDebugName("seam dummy 1x1"));
  if (!d.defSampler || !d.dummyTex)
  {
    VkNote("Init: сэмплер по умолчанию / dummy-текстура", false, 0);
    return false;
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
    // описание заполняется здесь же: GetSurfaceInfo обязан работать и на
    // бэк-буфере (по нему клиент берёт размеры для снимка экрана)
    so.fmt = nf;
    so.w = d.width; so.h = d.height;
    so.seamFormat = (nf == nvrhi::Format::BGRA8_UNORM) ? bfA8R8G8B8 : bfUnknown;
    so.pool = BACKEND_POOL_DEFAULT;
    so.usage = usageRenderTarget;
    d.backSurfaceIdx.push_back((unsigned)d.surfaces.size());
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
  // vkDestroySwapchainKHR. d.surfaces целиком НЕ чистится: там поверхности
  // клиента (RT/DS), чьи ручки — индексы этого же вектора; отпускаются только
  // записи бэк-буферов (их индексы лежат в backSurfaceIdx, и выданные
  // GetBackBuffer ручки становятся пустыми — клиент обязан взять бэк-буфер
  // заново после пересоздания).
  for (size_t i = 0; i < d.backSurfaceIdx.size(); ++i)
    if (d.backSurfaceIdx[i] < d.surfaces.size())
    {
      d.surfaces[d.backSurfaceIdx[i]].tex = nullptr;
      d.surfaces[d.backSurfaceIdx[i]].staging = nullptr;
    }
  d.backSurfaceIdx.clear();
  d.backTextures.clear();
  // Кэш пайплайнов держит framebuffer'ы, построенные по RT/DS; после
  // пересоздания swapchain'а часть из них указывает на мёртвые картинки,
  // поэтому сбрасываем (дороже, но молча рисовать по чужой картинке — хуже).
  d.pipes.clear();
  d.setCache.clear();
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
  // порядок: ВСЕ nvrhi-ресурсы обязаны умереть раньше nvrhiDevice и VkDevice
  // (иначе деструкторы RefCountPtr работают с уже уничтоженным устройством)
  d.setCache.clear();
  d.resMaps.clear();
  d.pipes.clear();
  d.textures.clear();
  d.buffers.clear();
  d.shaderObjs.clear();
  d.layoutObjs.clear();
  d.defSampler = nullptr;
  d.dummyTex = nullptr;
  d.set0Set = nullptr;
  d.set0Layout = nullptr;
  d.ubo[0] = nullptr;
  d.ubo[1] = nullptr;
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
  // DebugTrace — не printf, а PrintfFormatter: максимум 10 аргументов, поэтому
  // счётчики ресурсов в двух строках
  DebugTrace("VkRes%s: буферы=%u шейдеры=%u раскладки=%u пайплайны=%u/%u (попаданий %u) "
             "вывод=%u загруз=%u",
             pTag ? pTag : "", (unsigned)d.buffers.size(), (unsigned)d.shaderObjs.size(),
             (unsigned)d.layoutObjs.size(), d.pipesCreated, (unsigned)d.pipes.size(), d.pipeHits,
             d.draws, d.uploads);
  DebugTrace("VkTex%s: текстуры=%u наборы=%u/%u (промахов текстур %u)",
             pTag ? pTag : "", (unsigned)d.textures.size(), d.setsCreated,
             (unsigned)d.resMaps.size(), d.texMissing);
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

  if (d.imageIndex >= d.backTextures.size() || d.imageIndex >= d.backSurfaceIdx.size())
    return E_FAIL;
  // Индекс бэк-буфера берём из backSurfaceIdx: после пересоздания swapchain'а
  // картинки добавляются в КОНЕЦ d.surfaces (там уже лежат поверхности клиента),
  // поэтому «imageIndex» как индекс вектора больше не годится — он указывал бы
  // на мёртвую запись с tex == nullptr, и clearTextureFloat(null) ронял клиент
  // (замер 2026-10-15, прогон 2026.10.15-21.03.40: access violation сразу после
  // «пересоздание swapchain'а #1»). Заодно проверяем, что ручка жива.
  const unsigned sIdx = d.backSurfaceIdx[d.imageIndex];
  if (sIdx >= d.surfaces.size() || !d.surfaces[sIdx].tex)
    return E_FAIL;
  SurfaceObj& so = d.surfaces[sIdx];

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
  // без флага PS-банк никогда не загружается: в прогоне 2026.10.15-20.01.25
  // тест с текстурой дал чёрный вывод именно поэтому (outputMul читался нулём)
  m_pImpl->uboDirty[csPixel] = true;
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

// Общий путь создания текстур seam'а (2D и cube). Формат берётся из
// SurfaceFormat — тот же, что и для поверхностей (bfA8R8G8B8 -> BGRA8_UNORM).
static HRESULT CreateTextureCommon(NvrhiBackendImpl& d, unsigned width, unsigned height,
                                   unsigned mipLevels, EBackendUsage usage, EBackendFormat format,
                                   EBackendPool pool, bool cube, HTexture* pOut)
{
  if (!width || !height || !pOut) return D3DERR_INVALIDCALL;
  if (!d.nvrhiDevice) return E_FAIL;
  const nvrhi::Format fmt = SurfaceFormat(format);
  if (fmt == nvrhi::Format::UNKNOWN)
  {
    char det[64];
    snprintf(det, sizeof(det), "формат %d не переведён", (int)format);
    VkNote("CreateTexture", false, det);
    return E_FAIL;
  }
  nvrhi::TextureDesc td;
  td.setDimension(cube ? nvrhi::TextureDimension::TextureCube : nvrhi::TextureDimension::Texture2D)
    .setWidth(width).setHeight(height).setFormat(fmt)
    // mipLevels=0 у D3D9 означает «автоогенация»; без запроса mipmap'ов берём 1,
    // иначе NVRHI заведёт цепочку, которую никто не заполняет
    .setMipLevels(mipLevels ? mipLevels : 1u);
  if (usage & usageRenderTarget)
    td.setIsRenderTarget(true).setInitialState(nvrhi::ResourceStates::RenderTarget);
  else
    td.setInitialState(nvrhi::ResourceStates::ShaderResource);
  // keepInitialState обязателен: без него трекер состояний NVRHI не знает,
  // откуда стартовать, и рвёт командный список (замер спайка)
  td.setKeepInitialState(true).setDebugName(cube ? "seam texture cube" : "seam texture");
  nvrhi::TextureHandle tex = d.nvrhiDevice->createTexture(td);
  if (!tex)
  {
    char det[96];
    snprintf(det, sizeof(det), "%ux%u mip=%u fmt=%d usage=%u", width, height, mipLevels,
             (int)format, (unsigned)usage);
    VkNote("CreateTexture: createTexture", false, det);
    return E_FAIL;
  }
  if ((usage & usageAutoGenMipmap) && d.texMissing < 8u)
    VkNote("CreateTexture: автогенерация mipmap через seam не переведена (шаг 13)", false, 0);
  NvrhiBackendImpl::TextureObj to;
  to.tex = tex; to.fmt = fmt; to.w = width; to.h = height;
  to.mipLevels = mipLevels ? mipLevels : 1u; to.cube = cube;
  to.seamFormat = format; to.usage = usage; to.pool = pool;
  d.textures.push_back(to);
  pOut->p = Idx2Hnd((unsigned)d.textures.size() - 1u);
  return S_OK;
}

HRESULT NvrhiBackend::CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                      EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                      HTexture* pOut)
{
  return CreateTextureCommon(*m_pImpl, width, height, mipLevels, usage, format, pool, false, pOut);
}

HRESULT NvrhiBackend::CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                        EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                        HTexture* pOut)
{
  return CreateTextureCommon(*m_pImpl, edgeLen, edgeLen, mipLevels, usage, format, pool, true, pOut);
}

HRESULT NvrhiBackend::UpdateTexture(HTexture srcTexture, HTexture dstTexture)
{
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned s = Hnd2Idx(srcTexture.p), t = Hnd2Idx(dstTexture.p);
  if (s == kNoRes || t == kNoRes || s >= d.textures.size() || t >= d.textures.size())
    return D3DERR_INVALIDCALL;
  if (!d.textures[s].tex || !d.textures[t].tex) return E_FAIL;
  if (!EnsureRecording()) return E_FAIL;
  // копия целиком: seam не несёт подпрямоугольник/подмип (в D3D9-пути
  // UpdateTexture тоже вызывается на всю текстуру)
  d.commandList->copyTexture(d.textures[t].tex, nvrhi::TextureSlice(),
                             d.textures[s].tex, nvrhi::TextureSlice());
  ++d.uploads;
  return S_OK;
}

// Байт на пиксель для незапечатанных форматов seam'а. 0 = через LockTexture2D
// не грузится (сжатые DXT, глубина).
static unsigned BppOf(nvrhi::Format f)
{
  switch (f)
  {
    case nvrhi::Format::BGRA8_UNORM:
    case nvrhi::Format::RGBA8_UNORM:
    case nvrhi::Format::R32_FLOAT:      return 4u;
    case nvrhi::Format::R8_UNORM:      return 1u;
    case nvrhi::Format::B5G6R5_UNORM:
    case nvrhi::Format::R16_UNORM:
    case nvrhi::Format::R16_FLOAT:     return 2u;
    case nvrhi::Format::RGBA16_FLOAT:
    case nvrhi::Format::RG32_FLOAT:    return 8u;
    case nvrhi::Format::RGBA32_FLOAT:  return 16u;
    default:                           return 0u;
  }
}

// Загрузка бит в 2D-текстуру. В D3D9 это LockRect/UnlockRect у COM-текстуры
// (сеam закрыл их правкой renderbackend.h), в Vulkan — CPU-зеркало и
// copyTexture на Unlock. Поддержан только уровень 0: mip-цепочка и cube через
// этот метод не грузятся (у seam'а нет LockTextureCube, а автогенерация mipmap
// вообще не переведена — см. CreateTextureCommon).
HRESULT NvrhiBackend::LockTexture2D(HTexture texture, unsigned level, const BackendRect* pRect,
                                    ERenderLockType mode, void** ppData, unsigned* pRowPitch)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!texture.p || !ppData) return E_INVALIDARG;
  *ppData = 0;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::TextureObj& to = d.textures[t];
  const unsigned bpp = BppOf(to.fmt);
  if (level != 0u || to.cube || !bpp)
  {
    char det[96];
    snprintf(det, sizeof(det), "level=%u cube=%d формат=%d", level, (int)to.cube, (int)to.fmt);
    VkNote("LockTexture2D: вне Vulkan-пути", false, det);
    return E_FAIL;
  }
  long x0 = 0, y0 = 0, x1 = (long)to.w, y1 = (long)to.h;
  if (pRect)
  {
    if (pRect->left > x0) x0 = pRect->left;
    if (pRect->top > y0) y0 = pRect->top;
    if (pRect->right < x1) x1 = pRect->right;
    if (pRect->bottom < y1) y1 = pRect->bottom;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
  }
  to.mirror.resize((size_t)to.w * to.h * bpp);
  to.locked = true;
  to.lockX = x0; to.lockY = y0; to.lockW = x1 - x0; to.lockH = y1 - y0;
  *ppData = &to.mirror[(size_t)y0 * to.w * bpp + (size_t)x0 * bpp];
  if (pRowPitch) *pRowPitch = to.w * bpp;
  // режим блокировки — намерение (locktype.h): зеркало всегда полное, поэтому
  // DISCARD/NOOVERWRITE различать нечем
  (void)mode;
  return S_OK;
}

HRESULT NvrhiBackend::UnlockTexture2D(HTexture texture, unsigned level)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!texture.p) return E_INVALIDARG;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::TextureObj& to = d.textures[t];
  if (!to.locked || to.mirror.empty()) return E_FAIL;
  to.locked = false;
  const unsigned bpp = BppOf(to.fmt);
  nvrhi::StagingTextureHandle stg = d.nvrhiDevice->createStagingTexture(
      nvrhi::TextureDesc()
        .setDimension(to.cube ? nvrhi::TextureDimension::TextureCube : nvrhi::TextureDimension::Texture2D)
        .setWidth(to.w).setHeight(to.h).setFormat(to.fmt).setMipLevels(1)
        .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true)
        .setDebugName("seam texture staging"),
      nvrhi::CpuAccessMode::Write);
  if (!stg) { VkNote("UnlockTexture2D: staging", false, 0); return E_FAIL; }
  size_t pitch = 0;
  void* pDst = d.nvrhiDevice->mapStagingTexture(stg, nvrhi::TextureSlice(),
                                                nvrhi::CpuAccessMode::Write, &pitch);
  if (!pDst) { VkNote("UnlockTexture2D: mapStagingTexture", false, 0); return E_FAIL; }
  // шаг staging может отличаться от шага зеркала — перенос построчно
  for (unsigned y = 0; y < to.h; ++y)
    memcpy((unsigned char*)pDst + (size_t)y * pitch, &to.mirror[(size_t)y * to.w * bpp],
           (size_t)to.w * bpp);
  d.nvrhiDevice->unmapStagingTexture(stg);
  if (!EnsureRecording()) return E_FAIL;
  d.commandList->copyTexture(to.tex, nvrhi::TextureSlice(), stg, nvrhi::TextureSlice());
  ++d.uploads;
  return S_OK;
}

// Описание текстуры (шаг 13.2): клиенту нужен D3DSURFACE_DESC-эквивалент — по
// нему текстура пересоздаётся после потери устройства (Render/texture.cpp) и
// проверяется пул в деструкторе. Значения возвращаем те, что просили при
// создании: у Vulkan-текстуры COM-описания нет.
HRESULT NvrhiBackend::GetTextureInfo(HTexture texture, BackendTextureInfo* pInfo)
{
  if (!pInfo) return E_INVALIDARG;
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex)
  {
    VkNote("GetTextureInfo: вне Vulkan-пути", false, 0);
    return E_FAIL;
  }
  const NvrhiBackendImpl::TextureObj& to = d.textures[t];
  pInfo->width  = to.w;
  pInfo->height = to.h;
  pInfo->levels = to.mipLevels;
  pInfo->format = to.seamFormat;
  pInfo->usage  = to.usage;
  pInfo->pool   = to.pool;
  return S_OK;
}

// Цепочку mip'ов выбирает бэкенд (шаг 13.2): D3D9 — autogen драйвера, а под
// Wine/CrossOver — ручная цепочка по COM-поверхностям через StretchRect
// (прежние GenerateSubLevels_1/2 в texture.cpp). Здесь — те два шага через
// copyTexture: уровень n-1 -> временная текстура -> уровень n. Копия Vulkan не
// фильтрует (как и StretchRect), т.е. поведение совпадает с Wine-цепочкой, а не
// с autogen драйвера — на этом контуре так и было.
HRESULT NvrhiBackend::GenerateMipmaps(HTexture texture)
{
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::TextureObj& to = d.textures[t];
  if (to.mipLevels <= 1u) return S_OK;   // цепочки нет
  if (to.cube)
  {
    VkNote("GenerateMipmaps: cube-цепочка не переведена", false, 0);
    return E_FAIL;
  }
  if (!d.nvrhiDevice || !EnsureRecording()) return E_FAIL;
  for (unsigned n = 1; n < to.mipLevels; ++n)
  {
    const unsigned sw = (to.w >> (n - 1u)) ? (to.w >> (n - 1u)) : 1u;
    const unsigned sh = (to.h >> (n - 1u)) ? (to.h >> (n - 1u)) : 1u;
    const unsigned dw = (to.w >> n) ? (to.w >> n) : 1u;
    const unsigned dh = (to.h >> n) ? (to.h >> n) : 1u;
    nvrhi::TextureHandle tmp = d.nvrhiDevice->createTexture(
        nvrhi::TextureDesc()
          .setDimension(nvrhi::TextureDimension::Texture2D)
          .setWidth(sw).setHeight(sh).setFormat(to.fmt).setMipLevels(1)
          .setInitialState(nvrhi::ResourceStates::ShaderResource).setKeepInitialState(true)
          .setDebugName("seam mip chain"));
    if (!tmp)
    {
      char det[64];
      snprintf(det, sizeof(det), "уровень %u, %ux%u", n, sw, sh);
      VkNote("GenerateMipmaps: временная текстура", false, det);
      return E_FAIL;
    }
    d.commandList->copyTexture(tmp, nvrhi::TextureSlice(),
                               to.tex, nvrhi::TextureSlice().setMipLevel(n - 1u)
                                       .setWidth(sw).setHeight(sh));
    d.commandList->copyTexture(to.tex, nvrhi::TextureSlice().setMipLevel(n)
                                       .setWidth(dw).setHeight(dh),
                               tmp, nvrhi::TextureSlice());
  }
  ++d.uploads;
  return S_OK;
}

// Привязка текстуры к слоту сэмплера. binding в SPIR-V = номер слота D3D9
// (писатель размечает `register(tN, space2)` по номеру из исходника), поэтому
// здесь достаточно запомнить индекс — набор соберётся на выводе.
HRESULT NvrhiBackend::SetTexture(unsigned samplerIndex, HTexture texture)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (samplerIndex >= kMaxSamplerSlots)
  {
    char det[64];
    snprintf(det, sizeof(det), "слот %u вне 0..%u", samplerIndex, kMaxSamplerSlots - 1u);
    VkNote("SetTexture", false, det);
    return D3DERR_INVALIDCALL;
  }
  const unsigned t = Hnd2Idx(texture.p);
  if (t != kNoRes && t >= d.textures.size()) return D3DERR_INVALIDCALL;
  d.texBound[samplerIndex] = t;
  return S_OK;
}

// Набор ресурсов пары шейдеров (шаг 13.1). Layout — по одному на набор:
// setRegisterSpaceAndDescriptorSet задаёт ОДИН registerSpace на layout, а NVRHI
// рвёт «Can't have multiple binding sets with the same registerSpace»
// (vulkan-resource-bindings.cpp:1031). Набор 0 (оба c-банка) создан в Init.
static int FindOrCreateResMap(NvrhiBackendImpl& d)
{
  if (d.curVs == kNoRes || d.curPs == kNoRes) return -1;
  if (d.curVs >= d.shaderObjs.size() || d.curPs >= d.shaderObjs.size()) return -1;
  for (size_t i = 0; i < d.resMaps.size(); ++i)
    if (d.resMaps[i].vs == d.curVs && d.resMaps[i].ps == d.curPs) return (int)i;

  NvrhiBackendImpl::ResMapObj rm;
  rm.vs = d.curVs; rm.ps = d.curPs;
  const std::vector<NvrhiBackendImpl::ResItem>* lists[2] = { &d.shaderObjs[d.curVs].res,
                                                             &d.shaderObjs[d.curPs].res };
  for (int L = 0; L < 2; ++L)
  {
    for (size_t k = 0; k < lists[L]->size(); ++k)
    {
      const NvrhiBackendImpl::ResItem& it = (*lists[L])[k];
      bool dup = false;
      for (size_t e = 0; e < rm.items.size(); ++e)
        if (rm.items[e].set == it.set && rm.items[e].binding == it.binding) { dup = true; break; }
      if (!dup) rm.items.push_back(it);
    }
  }

  const nvrhi::VulkanBindingOffsets offs = nvrhi::VulkanBindingOffsets()
      .setShaderResourceOffset(0).setSamplerOffset(0)
      .setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
  for (unsigned setNo = 1u; setNo <= 3u; ++setNo)
  {
    nvrhi::BindingLayoutDesc ld;
    ld.visibility = nvrhi::ShaderType::AllGraphics;
    ld.setRegisterSpaceAndDescriptorSet(setNo);
    ld.bindingOffsets = offs;
    bool any = false;
    for (size_t k = 0; k < rm.items.size(); ++k)
    {
      const NvrhiBackendImpl::ResItem& it = rm.items[k];
      if (it.set != setNo) continue;
      switch (it.kind)
      {
      case rkTexture: ld.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(it.binding)); break;
      case rkSampler: ld.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(it.binding)); break;
      case rkSsbo:    ld.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(it.binding)); break;
      case rkUbo:     ld.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(it.binding)); break;
      default:        continue;
      }
      any = true;
    }
    if (!any) continue;
    nvrhi::BindingLayoutHandle lh = d.nvrhiDevice->createBindingLayout(ld);
    if (!lh)
    {
      char det[64];
      snprintf(det, sizeof(det), "набор %u, записей %u", setNo, (unsigned)ld.bindings.size());
      VkNote("ResMap: createBindingLayout", false, det);
      return -1;
    }
    if (setNo == 2u) rm.layoutTex = lh;
    else if (setNo == 3u) rm.layoutSmp = lh;
    else rm.layoutSsbo = lh;
  }

  // набор сэмплеров статичен: состояние сэмплера через seam не приходит
  // (RenderStatesManager пишет прямо в D3D9), поэтому везде один сэмплер
  if (rm.layoutSmp)
  {
    nvrhi::BindingSetDesc bsd;
    for (size_t k = 0; k < rm.items.size(); ++k)
      if (rm.items[k].set == 3u && rm.items[k].kind == rkSampler)
        bsd.bindings.push_back(nvrhi::BindingSetItem::Sampler(rm.items[k].binding, d.defSampler));
    rm.setSmp = d.nvrhiDevice->createBindingSet(bsd, rm.layoutSmp);
    if (!rm.setSmp) VkNote("ResMap: createBindingSet(сэмплеры)", false, 0);
  }
  // SSBO (набор 1) пока нечем заполнять: seam передаёт per-instance данные через
  // SetInstanceSource как вершинный буфер, а SPIR-V ждёт StructuredBuffer —
  // это шаг 13.2 (там же ключ пайплайна из шага 8)
  bool ssbo = false;
  for (size_t k = 0; k < rm.items.size(); ++k)
    if (rm.items[k].kind == rkSsbo) { ssbo = true; break; }
  if (ssbo && d.resMapsCreated < 8u)
    VkNote("ResMap: SSBO в наборе 1 ещё не привязан (шаг 13.2)", false, 0);

  d.resMaps.push_back(rm);
  ++d.resMapsCreated;
  if (d.resMaps.size() <= 4u || (d.resMaps.size() % 50u) == 0u)
  {
    char det[128];
    unsigned nTex = 0, nSmp = 0;
    for (size_t k = 0; k < rm.items.size(); ++k)
    {
      if (rm.items[k].kind == rkTexture) ++nTex;
      else if (rm.items[k].kind == rkSampler) ++nSmp;
    }
    snprintf(det, sizeof(det), "наборов %u, текстур %u, сэмплеров %u, всего %u",
             (unsigned)(!!rm.layoutTex + !!rm.layoutSmp + !!rm.layoutSsbo), nTex, nSmp,
             (unsigned)d.resMaps.size());
    VkNote("ResMap", true, det);
  }
  return (int)d.resMaps.size() - 1;
}

// Подпись привязок текстур пары (FNV-64 по парам binding->текстура). Набор
// биндингов в этой версии NVRHI неизменяем, поэтому при смене текстур набор
// пересоздаётся, а подписи кэшируются.
static unsigned long long TexSignature(NvrhiBackendImpl& d, const NvrhiBackendImpl::ResMapObj& rm)
{
  unsigned long long h = 1469598103934665603ull;
  for (size_t k = 0; k < rm.items.size(); ++k)
  {
    const NvrhiBackendImpl::ResItem& it = rm.items[k];
    if (it.set != 2u || it.kind != rkTexture) continue;
    const unsigned tex = (it.binding < kMaxSamplerSlots) ? d.texBound[it.binding] : kNoRes;
    const unsigned long long part = ((unsigned long long)it.binding << 32)
                                    | (unsigned long long)tex;
    h ^= part;
    h *= 1099511628211ull;
  }
  return h;
}

static nvrhi::BindingSetHandle FindOrCreateTexSet(NvrhiBackendImpl& d,
                                                  const NvrhiBackendImpl::ResMapObj& rm,
                                                  unsigned long long sig)
{
  for (size_t i = 0; i < d.setCache.size(); ++i)
    if (d.setCache[i].first == sig) return d.setCache[i].second;
  // кэп: за бой подписей больше, чем пайплайнов (текстуры меняются чаще), а
  // набор держит VkDescriptorSet — при переполнении чистим и строим заново
  if (d.setCache.size() > 4096u)
  {
    d.setCache.clear();
    VkNote("setCache очищен (>4096 подписей)", true, 0);
  }
  nvrhi::BindingSetDesc bsd;
  for (size_t k = 0; k < rm.items.size(); ++k)
  {
    const NvrhiBackendImpl::ResItem& it = rm.items[k];
    if (it.set != 2u || it.kind != rkTexture) continue;
    const unsigned tex = (it.binding < kMaxSamplerSlots) ? d.texBound[it.binding] : kNoRes;
    nvrhi::ITexture* pTex = d.dummyTex;
    if (tex != kNoRes && tex < d.textures.size() && d.textures[tex].tex)
      pTex = d.textures[tex].tex;
    else if (d.texMissing < 8u)
    {
      char det[64];
      snprintf(det, sizeof(det), "слот %u не привязан", it.binding);
      VkNote("Draw: текстура в набор", false, det);
      ++d.texMissing;
    }
    bsd.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(it.binding, pTex));
  }
  nvrhi::BindingSetHandle bs = d.nvrhiDevice->createBindingSet(bsd, rm.layoutTex);
  if (!bs)
  {
    VkNote("Draw: createBindingSet(текстуры)", false, 0);
    return nvrhi::BindingSetHandle();
  }
  d.setCache.push_back(std::make_pair(sig, bs));
  ++d.setsCreated;
  return bs;
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
  const int rmIdx = FindOrCreateResMap(d);
  if (rmIdx >= 0)
  {
    if (d.resMaps[rmIdx].layoutTex) pd.addBindingLayout(d.resMaps[rmIdx].layoutTex);
    if (d.resMaps[rmIdx].layoutSmp) pd.addBindingLayout(d.resMaps[rmIdx].layoutSmp);
    if (d.resMaps[rmIdx].layoutSsbo) pd.addBindingLayout(d.resMaps[rmIdx].layoutSsbo);
  }

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
  // Шаг 13.1: текстуры (набор 2) и сэмплеры (набор 3). Набор текстур зависит от
  // привязок, поэтому ищется по подписке (IBindingSet неизменяем).
  const int rmIdx = FindOrCreateResMap(d);
  if (rmIdx >= 0)
  {
    const NvrhiBackendImpl::ResMapObj& rm = d.resMaps[rmIdx];
    if (rm.setSmp) gs.addBindingSet(rm.setSmp);
    if (rm.layoutTex)
    {
      nvrhi::BindingSetHandle bsTex = FindOrCreateTexSet(d, rm, TexSignature(d, rm));
      if (bsTex) gs.addBindingSet(bsTex);
    }
  }

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

// Ещё не закрыто (шаг 13): вывод из пользовательских буферов (UP-пути в Vulkan
// требуют staging-копии на каждый вызов).
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

// Ресурсы модуля SPIR-V: (набор, binding, вид). Вид берётся из ТИПА переменной,
// а не из storage class: DXC кладёт StructuredBuffer в класс Uniform(2) с
// декорацией BufferBlock(3), поэтому различать надо Block(2)/BufferBlock(3) у
// структуры (замер спайка tools/nvrhi_probe/probe.cpp:ScanSpv). Номера opcode:
// OpTypeImage=25, OpTypeSampler=26, OpTypeSampledImage=27, OpTypeArray=28,
// OpTypeRuntimeArray=29, OpTypeStruct=30, OpTypePointer=32, OpVariable=59,
// OpDecorate=71; декорации Binding=33, DescriptorSet=34.
static void SpvResScan(const void* pCode, unsigned codeSize,
                       std::vector<NvrhiBackendImpl::ResItem>* pOut)
{
  const unsigned* w = static_cast<const unsigned*>(pCode);
  const unsigned n = codeSize / 4u;
  if (n < 5u || w[0] != 0x07230203u) return;

  std::vector<unsigned> opOfId, elemOfId, ptrBase, varType, decBind, decSet;
  std::vector<unsigned char> ptrStore, structBlock, structBufferBlock;
  for (unsigned i = 5u; i < n; )
  {
    const unsigned op = w[i], wc = op >> 16;
    if (wc == 0u || i + wc > n) return;
    const unsigned code = op & 0xffffu;
    // OpTypePointer разбирается ДО диапазона объявлений типов: 32 входит в
    // 19..33, и если сначала проверять диапазон, ветка указателей не выполняется
    // никогда (замер 2026-10-15, прогон 2026.10.15-19.55.06: из-за этого
    // ptrStore пуст, SpvResScan не находит НИ ОДНОГО ресурса — «наборов 0,
    // текстур 0, сэмплеров 0» даже там, где PS читает текстуру)
    if (code == 32u && wc >= 4u)             // OpTypePointer: id, класс, база
    {
      if (w[i + 1] + 1u > ptrBase.size()) { ptrBase.resize(w[i + 1] + 1u, 0xffffffffu);
                                            ptrStore.resize(w[i + 1] + 1u, 255); }
      ptrBase[w[i + 1]] = w[i + 3];
      ptrStore[w[i + 1]] = (unsigned char)w[i + 2];
    }
    else if (code >= 19u && code <= 33u && wc >= 2u)
    {
      // opOfId заполняется ТОЛЬКО объявлениями типов: у инструкций-значений
      // первое слово — Result Type, и он же попадал бы в реестр мусором
      if (w[i + 1] + 1u > opOfId.size()) opOfId.resize(w[i + 1] + 1u, 0u);
      if (!opOfId[w[i + 1]]) opOfId[w[i + 1]] = code;
      if ((code == 28u || code == 29u) && wc >= 3u)
      {
        if (w[i + 1] + 1u > elemOfId.size()) elemOfId.resize(w[i + 1] + 1u, 0xffffffffu);
        elemOfId[w[i + 1]] = w[i + 2];
      }
    }
    else if (code == 59u && wc >= 4u)             // OpVariable: тип, id, класс
    {
      if (w[i + 2] + 1u > varType.size()) varType.resize(w[i + 2] + 1u, 0xffffffffu);
      varType[w[i + 2]] = w[i + 1];
    }
    else if (code == 71u && wc >= 3u)             // OpDecorate: цель, декорация[, значение]
    {
      // у декораций без операнда word count = 3 (Block, BufferBlock), условие
      // «wc >= 4» их молча пропускало — на этом первый сканер давал ssbo=0
      const unsigned tgt = w[i + 1], dec = w[i + 2];
      const unsigned val = (wc >= 4u) ? w[i + 3] : 0u;
      if (dec == 33u)
      {
        if (tgt + 1u > decBind.size()) decBind.resize(tgt + 1u, 0xffffffffu);
        decBind[tgt] = val;
      }
      else if (dec == 34u)
      {
        if (tgt + 1u > decSet.size()) decSet.resize(tgt + 1u, 0xffffffffu);
        decSet[tgt] = val;
      }
      else if (dec == 2u)
      {
        if (tgt + 1u > structBlock.size()) structBlock.resize(tgt + 1u, 0);
        structBlock[tgt] = 1;
      }
      else if (dec == 3u)
      {
        if (tgt + 1u > structBufferBlock.size()) structBufferBlock.resize(tgt + 1u, 0);
        structBufferBlock[tgt] = 1;
      }
    }
    i += wc;
  }

  for (unsigned id = 0; id < varType.size(); ++id)
  {
    if (id >= decBind.size() || id >= decSet.size()) continue;
    if (decBind[id] == 0xffffffffu || decSet[id] == 0xffffffffu) continue;
    const unsigned pt = varType[id];
    if (pt == 0xffffffffu || pt >= ptrStore.size() || pt >= ptrBase.size()) continue;
    const unsigned st = ptrStore[pt];
    if (st != 0u && st != 2u && st != 12u) continue;   // UniformConstant/Uniform/StorageBuffer
    unsigned base = ptrBase[pt];
    unsigned baseOp = (base < opOfId.size()) ? opOfId[base] : 0u;
    if ((baseOp == 28u || baseOp == 29u) && base < elemOfId.size() && elemOfId[base] != 0xffffffffu)
    {
      const unsigned el = elemOfId[base];
      baseOp = (el < opOfId.size()) ? opOfId[el] : 0u;  // массив изображений = массив текстур
    }
    NvrhiBackendImpl::ResItem it;
    it.set = decSet[id];
    it.binding = decBind[id];
    it.kind = rkOther;
    if (baseOp == 26u) it.kind = rkSampler;            // OpTypeSampler
    else if (baseOp == 25u || baseOp == 27u) it.kind = rkTexture;  // Image / SampledImage
    else if (baseOp == 30u)                           // OpTypeStruct: cbuffer или SSBO
    {
      const bool bb = (base < structBufferBlock.size()) && structBufferBlock[base];
      const bool bl = (base < structBlock.size()) && structBlock[base];
      if (bb) it.kind = rkSsbo;
      else if (bl) it.kind = rkUbo;
      else it.kind = (st == 12u) ? rkSsbo : rkUbo;
    }
    else if (st == 12u) it.kind = rkSsbo;
    else if (st == 2u) it.kind = rkUbo;
    if (it.kind != rkOther) pOut->push_back(it);
  }
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
  SpvResScan(pCode, codeSize, &so.res);
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

HRESULT NvrhiBackend::CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                    EBackendFormat format, EBackendPool pool, HSurface* pOut)
{
  if (!width || !height || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;
  (void)pool;

  nvrhi::TextureDesc td;
  td.setDimension(nvrhi::TextureDimension::Texture2D).setWidth(width).setHeight(height);
  nvrhi::Format fmtSel = nvrhi::Format::UNKNOWN;
  bool depth = false;
  switch (format)
  {
    case bfD24S8:  fmtSel = nvrhi::Format::D24S8; td.setFormat(fmtSel); depth = true; break;
    // у NVRHI нет формата B8G8R8A8 — BGRA8_UNORM даёт в памяти те же байты
    // B,G,R,A, что D3D9 A8R8G8B8/X8R8G8B8 (замер спайка: RT BGRA8_UNORM +
    // readback пиксель-в-пиксель); X8 у нас читается как A, потерь в коде нет
    case bfA8R8G8B8:
    case bfX8R8G8B8: fmtSel = nvrhi::Format::BGRA8_UNORM; td.setFormat(fmtSel); break;
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
  so.fmt = fmtSel;
  so.w = width; so.h = height;
  so.seamFormat = format;
  so.pool = pool;
  so.usage = (kind == ssDepthStencil) ? usageDepthStencil
           : (kind == ssRenderTarget) ? usageRenderTarget : usageNone;
  so.depth = depth;
  d.surfaces.push_back(so);
  pOut->p = Idx2Hnd((unsigned)d.surfaces.size() - 1u);
  return S_OK;
}

// Блокировка поверхности (шаг 13.2): прежний путь — LockRect/UnlockRect у
// COM-поверхности (rendersurface.cpp), теперь — seam. Зеркало полное, поэтому
// частичная блокировка не затирает остальное; шаг строки сообщает бэкенд.
HRESULT NvrhiBackend::LockSurface(HSurface surface, const BackendRect* pRect,
                                  ERenderLockType mode, void** ppData, unsigned* pRowPitch)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!surface.p || !ppData) return E_INVALIDARG;
  *ppData = 0;
  const unsigned s = Hnd2Idx(surface.p);
  if (s == kNoRes || s >= d.surfaces.size() || !d.surfaces[s].tex) return D3DERR_INVALIDCALL;
  SurfaceObj& so = d.surfaces[s];
  const unsigned bpp = BppOf(so.fmt);
  if (so.depth || !bpp)
  {
    char det[96];
    snprintf(det, sizeof(det), "глубина=%d формат=%d", (int)so.depth, (int)so.seamFormat);
    VkNote("LockSurface: вне Vulkan-пути", false, det);
    return E_FAIL;
  }
  long x0 = 0, y0 = 0, x1 = (long)so.w, y1 = (long)so.h;
  if (pRect)
  {
    if (pRect->left > x0) x0 = pRect->left;
    if (pRect->top > y0) y0 = pRect->top;
    if (pRect->right < x1) x1 = pRect->right;
    if (pRect->bottom < y1) y1 = pRect->bottom;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
  }
  so.mirror.resize((size_t)so.w * so.h * bpp);
  so.locked = true;
  so.lockX = x0; so.lockY = y0; so.lockW = x1 - x0; so.lockH = y1 - y0;
  *ppData = &so.mirror[(size_t)y0 * so.w * bpp + (size_t)x0 * bpp];
  if (pRowPitch) *pRowPitch = so.w * bpp;
  (void)mode;   // режим — намерение (locktype.h), зеркало всегда полное
  return S_OK;
}

HRESULT NvrhiBackend::UnlockSurface(HSurface surface)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!surface.p) return E_INVALIDARG;
  const unsigned s = Hnd2Idx(surface.p);
  if (s == kNoRes || s >= d.surfaces.size() || !d.surfaces[s].tex) return D3DERR_INVALIDCALL;
  SurfaceObj& so = d.surfaces[s];
  if (!so.locked || so.mirror.empty()) return E_FAIL;
  so.locked = false;
  const unsigned bpp = BppOf(so.fmt);
  nvrhi::StagingTextureHandle stg = d.nvrhiDevice->createStagingTexture(
      nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(so.w).setHeight(so.h).setFormat(so.fmt).setMipLevels(1)
        .setInitialState(nvrhi::ResourceStates::CopyDest).setKeepInitialState(true)
        .setDebugName("seam surface staging"),
      nvrhi::CpuAccessMode::Write);
  if (!stg) { VkNote("UnlockSurface: staging", false, 0); return E_FAIL; }
  size_t pitch = 0;
  void* pDst = d.nvrhiDevice->mapStagingTexture(stg, nvrhi::TextureSlice(),
                                                nvrhi::CpuAccessMode::Write, &pitch);
  if (!pDst) { VkNote("UnlockSurface: mapStagingTexture", false, 0); return E_FAIL; }
  for (unsigned y = 0; y < so.h; ++y)
    memcpy((unsigned char*)pDst + (size_t)y * pitch, &so.mirror[(size_t)y * so.w * bpp],
           (size_t)so.w * bpp);
  d.nvrhiDevice->unmapStagingTexture(stg);
  if (!EnsureRecording()) return E_FAIL;
  d.commandList->copyTexture(so.tex, nvrhi::TextureSlice(), stg, nvrhi::TextureSlice());
  ++d.uploads;
  return S_OK;
}

HRESULT NvrhiBackend::GetSurfaceInfo(HSurface surface, BackendTextureInfo* pInfo)
{
  if (!pInfo) return E_INVALIDARG;
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned s = Hnd2Idx(surface.p);
  if (s == kNoRes || s >= d.surfaces.size() || !d.surfaces[s].tex)
  {
    VkNote("GetSurfaceInfo: вне Vulkan-пути", false, 0);
    return E_FAIL;
  }
  const SurfaceObj& so = d.surfaces[s];
  pInfo->width  = so.w;
  pInfo->height = so.h;
  pInfo->levels = 1u;
  pInfo->format = so.seamFormat;
  pInfo->usage  = so.usage;
  pInfo->pool   = so.pool;
  return S_OK;
}

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
  if (!pOut || d.imageIndex >= d.backSurfaceIdx.size())
    return E_FAIL;
  pOut->p = Idx2Hnd(d.backSurfaceIdx[d.imageIndex]);
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
// Читает пару SPIR-V-блобов из контейнера v2: берётся ПЕРВАЯ запись секции
// (её индекс возвращается). Файл копируется в buf, потому что CObj<Stream>
// живёт только внутри функции.
static bool LoadFirstShdPair(const char* pPath, std::vector<unsigned char>* pBuf, unsigned* pIndex,
                             unsigned* pVsOff, unsigned* pVsSize, unsigned* pPsOff, unsigned* pPsSize)
{
  CObj<Stream> f = RootFileSystem::OpenFile(pPath, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
  if (!f || !f->IsOk()) { VkNote("DIP: контейнер не открыт", false, pPath); return false; }
  const unsigned size = (unsigned)f->GetSize();
  if (!size) { VkNote("DIP: контейнер пуст", false, pPath); return false; }
  pBuf->resize(size);
  memcpy(&(*pBuf)[0], f->GetBuffer(), size);
  const unsigned char* base = &(*pBuf)[0];

  unsigned hdrOffset = 0, version = 0, v1Size = 0;
  nstl::vector<Materials::Shd2Section> secs;
  if (!Materials::Shd2FindTrailer(base, size, hdrOffset, version) ||
      !Materials::Shd2ParseHeader(base, size, hdrOffset, v1Size, secs))
  {
    VkNote("DIP: заголовок v2 не разобран", false, pPath);
    return false;
  }
  const Materials::Shd2Section* spv = Materials::Shd2FindSection(secs, (unsigned)Materials::stSpirv);
  if (!spv) { VkNote("DIP: секция SPIR-V не найдена", false, pPath); return false; }

  *pIndex = Materials::Shd2U32(base + spv->offset + 8);   // первая запись
  if (!Materials::Shd2Lookup(base, size, *spv, *pIndex, *pVsOff, *pVsSize, *pPsOff, *pPsSize) ||
      !*pVsSize || !*pPsSize)
  {
    VkNote("DIP: первая запись SPIR-V-секции не читается", false, pPath);
    return false;
  }
  return true;
}

bool NvrhiBackend::RunSeamDipTest()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice || !d.commandList) return false;

  const unsigned W = 256, H = 256;
  const char* path = "Shaders/UberShaders/WaterDummyMaterial.shd";
  std::vector<unsigned char> buf;
  unsigned index = 0, vsOff = 0, vsSize = 0, psOff = 0, psSize = 0;
  if (!LoadFirstShdPair(path, &buf, &index, &vsOff, &vsSize, &psOff, &psSize)) return false;
  const unsigned char* base = &buf[0];

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

// Тест 13.1: путь «текстура -> набор 2/3 -> сэмпл -> пиксель» через seam.
// SkyMaterial #1 (живая комбинация, DoDepthOutValue=1): VS — World (c0),
// ViewProjection (c8), uvOffset (VSHADER_LOCALCONST0=c20); PS — одна текстура
// s0, один сэмплер s0, outputMul (SAMPLER0_MUL=c20) и outputAdd (SAMPLER0_ADD=c21).
// Ожидание: весь кадр залит цветом текстуры (умножение на 1, сложение с 0) —
// если бы текстура не доехала, в наборе лежала бы dummy 1x1 чёрная и пиксели
// остались бы нулевыми.
bool NvrhiBackend::RunTextureDipTest()
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice || !d.commandList) return false;

  const unsigned W = 256, H = 256;
  const char* path = "Shaders/UberShaders/SkyMaterial.shd";
  std::vector<unsigned char> buf;
  unsigned index = 0, vsOff = 0, vsSize = 0, psOff = 0, psSize = 0;
  if (!LoadFirstShdPair(path, &buf, &index, &vsOff, &vsSize, &psOff, &psSize)) return false;
  const unsigned char* base = &buf[0];

  static const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  static const float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
  static const float kOne[4] = { 1.f, 1.f, 1.f, 1.f };
  SetVertexShaderConstants(0, 4, kIdentity);    // WORLD (c0)
  SetVertexShaderConstants(8, 4, kIdentity);    // VIEWPROJECTION (c8)
  SetVertexShaderConstants(20, 1, kZero);       // uvOffset (c20)
  SetPixelShaderConstants(20, 1, kOne);         // outputMul (c20)
  SetPixelShaderConstants(21, 1, kZero);        // outputAdd (c21)

  // 8x8 однотонная: байты B,G,R,A = 0,64,255,255. Формат BGRA8_UNORM =
  // VK_FORMAT_B8G8R8A8_UNORM, семпл отдаёт (R,G,B,A) = (255,64,0,255).
  // Загрузка — через seam (LockTexture2D/UnlockTexture2D), шаг строки берём
  // тот, что вернул бэкенд
  const unsigned TSZ = 8;
  HTexture tex;
  if (FAILED(CreateTexture2D(TSZ, TSZ, 1, usageNone, bfA8R8G8B8, BACKEND_POOL_DEFAULT, &tex)))
  { VkNote("TexDIP: CreateTexture2D", false, 0); return false; }
  void* pPix = 0;
  unsigned texPitch = 0;
  if (FAILED(LockTexture2D(tex, 0, 0, LOCK_DISCARD, &pPix, &texPitch)) || !pPix || !texPitch)
  { VkNote("TexDIP: LockTexture2D", false, 0); return false; }
  for (unsigned y = 0; y < TSZ; ++y)
  {
    unsigned char* row = (unsigned char*)pPix + (size_t)y * texPitch;
    for (unsigned x = 0; x < TSZ; ++x)
    {
      row[x * 4u] = 0; row[x * 4u + 1] = 64; row[x * 4u + 2] = 255; row[x * 4u + 3] = 255;
    }
  }
  if (FAILED(UnlockTexture2D(tex, 0))) { VkNote("TexDIP: UnlockTexture2D", false, 0); return false; }
  if (FAILED(SetTexture(0, tex))) { VkNote("TexDIP: SetTexture", false, 0); return false; }

  HShader vs, ps;
  if (FAILED(CreateVertexShader(base + vsOff, vsSize, &vs))) { VkNote("TexDIP: VS не создан", false, 0); return false; }
  if (FAILED(CreatePixelShader(base + psOff, psSize, &ps))) { VkNote("TexDIP: PS не создан", false, 0); return false; }
  SetVertexShader(vs);
  SetPixelShader(ps);

  HSurface rt, dsf;
  if (FAILED(CreateSurface(ssRenderTarget, W, H, bfA8R8G8B8, BACKEND_POOL_DEFAULT, &rt)))
  { VkNote("TexDIP: RT не создан", false, 0); return false; }
  if (FAILED(CreateSurface(ssDepthStencil, W, H, bfD24S8, BACKEND_POOL_DEFAULT, &dsf)))
  { VkNote("TexDIP: DS не создан", false, 0); return false; }
  if (FAILED(SetRenderTarget(0, rt))) { VkNote("TexDIP: SetRenderTarget", false, 0); return false; }
  if (FAILED(SetDepthStencil(dsf))) { VkNote("TexDIP: SetDepthStencil", false, 0); return false; }

  BackendViewport vp;
  vp.x = 0; vp.y = 0; vp.width = W; vp.height = H; vp.minZ = 0.f; vp.maxZ = 1.f;
  SetViewport(vp);
  Clear(bcTarget | bcZBuffer, 0xFF000000u, 1.f, 0);

  // треугольник покрывает весь кадр; UV в видимой области попадают в 0..1
  static const float kTri[] = {
    -1.f, -1.f, 0.5f,   0.f, 0.f,
     3.f, -1.f, 0.5f,   2.f, 0.f,
    -1.f,  3.f, 0.5f,   0.f, 2.f,
  };
  const unsigned stride = (unsigned)(sizeof(float) * 5u);
  HBuffer vb;
  if (FAILED(CreateVertexBuffer((unsigned)sizeof(kTri), BACKEND_POOL_DYNAMIC, &vb)))
  { VkNote("TexDIP: VB не создан", false, 0); return false; }
  void* pV = 0;
  if (FAILED(LockVertexBuffer(vb, 0, (unsigned)sizeof(kTri), LOCK_DISCARD, &pV)) || !pV)
  { VkNote("TexDIP: LockVertexBuffer", false, 0); return false; }
  memcpy(pV, kTri, sizeof(kTri));
  UnlockVertexBuffer(vb);

  VertexFormatDescriptor fd;
  fd.AddVertexElement(VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT3,
                                              VERETEXELEMENTUSAGE_POSITION, 0));
  fd.AddVertexElement(VertexElementDescriptor(0, 12, VERTEXELEMENTTYPE_FLOAT2,
                                              VERETEXELEMENTUSAGE_TEXCOORD, 0));
  HInputLayout il;
  if (FAILED(CreateInputLayout(fd, &il))) { VkNote("TexDIP: раскладка не создана", false, 0); return false; }
  if (FAILED(SetInputLayout(il))) { VkNote("TexDIP: SetInputLayout", false, 0); return false; }
  if (FAILED(SetStreamSource(0, vb, 0, stride))) { VkNote("TexDIP: SetStreamSource", false, 0); return false; }
  SetInstanceCount(0);

  const HRESULT hrDraw = Draw(RENDERPRIMITIVE_TRIANGLELIST, 0, 1);
  if (FAILED(hrDraw))
  {
    char det[64];
    snprintf(det, sizeof(det), "hr=0x%08X", (unsigned)hrDraw);
    VkNote("TexDIP: Draw", false, det);
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
    VkNote("TexDIP: ReadSurface", false, det);
    return false;
  }

  // считаем пиксели, совпадающие с ожидаемым цветом текстуры (допуск на
  // линейную фильтрацию по краям не нужен: текстура однотонная)
  unsigned painted = 0, exact = 0;
  for (unsigned y = 0; y < H; ++y)
    for (unsigned x = 0; x < W; ++x)
    {
      const unsigned char* p = &px[(size_t)y * W * 4 + (size_t)x * 4];
      if (p[2]) ++painted;
      if (p[2] > 250u && p[1] > 60u && p[1] < 68u && p[0] < 4u) ++exact;
    }
  const unsigned char* s = &px[((size_t)(H / 2)) * W * 4 + (size_t)(W / 2) * 4];
  char det[224];
  snprintf(det, sizeof(det), "комбинация #%u, блобы VS %u Б / PS %u Б, питч %u; залито %u/%u, "
                           "цвет текстуры %u/%u; выборка BGRA=%u,%u,%u,%u",
           index, vsSize, psSize, texPitch, painted, W * H, exact, painted,
           s[0], s[1], s[2], s[3]);
  const bool ok = (painted > (W * H * 9u) / 10u) && (exact > (W * H * 9u) / 10u);
  VkNote(ok ? "TexDIP: текстура доехала" : "TexDIP: цвета текстуры нет", ok, det);
  return ok;
}

} // namespace Render
