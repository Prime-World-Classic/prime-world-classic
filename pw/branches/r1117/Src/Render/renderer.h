#pragma once

#if defined(PW_LINUX_NULL_RENDER)

#include "../System/config.h"
#include "../System/Geom.h"
#include "../System/Color.h"
#include "../System/matrix43.h"
#include "../System/nvector.h"
#include "Rect.h"
#include "light.h"
#include "rendermode.h"
#include "vertexformatdescriptor.h"
#include "dipdescriptor.h"
#include "GlobalMasks.h"
#include "ConstantProtection.h"
#include "renderstatesmanager.h"

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DSurface9;

enum D3DSWAPEFFECT
{
  D3DSWAPEFFECT_DISCARD = 1,
  D3DSWAPEFFECT_FLIP = 2,
  D3DSWAPEFFECT_COPY = 3
};

namespace Render
{

class Texture2D;

class LightsManager
{
  vector<PointLight*> lights;
  Render::AABB bounds;
  NDb::PointLightFlags lightsDataFilterFlags;

public:
  LightsManager()
    : lights()
    , bounds()
    , lightsDataFilterFlags(NDb::POINTLIGHTFLAGS_NONE)
  {
  }

  ~LightsManager()
  {
    DeleteAllLights();
  }

  void SetBounds(Render::AABB const& aabb)
  {
    bounds = aabb;
  }

  PointLight* CreateLight()
  {
    PointLight* light = new PointLight();
    lights.push_back(light);
    return light;
  }

  PointLight* CreateLight(NDb::PointLightInfo const& lightInfo)
  {
    PointLight* light = CreateLight();
    light->m_attenuationType = lightInfo.attenuation;
    light->SetLocation(lightInfo.location);
    light->SetRange(lightInfo.range);
    light->m_diffuseColor = lightInfo.diffuse;
    light->m_diffuseIntensity = lightInfo.diffuseIntensity;
    light->m_flags = lightInfo.flags;
    light->m_lastUsedAABB = light->GetWorldAABB();
    return light;
  }

  void CreateLights(vector<NDb::PointLightInfo> const& lightInfos)
  {
    for (vector<NDb::PointLightInfo>::const_iterator it = lightInfos.begin(); it != lightInfos.end(); ++it)
    {
      CreateLight(*it);
    }
  }

  void UpdateLight(PointLight* light)
  {
    if (light)
    {
      light->m_lastUsedAABB = light->GetWorldAABB();
    }
  }

  void DeleteLight(PointLight* light)
  {
    if (!light)
    {
      return;
    }

    for (vector<PointLight*>::iterator it = lights.begin(); it != lights.end(); ++it)
    {
      if (*it == light)
      {
        lights.erase(it);
        break;
      }
    }

    delete light;
  }

  void DeleteAllLights()
  {
    for (vector<PointLight*>::iterator it = lights.begin(); it != lights.end(); ++it)
    {
      delete *it;
    }
    lights.clear();
  }

  template <class FUNC> void ForLightsAffectingBBox(Render::AABB const& aabb, FUNC& f) const
  {
    for (vector<PointLight*>::const_iterator it = lights.begin(); it != lights.end(); ++it)
    {
      PointLight* light = *it;
      if (light && aabb.IsIntersectedBy(light->GetWorldAABB()))
      {
        f(*light);
      }
    }
  }

  void SetLightsDataFilterFlags(const NDb::PointLightFlags flags) { lightsDataFilterFlags = flags; }
  NDb::PointLightFlags GetLightsDataFilterFlags() const { return lightsDataFilterFlags; }
};

struct Caps
{
  float pixelShaderVersion;
  float vertexShaderVersion;
  bool bSupportSM30;
};

class Renderer
{
  static Renderer* s_pInstance;

  RenderStatesManager statesMan;
  LightsManager lightsMan;
  RenderMode realRenderMode;
  bool bDeviceLost;
  bool bResetDevice;
  bool bTripleBufferUsed;
  bool isRunningUnderPerfHUD;
  HWND hWnd;
  D3DSWAPEFFECT swapEffect;
  DXSurfacePtr pDeviceDepthSurface;
  DXSurfacePtr pDeviceColorSurface;
  Caps caps;
  Rect sourceRect;
  ConstantProtectionChecker constChecker;

public:
  explicit Renderer(unsigned int _hWnd);
  ~Renderer();

  static void Init(unsigned int _hWnd)
  {
    s_pInstance = new Renderer(_hWnd);
  }

  static Renderer* Get();

  static void Term()
  {
    delete s_pInstance;
    s_pInstance = 0;
  }

  const RenderMode& GetCurrentRenderMode();

  void SetHWND(HWND hwnd)
  {
    hWnd = hwnd;
  }

  D3DSWAPEFFECT GetSwapEffectParams() const { return swapEffect; }

  int GetModesCount() const;
  void GetMode(int index, unsigned int &width, unsigned int &height, unsigned int &refreshRate) const;

  bool Start(const RenderMode& _renderMode);

  void Stop();
  const Caps& GetCaps() const { return caps; }

  DXVertexShaderRef CreateVertexShader(unsigned char const* data);
  DXPixelShaderRef CreatePixelShader(unsigned char const* data);

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
    SetVertexShaderConstants(startRegisterIndex, 3 * matrixCount, pMatrix43);
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
  void Present(HWND hWnd_, const Rect* sourceRect_, const Rect* destRect_);

  const DXSurfacePtr& GetDepthStencilSurface() { return pDeviceDepthSurface; }
  const DXSurfacePtr& GetColorSurface() { return pDeviceColorSurface; }

  void ClearDepthStencil();
  void Clear(Color color);
  void ClearColorOnly(Color color);

  const Rect& GetSourceRect() const { return sourceRect; }

  bool GetRenderTargetData(IDirect3DSurface9* pSrc, IDirect3DSurface9 *pDst);
  void StretchSurface(const DXSurfaceRef &pSrc, const DXSurfaceRef &pDst, bool bLinear = false);

  RenderStatesManager* GetRenderStatesManager() { return &statesMan; }
  LightsManager* GetLightsManager() { return &lightsMan; }

  IDirect3DDevice9* GetDevice() { return 0; }
  IDirect3D9* GetD3D() { return 0; }

  bool IsRunningUnderPerfHUD() const { return isRunningUnderPerfHUD; }

  bool DeviceIsLost() const { return bDeviceLost; }

  void ApplyNewParams(const RenderMode &_renderMode);

  bool NeedToFilterResolutionFromUser(unsigned int width, unsigned int height) const;

  void CorrectRenderMode(RenderMode &_renderMode) const;

  void InvalidateResources();

private:
  void UpdateRealRenderMode(const RenderMode &configRenderMode);
};

inline Renderer* GetRenderer() { return Renderer::Get(); }

inline IDirect3DDevice9* GetDevice()
{
  return Renderer::Get() ? Renderer::Get()->GetDevice() : 0;
}

inline RenderStatesManager* GetStatesManager()
{
  return Renderer::Get() ? Renderer::Get()->GetRenderStatesManager() : 0;
}

inline LightsManager* GetLightsManager()
{
  return Renderer::Get() ? Renderer::Get()->GetLightsManager() : 0;
}

struct ConstantProtectionBlock
{
  ConstantProtectionBlock(ContextType context) { GetRenderer()->BeginConstantProtectionBlock(context); }
  ~ConstantProtectionBlock() { GetRenderer()->EndConstantProtectionBlock(); }
};

}; // namespace Render

#else

#include "rendermode.h"
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
};

class Renderer
{
	static Renderer*      s_pInstance;

	DeviceLostWrapper<RenderStatesManager> statesMan;
  LightsManager         lightsMan;

	RenderMode            realRenderMode;
	IDirect3D9*           pD3D;
	IDirect3DDevice9*     pDevice;
  bool                  bDeviceLost;
  bool                  bResetDevice;
  bool                  bTripleBufferUsed;
  bool                  isRunningUnderPerfHUD;
	unsigned int          hWnd;
	D3DPRESENT_PARAMETERS presentParams;
	DXSurfacePtr          pDeviceDepthSurface;
	DXSurfacePtr          pDeviceColorSurface;
	Caps                  caps;
  DXQueryRef            pFrameSynchQuery;
  threading::Mutex      renderSceneLock;
  bool                  renderSceneLocked;

	//vector<Texture2D*>   textures;
	Rect                  sourceRect;

	//nstl::vector<Texture2D*> surfaces;

	ConstantProtectionChecker constChecker;

	void FillCaps();

public:
	explicit Renderer(unsigned int _hWnd);
	 ~Renderer();

	static void Init(unsigned int _hWnd)
	{
	  s_pInstance = new Renderer(_hWnd);
	}

	static Renderer* Get();// { return s_pInstance; }

  static void Term()
	{
		delete s_pInstance;
    s_pInstance = 0;
	}

	const RenderMode& GetCurrentRenderMode();

  void SetHWND( HWND hwnd)
  {
    presentParams.hDeviceWindow = hwnd;
  }
  
  D3DSWAPEFFECT GetSwapEffectParams() const { return presentParams.SwapEffect; }

	int GetModesCount() const;
	void GetMode(int index, unsigned int &width, unsigned int &height, unsigned int &refreshRate) const;

	bool Start(const RenderMode& _renderMode);

  void Stop();
	const Caps & GetCaps() const { return caps; }

	DXVertexShaderRef CreateVertexShader(unsigned char const* data);
	DXPixelShaderRef CreatePixelShader(unsigned char const* data);

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
  void Present( HWND hWnd_, const Rect* sourceRect_, const Rect* destRect_ );

	const DXSurfacePtr& GetDepthStencilSurface() { return pDeviceDepthSurface; }
	const DXSurfacePtr& GetColorSurface() { return pDeviceColorSurface; }

  void ClearDepthStencil();
	void Clear(Color color);
	void ClearColorOnly(Color color);

	const Rect &GetSourceRect() const { return sourceRect; }

	bool GetRenderTargetData(IDirect3DSurface9* pSrc, IDirect3DSurface9 *pDst);
	void StretchSurface(const DXSurfaceRef &pSrc, const DXSurfaceRef &pDst, bool bLinear = false);

  RenderStatesManager* GetRenderStatesManager() { return &statesMan; }
	LightsManager* GetLightsManager() { return &lightsMan; }

	IDirect3DDevice9* GetDevice() { return pDevice; }
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

inline IDirect3DDevice9* GetDevice() { return Renderer::Get()->GetDevice(); }

inline RenderStatesManager* GetStatesManager() { return Renderer::Get()->GetRenderStatesManager(); }

inline LightsManager* GetLightsManager() { return Renderer::Get()->GetLightsManager(); } 

struct ConstantProtectionBlock
{
	ConstantProtectionBlock(ContextType context) { GetRenderer()->BeginConstantProtectionBlock(context); }
	~ConstantProtectionBlock() { GetRenderer()->EndConstantProtectionBlock(); }
};

}; // namespace Render

#endif
