#include "stdafx.h"
#include "Vendor/DirectX/Include/DxErr.h"
#include "d3d9backend.h"   // seam этапа 2: здесь единственном создаётся реализация
#include "nvrhibackend.h"  // шаг 12a (§3.z): Vulkan-поверх-NVRHI, только по PW_VK_SPIKE
#include "conststore.h"

#include "System/AssertDumper.h"
#include "System/InlineProfiler.h"
#include "System/CrashRptWrapper.h"
#include "renderstatesmanager.h"
#include "renderflagsconverter.h"
#include "renderresourcemanager.h"
#include "dxutils.h" // Get()/NI_DX_THROW для поверхностей; D3DX отсюда больше не нужен
#include "screenshot.h"   // свои png/bmp/dds вместо D3DXSaveSurfaceToFile
#include <vector>
#include "vertexformatdescriptor.h"
#include "texture.h"
#include "rendersurface.h"
#include "renderer.h"
#include "ImmediateRenderer.h"
#include "DxResourcesControl.h"
#include "Reallocate.h"

#include "smartrenderer.h"
#include "ConfigManager.h"
#include "DXWarnSignal.h"

IMPLEMENT_SIMPLE_SIGNAL_ST(DXWarnSignal)

static DXWarnSignal s_DXWarnLevel;
REGISTER_VAR_INTERFACE( "DXWarnLevel", MakeSignalProxy(&s_DXWarnLevel, 0), STORAGE_NONE );

static bool s_render_threadsafe = true;
REGISTER_VAR( "render_threadsafe", s_render_threadsafe, STORAGE_NONE );

DEFINE_DEV_VAR(g_synchronizeRender, true, "synchronize_render", STORAGE_NONE);

static bool s_use_triple_buffer = false;
REGISTER_VAR("gfx_triple_buffer", s_use_triple_buffer, STORAGE_NONE);

static UINT s_adapterToUse = D3DADAPTER_DEFAULT;
REGISTER_VAR("adapter_to_use", s_adapterToUse, STORAGE_NONE);

static bool s_dump_adapters = false;
REGISTER_VAR("dump_adapters", s_dump_adapters, STORAGE_NONE);

// @BVS@CAPS DEPENDENT ACTUALLY
const int SHADER_REGISTERS_COUNT = 256;

const D3DFORMAT frontBufferFormat = D3DFMT_X8R8G8B8;

#ifndef _SHIPPING
#define _NVPERFHUD_ // comment this to disable NVPerfHUD profiling //////////////////
#endif // _SHIPPING

IMPLEMENT_SIMPLE_SIGNAL_ST(NullRenderSignal)

namespace Render
{

static NullRenderSignal s_nullrender;

Renderer*  Renderer::s_pInstance = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Renderer* Renderer::Get()
{
  return s_pInstance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const RenderMode& Renderer::GetCurrentRenderMode()
{
	return realRenderMode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Renderer::Renderer(NativeWindow _window) : 
	pD3D(0), 
	pDevice(0),
	pBackend(0),
  pVkSpike(0),
  bVkBackend(false),
	window(_window),
	bDeviceLost(true),
  bResetDevice(false),
  bTripleBufferUsed(false),
	isRunningUnderPerfHUD(false),
  renderSceneLocked( false )
{
  SmartRenderer::ResetTriangleAndDipCount();

	pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (pD3D == 0)
	{
		//Log.Add(MAIN_LOG, "[!]  Direct3DCreate9\n"); FIXME
	}

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Renderer::~Renderer()
{
  pDeviceDepthSurface = 0;
  pDeviceColorSurface = 0;
  pFrameSynchQuery = 0;

  NotifyResourcesRelease();
  DeviceDeleteHandler::NotifyDeviceDelete();
  DumpDXResourcesLeaks();
  delete pBackend;
  pBackend = 0;
  // Порядок важен: nvrhi-объекты живут внутри бэкенда и должны умереть раньше
  // VkDevice, а VkDevice — раньше, чем освободится загрузка vulkan-1.dll.
  delete pVkSpike;
  pVkSpike = 0;
  if (pDevice)
    pDevice->Release();
  pDevice = 0;

  pD3D->Release();
  pD3D = 0;

  DebugTrace("releasing renderer...\nnow we are NOT able to render  :)\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int Renderer::GetModesCount() const
{
	return pD3D->GetAdapterModeCount(s_adapterToUse, frontBufferFormat);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::CorrectResolution( unsigned int &width, unsigned int &height ) const
{
  const int numModes = GetModesCount();
  float bestCoeff = FLT_MAX;
  unsigned int bestWidth = 0;
  unsigned int bestHeight = 0;

  for (int i = 0; i < numModes; i++)
  {
    unsigned int curWidth, curHeight, curRefreshRate;

    GetMode(i, curWidth, curHeight, curRefreshRate);

    if ( curWidth == width && curHeight == height )
      return; 

    float deltaWidth =  (float)curWidth - width;
    float deltaHeight = (float)curHeight - height;  

    //       ,
    //         
    //,       
    //
    //       
    // 
    const float additionalDeltaPercent = 0.1f;

    if( deltaWidth < 0 )  deltaWidth  -= width * additionalDeltaPercent;
    if( deltaHeight < 0 ) deltaHeight -= height * additionalDeltaPercent;

    //     
    float curCoef = deltaWidth * deltaWidth + deltaHeight * deltaHeight;

    //DebugTrace( "Relation %dx%d to %dx%d is %g", width, height, curWidth, curHeight, curCoef );

    if( curCoef < bestCoeff )   
    {
      bestWidth = curWidth;
      bestHeight = curHeight;
      bestCoeff = curCoef;
    } 
  }

  NI_ASSERT( bestCoeff < FLT_MAX, "Problem with rendermodes" );

  DebugTrace("Requested resolution %d x %d not supported, change to %d x %d", width, height, bestWidth, bestHeight );

  width = bestWidth;
  height = bestHeight;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::CorrectRefreshRate( unsigned int width, unsigned int height, unsigned int &refreshRate ) const
{
  if( refreshRate == D3DPRESENT_RATE_DEFAULT )
    return;

  const int numModes = GetModesCount();
  float bestCoeff = FLT_MAX;
  unsigned int bestRefreshRate = 0;

  for (int i = 0; i < numModes; i++)
  {
    unsigned int curWidth, curHeight, curRefreshRate;

    GetMode(i, curWidth, curHeight, curRefreshRate);

    if ( curWidth != width || curHeight != height )
      continue;

    if( curRefreshRate == refreshRate )
      return;

    const float delta = (float)curRefreshRate - refreshRate;
    const float curCoef = delta * delta;

    if( curCoef < bestCoeff )   
    {
      bestRefreshRate = curRefreshRate;
      bestCoeff = curCoef;
    }
  }

  NI_ASSERT( bestCoeff < FLT_MAX, "Problem with rendermodes" );

  DebugTrace("Requested refresh rate %d not supported, change to %d ", refreshRate, bestRefreshRate );

  refreshRate = bestRefreshRate;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Renderer::GetMode(int index, unsigned int &width, unsigned int &height, unsigned int &refreshRate) const
{
  width = RenderMode::WIDTH_DEFAULT;
	height = RenderMode::HEIGHT_DEFAULT;
	refreshRate = RenderMode::REFRESH_RATE_DEFAULT;

	D3DDISPLAYMODE mode;
	HRESULT hr = pD3D->EnumAdapterModes(s_adapterToUse, frontBufferFormat, index, &mode);
	NI_VERIFY(hr == D3D_OK, NStr::StrFmt("Adapter mode enumeration problem for index %d (max:%d)", index, GetModesCount()), return; );
	width = mode.Width;
	height = mode.Height;
	refreshRate = mode.RefreshRate;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::UpdateRealRenderMode( const RenderMode &configRenderMode )
{
  realRenderMode = configRenderMode;
  
  D3DDISPLAYMODE mode = {};
  pDevice->GetDisplayMode( 0, &mode );
  
  //    mode.Width  mode.Height   
  // ,    presentParams
  realRenderMode.width = presentParams.BackBufferWidth;
  realRenderMode.height = presentParams.BackBufferHeight;
  
  //   refreshRate  0,       
  realRenderMode.refreshRate = mode.RefreshRate; 
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::NeedToFilterResolutionFromUser( unsigned int width, unsigned int height ) const
{
  //http://msdn.microsoft.com/en-us/library/ee417691.aspx#_1.5
  if( width < 1024 || height < 768 )
    return true;
    
  //The desktop aspect ratio must be used as a search criterion if the game 
  //chooses a different default resolution.
  const float curAspectRatio = float(width) / height;
  unsigned int deskWidth, deskHeight;
  
  GetDesktopResolution( deskWidth, deskHeight );
  
  const float desktopAspectRatio = float(deskWidth) / deskHeight;
  
  // ,       
  //         
  //   aspectRatioThreshold 
  const float aspectRatioThreshold = 0.1f;
  
  const float ratio = (curAspectRatio - desktopAspectRatio) / desktopAspectRatio;
  
  return ratio * ratio > aspectRatioThreshold * aspectRatioThreshold;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Renderer::OnDeviceChanged()
{
  DebugTrace("OnDeviceChanged");
  
  FillCaps();

  // Save links to surfaces — через seam (владеющие ручки, см. renderbackend.h)
  HSurface hSurface;
  if (SUCCEEDED(pBackend->GetDepthStencil(&hSurface)))
    pDeviceDepthSurface.Attach(static_cast<IDirect3DSurface9*>(hSurface.p));
  if (SUCCEEDED(pBackend->GetRenderTarget(0, &hSurface)))
    pDeviceColorSurface.Attach(static_cast<IDirect3DSurface9*>(hSurface.p));

  IDirect3DQuery9* pQuery;
  pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pQuery);
  pFrameSynchQuery = DXQueryRef(pQuery, false);

  
  //systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: render mode is " << presentParams.BackBufferWidth << " x " << presentParams.BackBufferHeight << " @ " 
    //<< (presentParams.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE ? "NOSYNC" : "VSYNC") << endl;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::CreateDevice(D3DPRESENT_PARAMETERS& pp)
{
  pDeviceDepthSurface = 0;
  pDeviceColorSurface = 0;
  pFrameSynchQuery = 0;

  if(RENDER_DISABLED) {
    bDeviceLost = true;
    return false;
  }
  // Defaults
  //UINT AdapterToUse = s_adapterToUse;
  D3DDEVTYPE DeviceType = s_nullrender ? D3DDEVTYPE_NULLREF : D3DDEVTYPE_HAL;

  // Look for 'NVIDIA PerfHUD' adapter
#ifdef _NVPERFHUD_
  for (UINT Adapter = 0; Adapter < pD3D->GetAdapterCount(); Adapter++)
  {
    D3DADAPTER_IDENTIFIER9 Identifier;
    HRESULT Res;
    Res = pD3D->GetAdapterIdentifier(Adapter, 0, &Identifier);

    if (strstr(Identifier.Description, "PerfHUD") != 0)
    {
      NI_ASSERT( !s_nullrender, "NullRender is activated under PerfHUD" );

      s_adapterToUse = Adapter;
      DeviceType = D3DDEVTYPE_REF;
			isRunningUnderPerfHUD = true;
      systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: Attaching to adapter #" << Adapter << " (" << Identifier.Description << ")" << endl;
      break;
    }
  }

#endif // _NVPERFHUD_
  if(s_dump_adapters) {
    for (UINT Adapter = 0; Adapter < pD3D->GetAdapterCount(); Adapter++)
    {
      D3DADAPTER_IDENTIFIER9 Identifier;
      HRESULT Res;
      Res = pD3D->GetAdapterIdentifier(Adapter, 0, &Identifier);

      systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: adapter #" << Adapter << " (" << Identifier.Description << ")" << endl;
    }

    D3DADAPTER_IDENTIFIER9 Identifier;
    HRESULT Res;
    Res = pD3D->GetAdapterIdentifier(s_adapterToUse, 0, &Identifier);
    systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: adapter #" << s_adapterToUse << " (" << Identifier.Description << ") choosed" << endl;
  }
  DWORD dwCreateFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING/* | D3DCREATE_PUREDEVICE*/;
  if ( s_render_threadsafe )
    dwCreateFlags |= D3DCREATE_MULTITHREADED;

#ifndef NI_DISABLE_CRASHRPT
  //Report display adapter information
  D3DADAPTER_IDENTIFIER9 adapId;
  if ( SUCCEEDED( pD3D->GetAdapterIdentifier( s_adapterToUse, 0, &adapId ) ) )
  {
    UINT availableVidMem, adapterRAM;
    GetConfigManager()->GetVidMem(pD3D->GetAdapterMonitor(s_adapterToUse), &availableVidMem, &adapterRAM);

    CrashRptWrapper::AddTagToReport( "DisplayDeviceName",    adapId.DeviceName );
    CrashRptWrapper::AddTagToReport( "InstalledVideoMemory", NStr::StrFmt("%u/%u MB", availableVidMem, adapterRAM) );
    CrashRptWrapper::AddTagToReport( "DisplayDriver",        adapId.Driver );
    CrashRptWrapper::AddTagToReport( "DisplayDescription",   adapId.Description );
    CrashRptWrapper::AddTagToReport( "DisplayDriverVersion", NStr::StrFmt( "%u.%u.%u.%u",
      (unsigned)HIWORD( adapId.DriverVersion.HighPart ), (unsigned)LOWORD( adapId.DriverVersion.HighPart ),
      (unsigned)HIWORD( adapId.DriverVersion.LowPart ), (unsigned)LOWORD( adapId.DriverVersion.LowPart ) ) );
  }
#endif

  // Before creating device, test if we support given resolution and bit depth
  if(pp.Windowed == FALSE)
  {
    DWORD mode = 0, maxw = 0, maxh = 0;
    DEVMODE devmode;

    while(true)
    {
      if(EnumDisplaySettings(NULL, mode++, &devmode) == FALSE)
      {
        systemLog(NLogg::LEVEL_MESSAGE) << " Res-check indicates that requested resolution isn't supported:" << endl;
        systemLog(NLogg::LEVEL_MESSAGE) << pp.BackBufferWidth << "x" << pp.BackBufferHeight << "x32" << endl;      
        
        // Clamp if over size limit
        if(pp.BackBufferWidth > maxw || pp.BackBufferHeight > maxh)
        {
          systemLog(NLogg::LEVEL_MESSAGE) << "Clamped to: " << maxw << "x" << maxh << "x32" << endl; 

          pp.BackBufferWidth = pp.BackBufferWidth > maxw ? maxw : pp.BackBufferWidth;
          pp.BackBufferHeight = pp.BackBufferHeight > maxh ? maxh : pp.BackBufferHeight;
        }

        break;
      }
      
      maxw = devmode.dmPelsWidth > maxw ? devmode.dmPelsWidth : maxw;
      maxh = devmode.dmPelsHeight > maxh ? devmode.dmPelsHeight : maxh;

      if(devmode.dmBitsPerPel == 32 && devmode.dmPelsWidth == pp.BackBufferWidth && devmode.dmPelsHeight == pp.BackBufferHeight)
      {
        break;
      }
    }
  }

  // Try to create chosen device
  HRESULT hr = pD3D->CreateDevice(s_adapterToUse, DeviceType, static_cast<HWND>(window.p), dwCreateFlags,	&pp, &pDevice);
  bDeviceLost = (D3D_OK != hr);

  // Notify about failure
  if( FAILED(hr) ) {
    // Some of presentpapams could be rejected(e.g., triple buffer) and changed by CreateDevice. Retry.
    hr = pD3D->CreateDevice(s_adapterToUse, DeviceType, static_cast<HWND>(window.p), dwCreateFlags,	&pp, &pDevice);
    bDeviceLost = (D3D_OK != hr);

    if( FAILED(hr) ) {
      systemLog(NLogg::LEVEL_CRITICAL) << "Renderer: CreateDevice() failed, hr = " << NStr::StrFmt("0x%08X (%s)", hr, DXGetErrorStringA(hr)) << endl;
      systemLog(NLogg::LEVEL_MESSAGE) << " pp.BackBufferHeight: " << pp.BackBufferHeight << endl;
      systemLog(NLogg::LEVEL_MESSAGE) << " pp.BackBufferWidth: " << pp.BackBufferWidth << endl;
      systemLog(NLogg::LEVEL_MESSAGE) << " pp.FullScreen_RefreshRateInHz: " << pp.FullScreen_RefreshRateInHz << endl;
      systemLog(NLogg::LEVEL_MESSAGE) << " pp.hDeviceWindow: " << pp.hDeviceWindow << endl;
      systemLog(NLogg::LEVEL_MESSAGE) << " pp.Flags: " << pp.Flags << endl;
      systemLog(NLogg::LEVEL_MESSAGE) << " pp.Windowed: " << pp.Windowed << endl;
      return false;
    }
  }

  // Бэкенд пересоздаётся вместе с устройством: Reset() объект устройства
  // не меняет (см. ResetDevice()), а вот CreateDevice() меняет.
  delete pBackend;
  pBackend = new D3D9Backend(pDevice);
  bVkBackend = false;

  // Шаг 13.4c (docs/PLAN_NVRHI.md §3.z): «переворот» — pBackend становится
  // Vulkan-бэкендом. Включается только env PW_VK_BACKEND=1. D3D9-устройство при
  // этом СОХРАНЯЕТСЯ: классы lifecycle (CreateQuery/Reset/TestCooperativeLevel),
  // D3DX-загрузка DDS и прямые обращения GetDevice() (20 сайтов внутри Src/Render,
  // PostFX/ShadowMap/ImmediateRenderer) пока идут через него. Поэтому картинка на
  // этом гейте смешанная, а цель — прогнать боевые вызовы seam'а через
  // NvrhiBackend и снять счётчики (частота смен состояний, число пайплайнов,
  // владение ручками).
  {
    char vkBackendFlag[8] = { 0 };
    const bool backendOn =
      (GetEnvironmentVariableA("PW_VK_BACKEND", vkBackendFlag, sizeof(vkBackendFlag) - 1) > 0 &&
       vkBackendFlag[0] == '1');
    if (backendOn)
    {
      NvrhiBackend* vk = NvrhiBackend::Create(window, pp.BackBufferWidth, pp.BackBufferHeight);
      if (vk)
      {
        delete pBackend;
        pBackend = vk;
        bVkBackend = true;
        // Кадр рисует клиент (его Clear/Draw идут через этот бэкенд), поэтому
        // заливка бэк-буфера в Present выключается.
        vk->SetClearOnPresent(false);
        systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_BACKEND up" << endl;
      }
      else
        systemLog(NLogg::LEVEL_CRITICAL) << "Renderer: PW_VK_BACKEND FAILED" << endl;
    }
  }

  // Шаг 12a (docs/PLAN_NVRHI.md §3.z): поднимает Vulkan-бэкенд на ТОМ ЖЕ окне,
  // что и D3D9. Включается только env PW_VK_SPIKE=1 (в проде его нет, поэтому
  // блок не исполняется). Проверяется то, что нельзя проверить спайком: граф
  // сборки клиента (пофайловый /std:c++17 /EHsc, CRT-согласованность с nvrhi_vk),
  // динамический диспетчер Vulkan внутри PW_Game.exe и пригодность HWND seam'а
  // для VkSurfaceKHR. Отсутствие Vulkan не должно ронять клиент: Create()
  // возвращает 0, причина — в логе.
  // Шаг 12b/13.1: PW_VK_DIP подразумевает тот же бэкенд (тесты рисуются им);
  // значение — битовая маска тестов (1 геометрия, 2 текстура), см. Present.
  delete pVkSpike;
  pVkSpike = 0;
  {
    char vkSpikeFlag[8] = { 0 };
    char vkDipFlag[8] = { 0 };
    const bool spikeOn = (GetEnvironmentVariableA("PW_VK_SPIKE", vkSpikeFlag, sizeof(vkSpikeFlag) - 1) > 0 &&
                          vkSpikeFlag[0] == '1');
    const bool dipOn = (GetEnvironmentVariableA("PW_VK_DIP", vkDipFlag, sizeof(vkDipFlag) - 1) > 0 &&
                        atoi(vkDipFlag) != 0);
    if ((spikeOn || dipOn) && !bVkBackend)
    {
      pVkSpike = NvrhiBackend::Create(window, pp.BackBufferWidth, pp.BackBufferHeight);
      systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_SPIKE "
                                     << (pVkSpike ? "up" : "FAILED")
                                     << (dipOn ? " (dip requested)" : "") << endl;
    }
  }

  GetConfigManager()->CheckDeviceCaps(pDevice);
#ifndef NI_DISABLE_CRASHRPT
  CrashRptWrapper::AddTagToReport( "DXcapsOEM",  NStr::StrFmt("%#X", GetConfigManager()->GetAllFeatures()) );
  CrashRptWrapper::AddTagToReport( "DXcapsOEM_version",  NStr::StrFmt("%d", featuresVersion) );
#endif // NI_DISABLE_CRASHRPT
  //systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: CreateDevice() successful." << endl;
  presentParams = pp;
  OnDeviceChanged();
  
  DeviceDeleteHandler::NotifyDeviceCreate();
  
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::ResetDevice(D3DPRESENT_PARAMETERS& pp)
{
  DebugTrace("Try to reset");

  // Смена размера/формата кадра = ре-аллокация: всё, что привязано к бэк-буферу
  // (в D3D9 — DEFAULT-пул), обязано быть отпущено до Reset() и пересоздано после.
  // В текущем клиенте этот путь вызывается только при повторном Start() (смены
  // разрешения на лету нет), поэтому раньше уведомления обработчиков здесь
  // отсутствовали; ресайз из SDL на этапе 3 сделает путь живым, и корректным он
  // обязан стать заранее (docs/PLAN_NVRHI.md §2.3, §3.x GAP: swapchain).
  NotifyResourcesRelease();
  bDeviceLost = true;

  pDeviceDepthSurface = 0;
  pDeviceColorSurface = 0;
  pFrameSynchQuery = 0;

  // Try to create chosen device
  DebugTrace("Before device reset");
  HRESULT hr = pDevice->Reset(&pp);
  DebugTrace("After device reset");
  
  // Notify about failure
  if (FAILED(hr))
  {
    systemLog(NLogg::LEVEL_CRITICAL) << "Renderer: ResetDevice() failed, hr = " << NStr::StrFmt("0x%08X (%s)", hr, DXGetErrorStringA(hr)) << endl;
    // остаёмся в состоянии «ресурсы отпущены»: его разберёт либо повторный
    // Start(), либо RecoverFromDeviceLost() на ближайшем Present()
    return false;
  }

  //systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: ResetDevice() successful." << endl;
  presentParams = pp;
  OnDeviceChanged();
  bDeviceLost = false;
  NotifyResourcesReallocate();
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::Start(const RenderMode& _renderMode)
{
  // prepare mode parameters
  D3DPRESENT_PARAMETERS pp; 
  Convert(_renderMode, pp);
  
  // create or reset device
  bool ok = (pDevice == 0) ? CreateDevice(pp) : ResetDevice(pp);
  if (!ok)
    return false;

	NLogg::CAssertDumper::EnableAssertMessageBox(!_renderMode.isFullScreen && !isRunningUnderPerfHUD);
	UpdateRealRenderMode(_renderMode);

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::Stop()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::FillCaps()
{
	D3DCAPS9 d3dCaps;
	if ( FAILED(GetDevice()->GetDeviceCaps(&d3dCaps)) )
	{
		caps.pixelShaderVersion = 0.0f;
		caps.vertexShaderVersion = 0.0f;
		// Консервативные границы для ClearDeviceResources: без caps перебирать
		// максимумы нечем.
		caps.numSimultaneousRTs = 1;
		caps.maxStreams = 256;
	}
	else
	{
		caps.numSimultaneousRTs = d3dCaps.NumSimultaneousRTs;
		caps.maxStreams = d3dCaps.MaxStreams;
	}

	// SM2.0-фолбэк удалён (вместе с .shd2 и FORCE_SM20 в шейдерах):
	// гейт запуска CheckHardwareCompatibility() всё равно не пускает
	// железо без SM3.0, а под Vulkan/DX12 минимальный профиль — SM5+.
	caps.vertexShaderVersion = float((d3dCaps.VertexShaderVersion >> 8) & 0xFF) +
	                           float(d3dCaps.VertexShaderVersion & 0xFF) * 0.1f;
	caps.pixelShaderVersion = float((d3dCaps.PixelShaderVersion >> 8) & 0xFF) +
	                          float(d3dCaps.PixelShaderVersion & 0xFF) * 0.1f;

	caps.bSupportSM30 = caps.pixelShaderVersion >= 3.0f && caps.vertexShaderVersion >= 3.0f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DXVertexDeclarationRef Renderer::CreateVertexFormatDeclaration(const VertexFormatDescriptor& descr)
{
  NI_ASSERT(descr.GetVertexElementsCount() > 0, "Invalid vertex format!");

  // Перевод описания формата в D3DVERTEXELEMENT9 — внутри бэкенда
  // (docs/PLAN_NVRHI.md §2.8): на этом месте у Vulkan будет VkVertexInputState.
  IDirect3DVertexDeclaration9* decl_d3d = 0;

  if(!RENDER_DISABLED) {
    HInputLayout layout;
    HRESULT hr = pBackend->CreateInputLayout(descr, &layout);
    NI_DX_THROW( hr, "CreateVertexFormatDeclaration" );
    decl_d3d = static_cast<IDirect3DVertexDeclaration9*>(layout.p);
  }

  return DXVertexDeclarationRef(decl_d3d, false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DXVertexShaderRef Renderer::CreateVertexShader(unsigned char const* data)
{
	return CreateVertexShader(data, 0);
}

DXVertexShaderRef Renderer::CreateVertexShader(unsigned char const* data, unsigned size)
{
	IDirect3DVertexShader9* d3dshader = 0;
  HShader shader;
	HRESULT hr = pBackend->CreateVertexShader(data, size, &shader);
	NI_DX_THROW(hr, "Creating vertex shader error!");
  d3dshader = static_cast<IDirect3DVertexShader9*>(shader.p);
	return DXVertexShaderRef(d3dshader, false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DXPixelShaderRef Renderer::CreatePixelShader(unsigned char const* data)
{
	return CreatePixelShader(data, 0);
}

DXPixelShaderRef Renderer::CreatePixelShader(unsigned char const* data, unsigned size)
{
	IDirect3DPixelShader9* d3dshader = 0;
  HShader shader;
	HRESULT hr = pBackend->CreatePixelShader(data, size, &shader);
	NI_DX_THROW(hr, "Creating pixel shader error!");
  d3dshader = static_cast<IDirect3DPixelShader9*>(shader.p);
	return DXPixelShaderRef(d3dshader, false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::SetClipPlane(bool bEnable, CVec4 *pPlane)
{
	pBackend->SetClipPlane(bEnable, (float*)pPlane);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::SetPixelShaderConstants(unsigned int startRegisterIndex, unsigned int registersCount, const void* data)
{
	NI_VERIFY(startRegisterIndex < SHADER_REGISTERS_COUNT, "Invalid register index!", return; );
	NI_VERIFY(SHADER_REGISTERS_COUNT - startRegisterIndex >= registersCount, "Invalid registers count!", return; );

#ifdef _DEBUG
	constChecker.SetPixelShaderConstants(startRegisterIndex, registersCount);
#endif

  // Тень регистров (docs/PLAN_NVRHI.md §2.7): зеркало того, что ушло в
  // устройство. Порядок не важен: для D3D9 источником состояния остаётся сама
  // запись в устройство, а тень нужна этапу 3 (uniform-буфер) и замеру раскладки.
  GetConstStore().Stage( csPixel ).Write( startRegisterIndex, registersCount, static_cast<const float*>(data) );
  bool res = pBackend->SetPixelShaderConstants(startRegisterIndex, registersCount, data) == D3D_OK;

	NI_ASSERT(res, "SetPixelShaderConstants error!");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::SetVertexShaderConstants(unsigned int startRegisterIndex, unsigned int registersCount, const void* data)
{
	NI_VERIFY(startRegisterIndex < SHADER_REGISTERS_COUNT, "Invalid register index!", return; );
	NI_VERIFY(SHADER_REGISTERS_COUNT - startRegisterIndex >= registersCount, "Invalid registers count!", return; )

#ifdef _DEBUG
		constChecker.SetVertexShaderConstants(startRegisterIndex, registersCount);
#endif

	GetConstStore().Stage( csVertex ).Write( startRegisterIndex, registersCount, static_cast<const float*>(data) );
	bool res = pBackend->SetVertexShaderConstants(startRegisterIndex, registersCount, data) == D3D_OK;

	NI_ASSERT(res, "SetPixelShaderConstants error!");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::BeginScene()
{
  if ( s_render_threadsafe )
  {
    renderSceneLock.Lock();
    renderSceneLocked = true;
  }

	HRESULT hr = pBackend->BeginScene();
  NI_DX_WARN(hr, "BeginScene");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::EndScene()
{
  HRESULT hr = pBackend->EndScene();

  if ( renderSceneLocked )
  {
    renderSceneLocked = false;
    renderSceneLock.Unlock();
  }
  NI_DX_WARN(hr, "EndScene");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::Present()
{
  Present(window, NULL, NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Renderer::Synchronize()
{
  NI_PROFILE_FUNCTION

  HRESULT issueResult = pFrameSynchQuery->Issue(D3DISSUE_END);
  RenderResourceManager::FrameTick();

  if ( issueResult != D3DERR_DEVICELOST )
  {
    HRESULT queryResult = pFrameSynchQuery->GetData( NULL, 0, D3DGETDATA_FLUSH );

    if ( queryResult == S_FALSE )
    {
      for(int i = 1000; --i > 0;)
      {
        queryResult = pFrameSynchQuery->GetData( NULL, 0, D3DGETDATA_FLUSH);
        
        if(queryResult != S_FALSE) {
          if( FAILED(queryResult) ) {
            //recreate query
            IDirect3DQuery9* pQuery;
            pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pQuery);
            pFrameSynchQuery.Attach(pQuery);
          }
          break;
        }

        //   - 

        Sleep(1);
      }

      bResetDevice = (queryResult == S_FALSE);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::ClearDeviceResources()
{
  // Отвязка всего, что было привязано к устройству — через seam (нулевые ручки
  // = unbind). Границы перебора берутся из кэша caps (FillCaps при смене
  // устройства): раньше здесь каждый раз дёргался GetDeviceCaps.
  IRenderBackend* const pB = GetBackend();
  HRESULT hr = S_OK;
  hr = pB->SetInputLayout(HInputLayout());
  hr = pB->SetVertexShader(HShader());
  hr = pB->SetPixelShader(HShader());

  static const int MAX_SAMPLERS_NUMBER = 16; // for ps.3.0
  for(int i = 0; i < MAX_SAMPLERS_NUMBER; ++i)
    hr = pB->SetTexture(i, HTexture());

  for(unsigned n = 0; n < caps.numSimultaneousRTs; ++n)
    hr = pB->SetRenderTarget(n, HSurface());

  // Инстансный путь выключается до отвязки потоков: иначе отображение
  // «логический поток -> физический слот» со сдвигом, и перебор ниже не
  // накрывает последний слот (даёт INVALIDCALL на несуществующем слоте).
  hr = pB->SetInstanceCount(0);

  for(unsigned n = 0; n < caps.maxStreams; ++n)
    hr = pB->SetStreamSource(n, HBuffer(), 0, 4);

  // Состояние, которое бэкенд кэширует для ленивой переустановки (частоты
  // потоков инстансинга), после сброса устройства недействительно.
  pB->InvalidateBoundState();
  
  pDeviceDepthSurface = 0;
  pDeviceColorSurface = 0;
  pFrameSynchQuery = 0;
  
  //SmartRenderer::NullThePointers();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::Present( NativeWindow window_, const Rect* sourceRect_, const Rect* destRect_ )
{
  NI_PROFILE_FUNCTION

  {
    NI_PROFILE_BLOCK( "Present prepare" )

    SmartRenderer::ResetTriangleAndDipCount();

    if (sourceRect_)
      sourceRect = *sourceRect_;
  }

  if(!bTripleBufferUsed && pFrameSynchQuery && g_synchronizeRender )
    Synchronize();
  else
    RenderResourceManager::FrameTick();

  HRESULT hr = 0;

  {
    NI_PROFILE_BLOCK( "Present itself" )

    //HRESULT hr = GetDevice()->Present((const RECT*)sourceRect_, NULL, hWnd_, NULL);
    // Render::Rect -> BackendRect (раньше здесь был reinterpret_cast: типы
    // совпадали по размеру, но seam не должен был знать про Win32).
    BackendRect srcRect, dstRect;
    if(sourceRect_)
      srcRect = MakeBackendRect((long)sourceRect_->left, (long)sourceRect_->top,
                                (long)sourceRect_->right, (long)sourceRect_->bottom);
    if(destRect_)
      dstRect = MakeBackendRect((long)destRect_->left, (long)destRect_->top,
                                (long)destRect_->right, (long)destRect_->bottom);
    hr = pBackend->Present(window_, sourceRect_ ? &srcRect : 0, destRect_ ? &dstRect : 0);

    // Шаг 12a: кадр Vulkan-бэкенда идёт ПОСЛЕ D3D9-вывода — по цвету заливки в
    // скриншоте видно, чей вывод лёг последним. Его результат в hr не попадает:
    // потеря D3D-устройства определяется ниже по hr именно D3D-вызова.
    if (pVkSpike)
    {
      pVkSpike->Present(window_, sourceRect_ ? &srcRect : 0, destRect_ ? &dstRect : 0);
      static unsigned s_vkSpikeFrames = 0;
      ++s_vkSpikeFrames;
      if ((s_vkSpikeFrames % 300) == 0)
        pVkSpike->DumpStats("frame");

      // Шаг 12b/13.1: ровно один раз, на 30-м кадре (паки уже открыты, swapchain
      // после первого кадра пересоздан под размер окна) — настоящий DIP через
      // seam: .shd v2 -> SPIR-V -> пайплайн -> вывод -> чтение пикселей.
      // PW_VK_DIP — битовая маска: 1 = геометрия/константы (WaterDummyMaterial),
      // 2 = текстура и сэмплер (SkyMaterial), 3 = оба теста.
      static int s_vkDipState = -1;   // -1 не решено, 0 не просили, 1 ждёт, 2 выполнено
      static unsigned s_vkDipMask = 0;
      if (s_vkDipState < 0)
      {
        char dipFlag[8] = { 0 };
        if (GetEnvironmentVariableA("PW_VK_DIP", dipFlag, sizeof(dipFlag) - 1) > 0)
          s_vkDipMask = (unsigned)atoi(dipFlag);
        s_vkDipState = s_vkDipMask ? 1 : 0;
      }
      if (s_vkDipState == 1 && s_vkSpikeFrames >= 30)
      {
        s_vkDipState = 2;
        if (s_vkDipMask & 1u)
          systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_DIP "
                                         << (pVkSpike->RunSeamDipTest() ? "OK" : "FAILED") << endl;
        if (s_vkDipMask & 2u)
          systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_DIP_TEX "
                                         << (pVkSpike->RunTextureDipTest() ? "OK" : "FAILED") << endl;
        if (s_vkDipMask & 4u)
          systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_DIP_SURF "
                                         << (pVkSpike->RunSurfaceOpsTest() ? "OK" : "FAILED") << endl;
      }
    }

    // Шаг 13.4c: «переворот» (env PW_VK_BACKEND=1) — pBackend и есть
    // Vulkan-бэкенд, поэтому отдельный present выше не нужен (основной путь уже
    // вызывает pBackend->Present). Остаются тесты seam'а, снимок кадра
    // (env PW_VK_SHOT=<номер кадра>) и счётчики — они и есть цель гейта:
    // частота смен состояний, число пайплайнов, владение ручками в бою.
    if (bVkBackend)
    {
      NvrhiBackend* vk = static_cast<NvrhiBackend*>(pBackend);
      static unsigned s_vkFlipFrames = 0;
      static bool   s_vkFlipFlagsRead = false;
      static unsigned s_vkFlipDipMask = 0;
      static unsigned s_vkShotAt = 0;
      ++s_vkFlipFrames;
      if (!s_vkFlipFlagsRead)
      {
        char dipFlag[8] = { 0 };
        char shotFlag[16] = { 0 };
        if (GetEnvironmentVariableA("PW_VK_DIP", dipFlag, sizeof(dipFlag) - 1) > 0)
          s_vkFlipDipMask = (unsigned)atoi(dipFlag);
        if (GetEnvironmentVariableA("PW_VK_SHOT", shotFlag, sizeof(shotFlag) - 1) > 0)
          s_vkShotAt = (unsigned)atoi(shotFlag);
        s_vkFlipFlagsRead = true;
      }
      if (s_vkFlipDipMask && s_vkFlipFrames == 30u)
      {
        if (s_vkFlipDipMask & 1u)
          systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_DIP(flip) "
            << (vk->RunSeamDipTest() ? "OK" : "FAILED") << endl;
        if (s_vkFlipDipMask & 2u)
          systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_DIP_TEX(flip) "
            << (vk->RunTextureDipTest() ? "OK" : "FAILED") << endl;
        if (s_vkFlipDipMask & 4u)
          systemLog(NLogg::LEVEL_MESSAGE) << "Renderer: PW_VK_DIP_SURF(flip) "
            << (vk->RunSurfaceOpsTest() ? "OK" : "FAILED") << endl;
      }
      // снимок кадра переехал в NvrhiBackend::Present: здесь он читал бы
      // картинку уже следующего acquire'а (acquire в Present — последний шаг)
      if ((s_vkFlipFrames % 300) == 0)
        vk->DumpStats("flip");
    }
    
    NI_DX_WARN( hr == D3D_OK || hr == D3DERR_DEVICELOST, "" );
  }

  if(hr == D3DERR_DEVICELOST || bResetDevice) {
    RecoverFromDeviceLost(presentParams);
    bResetDevice = false;
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::InvalidateResources()
{
  ClearDeviceResources();
  NotifyResourcesRelease();
  bDeviceLost = true;
  pBackend->EvictManagedResources();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   
void Renderer::RecoverFromDeviceLost( D3DPRESENT_PARAMETERS& pp )
{
  NI_PROFILE_BLOCK( "Device lost" )

    // report failure
    if (!bDeviceLost)
    {
      bDeviceLost = true;
      NotifyResourcesRelease();
    }

    const HRESULT coopLevel = pDevice->TestCooperativeLevel();
    // try to revive the device
    if (coopLevel == D3DERR_DEVICENOTRESET || coopLevel == D3D_OK )
    {
      DebugTrace("Device lost!");
      ClearDeviceResources();
      NotifyResourcesRelease();
      DumpDXResourcesInDefaultPool();

      for( int n = 30; bDeviceLost; --n )       
        if( ResetDevice(pp) )
          bDeviceLost = false;   
        else
          NotifyResourcesRelease();     

      if(bDeviceLost) 
      {
        NI_ALWAYS_ASSERT("Reset() failed on lost device! Exiting app.");
        exit(D3DERR_DEVICENOTRESET);
      }
      
      NotifyResourcesReallocate();
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::ClearDepthStencil()
{
  HRESULT hr = pBackend->Clear(bcZBuffer | bcStencil, 0, 1.0f, 0);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "ClearDepthStencil");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::Clear( Color color )
{
  HRESULT hr = pBackend->Clear(bcTarget | bcZBuffer | bcStencil, color.Dummy, 1.0f, 0);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "Clear");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::ClearColorOnly( Color color )
{
	HRESULT hr = pBackend->Clear(bcTarget, color.Dummy, 1.0f, 0);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "ClearColorOnly");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::GetRenderTargetData(IDirect3DSurface9* pSrc, IDirect3DSurface9* pDst) 
{
	HRESULT hr = pBackend->GetRenderTargetData(MakeBackendHandle<HSurface>(pSrc), MakeBackendHandle<HSurface>(pDst));
  NI_DX_THROW(hr, __FUNCTION__);

	return (hr == D3D_OK);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::StretchSurface(const DXSurfaceRef &pSrc, const DXSurfaceRef &pDst, bool bLinear)
{
  HRESULT hr = pBackend->StretchRect(MakeBackendHandle<HSurface>(::Get(pSrc)), 0, MakeBackendHandle<HSurface>(::Get(pDst)), 0,
                                     bLinear ? IRenderBackend::bfLinear : IRenderBackend::bfPoint);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "StretchSurface");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::GetViewport(D3DVIEWPORT9 &vp)
{
  BackendViewport bvp;
  pBackend->GetViewport(bvp);
  vp.X = bvp.x;
  vp.Y = bvp.y;
  vp.Width = bvp.width;
  vp.Height = bvp.height;
  vp.MinZ = bvp.minZ;
  vp.MaxZ = bvp.maxZ;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::SetScissorRect(const BackendRect *pRect)
{
  // Прямоугольник пришёл уже нейтральным — конверсии нет, RECT живёт только
  // у вызывающего кода.
  pBackend->SetScissorRect(pRect);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::ColorFillSurface(IDirect3DSurface9 *pSurface, DWORD color)
{
  if(!pSurface)
    return;
  HRESULT hr = pBackend->ColorFill(MakeBackendHandle<HSurface>(pSurface), color);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "ColorFillSurface");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::StretchSurfaceRect(IDirect3DSurface9 *pSrc, const RECT *pSrcRect,
                                  IDirect3DSurface9 *pDst, const RECT *pDstRect, D3DTEXTUREFILTERTYPE filter)
{
  // NULL-прямоугольник в D3D9 означает «вся поверхность» — подменять его нулевым
  // нельзя (получился бы пустой copy), поэтому null проходит сквозь seam как null.
  BackendRect srcRect, dstRect;
  const BackendRect* pRectSrc = 0;
  const BackendRect* pRectDst = 0;
  if(pSrcRect)
  {
    srcRect = MakeBackendRect(pSrcRect->left, pSrcRect->top, pSrcRect->right, pSrcRect->bottom);
    pRectSrc = &srcRect;
  }
  if(pDstRect)
  {
    dstRect = MakeBackendRect(pDstRect->left, pDstRect->top, pDstRect->right, pDstRect->bottom);
    pRectDst = &dstRect;
  }
  HRESULT hr = pBackend->StretchRect(MakeBackendHandle<HSurface>(pSrc), pRectSrc,
                                     MakeBackendHandle<HSurface>(pDst), pRectDst,
                                     filter == D3DTEXF_LINEAR ? IRenderBackend::bfLinear : IRenderBackend::bfPoint);
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "StretchSurfaceRect");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::EvictManagedResources()
{
  pBackend->EvictManagedResources();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int Renderer::GetAvailableVidMem()
{
  ConfigManager * const pCM = GetConfigManager();
  if(!pCM)
    return -1;

  IDirect3D9 *pD3D = 0;
  if(FAILED(GetDevice()->GetDirect3D(&pD3D)))
    return -1;
  IntrusivePtr<IDirect3D9> pD3D_holder(pD3D, false);

  D3DDEVICE_CREATION_PARAMETERS devParams;
  if(FAILED(GetDevice()->GetCreationParameters(&devParams)))
    return -1;

  UINT memSize = 0, memSizeTotal = 0;
  pCM->GetVidMem(pD3D->GetAdapterMonitor(devParams.AdapterOrdinal), &memSize, &memSizeTotal);
  return (int)memSize;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::SaveScreenshotToFile(const char *fileName, int x, int y, int width, int height, bool needCrop)
{
  BackendRect rect = MakeBackendRect(x, y, x + width, y + height);
  // Источник — бэк-буфер (как было), альфа отбрасывалась и раньше: D3DX-путь
  // шёл через X8R8G8B8.
  return SaveFrameToFile(fileName, /*keepAlpha=*/false, needCrop ? &rect : 0, /*useBackBuffer=*/true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Renderer::SaveFrameToFile(const char* fileName, bool keepAlpha, const BackendRect* pCropRect,
                               bool useBackBuffer)
{
  if(!pBackend || !fileName)
    return false;

  HSurface src;
  if(useBackBuffer) {
    if(FAILED(pBackend->GetBackBuffer(&src)))
      return false;
  } else {
    if(!pDeviceColorSurface)
      return false;
    src = MakeBackendHandle<HSurface>(::Get(pDeviceColorSurface));   // :: — иначе видится Renderer::Get();
  }

  // Размеры источника даёт бэкенд (шаг 13.2): у Vulkan-поверхности нет COM-
  // метода GetDesc, а снимок берётся и с бэк-буфера, и с RT.
  unsigned w = 0, h = 0;
  IRenderBackend::BackendTextureInfo info;
  if(FAILED(pBackend->GetSurfaceInfo(src, &info)))
    return false;
  w = info.width;
  h = info.height;

  unsigned cw = w, ch = h;
  BackendRect clamped = MakeBackendRect(0, 0, (long)w, (long)h);
  const BackendRect* pSrcRect = 0;
  if(pCropRect) {
    const LONG x0 = pCropRect->left  < 0 ? 0 : pCropRect->left;
    const LONG y0 = pCropRect->top   < 0 ? 0 : pCropRect->top;
    const LONG x1 = pCropRect->right > (LONG)w ? (LONG)w : pCropRect->right;
    const LONG y1 = pCropRect->bottom > (LONG)h ? (LONG)h : pCropRect->bottom;
    if(x1 <= x0 || y1 <= y0)
      return false;
    cw = (unsigned)(x1 - x0);
    ch = (unsigned)(y1 - y0);
    // Бэкенду отдаём УЖЕ ограниченный прямоугольник: ReadSurface выход за
    // поверхности отвергает (иначе он молча прочитал бы чужую память), а
    // ограничивать здесь и просить там исходный — значит гарантированно
    // получить отказ на частично выходящем кропе.
    clamped.left = x0; clamped.top = y0; clamped.right = x1; clamped.bottom = y1;
    pSrcRect = &clamped;
  }

  std::vector<unsigned char> pixels((size_t)ch * cw * 4);
  EBackendPixelLayout layout = plBGRA;
  if(FAILED(pBackend->ReadSurface(src, pSrcRect, &pixels[0], cw * 4, &layout)))
    return false;

  return WriteScreenshotFile(fileName, ScreenshotFormatFromName(fileName),
                             &pixels[0], cw, ch, cw * 4, layout, keepAlpha);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::Convert( const RenderMode &_renderMode, D3DPRESENT_PARAMETERS &pp )
{
  ZeroMemory(&pp, sizeof(pp));
  pp.BackBufferWidth = _renderMode.width;
  pp.BackBufferHeight = _renderMode.height;
  pp.Windowed = 1;
  pp.FullScreen_RefreshRateInHz = _renderMode.refreshRate;
  pp.EnableAutoDepthStencil = true;
  pp.AutoDepthStencilFormat = D3DFMT_D24S8;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = D3DFMT_A8R8G8B8;
  pp.hDeviceWindow = static_cast<HWND>(window.p);

	pp.BackBufferCount = 1;

  bTripleBufferUsed = s_use_triple_buffer && _renderMode.vsyncCount;
  if(bTripleBufferUsed)
    pp.BackBufferCount = 2;

  switch (_renderMode.vsyncCount)
  {                                   
    case 0: pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; break;
    case 1: pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; break;
    case 2: pp.PresentationInterval = D3DPRESENT_INTERVAL_TWO; break;
    case 3: pp.PresentationInterval = D3DPRESENT_INTERVAL_THREE; break;
    case 4: pp.PresentationInterval = D3DPRESENT_INTERVAL_FOUR; break;
    default: pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT; break;
  }
  //pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  //if( !pp.Windowed )
  //{
  //  CorrectResolution( pp.BackBufferWidth, pp.BackBufferHeight );
  //  CorrectRefreshRate( pp.BackBufferWidth, pp.BackBufferHeight, pp.FullScreen_RefreshRateInHz );
  //}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::CorrectRenderMode( RenderMode &_renderMode ) const
{
  if( _renderMode.isFullScreen )
  {
    //CorrectResolution( _renderMode.width, _renderMode.height );
    //CorrectRefreshRate( _renderMode.width, _renderMode.height, _renderMode.refreshRate );
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Renderer::ApplyNewParams( const RenderMode &_renderMode )
{ 
  D3DPRESENT_PARAMETERS pp;
  Convert(_renderMode, pp);
  
  RecoverFromDeviceLost(pp);
  DebugTrace( "RecoverFromDeviceLost finished" ); 
  UpdateRealRenderMode(_renderMode);
}

} // namespace Render
