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
#include <intrin.h>       // _ReturnAddress — RVA вызывающего кода (диагностика шага 13.4c)
#include <cstdio>         // FILE — рекордер вывода (шаг 14)
#include "nvrhibackend.h"
#include "shadercontainer.h"                              // формат .shd v2
// Модель состояний конвейера несёт ЧИСЛА D3D9 (см. комментарий в
// renderbackend.h), поэтому переводчику нужны константы d3d9types.h. Это
// временная зависимость: на этапе 4, когда D3D9-путь уйдёт, менеджер состояний
// станет нейтральным, и include исчезнет вместе с переводчиком.
#include "Vendor/DirectX/Include/d3d9types.h"
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

#include "stretch_shaders_spv.h"   // внутренний блит со скейлом (StretchRect)

#include <algorithm>
#include <thread>
#include <mutex>                      // apiMutex: два потока клиента на одном командном списке
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// Рекурсивная блокировка API бэкенда (см. NvrhiBackendImpl::apiMutex).
// Не берётся только в сторожевом потоке и в сводках — иначе диагностика
// встанет вместе с зависшим рендером.
struct VkApiGuard
{
  std::recursive_mutex& m;
  explicit VkApiGuard(std::recursive_mutex& mtx) : m(mtx) { m.lock(); }
  ~VkApiGuard() { m.unlock(); }
};
#define VK_API_GUARD  VkApiGuard vkApiGuard_(m_pImpl->apiMutex)


VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Render
{

//////////////////////////////////////////////////////////////////////////
// Учёт незакрытых методов. Порядок списка = порядок, в котором методы идут в
// seam'е; имена попадают в DebugTrace, так что дамп читается как план работ.
// Шаг 12b снял из списка всё, что реализовано (ресурсы, биндинги, вывод,
// ReadSurface); ниже остались только действительно незакрытые.
// Шаг 13.1 снял текстуры (создание 2D/cube, привязка к слоту сэмплера,
// UpdateTexture). Шаг 13.3 снял последнее: UP-пути (кольцо-буфер +
// writeBuffer с destOffsetBytes) и вспомогательные операции поверх
// поверхностей (ColorFill/StretchRect/GetRenderTargetData/GetRenderTarget).
// Список пуст: machinery оставлена на случай новой заглушки — чтобы список
// заглушек оставался машиночитаемым планом работ.
#define NVRHI_STUB_LIST(X)

enum EStubId
{
#define NVRHI_STUB_ENUM(n) st_##n,
  NVRHI_STUB_LIST(NVRHI_STUB_ENUM)
#undef NVRHI_STUB_ENUM
    st_count
};

// размер выводится из инициализаторов: при пустом списке остаётся один
// нулевой элемент (нулевой массив C++ запрещает), индексы st_* не съезжают
static const char* const kStubNames[] = {
#define NVRHI_STUB_NAME(n) #n,
  NVRHI_STUB_LIST(NVRHI_STUB_NAME)
#undef NVRHI_STUB_NAME
    ""
};

//////////////////////////////////////////////////////////////////////////
// Ручки seam'а = индекс+1 в таблицах Impl (0 = «нет»). 8 потоков вершин —
// столько клиент реально использует (замер шага 9: MapStream поднимает логический
// номер при инстансном режиме, физический слот инстанса — отдельный).
static const unsigned kNoRes = 0xffffffffu;
static const unsigned kMaxVertexStreams = 8;
// Шаг счётчика 0..N-1 для режима SetInstanceSource без буфера (int, не float:
// раскладка читает младшие 16 бит — грабля InstancedMeshResource::Init).
static const unsigned kInstCounterStride = 4;

// см. определение ниже (нужно раньше: defSampler создаётся в Init)
static bool SamplerLod0Forced();

// вид ресурса из SPIR-V (SpvResScan) — тот же набор, что у сканера спайка
enum EResKind { rkSampler = 0, rkTexture, rkUbo, rkSsbo, rkOther };
// слотов сэмплера у D3D9 16 (GlobalMasks.h использует s0..s12)
static const unsigned kMaxSamplerSlots = 16;

//////////////////////////////////////////////////////////////////////////
// Шаг 13.4c: владение ручками (docs/PLAN_NVRHI.md §3.z).
//
// Клиент хранит seam-ручки в умных указателях DX*Ref (Src/Render/DxIntrusivePtr.h:53):
// политика вызывает AddRef()/Release() ЧЕРЕЗ vtable IUnknown, а учёт ресурсов
// выключён (`//#define DX_RESOURCE_CONTROL`, DxResourcesControl.h:6 ->
// RegisterDXPtr/UnregisterDXPtr пустые макросы), т.е. над ручкой совершается
// ровно два COM-вызова. Поэтому ручка бэкенда — объект, чьи первые три
// виртуальных слота = QueryInterface/AddRef/Release (тот же порядок, что у
// IUnknown: порядок объявлений задаёт порядок слотов). Собственного наследника
// IUnknown здесь нет: вендорный DX SDK не даёт unknwn.h в этом графе, а нам
// нужны только три слота.
//
// QueryInterface отвечает E_NOINTERFACE — никакая COM-проверка не должна принять
// нашу ручку за D3D-объект. Магическое поле отделяет наши ручки от настоящих
// COM-указателей: DDS-текстуры до сих пор создаёт D3D9 (`TextureManager.cpp:483
// D3DXCreateTextureFromFileInMemoryEx(GetDevice(), ...)`), и такой указатель
// приходит в SetTexture — вычитать из него индекс нельзя (чтение мусора дало бы
// «всё работает, но не та текстура»).//
// Одна ручка на индекс (как один объект в D3D9): GetDepthStencil/GetRenderTarget
// возвращают тот же объект с дополнительной ссылкой, а не новый.
// Шаг 13.4c: клиент местами зовёт по полученной от seam'а ручке методы своего
// COM-интерфейса (IDirect3DTexture9 / IDirect3DSurface9 / …). У нашей ручки таких
// слотов нет, вызов уходит за конец vtable = мусор (замер 2026-10-16: minidump с
// rip=0, поток рендера встал на загрузке карты, в логе ни строки после
// последнего обращения к бэкенду). Пады занимают слоты 3..29 и печатают номер:
// номер совпадает с индексом метода D3D9 (порядок объявлений в d3d9.h), т.е. по
// логу видно, какого именно метода seam'а не хватает. Деструктор поэтому
// НЕ виртуальный — иначе он занял бы слоты 3 и 4 (GetDevice / SetPrivateData).
static unsigned g_seamPadHits = 0;
// pRet — адрес вызывающего кода (_ReturnAddress), печатается как RVA: по нему
// находится точное место в клиенте (objdump по exe, base = GetModuleHandle(0)).
struct SeamHandle;
static void SeamPadCalled(unsigned slot, const struct SeamHandle* pH, void* pRet);

#define SEAM_PAD_SLOT(n) virtual long seamPad##n() { SeamPadCalled(n, this, _ReturnAddress()); return 0; }

struct SeamUnknown
{
  virtual long QueryInterface(const void* iid, void** ppvObject) = 0;
  virtual unsigned long AddRef() = 0;
  virtual unsigned long Release() = 0;
  ~SeamUnknown() {}
};

struct NvrhiBackendImpl;   // определение ниже

struct SeamHandle : public SeamUnknown
{
  enum { kMagic = 0x5345414D,      // 'SEAM'
         // Шаг 13.4d: освобождённая обёртка НЕ удаляется, а помечается этим
         // значением (см. FreeHandle). Без метки освобождённая ручка выглядит
         // как «ресурс создан вне seam'а»: магия читается из уже чужой памяти,
         // и настоящий дефект (клиент держит ссылку на уничтоженный ресурс и
         // продолжает ею пользоваться) не отличим от непереведённого сайта.
         kFreed = 0xDEAD5245 };    // 'DEAD'+'SE'

  unsigned magic;
  NvrhiBackendImpl* impl;         // 0 после смерти бэкенда (Shutdown)
  unsigned index;                 // индекс в таблице бэкенда
  unsigned kind;                  // EHandleKind
  long refs;

  SeamHandle(NvrhiBackendImpl* i, unsigned idx, unsigned k)
    : magic((unsigned)kMagic), impl(i), index(idx), kind(k), refs(1) {}

  virtual long QueryInterface(const void*, void** ppvObject)
  {
    if (ppvObject) *ppvObject = 0;
    return 0x80004002L;           // E_NOINTERFACE
  }
  virtual unsigned long AddRef() { return (unsigned long)InterlockedIncrement(&refs); }
  virtual unsigned long Release();    // после определения NvrhiBackendImpl

  // слоты 3..29 — см. комментарий к SeamUnknown
  SEAM_PAD_SLOT(3)  SEAM_PAD_SLOT(4)  SEAM_PAD_SLOT(5)  SEAM_PAD_SLOT(6)
  SEAM_PAD_SLOT(7)  SEAM_PAD_SLOT(8)  SEAM_PAD_SLOT(9)  SEAM_PAD_SLOT(10)
  SEAM_PAD_SLOT(11) SEAM_PAD_SLOT(12) SEAM_PAD_SLOT(13) SEAM_PAD_SLOT(14)
  SEAM_PAD_SLOT(15) SEAM_PAD_SLOT(16) SEAM_PAD_SLOT(17) SEAM_PAD_SLOT(18)
  SEAM_PAD_SLOT(19) SEAM_PAD_SLOT(20) SEAM_PAD_SLOT(21) SEAM_PAD_SLOT(22)
  SEAM_PAD_SLOT(23) SEAM_PAD_SLOT(24) SEAM_PAD_SLOT(25) SEAM_PAD_SLOT(26)
  SEAM_PAD_SLOT(27) SEAM_PAD_SLOT(28) SEAM_PAD_SLOT(29)
};

// какой таблице принадлежит индекс ручки
enum EHandleKind { hkBuffer = 0, hkTexture, hkSurface, hkShader, hkLayout, hkKindCount };

// Чужую ручку (COM-указатель от D3D9-ресурса) молча превращать в индекс нельзя.
static unsigned g_hndForeign = 0;
static unsigned g_hndStale = 0;      // шаг 13.4d: обращений к освобождённой ручке

// вызов COM-метода по seam-ручке (слот вне IUnknown) — см. SEAM_PAD_SLOT
static void SeamPadCalled(unsigned slot, const SeamHandle* pH, void* pRet)
{
  ++g_seamPadHits;
  if (g_seamPadHits <= 40u)
    DebugTrace("NvrhiBackend: клиент позвал COM-метод по seam-ручке (слот %u, kind=%u index=%u refs=%ld, "
               "вызов из RVA 0x%x) — метода seam'а нет",
               slot, pH ? pH->kind : 0xffffu, pH ? pH->index : 0xffffu, pH ? pH->refs : 0,
               (unsigned)((size_t)pRet - (size_t)GetModuleHandle(0)));
}

static unsigned Hnd2Idx(const void* p)
{
  if (!p)
    return kNoRes;
  const SeamHandle* h = static_cast<const SeamHandle*>(p);
  if (h->magic == (unsigned)SeamHandle::kFreed)
  {
    ++g_hndStale;
    if (g_hndStale <= 8u)
      DebugTrace("NvrhiBackend: ручка 0x%llX уже освобождена — клиент держит мёртвый ресурс "
                 "(use-after-free, шаг 13.4d)", (unsigned long long)(size_t)p);
    return kNoRes;
  }
  if (h->magic != (unsigned)SeamHandle::kMagic)
  {
    if (g_hndForeign < 8u)
      DebugTrace("NvrhiBackend: ручка не нашего бэкенда (магия 0x%08X) — ресурс создан вне seam (шаг 13.4c)",
                 h->magic);
    ++g_hndForeign;
    return kNoRes;
  }
  return h->index;
}

// Индекс ручки сквозной внутри своего рода, поэтому ручку текстуры с тем же
// числом молча читается как запись d.surfaces: в прогоне 2026.10.16-23.02 это
// дало VUID-VkRenderingInfo-pDepthAttachment-06088 («pDepthAttachment->imageView
// references an image which was not created with VK_IMAGE_USAGE_DEPTH_STENCIL_
// ATTACHMENT_BIT … создан с TRANSFER_SRC|TRANSFER_DST|SAMPLED») — в качестве
// глубины привязана сэмплируемая ТЕКСТУРА, а проверка «не глубина» её не ловила,
// потому что брала чужую запись.
static bool KindOk(const void* p, unsigned wantKind, const char* what)
{
  if (!p) return true;
  const SeamHandle* h = static_cast<const SeamHandle*>(p);
  if (h->magic != (unsigned)SeamHandle::kMagic) return true;   // чужую уже учёл Hnd2Idx
  if (h->kind == wantKind) return true;
  static unsigned s_bad = 0u;
  if (s_bad < 6u)
  {
    ++s_bad;
    DebugTrace("NvrhiBackend: %s: ручка не того рода (kind=%u, ждём %u, index=%u)",
               what, h->kind, wantKind, h->index);
  }
  return false;
}

static void* NewHandle(NvrhiBackendImpl* impl, unsigned index, unsigned kind);  // после Impl

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
  // шаг 13.4c: COM-форменная ручка этой записи (одна на индекс) и флаг «ресурс
  // уже уничтожен» (запись не сжимается: индексы живых ручек не должны съехать)
  SeamHandle* hnd;
  bool dead;
  // Шаг 13.4e: ручка выдана GetBackBuffer. Клиент кэширует поверхность бэк-буфера
  // один раз (renderer.cpp:301-303, OnDeviceChanged), а swapchain меняет картинку
  // на каждом acquire. Замер 2026-10-16: `вывод: в бэк-буфер=0 в offscreen=107143`
  // при presents=1199 — клиент рисовал в картинку первого acquire, Present'овалась
  // другая, кадр оставался полностью чёрным (786432 пикселя BGR000000).
  // Такая ручка разрешается в ТЕКУЩУЮ картинку acquire'а (ResolveSurfaceIdx).
  bool needUpload;   // зеркало изменено чужим потоком
  // чтение картинки swapchain'а прямой копией (см. RawReadback)
  nvrhi::BufferHandle readBuf;
  bool backBufferAlias;
  SurfaceObj() : backBuffer(false), needUpload(false), fmt(nvrhi::Format::UNKNOWN), w(0), h(0),
                 seamFormat(bfUnknown), pool(BACKEND_POOL_DEFAULT), usage(usageNone),
                 depth(false), locked(false),
                 lockX(0), lockY(0), lockW(0), lockH(0), hnd(0), dead(false),
                 backBufferAlias(false) {}
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
  VkSemaphore acquired;   // с 13.4e не используется: acquire переведён на fence
  // По fence на acquire. Прежняя схема (один семафор на все acquire'ы) давала
  // VUID-vkAcquireNextImageKHR-semaphore-01779 («Semaphore must not have any
  // pending operations») и present ждал этот же семафор, чего делать нельзя.
  std::vector<std::string> enabledLayers;    // env PW_VK_LAYERS
  std::vector<const char*> enabledLayerPtrs; // указатели для VkInstanceCreateInfo
  std::vector<VkFence> acquireFences;
  unsigned fenceRot;
  // Картинки swapchain'а нужны руками: NVRHI не умеет переводить макет в
  // PRESENT_SRC_KHR, а без него present невалиден
  // (VUID-VkPresentInfoKHR-pImageIndices-01430).
  std::vector<VkImage> backImages;
  unsigned imageIndex;

  nvrhi::DeviceHandle nvrhiDevice;
  VkMsgLog msgLog;
  nvrhi::CommandListHandle commandList;

  std::vector<nvrhi::TextureHandle> backTextures;   // по одному на картинку swapchain'а
  // Индексы картинок swapchain'а в d.surfaces. Отдельный список нужен потому,
  // что пересоздание swapchain'а НЕ имеет права чистить d.surfaces целиком:
  // там же поверхности, созданные клиентом (RT/DS), а их ручки — индексы.
  std::vector<unsigned> backSurfaceIdx;
  // Индекс нашей поверхности глубины бэк-буфера (создаётся вместе с swapchain'ом).
  // SetDepthStencil(NULL) по контракту D3D9 = «штатная глубина устройства», и
  // без неё вывод в бэк-буфер с тестом глубины невозможен (замер 2026-10-16:
  // SetDepthStencil NULL=899, чужих=899, своих=0).
  unsigned backDepthIdx;
  unsigned dsSubIdx;      // подменная глубина размера RT (см. DrawImpl)
  std::vector<SurfaceObj> surfaces;                 // индекс + 1 = значение HSurface.p
  unsigned currentSurface;                          // поверхность текущего бэк-буфера

  VkPhysicalDeviceProperties props;
  char deviceName[128];

  // список заглушек может быть пуст (шаг 13.3 закрыл последние) — массив
  // нулевого размера C++ запрещает, поэтому минимум 1 слот
  unsigned stubCalls[st_count ? st_count : 1];
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
    SeamHandle* hnd;
    bool dead;
    // Шаг 13.4e: VB/IB режутся из слэба (см. Slab) — buf тогда заимствованный
    // handle слэба, а slab/off — его индекс и смещение; kNoRes = отдельный буфер
    bool needUpload;   // зеркало изменено чужим потоком, загрузка отложена
    unsigned slab;
    unsigned off;
    BufferObj() : isIndex(false), index32(false), locked(false), hnd(0), dead(false),
                  needUpload(false), slab((unsigned)kNoRes), off(0) {}
  };
  // Ресурс, объявленный шейдером: набор, binding и вид. binding для текстур и
  // сэмплеров = слот D3D9 (писатель tools/spirv_probe/shim.py размечает
  // `register(tN, space2)`/`register(sN, space3)` именно по номеру из
  // исходника; замер корпуса: RoadMap s0 -> set 2 binding 0), поэтому
  // SetTexture(N, tex) кладёт текстуру прямо в binding N.
  // dim — Dim из OpTypeImage (SPV: 0=1D, 1=2D, 2=3D, 3=Cube, 4=Rect, 5=Buffer,
  // 6=SubpassData), count — длина массива ресурсов (OpTypeArray). Нужны из-за строгой валидации Vulkan:
  // VUID-vkCmdDrawIndexed-viewType-07752 (view 2D, а шейдер ждёт Cube) и
  // VUID-vkCmdDrawIndexed-None-08114 (элемент массива дескрипторов никогда не
  // заполнялся — layout объявлен с count=1, а SPIR-V просит массив).
  struct ResItem
  {
    unsigned set, binding, kind;
    unsigned dim, count;
    ResItem() : set(0), binding(0), kind(0), dim(0), count(1) {}
  };
  struct ShaderObj
  {
    nvrhi::ShaderHandle sh;
    std::string entry;                // точка входа из OpEntryPoint
    // входы VS: имя семантики из SPIR-V (in.var.POSITION) -> location
    std::vector<std::pair<std::string, unsigned> > inputs;
    std::vector<unsigned> inputKinds; // класс скаляра входа (0 float, 1 sint, 2 uint)
    std::vector<ResItem> res;         // ресурсы этой стадии (шаг 13.1)
    // байты SPIR-V, из которых создан шейдер — только при включённом рекорде
    // (реплеер обязан компилировать ровно те же байты, иначе вывод про
    // компилятор не воспроизведётся)
    std::vector<unsigned char> spv;
    SeamHandle* hnd;
    bool dead;
    ShaderObj() : sh(0), hnd(0), dead(false) {}
  };
  struct LayoutObj
  {
    nvrhi::InputLayoutHandle il;
    // копия описания клиента — раскладку собираем позже, чем CreateInputLayout:
    // шаг потока известен только на момент вывода
    std::vector<VertexElementDescriptor> elems;
    // готовый массив nvrhi-атрибутов, которым реально построена раскладка
    // (location = позиция в массиве). Нужен рекордеру/реплееру: пересобирать
    // сопоставление семантик в реплеере нельзя — он обязан получить ровно ту же
    // раскладку, что видел драйвер.
    std::vector<nvrhi::VertexAttributeDesc> desc;
    SeamHandle* hnd;
    bool dead;
    LayoutObj() : il(0), hnd(0), dead(false) {}
  };
  struct PipeObj
  {
    nvrhi::GraphicsPipelineHandle pipe;
    nvrhi::FramebufferHandle fb;
    unsigned vs, ps, layout, rt, ds, prim;
    // подпись шагов потоков, для которой построена раскладка (см. IlEntry):
    // без неё два VB с разными шагами дают один и тот же ключ пайплайна
    unsigned long long lsig;
    // хэш модели состояний (шаг 13.4b): состояния — часть объекта пайплайна на
    // Vulkan (blend/depth/stencil/raster), поэтому без них два вывода с разными
    // состояниями получили бы один пайплайн молча
    unsigned long long stateHash;
  };
  std::vector<BufferObj> buffers;
  // Шаг 13.4e: крупные слэбы для вершинных/индексных буферов. NVRHI делает по
  // одному vkAllocateMemory на ресурс (Vendor/NVRHI/src/vulkan/vulkan-allocator.cpp:56
  // — под-аллокации нет), а драйвер поднимает на выделение не менее ~2 МБ:
  // замер 2026-10-16 — 11 636 отображений nvidiactl (~2 МБ каждое) = 22,7 ГБ RSS
  // при 0,95 ГБ использованной видеопамяти (хост 31 ГБ), загрузка карты уходила
  // в своп и вставала на `_.baked_terrain`. Клиент за прогон создаёт тысячи
  // маленьких VB/IB (буферы=7156), поэтому они режутся отсюда.
  struct Slab
  {
    nvrhi::BufferHandle buf;
    unsigned size;
    unsigned cursor;
    std::vector<std::pair<unsigned, unsigned> > holes;   // (смещение, размер)
    Slab() : size(0), cursor(0) {}
  };
  std::vector<Slab> slabs;
  unsigned slabBytes;
  unsigned slabCreated, slabHoleReuses, slabAllocFail, slabFreed;
  void Watchdog();
  bool SlabAlloc(unsigned bytes, unsigned* pSlab, unsigned* pOffset);
  void SlabFree(unsigned slab, unsigned offset, unsigned bytes);
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
    // Шаг 13.4d: какое зеркало сейчас в руках (уровень mip-цепочки, число строк
    // и шаг строки). Зеркало одно и оно переиспользуется по уровням: загрузчик
    // DDS и подкрашивание mip'ов блокируют уровни строго последовательно
    // (Lock -> заполнение -> Unlock), поэтому хранить зеркало каждого уровня
    // незачем, а вот перепутать уровень при Unlock — можно.
    bool needUpload;   // зеркало уровня изменено чужим потоком
    unsigned upLevel, upFace, upRowBytes;   // параметры отложенной загрузки
    unsigned lockLevel, lockRows, lockRowBytes, lockFace;
    // Диагностика (замер прогона 80): у ВСЕХ текстур с mipLevels>=2 GPU-содержимое
    // чёрное, а CPU-снимка (TXDP) по ним нет. Счётчик отвечает на вопрос, была ли
    // текстура загружена вообще: 0 = клиент её не заполнял (значит содержимое
    // должно приходить другим путём — GPU-копией/UpdateTexture — или не приходит).
    unsigned uploads = 0;
    // Шаг 13.4d: поверхность уровня (GetTextureSurface) — ОДИН объект на пару
    // (текстура, уровень), как GetSurfaceLevel в D3D9. Клиент кэширует сырой
    // указатель (SmartRenderer::pDefaultRT0, curColorSurface[]), поэтому новый
    // объект на каждый вызов означал бы работу по освобождённой ручке
    // (замер: мёртвых=4495 за прогон, вывод=0). kNoRes — ещё не создавалась.
    std::vector<unsigned> levelSurf;
    bool locked;
    long lockX, lockY, lockW, lockH;
    SeamHandle* hnd;
    bool dead;
    TextureObj() : fmt(nvrhi::Format::UNKNOWN), w(0), h(0), mipLevels(1), cube(false),
                   seamFormat(bfUnknown), usage(usageNone), pool(BACKEND_POOL_DEFAULT),
                   needUpload(false), upLevel(0), upFace(0), upRowBytes(0),
                   lockLevel(0), lockRows(0), lockRowBytes(0), lockFace(0),
                   locked(false), lockX(0), lockY(0), lockW(0), lockH(0), hnd(0), dead(false) {}
  };
  struct ResMapObj
  {
    // по одному layout на набор: setRegisterSpaceAndDescriptorSet задаёт ОДИН
    // registerSpace на layout, а NVRHI рвёт «Can't have multiple binding sets
    // with the same registerSpace» (vulkan-resource-bindings.cpp:1031)
    nvrhi::BindingLayoutHandle layoutTex, layoutSmp, layoutSsbo;
    // набор 1 (SSBO) — один на resMap: SSBO в seam ещё не переведены (шаг 13.5),
    // но layout в пайплайн входит, и не привязанный набор = невалидный вывод
    // (VUID-vkCmdDrawIndexed-None-08600 «VkPipeline uses set 1 but that set is
    // not bound») — вывод в такой пасс не оставляет пикселей.
    nvrhi::BindingSetHandle bsSsbo;
    std::vector<ResItem> items;
    unsigned vs, ps;
    ResMapObj() : vs(kNoRes), ps(kNoRes) {}
  };
  std::vector<TextureObj> textures;
  std::vector<ResMapObj> resMaps;
  // подпись (набор привязок текстур) -> готовый набор; линейный поиск: за кадр
  // уникальных подписей мало (замер шага 8: 85 активных ключей на кадр)
  std::vector<std::pair<unsigned long long, nvrhi::BindingSetHandle> > setCache;
  // Поток загрузки карты (замер 2026-10-16: поток (0036) — рендер, (0368) —
  // загрузка `_.baked_terrain`) создаёт буферы и грузит в них биты ПОСРЕДИ
  // вывода рендер-потока. В D3D9 это переживалось (рантайм сериализует), здесь
  // два потока лезут в ОДИН командный список NVRHI: валидатор даёт
  // `vkCmdPipelineBarrier2(): THREADING ERROR : object of type VkCommandPool is
  // simultaneously used in current thread ... and thread ...`, состояние
  // командного списка портится — отсюда VUID-vkCmdEndRendering-renderpass
  // («endRendering вне активного render pass»), VUID-vkCmdDraw-renderpass и
  // падение. Рекурсивный мьютекс на всех точках входа seam'а.
  std::recursive_mutex apiMutex;
  // Поток-владелец командного списка (создал бэкенд = рендер-поток).
  // Vulkan-командный список ОДНОПОТОЧНЫЙ: запись из двух потоков даёт
  // `vkCmdPipelineBarrier2(): THREADING ERROR : object of type VkCommandPool is
  // simultaneously used in current thread ... and thread ...` и порчу состояния
  // списка — отсюда VUID-vkCmdEndRendering-renderpass («endRendering вне
  // активного render pass») и VUID-vkCmdDraw-renderpass. Замер 2026-10-16:
  // поток (0036) рисует, поток (0368) пачками грузит `_.baked_terrain`
  // (CreateVertexBuffer/LockBuffer/UnlockBuffer). Загрузка с чужого потока
  // остаётся в зеркале и исполняется владельцем (FlushPendingUploads).
  unsigned ownerThread;
  unsigned threadSeen[6];
  unsigned threadSeenCount;
  // Загрузка текстур с чужого потока (шаг 13.1/13.2). Прежняя отложенная
  // очередь (pendTex: индекс текстуры + один слот уровня на текстуру) была
  // одновременно и причиной чёрного кадра, и причиной того, что её нельзя было
  // починить «в лоб»:
  //   1) слот уровня ОДИН на текстуру, а DDS-загрузчик (ReadDDS.cpp
  //      FaceHolder::Fill) блокирует уровни 0..mipCount-1 подряд — каждая
  //      следующая блокировка затирала предыдущую, на GPU уходил только
  //      ПОСЛЕДНИЙ (1x1) уровень, уровень 0 оставался пустым. Замер TXGP
  //      прогона 80: 200/200 текстур с mipLevels>=2 на GPU нулевые при загруз=1.
  //   2) попытка хранить в очереди байты каждого уровня (прогоны 83-88) встала:
  //      поток загрузки опережает рендер-поток (который в это время ждёт
  //      ресурсы и в seam не заходит), очередь выросла до 9,8 ГБ RSS, клиент
  //      остановился на загрузке (лог обрывается, VkResflip не печатается).
  // Поэтому чужой поток грузит САМ, своим командным списком: Vulkan позволяет
  // писать разные command buffer из разных потоков (однопо́точный общий
  // d.commandList — как раз причина прежней отложенной очереди), а пакет
  // закрывается при переходе на следующую текстуру или по порогу байт.
  struct ThreadUpload
  {
    nvrhi::CommandListHandle cl;
    size_t bytes;
    unsigned entries;
    unsigned lastTex;
    // список открыт ровно ОДИН раз на пакет: CommandList::open()
    // (Vendor/NVRHI/src/vulkan/vulkan-commandlist.cpp) берёт ИЗ ПУЛА НОВЫЙ
    // командный буфер и начинает запись в него, а предыдущий m_CurrentCmdBuf
    // просто забывается — все уже записанные в пакет загрузки теряются
    // (замер прогона 95: у #101 lvl0 дошёл до GPU только потому, что TexCheck
    // принудительно закрыл пакет; у #102 дошёл лишь последний уровень).
    bool open;
    ThreadUpload() : bytes(0), entries(0), lastTex((unsigned)kNoRes), open(false) {}
  };
  std::map<unsigned, ThreadUpload> threadUpload;
  std::vector<unsigned> pendBuf, pendSrf;
  nvrhi::SamplerHandle defSampler;    // состояние по умолчанию: слот, которого модель не касалась

  // Внутренний блит со скейлом (StretchRect). Клиент зовёт StretchRect с другим
  // размером приёмника: цепочка mip у текстур D3DUSAGE_AUTOGENMIPMAP
  // (renderer.cpp:1042, d3d9backend.cpp:623-664), водяная карта
  // (WaterManager.cpp:270), пост-эффекты (renderer.cpp:1102). У copyTexture
  // скейла нет, поэтому блит рисуется квадом; шейдеры вшиты в бинарник
  // (stretch_shaders_spv.h), чтобы путь не зависел от пайла и OnlineShaders.
  nvrhi::ShaderHandle stretchVs, stretchPs;
  nvrhi::InputLayoutHandle stretchIl;
  nvrhi::BufferHandle stretchVb;
  nvrhi::SamplerHandle stretchSmp[2];  // 0 = линейный, 1 = точка (bfPoint)
  nvrhi::BindingLayoutHandle stretchLayTex, stretchLaySmp;
  nvrhi::BindingSetHandle stretchBsTex, stretchBsSmp;
  unsigned long long stretchBsKey = 0;
  bool stretchFailed = false;          // инициализация не удалась — не повторять
  unsigned stretchBlits = 0;
  unsigned stretchScaleFail = 0;
  std::vector<std::pair<unsigned long long, std::pair<nvrhi::FramebufferHandle,
      nvrhi::GraphicsPipelineHandle> > > stretchPipes;
  nvrhi::TextureHandle dummyTex;      // в несвязанный слот кладётся 1x1
  nvrhi::TextureHandle dummyCubeTex;  // замена, когда шейдер ждёт cube
  nvrhi::BufferHandle dummyVb;        // замена пустого вершинного слота
  nvrhi::BufferHandle dummySsbo;      // заглушка набора 1 (SSBO)
  nvrhi::BufferHandle instCounter;    // счётчик 0..N-1 (SetInstanceSource без буфера)
  // Отладка 2026-10-17: дамп загружаемых текстур (см. TexDumpLevel0)
  FILE* texDump = nullptr;
  unsigned texDumpN = 0;
  unsigned instCounterEntries = 0;    // записей в instCounter
  unsigned texBound[16];              // слот сэмплера D3D9 -> индекс текстуры
  unsigned setsCreated, texMissing, resMapsCreated;
  unsigned ssboSetsCreated;
  unsigned texDimMismatch;   // шейдер ждёт cube/2D, привязано другое
  unsigned vbHoles;          // пустые вершинные слоты (подменены dummy)
  unsigned dsNonDepth;       // в глубину привязана не-глубина
  unsigned readbackRaw;      // сколько чтений сделано прямой копией
  unsigned shotAt;           // env PW_VK_SHOT: кадр, на котором снимаем
  unsigned shotProbe;        // env PW_VK_SHOT_PROBE: перед снимком залить картинку
  unsigned drawDiag;         // сколько выводов описано подробно (диагностика пустого кадра)
  std::vector<unsigned> rtDrawCount;   // сколько выводов в поверхность (индекс = индекс записи)

  // Рекордер вывода (шаг 14, env PW_VK_RECORD=<файл>). Нужен потому, что
  // RenderDoc ловит кадры ТОЛЬКО нативно: под Wine слой активен (winevulkan
  // идёт через хостовый загрузчик), но гость — Windows PE — не может вызвать
  // RENDERDOC_GetAPI, а без app-API слой кадр не сериализует (замер
  // 2026-10-17: слой в списке инстанса, клиент жив 120+ с, .rdc нет нигде).
  // Поэтому выводы пишутся в файл, а рисует их нативный реплеер
  // tools/render_replay — он и снимает капчу.
  bool recOn;
  FILE* recFile;
  unsigned recMax;        // лимит выводов (env PW_VK_RECORD_MAX)
  unsigned recSkip;       // сколько выводов пропустить в начале (env PW_VK_RECORD_SKIP)
  unsigned recSeen;       // сколько выводов вообще увидели (счётчик пропуска)
  unsigned recDraws;      // сколько выводов записано
  unsigned recObjs;       // сколько ресурсов выгружено
  unsigned recRtW, recRtH;   // фильтр цели (env PW_VK_RECORD_RT=ШхВ), 0 = любой
  // seam-индекс -> id записи в файле (ресурс выгружается один раз)
  std::vector<std::pair<unsigned, unsigned> > recShader, recBuffer, recTexture, recLayout;

  // Модель состояний конвейера (шаг 13.4b): SetPipelineState кладёт сюда то,
  // что собрал RenderStatesManager. Хэш модели входит в ключ пайплайна, поэтому
  // сравнивать структуру полей не нужно: совпал хэш — состояния те же.
  BackendPipelineState model;
  unsigned long long modelHash;
  unsigned stateSets;
  // Сэмплеры больше не статичны: состояния различаются по слотам, поэтому
  // сэмплер кэшируется по подписи одного слота, а набор сэмплеров — по подписи
  // всех слотов (линейный поиск: уникальных состояний за бой десятки).
  std::vector<std::pair<unsigned long long, nvrhi::SamplerHandle> > samplerCache;
  std::vector<std::pair<unsigned long long, nvrhi::BindingSetHandle> > smpSetCache;
  unsigned samplersCreated, smpSetsCreated, stateUnsupported;

  // Раскладка (input layout) в Vulkan ЗАШИВАЕТ шаг потока: у
  // createInputLayout шаг — часть описания потока (vulkan-shader.cpp:
  // bindingMap[bufferIndex] = {stride=elementStride, ...}). Кэш по одному лишь
  // индексу раскладки молча отдаёт чужую — второй VB с другим шагом получает
  // первую раскладку (очередь шага 13, п.8). Ключ — (индекс раскладки,
  // подпись шагов потоков).
  struct IlEntry
  {
    unsigned layout;
    unsigned long long sig;
    unsigned vs;                      // location'ы назначает DXC — они свои у каждого VS
    nvrhi::InputLayoutHandle il;
  };
  std::vector<IlEntry> ilCache;
  unsigned layoutsBuilt;
  unsigned ilPadHoles;                // сколько location пришлось заполнить дубликатами
  unsigned ilDropped;                 // сколько атрибутов закрыл НЕ этот VS (см. BuildInputLayout)
  unsigned ilDiag;

  // UP-пути: приведённой памяти у Vulkan нет, данные кладутся в кольцо; у
  // writeBuffer есть destOffsetBytes (nvrhi.h:3402), поэтому одно кольцо на VB
  // и одно на IB, а не отдельный буфер на вызов.
  nvrhi::BufferHandle upVb, upIb;
  unsigned long long upVbOff, upIbOff;
  unsigned upDraws, upFlushes;
  // SetRenderTarget(index, ...) -> поверхность; нужна для GetRenderTarget
  unsigned rtSlot[8];

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
  // Шаг 13.4c: в режиме «переворота» (pBackend = этот бэкенд) заливка бэк-буфера
  // в Present запрещена — кадр рисует клиент своими Clear/Draw, а заливка стёрла
  // бы его. В спайковом режиме (12a) заливка и есть кадр.
  bool clearOnPresent;
  unsigned draws, pipesCreated, pipeHits, uploads;
  // Шаг 13.4d: байт, загруженных в GPU с последней отправки/ожидания, и число
  // принудительных waitIdle (см. NvrhiBackend::NoteUpload)
  size_t uploadBytes;
  size_t uploadPending;   // байт с последней отправки списка
  unsigned uploadFlushes; // отправленных списков из-за порога байт
  // Шаг 13.1: отправки пакетов загрузки чужого потока (свой командный список);
  // без счётчика неотличимо «записали в список» от «дошло до GPU» (замер прогона
  // 89: TexUp есть, загруз=319095, а TXGP всё равно нулевой)
  unsigned uploadSubmits = 0;
  // Шаг 13.4e: NVRHI берёт на open() командный буфер из пула очереди, а пул
  // пополняется только после исполнения списка на GPU
  // (vulkan-queue.cpp: getOrCreateCommandBuffer / returnCommandBuffersToPool).
  // Если CPU порождает списки быстрее, чем GPU их исполняет, пул пустеет и
  // создаются новые командные буферы без предела; на winevulkan это рвёт процесс
  // ассертом `Assertion failed: !status && "vkBeginCommandBuffer",
  // dlls/winevulkan/loader_thunks.c, line 114` (замер 2026-10-16). Значит число
  // необработанных отправок ограничиваем явно, а не только порогом байт.
  unsigned flushesSinceIdle;
  unsigned uploadWaits;
  // Шаг 13.4d: причины, по которым вывод до пайплайна не дошёл. Обязательны:
  // без них «вывод=0» неотличим от «клиент вообще не рисует» (замер
  // 2026-10-16: 202 DrawImpl, 0 пайплайнов, в логе ни одной строки об отказе).
  unsigned drawSkipPrim, drawSkipVs, drawSkipPs, drawSkipRt;
  // Шаг 13.4e: вывод в бэк-буфер отдельно от вывода в offscreen. Кадр чёрный при
  // 119 000 успешных draw'ов — значит сцены в бэк-буфере нет вовсе, и надо
  // отличить «не чего рисовать» от «рисуем не туда».
  unsigned drawsBack, drawsOff;
  // SetRenderTarget: NULL = бэк-буфер (контракт D3D9), чужая ручка = ресурс,
  // созданный вне seam (D3DX-загрузка DDS, PostFX). Различать нужно: первое
  // лечится привязкой нашей картинки, второе — только переводом создания на seam.
  unsigned rtSetNull, rtSetForeign, rtSetOwn, rtSetEmpty;
  unsigned dsSetNull, dsSetForeign, dsSetOwn, dsSetEmpty;

  // Шаг 13.4c: владение. SeamSlot — слот ручки таблицы; FreeHandle вызывается из
  // SeamHandle::Release при последнем сбросе ссылки; RetireHandle помечает ресурс
  // мёртвым, НЕ трогая ручку (клиент ещё держит ссылку — иначе Release() уйдёт в
  // освобождённую память).
  SeamHandle** SeamSlot(unsigned kind, unsigned index);
  void FreeHandle(unsigned kind, unsigned index);
  void RetireHandle(unsigned kind, unsigned index);
  void InvalidateRefsTo(unsigned kind, unsigned index);
  unsigned handlesFreed[hkKindCount];
  // шаг 13.4d: обёртки с нулём ссылок держатся до конца бэкенда (диагностика
  // use-after-free) и удаляются в ~NvrhiBackend
  std::vector<SeamHandle*> deadHandles;

  NvrhiBackendImpl()
    : hVulkan(0), gipa(0), instance(0), physical(0), device(0), queue(0), queueFamily(0),
      surface(0), window(0), swapchain(0), swapFormat(VK_FORMAT_UNDEFINED),
      swapSpace(VkColorSpaceKHR(0)), presentMode(VK_PRESENT_MODE_FIFO_KHR),
      width(0), height(0), acquired(0), fenceRot(0), ownerThread(0), threadSeenCount(0),
      imageIndex(0), currentSurface(0), presents(0),
      backDepthIdx((unsigned)kNoRes), dsSubIdx((unsigned)kNoRes),
      swapRecreates(0), surfaceRecreates(0),
      uboDirty{ false, false }, curVs(kNoRes), curPs(kNoRes), curLayout(kNoRes),
      curIb(kNoRes), curRt(kNoRes), curDs(kNoRes),
      instSrc(kNoRes), instSlot(kMaxVertexStreams), instStride(0), instOffset(0),
      recording(false), draws(0), pipesCreated(0), pipeHits(0), uploads(0),
      uploadBytes(0), uploadWaits(0), uploadPending(0), uploadFlushes(0),
      flushesSinceIdle(0),
      slabBytes(64u * 1024u * 1024u),
      slabCreated(0), slabHoleReuses(0), slabAllocFail(0), slabFreed(0),
      drawSkipPrim(0), drawSkipVs(0), drawSkipPs(0), drawSkipRt(0),
      drawsBack(0), drawsOff(0),
      rtSetNull(0), rtSetForeign(0), rtSetOwn(0), rtSetEmpty(0),
      dsSetNull(0), dsSetForeign(0), dsSetOwn(0), dsSetEmpty(0),
      setsCreated(0), texMissing(0), resMapsCreated(0),
      ssboSetsCreated(0), texDimMismatch(0), vbHoles(0), dsNonDepth(0), readbackRaw(0), shotAt(0), shotProbe(0), drawDiag(0),
      recOn(false), recFile(0), recMax(0), recSkip(0), recSeen(0), recDraws(0), recObjs(0), recRtW(0), recRtH(0),
      modelHash(0ull), stateSets(0), samplersCreated(0), smpSetsCreated(0),
      stateUnsupported(0),
      layoutsBuilt(0), ilPadHoles(0), ilDropped(0), ilDiag(0), upVbOff(0), upIbOff(0), upDraws(0), upFlushes(0)
  {
    deviceName[0] = 0;
    clearOnPresent = true;
    memset(stubCalls, 0, sizeof(stubCalls));
    memset(handlesFreed, 0, sizeof(handlesFreed));
    for (int i = 0; i < 8; ++i) rtSlot[i] = kNoRes;
    for (int i = 0; i < 16; ++i) texBound[i] = kNoRes;
    for (int i = 0; i < kMaxVertexStreams; ++i)
    {
      vb[i] = kNoRes; vbStride[i] = 0; vbOffset[i] = 0;
    }
  }
};

//////////////////////////////////////////////////////////////////////////
// Шаг 13.4c: владение ручками (docs/PLAN_NVRHI.md §3.z).

SeamHandle** NvrhiBackendImpl::SeamSlot(unsigned kind, unsigned index)
{
  switch (kind)
  {
  case hkBuffer:  return index <  (unsigned)buffers.size()    ? &buffers[index].hnd  : 0;
  case hkTexture: return index <  (unsigned)textures.size()   ? &textures[index].hnd : 0;
  case hkSurface: return index <  (unsigned)surfaces.size()   ? &surfaces[index].hnd : 0;
  case hkShader:  return index <  (unsigned)shaderObjs.size() ? &shaderObjs[index].hnd : 0;
  case hkLayout:  return index <  (unsigned)layoutObjs.size() ? &layoutObjs[index].hnd : 0;
  default: break;
  }
  return 0;
}

// Уничтожает nvrhi-объект и помечает запись мёртвой. Запись НЕ удаляется из
// таблицы: индексы живут в ручках клиента, сдвиг индексов разъехал бы чужие
// ресурсы.
// Шаг 13.4d: `clear()` у std::vector НЕ отдаёт ёмкость. Ресурсы при загрузке
// карты создаются и гибнут тысячами (замер: мёртвых ручек 5995 за прогон), и
// каждое «мёртвое» зеркало держало бы байты до конца процесса: RSS клиента под
// Vulkan дорос до 25,3 ГБ (VmRSS, хост 31 ГБ, свободно 265 МБ) — из-за этого
// загрузка вала в своп и встала на `_.baked_terrain`. Поэтому зеркала
// освобождаются swap'ом с пустым вектором.
// Шаг 13.4e: простейший аллокатор слэбов (первый подходящий разрыв, потом
// бамп, потом новый слэб). Освобождение — в список разрывов со слиянием; если
// освобождается хвост, просто откатываем курсор.
bool NvrhiBackendImpl::SlabAlloc(unsigned bytes, unsigned* pSlab, unsigned* pOffset)
{
  static const unsigned kSlabAlign = 64u;
  const unsigned need = (bytes + kSlabAlign - 1u) / kSlabAlign * kSlabAlign;
  if (!need) return false;
  for (size_t s = 0; s < slabs.size(); ++s)
    for (size_t i = 0; i < slabs[s].holes.size(); ++i)
    {
      const unsigned off = slabs[s].holes[i].first, sz = slabs[s].holes[i].second;
      if (sz >= need)
      {
        if (sz == need) slabs[s].holes.erase(slabs[s].holes.begin() + (long)i);
        else { slabs[s].holes[i].first = off + need; slabs[s].holes[i].second = sz - need; }
        *pSlab = (unsigned)s; *pOffset = off;
        ++slabHoleReuses;
        return true;
      }
    }
  for (size_t s = 0; s < slabs.size(); ++s)
  {
    const unsigned off = (slabs[s].cursor + kSlabAlign - 1u) / kSlabAlign * kSlabAlign;
    if (off + need <= slabs[s].size)
    {
      slabs[s].cursor = off + need;
      *pSlab = (unsigned)s; *pOffset = off;
      return true;
    }
  }
  Slab sl;
  const unsigned sz = need > slabBytes ? need : slabBytes;
  sl.buf = nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                       .setByteSize(sz)
                                       .setIsVertexBuffer(true).setIsIndexBuffer(true)
                                       .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                       .setKeepInitialState(true)
                                       .setDebugName("seam vb/ib slab"));
  if (!sl.buf) { ++slabAllocFail; return false; }
  sl.size = sz;
  sl.cursor = need;
  slabs.push_back(sl);
  *pSlab = (unsigned)slabs.size() - 1u;
  *pOffset = 0;
  ++slabCreated;
  return true;
}

void NvrhiBackendImpl::SlabFree(unsigned slab, unsigned offset, unsigned bytes)
{
  static const unsigned kSlabAlign = 64u;
  if (slab >= slabs.size()) return;
  const unsigned need = (bytes + kSlabAlign - 1u) / kSlabAlign * kSlabAlign;
  Slab& sl = slabs[slab];
  ++slabFreed;
  if (offset + need == sl.cursor) { sl.cursor = offset; return; }   // хвост — дыру не заводим
  sl.holes.push_back(std::make_pair(offset, need));
  std::sort(sl.holes.begin(), sl.holes.end());
  for (size_t i = 0; i + 1 < sl.holes.size(); )
  {
    if (sl.holes[i].first + sl.holes[i].second == sl.holes[i + 1].first)
    {
      sl.holes[i].second += sl.holes[i + 1].second;
      sl.holes.erase(sl.holes.begin() + (long)i + 1);
    }
    else ++i;
  }
}

static void FreeMirror(std::vector<unsigned char>& v)
{
  std::vector<unsigned char>().swap(v);
}

void NvrhiBackendImpl::RetireHandle(unsigned kind, unsigned index)
{
  switch (kind)
  {
  case hkBuffer:
    if (index < buffers.size() && !buffers[index].dead)
    {
      if (buffers[index].slab != kNoRes)
        SlabFree(buffers[index].slab, buffers[index].off, (unsigned)buffers[index].cpu.size());
      buffers[index].buf = nullptr;
      FreeMirror(buffers[index].cpu);
      buffers[index].dead = true;
    }
    break;
  case hkTexture:
    if (index < textures.size() && !textures[index].dead)
    {
      textures[index].tex = nullptr;
      FreeMirror(textures[index].mirror);
      textures[index].dead = true;
    }
    break;
  case hkSurface:
    if (index < surfaces.size() && !surfaces[index].dead)
    {
      surfaces[index].tex = nullptr;
      surfaces[index].staging = nullptr;
      FreeMirror(surfaces[index].mirror);
      surfaces[index].dead = true;
    }
    break;
  case hkShader:
    if (index < shaderObjs.size() && !shaderObjs[index].dead)
    {
      shaderObjs[index].sh = nullptr;
      shaderObjs[index].inputs.clear();
      shaderObjs[index].res.clear();
      shaderObjs[index].dead = true;
    }
    break;
  case hkLayout:
    if (index < layoutObjs.size() && !layoutObjs[index].dead)
    {
      layoutObjs[index].il = nullptr;
      layoutObjs[index].elems.clear();
      layoutObjs[index].dead = true;
    }
    break;
  default:
    break;
  }
  InvalidateRefsTo(kind, index);
}

void NvrhiBackendImpl::FreeHandle(unsigned kind, unsigned index)
{
  if (kind < hkKindCount)
    ++handlesFreed[kind];
  // Шаг 13.4d: просмотр уровня живёт столько же, сколько его текстура (в D3D9
  // поверхность уровня держит сама текстура). Внутреннюю ссылку заводим в
  // GetTextureSurface, здесь отпускаем. Без этого клиент, кэширующий сырой
  // указатель (SmartRenderer::pDefaultRT0, curColorSurface[]), продолжает
  // пользоваться освобождённой ручкой: замер 2026-10-16 — мёртвых=5995 за
  // прогон, SetRenderTarget неудачен каждый кадр, вывод=0.
  if (kind == hkTexture && index < textures.size())
  {
    std::vector<unsigned> views;
    views.swap(textures[index].levelSurf);
    for (size_t i = 0; i < views.size(); ++i)
    {
      if (views[i] == kNoRes) continue;
      SeamHandle** vs = SeamSlot(hkSurface, views[i]);
      if (vs && *vs)
        (*vs)->Release();
    }
  }
  RetireHandle(kind, index);
  SeamHandle** slot = SeamSlot(kind, index);
  if (slot && *slot)
  {
    // обёртку НЕ удаляем: умные указатели клиента могут держать её до конца
    // жизни процесса, а удаление дало бы чтение freed memory вместо диагностики
    (*slot)->magic = (unsigned)SeamHandle::kFreed;
    (*slot)->index = kNoRes;
    deadHandles.push_back(*slot);
    *slot = 0;
  }
}

// Индексы не переиспользуются, поэтому кэш с мёртвым индексом не даст ложного
// попадания, но держит мёртвый nvrhi-объект — сбрасываем кэши целиком
// (консервативно; частота гибели ресурсов в живом клиенте низкая).
void NvrhiBackendImpl::InvalidateRefsTo(unsigned kind, unsigned index)
{
  if (kind == hkShader)
  {
    if (curVs == index) curVs = kNoRes;
    if (curPs == index) curPs = kNoRes;
  }
  else if (kind == hkLayout)
  {
    if (curLayout == index) curLayout = kNoRes;
  }
  else if (kind == hkBuffer)
  {
    if (curIb == index) curIb = kNoRes;
    if (instSrc == index) instSrc = kNoRes;
    for (unsigned i = 0; i < kMaxVertexStreams; ++i)
      if (vb[i] == index) vb[i] = kNoRes;
  }
  else if (kind == hkSurface)
  {
    if (curRt == index) curRt = kNoRes;
    if (curDs == index) curDs = kNoRes;
    for (unsigned i = 0; i < 8; ++i)
      if (rtSlot[i] == index) rtSlot[i] = kNoRes;
  }
  else if (kind == hkTexture)
  {
    for (unsigned i = 0; i < 16; ++i)
      if (texBound[i] == index) texBound[i] = kNoRes;
  }

  pipes.clear();
  resMaps.clear();
  setCache.clear();
  smpSetCache.clear();
  ilCache.clear();
  // поверхности (в т.ч. бэк-буферы) пересоздаются вместе с swapchain'ом —
  // пайплайны и наборы внутреннего блита держат вьюхи этих картинок
  stretchPipes.clear();
  stretchBsTex = nvrhi::BindingSetHandle();
  stretchBsSmp = nvrhi::BindingSetHandle();
  stretchBsKey = 0;
  pipesCreated = 0;
  resMapsCreated = 0;
  setsCreated = 0;
  samplersCreated = 0;
  smpSetsCreated = 0;
  layoutsBuilt = 0;
}

unsigned long SeamHandle::Release()
{
  const long r = InterlockedDecrement(&refs);
  if (r == 0)
  {
    // после Shutdown() бэкенда уже нет (именно поэтому Shutdown оставляет
    // impl = 0, а не освобождает ручки, у которых ещё есть ссылки клиента)
    if (impl)
      impl->FreeHandle(kind, index);
    else
    {
      // бэкенда нет: обёртку тоже не удаляем — кто-то может держать указатель,
      // а чтение освобождённой памяти неотличимо от порчи (см. FreeHandle)
      magic = (unsigned)kFreed;
      return 0;
    }
    return 0;
  }
  return (unsigned long)r;
}

static void* NewHandle(NvrhiBackendImpl* impl, unsigned index, unsigned kind)
{
  if (!impl)
    return 0;
  SeamHandle** slot = impl->SeamSlot(kind, index);
  if (!slot)
    return 0;
  if (!*slot)
    *slot = new SeamHandle(impl, index, kind);
  else
    // Шаг 13.4d: каждая выдача ручки — ОТДЕЛЬНАЯ ссылка (как в COM:
    // GetRenderTarget/GetDepthStencil/GetSurfaceLevel возвращают указатель со
    // своей ссылкой). Без этого умный указатель клиента, отработавший кадр,
    // ронял счётчик единственного владельца в 0, ресурс умирал, а индекс
    // оставался в кэшах бэкенда (curRt/rtSlot/backDepthIdx) — Clear по
    // мёртвой текстуре = access violation внутри nvrhi (прогон 2026-10-16
    // 17.05: трасса обрывается на Clear, exception.dmp, стек 0xa4d797/0xa2aa21).
    (*slot)->AddRef();
  return (void*)*slot;
}

//////////////////////////////////////////////////////////////////////////
// Вспомогательные вывод/ошибки

// Шаг 13.4d: заметку одного места печатаем не чаще kNotePrint раз: непереведённый
// путь (mip-цепочка DDS-текстуры, GetSurfaceInfo по чужой ручке) давал 36 092 и
// 16 860 строк на прогон — в этом потоке тонут все остальные сообщения. Счётчики
// мест печатает DumpStats (строка VkNotes), поэтому молчаливое усечение лога
// неотличимо от «так и надо».
static const unsigned kNotePrint = 6u;
static const unsigned kNoteSites = 32u;
struct NoteSite { char what[48]; unsigned hits; };
static NoteSite g_noteSites[kNoteSites];
static unsigned g_noteSiteCount = 0;
static unsigned g_noteSuppressed = 0;

// true — печатать; false — это повтор сверх лимита
static bool NoteGate(const char* what)
{
  for (unsigned i = 0; i < g_noteSiteCount; ++i)
    if (!strcmp(g_noteSites[i].what, what))
    {
      ++g_noteSites[i].hits;
      if (g_noteSites[i].hits > kNotePrint) { ++g_noteSuppressed; return false; }
      return true;
    }
  if (g_noteSiteCount < kNoteSites)
  {
    strncpy(g_noteSites[g_noteSiteCount].what, what, sizeof(g_noteSites[g_noteSiteCount].what) - 1);
    g_noteSites[g_noteSiteCount].what[sizeof(g_noteSites[g_noteSiteCount].what) - 1] = 0;
    g_noteSites[g_noteSiteCount].hits = 1;
    ++g_noteSiteCount;
  }
  return true;
}

static void VkNote(const char* what, bool ok, const char* detail)
{
  if (ok)
    DebugTrace("NvrhiBackend: %s — OK%s%s", what, detail ? " " : "", detail ? detail : "");
  else
  {
    if (!what || !NoteGate(what)) return;
    DebugTrace("NvrhiBackend: %s — НЕ УДАЛОСЬ%s%s", what, detail ? " " : "", detail ? detail : "");
    systemLog(NLogg::LEVEL_WARNING) << "NvrhiBackend: " << what << " — "
                                    << (detail ? detail : "") << endl;
  }
}

// Шаг 13.4c: «ручка не нашего бэкенда» означает, что ресурс создан в обход seam'а
// (D3D9/D3DX), и без адреса вызывающего это место не найти: RVA печатается всегда
// (objdump по RVA даёт функцию-виновника за один шаг). Молчаливый пропуск даёт
// «вывод пропал» без объяснения — ровно тот класс дефекта, который ловили весь шаг.
static void NoteForeign(const char* what, const void* p, void* pRet)
{
  if (!NoteGate(what)) return;
  const unsigned rva = (unsigned)((size_t)pRet - (size_t)GetModuleHandle(0));
  if (!p)
    DebugTrace("NvrhiBackend: %s — пустая ручка (0), вызов из RVA 0x%x", what, rva);
  else
    DebugTrace("NvrhiBackend: %s — ручка 0x%llX не нашего бэкенда (магия 0x%08X), вызов из RVA 0x%x",
               what, (unsigned long long)(size_t)p, static_cast<const SeamHandle*>(p)->magic, rva);
}

// Шаг 13.4d: «кто это создаёт» — когда ручка живёт один кадр, нужен адрес
// создателя, а не потребителя (иначе гонка создания/уничтожения выглядит как
// «ресурс создан вне seam'а»). Печать ограничена первыми cap обращений.
static void NoteCallSite(int slot, const char* what, void* pRet, unsigned cap)
{
  static unsigned hits[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  if (slot < 0 || slot >= 8) return;
  if (hits[slot]++ >= cap) return;
  DebugTrace("NvrhiBackend: %s — вызов из RVA 0x%x", what,
             (unsigned)((size_t)pRet - (size_t)GetModuleHandle(0)));
}

// Шаг 14 (перед коммитом): шумная диагностика выключена по умолчанию.
// Счётчики продолжают считаться (они дёшевы), молчит только печать: прогон боя
// (2026-10-18) давал 8912 строк лога, из них ~2100 — VkBeat/VkDiag/VkTexReq/
// VkIlMap/VkFb и периодические сводки DumpStats("flip"). Включается одной
// переменной окружения PW_VK_STATS=1 (её же читает renderer.cpp для сводок).
static bool VkVerbose()
{
  static int s_on = -1;
  if (s_on < 0)
  {
    char b[8] = { 0 };
    s_on = (GetEnvironmentVariableA("PW_VK_STATS", b, sizeof(b) - 1) > 0 && atoi(b)) ? 1 : 0;
  }
  return s_on != 0;
}

// Шаг 13.4c: хронологическая трассировка
// первых seam-вызовов: в режиме переворота клиент встаёт в первом
// кадре, и по последней строке видно, на чём именно заблокировался.
// Бюджет трассы задаётся env PW_VK_TRACE (по умолчанию 0 = трассы нет, кольцо
// пишется всегда): загрузка карты съедает больше 1500 вызовов, и без настройки
// обрыв трассы неотличим от «бэкенд больше не вызывали» (замер 2026-10-16).
static unsigned g_traceBudget = 0u;
static bool g_watchdogOn = true;
static unsigned g_traceSeq = 0u;
static bool     g_traceEnded = false;
// Шаг 13.4e: кольцо последних вызовов бэкенда. Бюджет трассы исчерпывается ещё
// до интересного момента (шторм загрузки съедает 200 000 записей за 25 с), а
// нужен именно момент ОСТАНОВКИ потока: VkBeat печатает последние записи кольца,
// и по ним видно, на каком вызове бэкенда всё встало. Работает и при
// PW_VK_TRACE=0 (кольцо пишется всегда, трасса — нет).
static const unsigned kTraceRing = 64u;
static const char* g_traceRingName[kTraceRing] = { 0 };
static unsigned g_traceRingSeq[kTraceRing] = { 0 };

// 4 записи на строку: DebugTrace принимает не больше 10 аргументов
static void VkRingDump(unsigned count)
{
  if (!g_traceSeq) return;
  if (count > g_traceSeq) count = g_traceSeq;
  if (count > kTraceRing) count = kTraceRing;
  // +1: иначе newest-запись (та, на которой поток и стоит) в дамп не попадает
  const unsigned start = g_traceSeq - count + 1u;
  for (unsigned i = 0; i < count; i += 4u)
  {
    const unsigned a = (start + i) % kTraceRing;
    const unsigned b = (start + i + 1u) % kTraceRing;
    const unsigned c = (start + i + 2u) % kTraceRing;
    const unsigned e = (start + i + 3u) % kTraceRing;
    DebugTrace("VkRing: %u %s | %u %s | %u %s | %u %s",
               g_traceRingSeq[a], g_traceRingName[a] ? g_traceRingName[a] : "",
               g_traceRingSeq[b], g_traceRingName[b] ? g_traceRingName[b] : "",
               g_traceRingSeq[c], g_traceRingName[c] ? g_traceRingName[c] : "",
               g_traceRingSeq[e], g_traceRingName[e] ? g_traceRingName[e] : "");
  }
}

#define VK_TRACE(what) \
  do { ++g_traceSeq; \
       g_traceRingName[g_traceSeq % kTraceRing] = (what); \
       g_traceRingSeq[g_traceSeq % kTraceRing] = g_traceSeq; \
       if (g_traceBudget) { --g_traceBudget; \
         DebugTrace("VkTrace: %u %s", g_traceSeq, what); \
         if (!g_traceBudget && !g_traceEnded) { g_traceEnded = true; \
           DebugTrace("VkTrace: БЮДЖЕТ ИСЧЕРПАН на %u (env PW_VK_TRACE)", g_traceSeq); } } } while (0)

// Шаг 13.4e: сторож зависания. Сводки (VkStats/VkRes/VkSlab) печатает Present,
// а при зависании загрузки Present не вызывается вовсе — молчание бэкенда
// неотлично от «всё в порядке» (замер 2026-10-16: поток рендера встал посреди
// загрузки карты, лог молчал минуты). Раз в 5 с смотрим, продвигается ли
// счётчик обращений к бэкенду; стоит — печатаем кольцо последних вызовов.
void NvrhiBackendImpl::Watchdog()
{
  unsigned last = 0;
  unsigned reports = 0;
  for (unsigned i = 0; i < 240u && reports < 6u; ++i)
  {
    Sleep(5000);
    const unsigned now = g_traceSeq;
    if (now == last)
    {
      DebugTrace("VkHang: к бэкенду нет обращений %u с (seq=%u загруз=%u presents=%u "
                 "буферы=%u текстуры=%u)",
                 (i + 1u) * 5u, now, uploads, presents,
                 (unsigned)buffers.size(), (unsigned)textures.size());
      VkRingDump(24u);
      ++reports;
    }
    last = now;
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
  {
    m_constShadow[i] = new float[SHADER_CONST_REGISTERS * 4];
    // обязательно обнулять: клиент задаёт не все регистры, а D3D9-путь
    // (через драйвер) видит незаданные нулями. Без обнуления в хвосте банка
    // остаётся мусор аллокатора — в прогоне 2026.10.17 (vk_rec7.pwrd) в c216+
    // лежали указатели Wine (0x0000776fe54bf5b0), и шейдер, читающий такой
    // регистр, получает мусор вместо нуля
    memset(m_constShadow[i], 0, sizeof(float) * (size_t)SHADER_CONST_REGISTERS * 4u);
  }
}

NvrhiBackend::~NvrhiBackend()
{
  Shutdown();
  for (int i = 0; i < csConstStageCount; ++i)
    delete[] m_constShadow[i];
  // обёртки освобождённых ручек (см. FreeHandle) — ссылок клиента на них нет
  for (size_t i = 0; i < m_pImpl->deadHandles.size(); ++i)
    delete m_pImpl->deadHandles[i];
  m_pImpl->deadHandles.clear();
  delete m_pImpl;
}

NvrhiBackend* NvrhiBackend::Create(NativeWindow window, unsigned width, unsigned height)
{
  // статический метод — m_pImpl ещё нет, блокировка не нужна (Init её возьмёт)
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

  // бюджет трассы (VK_TRACE) задаётся env: загрузка карты съедает больше
  // 1500 вызовов бэкенда, обрыв трассы без маркера неотличим от простоя
  {
    char tb[32];
    if (GetEnvironmentVariableA("PW_VK_TRACE", tb, sizeof(tb) - 1) > 0)
      g_traceBudget = (unsigned)atoi(tb);
    char wd[8];
    if (GetEnvironmentVariableA("PW_VK_WATCHDOG", wd, sizeof(wd) - 1) > 0)
      g_watchdogOn = (atoi(wd) != 0);
    // снимок кадра делает сам бэкенд ВНУТРИ Present (см. ниже): снимок из
    // renderer.cpp после Present читает уже СЛЕДУЮЩУЮ картинку acquire'а —
    // acquire в Present последний (замер 2026-10-16: vk_shot.bmp при
    // PW_VK_SHOT=300 был полностью чёрным, 786432 пикселя BGR000000)
    char sh[16];
    if (GetEnvironmentVariableA("PW_VK_SHOT", sh, sizeof(sh) - 1) > 0)
      d.shotAt = (unsigned)atoi(sh);
    char sp[8];
    if (GetEnvironmentVariableA("PW_VK_SHOT_PROBE", sp, sizeof(sp) - 1) > 0)
      d.shotProbe = (unsigned)atoi(sp);
    RecOpen(d);
  }

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

  // env PW_VK_LAYERS="VK_LAYER_A,VK_LAYER_B" — явные слои инстанса.
  // Нужно для RenderDoc: под Wine его LD_PRELOAD-хуки не видят вызовов
  // winevulkan (замер 2026-10-17: в госте «Registering Vulkan hooks» есть,
  // а .rdc не появляется), а слой, включённый самим приложением, кадры
  // считает и захват принимает.
  {
    char ly[512];
    if (GetEnvironmentVariableA("PW_VK_LAYERS", ly, sizeof(ly) - 1) > 0)
    {
      char* tok = strtok(ly, ",");
      while (tok && d.enabledLayers.size() < 8u)
      {
        while (*tok == ' ') ++tok;
        char* end = tok + strlen(tok) - 1;
        while (end > tok && (*end == ' ' || *end == '\r' || *end == '\n')) *end-- = 0;
        if (*tok) d.enabledLayers.push_back(std::string(tok));
        tok = strtok(0, ",");
      }
    }
  }
  for (size_t i = 0; i < d.enabledLayers.size(); ++i)
    d.enabledLayerPtrs.push_back(d.enabledLayers[i].c_str());
  if (!d.enabledLayerPtrs.empty())
  {
    ici.enabledLayerCount = (uint32_t)d.enabledLayerPtrs.size();
    ici.ppEnabledLayerNames = &d.enabledLayerPtrs[0];
  }
  for (size_t i = 0; i < d.enabledLayers.size(); ++i)
    VkNote("Слой инстанса", true, d.enabledLayers[i].c_str());

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
                                              .setAddressW(nvrhi::SamplerAddressMode::Wrap)
                                              .setMipBias(SamplerLod0Forced() ? -1000.f : 0.f));
  // 1x1 чёрная: в слот, который шейдер объявляет, а клиент не привязал, обязан
  // лежать валидный дескриптор — иначе валидация рвёт кадр молча
  d.dummyTex = d.nvrhiDevice->createTexture(nvrhi::TextureDesc()
                                            .setDimension(nvrhi::TextureDimension::Texture2D)
                                            .setWidth(1).setHeight(1)
                                            .setFormat(nvrhi::Format::BGRA8_UNORM)
                                            .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                            .setKeepInitialState(true)
                                            .setDebugName("seam dummy 1x1"));
  // cube-замена: шейдер объявил samplerCUBE, а клиент привязал 2D-текстуру
  d.dummyCubeTex = d.nvrhiDevice->createTexture(nvrhi::TextureDesc()
                                                .setDimension(nvrhi::TextureDimension::TextureCube)
                                                .setWidth(1).setHeight(1).setArraySize(6)
                                                .setFormat(nvrhi::Format::BGRA8_UNORM)
                                                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                .setKeepInitialState(true)
                                                .setDebugName("seam dummy cube 1x1"));
  // пустые вершинные слоты: NVRHI привязывает буферы до максимального
  // используемого слота, пропущенный слот = VK_NULL_HANDLE
  // (VUID-vkCmdBindVertexBuffers-pBuffers-04001)
  d.dummyVb = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                          .setByteSize(64)
                                          .setIsVertexBuffer(true)
                                          .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                          .setKeepInitialState(true)
                                          .setDebugName("seam dummy VB"));
  d.dummySsbo = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                            .setByteSize(4096)
                                            // без этого NVRHI не ставит
                                            // VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                            // (vulkan-buffer.cpp: structStride ||
                                            // canHaveUAVs || canHaveRawViews) и
                                            // валидатор рвёт
                                            // VUID-VkWriteDescriptorSet-descriptorType-00331
                                            .setCanHaveUAVs(true)
                                            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                            .setKeepInitialState(true)
                                            .setDebugName("seam dummy SSBO"));
  if (!d.defSampler || !d.dummyTex)
  {
    VkNote("Init: сэмплер по умолчанию / dummy-текстура", false, 0);
    return false;
  }

  if (g_watchdogOn)
    d.ownerThread = GetCurrentThreadId();
    std::thread(&NvrhiBackendImpl::Watchdog, &d).detach();

  VkNote("Vulkan-бэкенд поднят", true, d.deviceName);
  return true;
}

//////////////////////////////////////////////////////////////////////////
// Swapchain. Порядок и набор параметров — как в проверенном спайке
// (tools/nvrhi_probe/probe.cpp, ProbeWin32Swapchain): без TRANSFER_DST у
// lavapipe present падает на VK_ERROR_MEMORY_MAP_FAILED.

// Acquire следующей картинки swapchain'а. Возвращает VkResult: вызывающий
// обязан отличить OUT_OF_DATE/SURFACE_LOST (штатный повод пересоздать) от
// настоящей ошибки.
static VkResult AcquireNextImage(NvrhiBackendImpl& d)
{
  if (!d.swapchain || d.acquireFences.empty()) return VK_NOT_READY;
  VkFence f = d.acquireFences[d.fenceRot % (unsigned)d.acquireFences.size()];
  ++d.fenceRot;
  // Сброс ДО acquire. Сбрасывать сразу после ожидания валидатор запрещает:
  // VUID-vkResetFences-pFences-01123 «vkResetFences(): pFences[0] ... is in use»
  // (замер 2026-10-16, прогон 22.37 — 20 таких ошибок).
  VULKAN_HPP_DEFAULT_DISPATCHER.vkResetFences(d.device, 1, &f);
  uint32_t idx = 0;
  const VkResult ar = VULKAN_HPP_DEFAULT_DISPATCHER.vkAcquireNextImageKHR(
      d.device, d.swapchain, 0xFFFFFFFFFFFFFFFFull, VK_NULL_HANDLE, f, &idx);
  if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR)
  {
    VkNote("vkAcquireNextImageKHR", false, VkResultName(ar));
    return ar;
  }
  // Fence ждём сразу: картинка обязана стать доступной ДО записи в неё.
  // Ожидание дешёвое — перед present'ом и так стоит waitForIdle.
  if (VULKAN_HPP_DEFAULT_DISPATCHER.vkWaitForFences(d.device, 1, &f, VK_TRUE,
                                                   0xFFFFFFFFFFFFFFFFull) != VK_SUCCESS)
    VkNote("acquire: waitForFences", false, 0);
  d.imageIndex = idx;
  if (d.imageIndex >= d.backSurfaceIdx.size())
  {
    VkNote("acquire: индекс картинки вне swapchain'а", false, 0);
    return VK_NOT_READY;
  }
  return VK_SUCCESS;
}

// Ручной переход макета картинки swapchain'а. NVRHI оперирует состояниями
// ресурсов и не знает про VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: после вывода картинка
// остаётся в COLOR_ATTACHMENT_OPTIMAL, а present требует PRESENT_SRC_KHR
// (замер с VK_LAYER_KHRONOS_validation: VUID-VkPresentInfoKHR-pImageIndices-01430
// «images passed to present must be in layout VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
// but VkImage ... is in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL»).
// Трекер NVRHI про переход не знает, поэтому обратно возвращаем тем же способом.
// Новый поток, трогающий бэкенд, виден в логе однократно: по нему видно,
// кто именно грузит в обход владельца.
static void VkNoteThread(NvrhiBackendImpl& d, const char* what)
{
  const unsigned tid = GetCurrentThreadId();
  for (unsigned i = 0; i < d.threadSeenCount; ++i)
    if (d.threadSeen[i] == tid) return;
  if (d.threadSeenCount < 6u)
  {
    d.threadSeen[d.threadSeenCount] = tid;
    ++d.threadSeenCount;
    DebugTrace("VkThread: %s — новый поток 0x%08X (владелец 0x%08X)", what, tid,
               d.ownerThread);
  }
}

static void TransitionBackImage(NvrhiBackendImpl& d, unsigned imageIndex,
                                VkImageLayout fromL, VkImageLayout toL,
                                VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
  if (imageIndex >= d.backImages.size() || !d.commandList || !d.nvrhiDevice) return;
  d.commandList->open();
  // nvrhi::Object имеет operator T*(), VkCommandBuffer — это handle-указатель
  VkCommandBuffer cmd = d.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
  if (!cmd) { d.commandList->close(); return; }
  VkImageMemoryBarrier b;
  memset(&b, 0, sizeof(b));
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = fromL;
  b.newLayout = toL;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = d.backImages[imageIndex];
  b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b.subresourceRange.baseMipLevel = 0;
  b.subresourceRange.levelCount = 1;
  b.subresourceRange.baseArrayLayer = 0;
  b.subresourceRange.layerCount = 1;
  b.srcAccessMask = (fromL == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                  ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
  b.dstAccessMask = 0;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                                                     0, 0, 0, 0, 1, &b);
  d.commandList->close();
  d.nvrhiDevice->executeCommandList(d.commandList);
}

bool NvrhiBackend::CreateSwapchain(unsigned width, unsigned height)
{
  VK_API_GUARD;
  VK_TRACE("CreateSwapchain");
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
  d.backImages = images;

  // fence'ы acquire'а — по одному на картинку (кольцо)
  for (size_t i = 0; i < d.acquireFences.size(); ++i)
    if (d.acquireFences[i]) VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyFence(d.device, d.acquireFences[i], 0);
  d.acquireFences.assign(imgCount, (VkFence)0);
  for (uint32_t i = 0; i < imgCount; ++i)
  {
    VkFenceCreateInfo fci;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // БЕЗ SIGNALED: валидатор требует не signaled fence
    // (VUID-vkAcquireNextImageKHR-fence-01287 «fence submitted in SIGNALED
    // state»); после каждого acquire fence сразу ждётся и сбрасывается.
    fci.flags = 0;
    if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateFence(d.device, &fci, 0, &d.acquireFences[i]) != VK_SUCCESS)
    {
      VkNote("vkCreateFence (acquire)", false, 0);
      return false;
    }
  }

  // NVRHI не знает, в каком состоянии находятся картинки чужого swapchain'а,
  // поэтому начальное состояние выставляется вручную (замер спайка).
  nvrhi::Format nf = (d.swapFormat == VK_FORMAT_B8G8R8A8_UNORM) ? nvrhi::Format::BGRA8_UNORM
                                                                : nvrhi::Format::RGBA8_UNORM;
  d.backTextures.clear();
  // Записи бэк-буфера ПЕРЕИСПОЛЬЗУЮТСЯ при пересоздании swapchain'а.
  // Замер (прогон 2026.10.16-21.12): клиент получает поверхность бэк-буфера
  // один раз (renderer.cpp, OnDeviceChanged) и держит её кадрами; после
  // штатного пересоздания (смена разрешения 800x600 -> 1024x768) прежняя
  // запись была отпущена, и каждый кадр давал пропуск вывода
  // (`вывод: в бэк-буфер=0`, нет RT=1799 за 1799 кадров). Поэтому индексы
  // записей и выданные клиенту ручки сохраняем, а картинку подменяем.
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
    unsigned sidx = (i < d.backSurfaceIdx.size()) ? d.backSurfaceIdx[i] : kNoRes;
    if (sidx != kNoRes && sidx < d.surfaces.size())
    {
      SurfaceObj& so = d.surfaces[sidx];
      so.tex = h;
      // ручка бэк-буфера у клиента одна и получена один раз, поэтому
      // разворот в текущую картинку acquire'а обязан работать всегда
      so.backBufferAlias = true;
      so.fmt = nf;
      so.w = d.width; so.h = d.height;
      so.dead = false;
    }
    else
    {
      SurfaceObj so;
      so.tex = h;
      so.backBuffer = true;
      so.backBufferAlias = true;
      so.fmt = nf;
      so.w = d.width; so.h = d.height;
      so.seamFormat = (nf == nvrhi::Format::BGRA8_UNORM) ? bfA8R8G8B8 : bfUnknown;
      so.pool = BACKEND_POOL_DEFAULT;
      so.usage = usageRenderTarget;
      sidx = (unsigned)d.surfaces.size();
      d.surfaces.push_back(so);
    }
    if (i >= d.backSurfaceIdx.size()) d.backSurfaceIdx.push_back(sidx);
  }
  // Картинки могли измениться в меньшую сторону — лишние записи отпускаем.
  for (size_t i = imgCount; i < d.backSurfaceIdx.size(); ++i)
    if (d.backSurfaceIdx[i] < d.surfaces.size()) d.RetireHandle(hkSurface, d.backSurfaceIdx[i]);
  if (d.backSurfaceIdx.size() > imgCount) d.backSurfaceIdx.resize(imgCount);

  VkSemaphoreCreateInfo semci;
  memset(&semci, 0, sizeof(semci));
  semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateSemaphore(d.device, &semci, 0, &d.acquired) != VK_SUCCESS)
  {
    VkNote("vkCreateSemaphore", false, 0);
    return false;
  }
  // Первую картинку берём сразу: кадр клиента рисуется ДО первого Present, и
  // без acquire'а он шёл бы в картинку, которая никогда не была получена.
  // Замер: acquire до создания семафора возвращал VK_NOT_READY (`d.acquired`
  // был нулём), из-за чего backSurfaceIdx оставался незаполненным и
  // BackSurfaceIndex давал kNoRes — алиас бэк-буфера не работал.
  {
    const VkResult ar0 = AcquireNextImage(d);
    if (ar0 != VK_SUCCESS) VkNote("CreateSwapchain: первый acquire не удался", false, 0);
  }

  // Штатная глубина бэк-буфера (см. комментарий к backDepthIdx).
  {
    HSurface hs;
    bool made = false;
    // Запись глубины бэк-буфера ПЕРЕИСПОЛЬЗУЕТСЯ (см. DestroySwapchain): ручка
    // клиента обязана остаться валидной после пересоздания swapchain'а.
    if (d.backDepthIdx != kNoRes && d.backDepthIdx < d.surfaces.size() &&
        !d.surfaces[d.backDepthIdx].dead && !d.surfaces[d.backDepthIdx].tex)
    {
      SurfaceObj& ds = d.surfaces[d.backDepthIdx];
      nvrhi::TextureDesc td;
      td.setDimension(nvrhi::TextureDimension::Texture2D).setWidth(d.width).setHeight(d.height)
        .setFormat(nvrhi::Format::D24S8).setIsRenderTarget(true)
        .setInitialState(nvrhi::ResourceStates::DepthWrite).setKeepInitialState(true)
        .setDebugName("PW back depth");
      ds.tex = d.nvrhiDevice->createTexture(td);
      if (ds.tex)
      {
        ds.w = d.width; ds.h = d.height;
        ds.fmt = nvrhi::Format::D24S8;
        ds.seamFormat = bfD24S8;
        ds.usage = usageDepthStencil;
        ds.depth = true;
        made = true;
      }
    }
    if (!made)
    {
      if (SUCCEEDED(CreateSurface(ssDepthStencil, d.width, d.height, bfD24S8,
                                  BACKEND_POOL_DEFAULT, &hs)))
        d.backDepthIdx = Hnd2Idx(hs.p);
      else
      {
        d.backDepthIdx = kNoRes;
        VkNote("CreateSwapchain: глубина бэк-буфера не создана", false, 0);
      }
    }
  }


  DebugTrace("NvrhiBackend: swapchain %ux%u, картинок %u, формат %d, режим present %d",
             d.width, d.height, (unsigned)imgCount, (int)d.swapFormat, (int)d.presentMode);
  return true;
}

void NvrhiBackend::DestroySwapchain()
{
  VK_API_GUARD;
  VK_TRACE("DestroySwapchain");
  NvrhiBackendImpl& d = *m_pImpl;
  if (d.nvrhiDevice)
    VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");
  // Картинки принадлежат swapchain'у: NVRHI-обёртки и SurfaceObj отпускаем ДО
  // vkDestroySwapchainKHR. d.surfaces целиком НЕ чистится: там поверхности
  // клиента (RT/DS), чьи ручки — индексы этого же вектора; отпускаются только
  // записи бэк-буферов (их индексы лежат в backSurfaceIdx, и выданные
  // GetBackBuffer ручки становятся пустыми — клиент обязан взять бэк-буфер
  // заново после пересоздания).
  // Записи бэк-буфера и выданные по ним ручки остаются живыми (см.
  // CreateSwapchain): отпускаются только nvrhi-обёртки картинок (backTextures),
  // а картинка в записи обнуляется, чтобы вывод до следующего CreateSwapchain
  // отсекался явной заметкой, а не зависанием в createImageView.
  for (size_t i = 0; i < d.backSurfaceIdx.size(); ++i)
    if (d.backSurfaceIdx[i] < d.surfaces.size())
    {
      d.surfaces[d.backSurfaceIdx[i]].tex = nullptr;
    }
  if (d.backDepthIdx != kNoRes && d.backDepthIdx < d.surfaces.size())
  {
    // Запись глубины НЕ отпускается (RetireHandle), а только лишается картинки:
    // клиент кэширует GetDepthStencilSurface() один раз и держит её кадрами, а
    // пересоздание swapchain'а (800x600 -> 1024x768 на старте) оставляло ручку
    // на mёртвой записи. Замер прогона 100 (2026-10-18): 4226 «GetSurfaceInfo:
    // ручка не из этого бэкенда» + 4226 «BindRenderTargetDepth: GetSurfaceInfo
    // не удалось», трасса VkSrf — raw=3, «запись dead». Каждый кадр глубина не
    // привязывалась. Индекс и ручку сохраняем, картинку создаёт CreateSwapchain.
    SurfaceObj& ds = d.surfaces[d.backDepthIdx];
    ds.tex = nullptr;
    ds.staging = nullptr;
    FreeMirror(ds.mirror);
  }
  d.backTextures.clear();
  // Кэш пайплайнов держит framebuffer'ы, построенные по RT/DS; после
  // пересоздания swapchain'а часть из них указывает на мёртвые картинки,
  // поэтому сбрасываем (дороже, но молча рисовать по чужой картинке — хуже).
  d.pipes.clear();
  d.setCache.clear();
  d.currentSurface = 0;
  // Собственный кэш бэкенда по текущим RT/DS тоже обязан быть сброшен:
  // замер (прогон 2026.10.16-20.54) — поток встал внутри createFramebuffer,
  // потому что curRt остался равен индексу картинки СТАРОГО swapchain'а
  // (`VkFb: rt=0 800x600 back=1` при новом swapchain'е 1024x768), а
  // nvrhi-обёртка той картинки отпущена в CreateSwapchain (backTextures.clear)
  // => createImageView по освобождённому объекту. Глубина бэк-буфера
  // (backDepthIdx) в том же прогоне давала пару 1024x768 RT + 800x600 DS.
  d.curRt = kNoRes;
  d.curDs = kNoRes;
  for (int i = 0; i < 8; ++i) d.rtSlot[i] = kNoRes;
  for (size_t i = 0; i < d.acquireFences.size(); ++i)
    if (d.acquireFences[i]) VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyFence(d.device, d.acquireFences[i], 0);
  d.acquireFences.clear();
  d.backImages.clear();
  if (d.acquired) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySemaphore(d.device, d.acquired, 0); d.acquired = 0; }
  if (d.swapchain) { VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroySwapchainKHR(d.device, d.swapchain, 0); d.swapchain = 0; }
}

bool NvrhiBackend::RecreateSurface()
{
  VK_API_GUARD;
  VK_TRACE("RecreateSurface");
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
  VK_API_GUARD;
  VK_TRACE("RecreateSwapchain");
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

template <class T>
static unsigned OrphanHandles(std::vector<T>& v)
{
  unsigned n = 0;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i].hnd) { v[i].hnd->impl = 0; ++n; }
  return n;
}


void NvrhiBackend::Shutdown()
{
  VK_API_GUARD;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.device)
  {
    // Порядок важен: nvrhi-объекты должны умереть раньше VkDevice.
    if (d.hVulkan) { FreeLibrary(d.hVulkan); d.hVulkan = 0; }
    return;
  }
  if (d.nvrhiDevice)
    VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");
  RecClose(d);
  d.backTextures.clear();
  // Шаг 13.4c: обёртки ручек, у которых ещё есть ссылки клиента (умные указатели
  // живут в ShaderPair, Texture, RenderSurface), переживают бэкенд НАМЕРЕННО:
  // освободить их нельзя — более поздний Release() читал бы freed memory.
  // Имплант обнуляется, и Release() при нуле ссылок удаляет только обёртку.
  unsigned orphans = 0;
  orphans += OrphanHandles(d.surfaces);
  orphans += OrphanHandles(d.textures);
  orphans += OrphanHandles(d.buffers);
  orphans += OrphanHandles(d.shaderObjs);
  orphans += OrphanHandles(d.layoutObjs);
  if (orphans)
  {
    char det[48];
    snprintf(det, sizeof(det), "обёрток с живыми ссылками: %u", orphans);
    VkNote("Shutdown: владение", true, det);
  }
  // Шаг 14: периодические сводки выключены по умолчанию, поэтому итоговые
  // счётчики снимаем здесь — до очистки контейнеров
  DumpCompact();
  d.surfaces.clear();
  // порядок: ВСЕ nvrhi-ресурсы обязаны умереть раньше nvrhiDevice и VkDevice
  // (иначе деструкторы RefCountPtr работают с уже уничтоженным устройством)
  d.setCache.clear();
  d.smpSetCache.clear();
  d.samplerCache.clear();
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

// Шаг 13.4c: «сироты» — обёртки ручек, пережившие бэкенд (см. Shutdown)

void NvrhiBackend::DumpStats(const char* pTag) const
{
  const NvrhiBackendImpl& d = *m_pImpl;
  DebugTrace("VkStats%s: presents=%u recreates=%u (surface %u) unsupported=%u swap=%ux%u device=%s",
             pTag ? pTag : "", d.presents, d.swapRecreates, d.surfaceRecreates, m_unsupported,
             d.width, d.height, d.deviceName);
  // DebugTrace — не printf, а PrintfFormatter: максимум 10 аргументов, поэтому
  // счётчики ресурсов в двух строках
  DebugTrace("VkRes%s: буферы=%u шейдеры=%u раскладки=%u пайплайны=%u/%u (попаданий %u) "
             "вывод=%u загруз=%u (ожиданий кольца %u)",
             pTag ? pTag : "", (unsigned)d.buffers.size(), (unsigned)d.shaderObjs.size(),
             (unsigned)d.layoutObjs.size(), d.pipesCreated, (unsigned)d.pipes.size(), d.pipeHits,
             d.draws, d.uploads, d.uploadWaits);
  // DebugTrace: максимум 10 аргументов — отправки отдельной строкой
  DebugTrace("VkUp%s: загруз=%u отправок=%u байт=%u", pTag ? pTag : "",
             d.uploads, d.uploadSubmits, (unsigned)d.uploadBytes);
  {
    size_t mb = 0, mt = 0, ms = 0;
    unsigned nb = 0, nt = 0, ns = 0;
    for (size_t i = 0; i < d.buffers.size(); ++i)
      if (!d.buffers[i].dead) { mb += d.buffers[i].cpu.size(); ++nb; }
    for (size_t i = 0; i < d.textures.size(); ++i)
      if (!d.textures[i].dead) { mt += d.textures[i].mirror.size(); ++nt; }
    for (size_t i = 0; i < d.surfaces.size(); ++i)
      if (!d.surfaces[i].dead) { ms += d.surfaces[i].mirror.size(); ++ns; }
    DebugTrace("VkSlab%s: слэбов=%u (по %u МБ) выделений=%u переиспользований дыр=%u "
             "освобождений=%u отказов=%u",
             pTag ? pTag : "", (unsigned)d.slabs.size(), (unsigned)(d.slabBytes >> 20),
             d.slabCreated, d.slabHoleReuses, d.slabFreed, d.slabAllocFail);
  DebugTrace("VkMem%s: живых буферов=%u (зеркало %u МБ) текстур=%u (%u МБ) "
               "поверхностей=%u (%u МБ)",
               pTag ? pTag : "", nb, (unsigned)(mb >> 20), nt, (unsigned)(mt >> 20),
               ns, (unsigned)(ms >> 20));
  }
  DebugTrace("VkTex%s: текстуры=%u наборы=%u/%u (промахов текстур %u) ssbo-наборов=%u",
             pTag ? pTag : "", (unsigned)d.textures.size(), d.setsCreated,
             (unsigned)d.resMaps.size(), d.texMissing, d.ssboSetsCreated);
  DebugTrace("VkRt%s: SetRenderTarget: NULL=%u пустая-ручка=%u чужих=%u своих=%u | SetDepthStencil: NULL=%u "
             "пустая=%u чужих=%u своих=%u | глубина бэк-буфера=%d",
             pTag ? pTag : "", d.rtSetNull, d.rtSetEmpty, d.rtSetForeign, d.rtSetOwn,
             d.dsSetNull, d.dsSetEmpty, d.dsSetForeign, d.dsSetOwn, (int)d.backDepthIdx);
  DebugTrace("VkDraw%s: пропуски вывода: примитив=%u нет VS=%u нет PS=%u нет RT=%u",
             pTag ? pTag : "", d.drawSkipPrim, d.drawSkipVs, d.drawSkipPs, d.drawSkipRt);
  {
    // DumpStats работает с const-ссылкой, поэтому бэк-индекс считается здесь
    // руками (BackSurfaceIndex требует неконстантного доступа).
    const unsigned bi = d.backSurfaceIdx.empty() ? kNoRes
        : d.backSurfaceIdx[d.imageIndex < d.backSurfaceIdx.size() ? d.imageIndex : 0u];
    DebugTrace("VkDraw%s: backIdx=%u imageIndex=%u картинок=%u", pTag ? pTag : "",
               bi, (unsigned)d.imageIndex, (unsigned)d.backSurfaceIdx.size());
  }
  DebugTrace("VkDraw%s: вывод: в бэк-буфер=%u в offscreen=%u",
             pTag ? pTag : "", d.drawsBack, d.drawsOff);
  DebugTrace("VkUp%s: UP-вывод=%u колец-сбросов=%u раскладок построено=%u (кэш %u)",
             pTag ? pTag : "", d.upDraws, d.upFlushes, d.layoutsBuilt,
             (unsigned)d.ilCache.size());
  DebugTrace("VkIl%s: заполнителей location=%u закрыто атрибутов=%u", pTag ? pTag : "",
             d.ilPadHoles, d.ilDropped);
  DebugTrace("VkState%s: смен состояний=%u сэмплеров=%u наборов сэмплеров=%u потерь=%u",
             pTag ? pTag : "", d.stateSets, d.samplersCreated, d.smpSetsCreated,
             d.stateUnsupported);
  // владение (шаг 13.4c): g_hndForeign — ручки, пришедшие от чужого бэкенда
  // (DDS через D3DX), освобождено — дожитие ссылок клиента до нуля
  DebugTrace("VkHnd%s: чужих=%u мёртвых=%u COM-вызовов=%u освобождено: буферы=%u текстуры=%u поверхности=%u "
             "шейдеры=%u раскладки=%u",
             pTag ? pTag : "", g_hndForeign, g_hndStale, g_seamPadHits,
             d.handlesFreed[hkBuffer],
             d.handlesFreed[hkTexture], d.handlesFreed[hkSurface], d.handlesFreed[hkShader],
             d.handlesFreed[hkLayout]);
  DebugTrace("VkNotes%s: мест=%u подавлено повторов=%u", pTag ? pTag : "",
             g_noteSiteCount, g_noteSuppressed);
  for (unsigned i = 0; i < g_noteSiteCount; ++i)
    if (g_noteSites[i].hits > kNotePrint)
      DebugTrace("VkNotes: %s — %u раз", g_noteSites[i].what, g_noteSites[i].hits);
  for (int i = 0; i < st_count; ++i)
    if (d.stubCalls[i])
      DebugTrace("VkStub: %s вызван %u раз", kStubNames[i], d.stubCalls[i]);
}

// Шаг 14: одна строка вместо 15. Именно по ней сверяют объём кэшей и число
// выводов между прогонами (ср. §1.4: 206 ключей пайплайнов на бой); печатается
// всегда, даже когда PW_VK_STATS=0. Формат — ровно 9 аргументов: DebugTrace
// принимает не больше 10 всего.
void NvrhiBackend::DumpCompact() const
{
  const NvrhiBackendImpl& d = *m_pImpl;
  DebugTrace("VkSum: presents=%u вывод=%u пайплайны=%u/%u (попаданий %u) загруз=%u "
             "объектов B/T/S=%u/%u/%u",
             d.presents, d.draws, d.pipesCreated, (unsigned)d.pipes.size(), d.pipeHits,
             d.uploads, (unsigned)d.buffers.size(), (unsigned)d.textures.size(),
             (unsigned)d.surfaces.size());
}

const char* NvrhiBackend::DeviceName() const { return m_pImpl->deviceName; }
void NvrhiBackend::SetClearOnPresent(bool on) { m_pImpl->clearOnPresent = on; }
unsigned NvrhiBackend::SwapchainWidth() const { return m_pImpl->width; }
unsigned NvrhiBackend::SwapchainHeight() const { return m_pImpl->height; }

HRESULT NvrhiBackend::Unsupported(int which) const
{
  VK_API_GUARD;
  ++m_unsupported;
  if (which >= 0 && which < st_count)
    ++m_pImpl->stubCalls[which];
  return E_NOTIMPL;
}

//////////////////////////////////////////////////////////////////////////
// Сведения об адаптере — заполняются по-настоящему (шаг 4, §2.5).

HRESULT NvrhiBackend::GetSystemInfo(BackendSystemInfo& out)
{
  VK_API_GUARD;
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
HRESULT NvrhiBackend::BeginScene() { VK_TRACE("BeginScene"); return S_OK; }
HRESULT NvrhiBackend::EndScene() { VK_TRACE("EndScene"); return S_OK; }

HRESULT NvrhiBackend::Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect)
{
  VK_API_GUARD;
  VK_TRACE("Present");
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
  // Шаг 13.4e: порядок «acquire в начале Present» был причиной чёрного кадра.
  // Клиент рисовал в картинку предыдущего acquire'а, а Present'овалась картинка,
  // полученная в ЭТОМ acquire'е, — в ней не было ни одного draw'а. Замер
  // 2026-10-16: `вывод: в бэк-буфер=0 в offscreen=107143` при presents=1199,
  // vk_shot.bmp = 786432 пикселя BGR000000. Теперь present'имся той картинкой,
  // в которую кадр рисовался, а следующую acquire'им сразу после present'а.
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
  VK_TRACE("Present: flush");
  FlushRecording();

  d.commandList->open();
  // Цвет намеренно не тот, что у D3D9-кадра: по нему в скриншоте видно, какой
  // именно вывод лёг последним. В режиме переворота заливки нет (SetClearOnPresent
  // (false)) — иначе кадр клиента стирался бы перед present'ом.
  if (d.clearOnPresent)
    d.commandList->clearTextureFloat(so.tex, nvrhi::TextureSubresourceSet(),
                                     nvrhi::Color(0.10f, 0.35f, 0.65f, 1.0f));
  d.commandList->close();
  d.nvrhiDevice->executeCommandList(d.commandList);
  VK_TRACE("Present: waitIdle >");
  VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");
  VK_TRACE("Present: waitIdle <");

  VK_TRACE("present layout >");
  TransitionBackImage(d, d.imageIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  VK_TRACE("present layout <");

  VkPresentInfoKHR pi;
  memset(&pi, 0, sizeof(pi));
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  // wait-семафор acquire'а здесь не уместен (картинка уже синхронизирована
  // waitForIdle выше); прежняя схема давала и невалидный acquire, и present
  // по картинке в чужом макете.
  pi.waitSemaphoreCount = 0;
  pi.swapchainCount = 1;
  pi.pSwapchains = &d.swapchain;
  pi.pImageIndices = &d.imageIndex;
  VK_TRACE("Present: present >");
  const VkResult pr = VULKAN_HPP_DEFAULT_DISPATCHER.vkQueuePresentKHR(d.queue, &pi);
  if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR)
  {
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_ERROR_SURFACE_LOST_KHR)
      return RecreateSwapchain() ? S_OK : E_FAIL;
    VkNote("vkQueuePresentKHR", false, VkResultName(pr));
    return E_FAIL;
  }
  ++d.presents;

  // Обратный переход: трекер NVRHI считает картинку в состоянии RenderTarget и
  // сам её обратно не вернёт, а следующий кадр пишет в неё как в цветное
  // вложение. Порядок на очереди гарантирует, что present уже принят.
  VK_TRACE("present layout back >");
  TransitionBackImage(d, d.imageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  VK_TRACE("present layout back <");

  if (d.shotAt && d.presents == d.shotAt)
  {
    VK_TRACE("Present: shot >");
    TexGpuDump();
    if (d.shotProbe) ProbeFrameSurfaces();
    if (d.shotProbe && d.shotProbe != 7u)
    {
      // зонд: если снимок станет красным — readback и картинка в порядке,
      // значит чёрный кадр даёт сам вывод клиента
      // (режим 7 — «сцена в кадр»: копию крупнейшей offscreen-поверхности
      // красной заливкой затирать нельзя, иначе эксперимент бессмыслен)
      if (EnsureRecording())
      {
        d.commandList->clearTextureFloat(so.tex, nvrhi::TextureSubresourceSet(),
                                         nvrhi::Color(1.f, 0.f, 0.f, 1.f));
        FlushRecording();
        d.nvrhiDevice->waitForIdle();
        DebugTrace("VkShotProbe: картинка %u залита красным", d.imageIndex);
      }
    }
    if (SaveBackBufferShot("vk_shot.bmp"))
      DebugTrace("Renderer: PW_VK_SHOT OK vk_shot.bmp (кадр %u)", d.presents);
    else
      DebugTrace("Renderer: PW_VK_SHOT FAILED (кадр %u)", d.presents);
    VK_TRACE("Present: shot <");
  }
  VK_TRACE("Present: acquire next >");
  const VkResult ar = AcquireNextImage(d);
  if (ar != VK_SUCCESS)
  {
    // OUT_OF_DATE/SURFACE_LOST — штатный повод пересоздать swapchain (Wine
    // поднимает его и на смене размера, и на сворачивании окна).
    if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_ERROR_SURFACE_LOST_KHR)
      return RecreateSwapchain() ? S_OK : E_FAIL;
    return E_FAIL;
  }
  return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// Область вывода, отсечение, клип-плоскость — ведём тень (её читает клиент).

void NvrhiBackend::GetViewport(BackendViewport& vp) { vp = m_viewport; }

HRESULT NvrhiBackend::SetViewport(const BackendViewport& vp)
{
  VK_API_GUARD;
  VK_TRACE("SetViewport");
  m_viewport = vp;
  return S_OK;
}

void NvrhiBackend::SetScissorRect(const BackendRect* pRect)
{
  VK_API_GUARD;
  VK_TRACE("SetScissorRect");
  if (pRect) { m_scissor = *pRect; m_scissorValid = true; }
  else m_scissorValid = false;
}

void NvrhiBackend::GetScissorRect(BackendRect* pOut)
{
  VK_API_GUARD;
  if (!pOut) return;
  if (m_scissorValid) *pOut = m_scissor;
  else { pOut->left = pOut->top = 0; pOut->right = (long)m_viewport.width; pOut->bottom = (long)m_viewport.height; }
}

void NvrhiBackend::SetClipPlane(bool enable, const float* pPlaneEquation)
{
  VK_API_GUARD;
  VK_TRACE("SetClipPlane");
  m_clipPlaneEnabled = enable;
  if (pPlaneEquation) memcpy(m_clipPlane, pPlaneEquation, sizeof(m_clipPlane));
}

void NvrhiBackend::SetPipelineState(const BackendPipelineState& state, unsigned long long stateKey)
{
  VK_API_GUARD;
  VK_TRACE("SetPipelineState");
  NvrhiBackendImpl& d = *m_pImpl;
  // менеджер отдаёт закэшированную модель и её хэш; пока хэш не менялся,
  // копия не нужна (вызов происходит на каждом выводе)
  if (d.modelHash == stateKey)
    return;
  d.model = state;
  d.modelHash = stateKey;
  ++d.stateSets;
}

HRESULT NvrhiBackend::Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil)
{
  VK_API_GUARD;
  VK_TRACE("Clear");
  NvrhiBackendImpl& d = *m_pImpl;
  if (d.curRt == kNoRes || d.curRt >= d.surfaces.size()) return D3DERR_INVALIDCALL;
  if (!EnsureRecording()) return E_FAIL;
  if (clearFlags & bcTarget)
  {
    if (!d.surfaces[d.curRt].tex)
    {
      char det[64];
      snprintf(det, sizeof(det), "curRt=%u — текстуры уже нет", d.curRt);
      VkNote("Clear: мёртвая поверхность", false, det);
      return D3DERR_INVALIDCALL;
    }
    const unsigned a = (colorRGBA >> 24) & 0xffu, r = (colorRGBA >> 16) & 0xffu,
                   g = (colorRGBA >> 8) & 0xffu, b = colorRGBA & 0xffu;
    // цвет логический (r,g,b,a): VK_FORMAT_B8G8R8A8_UNORM сам раскладывает
    // каналы, байты в памяти получаются B,G,R,A — как у D3DFMT_A8R8G8B8
    d.commandList->clearTextureFloat(d.surfaces[d.curRt].tex, nvrhi::TextureSubresourceSet(),
                                     nvrhi::Color(r / 255.f, g / 255.f, b / 255.f, a / 255.f));
  }
  if ((clearFlags & (bcZBuffer | bcStencil)) && d.curDs != kNoRes && d.curDs < d.surfaces.size() &&
      d.surfaces[d.curDs].tex)
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
  VK_API_GUARD;
  VK_TRACE("SetVertexShaderConstants");
  if (!pData || !registerCount || startRegister + registerCount > (unsigned)SHADER_CONST_REGISTERS)
    return D3DERR_INVALIDCALL;
  memcpy(m_constShadow[csVertex] + startRegister * 4, pData, (size_t)registerCount * 16);
  m_pImpl->uboDirty[csVertex] = true;   // банк = образ тени, байт = 16*регистр
  return S_OK;
}

HRESULT NvrhiBackend::SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData)
{
  VK_API_GUARD;
  VK_TRACE("SetPixelShaderConstants");
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
  VK_TRACE("GetShaderConstants");
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
  VK_API_GUARD;
  VK_TRACE("EnsureRecording");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.commandList) return false;
  if (!d.recording)
  {
    // Замер 2026-10-16: поток вставал ровно здесь (последняя запись кольца —
    // EnsureRecording). open() берёт командный буфер из пула NVRHI, а пул
    // пополняется только после исполнения списка на GPU: если GPU встал
    // (залипший шейдер), буферы кончаются и createCommandPool зависает в
    // драйвере. Трасса ниже отличает open() от последующих шагов.
    VK_TRACE("open >");
    d.commandList->open();
    d.recording = true;
    VK_TRACE("open <");
  }
  return true;
}

void NvrhiBackend::FlushRecording()
{
  VK_API_GUARD;
  VK_TRACE("FlushRecording");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.recording || !d.nvrhiDevice) return;
  VK_TRACE("cmd close >");
  d.commandList->close();
  VK_TRACE("cmd exec >");
  d.nvrhiDevice->executeCommandList(d.commandList);
  VK_TRACE("cmd exec <");
  d.recording = false;
}

// Шаг 13.4d: загрузка бит идёт через автоматический upload-буфер NVRHI
// (writeBuffer / writeTexture / staging), а его ячейки возвращаются в пул только
// после исполнения командного списка на GPU. Между двумя Present клиент делает
// тысячи UnlockBuffer / UnlockTexture2D (загрузка карты): список не отправлен,
// кольцо растёт, и очередной writeTexture встаёт в ожидание навсегда (замер
// 2026-10-16: загрузка `_.baked_terrain`, 3 минуты тишины, ни одного Present,
// ни одной строки в логе). Поэтому после каждой загрузки отправляем список, а
// waitIdle делаем по порогу накопленных байт.
void NvrhiBackend::NoteUpload(size_t bytes)
{
  VK_API_GUARD;
  NvrhiBackendImpl& d = *m_pImpl;
  ++d.uploads;
  d.uploadBytes += bytes;
  // Шаг 13.4d: «пульс» загрузки. Сводки DumpStats печатает Present, а во время
  // загрузки карты Present не происходит вовсе (замер: 3 минуты тишины), и
  // прирост памяти неотличим от зависания. Раз в 2000 загрузок — счётчики
  // объектов и живых зеркал.
  if (VkVerbose() && (d.uploads % 2000u) == 0u)
  {
    size_t mb = 0, mt = 0, ms = 0;
    unsigned nb = 0, nt = 0, ns = 0;
    for (size_t i = 0; i < d.buffers.size(); ++i)
      if (!d.buffers[i].dead) { mb += d.buffers[i].cpu.size(); ++nb; }
    for (size_t i = 0; i < d.textures.size(); ++i)
      if (!d.textures[i].dead) { mt += d.textures[i].mirror.size(); ++nt; }
    for (size_t i = 0; i < d.surfaces.size(); ++i)
      if (!d.surfaces[i].dead) { ms += d.surfaces[i].mirror.size(); ++ns; }
    DebugTrace("VkBeat: загруз=%u (отправок %u, ожиданий %u) объектов B/T/S=%u/%u/%u живых=%u/%u/%u",
               d.uploads, d.uploadFlushes, d.uploadWaits, (unsigned)d.buffers.size(),
               (unsigned)d.textures.size(), (unsigned)d.surfaces.size(), nb, nt, ns);
    VkRingDump(24u);
    DebugTrace("VkBeat: зеркало буферов=%u МБ текстур=%u МБ поверхностей=%u МБ",
               (unsigned)(mb >> 20), (unsigned)(mt >> 20), (unsigned)(ms >> 20));
  }
  // Шаг 13.4e: список отправляется НЕ на каждую загрузку. Прежняя политика
  // (FlushRecording на каждый Unlock) дала 24 000 отправлений за одну загрузку
  // карты: NVRHI на open() берёт отдельный командный буфер
  // (vulkan-commandlist.cpp: open() -> Queue::getOrCreateCommandBuffer, пул
  // пополняется только после исполнения на GPU), очередь не успевает исполнять,
  // буферы кончаются, и vkBeginCommandBuffer начинает отказывать. Результат
  // никто не проверяет (`(void)...begin(...)`), а winevulkan рвёт процесс:
  // `Assertion failed: !status && "vkBeginCommandBuffer",
  // dlls/winevulkan/loader_thunks.c, line 114` (замер 2026-10-16,
  // Event_6ad27247_6c000000.pwevt). Порог 4 МБ: ~400 МБ бит загрузки карты
  // дают ~100 отправлений вместо десятков тысяч.
  // Шаг 13.4e: отправляем список редко. Замеры 2026-10-16: при отправке на
  // каждые 4 МБ (2393 списка за загрузку карты) winevulkan рвёт процесс
  // ассертом `Assertion failed: !status && "vkQueueSubmit",
  // dlls/winevulkan/loader_thunks.c, line 6676` (при 64 МБ — прогон доходит).
  // Один список на кадр — это и есть модель D3D9: загрузки там тоже копятся в
  // отложенный пул и уходят на GPU разом.
  d.uploadPending += bytes;
  if (d.uploadPending >= (size_t)64u * 1024u * 1024u)
  {
    FlushRecording();
    d.uploadPending = 0;
    ++d.uploadFlushes;
    ++d.flushesSinceIdle;
    if (d.flushesSinceIdle >= 4u)
    {
      if (d.uploadWaits < 3u)
        VkNote("upload-кольцо: waitIdle по лимиту отправок", true, 0);
      ++d.uploadWaits;
      d.flushesSinceIdle = 0;
      VK_TRACE("waitIdle >");
      d.nvrhiDevice->waitForIdle();
      VK_TRACE("waitIdle <");
    }
  }
  if (d.uploadBytes >= (size_t)64u * 1024u * 1024u)
  {
    if (d.uploadWaits < 3u)
    {
      char det[96];
      snprintf(det, sizeof(det), "загрузок=%u накоплено=%u МБ", d.uploads,
               (unsigned)(d.uploadBytes >> 20));
      VkNote("upload-кольцо: принудительный waitIdle", true, det);
    }
    ++d.uploadWaits;
    d.uploadBytes = 0;
    VK_TRACE("waitIdle >");
    d.nvrhiDevice->waitForIdle();
    VK_TRACE("waitIdle <");
  }
}

HRESULT NvrhiBackend::SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride)
{
  VK_API_GUARD;
  VK_TRACE("SetStreamSource");
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
  VK_API_GUARD;
  VK_TRACE("SetIndices");
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(indexBuffer.p);
  if (idx != kNoRes && idx >= d.buffers.size()) return D3DERR_INVALIDCALL;
  d.curIb = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetInputLayout(HInputLayout layout)
{
  VK_API_GUARD;
  VK_TRACE("SetInputLayout");
  const unsigned idx = Hnd2Idx(layout.p);
  if (idx != kNoRes && idx >= m_pImpl->layoutObjs.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curLayout = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetVertexShader(HShader shader)
{
  VK_API_GUARD;
  VK_TRACE("SetVertexShader");
  const unsigned idx = Hnd2Idx(shader.p);
  if (idx != kNoRes && idx >= m_pImpl->shaderObjs.size()) return D3DERR_INVALIDCALL;
  if (idx == kNoRes)
    NoteForeign("SetVertexShader", shader.p, _ReturnAddress());
  m_pImpl->curVs = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetPixelShader(HShader shader)
{
  VK_API_GUARD;
  VK_TRACE("SetPixelShader");
  const unsigned idx = Hnd2Idx(shader.p);
  if (idx != kNoRes && idx >= m_pImpl->shaderObjs.size()) return D3DERR_INVALIDCALL;
  if (idx == kNoRes)
    NoteForeign("SetPixelShader", shader.p, _ReturnAddress());
  m_pImpl->curPs = idx;
  return S_OK;
}

// Индекс нашей картинки бэк-буфера, соответствующей текущему acquire'у
// (после пересоздания swapchain'а индексы меняются — берём из backSurfaceIdx).
static unsigned BackSurfaceIndex(NvrhiBackendImpl& d)
{
  if (d.backSurfaceIdx.empty()) return kNoRes;
  size_t i = d.imageIndex < d.backSurfaceIdx.size() ? d.imageIndex : 0u;
  return d.backSurfaceIdx[i];
}

// Ручка бэк-буфера, полученная GetBackBuffer, — это «бэк-буфер вообще», а не
// конкретная картинка: в D3D9 клиент держит её кадрами и попадает всегда в тот
// бэк-буфер, который будет Present'ован. Разворачиваем такую индекс-запись в
// картинку текущего acquire'а.
static unsigned ResolveSurfaceIdx(NvrhiBackendImpl& d, unsigned idx)
{
  if (idx == kNoRes || idx >= d.surfaces.size()) return idx;
  return d.surfaces[idx].backBufferAlias ? BackSurfaceIndex(d) : idx;
}

HRESULT NvrhiBackend::SetRenderTarget(unsigned index, HSurface surface)
{
  VK_API_GUARD;
  VK_TRACE("SetRenderTarget");
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned idx = Hnd2Idx(surface.p);
  if (idx != kNoRes && !KindOk(surface.p, (unsigned)hkSurface, "SetRenderTarget"))
    idx = kNoRes;
  if (!surface.p)
  {
    // Контракт D3D9: NULL в SetRenderTarget(0, NULL) = бэк-буфер устройства.
    // Без привязки нашей картинки клиент, рисующий «в бэк-буфер», получал
    // kNoRes и весь вывод пропадал молча (замер 2026-10-16: 98 269 пропусков
    // вывода с причиной «нет RT» при 20 вызовах SetRenderTarget).
    ++d.rtSetNull;
    idx = BackSurfaceIndex(d);
    if (idx == kNoRes) VkNote("SetRenderTarget(NULL): картинки swapchain'а нет", false, 0);
  }
  else if (idx == kNoRes)
  {
    // Ручка наша, но индекс kNoRes — её выдаёт GetRenderTarget/GetDepthStencil,
    // когда цель не выставлена. Перепривязка такой ручки = «цель по умолчанию»,
    // то есть бэк-буфер. Отличать от чужого COM-указателя обязательно: чужую
    // поверхность привязать нечем, и вывод в неё надо пропускать явно.
    const SeamHandle* h = static_cast<const SeamHandle*>(surface.p);
    if (h->magic == (unsigned)SeamHandle::kMagic)
    {
      ++d.rtSetEmpty;
      idx = BackSurfaceIndex(d);
    }
    else
    {
      ++d.rtSetForeign;
      NoteForeign("SetRenderTarget", surface.p, _ReturnAddress());
    }
  }
  else
    ++d.rtSetOwn;
  idx = ResolveSurfaceIdx(d, idx);
  if (idx != kNoRes && idx >= d.surfaces.size()) return D3DERR_INVALIDCALL;
  m_pImpl->curRt = idx;
  // реестр «слот -> поверхность» нужен GetRenderTarget (шаг 13.3): у D3D9 RT
  // принадлежат устройству, здесь их должен помнить бэкенд
  if (index < sizeof(d.rtSlot) / sizeof(d.rtSlot[0]))
    d.rtSlot[index] = idx;
  return S_OK;
}

HRESULT NvrhiBackend::SetDepthStencil(HSurface surface)
{
  VK_API_GUARD;
  VK_TRACE("SetDepthStencil");
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned idx = Hnd2Idx(surface.p);
  if (idx != kNoRes && !KindOk(surface.p, (unsigned)hkSurface, "SetDepthStencil"))
    idx = kNoRes;   // дальше попадёт в ветку «пустая ручка» -> глубина бэк-буфера
  if (!surface.p)
  {
    ++d.dsSetNull;
    idx = d.backDepthIdx;              // «глубина устройства» = наша глубина бэк-буфера
    if (idx == kNoRes) VkNote("SetDepthStencil(NULL): глубины бэк-буфера нет", false, 0);
  }
  else if (idx == kNoRes)
  {
    const SeamHandle* h = static_cast<const SeamHandle*>(surface.p);
    if (h->magic == (unsigned)SeamHandle::kMagic)
    {
      ++d.dsSetEmpty;
      idx = d.backDepthIdx;
    }
    else
    {
      ++d.dsSetForeign;
      NoteForeign("SetDepthStencil", surface.p, _ReturnAddress());
    }
  }
  else
    ++d.dsSetOwn;
  idx = ResolveSurfaceIdx(d, idx);
  if (idx != kNoRes && idx >= d.surfaces.size()) return D3DERR_INVALIDCALL;
  // В Vulkan глубина обязана быть изображением с DEPTH_STENCIL_ATTACHMENT.
  // Клиент местами привязывает сэмплируемую картинку (PostFX/тени): D3D9 это
  // терпит, Vulkan рвёт кадр (VUID-VkRenderingInfo-pDepthAttachment-06088
  // «image was not created with VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT»,
  // фактический usage — TRANSFER_SRC|TRANSFER_DST|SAMPLED). Подмена — глубина
  // бэк-буфера; счётчик показывает, насколько это вообще штатный путь.
  if (idx != kNoRes && idx < d.surfaces.size() && !d.surfaces[idx].depth)
  {
    if (d.dsNonDepth < 6u)
    {
      char det[64];
      snprintf(det, sizeof(det), "поверхность %u (формат seam=%d) — не глубина", idx,
               (int)d.surfaces[idx].seamFormat);
      VkNote("SetDepthStencil: подмена не-глубины", false, det);
    }
    ++d.dsNonDepth;
    idx = d.backDepthIdx;
  }
  d.curDs = idx;
  return S_OK;
}

HRESULT NvrhiBackend::GetDepthStencil(HSurface* pOut)
{
  VK_API_GUARD;
  VK_TRACE("GetDepthStencil");
  if (!pOut) return D3DERR_INVALIDCALL;
  // Шаг 13.4d: глубина устройства, которую ещё не ставили, — та же, что бэк-буфер
  // (backDepthIdx). Без этого клиент получал ручку с индексом kNoRes и
  // SetDepthStencil каждый кадр откатывался (замер: dsSetEmpty=899).
  unsigned idx = m_pImpl->curDs;
  if (idx == kNoRes)
    idx = m_pImpl->backDepthIdx;
  pOut->p = NewHandle(m_pImpl, idx, hkSurface);
  return S_OK;
}

HRESULT NvrhiBackend::SetInstanceSource(HBuffer perInstanceData, unsigned stride, unsigned offset)
{
  VK_API_GUARD;
  VK_TRACE("SetInstanceSource");
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
  VK_API_GUARD;
  const NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(pVertexBuffer);
  if (idx == kNoRes) return false;
  for (unsigned i = 0; i < kMaxVertexStreams; ++i)
    if (d.vb[i] == idx) return true;
  return d.instSrc == idx;
}

void NvrhiBackend::InvalidateBoundState()
{
  VK_API_GUARD;
  VK_TRACE("InvalidateBoundState");
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

// Шаг потока — часть описания потока в Vulkan (vulkan-shader.cpp:
// bindingMap[bufferIndex] = {stride=elementStride, inputRate=...}), поэтому
// раскладка различается шагами. Подпись считается по всем слотам и по шагу
// инстанса; смещения в неё НЕ входят — они относятся к привязке, а не к
// описанию.
static unsigned long long StrideSignature(const NvrhiBackendImpl& d)
{
  unsigned long long h = 1469598103934665603ull;
  const unsigned long long prime = 1099511628211ull;
  for (unsigned i = 0; i < kMaxVertexStreams; ++i) { h = (h ^ d.vbStride[i]) * prime; }
  h = (h ^ d.instStride) * prime;
  h = (h ^ d.instSlot) * prime;
  return h;
}

// Класс формата обязан совпадать с классом входа шейдера: UNORM/SNORM/FLOAT
// дают float-вход, UINT/SINT — целочисленный. Подбираем целочисленный
// аналог того же размера (порядок каналов у D3DCOLOR при этом теряется —
// отдельная задача, на геометрию не влияет).
// Проектная семантика клиента <-> имя входа SPIR-V. Писатель перенумеровывает
// COLORn в TEXCOORD(100+n) (в SM6 COLOR в профиле ps недопустим), а исходники
// местами пишут POSITION0 там, где клиентский usage — POSITION. Без этого входа
// нет совпадения, и атрибут уезжает на следующий свободный location молча.
static bool SemEq(const std::string& vsName, const char* name)
{
  if (vsName == name) return true;
  const size_t n = strlen(name);
  if (vsName.size() == n + 1u && vsName.compare(0, n, name) == 0 && vsName[n] == '0')
    return true;
  if (strncmp(name, "COLOR", 5) == 0)
  {
    char alt[24];
    snprintf(alt, sizeof(alt), "TEXCOORD%d", 100 + (name[5] ? atoi(name + 5) : 0));
    if (vsName == alt) return true;
  }
  return false;
}

static nvrhi::Format FormatIntClass(nvrhi::Format f, bool isUnsigned)
{
  switch (f)
  {
  case nvrhi::Format::BGRA8_UNORM:
  case nvrhi::Format::RGBA8_UNORM:  return isUnsigned ? nvrhi::Format::RGBA8_UINT
                                                      : nvrhi::Format::RGBA8_SINT;
  case nvrhi::Format::RG16_UNORM:   return isUnsigned ? nvrhi::Format::RG16_UINT
                                                      : nvrhi::Format::RG16_SINT;
  case nvrhi::Format::RGBA16_UNORM: return isUnsigned ? nvrhi::Format::RGBA16_UINT
                                                      : nvrhi::Format::RGBA16_SINT;
  case nvrhi::Format::R32_FLOAT:    return isUnsigned ? nvrhi::Format::R32_UINT
                                                      : nvrhi::Format::R32_SINT;
  case nvrhi::Format::RG32_FLOAT:   return isUnsigned ? nvrhi::Format::RG32_UINT
                                                      : nvrhi::Format::RG32_SINT;
  case nvrhi::Format::RGB32_FLOAT:  return isUnsigned ? nvrhi::Format::RGB32_UINT
                                                      : nvrhi::Format::RGB32_SINT;
  case nvrhi::Format::RGBA32_FLOAT: return isUnsigned ? nvrhi::Format::RGBA32_UINT
                                                      : nvrhi::Format::RGBA32_SINT;
  default:                          return f;
  }
}

// Невиртуальные помощники — свободные функции (в nvrhibackend.h типы nvrhi
// просачиваться не должны: пофайловый /std:c++17 только у nvrhibackend.cpp).
// Возвращает готовую раскладку по (индекс раскладки, подпись шагов).
static nvrhi::InputLayoutHandle BuildInputLayout(NvrhiBackendImpl& d, unsigned layoutIdx,
                                                 unsigned long long sig, unsigned vsIdx)
{
  if (layoutIdx == kNoRes || layoutIdx >= d.layoutObjs.size()) return nvrhi::InputLayoutHandle();
  for (size_t i = 0; i < d.ilCache.size(); ++i)
    if (d.ilCache[i].layout == layoutIdx && d.ilCache[i].sig == sig && d.ilCache[i].vs == vsIdx)
      return d.ilCache[i].il;
  NvrhiBackendImpl::LayoutObj& lo = d.layoutObjs[layoutIdx];
  if (lo.elems.empty()) return nvrhi::InputLayoutHandle();
  // Примечание: attrs может оказаться пустым — это легально (шейдер не читает
  // ни один атрибут, например экран-квад), createInputLayout с нулём
  // атрибутов NVRHI принимает.

  struct Attr
  {
    unsigned loc;
    nvrhi::VertexAttributeDesc vd;
  };
  const NvrhiBackendImpl::ShaderObj* pVs =
      (vsIdx != kNoRes && vsIdx < d.shaderObjs.size()) ? &d.shaderObjs[vsIdx] : 0;
  std::vector<Attr> attrs;
  unsigned nextFree = 0;
  for (size_t i = 0; i < lo.elems.size(); ++i)
  {
    const VertexElementDescriptor& e = lo.elems[i];
    char name[32];
    SemanticName((EVertexElementUsage)e.usage, e.usageIndex, name, sizeof(name));
    nvrhi::Format fmt = VtxFormat((EVertexElementType)e.type);
    if (fmt == nvrhi::Format::UNKNOWN)
    {
      char det[96];
      snprintf(det, sizeof(det), "%s (тип %d)", name, (int)e.type);
      VkNote("CreateInputLayout: формат атрибута не отображается", false, det);
      return nvrhi::InputLayoutHandle();
    }
    // location берём из SPIR-V (что назначил DXC), а не из порядка объявления:
    // NVRHI назначает location по порядку массива, и при расхождении атрибуты
    // разъезжаются молча (замер vulkan-shader.cpp: createInputLayout игнорирует
    // переданный шейдер).
    // location — из ТЕКУЩЕГО VS: DXC назначает их по своему порядку, и у разных
    // материалов одна и та же семантика стоит на разных location (прежний
    // глобальный реестр nameLoc брал номер из первого встретившегося шейдера —
    // атрибуты разъезжались молча)
    unsigned loc = kNoRes;
    unsigned kind = 0u;
    if (pVs)
      for (size_t k = 0; k < pVs->inputs.size(); ++k)
        if (SemEq(pVs->inputs[k].first, name))
        {
          loc = pVs->inputs[k].second;
          kind = (k < pVs->inputKinds.size()) ? pVs->inputKinds[k] : 0u;
          break;
        }
    // Нет входа у ЭТОГО VS — атрибут шейдером не читается, и его обязан
    // закрыть (прежний фолбэк на глобальный реестр/nextFree молча ставил
    // атрибут на чужой location: замер layout 4 — TEXCOORD2 и TEXCOORD3 оба
    // легли на location 4, VUID-Input-07904 «в pVertexInputState нет
    // Location 2, а у VS есть вход на 2»).
    if (loc == kNoRes)
    {
      if (d.ilDropped < 8u)
      {
        ++d.ilDropped;
        char det[96];
        snprintf(det, sizeof(det), "%s (vs %u)", name, vsIdx);
        VkNote("BuildInputLayout: атрибут не читается этим VS — закрыт", false, det);
      }
      continue;
    }
    if (loc + 1 > nextFree) nextFree = loc + 1;
    if (kind)
    {
      const nvrhi::Format fi = FormatIntClass(fmt, kind == 2u);
      if (fi != fmt)
      {
        char det[112];
        snprintf(det, sizeof(det), "%s: SPIR-V ждёт целочисленный вход, формат %d -> %d",
                 name, (int)fmt, (int)fi);
        VkNote("BuildInputLayout: класс формата подогнан", true, det);
        fmt = fi;
      }
    }

    const unsigned logical = e.stream;
    const unsigned slot = SlotOfStream(d, logical);
    // Инстансный атрибут без клиентского буфера — это режим счётчика
    // (BindInstanceCounter): шаг берём из встроенного счётчика, иначе
    // CreateInputLayout падает и каждый инстансный вывод теряется.
    const unsigned instStrideEff = d.instStride
        ? d.instStride
        : ((logical == kInstanceStream && d.instSrc == kNoRes) ? kInstCounterStride : 0u);
    const unsigned stride = (logical == kInstanceStream) ? instStrideEff : d.vbStride[slot];
    if (!stride)
    {
      char det[128];
      snprintf(det, sizeof(det), "%s: поток %u без шага (SetStreamSource раньше CreateInputLayout?)",
               name, logical);
      VkNote("CreateInputLayout", false, det);
      return nvrhi::InputLayoutHandle();
    }
    Attr a;
    a.loc = loc;
    a.vd.setName(name).setFormat(fmt).setOffset(e.offset).setElementStride(stride)
       .setBufferIndex(slot).setIsInstanced(logical == kInstanceStream);
    attrs.push_back(a);
  }
  if (VkVerbose() && d.ilDiag < 6u)
  {
    ++d.ilDiag;
    char det[512];
    int o = 0;
    o += snprintf(det + o, sizeof(det) - o, "layout %u vs %u (%s) входов VS %u: ",
                  layoutIdx, vsIdx, pVs ? pVs->entry.c_str() : "-",
                  pVs ? (unsigned)pVs->inputs.size() : 0u);
    for (size_t k = 0; pVs && k < pVs->inputs.size() && k < 8u && o < 400; ++k)
      o += snprintf(det + o, sizeof(det) - o, "%s@%u ", pVs->inputs[k].first.c_str(),
                    pVs->inputs[k].second);
    DebugTrace("VkIlMap: %s", det);
    o = 0;
    snprintf(det, sizeof(det), "");
    for (size_t k = 0; k < attrs.size() && k < 8u && o < 400; ++k)
      o += snprintf(det + o, sizeof(det) - o, "%s@%u(fmt %d,off %u,slot %u) ",
                    attrs[k].vd.name.c_str(), attrs[k].loc,
                    (int)attrs[k].vd.format, (unsigned)attrs[k].vd.offset,
                    (unsigned)attrs[k].vd.bufferIndex);
    DebugTrace("VkIlMap: атр: %s", det);
  }
  std::sort(attrs.begin(), attrs.end(), [](const Attr& a, const Attr& b) { return a.loc < b.loc; });

  // NVRHI назначает location = индекс в массиве (vulkan-shader.cpp), поэтому
  // пропущенный location обязан быть заполнен, иначе все последующие атрибуты
  // уезжают на соседние location (замер: VUID-VkGraphicsPipelineCreateInfo-
  // Input-07904 «в pVertexInputState нет Location 3, а у VS есть вход на 3»).
  unsigned maxLoc = 0;
  for (size_t i = 0; i < attrs.size(); ++i) if (attrs[i].loc > maxLoc) maxLoc = attrs[i].loc;
  if (attrs.size() != maxLoc + 1u)
  {
    std::vector<Attr> padded;
    for (unsigned l = 0; l <= maxLoc; ++l)
    {
      bool found = false;
      for (size_t i = 0; i < attrs.size(); ++i)
        if (attrs[i].loc == l) { padded.push_back(attrs[i]); found = true; break; }
      if (!found && !padded.empty())
      {
        padded.push_back(padded.back());   // дубликат: этот location шейдер не читает
        ++d.ilPadHoles;
      }
    }
    attrs.swap(padded);
  }

  std::vector<nvrhi::VertexAttributeDesc> vads;
  for (size_t i = 0; i < attrs.size(); ++i) vads.push_back(attrs[i].vd);
  nvrhi::InputLayoutHandle il = d.nvrhiDevice->createInputLayout(
      vads.data(), (uint32_t)vads.size(),
      d.curVs != kNoRes ? d.shaderObjs[d.curVs].sh : nullptr);
  if (!il)
  {
    VkNote("CreateInputLayout: createInputLayout", false, 0);
    return nvrhi::InputLayoutHandle();
  }
  // рост кэша под контролем: при переборе шагов на каждый draw кэш обязан
  // сбрасываться, иначе он разрастается до числа draw
  if (d.ilCache.size() > 512)
  {
    d.ilCache.clear();
    VkNote("ilCache очищен (>512 раскладок)", false, 0);
  }
  NvrhiBackendImpl::IlEntry ent;
  ent.layout = layoutIdx; ent.sig = sig; ent.vs = vsIdx; ent.il = il;
  d.ilCache.push_back(ent);
  ++d.layoutsBuilt;
  lo.il = il;   // для диагностики: «последняя построенная»
  lo.desc = vads;   // см. LayoutObj::desc — тот же массив, что ушёл в драйвер
  return il;
}

// Нужен ли текущей раскладке поток данных экземпляра (атрибуты с logical
// потоком kInstanceStream). Спрашивается на выводе: там решают, тянуть ли
// встроенный счётчик.
static bool LayoutWantsInstanceStream(const NvrhiBackendImpl& d)
{
  if (d.curLayout >= d.layoutObjs.size()) return false;
  const std::vector<VertexElementDescriptor>& e = d.layoutObjs[d.curLayout].elems;
  for (size_t i = 0; i < e.size(); ++i)
    if (e[i].stream == kInstanceStream) return true;
  return false;
}

// Встроенный счётчик экземпляров 0..N-1 (шаг 9 очереди): клиент зовёт
// BindInstanceCounter() -> SetInstanceSource(пусто, 0, 0), инстансные атрибуты
// раскладки при этом остаются (InstancedMeshResource). В D3D9 счётчик заводит
// бэкенд (D3D9Backend::EnsureInstanceCounter); в seam'е его не было ->
// CreateInputLayout падал («поток 4294967295 без шага») и каждый инстансный
// вывод терялся (замер: 96894 отказа за бой, сцена без юнитов/башен).
static bool EnsureInstCounter(NvrhiBackendImpl& d, unsigned count)
{
  const unsigned entries = count < 1024u ? 1024u : count;
  if (d.instCounter && d.instCounterEntries >= entries) return true;
  d.instCounter = nullptr;
  d.instCounterEntries = 0;
  d.instCounter = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                              .setByteSize((size_t)entries * kInstCounterStride)
                                              .setIsVertexBuffer(true)
                                              .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                              .setKeepInitialState(true)
                                              .setDebugName("seam instance counter"));
  if (!d.instCounter)
  {
    VkNote("EnsureInstCounter: createBuffer", false, 0);
    return false;
  }
  std::vector<int> v(entries);
  for (unsigned n = 0; n < entries; ++n) v[n] = (int)n;
  d.commandList->writeBuffer(d.instCounter, v.data(), (size_t)entries * kInstCounterStride);
  d.instCounterEntries = entries;
  VkNote("EnsureInstCounter: счётчик заведён", true, 0);
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
  // Параметры первых запросов: в режиме переворота
  // клиент встал внутри createTexture (трасса: пять входов в
  // CreateTexture2D без выхода) — без чисел не отличить абсурдный запрос
  // от заблокировки драйвера.
  static unsigned s_texReqLogged = 0u;
  if (VkVerbose() && s_texReqLogged < 40u)
  {
    ++s_texReqLogged;
    char req[96];
    snprintf(req, sizeof(req), "%ux%u mip %u usage %d формат %d пул %d%s",
             width, height, mipLevels, (int)usage, (int)format, (int)pool,
             cube ? " cube" : "");
    DebugTrace("VkTexReq: %s", req);
  }
  nvrhi::TextureDesc td;
  td.setDimension(cube ? nvrhi::TextureDimension::TextureCube : nvrhi::TextureDimension::Texture2D)
    .setWidth(width).setHeight(height).setFormat(fmt)
    // mipLevels=0 у D3D9 означает «автоогенация»; без запроса mipmap'ов берём 1,
    // иначе NVRHI заведёт цепочку, которую никто не заполняет
    .setMipLevels(mipLevels ? mipLevels : 1u)
    // cube без arraySize=6 невалиден: NVRHI ставит CUBE_COMPATIBLE, а слоёв
    // оставляет 1 (VUID-VkImageCreateInfo-flags-08866 «flags includes
    // VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT but arrayLayers (1) is less than 6»),
    // из-за чего и загрузка грани падала на baseArrayLayer=1
    // (VUID-vkCmdCopyBufferToImage-imageSubresource-07968).
    .setArraySize(cube ? 6u : 1u);
  if (usage & usageRenderTarget)
    td.setIsRenderTarget(true).setInitialState(nvrhi::ResourceStates::RenderTarget);
  else
    td.setInitialState(nvrhi::ResourceStates::ShaderResource);
  // D24S8-текстуру (карты теней) клиент создаёт как обычную текстуру, а привязывает
  // как глубину. Без isRenderTarget у NVRHI usage = SAMPLED|TRANSFER, и рендер-пасс
  // с такой глубиной невалиден: VUID-VkRenderingInfo-pDepthAttachment-06088
  // («image was created with TRANSFER_SRC|TRANSFER_DST|SAMPLED»), -pStencilAttachment-06089.
  // Глубинный формат обязан давать DEPTH_STENCIL_ATTACHMENT; SAMPLED остаётся
  // (isShaderResource по умолчанию true) — тень выбирается из той же текстуры.
  if (fmt == nvrhi::Format::D24S8 || fmt == nvrhi::Format::D16
      || fmt == nvrhi::Format::D32 || fmt == nvrhi::Format::D32S8)
  {
    td.setIsRenderTarget(true);
    td.setInitialState(nvrhi::ResourceStates::DepthWrite);
    if (d.texMissing < 8u) VkNote("CreateTexture: глубинный формат — добавлен DEPTH_STENCIL", true, 0);
  }
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
    VK_TRACE("CreateTexture: fail");
    return E_FAIL;
  }
  if ((usage & usageAutoGenMipmap) && d.texMissing < 8u)
    VkNote("CreateTexture: автогенерация mipmap через seam не переведена (шаг 13)", false, 0);
  NvrhiBackendImpl::TextureObj to;
  to.tex = tex; to.fmt = fmt; to.w = width; to.h = height;
  to.mipLevels = mipLevels ? mipLevels : 1u; to.cube = cube;
  to.seamFormat = format; to.usage = usage; to.pool = pool;
  d.textures.push_back(to);
  pOut->p = NewHandle(&d, (unsigned)d.textures.size() - 1u, hkTexture);
  VK_TRACE("CreateTexture: out");
  return S_OK;
}

HRESULT NvrhiBackend::CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                      EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                      HTexture* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreateTexture2D");
  return CreateTextureCommon(*m_pImpl, width, height, mipLevels, usage, format, pool, false, pOut);
}

HRESULT NvrhiBackend::CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                        EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                        HTexture* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreateTextureCube");
  return CreateTextureCommon(*m_pImpl, edgeLen, edgeLen, mipLevels, usage, format, pool, true, pOut);
}

HRESULT NvrhiBackend::UpdateTexture(HTexture srcTexture, HTexture dstTexture)
{
  VK_API_GUARD;
  VK_TRACE("UpdateTexture");
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

// Сжатые форматы: единица хранения — блок 4x4, а не пиксель. D3D9 DXT1 = 8
// байт на блок, DXT3/DXT5 = 16 (см. SurfaceFormat: bfDXT1..5 -> BC1/BC2/BC3).
// Именно такие байты отдаёт и ждёт DDS-загрузчик (ReadDDS.cpp) и D3D9 LockRect
// на сжатой текстуре, поэтому зеркало побайтово совпадает с D3D9-путём.
static unsigned BytesPerBlockOf(nvrhi::Format f)
{
  switch (f)
  {
    case nvrhi::Format::BC1_UNORM:                          return 8u;
    case nvrhi::Format::BC2_UNORM:
    case nvrhi::Format::BC3_UNORM:                          return 16u;
    default:                                                return 0u;
  }
}

// Размеры mip-уровня: D3D9 и Vulkan считают одинаково — halving с минимумом 1
// (не «деление с округлением до блока», блоки на краях просто неполные).
static void LevelDims(unsigned w, unsigned h, unsigned level, unsigned* pW, unsigned* pH)
{
  *pW = (w >> level) ? (w >> level) : 1u;
  *pH = (h >> level) ? (h >> level) : 1u;
}

// Загрузка бит в 2D-текстуру. В D3D9 это LockRect/UnlockRect у COM-текстуры
// (seam закрыл их правкой renderbackend.h), в Vulkan — CPU-зеркало и
// copyTexture на Unlock. Поддержаны все уровни mip-цепочки и сжатые форматы
// (шаг 13.4d: без этого DDS-загрузчик получал уровень 0 и молча проваливал
// остальные — 18046 отказа за прогон, кадр чёрный). Грани куба через этот метод
// по-прежнему не заполняются.
static const char* kLockWhy[] = { "", "формат через seam не грузится", "грань куба не переведена",
                                  "уровень вне mip-цепочки" };

// определение ниже (рядом с UnlockTexture2D); значения по умолчанию — там
static void TexDumpLevel0(NvrhiBackendImpl& d, unsigned idx,
                          const NvrhiBackendImpl::TextureObj& to,
                          unsigned level, unsigned rowBytes,
                          const void* src, size_t srcSize);

// Пакет загрузки чужого потока закрыт и отправлен. Вызывается с того же потока,
// что и писал (список однопо́точный); executeCommandList — VkQueueSubmit,
// VkQueue по спецификации потокобезопасен.
static bool SubmitThreadUpload(NvrhiBackendImpl& d, unsigned tid, const char* why)
{
  std::map<unsigned, NvrhiBackendImpl::ThreadUpload>::iterator it = d.threadUpload.find(tid);
  if (it == d.threadUpload.end() || !it->second.cl || !it->second.entries) return true;
  NvrhiBackendImpl::ThreadUpload& ctx = it->second;
  const size_t bytes = ctx.bytes;
  const unsigned entries = ctx.entries;
  if (!ctx.open) return true;
  ctx.cl->close();
  d.nvrhiDevice->executeCommandList(ctx.cl);
  ctx.open = false;
  ctx.bytes = 0; ctx.entries = 0; ctx.lastTex = (unsigned)kNoRes;
  ++d.uploadSubmits;
  if (d.uploadSubmits <= 5u)
    DebugTrace("UpSubmit: #%u байт %u записей %u", d.uploadSubmits,
               (unsigned)bytes, entries);
  VK_TRACE("upload submit (%s) байт %zu записей %u", why, bytes, entries);
  return true;
}

// Загрузка уровня текстуры с чужого потока: собственный командный список потока.
static bool UploadTextureHere(NvrhiBackendImpl& d, unsigned tid, unsigned t,
                              unsigned face, unsigned level, unsigned rowBytes)
{
  static const size_t kBatchBytes = 8u * 1024u * 1024u;
  NvrhiBackendImpl::ThreadUpload& ctx = d.threadUpload[tid];
  if (!ctx.cl)
  {
    ctx.cl = d.nvrhiDevice->createCommandList(nvrhi::CommandListParameters()
                                                .setEnableImmediateExecution(false));
    if (!ctx.cl) { VkNote("UnlockTexture2D: createCommandList", false, 0); return false; }
  }
  if (ctx.entries && ctx.lastTex != t) SubmitThreadUpload(d, tid, "смена текстуры");
  if (!ctx.open) { ctx.cl->open(); ctx.open = true; }   // open() НЕ повторять (см. ThreadUpload::open)
  ctx.cl->writeTexture(d.textures[t].tex, face, level, &d.textures[t].mirror[0],
                       (size_t)rowBytes);
  ctx.bytes += d.textures[t].mirror.size();
  ++ctx.entries; ctx.lastTex = t;
  ++d.textures[t].uploads;
  if (d.textures[t].uploads == 1u)
    // DebugTrace — PrintfFormatter: %zu НЕ поддерживается (печатает «zu» и
    // сдвигает остальные аргументы), размеры — (unsigned)
    DebugTrace("TexUp: #%u lvl%u face%u шаг%u байт%u %ux%u fmt%d mips%u (поток загрузки)",
               t, level, face, rowBytes, (unsigned)d.textures[t].mirror.size(),
               d.textures[t].w, d.textures[t].h, (int)d.textures[t].seamFormat,
               d.textures[t].mipLevels);
  if (level == 0u) TexDumpLevel0(d, t, d.textures[t], level, rowBytes, 0, 0);
  // счётчики как в NvrhiBackend::NoteUpload (метод класса, из статического
  // хелпера недоступен; принудительный flush общего списка тут не нужен —
  // пакет закрывается сам)
  ++d.uploads;
  d.uploadBytes += d.textures[t].mirror.size();
  // PW_VK_TEXCHECK=<индекс шва>: сразу после отправки чтенияем обратно эту же
  // текстуру. Разделяет «writeTexture не дошёл до VkImage» и «дошло, но потом
  // затёрто» (замер прогона 92: у всех текстур с mipLevels>=2 CPU-зеркало
  // заполнено, а GPU нулевой — при этом у mipLevels==1 CPU==GPU совпадают).
  static unsigned checkIdx[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  static bool checkInit = false;
  if (!checkInit)
  {
    checkInit = true;
    char cn[64]; cn[0] = 0;
    if (GetEnvironmentVariableA("PW_VK_TEXCHECK", cn, sizeof(cn) - 1) && cn[0])
    {
      unsigned k = 0;
      for (char* s = strtok(cn, ","); s && k < 4u; s = strtok(0, ","), ++k)
        checkIdx[k] = (unsigned)atoi(s);
    }
  }
  bool checked = false;
  for (unsigned k = 0; k < 4u; ++k) if (checkIdx[k] == t) checked = true;
  if (checked && level == 0u)
  {
    SubmitThreadUpload(d, tid, "PW_VK_TEXCHECK");
    d.nvrhiDevice->waitForIdle();
    const nvrhi::TextureDesc td = d.textures[t].tex->getDesc();
    nvrhi::StagingTextureHandle st = d.nvrhiDevice->createStagingTexture(
        nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
                            .setWidth(td.width).setHeight(td.height).setFormat(td.format)
                            .setMipLevels(1)
                            .setInitialState(nvrhi::ResourceStates::CopyDest)
                            .setKeepInitialState(true)
                            .setDebugName("seam tex check"),
        nvrhi::CpuAccessMode::Read);
    if (st)
    {
      ctx.cl->open();
      ctx.cl->copyTexture(st, nvrhi::TextureSlice(), d.textures[t].tex, nvrhi::TextureSlice());
      ctx.cl->close();
      ctx.open = false;
      d.nvrhiDevice->executeCommandList(ctx.cl);
      d.nvrhiDevice->waitForIdle();
      size_t cpitch = 0;
      void* cpx = d.nvrhiDevice->mapStagingTexture(st, nvrhi::TextureSlice(),
                                                   nvrhi::CpuAccessMode::Read, &cpitch);
      if (cpx)
      {
        const bool comp = (td.format >= nvrhi::Format::BC1_UNORM &&
                           td.format <= nvrhi::Format::BC7_UNORM);
        const unsigned rows = comp ? ((td.height + 3u) / 4u) : td.height;
        const unsigned long bytes = (unsigned long)cpitch * rows;
        unsigned nz = 0;
        const unsigned char* p = (const unsigned char*)cpx;
        for (unsigned long i = 0; i < bytes; ++i) nz += (p[i] != 0);
        unsigned cnz = 0;
        for (size_t i = 0; i < d.textures[t].mirror.size(); ++i)
          cnz += (d.textures[t].mirror[i] != 0);
        DebugTrace("TexCheck: #%u lvl0 GPU ненулевых %u из %u, CPU %u из %u (%ux%u fmt%u mips%u)",
                   t, nz, bytes, cnz, (unsigned)d.textures[t].mirror.size(),
                   td.width, td.height, (unsigned)td.format, d.textures[t].mipLevels);
        d.nvrhiDevice->unmapStagingTexture(st);
      } else DebugTrace("TexCheck: #%u map не удалось", t);
    } else DebugTrace("TexCheck: #%u staging не создан", t);
  }
  if (ctx.bytes >= kBatchBytes) SubmitThreadUpload(d, tid, "порог пакета");
  return true;
}

HRESULT NvrhiBackend::LockTexture2D(HTexture texture, unsigned level, unsigned face,
                                    const BackendRect* pRect,
                                    ERenderLockType mode, void** ppData, unsigned* pRowPitch)
{
  VK_API_GUARD;
  VK_TRACE("LockTexture2D");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!texture.p || !ppData) return E_INVALIDARG;
  *ppData = 0;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex) return D3DERR_INVALIDCALL;
  // пакет загрузки чужого потока закрывается на смене текстуры: загрузчик
  // (ReadDDS.cpp FaceHolder::Fill) идёт по уровням одной текстуры подряд,
  // поэтому «другая текстура» = конец цепочки предыдущей
  if (GetCurrentThreadId() != d.ownerThread)
  {
    std::map<unsigned, NvrhiBackendImpl::ThreadUpload>::iterator it =
        d.threadUpload.find(GetCurrentThreadId());
    if (it != d.threadUpload.end() && it->second.entries && it->second.lastTex != t)
      SubmitThreadUpload(d, GetCurrentThreadId(), "смена текстуры");
  }
  NvrhiBackendImpl::TextureObj& to = d.textures[t];
  const unsigned bpp = BppOf(to.fmt);
  const unsigned bpb = BytesPerBlockOf(to.fmt);
  const char* why = 0;
  if (!bpp && !bpb)      why = kLockWhy[1];
  // Шаг 13.4e: грани куб-мапа грузятся тем же путём — у writeTexture есть
  // arraySlice, а mirror одно (загрузчик блокирует уровни и грани строго
  // последовательно). Для 2D грань обязана быть нулевой, иначе молча
  // записали бы не туда.
  else if (to.cube ? (face >= 6u) : (face != 0u)) why = kLockWhy[2];
  else if (level >= to.mipLevels) why = kLockWhy[3];
  if (why)
  {
    char det[128];
    // в заметке — seam-формат: у nvrhi::Format другая нумерация (в логе было
    // «формат=60», что читалось как мусор из enum seam'а)
    snprintf(det, sizeof(det), "%s: level=%u face=%u cube=%d формат=%d цепочка=%u",
             why, level, face, (int)to.cube, (int)to.seamFormat, to.mipLevels);
    VkNote("LockTexture2D: вне Vulkan-пути", false, det);
    // VkNote подавляет повторы (32 слота), а именно отказ в блокировке и есть
    // кандидат на «текстура создана, но пуста» — первые 40 случаев печатаем
    // безусловно, с RVA вызывающего.
    static unsigned s_lockFail = 0;
    if (s_lockFail < 40u)
    {
      ++s_lockFail;
      DebugTrace("LockTexture2D: ОТКАЗ #%u #%u %ux%u fmt%d mips%u %s RVA 0x%x",
                 s_lockFail, t, to.w, to.h, (int)to.seamFormat, to.mipLevels, det,
                 (unsigned)((size_t)_ReturnAddress() - (size_t)GetModuleHandle(0)));
    }
    return E_FAIL;
  }

  unsigned lw = 0, lh = 0;
  LevelDims(to.w, to.h, level, &lw, &lh);
  // Частичная блокировка сжатой текстуры Possible только по границам блоков
  // (D3D9 так и требует); незапечатанные форматы — произвольный прямоугольник.
  long x0 = 0, y0 = 0, x1 = (long)lw, y1 = (long)lh;
  if (pRect)
  {
    if (pRect->left > x0)   x0 = pRect->left;
    if (pRect->top > y0)    y0 = pRect->top;
    if (pRect->right < x1)  x1 = pRect->right;
    if (pRect->bottom < y1) y1 = pRect->bottom;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    if (bpb)
    {
      x0 = (x0 / 4) * 4; y0 = (y0 / 4) * 4;
      x1 = ((x1 + 3) / 4) * 4; y1 = ((y1 + 3) / 4) * 4;
      if (x1 > (long)lw) x1 = (long)lw;
      if (y1 > (long)lh) y1 = (long)lh;
    }
  }
  unsigned rowBytes = 0, rows = 0;
  if (bpb)
  {
    rowBytes = ((lw + 3u) / 4u) * bpb;
    rows     = (lh + 3u) / 4u;
  }
  else
  {
    rowBytes = lw * bpp;
    rows     = lh;
  }
  to.mirror.resize((size_t)rowBytes * rows);
  to.locked = true;
  to.lockLevel = level; to.lockRows = rows; to.lockRowBytes = rowBytes;
  to.lockFace = to.cube ? face : 0u;
  to.lockX = x0; to.lockY = y0; to.lockW = x1 - x0; to.lockH = y1 - y0;
  const unsigned unit = bpb ? bpb : bpp;
  const unsigned unitPerRow = bpb ? 1u : (unsigned)lw;   // блоков в строке уже учтено в rowBytes
  const size_t off = bpb ? ((size_t)(y0 / 4) * rowBytes + (size_t)(x0 / 4) * unit)
                         : ((size_t)y0 * rowBytes + (size_t)x0 * unit);
  *ppData = off < to.mirror.size() ? &to.mirror[off] : &to.mirror[0];
  if (pRowPitch) *pRowPitch = rowBytes;
  (void)unitPerRow;
  // режим блокировки — намерение (locktype.h): зеркало всегда полное, поэтому
  // DISCARD/NOOVERWRITE различать нечем
  (void)mode;
  return S_OK;
}

// Отладка (2026-10-17): дамп того, что реально уходит в GPU по writeTexture.
// Кадр выходил чёрным при корректных привязках и корректных константах — нужно
// отличить «текстура на GPU чёрная» (битая загрузка/формат) от «привязка/UV не
// те». env PW_VK_TEXDUMP=<файл>: дописывается уровень 0 каждой текстуры, не
// более kTexDumpMax записей и не более kTexDumpCap байт на запись (иначе
// 2048x2048 DXT-цепочка съедает диск).
static void TexDumpLevel0(NvrhiBackendImpl& d, unsigned idx,
                          const NvrhiBackendImpl::TextureObj& to,
                          unsigned level, unsigned rowBytes,
                          const void* src = 0, size_t srcSize = 0)
{
  static const unsigned kTexDumpMax = 600u;
  static const size_t   kTexDumpCap = 16u * 1024u * 1024u;
  // src: дамп идёт из очереди отложенных загрузок, где байты уровня лежат
  // отдельно (зеркало к этому моменту уже переиспользовано следующим Lock'ом).
  const unsigned char* psrc = src ? static_cast<const unsigned char*>(src)
                                  : (to.mirror.empty() ? 0 : &to.mirror[0]);
  const size_t nbytes = src ? srcSize : to.mirror.size();
  if (d.texDumpN >= kTexDumpMax || !psrc || !nbytes) return;
  // PW_VK_TEXDUMP_MIN=<N>: только текстуры шире N пикселей. Первые 60 записей
  // дампа (прогон 74) оказались мелкими иконками, а в дампе с min=512 (прогон
  // 76) 242 записи из 400 съела одна и та же 205x205 карта (перезаливается каждый
  // кадр) — поэтому фильтр применяется к КАЖДОЙ записи (раньше — только к
  // первой, пока не открыт файл) и добавлена дедупликация по (idx,level,размер).
  static unsigned s_minW = 0xFFFFFFFFu;
  if (s_minW == 0xFFFFFFFFu)
  {
    char mn[MAX_PATH];
    unsigned v = 0u;
    if (GetEnvironmentVariableA("PW_VK_TEXDUMP_MIN", mn, sizeof(mn) - 1))
      v = (unsigned)atoi(mn);
    s_minW = v;
  }
  if (to.w < s_minW) return;
  static std::set<unsigned long long> s_seen;
  const unsigned long long key =
      ((unsigned long long)idx << 40) | ((unsigned long long)level << 32)
    | ((unsigned long long)to.w << 16) | to.h;
  if (!s_seen.insert(key).second) return;
  if (!d.texDump)
  {
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("PW_VK_TEXDUMP", path, sizeof(path) - 1)) return;
    d.texDump = fopen(path, "wb");
    if (!d.texDump) return;
    fwrite("TXDP", 1, 4, d.texDump);
    const unsigned ver = 1u;
    fwrite(&ver, 4, 1, d.texDump);
  }
  const size_t n = nbytes > kTexDumpCap ? kTexDumpCap : nbytes;
  const unsigned rec[9] = { idx, to.w, to.h, to.mipLevels,
                            (unsigned)to.seamFormat, to.cube ? 1u : 0u,
                            rowBytes, level, (unsigned)n };
  fwrite(rec, 4, 9, d.texDump);
  fwrite(psrc, 1, n, d.texDump);
  fflush(d.texDump);
  ++d.texDumpN;
  char det[80];
  snprintf(det, sizeof(det), "#%u %ux%u lvl%u формат%u байт %u",
           idx, to.w, to.h, level, (unsigned)to.seamFormat, (unsigned)n);
  VkNote("TexDump", false, det);
}

HRESULT NvrhiBackend::UnlockTexture2D(HTexture texture, unsigned level, unsigned face)
{
  VK_API_GUARD;
  VK_TRACE("UnlockTexture2D");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!texture.p) return E_INVALIDARG;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::TextureObj& to = d.textures[t];
  if (!to.locked || to.mirror.empty()) return E_FAIL;
  to.locked = false;
  if (to.lockLevel != level)
  {
    char det[80];
    snprintf(det, sizeof(det), "Lock был level=%u, Unlock для level=%u", to.lockLevel, level);
    VkNote("UnlockTexture2D: уровень не совпал", false, det);
    return E_FAIL;
  }
  unsigned lw = 0, lh = 0;
  LevelDims(to.w, to.h, level, &lw, &lh);
  // Сжатые форматы (DXT1/3/5 = BC1/BC2/BC3) staging-текстурой НЕ грузятся:
  // Шаг 13.4d: загрузка уровня делается через ICommandList::writeTexture, а НЕ
  // через createStagingTexture + copyTexture. Причина — стоимость staging:
  // staging-текстура у NVRHI это отдельное host-visible выделение
  // (vulkan-staging-texture.cpp:111 -> createBuffer с cpuAccess), а драйвер
  // выделяет их кратно 16 МБ (замер 2026-10-16: 1300 отображений по 16 МБ в
  // pmap = 20,8 ГБ RSS, хост 31 ГБ, загрузка карты вала в своп и вставала на
  // `_.baked_terrain`). writeTexture режет данные через переиспользуемые чанки
  // автоматического upload-буфера — одно выделение на тысячи загрузок.
  // Зеркало уровня полное (шаг строки = полный шаг уровня), поэтому прямоугольник
  // частичной блокировки перекрывается целиком.
  if (GetCurrentThreadId() != d.ownerThread)
  {
    VkNoteThread(d, "UnlockTexture2D");
    to.needUpload = true;
    // загрузка своим списком этого потока (см. ThreadUpload в impl): очередь
    // «до исполнения рендер-потоком» больше не используется
    if (!UploadTextureHere(d, GetCurrentThreadId(), t, to.lockFace, level,
                           to.lockRowBytes))
      return E_FAIL;
    return S_OK;
  }
  if (!EnsureRecording()) return E_FAIL;
  VK_TRACE("writeTexture >");
  d.commandList->writeTexture(to.tex, to.lockFace, level, &to.mirror[0], to.lockRowBytes);
  if (level == 0u) TexDumpLevel0(d, t, to, level, to.lockRowBytes);
  VK_TRACE("writeTexture <");
  ++to.uploads;
  // Замер прогона 81: у ВСЕХ текстур (в том числе чёрных на GPU) загруз=1, а
  // CPU-снимка уровня 0 по ним нет — значит единственный writeTexture ушёл на
  // ДРУГОЙ уровень. Печатаем первый уровень каждой текстуры + RVA вызывающего.
  if (to.uploads == 1u)
    DebugTrace("TexUp: #%u lvl%u face%u шаг%u зеркало%zu %ux%u fmt%d mips%u RVA 0x%x",
               t, level, to.lockFace, to.lockRowBytes, to.mirror.size(), to.w, to.h,
               (int)to.seamFormat, to.mipLevels,
               (unsigned)((size_t)_ReturnAddress() - (size_t)GetModuleHandle(0)));
  NoteUpload(to.mirror.size());
  return S_OK;
}

// Описание текстуры (шаг 13.2): клиенту нужен D3DSURFACE_DESC-эквивалент — по
// нему текстура пересоздаётся после потери устройства (Render/texture.cpp) и
// проверяется пул в деструкторе. Значения возвращаем те, что просили при
// создании: у Vulkan-текстуры COM-описания нет.
HRESULT NvrhiBackend::GetTextureInfo(HTexture texture, BackendTextureInfo* pInfo)
{
  VK_API_GUARD;
  VK_TRACE("GetTextureInfo");
  if (!pInfo) return E_INVALIDARG;
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex)
  {
    NoteForeign("GetTextureInfo: ручка не из этого бэкенда", texture.p, _ReturnAddress());
    return E_FAIL;
  }
  const NvrhiBackendImpl::TextureObj& to = d.textures[t];
  pInfo->width  = to.w;
  pInfo->height = to.h;
  pInfo->levels = to.mipLevels;
  pInfo->format = to.seamFormat;
  pInfo->usage  = to.usage;
  pInfo->pool   = to.pool;
  pInfo->cube   = to.cube;
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
  VK_API_GUARD;
  VK_TRACE("GenerateMipmaps");
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
  VK_API_GUARD;
  VK_TRACE("SetTexture");
  NvrhiBackendImpl& d = *m_pImpl;
  if (samplerIndex >= kMaxSamplerSlots)
  {
    char det[64];
    snprintf(det, sizeof(det), "слот %u вне 0..%u", samplerIndex, kMaxSamplerSlots - 1u);
    VkNote("SetTexture", false, det);
    return D3DERR_INVALIDCALL;
  }
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes && texture.p)
    NoteForeign("SetTexture", texture.p, _ReturnAddress());
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
      case rkTexture: {
        nvrhi::BindingLayoutItem li = nvrhi::BindingLayoutItem::Texture_SRV(it.binding);
        li.setSize(it.count ? it.count : 1u);
        ld.bindings.push_back(li);
        break;
      }
      case rkSampler: {
        nvrhi::BindingLayoutItem li = nvrhi::BindingLayoutItem::Sampler(it.binding);
        li.setSize(it.count ? it.count : 1u);
        ld.bindings.push_back(li);
        break;
      }
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

  // набор сэмплеров НЕ статичен: с шагом 13.4b состояния приходят через seam,
  // поэтому набор строится по подписи состояний (FindOrCreateSamplerSet)
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

//////////////////////////////////////////////////////////////////////////
// Модель состояний (шаг 13.4b): перевод чисел D3D9 в перечисления NVRHI.
//
// Числа в BackendPipelineState — D3D9-ные намеренно: менеджер состояний устроен
// по D3D9, а нейтральный словарь состояний — этап 4, когда переводить станет
// нечего. Проверено по Vendor/DirectX/Include/d3d9types.h:264-284 и
// Vendor/NVRHI/include/nvrhi/nvrhi.h:1211-1234: нумерация сравнений, операций
// со stencil, множителей и операций смешивания в NVRHI ДОСЛОВНО совпадает с
// D3D9 (в том числе D3DSTENCILOP_INCRSAT=4 -> IncrementAndClamp=4 и
// D3DSTENCILOP_INCR=7 -> IncrementAndWrap=7 — порядок у них разный, но числа
// подобраны так, что совпадают). Верить этому совпадению молча нельзя, поэтому
// ниже стоят static_assert: если NVRHI сменит нумерацию, сборка падает здесь, а
// не начинает рисовать инверсные сравнения. Для fill/cull/адресации/фильтров
// перечисления разные — они переводятся таблицей.

static void NoteStateIssue(NvrhiBackendImpl& d, const char* what, unsigned value)
{
  ++d.stateUnsupported;
  if (d.stateUnsupported <= 8u)
  {
    char det[96];
    snprintf(det, sizeof(det), "%s = %u", what, value);
    VkNote("Состояния: аналога у Vulkan нет", false, det);
  }
}

static_assert((unsigned)nvrhi::ComparisonFunc::Never == D3DCMP_NEVER &&
              (unsigned)nvrhi::ComparisonFunc::Always == D3DCMP_ALWAYS,
              "нумерация nvrhi::ComparisonFunc разошлась с D3DCMP_*");
static_assert((unsigned)nvrhi::StencilOp::Keep == D3DSTENCILOP_KEEP &&
              (unsigned)nvrhi::StencilOp::IncrementAndClamp == D3DSTENCILOP_INCRSAT &&
              (unsigned)nvrhi::StencilOp::DecrementAndClamp == D3DSTENCILOP_DECRSAT &&
              (unsigned)nvrhi::StencilOp::Invert == D3DSTENCILOP_INVERT &&
              (unsigned)nvrhi::StencilOp::IncrementAndWrap == D3DSTENCILOP_INCR &&
              (unsigned)nvrhi::StencilOp::DecrementAndWrap == D3DSTENCILOP_DECR,
              "нумерация nvrhi::StencilOp разошлась с D3DSTENCILOP_*");
static_assert((unsigned)nvrhi::BlendFactor::Zero == D3DBLEND_ZERO &&
              (unsigned)nvrhi::BlendFactor::SrcAlphaSaturate == D3DBLEND_SRCALPHASAT &&
              (unsigned)nvrhi::BlendFactor::ConstantColor == D3DBLEND_BLENDFACTOR &&
              (unsigned)nvrhi::BlendFactor::InvConstantColor == D3DBLEND_INVBLENDFACTOR,
              "нумерация nvrhi::BlendFactor разошлась с D3DBLEND_*");
static_assert((unsigned)nvrhi::BlendOp::Add == D3DBLENDOP_ADD &&
              (unsigned)nvrhi::BlendOp::Max == D3DBLENDOP_MAX,
              "нумерация nvrhi::BlendOp разошлась с D3DBLENDOP_*");

static float FloatBits(unsigned raw)
{
  float f = 0.f;
  memcpy(&f, &raw, sizeof(f));
  return f;
}

static nvrhi::ComparisonFunc CmpOf(NvrhiBackendImpl& d, unsigned d3dCmp)
{
  if (d3dCmp < (unsigned)nvrhi::ComparisonFunc::Never ||
      d3dCmp > (unsigned)nvrhi::ComparisonFunc::Always)
  {
    NoteStateIssue(d, "функция сравнения", d3dCmp);
    return nvrhi::ComparisonFunc::Less;
  }
  return static_cast<nvrhi::ComparisonFunc>(d3dCmp);
}

static nvrhi::StencilOp StencilOpOf(NvrhiBackendImpl& d, unsigned d3dOp)
{
  // 0 в модели — «состояние не запрашивалось» (D3DSTENCILOP_KEEP = 1), а не
  // «операция вне словаря»; для пары без трафарета это нормальная ситуация
  if (d3dOp == 0u) return nvrhi::StencilOp::Keep;
  if (d3dOp < (unsigned)nvrhi::StencilOp::Keep ||
      d3dOp > (unsigned)nvrhi::StencilOp::DecrementAndWrap)
  {
    NoteStateIssue(d, "операция трафарета", d3dOp);
    return nvrhi::StencilOp::Keep;
  }
  return static_cast<nvrhi::StencilOp>(d3dOp);
}

static nvrhi::BlendFactor BlendOf(NvrhiBackendImpl& d, unsigned d3dFactor)
{
  // D3DBLEND_BOTHSRCALPHA(12) и BOTHINVSRCALPHA(13) — режимы D3D9, у которых
  // нет аналога в D3D11/Vulkan (у NVRHI на этих числах другие множители),
  // поэтому они переводятся явно и считаются потерей
  if (d3dFactor == D3DBLEND_BOTHSRCALPHA || d3dFactor == D3DBLEND_BOTHINVSRCALPHA)
  {
    NoteStateIssue(d, "режим смешивания (устарел)", d3dFactor);
    return d3dFactor == D3DBLEND_BOTHSRCALPHA ? nvrhi::BlendFactor::SrcAlpha
                                              : nvrhi::BlendFactor::InvSrcAlpha;
  }
  if (d3dFactor < (unsigned)nvrhi::BlendFactor::Zero ||
      d3dFactor > (unsigned)nvrhi::BlendFactor::InvConstantColor)
  {
    NoteStateIssue(d, "множитель смешивания", d3dFactor);
    return nvrhi::BlendFactor::One;
  }
  return static_cast<nvrhi::BlendFactor>(d3dFactor);
}

static nvrhi::RasterCullMode CullOf(NvrhiBackendImpl& d, unsigned d3dCull)
{
  switch (d3dCull)
  {
  case D3DCULL_NONE: return nvrhi::RasterCullMode::None;
  // намотка у нас frontCounterClockwise=true (см. FillPipelineState), т.е.
  // «видимые против часовой» = лицевые, и D3DCULL_CW снимает задние
  case D3DCULL_CW:  return nvrhi::RasterCullMode::Back;
  case D3DCULL_CCW: return nvrhi::RasterCullMode::Front;
  default:
    NoteStateIssue(d, "режим отсечения", d3dCull);
    return nvrhi::RasterCullMode::None;
  }
}

static nvrhi::RasterFillMode FillOf(NvrhiBackendImpl& d, unsigned d3dFill)
{
  if (d3dFill == D3DFILL_WIREFRAME) return nvrhi::RasterFillMode::Wireframe;
  if (d3dFill != D3DFILL_SOLID) NoteStateIssue(d, "режим растеризации", d3dFill);
  return nvrhi::RasterFillMode::Solid;
}

// ОТЛАДКА (PW_VK_SAMPLER_LOD0=1): все сэмплеры принудительно берут mip 0.
// Причина: клиент создаёт текстуры с D3DUSAGE_AUTOGENMIPMAP, а автогенерация
// через seam не переведена (замер прогона 58: 144 заметки), т.е. у текстур с
// mipLevels=8..11 заполнен только уровень 0, а производные UV (вид сверху на весь
// мир ~13650x10240 юнитов в 1024x768 пикселей) выбирают глубокий mip —
// неинициализированный (чёрный). В Vulkan итоговый LOD = bias + производная и
// зажимается [minLod, maxLod] = [0, VK_LOD_CLAMP_NONE], поэтому -1000 даёт ровно
// уровень 0. Только локальный стенд, в релиз не входит.
static bool SamplerLod0Forced()
{
  static int v = -1;
  if (v < 0)
  {
    char b[8];
    b[0] = 0;
    GetEnvironmentVariableA("PW_VK_SAMPLER_LOD0", b, sizeof(b) - 1);
    v = (b[0] == '1') ? 1 : 0;
  }
  return v == 1;
}

static nvrhi::SamplerAddressMode AddressOf(NvrhiBackendImpl& d, unsigned d3dAddr)
{
  switch (d3dAddr)
  {
  case D3DTADDRESS_WRAP:       return nvrhi::SamplerAddressMode::Wrap;
  case D3DTADDRESS_MIRROR:     return nvrhi::SamplerAddressMode::Mirror;
  case D3DTADDRESS_CLAMP:      return nvrhi::SamplerAddressMode::Clamp;
  case D3DTADDRESS_BORDER:     return nvrhi::SamplerAddressMode::Border;
  case D3DTADDRESS_MIRRORONCE: return nvrhi::SamplerAddressMode::MirrorOnce;
  default:
    NoteStateIssue(d, "режим адресации", d3dAddr);
    return nvrhi::SamplerAddressMode::Clamp;
  }
}

// подпись одного сэмплера (для кэша объектов сэмплеров)
static unsigned long long SamplerKeyOf(const BackendSamplerState& s)
{
  unsigned long long h = 1469598103934665603ull;
  const unsigned vals[] = { s.minFilter, s.magFilter, s.mipFilter, s.addressU,
                            s.addressV, s.addressW, s.borderColor, s.maxAnisotropy,
                            (unsigned)s.maxMipLevel, (unsigned)s.lodBias,
                            s.set ? 1u : 0u };
  for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i)
  {
    h ^= vals[i];
    h *= 1099511628211ull;
  }
  return h;
}

static nvrhi::SamplerHandle FindOrCreateSampler(NvrhiBackendImpl& d, unsigned slot)
{
  const BackendSamplerState& s = d.model.sampler[slot];
  if (!s.set) return d.defSampler;      // этого слота модель не касалась
  const unsigned long long key = SamplerKeyOf(s);
  for (size_t i = 0; i < d.samplerCache.size(); ++i)
    if (d.samplerCache[i].first == key) return d.samplerCache[i].second;

  nvrhi::SamplerDesc sd;
  sd.setMinFilter(s.minFilter == D3DTEXF_LINEAR || s.minFilter == D3DTEXF_ANISOTROPIC)
    .setMagFilter(s.magFilter == D3DTEXF_LINEAR || s.magFilter == D3DTEXF_ANISOTROPIC)
    .setMipFilter(s.mipFilter != D3DTEXF_NONE)
    .setAddressU(AddressOf(d, s.addressU))
    .setAddressV(AddressOf(d, s.addressV))
    .setAddressW(AddressOf(d, s.addressW));
  if (s.maxAnisotropy > 1u)
  {
    // Vulkan требует линейные фильтры при анизотропной фильтрации
    sd.setMaxAnisotropy((float)s.maxAnisotropy).setAllFilters(true);
  }
  // D3DSAMP_BORDERCOLOR — это не цвет, а константа D3DTOP_* (0 = TRANSPARENTBLACK,
  // 1 = OPAQUEBLACK, 2 = TRANSPARENTWHITE, 3 = OPAQUEWHITE); в вендоренном
  // d3d9types.h этих имён нет, поэтому числа
  switch (s.borderColor)
  {
  case 0u: sd.setBorderColor(nvrhi::Color(0.f, 0.f, 0.f, 0.f)); break;
  case 1u: sd.setBorderColor(nvrhi::Color(0.f, 0.f, 0.f, 1.f)); break;
  case 2u: sd.setBorderColor(nvrhi::Color(1.f, 1.f, 1.f, 0.f)); break;
  case 3u: sd.setBorderColor(nvrhi::Color(1.f, 1.f, 1.f, 1.f)); break;
  default: NoteStateIssue(d, "цвет границы", s.borderColor); break;
  }
  // lodBias в модели — биты float: клиент пишет float прямо в
  // D3DSAMP_MIPMAPLODBIAS (PFRenderInterface.cpp:1613), поэтому интерпретация
  // побитовая, а не «16.16 с дробной частью в младших битах»
  const float bias = FloatBits((unsigned)s.lodBias);
  if (bias != 0.f) sd.setMipBias(bias);
  if (SamplerLod0Forced()) sd.setMipBias(-1000.f).setMipFilter(false);

  nvrhi::SamplerHandle sh = d.nvrhiDevice->createSampler(sd);
  if (!sh)
  {
    VkNote("Состояния: createSampler", false, 0);
    return d.defSampler;
  }
  d.samplerCache.push_back(std::make_pair(key, sh));
  ++d.samplersCreated;
  return sh;
}

// подпись всех сэмплеров, которые реально нужны этой паре шейдеров
static unsigned long long SamplerSetSignature(NvrhiBackendImpl& d, const NvrhiBackendImpl::ResMapObj& rm)
{
  unsigned long long h = 1469598103934665603ull;
  for (size_t k = 0; k < rm.items.size(); ++k)
  {
    const NvrhiBackendImpl::ResItem& it = rm.items[k];
    if (it.set != 3u || it.kind != rkSampler) continue;
    unsigned long long sk = 0;
    if (it.binding < kMaxSamplerSlots) sk = SamplerKeyOf(d.model.sampler[it.binding]);
    h ^= sk;
    h *= 1099511628211ull;
    h ^= it.binding;
    h *= 1099511628211ull;
  }
  return h ? h : 1ull;
}

static nvrhi::BindingSetHandle FindOrCreateSamplerSet(NvrhiBackendImpl& d, const NvrhiBackendImpl::ResMapObj& rm,
                                                      unsigned long long sig)
{
  for (size_t i = 0; i < d.smpSetCache.size(); ++i)
    if (d.smpSetCache[i].first == sig) return d.smpSetCache[i].second;
  if (d.smpSetCache.size() > 4096u)
  {
    d.smpSetCache.clear();
    VkNote("smpSetCache очищен (>4096 подписей)", true, 0);
  }
  nvrhi::BindingSetDesc bsd;
  for (size_t k = 0; k < rm.items.size(); ++k)
  {
    const NvrhiBackendImpl::ResItem& it = rm.items[k];
    if (it.set != 3u || it.kind != rkSampler) continue;
    nvrhi::SamplerHandle sh = (it.binding < kMaxSamplerSlots) ? FindOrCreateSampler(d, it.binding)
                                                              : d.defSampler;
    if (!sh) sh = d.defSampler;
    const unsigned cnt = it.count ? it.count : 1u;
    for (unsigned e = 0; e < cnt; ++e)
    {
      nvrhi::BindingSetItem bi = nvrhi::BindingSetItem::Sampler(it.binding, sh);
      bi.arrayElement = e;
      bsd.bindings.push_back(bi);
    }
  }
  nvrhi::BindingSetHandle bs = d.nvrhiDevice->createBindingSet(bsd, rm.layoutSmp);
  if (!bs)
  {
    VkNote("Состояния: createBindingSet(сэмплеры)", false, 0);
    return nvrhi::BindingSetHandle();
  }
  d.smpSetCache.push_back(std::make_pair(sig, bs));
  ++d.smpSetsCreated;
  return bs;
}

// Собирает nvrhi::RenderState из модели. Ноль в полях модели — «состояние не
// запрашивалось», но тень заполняется чтением устройства, поэтому для
// состояний с дефолтом D3D9 = 1 (ZENABLE, ZWRITEENABLE) ноль означает именно
// «выключено»: расхождение возможно только если чтение устройства не удалось.
static void FillPipelineState(NvrhiBackendImpl& d, nvrhi::GraphicsPipelineDesc& pd)
{
  const BackendPipelineState& st = d.model;

  nvrhi::RasterState& rs = pd.renderState.rasterState;
  // намотка: у NVRHI вьюпорт с отрицательной высотой, без frontCounterClockwise
  // геометрия вырезается (замер спайка b0cull: 0 против 3072 пикселей)
  rs.setFrontCounterClockwise(true)
    .setScissorEnable(st.scissorTestEnable != 0)
    .setFillMode(FillOf(d, st.fillMode ? st.fillMode : D3DFILL_SOLID))
    .setCullMode(CullOf(d, st.cullMode ? st.cullMode : D3DCULL_NONE));
  // имя сеттера в этой версии NVRHI — setSlopeScaleDepthBias (без «d» в
  // середине), хотя поле называется slopeScaledDepthBias

  // D3DRS_DEPTHBIAS и D3DRS_SLOPESCALEDEPTHBIAS клиент пишет БИТАМИ float
  // (ShadowMap.cpp:238-239), поэтому интерпретация побитовая. У NVRHI
  // постоянный сдвиг целочисленный (nvrhi.h:1149) — дробная часть теряется, и
  // об этом печатаем, иначе тень в A/B расходится молча
  rs.setSlopeScaleDepthBias(st.slopeScaleDepthBias);
  const float db = FloatBits(st.depthBiasRaw);
  rs.setDepthBias((int)(db >= 0.f ? db + 0.5f : db - 0.5f));
  if (db != 0.f && db != (float)(int)db)
  {
    char det[64];
    snprintf(det, sizeof(det), "constant depth bias %g усечён до %d", (double)db, (int)db);
    VkNote("Состояния: дробная часть потеряна", false, det);
    ++d.stateUnsupported;
  }

  nvrhi::DepthStencilState& ds = pd.renderState.depthStencilState;
  ds.depthTestEnable = st.zEnable != 0;
  ds.depthWriteEnable = st.zWriteEnable != 0;
  ds.depthFunc = st.zFunc ? CmpOf(d, st.zFunc) : nvrhi::ComparisonFunc::Less;
  ds.stencilEnable = st.stencilEnable != 0;
  ds.stencilReadMask = (uint8_t)st.stencilReadMask;
  ds.stencilWriteMask = (uint8_t)st.stencilWriteMask;
  ds.stencilRefValue = (uint8_t)st.stencilRef;
  // D3D9 не различает лицевые и обратные грани в трафарете (D3DRS_STENCIL* —
  // общие), поэтому второй набор повторяет первый; StencilOpDesc — вложенный
  // тип DepthStencilState, отдельного nvrhi::StencilOpDesc в этой версии нет
  nvrhi::DepthStencilState::StencilOpDesc& front = ds.frontFaceStencil;
  front.failOp = StencilOpOf(d, st.stencilFail);
  front.depthFailOp = StencilOpOf(d, st.stencilZFail);
  front.passOp = StencilOpOf(d, st.stencilPass);
  front.stencilFunc = st.stencilFunc ? CmpOf(d, st.stencilFunc) : nvrhi::ComparisonFunc::Always;
  // D3D9 не различает лицевые и обратные грани в трафарете (D3DRS_STENCIL* —
  // общие), поэтому второй набор повторяет первый
  ds.backFaceStencil = front;

  nvrhi::BlendState::RenderTarget target;
  target.blendEnable = st.alphaBlendEnable != 0;
  target.srcBlend = st.srcBlend ? BlendOf(d, st.srcBlend) : nvrhi::BlendFactor::One;
  target.destBlend = st.destBlend ? BlendOf(d, st.destBlend) : nvrhi::BlendFactor::Zero;
  target.blendOp = st.blendOp ? static_cast<nvrhi::BlendOp>(st.blendOp) : nvrhi::BlendOp::Add;
  if (st.separateAlphaBlend)
  {
    target.srcBlendAlpha = st.srcBlendAlpha ? BlendOf(d, st.srcBlendAlpha) : target.srcBlend;
    target.destBlendAlpha = st.destBlendAlpha ? BlendOf(d, st.destBlendAlpha) : target.destBlend;
    target.blendOpAlpha = st.blendOpAlpha ? static_cast<nvrhi::BlendOp>(st.blendOpAlpha) : target.blendOp;
  }
  else
  {
    target.srcBlendAlpha = target.srcBlend;
    target.destBlendAlpha = target.destBlend;
    target.blendOpAlpha = target.blendOp;
  }
  // D3D9 по умолчанию пишет RGB без альфы (7), а не всё (0x0f)
  target.colorWriteMask = static_cast<nvrhi::ColorMask>(
      st.colorWriteEnable ? st.colorWriteEnable : 0x7u);
  pd.renderState.blendState.setRenderTarget(0, target);

  // фиксированной альфа-смены ни в Vulkan, ни в D3D12 нет: ALPHATESTENABLE
  // обязан переехать в шейдер (шаг 13/14), иначе материал рисуется без вырезания
  if (st.alphaTestEnable) NoteStateIssue(d, "альфа-тест (ALPHATESTENABLE)", st.alphaRef);
  if (st.clipPlaneEnable) NoteStateIssue(d, "плоскость отсечения (CLIPPLANEENABLE)", st.clipPlaneEnable);
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
    {
      pTex = d.textures[tex].tex;
      // Тип view обязан совпадать с тем, что объявил шейдер (VUID-...-07752):
      // D3D9 терпит «sampler2D по cube-текстуре» и наоборот, Vulkan — нет.
      const bool wantCube = (it.dim == 3u);   // SPV Dim: 3 = Cube (не 4 — там Rect)
      if (wantCube != d.textures[tex].cube)
      {
        if (d.texDimMismatch < 6u)
        {
          char det[80];
          snprintf(det, sizeof(det), "слот %u: шейдер %s, привязана %s", it.binding,
                   wantCube ? "cube" : "2D", d.textures[tex].cube ? "cube" : "2D");
          VkNote("Draw: тип текстуры не совпал с шейдером", false, det);
          ++d.texDimMismatch;
        }
        pTex = wantCube ? d.dummyCubeTex : d.dummyTex;
        if (!pTex) pTex = d.textures[tex].tex;
      }
    }
    else if (d.texMissing < 8u)
    {
      char det[64];
      snprintf(det, sizeof(det), "слот %u не привязан", it.binding);
      VkNote("Draw: текстура в набор", false, det);
      ++d.texMissing;
    }
    // каждый элемент массива дескрипторов обязан быть записан
    // (VUID-vkCmdDrawIndexed-None-08114: «descriptor ... never been updated»)
    const unsigned cnt = it.count ? it.count : 1u;
    for (unsigned e = 0; e < cnt; ++e)
    {
      nvrhi::BindingSetItem bi = nvrhi::BindingSetItem::Texture_SRV(it.binding, pTex);
      bi.arrayElement = e;
      bsd.bindings.push_back(bi);
    }
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
  // подпись шагов потоков обязана входить в ключ: раскладка в Vulkan несёт шаг
  // потока, и без неё два VB с разными шагами получили бы один пайплайн
  const unsigned long long lsig = StrideSignature(d);
  for (size_t i = 0; i < d.pipes.size(); ++i)
  {
    const NvrhiBackendImpl::PipeObj& p = d.pipes[i];
    if (p.vs == d.curVs && p.ps == d.curPs && p.layout == d.curLayout &&
        p.lsig == lsig && p.stateHash == d.modelHash &&
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
  // раскладка живёт в кэше по (индекс, подпись шагов) и в пайплайн её обязан
  // получить именно оттуда: у Vulkan-бэкенда `if (inputLayout)`
  // (vulkan-graphics.cpp:300) и при нулевом указателе пайплайн создаётся
  // БЕСШТРИХОВЫМ — вершинные атрибуты читаются нулями, геометрия вырождается
  // молча (замер 2026-10-15: 0 пикселей при пайплайне 1/1 и выводе 1)
  il = BuildInputLayout(d, d.curLayout, lsig, d.curVs);
  if (!il) { VK_TRACE("il ОТКАЗ"); return nvrhi::GraphicsPipelineHandle(); }
  VK_TRACE("il ok");

  if (d.curRt >= d.surfaces.size()) return nvrhi::GraphicsPipelineHandle();

  // Разделяющий эксперимент (шаг 14): поток встал ровно внутри
  // d.nvrhiDevice->createFramebuffer (кольцо: `fb >` без `fb <`; nvidia-smi
  // при этом 0% и 269 МиБ — блокировка, а не залипший шейдер). Это ПЕРВЫЙ
  // вывод в картинку swapchain'а: прежде все выводы шли в offscreen
  // (`вывод: в бэк-буфер=0`). Приложение печатается всегда (первые 12 раз),
  // чтобы отличить native-обёртку swapchain-изображения от обычной поверхности.
  {
    static unsigned s_fbTrace = 0u;
    if (VkVerbose() && s_fbTrace < 12u)
    {
      ++s_fbTrace;
      DebugTrace("VkFb: rt=%u %ux%u back=%d ds=%u dsWH=%ux%u", d.curRt,
                 d.surfaces[d.curRt].w, d.surfaces[d.curRt].h,
                 (int)d.surfaces[d.curRt].backBuffer, d.curDs,
                 (d.curDs != kNoRes && d.curDs < d.surfaces.size()) ? d.surfaces[d.curDs].w : 0u,
                 (d.curDs != kNoRes && d.curDs < d.surfaces.size()) ? d.surfaces[d.curDs].h : 0u);
    }
  }
  nvrhi::FramebufferDesc fbd;
  fbd.addColorAttachment(d.surfaces[d.curRt].tex);
  if (d.curDs != kNoRes && d.curDs < d.surfaces.size())
    fbd.setDepthAttachment(d.surfaces[d.curDs].tex);
  VK_TRACE("fb >");
  nvrhi::FramebufferHandle fb = d.nvrhiDevice->createFramebuffer(fbd);
  VK_TRACE("fb <");
  if (!fb) { VkNote("Пайплайн: createFramebuffer", false, 0); return nvrhi::GraphicsPipelineHandle(); }

  nvrhi::GraphicsPipelineDesc pd;
  pd.setPrimType(PrimType(type)).setInputLayout(il)
    .setVertexShader(d.shaderObjs[d.curVs].sh).setPixelShader(d.shaderObjs[d.curPs].sh);
  // Намотка: у NVRHI вьюпорт с отрицательной высотой, без флага frontCounterClockwise
  // геометрия вырезается (замер спайка b0cull: 0 против 3072 пикселей). Остальное
  // — из модели состояний (шаг 13.4b): на Vulkan blend, глубина и трафарет —
  // часть объекта пайплайна, а не динамическое состояние
  FillPipelineState(d, pd);
  if (d.set0Layout) pd.addBindingLayout(d.set0Layout);
  const int rmIdx = FindOrCreateResMap(d);
  VK_TRACE("resmap");
  if (rmIdx < 0) VK_TRACE("resmap ОТКАЗ");
  if (rmIdx >= 0)
  {
    if (d.resMaps[rmIdx].layoutTex) pd.addBindingLayout(d.resMaps[rmIdx].layoutTex);
    if (d.resMaps[rmIdx].layoutSmp) pd.addBindingLayout(d.resMaps[rmIdx].layoutSmp);
    if (d.resMaps[rmIdx].layoutSsbo) pd.addBindingLayout(d.resMaps[rmIdx].layoutSsbo);
  }

  VK_TRACE("pipe >");
  nvrhi::GraphicsPipelineHandle pipe = d.nvrhiDevice->createGraphicsPipeline(pd, fb->getFramebufferInfo());
  VK_TRACE("pipe <");
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
  po.lsig = lsig;
  po.stateHash = d.modelHash;
  d.pipes.push_back(po);
  ++d.pipesCreated;
  if (ppFb) *ppFb = fb;
  return pipe;
}

// Исполнение отложенных загрузок — только на потоке-владельце.
void NvrhiBackend::FlushPendingUploads()
{
  VK_TRACE("FlushPending");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return;
  if (d.pendBuf.empty() && d.pendSrf.empty()) return;
  for (size_t i = 0; i < d.pendBuf.size(); ++i)
  {
    const unsigned idx = d.pendBuf[i];
    if (idx >= d.buffers.size()) continue;
    NvrhiBackendImpl::BufferObj& bo = d.buffers[idx];
    if (!bo.needUpload || bo.dead || !bo.buf || bo.cpu.empty()) continue;
    bo.needUpload = false;
    if (!EnsureRecording()) return;   // NoteUpload ниже мог закрыть список
    d.commandList->writeBuffer(bo.buf, bo.cpu.data(), bo.cpu.size(), bo.off);
    NoteUpload(bo.cpu.size());
  }
  for (size_t i = 0; i < d.pendSrf.size(); ++i)
  {
    const unsigned idx = d.pendSrf[i];
    if (idx >= d.surfaces.size()) continue;
    SurfaceObj& so = d.surfaces[idx];
    if (!so.needUpload || so.dead || !so.tex || so.mirror.empty()) continue;
    so.needUpload = false;
    const unsigned bpp = BppOf(so.fmt);
    if (!bpp) continue;
    if (!EnsureRecording()) return;   // NoteUpload ниже мог закрыть список
    d.commandList->writeTexture(so.tex, 0, 0, &so.mirror[0], (size_t)so.w * bpp);
    NoteUpload(so.mirror.size());
  }
  d.pendBuf.clear();
  d.pendSrf.clear();
}

////////////////////////////////////////////////////////////////////
// Рекордер вывода (шаг 14, env PW_VK_RECORD=<файл>).
//
// Рендер-отладчик работает только нативно: под Wine слой RenderDoc активен
// (winevulkan идёт через хостовый загрузчик), но гость — Windows PE — не может
// вызвать RENDERDOC_GetAPI, а без app-API слой кадр не сериализует (замер
// 2026-10-17: слой в списке инстанса, клиент жив 120+ с, .rdc нет ни в одном
// каталоге). Поэтому выводы выгружаются в файл, а рисует их нативный реплеер
// tools/render_replay — он и снимает капчу.
//
// Формат: "PWRD" + u32 версия + u32 ширина + u32 высота swapchain'а, далее
// последовательные записи: u32 тип + поля (u32/f32, little-endian). Ресурс
// выгружается ОДИН РАЗ, вывод ссылается на него по id:
//   1 SHADER : id, стадия (0 VS / 1 PS), entry, SPIR-V
//   2 BUFFER : id, isIndex, index32, байты CPU-зеркала
//   3 TEXTURE: id, w, h, mipLevels, cube, seamFormat, байты уровня 0
//   4 LAYOUT : id, количество, (stream, offset, type, usage, usageIndex)
//   5 DRAW   : см. RecDraw
//   6 END
// Смещения потоков пишутся ЛОГИЧЕСКИМИ (внутри seam-буфера): в бэкенде VB/IB
// лежат в слэбах, а реплеер делает каждому буферу отдельный nvrhi-буфер, так
// что slab-смещение (BufferObj::off) в файл не попадает.
enum {
  kRecShader = 1, kRecBuffer = 2, kRecTexture = 3, kRecLayout = 4,
  kRecDraw = 5, kRecEnd = 6
};

// Потолок блоба: геометрия карты в сумме — гигабайты, а смысл реплея — несколько
// выводов. При превышении размер пишется настоящий, байты — нет; реплеер по
// отсутствующим байтам подставляет dummy и говорит об этом.
static const size_t kRecBlobCap = 8u * 1024u * 1024u;

static void RecU32(FILE* f, unsigned v) { fwrite(&v, 4, 1, f); }
static void RecF32(FILE* f, float v) { fwrite(&v, 4, 1, f); }
static void RecBlob(FILE* f, const void* p, size_t n)
{
  size_t w = n;
  if (w > kRecBlobCap) w = kRecBlobCap;
  RecU32(f, (unsigned)n);
  RecU32(f, (unsigned)w);
  if (w && p) fwrite(p, 1, w, f);
}
static unsigned RecLookup(const std::vector<std::pair<unsigned, unsigned> >& m, unsigned idx)
{
  for (size_t i = 0; i < m.size(); ++i)
    if (m[i].first == idx) return m[i].second;
  return 0xffffffffu;
}

static unsigned RecShaderId(NvrhiBackendImpl& d, unsigned idx, unsigned stage)
{
  if (idx >= d.shaderObjs.size()) return 0xffffffffu;
  unsigned id = RecLookup(d.recShader, idx);
  if (id != 0xffffffffu) return id;
  id = (unsigned)d.recShader.size();
  d.recShader.push_back(std::make_pair(idx, id));
  const NvrhiBackendImpl::ShaderObj& so = d.shaderObjs[idx];
  RecU32(d.recFile, kRecShader);
  RecU32(d.recFile, id);
  RecU32(d.recFile, stage);
  RecBlob(d.recFile, so.entry.empty() ? "" : so.entry.c_str(), so.entry.size());
  RecBlob(d.recFile, so.spv.empty() ? 0 : &so.spv[0], so.spv.size());
  ++d.recObjs;
  return id;
}

static unsigned RecBufferId(NvrhiBackendImpl& d, unsigned idx)
{
  if (idx >= d.buffers.size()) return 0xffffffffu;
  unsigned id = RecLookup(d.recBuffer, idx);
  if (id != 0xffffffffu) return id;
  id = (unsigned)d.recBuffer.size();
  d.recBuffer.push_back(std::make_pair(idx, id));
  const NvrhiBackendImpl::BufferObj& bo = d.buffers[idx];
  RecU32(d.recFile, kRecBuffer);
  RecU32(d.recFile, id);
  RecU32(d.recFile, bo.isIndex ? 1u : 0u);
  RecU32(d.recFile, bo.index32 ? 1u : 0u);
  RecBlob(d.recFile, bo.cpu.empty() ? 0 : &bo.cpu[0], bo.cpu.size());
  ++d.recObjs;
  return id;
}

static unsigned RecTextureId(NvrhiBackendImpl& d, unsigned idx)
{
  if (idx >= d.textures.size()) return 0xffffffffu;
  unsigned id = RecLookup(d.recTexture, idx);
  if (id != 0xffffffffu) return id;
  id = (unsigned)d.recTexture.size();
  d.recTexture.push_back(std::make_pair(idx, id));
  const NvrhiBackendImpl::TextureObj& to = d.textures[idx];
  RecU32(d.recFile, kRecTexture);
  RecU32(d.recFile, id);
  RecU32(d.recFile, to.w);
  RecU32(d.recFile, to.h);
  RecU32(d.recFile, to.mipLevels);
  RecU32(d.recFile, to.cube ? 1u : 0u);
  RecU32(d.recFile, (unsigned)to.seamFormat);
  RecBlob(d.recFile, to.mirror.empty() ? 0 : &to.mirror[0], to.mirror.size());
  ++d.recObjs;
  return id;
}

static unsigned RecLayoutId(NvrhiBackendImpl& d, unsigned idx)
{
  if (idx >= d.layoutObjs.size()) return 0xffffffffu;
  unsigned id = RecLookup(d.recLayout, idx);
  if (id != 0xffffffffu) return id;
  id = (unsigned)d.recLayout.size();
  d.recLayout.push_back(std::make_pair(idx, id));
  const NvrhiBackendImpl::LayoutObj& lo = d.layoutObjs[idx];
  RecU32(d.recFile, kRecLayout);
  RecU32(d.recFile, id);
  RecU32(d.recFile, (unsigned)lo.elems.size());
  for (size_t k = 0; k < lo.elems.size(); ++k)
  {
    const VertexElementDescriptor& e = lo.elems[k];
    RecU32(d.recFile, e.stream);
    RecU32(d.recFile, e.offset);
    RecU32(d.recFile, (unsigned)e.type);
    RecU32(d.recFile, (unsigned)e.usage);
    RecU32(d.recFile, e.usageIndex);
  }
  RecU32(d.recFile, (unsigned)lo.desc.size());
  for (size_t k = 0; k < lo.desc.size(); ++k)
  {
    const nvrhi::VertexAttributeDesc& a = lo.desc[k];
    RecBlob(d.recFile, a.name.empty() ? "" : a.name.c_str(), a.name.size());
    RecU32(d.recFile, (unsigned)a.format);
    RecU32(d.recFile, a.arraySize);
    RecU32(d.recFile, a.bufferIndex);
    RecU32(d.recFile, (unsigned)a.offset);
    RecU32(d.recFile, (unsigned)a.elementStride);
    RecU32(d.recFile, a.isInstanced ? 1u : 0u);
  }
  ++d.recObjs;
  return id;
}

void NvrhiBackend::RecOpen(NvrhiBackendImpl& d)
{
  char path[512];
  if (GetEnvironmentVariableA("PW_VK_RECORD", path, sizeof(path) - 1) <= 0) return;
  d.recFile = fopen(path, "wb");
  if (!d.recFile)
  {
    VkNote("VkRec: файл записи не открыт", false, path);
    return;
  }
  d.recOn = true;
  d.recMax = 40u;
  char rm[16];
  if (GetEnvironmentVariableA("PW_VK_RECORD_MAX", rm, sizeof(rm) - 1) > 0)
    d.recMax = (unsigned)atoi(rm);
  char rr[32];
  if (GetEnvironmentVariableA("PW_VK_RECORD_RT", rr, sizeof(rr) - 1) > 0)
    sscanf(rr, "%ux%u", &d.recRtW, &d.recRtH);
  // пропуск первых N выводов: лимит выводов выгорает на загрузке, а нужен бой
  // (замер: 79 записанных выводов — все MainPass_VSMain/MainPass_PSMain из
  // экрана загрузки, боевые в запись не попали)
  char sk[16];
  if (GetEnvironmentVariableA("PW_VK_RECORD_SKIP", sk, sizeof(sk) - 1) > 0)
    d.recSkip = (unsigned)atoi(sk);
  fwrite("PWRD", 1, 4, d.recFile);
  // версия 2: в LAYOUT добавлен готовый массив nvrhi-атрибутов (lo.desc),
  // в DRAW — список ресурсов набора (set/binding/kind/dim/count)
  RecU32(d.recFile, 2u);
  // размер swapchain'а на момент открытия (может быть 0x0 — swapchain создаётся
  // после Init); реплеер берёт размер цели из каждой записи вывода
  RecU32(d.recFile, d.width);
  RecU32(d.recFile, d.height);
  DebugTrace("VkRec: вывод пишется в %s (лимит %u, цель %ux%u)", path, d.recMax, d.recRtW, d.recRtH);
}

void NvrhiBackend::RecClose(NvrhiBackendImpl& d)
{
  if (!d.recFile) return;
  RecU32(d.recFile, kRecEnd);
  fclose(d.recFile);
  d.recFile = 0;
  d.recOn = false;
  DebugTrace("VkRec: записано выводов=%u ресурсов=%u", d.recDraws, d.recObjs);
}

void NvrhiBackend::RecDraw(NvrhiBackendImpl& d, ERenderPrimitiveType type, bool indexed,
                           unsigned startIndexLocation, unsigned startVertexLocation,
                           unsigned primitiveCount, unsigned vtx)
{
  if (!d.recFile || d.recDraws >= d.recMax) return;
  ++d.recDraws;
  // Ресурсы пишутся ДО тела вывода: id считаются как побочный эффект (запись
  // ресурса уходит в файл), поэтому без предварительного сбора идентификаторов
  // тело вывода перемешалось бы с записями ресурсов.
  const unsigned vsId = RecShaderId(d, d.curVs, 0u);
  const unsigned psId = RecShaderId(d, d.curPs, 1u);
  const unsigned layId = RecLayoutId(d, d.curLayout);
  unsigned streamId[kMaxVertexStreams];
  for (unsigned i = 0; i < (unsigned)kMaxVertexStreams; ++i)
    streamId[i] = (d.vb[i] != kNoRes && d.vb[i] < d.buffers.size()) ? RecBufferId(d, d.vb[i]) : 0xffffffffu;
  const unsigned ibId = (d.curIb != kNoRes && d.curIb < d.buffers.size()) ? RecBufferId(d, d.curIb) : 0xffffffffu;
  const unsigned instId = (d.instSrc != kNoRes && d.instSrc < d.buffers.size()) ? RecBufferId(d, d.instSrc) : 0xffffffffu;
  unsigned texId[kBackendSamplerSlots];
  for (unsigned i = 0; i < (unsigned)kBackendSamplerSlots; ++i)
    texId[i] = (d.texBound[i] != kNoRes && d.texBound[i] < d.textures.size()) ? RecTextureId(d, d.texBound[i]) : 0xffffffffu;
  RecU32(d.recFile, kRecDraw);
  RecU32(d.recFile, d.recDraws);
  RecU32(d.recFile, vsId);
  RecU32(d.recFile, psId);
  RecU32(d.recFile, layId);
  const unsigned rtI = (d.curRt < d.surfaces.size()) ? d.curRt : kNoRes;
  const unsigned dsI = (d.curDs < d.surfaces.size()) ? d.curDs : kNoRes;
  RecU32(d.recFile, (rtI == kNoRes) ? 0u : d.surfaces[rtI].w);
  RecU32(d.recFile, (rtI == kNoRes) ? 0u : d.surfaces[rtI].h);
  RecU32(d.recFile, (rtI == kNoRes) ? 0u : (d.surfaces[rtI].depth ? 1u : 0u));
  RecU32(d.recFile, (dsI == kNoRes) ? 0u : d.surfaces[dsI].w);
  RecU32(d.recFile, (dsI == kNoRes) ? 0u : d.surfaces[dsI].h);
  RecU32(d.recFile, (dsI == kNoRes) ? 0u : (d.surfaces[dsI].depth ? 1u : 0u));
  RecU32(d.recFile, (unsigned)type);
  RecU32(d.recFile, indexed ? 1u : 0u);
  RecU32(d.recFile, startIndexLocation);
  RecU32(d.recFile, startVertexLocation);
  RecU32(d.recFile, primitiveCount);
  RecU32(d.recFile, vtx);
  RecU32(d.recFile, m_instanceCount ? m_instanceCount : 1u);
  RecU32(d.recFile, (unsigned)m_viewport.x);
  RecU32(d.recFile, (unsigned)m_viewport.y);
  RecU32(d.recFile, (unsigned)m_viewport.width);
  RecU32(d.recFile, (unsigned)m_viewport.height);
  RecF32(d.recFile, m_viewport.minZ);
  RecF32(d.recFile, m_viewport.maxZ);
  RecU32(d.recFile, m_scissorValid ? 1u : 0u);
  RecU32(d.recFile, (unsigned)m_scissor.left);
  RecU32(d.recFile, (unsigned)m_scissor.top);
  RecU32(d.recFile, (unsigned)m_scissor.right);
  RecU32(d.recFile, (unsigned)m_scissor.bottom);
  RecU32(d.recFile, (unsigned)kMaxVertexStreams);
  for (unsigned i = 0; i < (unsigned)kMaxVertexStreams; ++i)
  {
    RecU32(d.recFile, streamId[i]);
    RecU32(d.recFile, d.vbOffset[i]);
    RecU32(d.recFile, d.vbStride[i]);
  }
  RecU32(d.recFile, ibId);
  RecU32(d.recFile, instId);
  RecU32(d.recFile, d.instSlot);
  RecU32(d.recFile, d.instOffset);
  RecU32(d.recFile, d.instStride);
  RecU32(d.recFile, (unsigned)kBackendSamplerSlots);
  for (unsigned i = 0; i < (unsigned)kBackendSamplerSlots; ++i)
  {
    RecU32(d.recFile, texId[i]);
    const BackendSamplerState& ss = d.model.sampler[i];
    RecU32(d.recFile, ss.minFilter);
    RecU32(d.recFile, ss.magFilter);
    RecU32(d.recFile, ss.mipFilter);
    RecU32(d.recFile, ss.addressU);
    RecU32(d.recFile, ss.addressV);
    RecU32(d.recFile, ss.addressW);
    RecU32(d.recFile, ss.borderColor);
    RecU32(d.recFile, (unsigned)ss.lodBias);
    RecU32(d.recFile, ss.maxAnisotropy);
    RecU32(d.recFile, (unsigned)ss.maxMipLevel);
    RecU32(d.recFile, ss.set ? 1u : 0u);
  }
  const BackendPipelineState& ms = d.model;
  RecU32(d.recFile, ms.cullMode);
  RecU32(d.recFile, ms.fillMode);
  RecU32(d.recFile, ms.scissorTestEnable ? 1u : 0u);
  RecU32(d.recFile, ms.zEnable ? 1u : 0u);
  RecU32(d.recFile, ms.zWriteEnable ? 1u : 0u);
  RecU32(d.recFile, ms.zFunc);
  RecF32(d.recFile, ms.slopeScaleDepthBias);
  RecU32(d.recFile, ms.depthBiasRaw);
  RecU32(d.recFile, ms.stencilEnable ? 1u : 0u);
  RecU32(d.recFile, ms.stencilFunc);
  RecU32(d.recFile, ms.stencilRef);
  RecU32(d.recFile, ms.stencilReadMask);
  RecU32(d.recFile, ms.stencilWriteMask);
  RecU32(d.recFile, ms.stencilFail);
  RecU32(d.recFile, ms.stencilZFail);
  RecU32(d.recFile, ms.stencilPass);
  RecU32(d.recFile, ms.alphaBlendEnable ? 1u : 0u);
  RecU32(d.recFile, ms.separateAlphaBlend ? 1u : 0u);
  RecU32(d.recFile, ms.srcBlend);
  RecU32(d.recFile, ms.destBlend);
  RecU32(d.recFile, ms.blendOp);
  RecU32(d.recFile, ms.srcBlendAlpha);
  RecU32(d.recFile, ms.destBlendAlpha);
  RecU32(d.recFile, ms.blendOpAlpha);
  RecU32(d.recFile, ms.colorWriteEnable);
  RecU32(d.recFile, ms.alphaTestEnable ? 1u : 0u);
  RecU32(d.recFile, ms.alphaRef);
  RecU32(d.recFile, ms.alphaFunc);
  RecU32(d.recFile, ms.clipPlaneEnable);
  const unsigned nf = (unsigned)SHADER_CONST_REGISTERS * 4u;
  RecU32(d.recFile, nf);
  for (unsigned i = 0; i < nf; ++i) RecF32(d.recFile, m_constShadow[csVertex][i]);
  RecU32(d.recFile, nf);
  for (unsigned i = 0; i < nf; ++i) RecF32(d.recFile, m_constShadow[csPixel][i]);
  // набор ресурсов (set/binding/kind/dim/count): реплеер строит наборы по этому
  // списку, повторять сканер SPIR-V в нативном коде нельзя — иная реализация
  // даст иную раскладку, и вывод перестанет быть тем же самым
  const int rmIdx = FindOrCreateResMap(d);
  if (rmIdx >= 0 && rmIdx < (int)d.resMaps.size())
  {
    const std::vector<NvrhiBackendImpl::ResItem>& items = d.resMaps[rmIdx].items;
    RecU32(d.recFile, (unsigned)items.size());
    for (size_t k = 0; k < items.size(); ++k)
    {
      RecU32(d.recFile, items[k].set);
      RecU32(d.recFile, items[k].binding);
      RecU32(d.recFile, items[k].kind);
      RecU32(d.recFile, items[k].dim);
      RecU32(d.recFile, items[k].count);
    }
  }
  else RecU32(d.recFile, 0u);
}

HRESULT NvrhiBackend::DrawImpl(ERenderPrimitiveType type, bool indexed, unsigned startIndexLocation,
                               unsigned startVertexLocation, unsigned primitiveCount,
                               unsigned explicitVtx)
{
  VK_API_GUARD;
  VK_TRACE("DrawImpl");
  FlushPendingUploads();
  NvrhiBackendImpl& d = *m_pImpl;
  // explicitVtx — для UP-путей: там вызывающий задаёт ЧИСЛО ИНДЕКСОВ, а не
  // число примитивов (у D3D9 DrawIndexedPrimitiveUP аргументы
  // MinVertexIndex/NumIndices/PrimitiveCount)
  const unsigned vtx = explicitVtx ? explicitVtx : PrimVertexCount(type, primitiveCount);
  if (!vtx)
  {
    ++d.drawSkipPrim;
    VkNote("Вывод: примитив вне Vulkan (TRIANGLEFAN)", false, 0);
    return E_FAIL;
  }
  if (d.curVs == kNoRes || d.curPs == kNoRes || d.curRt == kNoRes)
  {
    // Причина печатается обязательно (см. счётчики выше): kNoRes означает и
    // «не выставлен», и «выставлен ресурс чужого бэкенда» (шейдеры OnlineShaders
    // и DDS-текстуры до сих пор создаёт D3D9).
    if (d.curVs == kNoRes)      { ++d.drawSkipVs; VkNote("Вывод: нет VS (не выставлен или чужой)", false, 0); }
    else if (d.curPs == kNoRes) { ++d.drawSkipPs; VkNote("Вывод: нет PS (не выставлен или чужой)", false, 0); }
    else
    {
      ++d.drawSkipRt;
      VkNote("Вывод: нет render target'а", false, 0);
      // Замер (прогон 2026.10.16-21.03): ровно 1 пропуск «нет RT» на кадр при
      // 2 привязках своих поверхностей и `вывод: в бэк-буфер=0`. Печатаем
      // состояние реестра, чтобы отличить «не выставлен» от «бэк-буфер
      // разворачивается в kNoRes».
      static unsigned s_noRtDump = 0u;
      if (s_noRtDump < 6u)
      {
        ++s_noRtDump;
        DebugTrace("VkNoRt: curRt=%u surfaces=%u backIdx=%u imageIndex=%u картинок=%u rtSlot0=%u",
                   d.curRt, (unsigned)d.surfaces.size(), BackSurfaceIndex(d),
                   (unsigned)d.imageIndex, (unsigned)d.backSurfaceIdx.size(), d.rtSlot[0]);
      }
    }
    return E_FAIL;
  }
  // Замер (прогон 2026.10.16-20.54): вывод по записи поверхности, чья
  // nvrhi-картинка уже отпущена, вешает поток внутри createFramebuffer
  // (createImageView по освобождённому объекту; GPU при этом 0%). Дешевле
  // отказать и сказать почему.
  if (d.curRt >= d.surfaces.size() || !d.surfaces[d.curRt].tex)
  {
    ++d.drawSkipRt;
    VkNote("Вывод: поверхность мертва (swapchain пересоздан?)", false, 0);
    return E_FAIL;
  }
  // Vulkan требует, чтобы глубина совпадала по размеру с цветовым приложением.
  // Замер (прогон 2026.10.16-21.00): после смены разрешения клиент продолжает
  // привязывать свою поверхность глубины 800x600 к RT 1024x768
  // (`VkFb: rt=14 1024x768 ds=3 dsWH=800x600`) — D3D9 это допускает, Vulkan
  // нет. Подставляем внутреннюю глубину нужного размера (одна на размер).
  if (d.curDs != kNoRes && d.curDs < d.surfaces.size() &&
      (d.surfaces[d.curDs].w != d.surfaces[d.curRt].w ||
       d.surfaces[d.curDs].h != d.surfaces[d.curRt].h))
  {
    const unsigned rw = d.surfaces[d.curRt].w, rh = d.surfaces[d.curRt].h;
    unsigned sub = kNoRes;
    if (d.backDepthIdx != kNoRes && d.backDepthIdx < d.surfaces.size() &&
        d.surfaces[d.backDepthIdx].w == rw && d.surfaces[d.backDepthIdx].h == rh)
      sub = d.backDepthIdx;
    else if (d.dsSubIdx != kNoRes && d.dsSubIdx < d.surfaces.size() &&
             d.surfaces[d.dsSubIdx].w == rw && d.surfaces[d.dsSubIdx].h == rh)
      sub = d.dsSubIdx;
    else
    {
      HSurface hs;
      if (SUCCEEDED(CreateSurface(ssDepthStencil, rw, rh, bfD24S8,
                                  BACKEND_POOL_DEFAULT, &hs)))
      {
        d.dsSubIdx = Hnd2Idx(hs.p);
        sub = d.dsSubIdx;
      }
    }
    if (sub != kNoRes)
    {
      static unsigned s_dsSubNote = 0u;
      if (s_dsSubNote < 4u)
      {
        ++s_dsSubNote;
        DebugTrace("VkDs: глубина %ux%u не совпадает с RT %ux%u — подставлена %ux%u",
                   d.surfaces[d.curDs].w, d.surfaces[d.curDs].h, rw, rh,
                   d.surfaces[sub].w, d.surfaces[sub].h);
      }
      d.curDs = sub;
    }
    else
      VkNote("Вывод: глубина не совпадает с RT и замена не создана", false, 0);
  }
  if (d.curRt < 4096u)
  {
    if (d.rtDrawCount.size() <= d.curRt) d.rtDrawCount.resize(d.curRt + 1u, 0u);
    ++d.rtDrawCount[d.curRt];
  }
  if (d.curRt == BackSurfaceIndex(d)) ++d.drawsBack; else ++d.drawsOff;
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
  VK_TRACE("ubo ok");

  nvrhi::IFramebuffer* fbRaw = 0;
  nvrhi::GraphicsPipelineHandle pipe = FindOrCreatePipeline(d, type, &fbRaw);
  if (!pipe) { VK_TRACE("pipe НЕ создан"); return E_FAIL; }
  VK_TRACE("pipe ok");

  nvrhi::GraphicsState gs;
  gs.setPipeline(pipe).setFramebuffer(fbRaw);
  unsigned maxSlot = 0u;
  for (unsigned i = 0; i < kMaxVertexStreams; ++i)
    if (d.vb[i] != kNoRes && d.vb[i] < d.buffers.size()) maxSlot = i;
  if (d.instSrc != kNoRes && d.instSrc < d.buffers.size() && d.instSlot > maxSlot)
    maxSlot = d.instSlot;
  for (unsigned i = 0; i <= maxSlot && i < kMaxVertexStreams; ++i)
  {
    if (d.instSrc != kNoRes && d.instSlot == i) continue;   // ниже добавляется инстансный
    // Замер 2026-10-17 (прогон 58): VUID-vkCmdBindVertexBuffers-pBuffers-04001
    // «pBuffers[1] is VK_NULL_HANDLE» — объект буфера у клиента есть, а
    // nvrhi-буфер у него не создан (пустое CPU-зеркало /创建 без загрузки).
    // Проверять надо НЕ только индекс слота, но и сам handle.
    nvrhi::BufferHandle vbh;
    if (d.vb[i] != kNoRes && d.vb[i] < d.buffers.size()) vbh = d.buffers[d.vb[i]].buf;
    if (vbh)
      gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vbh)
                         .setSlot(i).setOffset(d.buffers[d.vb[i]].off + d.vbOffset[i]));
    else if (d.dummyVb)
    {
      if (d.vbHoles < 6u)
      {
        char det[48];
        snprintf(det, sizeof(det), "слот %u пуст — подставлен dummy", i);
        VkNote("Draw: вершинный слот", false, det);
        ++d.vbHoles;
      }
      gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(d.dummyVb)
                         .setSlot(i).setOffset(0));
    }
  }
  if (d.instSrc != kNoRes && d.instSrc < d.buffers.size() && d.buffers[d.instSrc].buf)
    gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(d.buffers[d.instSrc].buf)
                       .setSlot(d.instSlot).setOffset(d.buffers[d.instSrc].off + d.instOffset));
  else if (m_instanceCount && LayoutWantsInstanceStream(d) && EnsureInstCounter(d, m_instanceCount))
    gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(d.instCounter)
                       .setSlot(d.instSlot).setOffset(0));
  if (d.curIb != kNoRes && d.curIb < d.buffers.size() && d.buffers[d.curIb].buf)
    gs.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(d.buffers[d.curIb].buf)
                      .setOffset(d.buffers[d.curIb].off)
                      .setFormat(d.buffers[d.curIb].index32 ? nvrhi::Format::R32_UINT
                                                            : nvrhi::Format::R16_UINT));
  if (d.set0Set) gs.addBindingSet(d.set0Set);
  // Шаг 13.1: текстуры (набор 2) и сэмплеры (набор 3). Набор текстур зависит от
  // привязок, поэтому ищется по подписке (IBindingSet неизменяем).
  const int rmIdx = FindOrCreateResMap(d);
  VK_TRACE("resmap");
  if (rmIdx < 0) VK_TRACE("resmap ОТКАЗ");
  if (rmIdx >= 0)
  {
    const NvrhiBackendImpl::ResMapObj& rm = d.resMaps[rmIdx];
    // Порядок addBindingSet ОБЯЗАН совпадать с порядком addBindingLayout в
    // пайплайне (set0, набор 2 = текстуры, набор 3 = сэмплеры, набор 1 = SSBO):
    // NVRHI сопоставляет наборы позиционно. Прежний порядок «сэмплеры, потом
    // текстуры» давал VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358 и
    // VUID-vkCmdDrawIndexed-None-08600 («...pipeline layout is type
    // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE but binding 0 ... trying to bind, is type
    // VK_DESCRIPTOR_TYPE_SAMPLER» и наоборот) — то есть почти каждый вывод был
    // невалидным.
    if (rm.layoutTex)
    {
      nvrhi::BindingSetHandle bsTex = FindOrCreateTexSet(d, rm, TexSignature(d, rm));
      VK_TRACE("texset");
      if (bsTex) gs.addBindingSet(bsTex);
    }
    if (rm.layoutSmp)
    {
      nvrhi::BindingSetHandle bsSmp = FindOrCreateSamplerSet(d, rm, SamplerSetSignature(d, rm));
      VK_TRACE("smpset");
      if (bsSmp) gs.addBindingSet(bsSmp);
    }
    // Набор 1 обязан быть привязан, если он есть в layout'е пайплайна:
    // привязка отсутствует = вывод невалиден (08600). SSBO по-настоящему
    // переводятся шагом 13.5, здесь — dummy-буфер на все слоты набора.
    if (rm.layoutSsbo)
    {
      NvrhiBackendImpl::ResMapObj& rmw = d.resMaps[rmIdx];
      if (!rmw.bsSsbo)
      {
        nvrhi::BindingSetDesc bsd;
        for (size_t k = 0; k < rmw.items.size(); ++k)
        {
          const NvrhiBackendImpl::ResItem& it = rmw.items[k];
          if (it.set != 1u || it.kind != rkSsbo) continue;
          const unsigned cnt = it.count ? it.count : 1u;
          for (unsigned e = 0; e < cnt; ++e)
          {
            nvrhi::BindingSetItem bi = nvrhi::BindingSetItem::StructuredBuffer_SRV(it.binding,
                                                                                   d.dummySsbo);
            bi.arrayElement = e;
            bsd.bindings.push_back(bi);
          }
        }
        if (!bsd.bindings.empty() && d.dummySsbo)
          rmw.bsSsbo = d.nvrhiDevice->createBindingSet(bsd, rmw.layoutSsbo);
        if (!rmw.bsSsbo) VkNote("Draw: набор SSBO", false, 0);
        else ++d.ssboSetsCreated;
      }
      if (rmw.bsSsbo) gs.addBindingSet(rmw.bsSsbo);
      VK_TRACE("ssboset");
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
  // Диагностика пустого кадра (прогон 2026.10.16-22.53: ЛЮБОЙ вывод клиента не
  // оставляет пикселей — surf#11 1024x768, 28597 выводов, 0 ненулевых). Печать
  // только первых обращений: всё, что может «съесть» геометрию, — вьюпорт,
  // сциссор, отсечение, глубина, состав атрибутов.
  if (VkVerbose() && d.drawDiag < 6u)
  {
    ++d.drawDiag;
    const BackendPipelineState& ms = d.model;
    const unsigned rtI = d.curRt, dsI = d.curDs;
    const unsigned rw = (rtI < d.surfaces.size()) ? d.surfaces[rtI].w : 0u;
    const unsigned rh = (rtI < d.surfaces.size()) ? d.surfaces[rtI].h : 0u;
    const unsigned dw = (dsI < d.surfaces.size()) ? d.surfaces[dsI].w : 0u;
    const unsigned dh = (dsI < d.surfaces.size()) ? d.surfaces[dsI].h : 0u;
    DebugTrace("VkDiag: draw#%u rt=%u %ux%u ds=%u %ux%u dsDepth=%u", d.drawDiag, rtI, rw, rh, dsI, dw, dh,
               (dsI < d.surfaces.size()) ? (d.surfaces[dsI].depth ? 1u : 0u) : 0u);
    DebugTrace("VkDiag:   vp=%ld,%ld %ldx%ld z=%g..%g scissorValid=%u",
               (long)m_viewport.x, (long)m_viewport.y, (long)m_viewport.width, (long)m_viewport.height,
               (double)m_viewport.minZ, (double)m_viewport.maxZ, m_scissorValid ? 1u : 0u);
    DebugTrace("VkDiag:   scissor=%ld,%ld..%ld,%ld cull=%u fill=%u scissorTest=%u",
               (long)m_scissor.left, (long)m_scissor.top, (long)m_scissor.right, (long)m_scissor.bottom,
               ms.cullMode, ms.fillMode, ms.scissorTestEnable ? 1u : 0u);
    DebugTrace("VkDiag:   zEnable=%u zWrite=%u zFunc=%u colorWrite=0x%x alphaTest=%u",
               ms.zEnable ? 1u : 0u, ms.zWriteEnable ? 1u : 0u, ms.zFunc, ms.colorWriteEnable,
               ms.alphaTestEnable ? 1u : 0u);
    const unsigned elems = (d.curLayout < d.layoutObjs.size())
                           ? (unsigned)d.layoutObjs[d.curLayout].elems.size() : 0xffffffffu;
    DebugTrace("VkDiag:   prim=%u idx=%u vtx=%u layout=%u elems=%u ib=%u",
               (unsigned)type, indexed ? 1u : 0u, vtx, d.curLayout, elems, d.curIb);
    DebugTrace("VkDiag:   vb0=%u stride0=%u off0=%u instSlot=%u instStride=%u instCnt=%u",
               d.vb[0], d.vbStride[0], d.vbOffset[0], d.instSlot, d.instStride, m_instanceCount);
    // банк констант: если он пуст, матрицы нулевые и вся геометрия вырождается
    // в точку (ровно то и даёт «0 ненулевых пикселей» при исправных состояниях)
    unsigned nz0 = 0u, nz1 = 0u;
    for (unsigned i = 0; i < (unsigned)SHADER_CONST_REGISTERS * 4u; ++i)
    {
      if (m_constShadow[csVertex][i] != 0.f) ++nz0;
      if (m_constShadow[csPixel][i] != 0.f) ++nz1;
    }
    DebugTrace("VkDiag:   vs=%u ps=%u банкVS ненулевых=%u/%u банкPS ненулевых=%u/%u",
               d.curVs, d.curPs, nz0, (unsigned)(SHADER_CONST_REGISTERS * 4u),
               nz1, (unsigned)(SHADER_CONST_REGISTERS * 4u));
    DebugTrace("VkDiag:   VS c0={%g,%g,%g,%g} c1={%g,%g,%g,%g}",
               (double)m_constShadow[csVertex][0], (double)m_constShadow[csVertex][1],
               (double)m_constShadow[csVertex][2], (double)m_constShadow[csVertex][3],
               (double)m_constShadow[csVertex][4], (double)m_constShadow[csVertex][5],
               (double)m_constShadow[csVertex][6], (double)m_constShadow[csVertex][7]);
    // первые байты вершинного буфера: если там нули — не доехали вершины
    unsigned vbz = 0u;
    if (d.vb[0] != kNoRes && d.vb[0] < d.buffers.size())
    {
      const std::vector<unsigned char>& cpu = d.buffers[d.vb[0]].cpu;
      const unsigned n = (unsigned)cpu.size();
      for (unsigned i = 0; i < n; ++i) if (cpu[i]) { vbz = n; break; }
      DebugTrace("VkDiag:   vb0 размер=%u ненулевых байт=%u первые=%02x %02x %02x %02x",
                 n, vbz, n > 0 ? cpu[0] : 0, n > 1 ? cpu[1] : 0, n > 2 ? cpu[2] : 0, n > 3 ? cpu[3] : 0);
    }
    else
      DebugTrace("VkDiag:   vb0 пуст (ручка kNoRes)");
  }
  // Рекордер вывода (env PW_VK_RECORD): снимается ВСЁ, что нужно реплееру,
  // чтобы повторить этот вывод нативно и открыть его в RenderDoc.
  if (d.recOn && d.recDraws < d.recMax)
  {
    ++d.recSeen;
    const bool rtOk = d.recSeen > d.recSkip &&
                      ((!d.recRtW && !d.recRtH) ||
                       (d.curRt < d.surfaces.size() && d.surfaces[d.curRt].w == d.recRtW &&
                        d.surfaces[d.curRt].h == d.recRtH));
    if (rtOk) RecDraw(d, type, indexed, startIndexLocation, startVertexLocation, primitiveCount, vtx);
  }
  VK_TRACE("setGfxState >");
  d.commandList->setGraphicsState(gs);
  VK_TRACE("setGfxState <");

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
  VK_TRACE("DrawIndexed");
  (void)minIndex; (void)indexedVertexCount;
  return DrawImpl(type, true, startIndex, baseVertexIndex, primitiveCount);
}

HRESULT NvrhiBackend::Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount)
{
  VK_API_GUARD;
  VK_TRACE("Draw");
  return DrawImpl(type, false, 0, startVertex, primitiveCount);
}

HRESULT NvrhiBackend::SetInstanceCount(unsigned count)
{
  VK_API_GUARD;
  VK_TRACE("SetInstanceCount");
  m_instanceCount = count;
  return S_OK;
}

// UP-пути (шаг 13.3). Приведённой памяти у Vulkan нет, поэтому данные
// вызывающего копируются в кольцо-буфер, а draw получает смещение: у
// writeBuffer есть destOffsetBytes (nvrhi.h:3402), так что отдельный буфер на
// каждый вызов не нужен. Кольца регистрируются в d.buffers как обычные буферы —
// тогда общий путь DrawImpl (вершинные потоки, индексный буфер) не меняется.
static const unsigned kUpVbSize = 4u * 1024u * 1024u;
static const unsigned kUpIbSize = 1u * 1024u * 1024u;

static unsigned RegisterUpRing(NvrhiBackendImpl& d, bool index)
{
  nvrhi::BufferHandle& slot = index ? d.upIb : d.upVb;   // RefCountPtr::operator& перегружен —
                                                        // адрес брать нельзя, только ссылку
  if (slot)
  {
    for (size_t i = 0; i < d.buffers.size(); ++i)
      if (d.buffers[i].buf == slot) return (unsigned)i;
  }
  const unsigned size = index ? kUpIbSize : kUpVbSize;
  nvrhi::BufferDesc bd;
  bd.setByteSize(size).setDebugName(index ? "seam UP IB ring" : "seam UP VB ring");
  if (index) bd.setIsIndexBuffer(true).setInitialState(nvrhi::ResourceStates::IndexBuffer);
  else bd.setIsVertexBuffer(true).setInitialState(nvrhi::ResourceStates::VertexBuffer);
  bd.setKeepInitialState(true);
  slot = d.nvrhiDevice->createBuffer(bd);
  if (!slot) { VkNote("UP-кольцо: createBuffer", false, 0); return kNoRes; }
  NvrhiBackendImpl::BufferObj bo;
  bo.buf = slot;
  bo.isIndex = index;
  bo.index32 = false;
  // ручка не создаётся: кольцо внутреннее, клиент его не видит
  d.buffers.push_back(bo);
  return (unsigned)(d.buffers.size() - 1);
}

// Выравнивание по 16 (требование Vulkan к смещению вершинного/индексного
// буфера). Если данные не влезают в хвост кольца, возвращается true — вызывающий
// обязан закрыть командный список (иначе новая запись с нуля затрёт данные более
// раннего draw этого же списка).
static bool ReserveUp(NvrhiBackendImpl& d, bool index, unsigned bytes, unsigned long long* pOff)
{
  unsigned long long& off = index ? d.upIbOff : d.upVbOff;
  const unsigned cap = index ? kUpIbSize : kUpVbSize;
  unsigned long long aligned = (off + 15ull) & ~15ull;
  if (aligned + bytes > (unsigned long long)cap)
  {
    ++d.upFlushes;
    off = 0ull;
    *pOff = 0ull;
    return true;
  }
  *pOff = aligned;
  return false;
}

HRESULT NvrhiBackend::DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                    unsigned vertexCount, const void* pIndexData, bool index16,
                                    const void* pVertexData, unsigned vertexStride)
{
  VK_TRACE("DrawIndexedUP");
  (void)minIndexValue; (void)vertexCount;   // D3D9-тройка: NumIndices/PrimitiveCount
  NvrhiBackendImpl& d = *m_pImpl;
  if (!pIndexData || !pVertexData || !vertexStride || !indexCount)
    return D3DERR_INVALIDCALL;
  const unsigned ibIdx = RegisterUpRing(d, true);
  const unsigned vbIdx = RegisterUpRing(d, false);
  if (ibIdx == kNoRes || vbIdx == kNoRes) return E_FAIL;

  const unsigned ibBytes = indexCount * (index16 ? 2u : 4u);
  // размер VB: D3D9 его тоже не получает (UP-вызов читает индексы), поэтому
  // верхняя граница считается обходом индексов — иначе кольцо переполнилось бы
  // или, наоборот, копировались лишние байты
  unsigned maxIdx = 0;
  if (index16)
    for (unsigned i = 0; i < indexCount; ++i)
      maxIdx = maxIdx > ((const unsigned short*)pIndexData)[i] ? maxIdx : ((const unsigned short*)pIndexData)[i];
  else
    for (unsigned i = 0; i < indexCount; ++i)
      maxIdx = maxIdx > ((const unsigned*)pIndexData)[i] ? maxIdx : ((const unsigned*)pIndexData)[i];
  const unsigned vbBytes = vertexStride * (maxIdx + 1u);
  unsigned long long ibOff = 0, vbOff = 0;
  const bool needFlush = ReserveUp(d, true, ibBytes, &ibOff) | ReserveUp(d, false, vbBytes, &vbOff);
  if (needFlush)
  {
    FlushRecording();
    // кольцо перезапускается с нуля, а ранее записанный список мог ещё
    // исполняться: без ожидания записи затёрли бы данные, которые draw ещё
    // читает (для UP-вывода его мало — PostFX, ценой ожидания можно платить)
    if (d.nvrhiDevice) { VK_TRACE("UP ring: waitIdle >"); d.nvrhiDevice->waitForIdle(); VK_TRACE("UP ring: waitIdle <"); }
  }
  if (!EnsureRecording()) return E_FAIL;

  d.commandList->writeBuffer(d.upIb, pIndexData, ibBytes, ibOff);
  d.commandList->writeBuffer(d.upVb, pVertexData, vbBytes, vbOff);
  d.uploads += 2;

  // поток 0 и индексный буфер подменяются на кольцо, после draw восстанавливаются
  // (в D3D9 UP-вызов игнорирует привязанные потоки, но состояние не меняет)
  const unsigned saveVb = d.vb[0], saveOff = d.vbOffset[0], saveStride = d.vbStride[0];
  const unsigned saveIb = d.curIb;
  const bool saveIdx32 = d.buffers[ibIdx].index32;
  d.vb[0] = vbIdx; d.vbOffset[0] = (unsigned)vbOff; d.vbStride[0] = vertexStride;
  d.curIb = ibIdx; d.buffers[ibIdx].index32 = !index16;
  const HRESULT hr = DrawImpl(type, true, 0, 0, 0, indexCount);
  d.vb[0] = saveVb; d.vbOffset[0] = saveOff; d.vbStride[0] = saveStride;
  d.curIb = saveIb; d.buffers[ibIdx].index32 = saveIdx32;
  d.upVbOff = vbOff + vbBytes;
  d.upIbOff = ibOff + ibBytes;
  if (SUCCEEDED(hr)) ++d.upDraws;
  return hr;
}

HRESULT NvrhiBackend::DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                             const void* pVertexData, unsigned vertexStride)
{
  VK_TRACE("DrawUP");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!pVertexData || !vertexStride || !primitiveCount) return D3DERR_INVALIDCALL;
  const unsigned vbIdx = RegisterUpRing(d, false);
  if (vbIdx == kNoRes) return E_FAIL;

  const unsigned vtx = PrimVertexCount(type, primitiveCount);
  if (!vtx) { VkNote("Вывод: примитив вне Vulkan (TRIANGLEFAN)", false, 0); return E_FAIL; }
  const unsigned vbBytes = vertexStride * vtx;
  unsigned long long vbOff = 0;
  if (ReserveUp(d, false, vbBytes, &vbOff))
  {
    FlushRecording();
    if (d.nvrhiDevice) { VK_TRACE("UP ring(DrawUP): waitIdle >"); d.nvrhiDevice->waitForIdle(); VK_TRACE("UP ring(DrawUP): waitIdle <"); }
  }
  if (!EnsureRecording()) return E_FAIL;
  d.commandList->writeBuffer(d.upVb, pVertexData, vbBytes, vbOff);
  ++d.uploads;

  const unsigned saveVb = d.vb[0], saveOff = d.vbOffset[0], saveStride = d.vbStride[0];
  d.vb[0] = vbIdx; d.vbOffset[0] = (unsigned)vbOff; d.vbStride[0] = vertexStride;
  const HRESULT hr = DrawImpl(type, false, 0, 0, primitiveCount, vtx);
  d.vb[0] = saveVb; d.vbOffset[0] = saveOff; d.vbStride[0] = saveStride;
  d.upVbOff = vbOff + vbBytes;
  if (SUCCEEDED(hr)) ++d.upDraws;
  return hr;
}

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
// pInputKinds (параллельно pInputs): класс скаляра входа по SPIR-V —
// 0 = float, 1 = signed int, 2 = unsigned int. Нужен, потому что Vulkan
// требует совпадения КЛАССА формата атрибута и типа входа шейдера
// (VUID-VkGraphicsPipelineCreateInfo-Input-08733).
static bool SpvScan(const void* pCode, unsigned codeSize, bool fragment,
                    std::string* pEntry,
                    std::vector<std::pair<std::string, unsigned> >* pInputs,
                    std::vector<unsigned>* pInputKinds = 0)
{
  const unsigned* w = static_cast<const unsigned*>(pCode);
  const unsigned n = codeSize / 4u;
  if (n < 5u || w[0] != 0x07230203u) return false;

  std::vector<unsigned> inputIds;
  std::map<unsigned, std::string> names;
  std::map<unsigned, unsigned> locs;
  std::map<unsigned, unsigned> varType, ptrBaseT, vecBase, kindOf;
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
      if (w[i + 3] == 1u) { inputIds.push_back(w[i + 2]); varType[w[i + 2]] = w[i + 1]; }
    }
    else if (code == 32u && wc >= 4u)            // OpTypePointer: id, класс, база
    {
      ptrBaseT[w[i + 1]] = w[i + 3];
    }
    else if (code == 23u && wc >= 4u)            // OpTypeVector: id, база, кол-во
    {
      vecBase[w[i + 1]] = w[i + 2];
    }
    else if (code == 22u && wc >= 2u)            // OpTypeFloat
    {
      kindOf[w[i + 1]] = 0u;
    }
    else if (code == 21u && wc >= 4u)            // OpTypeInt: id, ширина, знаковость
    {
      kindOf[w[i + 1]] = w[i + 3] ? 1u : 2u;
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
    unsigned kind = 0u;
    if (pInputKinds)
    {
      unsigned t = 0xffffffffu;
      std::map<unsigned, unsigned>::const_iterator itV = varType.find(inputIds[k]);
      if (itV != varType.end()) t = itV->second;
      std::map<unsigned, unsigned>::const_iterator itP = ptrBaseT.find(t);
      if (itP != ptrBaseT.end()) t = itP->second;
      std::map<unsigned, unsigned>::const_iterator itC = vecBase.find(t);
      if (itC != vecBase.end()) t = itC->second;
      std::map<unsigned, unsigned>::const_iterator itK = kindOf.find(t);
      if (itK != kindOf.end()) kind = itK->second;
      pInputKinds->push_back(kind);
    }
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
  std::vector<unsigned> imgDim, smpImgBase, arrayLenId, constVal;
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
      // OpTypeArray (28): id, элемент, длина (id константы) — длина нужна, чтобы
      // завести descriptor array нужного размера
      if (code == 28u && wc >= 4u)
      {
        if (w[i + 1] + 1u > arrayLenId.size()) arrayLenId.resize(w[i + 1] + 1u, 0xffffffffu);
        arrayLenId[w[i + 1]] = w[i + 3];
      }
      // OpTypeImage (25): id, sampled, dim, imageFormat, depth, arrayed, ms,
      // depthArrayed — то есть Dim лежит в w[i+3] (enum SPV: 0=1D, 1=2D, 2=3D,
      // 3=Cube, 4=Rect, 5=Buffer, 6=SubpassData). w[i+4] — ImageFormat
      // (обычно 0=UNKNOWN), поэтому прежняя читка давала «dim=0» у всех
      // текстур и подмена куба не срабатывала
      // (VUID-vkCmdDrawIndexed-viewType-07752: sampled image Set2 Binding1
      // "envMapSampler" VkImageViewType 2D, а OpTypeImage Dim=Cube).
      if (code == 25u && wc >= 5u)
      {
        if (w[i + 1] + 1u > imgDim.size()) imgDim.resize(w[i + 1] + 1u, 0u);
        imgDim[w[i + 1]] = w[i + 3];
      }
      // OpTypeSampledImage (27): id, базовый OpTypeImage
      if (code == 27u && wc >= 3u)
      {
        if (w[i + 1] + 1u > smpImgBase.size()) smpImgBase.resize(w[i + 1] + 1u, 0xffffffffu);
        smpImgBase[w[i + 1]] = w[i + 2];
      }
    }
    else if (code == 43u && wc >= 4u)             // OpConstant: тип, id, значение
    {
      if (w[i + 2] + 1u > constVal.size()) constVal.resize(w[i + 2] + 1u, 0u);
      constVal[w[i + 2]] = w[i + 3];
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
    unsigned cnt = 1u;
    if (baseOp == 28u || baseOp == 29u)
    {
      if (base < arrayLenId.size() && arrayLenId[base] != 0xffffffffu
          && arrayLenId[base] < constVal.size() && constVal[arrayLenId[base]] > 0u)
        cnt = constVal[arrayLenId[base]];
      if (cnt > 16u) cnt = 1u;      // OpTypeRuntimeArray / неразрешимая длина
    }
    if ((baseOp == 28u || baseOp == 29u) && base < elemOfId.size() && elemOfId[base] != 0xffffffffu)
    {
      const unsigned el = elemOfId[base];
      baseOp = (el < opOfId.size()) ? opOfId[el] : 0u;  // массив изображений = массив текстур
      base = el;
    }
    unsigned dim = 0u;
    unsigned imgId = base;
    if (baseOp == 27u && base < smpImgBase.size() && smpImgBase[base] != 0xffffffffu)
      imgId = smpImgBase[base];
    if (imgId < imgDim.size()) dim = imgDim[imgId];
    NvrhiBackendImpl::ResItem it;
    it.set = decSet[id];
    it.binding = decBind[id];
    it.dim = dim;
    it.count = cnt;
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
  VK_API_GUARD;
  VK_TRACE("CreateShaderImpl");
  if (!pCode || !codeSize || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;

  std::string entry;
  std::vector<std::pair<std::string, unsigned> > inputs;
  std::vector<unsigned> ikinds;
  if (!SpvScan(pCode, codeSize, fragment, &entry, &inputs, &ikinds))
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
  so.inputKinds = ikinds;
  if (d.recOn)   // см. комментарий к полям рекорда: байты обязаны быть те же
    so.spv.assign((const unsigned char*)pCode, (const unsigned char*)pCode + codeSize);
  SpvResScan(pCode, codeSize, &so.res);
  d.shaderObjs.push_back(so);
  pOut->p = NewHandle(&d, (unsigned)d.shaderObjs.size() - 1u, hkShader);

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
  VK_API_GUARD;
  VK_TRACE("CreateVertexShader");
  return CreateShaderImpl(pShaderCode, codeSize, false, pOut);
}

HRESULT NvrhiBackend::CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreatePixelShader");
  return CreateShaderImpl(pShaderCode, codeSize, true, pOut);
}

// Шаг 13.4c: элементы раскладки обратно — из копии описания клиента, которую
// CreateInputLayout сохраняет в LayoutObj (см. шаг потока: раскладку nvrhi
// собирает позже, на момент вывода).
HRESULT NvrhiBackend::GetInputLayoutElements(HInputLayout layout, VertexElementDescriptor* pOut,
                                             unsigned maxElements, unsigned* pCount)
{
  VK_API_GUARD;
  VK_TRACE("GetInputLayoutElements");
  if (pCount) *pCount = 0;
  if (!pOut || !pCount) return E_INVALIDARG;
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned idx = Hnd2Idx(layout.p);
  if (idx == kNoRes || idx >= d.layoutObjs.size()) return E_FAIL;
  const std::vector<VertexElementDescriptor>& e = d.layoutObjs[idx].elems;
  if (e.size() > maxElements) {
    VkNote("GetInputLayoutElements", false, "не влезает");
    return E_FAIL;
  }
  for (size_t i = 0; i < e.size(); ++i)
    pOut[i] = e[i];
  *pCount = (unsigned)e.size();
  return S_OK;
}

unsigned NvrhiBackend::GetShaderTargetTag() const { return (unsigned)Materials::stSpirv; }

HRESULT NvrhiBackend::CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreateInputLayout");
  if (!pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  NvrhiBackendImpl::LayoutObj lo;
  const unsigned n = descr.GetVertexElementsCount();
  for (unsigned i = 0; i < n; ++i) lo.elems.push_back(descr.GetVertexElement(i));
  d.layoutObjs.push_back(lo);
  pOut->p = NewHandle(&d, (unsigned)d.layoutObjs.size() - 1u, hkLayout);
  return S_OK;
}

HRESULT NvrhiBackend::CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                    EBackendFormat format, EBackendPool pool, HSurface* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreateSurface");
  NoteCallSite(0, "CreateSurface", _ReturnAddress(), 10);
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
  pOut->p = NewHandle(&d, (unsigned)d.surfaces.size() - 1u, hkSurface);
  return S_OK;
}

// Блокировка поверхности (шаг 13.2): прежний путь — LockRect/UnlockRect у
// COM-поверхности (rendersurface.cpp), теперь — seam. Зеркало полное, поэтому
// частичная блокировка не затирает остальное; шаг строки сообщает бэкенд.
HRESULT NvrhiBackend::LockSurface(HSurface surface, const BackendRect* pRect,
                                  ERenderLockType mode, void** ppData, unsigned* pRowPitch)
{
  VK_API_GUARD;
  VK_TRACE("LockSurface");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!surface.p || !ppData) return E_INVALIDARG;
  *ppData = 0;
  unsigned s = Hnd2Idx(surface.p);
  s = ResolveSurfaceIdx(d, s);
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
  VK_API_GUARD;
  VK_TRACE("UnlockSurface");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!surface.p) return E_INVALIDARG;
  unsigned s = Hnd2Idx(surface.p);
  s = ResolveSurfaceIdx(d, s);
  if (s == kNoRes || s >= d.surfaces.size() || !d.surfaces[s].tex) return D3DERR_INVALIDCALL;
  SurfaceObj& so = d.surfaces[s];
  if (!so.locked || so.mirror.empty()) return E_FAIL;
  so.locked = false;
  const unsigned bpp = BppOf(so.fmt);
  // см. комментарий к UnlockTexture2D: staging = отдельное выделение на каждую
  // загрузку, поэтому поверхность грузится через writeTexture
  if (GetCurrentThreadId() != d.ownerThread)
  {
    VkNoteThread(d, "UnlockSurface");
    so.needUpload = true;
    if (std::find(d.pendSrf.begin(), d.pendSrf.end(), s) == d.pendSrf.end())
      d.pendSrf.push_back(s);
    return S_OK;
  }
  if (!EnsureRecording()) return E_FAIL;
  VK_TRACE("writeTexture(surf) >");
  d.commandList->writeTexture(so.tex, 0, 0, &so.mirror[0], (size_t)so.w * bpp);
  VK_TRACE("writeTexture(surf) <");
  NoteUpload(so.mirror.size());
  return S_OK;
}

HRESULT NvrhiBackend::GetSurfaceInfo(HSurface surface, BackendTextureInfo* pInfo)
{
  VK_API_GUARD;
  VK_TRACE("GetSurfaceInfo");
  if (!pInfo) return E_INVALIDARG;
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned s = Hnd2Idx(surface.p);
  s = ResolveSurfaceIdx(d, s);
  if (s == kNoRes || s >= d.surfaces.size() || !d.surfaces[s].tex)
  {
    // Разбор 2026-10-18 (прогон 100): 4226 провала за прогон, все — из
    // BindRenderTargetDepth. Без индекса непонятно, чего не хватает: ручки нет,
    // индекс вне диапазона или запись есть, но tex пуст.
    static unsigned s_srfFail = 0;
    if (s_srfFail < 12u)
    {
      ++s_srfFail;
      const unsigned raw = Hnd2Idx(surface.p);
      const bool inr = (raw != kNoRes && raw < d.surfaces.size());
      DebugTrace("VkSrf: GetSurfaceInfo провал #%u raw=%u idx=%u size=%u %s",
                 s_srfFail, raw, s, (unsigned)d.surfaces.size(),
                 !inr ? "вне диапазона"
                      : (d.surfaces[raw].dead ? "запись dead"
                                              : (d.surfaces[raw].tex ? "tex есть?!" : "tex пуст")));
    }
    NoteForeign("GetSurfaceInfo: ручка не из этого бэкенда", surface.p, _ReturnAddress());
    return E_FAIL;
  }
  const SurfaceObj& so = d.surfaces[s];
  pInfo->width  = so.w;
  pInfo->height = so.h;
  pInfo->levels = 1u;
  pInfo->format = so.seamFormat;
  pInfo->usage  = so.usage;
  pInfo->pool   = so.pool;
  pInfo->cube   = false;   // поверхность уровня cube-текстуры — не cube
  return S_OK;
}

// Просматриваемая поверхность уровня (шаг 13.4c): в Vulkan нет отдельного
// объекта уровня, поэтому запись surfaces ссылается на ту же nvrhi-текстуру
// (TextureHandle счётный — картинка переживёт уничтожение текстуры-владельца).
// Уровень поддерживается только 0: замер вызовов (PFRenderInterface,
// PFRenderWater, PFClientVisibilityMap, PFPostFXParams) — везде GetSurface(0);
// остальные уровни отказываем явно, а не молча подменяем нулём.
HRESULT NvrhiBackend::GetTextureSurface(HTexture texture, unsigned level, HSurface* pOut)
{
  VK_API_GUARD;
  VK_TRACE("GetTextureSurface");
  if (!pOut) return E_INVALIDARG;
  NvrhiBackendImpl& d = *m_pImpl;
  const unsigned t = Hnd2Idx(texture.p);
  if (t == kNoRes || t >= d.textures.size() || !d.textures[t].tex)
  {
    NoteForeign("GetTextureSurface: ручка не из этого бэкенда", texture.p, _ReturnAddress());
    return E_FAIL;
  }
  if (level != 0u)
  {
    char det[80];
    snprintf(det, sizeof(det), "level=%u — поверхность уровня не отдаётся", level);
    VkNote("GetTextureSurface", false, det);
    return E_FAIL;
  }
  NvrhiBackendImpl::TextureObj& to = d.textures[t];
  NoteCallSite(1, "GetTextureSurface", _ReturnAddress(), 10);
  if (to.levelSurf.size() <= level)
    to.levelSurf.resize(level + 1u, kNoRes);
  unsigned sIdx = to.levelSurf[level];
  if (sIdx != kNoRes)
  {
    // та же обёртка и та же картинка + дополнительная ссылка (сеанс владения
    // как у COM-вызова: вызывающий получает ссылку и отпускает её сам)
    SeamHandle** slot = d.SeamSlot(hkSurface, sIdx);
    if (!slot)
    {
      VkNote("GetTextureSurface: слот ручки потерян", false, 0);
      return E_FAIL;
    }
    if (*slot)
    {
      pOut->p = NewHandle(&d, sIdx, hkSurface);   // NewHandle добавляет ссылку
      return S_OK;
    }
    sIdx = kNoRes;    // обёртку клиент отпустил — заводим заново
  }
  SurfaceObj so;
  so.tex        = to.tex;
  so.fmt        = to.fmt;
  so.w          = to.w;
  so.h          = to.h;
  so.seamFormat = to.seamFormat;
  so.pool       = to.pool;
  so.usage      = to.usage;
  // глубина определяется форматом (в этой версии NVRHI форматы глубины —
  // D16, D24S8, D32, D32S8; D32_FLOAT/D16_UNORM в enum'е отсутствуют)
  so.depth      = (to.fmt == nvrhi::Format::D24S8 || to.fmt == nvrhi::Format::D16 ||
                   to.fmt == nvrhi::Format::D32 || to.fmt == nvrhi::Format::D32S8);
  d.surfaces.push_back(so);
  sIdx = (unsigned)d.surfaces.size() - 1u;
  to.levelSurf[level] = sIdx;
  void* h = NewHandle(&d, sIdx, hkSurface);
  if (h)
    static_cast<SeamHandle*>(h)->AddRef();   // внутренняя ссылка текстуры (см. TextureObj::levelSurf)
  pOut->p = h;
  return S_OK;
}

HRESULT NvrhiBackend::CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreateVertexBuffer");
  if (!size || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;
  (void)pool;
  NvrhiBackendImpl::BufferObj bo;
  bo.cpu.resize(size);
  unsigned slabIdx = (unsigned)kNoRes, off = 0;
  if (d.SlabAlloc(size, &slabIdx, &off))
  {
    bo.slab = slabIdx;
    bo.off = off;
    bo.buf = d.slabs[slabIdx].buf;      // заимствованный handle слэба (ссылка)
  }
  else
  {
    bo.buf = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                           .setByteSize(size)
                                           .setIsVertexBuffer(true)
                                           .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                           .setKeepInitialState(true)
                                           .setDebugName("seam vb"));
    if (!bo.buf) { VkNote("CreateVertexBuffer", false, 0); return E_FAIL; }
  }
  d.buffers.push_back(bo);
  pOut->p = NewHandle(&d, (unsigned)d.buffers.size() - 1u, hkBuffer);
  return S_OK;
}

HRESULT NvrhiBackend::CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut)
{
  VK_API_GUARD;
  VK_TRACE("CreateIndexBuffer");
  if (!size || !pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice) return E_FAIL;
  (void)pool;
  NvrhiBackendImpl::BufferObj bo;
  bo.cpu.resize(size);
  bo.index32 = index32;
  unsigned slabIdx = (unsigned)kNoRes, off = 0;
  if (d.SlabAlloc(size, &slabIdx, &off))
  {
    bo.slab = slabIdx;
    bo.off = off;
    bo.buf = d.slabs[slabIdx].buf;
  }
  else
  {
    bo.buf = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                           .setByteSize(size)
                                           .setIsIndexBuffer(true)
                                           .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                           .setKeepInitialState(true)
                                           .setDebugName("seam ib"));
    if (!bo.buf) { VkNote("CreateIndexBuffer", false, 0); return E_FAIL; }
  }
  d.buffers.push_back(bo);
  pOut->p = NewHandle(&d, (unsigned)d.buffers.size() - 1u, hkBuffer);
  return S_OK;
}

// Блокировка отдаёт CPU-зеркало буфера; в Vulkan загрузка — на Unlock
// (целиком: для DIP-проходов это один буфер на материал, кольцевой upload
// появится вместе с боевыми проходами).
HRESULT NvrhiBackend::LockBuffer(HBuffer h, unsigned offset, unsigned size, void** ppData)
{
  VK_API_GUARD;
  VK_TRACE("LockBuffer");
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
  VK_API_GUARD;
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
  VK_API_GUARD;
  VK_TRACE("UnlockBuffer");
  const unsigned idx = Hnd2Idx(h.p);
  if (idx == kNoRes || idx >= m_pImpl->buffers.size()) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl::BufferObj& bo = m_pImpl->buffers[idx];
  if (!bo.locked) return S_OK;
  bo.locked = false;
  if (!bo.buf || !m_pImpl->nvrhiDevice) return E_FAIL;
  // Чужой поток (загрузка карты) в командный список не пишет — ownerThread.
  if (GetCurrentThreadId() != m_pImpl->ownerThread)
  {
    VkNoteThread(*m_pImpl, "UnlockBuffer");
    bo.needUpload = true;
    if (std::find(m_pImpl->pendBuf.begin(), m_pImpl->pendBuf.end(), idx)
        == m_pImpl->pendBuf.end())
      m_pImpl->pendBuf.push_back(idx);
    return S_OK;
  }
  if (!EnsureRecording()) return E_FAIL;
  VK_TRACE("writeBuffer >");
  m_pImpl->commandList->writeBuffer(bo.buf, bo.cpu.data(), bo.cpu.size(), bo.off);
  VK_TRACE("writeBuffer <");
  NoteUpload(bo.cpu.size());
  return S_OK;
}

HRESULT NvrhiBackend::UnlockVertexBuffer(HBuffer vb) { return UnlockBuffer(vb); }
HRESULT NvrhiBackend::UnlockIndexBuffer(HBuffer ib) { return UnlockBuffer(ib); }

// Шаг 13.4c: размер буфера из зеркала (cpu аллоцируется полностью,
// так что его размер = размер созданного буфера). UP-кольца клиент не видит.
HRESULT NvrhiBackend::GetBufferInfo(HBuffer buffer, unsigned* pByteSize)
{
  VK_API_GUARD;
  VK_TRACE("GetBufferInfo");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!buffer.p) return D3DERR_INVALIDCALL;
  const unsigned b = Hnd2Idx(buffer.p);
  if (b == kNoRes || b >= d.buffers.size() || !d.buffers[b].buf) return D3DERR_INVALIDCALL;
  if (pByteSize) *pByteSize = (unsigned)d.buffers[b].cpu.size();
  return S_OK;
}

//////////////////////////////////////////////////////////////////////////
// Поверхности

// GetRenderTargetData (шаг 13.3): у D3D9 это «RT -> системная поверхность»
// (путь снимка экрана: PFPostFXParams.cpp:321, renderer.cpp:943). Здесь —
// staging-копия в CPU-зеркало поверхности назначения, которую клиент потом
// блокирует через LockSurface.
HRESULT NvrhiBackend::GetRenderTargetData(HSurface srcSurface, HSurface dstSurface)
{
  VK_API_GUARD;
  VK_TRACE("GetRenderTargetData");
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned sIdx = Hnd2Idx(srcSurface.p), dIdx = Hnd2Idx(dstSurface.p);
  sIdx = ResolveSurfaceIdx(d, sIdx); dIdx = ResolveSurfaceIdx(d, dIdx);
  if (sIdx == kNoRes || sIdx >= d.surfaces.size() || !d.surfaces[sIdx].tex) return D3DERR_INVALIDCALL;
  if (dIdx == kNoRes || dIdx >= d.surfaces.size()) return D3DERR_INVALIDCALL;
  SurfaceObj& src = d.surfaces[sIdx];
  SurfaceObj& dst = d.surfaces[dIdx];
  if (!d.nvrhiDevice) return E_FAIL;
  const nvrhi::TextureDesc sd = src.tex->getDesc();
  const unsigned dw = dst.w ? dst.w : sd.width, dh = dst.h ? dst.h : sd.height;
  if (!dst.mirror.size()) dst.mirror.resize((size_t)dw * dh * 4u);
  if (!dst.mirror.size()) return E_FAIL;

  if (!src.staging)
  {
    src.staging = d.nvrhiDevice->createStagingTexture(
        nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
                            .setWidth(sd.width).setHeight(sd.height).setFormat(sd.format)
                            .setInitialState(nvrhi::ResourceStates::CopyDest)
                            .setKeepInitialState(true)
                            .setDebugName("seam RT data"),
        nvrhi::CpuAccessMode::Read);
    if (!src.staging) { VkNote("GetRenderTargetData: createStagingTexture", false, 0); return E_FAIL; }
  }
  if (!EnsureRecording()) return E_FAIL;
  d.commandList->copyTexture(src.staging, nvrhi::TextureSlice(), src.tex, nvrhi::TextureSlice());
  FlushRecording();
  VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");

  size_t srcPitch = 0;
  void* px = d.nvrhiDevice->mapStagingTexture(src.staging, nvrhi::TextureSlice(),
                                              nvrhi::CpuAccessMode::Read, &srcPitch);
  if (!px) { VkNote("GetRenderTargetData: mapStagingTexture", false, 0); return E_FAIL; }
  const unsigned w = sd.width < dw ? sd.width : dw;
  const unsigned h = sd.height < dh ? sd.height : dh;
  for (unsigned y = 0; y < h; ++y)
    memcpy(&dst.mirror[(size_t)y * dw * 4u],
           static_cast<unsigned char*>(px) + (size_t)y * srcPitch, (size_t)w * 4u);
  d.nvrhiDevice->unmapStagingTexture(src.staging);
  ++d.uploads;
  return S_OK;
}

// ColorFill (шаг 13.3): очистка поверхности цветом. Цвет — D3DCOLOR
// 0xAARRGGBB, как в Clear; логика та же, что у bcTarget в Clear.
HRESULT NvrhiBackend::ColorFill(HSurface surface, unsigned colorRGBA)
{
  VK_API_GUARD;
  VK_TRACE("ColorFill");
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned idx = Hnd2Idx(surface.p);
  idx = ResolveSurfaceIdx(d, idx);
  if (idx == kNoRes || idx >= d.surfaces.size() || !d.surfaces[idx].tex) return D3DERR_INVALIDCALL;
  SurfaceObj& so = d.surfaces[idx];
  if (so.depth) { VkNote("ColorFill: поверхность глубины", false, 0); return E_FAIL; }
  if (!EnsureRecording()) return E_FAIL;
  const float a = (float)((colorRGBA >> 24) & 0xFFu) / 255.0f;
  const float r = (float)((colorRGBA >> 16) & 0xFFu) / 255.0f;
  const float g = (float)((colorRGBA >> 8) & 0xFFu) / 255.0f;
  const float b = (float)(colorRGBA & 0xFFu) / 255.0f;
  d.commandList->clearTextureFloat(so.tex, nvrhi::TextureSubresourceSet(), nvrhi::Color(r, g, b, a));
  return S_OK;
}

// StretchRect (шаг 13.3). Переведён только случай «без масштабирования» —
// прямоугольники одинакового размера: это copyTexture (при равных размерах
// POINT и LINEAR совпадают). Со скейлом нужен блит-пайплайн с фильтром; в
// живом клиенте такого вызова нет (renderer.cpp:955 и rendersurface.cpp:129
// зовут с пустыми прямоугольниками, цепочка mip переехала в
// D3D9Backend::GenerateMipmaps), поэтому оставлен явный отказ с логом.
// --- Внутренний блит со скейлом (StretchRect) --------------------------------
// Квад по прямоугольнику приёмника: позиция идёт из VB уже в clip space (считает
// CPU), PS делает одну выборку из источника по UV прямоугольника. Ориентация —
// как у клиента (py=0 -> верхний край, y_clip = 1 - 2*py/H), вьюпорт — вся
// поверхность приёмника, глубина/трафарет/отсечение выключены.
static bool EnsureStretch(NvrhiBackendImpl& d)
{
  if (d.stretchVs && d.stretchPs && d.stretchIl && d.stretchVb
      && d.stretchLayTex && d.stretchLaySmp
      && d.stretchSmp[0] && d.stretchSmp[1]) return true;
  if (d.stretchFailed || !d.nvrhiDevice) { d.stretchFailed = true; return false; }
  d.stretchVs = d.nvrhiDevice->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setEntryName("main"),
      kStretchVsSpv, kStretchVsSpvSize);
  d.stretchPs = d.nvrhiDevice->createShader(
      nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("main"),
      kStretchPsSpv, kStretchPsSpvSize);
  // location NVRHI назначает по порядку массива (см. комментарий в
  // BuildInputLayout) — порядок обязан совпасть с SPIR-V: POSITION0=0, TEXCOORD0=1
  std::vector<nvrhi::VertexAttributeDesc> vads;
  nvrhi::VertexAttributeDesc a;
  a.setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(0u)
   .setElementStride(16u).setBufferIndex(0u).setIsInstanced(false);
  vads.push_back(a);
  a.setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(8u)
   .setElementStride(16u).setBufferIndex(0u).setIsInstanced(false);
  vads.push_back(a);
  d.stretchIl = d.nvrhiDevice->createInputLayout(vads.data(), (uint32_t)vads.size(), d.stretchVs);
  d.stretchVb = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
      .setByteSize(6u * 4u * sizeof(float)).setIsVertexBuffer(true)
      .setInitialState(nvrhi::ResourceStates::VertexBuffer).setKeepInitialState(true)
      .setDebugName("seam stretch vb"));
  // mipmap-фильтр везде выключен: у поверхности-приёмника уровней может не быть,
  // а линейный фильтр по уровням читал бы незагруженные mip
  nvrhi::SamplerDesc sd;
  sd.setAddressU(nvrhi::SamplerAddressMode::Clamp).setAddressV(nvrhi::SamplerAddressMode::Clamp)
    .setAddressW(nvrhi::SamplerAddressMode::Clamp).setMipFilter(false);
  d.stretchSmp[0] = d.nvrhiDevice->createSampler(nvrhi::SamplerDesc(sd).setMinFilter(true).setMagFilter(true));
  d.stretchSmp[1] = d.nvrhiDevice->createSampler(nvrhi::SamplerDesc(sd).setMinFilter(false).setMagFilter(false));
  {
    const nvrhi::VulkanBindingOffsets bo = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0).setSamplerOffset(0)
        .setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
    nvrhi::BindingLayoutDesc ld;
    ld.visibility = nvrhi::ShaderType::Pixel;
    ld.setRegisterSpaceAndDescriptorSet(0);
    ld.bindingOffsets = bo;
    ld.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(0));
    d.stretchLayTex = d.nvrhiDevice->createBindingLayout(ld);
    nvrhi::BindingLayoutDesc ls;
    ls.visibility = nvrhi::ShaderType::Pixel;
    ls.setRegisterSpaceAndDescriptorSet(1);
    ls.bindingOffsets = bo;
    ls.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(0));
    d.stretchLaySmp = d.nvrhiDevice->createBindingLayout(ls);
  }
  if (!d.stretchVs || !d.stretchPs || !d.stretchIl || !d.stretchVb
      || !d.stretchSmp[0] || !d.stretchSmp[1] || !d.stretchLayTex || !d.stretchLaySmp)
  {
    d.stretchFailed = true;
    VkNote("Stretch: инициализация внутреннего блита", false, 0);
    return false;
  }
  DebugTrace("VkStretch: внутренний блит заведён (vs %u Б, ps %u Б)",
             kStretchVsSpvSize, kStretchPsSpvSize);
  return true;
}

static std::pair<nvrhi::FramebufferHandle, nvrhi::GraphicsPipelineHandle>
GetStretchPipe(NvrhiBackendImpl& d, unsigned dIdx, unsigned fidx)
{
  const unsigned long long key = ((unsigned long long)dIdx << 1) | fidx;
  for (size_t i = 0; i < d.stretchPipes.size(); ++i)
    if (d.stretchPipes[i].first == key) return d.stretchPipes[i].second;
  if (d.stretchPipes.size() > 256u)
  {
    d.stretchPipes.clear();
    VkNote("stretchPipes очищен (>256)", true, 0);
  }
  nvrhi::FramebufferDesc fbd;
  fbd.addColorAttachment(d.surfaces[dIdx].tex);
  nvrhi::FramebufferHandle fb = d.nvrhiDevice->createFramebuffer(fbd);
  if (!fb)
  {
    VkNote("Stretch: createFramebuffer", false, 0);
    return std::make_pair(nvrhi::FramebufferHandle(), nvrhi::GraphicsPipelineHandle());
  }
  nvrhi::GraphicsPipelineDesc pd;
  pd.setPrimType(nvrhi::PrimitiveType::TriangleList).setInputLayout(d.stretchIl)
    .setVertexShader(d.stretchVs).setPixelShader(d.stretchPs);
  pd.renderState.rasterState.setFrontCounterClockwise(true)
    .setCullMode(nvrhi::RasterCullMode::None).setScissorEnable(false);
  pd.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false)
    .setStencilEnable(false);
  // порядок addBindingLayout обязан совпадать с порядком addBindingSet в GraphicsState
  pd.addBindingLayout(d.stretchLayTex);
  pd.addBindingLayout(d.stretchLaySmp);
  nvrhi::GraphicsPipelineHandle pipe = d.nvrhiDevice->createGraphicsPipeline(pd, fb->getFramebufferInfo());
  if (!pipe)
  {
    VkNote("Stretch: createGraphicsPipeline", false, 0);
    return std::make_pair(nvrhi::FramebufferHandle(), nvrhi::GraphicsPipelineHandle());
  }
  d.stretchPipes.push_back(std::make_pair(key, std::make_pair(fb, pipe)));
  return d.stretchPipes.back().second;
}

static bool StretchBlit(NvrhiBackendImpl& d, unsigned sIdx, long sx, long sy, long sw, long sh,
                        unsigned dIdx, long dx, long dy, long dw, long dh, unsigned fidx)
{
  if (!EnsureStretch(d)) return false;
  const float W = (float)d.surfaces[dIdx].w, H = (float)d.surfaces[dIdx].h;
  const float SW = (float)d.surfaces[sIdx].w, SH = (float)d.surfaces[sIdx].h;
  if (W <= 0.f || H <= 0.f || SW <= 0.f || SH <= 0.f) return false;
  const float x0 = 2.f * (float)dx / W - 1.f, x1 = 2.f * (float)(dx + dw) / W - 1.f;
  const float y0 = 1.f - 2.f * (float)dy / H, y1 = 1.f - 2.f * (float)(dy + dh) / H;
  const float u0 = (float)sx / SW, u1 = (float)(sx + sw) / SW;
  const float v0 = (float)sy / SH, v1 = (float)(sy + sh) / SH;
  const float verts[6][4] = {
    { x0, y0, u0, v0 }, { x1, y0, u1, v0 }, { x1, y1, u1, v1 },
    { x0, y0, u0, v0 }, { x1, y1, u1, v1 }, { x0, y1, u0, v1 }
  };
  const unsigned long long bkey = ((unsigned long long)sIdx << 24)
                                | ((unsigned long long)dIdx << 8) | fidx;
  if (d.stretchBsKey != bkey || !d.stretchBsTex || !d.stretchBsSmp)
  {
    nvrhi::BindingSetDesc bsd;
    bsd.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(0, d.surfaces[sIdx].tex));
    nvrhi::BindingSetHandle texSet = d.nvrhiDevice->createBindingSet(bsd, d.stretchLayTex);
    nvrhi::BindingSetDesc bsd2;
    bsd2.bindings.push_back(nvrhi::BindingSetItem::Sampler(0, d.stretchSmp[fidx]));
    nvrhi::BindingSetHandle smpSet = d.nvrhiDevice->createBindingSet(bsd2, d.stretchLaySmp);
    if (!texSet || !smpSet)
    {
      d.stretchBsKey = 0;
      d.stretchBsTex = nvrhi::BindingSetHandle();
      d.stretchBsSmp = nvrhi::BindingSetHandle();
      VkNote("Stretch: createBindingSet", false, 0);
      return false;
    }
    d.stretchBsTex = texSet;
    d.stretchBsSmp = smpSet;
    d.stretchBsKey = bkey;
  }
  std::pair<nvrhi::FramebufferHandle, nvrhi::GraphicsPipelineHandle> p =
      GetStretchPipe(d, dIdx, fidx);
  if (!p.second) return false;
  d.commandList->writeBuffer(d.stretchVb, verts, sizeof(verts));
  nvrhi::GraphicsState gs;
  gs.setPipeline(p.second).setFramebuffer(p.first);
  gs.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(d.stretchVb)
                     .setSlot(0).setOffset(0));
  gs.addBindingSet(d.stretchBsTex);
  gs.addBindingSet(d.stretchBsSmp);
  nvrhi::ViewportState vps;
  vps.addViewportAndScissorRect(nvrhi::Viewport(0.f, W, 0.f, H, 0.f, 1.f));
  gs.setViewport(vps);
  d.commandList->setGraphicsState(gs);
  d.commandList->draw(nvrhi::DrawArguments().setVertexCount(6));
  ++d.stretchBlits;
  if (d.stretchBlits <= 6u)
    DebugTrace("VkStretch: #%u %ldx%ld -> %ldx%ld (src %u, dst %u, фильтр %u)",
               d.stretchBlits, sw, sh, dw, dh, sIdx, dIdx, fidx);
  return true;
}

HRESULT NvrhiBackend::StretchRect(HSurface src, const BackendRect* pSrcRect, HSurface dst,
                                  const BackendRect* pDstRect, EBackendFilter filter)
{
  VK_API_GUARD;
  VK_TRACE("StretchRect");
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned sIdx = Hnd2Idx(src.p), dIdx = Hnd2Idx(dst.p);
  sIdx = ResolveSurfaceIdx(d, sIdx); dIdx = ResolveSurfaceIdx(d, dIdx);
  if (sIdx == kNoRes || sIdx >= d.surfaces.size() || !d.surfaces[sIdx].tex) return D3DERR_INVALIDCALL;
  if (dIdx == kNoRes || dIdx >= d.surfaces.size() || !d.surfaces[dIdx].tex) return D3DERR_INVALIDCALL;
  SurfaceObj& s = d.surfaces[sIdx];
  SurfaceObj& t = d.surfaces[dIdx];
  long sx = 0, sy = 0, sw = (long)s.w, sh = (long)s.h;
  if (pSrcRect) { sx = pSrcRect->left; sy = pSrcRect->top;
                  sw = pSrcRect->right - pSrcRect->left; sh = pSrcRect->bottom - pSrcRect->top; }
  long dx = 0, dy = 0, dw = (long)t.w, dh = (long)t.h;
  if (pDstRect) { dx = pDstRect->left; dy = pDstRect->top;
                  dw = pDstRect->right - pDstRect->left; dh = pDstRect->bottom - pDstRect->top; }
  if (sw != dw || sh != dh)
  {
    // copyTexture скейла не умеет — рисуется внутренний квад-блит (EnsureStretch).
    // Отказ прежней версии (E_FAIL) ронял цепочку mip у AUTOGENMIPMAP, водяную
    // карту и пост-эффекты (замер: 7566 провала за прогон).
    if (!EnsureRecording()) return E_FAIL;
    if (StretchBlit(d, sIdx, sx, sy, sw, sh, dIdx, dx, dy, dw, dh,
                    (filter == IRenderBackend::bfPoint) ? 1u : 0u)) return S_OK;
    char det[96];
    snprintf(det, sizeof(det), "%ldx%ld -> %ldx%ld (фильтр %d)", sw, sh, dw, dh, (int)filter);
    if (d.stretchScaleFail < 8u)
    {
      ++d.stretchScaleFail;
      VkNote("StretchRect: скейл не выполнен", false, det);
    }
    return E_FAIL;
  }
  if (sw <= 0 || sh <= 0) return D3DERR_INVALIDCALL;
  if (!EnsureRecording()) return E_FAIL;
  d.commandList->copyTexture(t.tex, nvrhi::TextureSlice().setOrigin((uint32_t)dx, (uint32_t)dy)
                             .setSize((uint32_t)dw, (uint32_t)dh),
                             s.tex, nvrhi::TextureSlice().setOrigin((uint32_t)sx, (uint32_t)sy)
                             .setSize((uint32_t)sw, (uint32_t)sh));
  return S_OK;
}
// Чтение ПРЯМОЙ копией (vkCmdCopyImageToBuffer) — для картинок swapchain'а.
// Причина: штатный путь NVRHI (createStagingTexture + copyTexture +
// mapStagingTexture) на swapchain-изображении вешается навсегда внутри
// mapStagingTexture (замер 2026-10-16, прогоны 22.13 и 22.23: кольцо обрывается
// на `mapStaging >`, `mapStaging <` нет). mapStagingTexture -> mapBuffer
// (vulkan-buffer.cpp:585) ждёт `queue->waitCommandList(buffer->lastUseCommandListID,
// ~0ull)`, то есть таймлайн-семафор NVRHI; для native-обёртки swapchain'а
// эта пара «submission -> сигнал» не сходится, ожидание бесконечное.
// У буфера, созданного нами и не участвующего в staging-учёте NVRHI,
// lastUseCommandListID = 0, поэтому mapBuffer не ждёт вовсе.
// Макет картинки мы ведём сами (TransitionBackImage), поэтому барьеры тоже наши.
bool NvrhiBackend::RawReadback(SurfaceObj& so, unsigned x0, unsigned y0, unsigned x1,
                               unsigned y1, void* pDst, unsigned dstPitch)
{
  NvrhiBackendImpl& d = *m_pImpl;
  if (!so.tex || !pDst) return false;
  const unsigned w = so.w, h = so.h;
  if (!w || !h || x0 >= x1 || y0 >= y1 || x1 > w || y1 > h) return false;
  const nvrhi::Format fmt = so.fmt;
  if (fmt != nvrhi::Format::BGRA8_UNORM)
  {
    VkNote("ReadSurface: прямой путь только для 32-битных цветов", false, 0);
    return false;
  }
  if (!so.readBuf)
  {
    so.readBuf = d.nvrhiDevice->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize((size_t)w * h * 4u)
                                             .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                             .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                             .setKeepInitialState(true)
                                             .setDebugName("seam readback buf"));
    if (!so.readBuf) { VkNote("ReadSurface: readback-буфер не создан", false, 0); return false; }
  }
  if (!EnsureRecording()) return false;

  const VkImage img = so.tex->getNativeObject(nvrhi::ObjectTypes::VK_Image);
  const VkBuffer buf = so.readBuf->getNativeObject(nvrhi::ObjectTypes::VK_Buffer);
  const VkCommandBuffer cmd = d.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
  if (!img || !buf || !cmd) { VkNote("ReadSurface: native-хендеры не получены", false, 0); return false; }

  // картинка после Present'а у нас в COLOR_ATTACHMENT_OPTIMAL (обратный переход
  // в TransitionBackImage), трекер NVRHI про неё ничего не знает.
  // Барьеры — классические (VkImageMemoryBarrier) через диспетчер C++-биндингов:
  // C-имена vkCmd* в этом TU не видны (vulkan.hpp с динамическим диспетчером),
  // образец — TransitionBackImage.
  VkImageMemoryBarrier b1;
  memset(&b1, 0, sizeof(b1));
  b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  b1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b1.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b1.image = img;
  b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b1.subresourceRange.levelCount = 1;
  b1.subresourceRange.layerCount = 1;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPipelineBarrier(
      cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
      0, 0, 0, 0, 1, &b1);

  VkBufferImageCopy region;
  memset(&region, 0, sizeof(region));
  region.bufferOffset = 0;
  region.bufferRowLength = 0;          // плотно
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageOffset.x = 0; region.imageOffset.y = 0; region.imageOffset.z = 0;
  region.imageExtent.width = w; region.imageExtent.height = h; region.imageExtent.depth = 1;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdCopyImageToBuffer(
      cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);

  VkImageMemoryBarrier b2;
  memset(&b2, 0, sizeof(b2));
  b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b2.image = img;
  b2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  b2.subresourceRange.levelCount = 1;
  b2.subresourceRange.layerCount = 1;
  VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPipelineBarrier(
      cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
      0, 0, 0, 0, 1, &b2);

  FlushRecording();
  VK_TRACE("readback waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("readback waitIdle <");

  void* px = d.nvrhiDevice->mapBuffer(so.readBuf, nvrhi::CpuAccessMode::Read);
  if (!px) { VkNote("ReadSurface: mapBuffer(readBuf)", false, 0); return false; }
  const unsigned rowBytes = (x1 - x0) * 4u;
  unsigned row = 0;
  for (unsigned y = y0; y < y1; ++y, ++row)
    memcpy(static_cast<unsigned char*>(pDst) + (size_t)row * dstPitch,
           static_cast<unsigned char*>(px) + (size_t)y * (size_t)w * 4u + (size_t)x0 * 4u,
           rowBytes);
  d.nvrhiDevice->unmapBuffer(so.readBuf);
  ++d.readbackRaw;
  return true;
}

HRESULT NvrhiBackend::ReadSurface(HSurface srcSurface, const BackendRect* pCropRect, void* pDstBuffer,
                                  unsigned dstRowPitch, EBackendPixelLayout* pOutLayout)
{
  VK_API_GUARD;
  VK_TRACE("ReadSurface");
  if (!pDstBuffer) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  unsigned idx = Hnd2Idx(srcSurface.p);
  idx = ResolveSurfaceIdx(d, idx);
  if (idx == kNoRes || idx >= d.surfaces.size() || !d.surfaces[idx].tex) return D3DERR_INVALIDCALL;
  if (!d.nvrhiDevice) return E_FAIL;
  SurfaceObj& so = d.surfaces[idx];
  const nvrhi::TextureDesc td = so.tex->getDesc();

  if (so.backBuffer || so.backBufferAlias)
  {
    long rx0 = 0, ry0 = 0, rx1 = (long)td.width, ry1 = (long)td.height;
    if (pCropRect)
    {
      rx0 = pCropRect->left; ry0 = pCropRect->top;
      rx1 = pCropRect->right; ry1 = pCropRect->bottom;
      if (rx0 < 0) rx0 = 0; if (ry0 < 0) ry0 = 0;
      if (rx1 > (long)td.width) rx1 = (long)td.width;
      if (ry1 > (long)td.height) ry1 = (long)td.height;
    }
    if (RawReadback(so, (unsigned)rx0, (unsigned)ry0, (unsigned)rx1, (unsigned)ry1,
                    pDstBuffer, dstRowPitch))
    {
      if (pOutLayout) *pOutLayout = plBGRA;
      return S_OK;
    }
    VkNote("ReadSurface: прямой путь не сработал, беру staging", false, 0);
  }
  // staging переиспользуется, но после пересоздания swapchain'а размеры картинки
  // меняются (замер: 800x600 -> 1024x768). Копия в staging другого размера —
  // невалидный copyTexture, а mapStagingTexture после этого встаёт навечно в
  // queue->waitCommandList (vulkan-buffer.cpp:585 -> waitSemaphores(~0ull)):
  // кольцо трассы обрывается на waitIdle < внутри ReadSurface (прогон
  // 2026-10-16-22.13, presents=299, кадр не снят).
  if (so.staging)
  {
    const nvrhi::TextureDesc sd = so.staging->getDesc();
    if (sd.width != td.width || sd.height != td.height || sd.format != td.format)
    {
      so.staging = nullptr;
      VkNote("ReadSurface: staging пересоздан (размеры картинки сменились)", true, 0);
    }
  }
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
  VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");

  size_t srcPitch = 0;
  VK_TRACE("mapStaging >");
  void* px = d.nvrhiDevice->mapStagingTexture(so.staging, nvrhi::TextureSlice(),
                                              nvrhi::CpuAccessMode::Read, &srcPitch);
  VK_TRACE("mapStaging <");
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
  VK_API_GUARD;
  VK_TRACE("GetBackBuffer");
  NvrhiBackendImpl& d = *m_pImpl;
  if (!pOut || d.imageIndex >= d.backSurfaceIdx.size())
    return E_FAIL;
  const unsigned idx = d.backSurfaceIdx[d.imageIndex];
  if (idx >= d.surfaces.size() || d.surfaces[idx].dead)
    return E_FAIL;              // swapchain пересоздан — клиент обязан взять заново
  d.surfaces[idx].backBufferAlias = true;
  pOut->p = NewHandle(&d, idx, hkSurface);
  return S_OK;
}

// GetRenderTarget (шаг 13.3): у D3D9 RT принадлежат устройству, здесь бэкенд
// помнит слоты сам (SetRenderTarget). Для индекса 0 без явного слота
// возвращается бэк-буфер — как у D3D9.
// Владение (шаг 13.4c): контракт seam'а — «владеющая ручка», клиент хранит такие
// ручки в COM-умных указателях (renderer.cpp:301 pDeviceColorSurface.Attach(...)),
// поэтому ручка — COM-форменный объект (SeamHandle), а не «индекс+1»: Release()
// уничтожает ресурс по последнему сбросу ссылки.
HRESULT NvrhiBackend::GetRenderTarget(unsigned index, HSurface* pOut)
{
  VK_API_GUARD;
  VK_TRACE("GetRenderTarget");
  if (!pOut) return D3DERR_INVALIDCALL;
  NvrhiBackendImpl& d = *m_pImpl;
  if (index < sizeof(d.rtSlot) / sizeof(d.rtSlot[0]) &&
      d.rtSlot[index] != kNoRes && d.rtSlot[index] < d.surfaces.size() &&
      !d.surfaces[d.rtSlot[index]].dead)
  {
    pOut->p = NewHandle(&d, d.rtSlot[index], hkSurface);
    return S_OK;
  }
  if (index == 0u && !d.backSurfaceIdx.empty() && d.imageIndex < d.backSurfaceIdx.size() &&
      d.backSurfaceIdx[d.imageIndex] < d.surfaces.size() &&
      !d.surfaces[d.backSurfaceIdx[d.imageIndex]].dead)
  {
    pOut->p = NewHandle(&d, d.backSurfaceIdx[d.imageIndex], hkSurface);
    return S_OK;
  }
  char det[64];
  snprintf(det, sizeof(det), "слот %u не заполнен (SetRenderTarget раньше?)", index);
  VkNote("GetRenderTarget", false, det);
  pOut->p = 0;
  return D3DERR_NOTFOUND;
}

void NvrhiBackend::EvictManagedResources()
{
  VK_API_GUARD;
  VK_TRACE("EvictManagedResources");
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
  VK_API_GUARD;
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
  VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");

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
  VK_API_GUARD;
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
  if (FAILED(LockTexture2D(tex, 0, 0, 0, LOCK_DISCARD, &pPix, &texPitch)) || !pPix || !texPitch)
  { VkNote("TexDIP: LockTexture2D", false, 0); return false; }
  for (unsigned y = 0; y < TSZ; ++y)
  {
    unsigned char* row = (unsigned char*)pPix + (size_t)y * texPitch;
    for (unsigned x = 0; x < TSZ; ++x)
    {
      row[x * 4u] = 0; row[x * 4u + 1] = 64; row[x * 4u + 2] = 255; row[x * 4u + 3] = 255;
    }
  }
  if (FAILED(UnlockTexture2D(tex, 0, 0))) { VkNote("TexDIP: UnlockTexture2D", false, 0); return false; }
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
  VK_TRACE("waitIdle >");
  d.nvrhiDevice->waitForIdle();
  VK_TRACE("waitIdle <");

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

// Тест шага 13.3: поверхности и UP-вывод. Берёт тот же материал, что и тест
// 12b (WaterDummyMaterial — без текстур), и проверяет четыре механизма, у
// которых нет D3D9-аналога «из коробки»: GetRenderTarget (реестр слотов),
// ColorFill, StretchRect (копия без скейла), GetRenderTargetData + LockSurface
// (CPU-зеркало) и DrawUP (кольцо вместо приведённой памяти).
bool NvrhiBackend::RunSurfaceOpsTest()
{
  VK_API_GUARD;
  NvrhiBackendImpl& d = *m_pImpl;
  if (!d.nvrhiDevice || !d.commandList) return false;

  const unsigned W = 128, H = 128;
  const char* path = "Shaders/UberShaders/WaterDummyMaterial.shd";
  std::vector<unsigned char> buf;
  unsigned index = 0, vsOff = 0, vsSize = 0, psOff = 0, psSize = 0;
  if (!LoadFirstShdPair(path, &buf, &index, &vsOff, &vsSize, &psOff, &psSize)) return false;
  const unsigned char* base = &buf[0];

  static const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  SetVertexShaderConstants(0, 4, kIdentity);      // WORLD  (c0)
  SetVertexShaderConstants(8, 4, kIdentity);      // VIEWPROJECTION (c8)

  HShader vs, ps;
  if (FAILED(CreateVertexShader(base + vsOff, vsSize, &vs))) { VkNote("SurfOps: VS", false, path); return false; }
  if (FAILED(CreatePixelShader(base + psOff, psSize, &ps))) { VkNote("SurfOps: PS", false, path); return false; }
  SetVertexShader(vs);
  SetPixelShader(ps);

  HSurface rt, dsf, rt2, plain;
  if (FAILED(CreateSurface(ssRenderTarget, W, H, bfA8R8G8B8, BACKEND_POOL_DEFAULT, &rt)))
  { VkNote("SurfOps: RT", false, 0); return false; }
  if (FAILED(CreateSurface(ssDepthStencil, W, H, bfD24S8, BACKEND_POOL_DEFAULT, &dsf)))
  { VkNote("SurfOps: DS", false, 0); return false; }
  if (FAILED(CreateSurface(ssRenderTarget, W, H, bfA8R8G8B8, BACKEND_POOL_DEFAULT, &rt2)))
  { VkNote("SurfOps: RT2", false, 0); return false; }
  if (FAILED(CreateSurface(ssPlain, W, H, bfA8R8G8B8, BACKEND_POOL_SYSMEM, &plain)))
  { VkNote("SurfOps: системная поверхность", false, 0); return false; }
  SetRenderTarget(0, rt);
  SetDepthStencil(dsf);

  // (1) GetRenderTarget обязан вернуть поверхность из реестра слотов
  HSurface got;
  const bool okRt = SUCCEEDED(GetRenderTarget(0, &got)) && got.p == rt.p;

  BackendViewport vp;
  vp.x = 0; vp.y = 0; vp.width = W; vp.height = H; vp.minZ = 0.f; vp.maxZ = 1.f;
  SetViewport(vp);
  // глубину обязан очистить: у только что созданной DS содержимое не
  // определено, а depthFunc=Less при случайном значении < 0.5 отсекает всю
  // геометрию молча (замер 2026-10-15: UP-вывод hr=0x00000000 и 0 пикселей при
  // исправных ColorFill/StretchRect)
  Clear(bcZBuffer, 0u, 1.f, 0u);

  // (2) ColorFill: зелёный 0xFF00FF00 -> в памяти BGRA = (0,255,0,255)
  if (FAILED(ColorFill(rt, 0xFF00FF00u))) { VkNote("SurfOps: ColorFill", false, 0); return false; }
  nstl::vector<unsigned char> px1((size_t)H * W * 4, 0);
  EBackendPixelLayout lay = plBGRA;
  if (FAILED(ReadSurface(rt, 0, &px1[0], W * 4, &lay))) { VkNote("SurfOps: ReadSurface 1", false, 0); return false; }
  unsigned green = 0;
  for (size_t i = 0; i < px1.size(); i += 4)
    if (px1[i] == 0 && px1[i + 1] == 255 && px1[i + 2] == 0 && px1[i + 3] == 255) ++green;

  // (3) DrawUP: те же координаты, что в тесте 12b (левая четверть кадра), но
  // данные — из стека вызывающего, без VB и без SetStreamSource
  static const float kTri[] = {
    -1.f, -1.f, 0.5f,
      0.f, -1.f, 0.5f,
     -1.f,  1.f, 0.5f,
  };
  VertexFormatDescriptor fd;
  fd.AddVertexElement(VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT3,
                                              VERETEXELEMENTUSAGE_POSITION, 0));
  HInputLayout il;
  if (FAILED(CreateInputLayout(fd, &il))) { VkNote("SurfOps: раскладка", false, 0); return false; }
  if (FAILED(SetInputLayout(il))) { VkNote("SurfOps: SetInputLayout", false, 0); return false; }
  SetInstanceCount(0);
  const HRESULT hrUp = DrawUP(RENDERPRIMITIVE_TRIANGLELIST, 1, kTri, (unsigned)sizeof(float) * 3u);

  nstl::vector<unsigned char> px2((size_t)H * W * 4, 0);
  if (FAILED(ReadSurface(rt, 0, &px2[0], W * 4, &lay))) { VkNote("SurfOps: ReadSurface 2", false, 0); return false; }
  unsigned painted = 0, paintedRight = 0;
  for (unsigned y = 0; y < H; ++y)
    for (unsigned x = 0; x < W; ++x)
    {
      const unsigned char* p = &px2[(size_t)y * W * 4 + (size_t)x * 4];
      if (p[2]) { ++painted; if (x >= W / 2u) ++paintedRight; }
    }

  // (4) StretchRect без скейла -> копия; сверяется число нарисованных пикселей
  const bool okStretch = SUCCEEDED(StretchRect(rt, 0, rt2, 0, bfPoint));
  nstl::vector<unsigned char> px3((size_t)H * W * 4, 0);
  if (okStretch && FAILED(ReadSurface(rt2, 0, &px3[0], W * 4, &lay)))
  { VkNote("SurfOps: ReadSurface 3", false, 0); return false; }
  unsigned painted2 = 0, same = 0;
  for (size_t i = 0; i < px2.size(); i += 4)
  {
    if (px3[i + 2]) ++painted2;
    if (px2[i] == px3[i] && px2[i + 1] == px3[i + 1] && px2[i + 2] == px3[i + 2] &&
        px2[i + 3] == px3[i + 3]) ++same;
  }

  // (5) GetRenderTargetData -> CPU-зеркало системной поверхности, затем
  // LockSurface: ровно тот же путь, которым клиент читает кадр с экрана
  const bool okRtData = SUCCEEDED(GetRenderTargetData(rt, plain));
  unsigned painted3 = 0;
  void* pPix = 0;
  unsigned pitch = 0;
  if (okRtData && SUCCEEDED(LockSurface(plain, 0, LOCK_READONLY, &pPix, &pitch)) && pPix)
  {
    for (unsigned y = 0; y < H; ++y)
      for (unsigned x = 0; x < W; ++x)
        if (static_cast<unsigned char*>(pPix)[(size_t)y * pitch + (size_t)x * 4 + 2]) ++painted3;
    UnlockSurface(plain);
  }

  const unsigned expect = W * H / 4u, tol = 2u * W;
  const bool okDraw = SUCCEEDED(hrUp) && painted + tol > expect && painted < expect + tol &&
                      paintedRight == 0;
  char det[256];
  snprintf(det, sizeof(det), "комбинация #%u; GetRenderTarget %s, зелёных %u/%u; UP: hr=0x%08X "
                           "нарисовано %u/%u (справа %u); StretchRect %s: %u/%u совпало; "
                           "RTData %s: %u", index, okRt ? "OK" : "СБОЙ", green, W * H,
           (unsigned)hrUp, painted, W * H, paintedRight, okStretch ? "OK" : "СБОЙ", same, W * H,
           okRtData ? "OK" : "СБОЙ", painted3);
  const bool ok = okRt && green == W * H && okDraw && okStretch && painted2 == painted &&
                  same == W * H && okRtData && painted3 == painted;
  VkNote(ok ? "SurfOps: поверхности и UP-вывод на месте" : "SurfOps: расхождение", ok, det);
  return ok;
}

//////////////////////////////////////////////////////////////////////////
// Шаг 13.4c: снимок бэк-буфера. Путь ровно клиентский: GetBackBuffer ->
// ReadSurface (ленивый staging + copyTexture + map). BMP пишется без D3DX:
// D3DX — это D3D9, а цель гейта — убедиться, что кадр доехал из Vulkan.

static void PutLE32(unsigned char* p, unsigned v)
{
  p[0] = (unsigned char)(v & 0xFFu);
  p[1] = (unsigned char)((v >> 8) & 0xFFu);
  p[2] = (unsigned char)((v >> 16) & 0xFFu);
  p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

// Диагностика чёрного кадра: что внутри offscreen-поверхностей в момент снимка.
// Композит клиента — экран-квад, сэмплирующий главный RT сцены; если сцена
// чёрная, чёрным будет и кадр, хотя сам композит исправен. Readback — прямой
// (RawReadback), временные ручки не создаём, чтобы не трогать владение.
//////////////////////////////////////////////////////////////////////////
// Отладка 2026-10-18: GPU-снимок текстур. TexDumpLevel0 пишет то, что клиент
// отдал в загрузку (CPU-зеркало), но это НЕ доказывает, что данные легли в
// VkImage. env PW_VK_TEXGPU=<файл>: на кадре PW_VK_SHOT каждая текстура с
// шириной >= PW_VK_TEXDUMP_MIN копируется в staging-текстуру, читается на CPU
// и пишется в файл в том же формате, что TXDP, но с магией TXGP и ФАКТИЧЕСКИМ
// шагом строки. Сравнение одной и той же (idx,w,h,fmt) в TXDP и TXGP отвечает
// на вопрос «текстура на GPU чёрная» (загрузка битая) vs «выборка не та».
void NvrhiBackend::TexGpuDump()
{
  VK_API_GUARD;
  NvrhiBackendImpl& d = *m_pImpl;
  // лимит и фильтр: по умолчанию читаем обратно ТОЛЬКО настоящие текстуры
  // (mipLevels >= 2): поверхности/RT (mips=1) затопили бы дамп первыми 60
  // записями и до боевых DXT-диффузов (индексы 2600+) дело не дошло (замер
  // прогона 79). PW_VK_TEXGPU_ALL=1 — читать всё, PW_VK_TEXGPU_MAX=<N> — лимит.
  char num[64];
  num[0] = 0;
  unsigned kMax = 200u;
  if (GetEnvironmentVariableA("PW_VK_TEXGPU_MAX", num, sizeof(num) - 1) && num[0]) kMax = (unsigned)atoi(num);
  char all[16];
  all[0] = 0;
  const bool allTex = GetEnvironmentVariableA("PW_VK_TEXGPU_ALL", all, sizeof(all) - 1) != 0;
  // диапазон индексов шва: CPU-дамп (TXDP) покрывает текстуры 2300+, а обход
  // с лимитом 200 записей останавливается на ~438 — сравнить нечего. Задав
  // PW_VK_TEXGPU_FROM/TO, читаем обратно ровно те объекты, по которым есть
  // CPU-снимок.
  unsigned fromIdx = 0, toIdx = 0xFFFFFFFFu;
  num[0] = 0;
  if (GetEnvironmentVariableA("PW_VK_TEXGPU_FROM", num, sizeof(num) - 1) && num[0]) fromIdx = (unsigned)atoi(num);
  num[0] = 0;
  if (GetEnvironmentVariableA("PW_VK_TEXGPU_TO", num, sizeof(num) - 1) && num[0]) toIdx = (unsigned)atoi(num);
  char path[512];
  path[0] = 0;
  if (!GetEnvironmentVariableA("PW_VK_TEXGPU", path, sizeof(path) - 1)) return;
  if (!d.nvrhiDevice) return;
  char mn[64];
  mn[0] = 0;
  unsigned minW = 0;
  if (GetEnvironmentVariableA("PW_VK_TEXDUMP_MIN", mn, sizeof(mn) - 1)) minW = (unsigned)atoi(mn);
  FILE* f = fopen(path, "wb");
  if (!f) { DebugTrace("TexGpuDump: %s не открывается", path); return; }
  fwrite("TXGP", 1, 4, f);
  const unsigned ver = 1u;
  fwrite(&ver, 4, 1, f);
  unsigned n = 0;
  for (unsigned i = 0; i < d.textures.size() && n < kMax; ++i)
  {
    NvrhiBackendImpl::TextureObj& to = d.textures[i];
    if (!to.tex || to.dead || to.w < minW) continue;
    if (i < fromIdx || i > toIdx) continue;
    if (!allTex && to.mipLevels < 2u) continue;
    const nvrhi::TextureDesc td = to.tex->getDesc();
    nvrhi::StagingTextureHandle st = d.nvrhiDevice->createStagingTexture(
        nvrhi::TextureDesc().setDimension(nvrhi::TextureDimension::Texture2D)
                            .setWidth(td.width).setHeight(td.height).setFormat(td.format)
                            .setMipLevels(1)
                            .setInitialState(nvrhi::ResourceStates::CopyDest)
                            .setKeepInitialState(true)
                            .setDebugName("seam tex gpu readback"),
        nvrhi::CpuAccessMode::Read);
    if (!st) { DebugTrace("TexGpuDump: #%u staging не создан", i); continue; }
    // PW_VK_TEXGPU_IMM=1 — читать обратно отдельным командным списком
    // (как TexCheck), а не общим списком рендера. Замер прогона 94: TexCheck
    // сразу после загрузки lvl0 даёт GPU==CPU (#101 mips8: 29817/31136), а
    // снимок на кадре 2000 через d.commandList — нули. Разделяет «данные
    // затёрты» и «снимок через список рендера нечитаем».
    static bool immMode = false;
    static bool immInit = false;
    if (!immInit)
    {
      immInit = true;
      char im[8]; im[0] = 0;
      immMode = GetEnvironmentVariableA("PW_VK_TEXGPU_IMM", im, sizeof(im) - 1) != 0;
    }
    if (immMode)
    {
      FlushRecording();   // неотправленные команды рендера обязаны уйти раньше снимка
      nvrhi::CommandListHandle cl = d.nvrhiDevice->createCommandList(
          nvrhi::CommandListParameters().setEnableImmediateExecution(false));
      if (!cl) { DebugTrace("TexGpuDump: #%u список не создан", i); continue; }
      cl->open();
      cl->copyTexture(st, nvrhi::TextureSlice(), to.tex, nvrhi::TextureSlice());
      cl->close();
      d.nvrhiDevice->executeCommandList(cl);
      d.nvrhiDevice->waitForIdle();
    }
    else
    {
    if (!EnsureRecording()) { fclose(f); return; }
    d.commandList->copyTexture(st, nvrhi::TextureSlice(), to.tex, nvrhi::TextureSlice());
    FlushRecording();
    d.nvrhiDevice->waitForIdle();
    }
    size_t pitch = 0;
    void* px = d.nvrhiDevice->mapStagingTexture(st, nvrhi::TextureSlice(),
                                                nvrhi::CpuAccessMode::Read, &pitch);
    if (!px) { DebugTrace("TexGpuDump: #%u map не удалось", i); continue; }
    const bool comp = (td.format >= nvrhi::Format::BC1_UNORM &&
                       td.format <= nvrhi::Format::BC7_UNORM);
    const unsigned rows = comp ? ((td.height + 3u) / 4u) : td.height;
    const unsigned long bytes = (unsigned long)pitch * rows;
    const unsigned hdr[9] = { i, td.width, td.height, to.mipLevels,
                              (unsigned)to.seamFormat, to.cube ? 1u : 0u,
                              (unsigned)pitch, 0u, bytes };
    fwrite(hdr, 4, 9, f);
    fwrite(px, 1, bytes, f);
    d.nvrhiDevice->unmapStagingTexture(st);
    ++n;
    DebugTrace("TexGpuDump: #%u %ux%u fmt%u mips%u шаг %u байт %u загруз=%u",
               i, td.width, td.height, (unsigned)to.seamFormat, to.mipLevels,
               (unsigned)pitch, bytes, to.uploads);
  }
  fclose(f);
  DebugTrace("TexGpuDump: записей %u (%s)", n, path);
}

void NvrhiBackend::ProbeFrameSurfaces()
{
  VK_API_GUARD;
  NvrhiBackendImpl& d = *m_pImpl;
  // 1) инвентарь: ЧЕМУ вообще поверхности клиент привязывает вывод (формат,
  // глубина, число выводов). Прежний фильтр «только BGRA8» показал 3 записи,
  // хотя записей поверхностей >1300 — т.е. главный RT сцены мог просто не
  // попасть в выборку.
  std::vector<std::pair<unsigned, unsigned> > byDraws;   // (выводы, индекс)
  for (unsigned i = 0; i < d.surfaces.size(); ++i)
  {
    SurfaceObj& so = d.surfaces[i];
    if (!so.tex || so.dead) continue;
    const unsigned dr = (i < d.rtDrawCount.size()) ? d.rtDrawCount[i] : 0u;
    if (dr) byDraws.push_back(std::make_pair(dr, i));
  }
  std::sort(byDraws.rbegin(), byDraws.rend());
  DebugTrace("VkProbe: поверхностей с выводами=%u (всего записей %u)",
             (unsigned)byDraws.size(), (unsigned)d.surfaces.size());
  for (unsigned k = 0; k < byDraws.size() && k < 8u; ++k)
  {
    const unsigned i = byDraws[k].second;
    const SurfaceObj& so = d.surfaces[i];
    DebugTrace("VkProbe: surf#%u %ux%u fmt=%d depth=%u back=%u вывода=%u",
               i, so.w, so.h, (int)so.fmt, so.depth ? 1u : 0u,
               (so.backBuffer || so.backBufferAlias) ? 1u : 0u, byDraws[k].first);
  }
  // 2) readback только для 32-битных цветовых
  std::vector<std::pair<unsigned, unsigned> > list;   // (площадь, индекс)
  for (unsigned k = 0; k < byDraws.size() && k < 8u; ++k)
  {
    const unsigned i = byDraws[k].second;
    SurfaceObj& so = d.surfaces[i];
    if (so.depth || so.backBuffer || so.backBufferAlias) continue;
    if (so.fmt != nvrhi::Format::BGRA8_UNORM) continue;
    if (!so.w || !so.h || so.w > 2048u || so.h > 2048u) continue;
    list.push_back(std::make_pair(so.w * so.h, i));
  }
  std::sort(list.rbegin(), list.rend());
  DebugTrace("VkProbe: кандидатов readback=%u", (unsigned)list.size());
  // Проверка самого readback: заливаем крупнейшую offscreen-поверхность зелёным
  // и читаем обратно. Если зелёный виден — readback исправен и вывод действительно
  // не оставляет пикселей; если нет — врёт readback, а не вывод.
  if ((d.shotProbe == 1u || d.shotProbe == 4u || d.shotProbe == 7u) && !byDraws.empty())
  {
    const unsigned i = byDraws[0].second;      // крупнейший получатель выводов
    SurfaceObj& so = d.surfaces[i];
    const unsigned bi = BackSurfaceIndex(d);
    // режим 4: тот же путь, но источник предварительно залит — проверяет саму
    // копию (а не только её приёмник)
    if (d.shotProbe == 4u && so.tex)
    {
      d.commandList->open();
      d.commandList->clearTextureFloat(so.tex, nvrhi::TextureSubresourceSet(),
                                       nvrhi::Color(0.f, 1.f, 0.f, 1.f));
      d.commandList->close();
      d.nvrhiDevice->executeCommandList(d.commandList);
      d.nvrhiDevice->waitForIdle();
      DebugTrace("VkProbe: surf#u залита зелёным (проверка копии)", i);
    }
    if (bi != kNoRes && bi < d.surfaces.size() && d.surfaces[bi].tex && so.tex
        && d.surfaces[bi].w == so.w && d.surfaces[bi].h == so.h)
    {
      d.commandList->open();
      d.commandList->copyTexture(d.surfaces[bi].tex, nvrhi::TextureSlice(),
                                 so.tex, nvrhi::TextureSlice());
      d.commandList->close();
      d.nvrhiDevice->executeCommandList(d.commandList);
      d.nvrhiDevice->waitForIdle();
      DebugTrace("VkProbe: surf#u скопирован в картинку %u (%ux%u)", i, bi, so.w, so.h);
    }
    else
      DebugTrace("VkProbe: копия невозможна (surf#u %ux%u, картинка %u)", i, so.w, so.h, bi);
  }
  if (d.shotProbe >= 2u && !list.empty())
  {
    const unsigned i = list[0].second;
    SurfaceObj& so = d.surfaces[i];
    d.commandList->open();
    d.commandList->clearTextureFloat(so.tex, nvrhi::TextureSubresourceSet(),
                                     nvrhi::Color(0.f, 1.f, 0.f, 1.f));
    d.commandList->close();
    d.nvrhiDevice->executeCommandList(d.commandList);
    d.nvrhiDevice->waitForIdle();
    DebugTrace("VkProbe: surf#%u залита зелёным", i);
  }
  if (d.shotProbe < 5u) return;   // readback offscreen — только по явному флагу (вешает staging)
  std::vector<unsigned char> buf;
  for (unsigned k = 0; k < list.size() && k < 6u; ++k)
  {
    const unsigned i = list[k].second;
    SurfaceObj& so = d.surfaces[i];
    const unsigned n = so.w * so.h;
    buf.assign((size_t)n * 4u, 0);
    // ReadSurface, а не RawReadback: прямой путь (свои барьеры + copyImageToBuffer)
    // годится только для картинки swapchain'а, где макет известен нам; для
    // offscreen-поверхностей макет ведёт трекер NVRHI, и ручные барьеры дают
    // нулевой readback даже сразу после clearTextureFloat (замер 2026.10.16-23.02:
    // surf#126 залита зелёным, readback — 0 ненулевых).
    HSurface hs;
    hs.p = NewHandle(&d, i, (unsigned)hkSurface);
    EBackendPixelLayout lay = plBGRA;
    const bool ok = hs.p && SUCCEEDED(ReadSurface(hs, 0, &buf[0], so.w * 4u, &lay));
    unsigned nonZero = 0, sample = 0;
    if (ok)
    {
      for (unsigned q = 0; q < n; ++q)
      {
        const unsigned v = *(const unsigned*)&buf[(size_t)q * 4u];
        if (v) { ++nonZero; if (!sample) sample = v; }
      }
    }
    DebugTrace("VkProbe: surf#%u %ux%u readback=%u ненулевых=%u из %u пример=0x%08x",
               i, so.w, so.h, ok ? 1u : 0u, nonZero, n, sample);
  }
}

bool NvrhiBackend::SaveBackBufferShot(const char* pPath)
{
  VK_API_GUARD;
  HSurface back;
  if (FAILED(GetBackBuffer(&back)) || !back.p)
  {
    VkNote("SaveBackBufferShot: бэк-буфер не получен", false, 0);
    return false;
  }
  BackendTextureInfo info;
  if (FAILED(GetSurfaceInfo(back, &info)) || !info.width || !info.height)
  {
    VkNote("SaveBackBufferShot: описание бэк-буфера не получено", false, 0);
    return false;
  }
  const unsigned W = info.width, H = info.height;
  nstl::vector<unsigned char> px((size_t)W * H * 4u, 0);
  EBackendPixelLayout lay = plBGRA;
  if (FAILED(ReadSurface(back, 0, &px[0], W * 4u, &lay)))
  {
    VkNote("SaveBackBufferShot: ReadSurface", false, 0);
    return false;
  }
  const unsigned rowBytes = W * 3u;
  const unsigned rowPad = (rowBytes + 3u) & ~3u;
  const unsigned pixSize = rowPad * H;
  FILE* f = fopen(pPath, "wb");
  if (!f)
  {
    VkNote("SaveBackBufferShot: не удалось открыть файл", false, pPath);
    return false;
  }
  unsigned char hdr[54];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B'; hdr[1] = 'M';
  PutLE32(hdr + 2,  54u + pixSize);   // bfSize
  PutLE32(hdr + 10, 54u);             // bfOffBits
  PutLE32(hdr + 14, 40u);             // biSize
  PutLE32(hdr + 18, W);               // biWidth
  PutLE32(hdr + 22, (unsigned)(-(int)H));  // biHeight <0 — строки сверху вниз
  hdr[26] = 1;                        // biPlanes
  hdr[28] = 24;                       // biBitCount
  PutLE32(hdr + 34, pixSize);         // biSizeImage
  fwrite(hdr, 1, sizeof(hdr), f);
  nstl::vector<unsigned char> row(rowPad, 0);
  for (unsigned y = 0; y < H; ++y)
  {
    const unsigned char* src = &px[(size_t)y * W * 4u];
    for (unsigned x = 0; x < W; ++x)
    {
      row[x * 3u + 0u] = src[x * 4u + 0u];   // B
      row[x * 3u + 1u] = src[x * 4u + 1u];   // G
      row[x * 3u + 2u] = src[x * 4u + 2u];   // R
    }
    fwrite(&row[0], 1, rowPad, f);
  }
  fclose(f);
  char det[128];
  snprintf(det, sizeof(det), "%s — %ux%u, порядок байт %s", pPath, W, H,
           lay == plBGRA ? "BGRA" : "RGBA");
  VkNote("SaveBackBufferShot", true, det);
  return true;
}

} // namespace Render
