#pragma once
//////////////////////////////////////////////////////////////////////////
// Внутренний интерфейс бэкенда рисования (этап 2 плана NVRHI,
// docs/PLAN_NVRHI.md §Этап 2).
//
// Здесь только ядро: кадр, область вывода, очистка, константы шейдеров,
// плоскость отсечения, служебные запросы. Создание ресурсов (текстуры,
// шейдеры, вершинные декларации) и операции над поверхностями пока остаются
// в Renderer/DX*Ref — они закрываются вместе с NvrhiBackend на этапе 3,
// иначе рефакторинг превращается в переписывание половины Render/ разом.
//
#include "renderwindow.h"        // Render::NativeWindow (окно вывода seam'а)
#include "renderpixellayout.h"   // Render::EBackendPixelLayout (формат пикселя при readback)
#include "locktype.h"            // Render::ERenderLockType (режим блокировки буфера)
#include "renderprimitivetype.h"   // Render::ERenderPrimitiveType
#include "vertexformatdescriptor.h"   // Render::VertexFormatDescriptor
#include "Features.h"            // Render::Features (биты BackendSystemInfo::features)

namespace Render
{

// «Виртуальные регистры» констант шейдера (GlobalMasks.h, §2.7 плана).
// Лексикон живёт здесь, а не в conststore.h: seam обязан уметь и записать,
// и прочитать регистр, а тень констант — только один из владельцев.
const int SHADER_CONST_REGISTERS = 256;

enum EShaderConstStage
{
  csVertex = 0,
  csPixel  = 1,
  csConstStageCount
};

// План предполагал каталог Src/Render/Backend/, но список файлов Render.vcproj
// плоский (подкаталогов в нём нет), а из этого списка строится CMake-граф
// клиента — поэтому файлы лежат рядом с остальными исходниками Render.
//
// Что тут сознательно осталось D3D9-специфичным:
//   * HRESULT   — тот же контракт у NVRHI, не мешает;
//   * RECT      — вынесен в BackendRect (Present/Scissor/StretchRect/ReadSurface);
//               Vulkan-свопчейну;
//   * семантика Present (прямого swapchain-интерфейса у NVRHI нет, он
//               остаётся наружу бэкенда, см. §3.1 плана).

// Флаги очистки — свои, не D3DCLEAR_*: значения выбирает бэкенд.
enum EBackendClearFlag {
  bcNone    = 0,
  bcTarget  = 1,
  bcZBuffer = 2,
  bcStencil = 4,
};

// Область вывода. По составу совпадает с D3DVIEWPORT9, но не зависит от D3D9
// (у Vulkan/OpenGL своя система координат и диапазон Z — переносит бэкенд).
// Прямоугольник для seam'а (Present, scissor, StretchRect, readback).
// RECT — Win32-структура, а seam обязан пережить не-Windows (ср.
// BackendViewport); поля названы как в RECT, чтобы конвертация была
// механической.
struct BackendRect {
  long left, top, right, bottom;
};

inline BackendRect MakeBackendRect(long left, long top, long right, long bottom) {
  BackendRect r; r.left = left; r.top = top; r.right = right; r.bottom = bottom;
  return r;
}

struct BackendViewport {
  unsigned x, y, width, height;
  float minZ, maxZ;
};

//////////////////////////////////////////////////////////////////////////
// Ручки ресурсов. На этапе 2 это указатели на объекты D3D9 (см. D3D9Backend),
// но тип у каждого вида ресурса свой: этап 3 подставляет настоящие ручки
// (nvrhi::BufferHandle и т.п.), не трогая вызывающий код. Отдельный тип на
// вид ресурса нужен именно ради этого — перепутать HTexture и HBuffer на
// этапе 3 должно быть невозможно.
struct HBuffer      { void* p = 0; };
struct HTexture     { void* p = 0; };
struct HSurface     { void* p = 0; };
struct HShader      { void* p = 0; };   // VS и PS: бэкенд знает, какой слот
struct HInputLayout { void* p = 0; };

// Пул ресурса — нейтральная лексика вместо D3DPOOL (docs/PLAN_NVRHI.md §2.3).
// Порядок значений совпадает с dxutils.h::PoolType (проверяется static_assert'
// ами в dxutils.cpp), поэтому вызывающий код продолжает писать RENDER_POOL_*.
// Семантика за бэкендом: в D3D9 это D3DPOOL+usage, в Vulkan/DX12 MANAGED и
// DYNAMIC становятся staging-копией + default-ресурсом + upload-кольцом.
// Формат ресурса. Набор замкнут по тому, что реально создаётся в клиенте
// (замер DDS-контента: A8R8G8B8, DXT1, DXT5, L8; кубы — DXT1/A8R8G8B8;
// проектный ERenderFormat покрывает остальные; глубина — D24S8), а не по всему
// пространству D3DFORMAT: перевод в D3DFORMAT знает только D3D9-реализация.
enum EBackendFormat {
  bfUnknown = 0,
  bfA8R8G8B8, bfX8R8G8B8,
  bfA8, bfL8, bfL16, bfR5G6B5,
  bfR16F, bfA16B16G16R16F,
  bfR32F, bfG32R32F, bfA32B32G32R32F,
  bfDXT1, bfDXT2, bfDXT3, bfDXT4, bfDXT5,
  bfD24S8,
  BACKEND_FORMAT_COUNT
};

// Назначение ресурса (в D3D9 — флаги D3DUSAGE_*; набор замкнут по замеру:
// RENDERTARGET, DEPTHSTENCIL, AUTOGENMIPMAP). Флаги памяти (DYNAMIC, WRITEONLY)
// сюда НЕ кладутся: они выражены пулом (EBackendPool -> D3DPOOL + usage), иначе
// одно и то же значение приходилось бы задавать дважды и оно могло бы разойтись.
enum EBackendUsage {
  usageNone          = 0,
  usageRenderTarget  = 1 << 0,
  usageDepthStencil  = 1 << 1,
  usageAutoGenMipmap = 1 << 2,
};

// Чем именно является поверхность: у D3D9 под каждую — своя фабрика, у
// Vulkan/DX12 — одна текстура с нужными usage-флагами.
enum EBackendSurfaceKind {
  ssPlain = 0,       // внекадровая плоская поверхность (staging для readback)
  ssRenderTarget,    // цель рендеринга (цвет)
  ssDepthStencil,    // цель рендеринга (глубина/трафарет)
};

enum EBackendPool {
  BACKEND_POOL_DEFAULT = 0,
  BACKEND_POOL_MANAGED,
  BACKEND_POOL_DYNAMIC,
  BACKEND_POOL_TEX_DYNAMIC,
  BACKEND_POOL_SYSMEM,
  BACKEND_POOL_SYSMEM_DYNAMIC,
  BACKEND_POOL_COUNT
};

template <class H, class P>
inline H MakeBackendHandle(P* pObj) {
  H h;
  h.p = static_cast<void*>(pObj);
  return h;
}

// Сведения об адаптере, нейтральные по отношению к графическому API
// (шаг 4, docs/PLAN_NVRHI.md §2.5). Заполняет бэкенд; это точка, где D3DCAPS9
// перестаёт течь в клиентский код: MaxVertexShaderConst (лимит партиклов,
// ParticleFX.cpp:498), гейт аппаратных карт теней (features -> HasDSM(),
// IConfigManager.h:45-50) и отчёты/дампы (description, vendor, device) читают
// только структуру. На этапе 3 вместо D3DCAPS9+GetAdapterIdentifier здесь
// появится синтез из Vulkan/DX12-свойств — и обязан дать те же значения
// (эталон пинов снят, §2.5).
//
// Чего здесь СОЗНАТЕЛЬНО нет: десятков масок caps (DevCaps, RasterCaps,
// TextureCaps, BlendCaps, …) и полей адаптера (SubSysId, Revision,
// DeviceIdentifier-GUID), которые читает матчер config.txt
// (configdatabase.cpp:942-1032, 1157-1162, 1383-1425, 1900-1993). Под них
// нейтральный словарь не синтезирован: матчер пока получает D3D9-структуры
// напрямую (ConfigManager::CheckDeviceCaps), и это единственный оставшийся
// D3D9-вход в ConfigManager.
struct BackendSystemInfo {
  // идентичность адаптера — входы матчера и отчётов
  unsigned vendorId;
  unsigned deviceId;
  unsigned long long driverVersion; // целиком, не HighPart/LowPart
  const char* description;          // буфер бэкенда, живёт пока жив бэкенд

  // возможности
  unsigned maxVertexShaderConst;    // 256 и на D3D9-железе, и на стенде (замер §2.5)
  bool isNullRef;                   // «пустой» референсный девайс (nullrender-путь)
  unsigned features;                // биты Render::Features
};

class IRenderBackend {
public:
  virtual ~IRenderBackend() {}

  // --- сведения об адаптере ---
  // Вызывается один раз после создания устройства (Renderer::CreateDevice).
  virtual HRESULT GetSystemInfo(BackendSystemInfo& out) = 0;

  // --- кадр ---
  virtual HRESULT BeginScene() = 0;
  virtual HRESULT EndScene() = 0;
  virtual HRESULT Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect) = 0;

  // --- область вывода и маска отсечения ---
  virtual void GetViewport(BackendViewport& vp) = 0;
  virtual void SetScissorRect(const BackendRect* pRect) = 0;
  // Чтение текущего scissor (save/restore вокруг временного). На этапе 3 —
  // тень в бэкенде: динамического состояния у NVRHI нет.
  virtual void GetScissorRect(BackendRect* pOut) = 0;

  // --- очистка привязанных поверхностей ---
  virtual HRESULT Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil) = 0;

  // --- константы шейдеров: «виртуальные регистры» c0..c255 (GlobalMasks.h) ---
  virtual HRESULT SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData) = 0;
  virtual HRESULT SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData) = 0;

  // Чтение тех же регистров обратно. Нужно не «для полноты»: на D3D9 у
  // устройства есть Get{Vertex,Pixel}ShaderConstantF, и им сверяют тень
  // констант (conststore, шаг 7); на uniform-буферах ответить нечем, поэтому
  // отвечать будет блок, собранный из тени. Пока бэкенд D3D9 отвечает из
  // устройства — расхождение с тенью означает, что где-то в клиенте константы
  // пишутся мимо Renderer::Set*ShaderConstants (или устройство сбрасывалось).
  virtual HRESULT GetShaderConstants(EShaderConstStage stage, unsigned startRegister,
                                     unsigned registerCount, void* pData) = 0;

  // Плоскость отсечения: включение/выключение и запись в слот 0 одним вызовом
  // (в D3D9 это два разных вызова — SetRenderState(D3DRS_CLIPPLANEENABLE) и
  // SetClipPlane(0, ...); в Vulkan это константа/clip-distance, поэтому пара
  // спрятана внутри бэкенда).
  virtual void SetClipPlane(bool enable, const float* pPlaneEquation) = 0;

  // --- биндинги ---
  // stride/offset — в байтах; slot 0 — поток вершин, инстансинг бэкенд
  // разворачивает сам (см. SmartRenderer::instancingEnabled).
  //
  // Возвращают HRESULT, хотя у D3D9-бэкенда это просто проброс кода вызова:
  // вызывающий код SmartRenderer логирует их по s_DXWarnLevel. Бэкенду этапа 3
  // (NVRHI, методы void) возвращать S_OK — терять эту диагностику нельзя.
  virtual HRESULT SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride) = 0;
  virtual HRESULT SetIndices(HBuffer indexBuffer) = 0;
  virtual HRESULT SetInputLayout(HInputLayout layout) = 0;
  virtual HRESULT SetVertexShader(HShader shader) = 0;
  virtual HRESULT SetPixelShader(HShader shader) = 0;
  virtual HRESULT SetTexture(unsigned samplerIndex, HTexture texture) = 0;
  virtual HRESULT SetRenderTarget(unsigned index, HSurface surface) = 0;
  virtual HRESULT SetDepthStencil(HSurface surface) = 0;
  // Текущая поверхность глубины/трафарета (после Reset() она новая). Владеющая
  // ручка — см. GetRenderTarget.
  virtual HRESULT GetDepthStencil(HSurface* pOut) = 0;
  virtual HRESULT SetViewport(const BackendViewport& vp) = 0;

  // --- вывод ---
  // Тип примитива — проектный ERenderPrimitiveType (renderformat.h), его
  // перевод в D3DPRIMITIVETYPE/VkPrimitiveTopology делает бэкенд.
  virtual HRESULT DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                              unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount) = 0;
  virtual HRESULT Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount) = 0;
  // UP-версии (данные прямо из памяти вызывающего): у NVRHI аналога нет, их
  // бэкенду этапа 3 придётся реализовывать через транзитный staging-буфер.
  // minIndexValue/indexCount/vertexCount — ровно те же тройка, что уходит в
  // D3D9 (сохраняем как есть, см. d3d9backend.cpp); index16 — формат индексов.
  virtual HRESULT DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                unsigned vertexCount, const void* pIndexData, bool index16,
                                const void* pVertexData, unsigned vertexStride) = 0;
  virtual HRESULT DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                         const void* pVertexData, unsigned vertexStride) = 0;

  // --- инстансинг ---
  // Число экземпляров текущей геометрии для последующих Draw* (0 — обычный
  // путь). Механизм — за бэкендом: D3D9 не имеет инстансных draw-вызовов и
  // выставляет частоту потоков (INDEXEDDATA|count для геометрии, INSTANCEDATA|1
  // для потока экземпляра), поэтому применяет их лениво перед рисованием —
  // после биндингов, как это делал SmartRenderer::ApplyStates. Vulkan/DX12
  // несут count прямо в drawIndexedInstanced, а данные экземпляра — vertex
  // attribute с divisor=1.
  // Размещение потока экземпляра и сдвиг геометрийных потоков остаются за
  // вызывающим (SmartRenderer::BindVertexBuffer): занятый поток 1 — соглашение
  // клиента о раскладке, а не свойство устройства (партиклы, например, кладут
  // в поток 1 свои per-instance данные и не сдвигаются).
  virtual HRESULT SetInstanceCount(unsigned count) = 0;

  // Вариант входной раскладки с атрибутом номера экземпляра. D3D9: сдвигает
  // ненулевые потоки на 1 вверх и добавляет в поток 1 SHORT2/BLENDINDICES0
  // (шиммер HLSL читает его как InstanceId). Vulkan/DX12: номер экземпляра —
  // встроенный SV_InstanceID, отдельного атрибута нет, и реализация вернёт
  // раскладку без изменений — но решение о представлении именно за бэкендом.
  virtual HRESULT CreateInstancedInputLayout(HInputLayout base, HInputLayout* pOut) = 0;

  // --- прочее ---
  // Renderer выбросил состояние конвейера (ClearDeviceResources перед Reset()
  // или удалением устройства). Бэкенд, кэширующий состояние для ленивой
  // переустановки (частоты потоков у D3D9-инстансинга), обязан счётчик сбросить:
  // иначе после Reset() он решит, что на устройстве уже нужное состояние, и
  // пропустит вызов — инстансинг молча сломается после смены разрешения.
  virtual void InvalidateBoundState() = 0;

  // --- создание ресурсов ---
  // Код шейдера — DXBC из контейнера .shd, размер в байтах. На этапе 3 в этот
  // же слот ляжет альтернативный блоб (SPIR-V) — см. §1.3 плана про контейнер
  // с несколькими бэкендами.
  // HRESULT наружу, а не в сигнатуре «вернул ручку или 0»: вызывающий код
  // логирует код ошибки (NI_DX_THROW), терять его нельзя.
  virtual HRESULT CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut) = 0;
  virtual HRESULT CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut) = 0;
  // Вершинная декларация строится из проектного VertexFormatDescriptor: перевод
  // в D3DVERTEXELEMENT9 (а в Vulkan — в VkVertexInputState) делает бэкенд.
  virtual HRESULT CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut) = 0;

  // Создание буферов (этап 2, пулы). Выбор D3DPOOL/usage — за бэкендом.
  // Ручка владеет одной ссылкой на созданный объект (в D3D9 — счётчик от Create*).
  // Начальных данных в сигнатуре нет намеренно: путь записи (Lock в D3D9 сегодня,
  // upload-кольцо в этапе 3) — отдельный, не надо плодить второй способ залить
  // буфер прямо на создании.
  // mipLevels: 0 = вся mipmap-цепочка (как в D3D9).
  virtual HRESULT CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                  EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                  HTexture* pOut) = 0;
  virtual HRESULT CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                    EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                    HTexture* pOut) = 0;
  // Multisample не передаётся: в клиенте MSAA не встречается (замер — во всех
  // созданиях D3DMULTISAMPLE_NONE / качество 0).
  virtual HRESULT CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                EBackendFormat format, EBackendPool pool, HSurface* pOut) = 0;
  // Копирование текстуры целиком (системная память -> DEFAULT). В D3D9 —
  // UpdateTexture; у NVRHI есть copy-операция.
  virtual HRESULT UpdateTexture(HTexture srcTexture, HTexture dstTexture) = 0;

  virtual HRESULT CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut) = 0;
  virtual HRESULT CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut) = 0;

  // --- запись в буферы ---
  // Режим блокировки — намерение, а не флаг D3D9 (см. locktype.h):
  //   LOCK_DEFAULT    - обычная запись (D3D9: 0)
  //   LOCK_DISCARD    - прежнее содержимое не нужно, взять свежий диапазон
  //                     (D3D9: D3DLOCK_DISCARD; Vulkan/DX12: шаг кольца
  //                     записи за пределы того, что уже в полёте)
  //   LOCK_NOOVERWRITE- дописать в текущий диапазон, не трогать то, что может
  //                     читаться GPU (D3D9: D3DLOCK_NOOVERWRITE)
  //   LOCK_READONLY   - чтение (D3D9: D3DLOCK_READONLY)
  //   LOCK_NOSYSLOCK  - D3D9-подсказка, остальные бэкенды игнорируют
  // size == 0 означает «весь буфер» — семантика D3D9; подменять её нулевым
  // диапазоном нельзя по той же причине, что и NULL-прямоугольник у
  // StretchRect (см. §2.8 «Пятый шаг»). Раздельно для VB и IB, потому что в
  // D3D9 это разные COM-интерфейсы с разными слотами vtable для Lock/Unlock —
  // кастом между ними не обойтись.
  virtual HRESULT LockVertexBuffer(HBuffer vb, unsigned offset, unsigned size,
                                   ERenderLockType mode, void** ppData) = 0;
  virtual HRESULT UnlockVertexBuffer(HBuffer vb) = 0;
  virtual HRESULT LockIndexBuffer(HBuffer ib, unsigned offset, unsigned size,
                                  ERenderLockType mode, void** ppData) = 0;
  virtual HRESULT UnlockIndexBuffer(HBuffer ib) = 0;

  // --- поверхности ---
  // Фильтр при копировании прямоугольника: замер вызовов показал, что в клиенте
  // реально используются только POINT и LINEAR, поэтому enum свой, а не
  // D3DTEXTUREFILTERTYPE.
  enum EBackendFilter { bfPoint = 0, bfLinear = 1 };
  virtual HRESULT GetRenderTargetData(HSurface srcSurface, HSurface dstSurface) = 0;
  virtual HRESULT ColorFill(HSurface surface, unsigned colorRGBA) = 0;
  virtual HRESULT StretchRect(HSurface src, const BackendRect* pSrcRect, HSurface dst,
                              const BackendRect* pDstRect, EBackendFilter filter) = 0;

  // Чтение пикселей поверхности в память вызывающего (32 бита на пиксель).
  // pCropRect — прямоугольник источника (0 = вся поверхность), dstRowPitch —
  // stride строки назначения в байтах. Бэкенд сам решает, нужен ли staging:
  // у D3D9 поверхности пула DEFAULT (RT, бэк-буфер) CPU недоступны, поэтому
  // они сначала копируются в системную память.
  // Требование: пиксель 32 бита (A8R8G8B8/X8R8G8B8) — другие форматы в клиенте
  // для readback не используются.
  virtual HRESULT ReadSurface(HSurface srcSurface, const BackendRect* pCropRect, void* pDstBuffer,
                              unsigned dstRowPitch, EBackendPixelLayout* pOutLayout) = 0;

  // Бэк-буфер текущего окна. Ручка действительна до конца вызова вызывающего
  // кода: поверхность принадлежит устройству (у Vulkan — swapchain'у), а не
  // тому, кто её запросил.
  // Возвращает НЕвладеющую ручку: действительна до конца вызова звенящего.
  virtual HRESULT GetBackBuffer(HSurface* pOut) = 0;
  // В отличие от GetBackBuffer — владеющая ручка (вызывающий обязан отпустить;
  // в Render это делает DXSurfacePtr::Attach). Нужна для хранения поверхности
  // кадра/глубины между кадрами.
  virtual HRESULT GetRenderTarget(unsigned index, HSurface* pOut) = 0;

  // --- прочее ---
  // D3D9-специфично (пул MANAGED); для бэкендов без managed-пула — пустая
  // функция, вызов остаётся на месте.
  virtual void EvictManagedResources() = 0;
};

} // namespace Render
