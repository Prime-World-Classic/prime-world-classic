#pragma once
//////////////////////////////////////////////////////////////////////////
// Vulkan-бэкенд поверх NVRHI (шаг 12 очереди этапа 3, docs/PLAN_NVRHI.md §3.y, §3.z).
//
// Что закрывает этот файл (шаг 12a): клиент собирает и линкует NVRHI, поднимает
// VkInstance + VkDevice + surface/swapchain на СОБСТВЕННОМ окне и отдаёт в это же
// окно кадр, нарисованный Vulkan'ом (clear + present). Смысл — снять все
// неизвестные этапа 3 (граф сборки, CRT, динамический диспетчер Vulkan, годится
// ли HWND seam'а для VkSurfaceKHR) до того, как начнётся перевод проходов.
// Проверки «блоб из .shd v2 создаётся и рисует» и «SDL3-окно даёт surface»
// сняты раньше спайками в tools/nvrhi_probe (1d2ac6a45, ff7d237c0); здесь
// проверяется, что то же живёт внутри PW_Game.exe.
//
// Чего здесь СОЗНАТЕЛЬНО нет:
//  * типов NVRHI/Vulkan в интерфейсе — pimpl. Остальной Render компилируется
//    без /std: (дефолт cl 14.44 = C++14, замер FLAGS в build.ninja), а NVRHI и
//    Vulkan-Hpp требуют C++17; пофайловый /std:c++17 /EHsc получает только
//    nvrhibackend.cpp (Tools/ClientCMake/gen_cmake.py, X64_EXTRA_OPTS), поэтому
//    его заголовки не должны просачиваться в другие TU;
//  * SDL3-окна — окно остаётся клиентское (NativeWindow), SDL3 нужен шагу
//    «окно и ввод», не здесь;
//  * рисования через seam — перевод проходов это шаг 13. Остальные методы
//    считаются (см. UnsupportedCalls/DumpStats): этот список и есть work-list
//    шага 13, снятый по факту вызовов, а не по догадке.
//
// В renderer.cpp этот бэкенд пока НЕ подменяет D3D9Backend: он создаётся
// дополнительно по env PW_VK_SPIKE=1 и рисует свой кадр после D3D9-вывода
// (см. комментарий у Renderer::Present).
//
#include "renderbackend.h"

namespace Render
{

// Реализация (nvrhi/vulkan) живёт целиком в nvrhibackend.cpp.
struct NvrhiBackendImpl;

class NvrhiBackend : public IRenderBackend
{
public:
  // window — окно клиента (NativeWindow; внутри HWND). width/height == 0 —
  // взять текущий размер окна. 0 в ответе — Vulkan недоступен (нет драйвера,
  // нет surface-расширений, нет подходящего устройства); причину пишет в
  // DebugTrace/systemLog.
  static NvrhiBackend* Create(NativeWindow window, unsigned width, unsigned height);

  virtual ~NvrhiBackend();

  // --- диагностика (не часть seam'а) ---
  // Сколько раз клиент вызвал метод, который Vulkan-цель ещё не реализует.
  unsigned UnsupportedCalls() const { return m_unsupported; }
  // Дамп счётчиков по каждому незакрытому методу (work-list шага 13).
  void DumpStats(const char* pTag) const;
  // Имя устройства и размеры swapchain'а — в отчёты/дампы.
  const char* DeviceName() const;
  unsigned SwapchainWidth() const;
  unsigned SwapchainHeight() const;

  // --- сведения об адаптере ---
  virtual HRESULT GetSystemInfo(BackendSystemInfo& out);

  // --- кадр ---
  virtual HRESULT BeginScene();
  virtual HRESULT EndScene();
  virtual HRESULT Present(NativeWindow window, const BackendRect* pSrcRect, const BackendRect* pDstRect);

  // --- область вывода и маска отсечения ---
  virtual void GetViewport(BackendViewport& vp);
  virtual void SetScissorRect(const BackendRect* pRect);
  virtual void GetScissorRect(BackendRect* pOut);

  // --- очистка привязанных поверхностей ---
  // модель состояний конвейера (шаг 13.4b): вызывается путями вывода перед
  // каждым выводом; здесь это только источник ключа для кэша пайплайнов
  virtual void SetPipelineState(const BackendPipelineState& state, unsigned long long stateKey);
  virtual HRESULT Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil);

  // --- константы шейдеров ---
  virtual HRESULT SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData);
  virtual HRESULT SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData);
  virtual HRESULT GetShaderConstants(EShaderConstStage stage, unsigned startRegister,
                                     unsigned registerCount, void* pData);

  virtual void SetClipPlane(bool enable, const float* pPlaneEquation);

  // --- биндинги ---
  virtual HRESULT SetStreamSource(unsigned slot, HBuffer vertexBuffer, unsigned byteOffset, unsigned stride);
  virtual HRESULT SetIndices(HBuffer indexBuffer);
  virtual HRESULT SetInputLayout(HInputLayout layout);
  virtual HRESULT SetVertexShader(HShader shader);
  virtual HRESULT SetPixelShader(HShader shader);
  virtual HRESULT SetTexture(unsigned samplerIndex, HTexture texture);
  virtual HRESULT SetRenderTarget(unsigned index, HSurface surface);
  virtual HRESULT SetDepthStencil(HSurface surface);
  virtual HRESULT GetDepthStencil(HSurface* pOut);
  virtual HRESULT SetViewport(const BackendViewport& vp);

  // --- вывод ---
  virtual HRESULT DrawIndexed(ERenderPrimitiveType type, unsigned baseVertexIndex, unsigned minIndex,
                              unsigned indexedVertexCount, unsigned startIndex, unsigned primitiveCount);
  virtual HRESULT Draw(ERenderPrimitiveType type, unsigned startVertex, unsigned primitiveCount);
  virtual HRESULT DrawIndexedUP(ERenderPrimitiveType type, unsigned minIndexValue, unsigned indexCount,
                                unsigned vertexCount, const void* pIndexData, bool index16,
                                const void* pVertexData, unsigned vertexStride);
  virtual HRESULT DrawUP(ERenderPrimitiveType type, unsigned primitiveCount,
                         const void* pVertexData, unsigned vertexStride);

  // --- инстансинг ---
  virtual HRESULT SetInstanceCount(unsigned count);
  virtual HRESULT SetInstanceSource(HBuffer perInstanceData, unsigned stride, unsigned offset);
  virtual bool IsStreamBufferBound(const void* pVertexBuffer) const;

  // --- прочее ---
  virtual void InvalidateBoundState();

  // --- создание ресурсов ---
  virtual HRESULT CreateVertexShader(const void* pShaderCode, unsigned codeSize, HShader* pOut);
  virtual HRESULT CreatePixelShader(const void* pShaderCode, unsigned codeSize, HShader* pOut);
  virtual HRESULT CreateInputLayout(const VertexFormatDescriptor& descr, HInputLayout* pOut);
  virtual HRESULT CreateTexture2D(unsigned width, unsigned height, unsigned mipLevels,
                                  EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                  HTexture* pOut);
  virtual HRESULT CreateTextureCube(unsigned edgeLen, unsigned mipLevels,
                                    EBackendUsage usage, EBackendFormat format, EBackendPool pool,
                                    HTexture* pOut);
  virtual HRESULT CreateSurface(EBackendSurfaceKind kind, unsigned width, unsigned height,
                                EBackendFormat format, EBackendPool pool, HSurface* pOut);
  virtual HRESULT UpdateTexture(HTexture srcTexture, HTexture dstTexture);
  // Загрузка бит в текстуру (шаг 13.1) — часть seam'а, см. renderbackend.h:345
  virtual HRESULT LockTexture2D(HTexture texture, unsigned level, const BackendRect* pRect,
                                ERenderLockType mode, void** ppData, unsigned* pRowPitch);
  virtual HRESULT UnlockTexture2D(HTexture texture, unsigned level);
  // Описание и mip-цепочка (шаг 13.2) — см. renderbackend.h:363
  virtual HRESULT GetTextureInfo(HTexture texture, BackendTextureInfo* pInfo);
  virtual HRESULT GetSurfaceInfo(HSurface surface, BackendTextureInfo* pInfo);
  virtual HRESULT GenerateMipmaps(HTexture texture);
  // Блокировка поверхности (шаг 13.2) — RenderSurface::LockRect в клиенте звал
  // COM напрямую
  virtual HRESULT LockSurface(HSurface surface, const BackendRect* pRect,
                              ERenderLockType mode, void** ppData, unsigned* pRowPitch);
  virtual HRESULT UnlockSurface(HSurface surface);
  virtual HRESULT CreateVertexBuffer(unsigned size, EBackendPool pool, HBuffer* pOut);
  virtual HRESULT CreateIndexBuffer(unsigned size, bool index32, EBackendPool pool, HBuffer* pOut);

  // --- запись в буферы ---
  virtual HRESULT LockVertexBuffer(HBuffer vb, unsigned offset, unsigned size,
                                   ERenderLockType mode, void** ppData);
  virtual HRESULT UnlockVertexBuffer(HBuffer vb);
  virtual HRESULT LockIndexBuffer(HBuffer ib, unsigned offset, unsigned size,
                                  ERenderLockType mode, void** ppData);
  virtual HRESULT UnlockIndexBuffer(HBuffer ib);

  // --- поверхности ---
  virtual HRESULT GetRenderTargetData(HSurface srcSurface, HSurface dstSurface);
  virtual HRESULT ColorFill(HSurface surface, unsigned colorRGBA);
  virtual HRESULT StretchRect(HSurface src, const BackendRect* pSrcRect, HSurface dst,
                              const BackendRect* pDstRect, EBackendFilter filter);
  virtual HRESULT ReadSurface(HSurface srcSurface, const BackendRect* pCropRect, void* pDstBuffer,
                              unsigned dstRowPitch, EBackendPixelLayout* pOutLayout);

  virtual HRESULT GetBackBuffer(HSurface* pOut);
  virtual HRESULT GetRenderTarget(unsigned index, HSurface* pOut);

  // --- прочее ---
  virtual void EvictManagedResources();

private:
  NvrhiBackend();
  bool Init(NativeWindow window, unsigned width, unsigned height);
  void Shutdown();
  // Swapchain поверх surface'а текущего окна. Вызывается из Init и (шаг 12b)
  // при смене размера/потере устройства.
  bool CreateSwapchain(unsigned width, unsigned height);
  // Разбор перед пересозданием и само пересоздание: окно клиента resize'ится
  // ПОСЛЕ CreateDevice, без этого Present ловит VK_ERROR_OUT_OF_DATE_KHR на
  // каждом кадре (замер 2026-10-15).
  void DestroySwapchain();
  bool RecreateSwapchain();
  bool RecreateSurface();
  // Учёт незакрытого метода. Возвращает E_NOTIMPL, чтобы вызывающий код вёл
  // себя как с любой другой ошибкой (а не принял пустой результат за успех).
  // const: seam объявляет IsStreamBufferBound константным, а счётчик обязан
  // расти и оттуда, иначе work-list шага 13 недосчитается вызовов.
  HRESULT Unsupported(int which) const;

  // --- шаг 12b: механизмы реализации seam'а (не часть интерфейса) ---
  // Командный список один на бэкенд: открывается лениво и закрывается в Present
  // либо там, где вызывающему нужен результат (ReadSurface, тест 12b).
  bool EnsureRecording();
  void FlushRecording();
  HRESULT CreateShaderImpl(const void* pCode, unsigned codeSize, bool fragment, HShader* pOut);
  // Блокировка/разблокировка: у seam'а четыре метода, у реализации — два
  // (разница только в том, индексный буфер или вершинный).
  HRESULT LockBuffer(HBuffer h, unsigned offset, unsigned size, void** ppData);
  HRESULT UnlockBuffer(HBuffer h);
  HRESULT DrawImpl(ERenderPrimitiveType type, bool indexed, unsigned startIndexLocation,
                   unsigned startVertexLocation, unsigned count, unsigned explicitVtx = 0);

public:
  // Тест шага 12b: один настоящий DIP через seam (материал из .shd v2 в
  // offscreen RT, чтение пикселей обратно). Вызывается по env PW_VK_DIP.
  bool RunSeamDipTest();
  // Тест шага 13.1: тот же путь, но с текстурой и сэмплером (SkyMaterial).
  // Загрузка бит идёт через seam (LockTexture2D/UnlockTexture2D).
  bool RunTextureDipTest();
  // Тест шага 13.3: UP-вывод (DrawUP/DrawIndexedUP из памяти вызывающего) +
  // ColorFill + StretchRect + readback. Env PW_VK_DIP бит 4.
  bool RunSurfaceOpsTest();

private:
  NvrhiBackendImpl* m_pImpl;
  mutable unsigned m_unsupported;
  // Тени состояния, которые seam обязан уметь читать обратно (см. шаг 7:
  // GetShaderConstants проверяет тень констант о устройство).
  float* m_constShadow[csConstStageCount];
  BackendViewport m_viewport;
  BackendRect m_scissor;
  bool m_scissorValid;
  float m_clipPlane[4];
  bool m_clipPlaneEnabled;
  unsigned m_instanceCount;
};

} // namespace Render
