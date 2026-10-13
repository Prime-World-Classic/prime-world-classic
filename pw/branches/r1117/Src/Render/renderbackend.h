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
// План предполагал каталог Src/Render/Backend/, но список файлов Render.vcproj
// плоский (подкаталогов в нём нет), а из этого списка строится CMake-граф
// клиента — поэтому файлы лежат рядом с остальными исходниками Render.
//
// Что тут сознательно осталось D3D9-специфичным:
//   * HRESULT   — тот же контракт у NVRHI, не мешает;
//   * RECT      in Present/SetScissorRect — Win32, а не D3D9, нужен и
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
struct BackendViewport {
  unsigned x, y, width, height;
  float minZ, maxZ;
};

class IRenderBackend {
public:
  virtual ~IRenderBackend() {}

  // --- кадр ---
  virtual HRESULT BeginScene() = 0;
  virtual HRESULT EndScene() = 0;
  virtual HRESULT Present(HWND window, const RECT* pSrcRect, const RECT* pDstRect) = 0;

  // --- область вывода и маска отсечения ---
  virtual void GetViewport(BackendViewport& vp) = 0;
  virtual void SetScissorRect(const RECT* pRect) = 0;

  // --- очистка привязанных поверхностей ---
  virtual HRESULT Clear(unsigned clearFlags, unsigned colorRGBA, float z, unsigned stencil) = 0;

  // --- константы шейдеров: «виртуальные регистры» c0..c255 (GlobalMasks.h) ---
  virtual HRESULT SetVertexShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData) = 0;
  virtual HRESULT SetPixelShaderConstants(unsigned startRegister, unsigned registerCount, const void* pData) = 0;

  // Плоскость отсечения: включение/выключение и запись в слот 0 одним вызовом
  // (в D3D9 это два разных вызова — SetRenderState(D3DRS_CLIPPLANEENABLE) и
  // SetClipPlane(0, ...); в Vulkan это константа/clip-distance, поэтому пара
  // спрятана внутри бэкенда).
  virtual void SetClipPlane(bool enable, const float* pPlaneEquation) = 0;

  // --- прочее ---
  // D3D9-специфично (пул MANAGED); для бэкендов без managed-пула — пустая
  // функция, вызов остаётся на месте.
  virtual void EvictManagedResources() = 0;
};
