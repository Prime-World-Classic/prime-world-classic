#pragma once

#include "rendermode.h"
#include "renderwindow.h"   // NativeWindow: Renderer не хранит HWND (§2.8)
#include "vertexformatdescriptor.h"
#include "dipdescriptor.h"
#include "renderstates.h"
#include "renderstatesmanager.h"
#include "dxutils.h"
#include "renderformat.h"
#include "TextureManager.h"
#include "IQuery.h"
#include "ConstantProtection.h"
#include "../System/ScopedPtr.h"

#include "renderstatesmanager.h"
#include "LightsManager.h"
#include "DxIntrusivePtr.h"
#include "../System/Thread.h"


namespace Render
{

class Texture2D;

struct Caps
{
	float pixelShaderVersion;
	float vertexShaderVersion;
	bool  bSupportSM30;
  // Заполняются из caps устройства при его смене (FillCaps). Нужны, чтобы
  // отвязка ресурсов (ClearDeviceResources) не дёргала GetDeviceCaps; на этапе 3
  // те же поля придут из синтезируемой caps-структуры (§2.5).
  unsigned numSimultaneousRTs;
  unsigned maxStreams;
};

// Seam этапа 2 (docs/PLAN_NVRHI.md §2.8): сырые вызовы устройства идут через
// IRenderBackend. Здесь только прямое объявление: renderbackend.h подключается
// в тех TU, которые реально дергают бэкенд (Render/*.cpp). Иначе любое касание
// интерфейса пересобирает полмира — renderer.h включается почти везде.
class IRenderBackend;
class NvrhiBackend;   // Vulkan-бэкенд (шаг 12a, §3.z): живёт рядом с D3D9, не подменяет его
struct BackendRect;   // тот же нейтральный прямоугольник, что и в seam: публичный
                      // API Renderer больше не берёт Win32 RECT (§2.8). Полный тип
                      // нужен вызывающему коду (он и подключает renderbackend.h).

class Renderer
{
	static Renderer*      s_pInstance;

  // Создаётся вместе с устройством (реализация — D3D9Backend, см. renderer.cpp),
  // живёт до его смерти.
  IRenderBackend*       pBackend;

  // Шаг 12a (docs/PLAN_NVRHI.md §3.z): Vulkan-бэкенд поверх NVRHI, поднимаемый
  // ДОПОЛНИТЕЛЬНО по env PW_VK_SPIKE=1. Он не подменяет pBackend: его кадр
  // (clear + present) кладётся поверх D3D9-вывода, а счётчик незакрытых методов
  // seam'а даёт work-list шага 13 по фактическим вызовам клиента. Живёт ровно
  // столько, сколько pBackend (пересоздаётся вместе с устройством).
  NvrhiBackend*         pVkSpike;

  // Шаг 13.4c: pBackend УКАЗЫВАЕТ на Vulkan-бэкенд (env PW_VK_BACKEND=1).
  // Отдельного указателя нет — владение остаётся у pBackend, иначе два
  // delete на один объект.
  bool                  bVkBackend;

	ReallocateWrapper<RenderStatesManager> statesMan;
  LightsManager         lightsMan;

	RenderMode            realRenderMode;
	IDirect3D9*           pD3D;
	IDirect3DDevice9*     pDevice;
  bool                  bDeviceLost;
  bool                  bResetDevice;
  bool                  bTripleBufferUsed;
  bool                  isRunningUnderPerfHUD;
	NativeWindow          window; // не HWND: окно — граница платформы, seam его не знает
	D3DPRESENT_PARAMETERS presentParams;
	DXSurfacePtr          pDeviceDepthSurface;
	DXSurfacePtr          pDeviceColorSurface;
	Caps                  caps;
  DXQueryRef            pFrameSynchQuery;
  DXQueryRef            pPrevFrameSynchQuery; // gfx_gpu_sync_lag: issued a frame earlier, waited for now
  bool                  prevFrameSynchIssued;
  threading::Mutex      renderSceneLock;
  bool                  renderSceneLocked;

	//vector<Texture2D*>   textures;
	Rect                  sourceRect;

	//nstl::vector<Texture2D*> surfaces;

	ConstantProtectionChecker constChecker;

	void FillCaps();

public:
	explicit Renderer(NativeWindow _window);
	 ~Renderer();

	static void Init(NativeWindow _window)
	{
	  s_pInstance = new Renderer(_window);
	}

	static Renderer* Get();// { return s_pInstance; }

  static void Term()
	{
		delete s_pInstance;
    s_pInstance = 0;
	}

	const RenderMode& GetCurrentRenderMode();

  // Граница платформы: HWND приходит снаружи и сразу превращается в ручку seam'а.
  void SetWindow( NativeWindow w )
  {
    window = w;
    presentParams.hDeviceWindow = static_cast< HWND >( w.p );
  }
  
  D3DSWAPEFFECT GetSwapEffectParams() const { return presentParams.SwapEffect; }

	int GetModesCount() const;
	void GetMode(int index, unsigned int &width, unsigned int &height, unsigned int &refreshRate) const;

	bool Start(const RenderMode& _renderMode);

  void Stop();
	const Caps & GetCaps() const { return caps; }

	DXVertexShaderRef CreateVertexShader(unsigned char const* data);
	DXPixelShaderRef CreatePixelShader(unsigned char const* data);
	// Тот же вызов с явной длиной кода (шаг 13.4d: SPIR-V-слой контейнера v2).
	// Пустая ссылка при неудаче — NI_DX_THROW в shipping не бросает исключение.
	DXVertexShaderRef CreateVertexShader(unsigned char const* data, unsigned size);
	DXPixelShaderRef CreatePixelShader(unsigned char const* data, unsigned size);

	DXVertexDeclarationRef CreateVertexFormatDeclaration(const VertexFormatDescriptor& descr);

	void SetClipPlane(bool bEnable, CVec4 *pPlane);

#ifdef _DEBUG
	void BeginConstantProtectionBlock(ContextType context) { constChecker.BeginBlock(context); }
	void EndConstantProtectionBlock() { constChecker.EndBlock(); }
	void ResetConstantProtectionBlock(ContextType context) { constChecker.Reset(context); }
#else
	void BeginConstantProtectionBlock(ContextType /*context*/) {}
	void EndConstantProtectionBlock() {}
	void ResetConstantProtectionBlock(ContextType /*context*/) {}
#endif

	void SetPixelShaderConstants(unsigned int startRegisterIndex, unsigned int registersCount, const void* data);
	void SetVertexShaderConstants(unsigned int startRegisterIndex, unsigned int registersCount, const void* data);

	void SetPixelShaderConstantsVector4(unsigned int startRegisterIndex, const CVec4& value)
	{
		SetPixelShaderConstants(startRegisterIndex, 1, &value);
	}
	void SetPixelShaderConstantsHDRColor(unsigned int startRegisterIndex, const HDRColor& value)
	{
		SetPixelShaderConstants(startRegisterIndex, 1, &value);
	}
	void SetPixelShaderConstantsFloat(unsigned int startRegisterIndex, const float value)
	{
		CVec4 fake(value, 0.f, 0.f, 0.f);
		SetPixelShaderConstants(startRegisterIndex, 1, &fake);
	}
	void SetPixelShaderConstantsMatrix(unsigned int startRegisterIndex, const SHMatrix& value)
	{
		SetPixelShaderConstants(startRegisterIndex, 4, &value);
	}

	void SetVertexShaderConstantsMatrix(unsigned int startRegisterIndex, const SHMatrix& value)
	{
		SetVertexShaderConstants(startRegisterIndex, 4, &value);
	}
	void SetVertexShaderConstantsMatrix43(unsigned int startRegisterIndex, unsigned int matrixCount, const Matrix43* pMatrix43)
	{
		SetVertexShaderConstants(startRegisterIndex, 3*matrixCount, pMatrix43);
	}
	void SetVertexShaderConstantsVector4(unsigned int startRegisterIndex, const CVec4& value)
	{
		SetVertexShaderConstants(startRegisterIndex, 1, &value);
	}
	void SetVertexShaderConstantsFloat(unsigned int startRegisterIndex, const float value)
	{
		CVec4 fake(value, 0.f, 0.f, 0.f);
		SetVertexShaderConstants(startRegisterIndex, 1, &fake);
	}
	void SetVertexShaderConstantsColor(unsigned int startRegisterIndex, const HDRColor& value)
	{
		SetVertexShaderConstants(startRegisterIndex, 1, &value);
	}

	void BeginScene();
	void EndScene();
	void Present();
  void Present( NativeWindow window_, const Rect* sourceRect_, const Rect* destRect_ );

	const DXSurfacePtr& GetDepthStencilSurface() { return pDeviceDepthSurface; }
	const DXSurfacePtr& GetColorSurface() { return pDeviceColorSurface; }

  void ClearDepthStencil();
	void Clear(Color color);
	void ClearColorOnly(Color color);

	const Rect &GetSourceRect() const { return sourceRect; }

	bool GetRenderTargetData(IDirect3DSurface9* pSrc, IDirect3DSurface9 *pDst);
	void StretchSurface(const DXSurfaceRef &pSrc, const DXSurfaceRef &pDst, bool bLinear = false);

  // --- Этап 1 миграции на NVRHI (docs/PLAN_NVRHI.md): сюда переезжают прямые
  //     вызовы устройства, которые раньше жили вне Render/. Всё это — D3D9,
  //     на этапах 3-4 часть исчезнет, часть станет реализацией IRenderBackend.
  //     Цель сейчас: вне Render/ не остаётся ни одного вызова
  //     IDirect3DDevice9, чтобы поверхность портирования была измерима.
  void GetViewport(D3DVIEWPORT9 &vp);
  // Прямоугольник — нейтральный BackendRect (тот же тип, что и в seam);
  // Win32 RECT остаётся только внутри вызывающего кода.
  void SetScissorRect(const BackendRect *pRect);
  void ColorFillSurface(IDirect3DSurface9 *pSurface, DWORD color);
  void StretchSurfaceRect(IDirect3DSurface9 *pSrc, const RECT *pSrcRect,
                          IDirect3DSurface9 *pDst, const RECT *pDstRect, D3DTEXTUREFILTERTYPE filter);
  // Сброс managed-пула. Есть только в D3D9; в Vulkan/DX12 аналога нет, поэтому
  // на этапе 3 вызов просто удаляется (пула managed там нет).
  void EvictManagedResources();
  // Доступная видеопамять адаптера (-1 = неизвестно). D3D9 + ConfigManager.
  int GetAvailableVidMem();
  // Бэк-буфер -> файл (needCrop=false берётся весь кадр). Формат — по
  // расширению (png/bmp/dds), см. Render/screenshot.h.
  bool SaveScreenshotToFile(const char *fileName, int x, int y, int width, int height, bool needCrop);
  // Кадр -> файл своим png/bmp/dds (без D3DX). useBackBuffer=false — поверхность
  // сцены (GetColorSurface), true — бэк-буфер. pCropRect — прямоугольник
  // источника (0 — весь кадр), keepAlpha=false — альфа отбрасывается.
  bool SaveFrameToFile(const char* fileName, bool keepAlpha, const BackendRect* pCropRect,
                       bool useBackBuffer);

  RenderStatesManager* GetRenderStatesManager() { return &statesMan; }
	LightsManager* GetLightsManager() { return &lightsMan; }

	IDirect3DDevice9* GetDevice() { return pDevice; }
  IRenderBackend*     GetBackend() { return pBackend; }
  // 0, если PW_VK_SPIKE не выставлен (в проде не выставлен никогда).
  NvrhiBackend*       GetVkSpike() { return pVkSpike; }
  IDirect3D9*       GetD3D() { return pD3D; }
  
	bool IsRunningUnderPerfHUD() const { return isRunningUnderPerfHUD; }

  bool DeviceIsLost() const { return bDeviceLost; }
  
  void ApplyNewParams( const RenderMode &_renderMode );
  
  bool NeedToFilterResolutionFromUser( unsigned int width, unsigned int height ) const;
  
  void CorrectRenderMode( RenderMode &_renderMode ) const;
  
  void InvalidateResources();
  
private:
  void RecoverFromDeviceLost( D3DPRESENT_PARAMETERS& pp );
  void Convert( const RenderMode &_renderMode, D3DPRESENT_PARAMETERS &pp );
  bool CreateDevice(D3DPRESENT_PARAMETERS& pp);
  bool ResetDevice(D3DPRESENT_PARAMETERS& pp);
  void OnDeviceChanged();
  void Synchronize();
  void CorrectResolution( unsigned int &width, unsigned int &height ) const;
  void CorrectRefreshRate( unsigned int width, unsigned int height, unsigned int &refreshRate ) const;
  void ClearDeviceResources();
  
  void UpdateRealRenderMode( const RenderMode &configRenderMode );
};

inline Renderer* GetRenderer() { return Renderer::Get(); }

// Запрос рендер-бэкенда, переданный протоколом запуска ("render=vulkan", см.
// PW_Client/Game.cpp). Задаётся до Renderer::Init(), читается в CreateDevice().
// Пустая строка — бэкенд не запрошен, работает старый пайплайн (D3D9).
// Возможные значения: "vulkan", "d3d9" (регистр не важен).
void SetBackendRequest(const char* request);
const char* GetBackendRequest();

inline IDirect3DDevice9* GetDevice() { return Renderer::Get()->GetDevice(); }

// Seam этапа 2 (docs/PLAN_NVRHI.md §2.8): путь коду Render к бэкенду.
inline IRenderBackend* GetBackend() { return Renderer::Get()->GetBackend(); }

inline RenderStatesManager* GetStatesManager() { return Renderer::Get()->GetRenderStatesManager(); }

inline LightsManager* GetLightsManager() { return Renderer::Get()->GetLightsManager(); } 

struct ConstantProtectionBlock
{
	ConstantProtectionBlock(ContextType context) { GetRenderer()->BeginConstantProtectionBlock(context); }
	~ConstantProtectionBlock() { GetRenderer()->EndConstantProtectionBlock(); }
};

}; // namespace Render