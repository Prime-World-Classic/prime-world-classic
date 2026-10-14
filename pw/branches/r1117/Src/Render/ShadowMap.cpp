#include "stdafx.h"
#include "ShadowMap.h"
#include "ImmediateRenderer.h"
#include "smartrenderer.h"
#include "renderbackend.h"   // seam этапа 2: GetBackend()/Clear/EvictManagedResources
#include "IConfigManager.h"

static bool shadowPCF4_enable = true;
REGISTER_VAR("shadowPCF4_enable", shadowPCF4_enable, STORAGE_USER)
REGISTER_VAR_INTERFACE( "proxy_shadowPCF4_enable", NGlobal::MakeVarProxy(&shadowPCF4_enable), STORAGE_NONE );

static float shadowPCF4_coeff = 2.0;
REGISTER_DEV_VAR("shadowPCF4_coeff", shadowPCF4_coeff, STORAGE_NONE)

static bool s_forceMRT = false;
REGISTER_DEV_VAR("shadowForceMRT", s_forceMRT, STORAGE_NONE)

static float s_depthBias = 0.f, s_biasSlope = 0.f;
REGISTER_DEV_VAR("shadowBias",  s_depthBias, STORAGE_NONE)
REGISTER_DEV_VAR("shadowSlope", s_biasSlope, STORAGE_NONE)

static float s_texScale = 1.f, s_texBias = 0.f;
REGISTER_DEV_VAR("shadowTexScale", s_texScale, STORAGE_NONE)
REGISTER_DEV_VAR("shadowTexBias" , s_texBias, STORAGE_NONE)

static bool NewFrustumEnabled()
{
  return 0 != NGlobal::GetVar("shadowFrustumType").GetInt64() && 0 != NGlobal::GetVar("shadowFrustumMask").GetInt64();
}


namespace Render
{

//////////////////////////////////////////////////////////////////////////
ShadowMap::ShadowMap(void)
{
}

//////////////////////////////////////////////////////////////////////////
ShadowMap::~ShadowMap(void)
{
}

//////////////////////////////////////////////////////////////////////////
void ShadowMap::PreRender(void)
{
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMap::PostRender(void)
{
  return false;
}

//////////////////////////////////////////////////////////////////////////
inline bool ShadowMap::Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex)
{
  GetRenderer()->SetPixelShaderConstants(PSHADER_LOCALCONST0, ARRAY_SIZE(_constants), &_constants);
  GetRenderer()->SetPixelShaderConstantsMatrix(PSHADER_LOCALCONST4, _viewToLightSpace);
  return false;
}


//////////////////////////////////////////////////////////////////////////
//  class ShadowMapPCF
//////////////////////////////////////////////////////////////////////////
class ShadowMapPCF : public ShadowMap
{
  Texture2DRef     pShadowTexture;
  RenderSurfaceRef pDepth;

public:
  ShadowMapPCF(void);
  virtual ~ShadowMapPCF(void);
  virtual bool Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope);
  virtual void PreRender(void);
  virtual bool PostRender(void);
  virtual bool Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex);
  virtual bool ShowTexture(const CTRect<float> &_rect) const;
};

//////////////////////////////////////////////////////////////////////////

ShadowMapPCF::ShadowMapPCF(void)
{
}

ShadowMapPCF::~ShadowMapPCF(void)
{
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF::Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope)
{
  pShadowTexture = CreateRenderTexture2D(_width, _height, FORMAT_R32F);
  pDepth = CreateDepthStencilSurface(_width, _height);

  return IsValidPtr(pShadowTexture) && IsValidPtr(pDepth);
}

//////////////////////////////////////////////////////////////////////////
void ShadowMapPCF::PreRender(void)
{
  SmartRenderer::BindRenderTargetColor( 0, pShadowTexture->GetSurface(0) );
  SmartRenderer::BindRenderTargetDepth( pDepth->GetDXSurface() );
  if( NewFrustumEnabled() )
    GetBackend()->Clear(bcTarget | bcZBuffer, 0, 0.f, 0);
  else
    GetRenderer()->Clear(Color(255,255,255,255));
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF::PostRender(void)
{
  return false;
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF::Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex)
{
  SamplerState samplerState( SamplerState::PRESET_CLAMP_POINT() );
  samplerState.SetAddressU(NDb::TEXTUREADDRESSTYPE_BORDER);
  samplerState.SetAddressV(NDb::TEXTUREADDRESSTYPE_BORDER);
  Render::GetStatesManager()->SetSampler(1, samplerState, pShadowTexture);

  const float texSize = pShadowTexture->GetWidth();
  _constants[2].x = texSize;
  _constants[2].y = 1.0f / texSize;
  ShadowMap::Apply(_viewToLightSpace, _constants, _depthTex);
  GetImmRenderer()->RenderScreenQuad( ImmRenderer::ParamsVP("ShadowPass", _depthTex, &SamplerState::PRESET_CLAMP_POINT()) );

  return true;
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF::ShowTexture(const CTRect<float> &_rect) const
{
  GetRenderer()->SetPixelShaderConstantsVector4(PSHADER_LOCALCONST0, CVec4(s_texScale, s_texBias, 0.0f, 0.0f)); // scale and offset for R32f
  GetImmRenderer()->RenderScreenQuad( _rect, ImmRenderer::Params("DiffuseMapR32f", pShadowTexture) );
  return true;
}

//////////////////////////////////////////////////////////////////////////
//  class ShadowMapPCF_DS
//////////////////////////////////////////////////////////////////////////
class ShadowMapPCF_DS : public ShadowMap, public ReallocateHandler
{
  SHMatrix texScaleBiasMatrix;

  RenderSurfaceRef pRT;
  Texture2DRef  pDepth;
  DXSurfaceRef  pDepthSurface;

  float depthBias, biasSlope;

protected:
  ShadowMapPCF_DS(void);

public:
  virtual ~ShadowMapPCF_DS(void);
  virtual bool Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope);
  virtual void PreRender(void);
  virtual bool PostRender(void);
  virtual bool Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex);

  // from ReallocateHandler
  virtual void OnResourcesRelease()  { pDepthSurface.Attach(0); }
  virtual void OnResourcesReallocate() { pDepthSurface = pDepth->GetSurface(0); }
};

//////////////////////////////////////////////////////////////////////////

ShadowMapPCF_DS::ShadowMapPCF_DS(void)
: ReallocateHandler(HANDLERPRIORITY_LOW)
{
  depthBias = 0;
  biasSlope = 1;
}

ShadowMapPCF_DS::~ShadowMapPCF_DS(void)
{
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF_DS::Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope)
{
  depthBias = _depthBias;
  biasSlope = _biasSlope;
  s_depthBias = _depthBias;
  s_biasSlope = _biasSlope;

  const IConfigManager* const pConfigManager = GetIConfigManager();
  // Vendor-форматы глубины (DF16/DF24/INTZ/RAWZ) удалены: под Vulkan/DX12
  // аппаратные карты теней читаются как depth SRV (D24S8 / D32F),
  // «жирная» глубина под них отдельным форматом не выражается.
  D3DFORMAT format = D3DFMT_D24S8;
  D3DSURFACE_DESC desc = { format, D3DRTYPE_SURFACE, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT,
                           D3DMULTISAMPLE_NONE, 0, _width, _height };
  pDepth = Create<Texture2D>(desc);
  if( !IsValidPtr(pDepth) )
    return false;
  pDepthSurface = pDepth->GetSurface(0);

  // Пустой color-attachment под Vulkan/DX12 выражается color-write маской
  // (она и обнуляется в PreRender), отдельный «null RT»-формат не нужен.
  desc.Format = D3DFMT_R5G6B5;
  desc.Usage  = D3DUSAGE_RENDERTARGET;
  pRT = Create<RenderSurface>(desc);
  if( !IsValidPtr(pRT) || !pDepthSurface )
    return false;

  //set special texture matrix for shadow mapping
  const float fOffsetX = 0.5f + (0.5f / (float)_width);
  const float fOffsetY = 0.5f + (0.5f / (float)_height);
  const float range = 1; //   .  DX8    2^(bit depth)
  const float fBias    = 0.0f;
  //float fBias    = -0.001f * range;
  texScaleBiasMatrix.Set( 0.5f,  0.0f,  0.0f, fOffsetX,
                          0.0f, -0.5f,  0.0f, fOffsetY,
                          0.0f,  0.0f, range, fBias,
                          0.0f,  0.0f,  0.0f, 1.0f );

  return true;
}

//////////////////////////////////////////////////////////////////////////
void ShadowMapPCF_DS::PreRender(void)
{
  SmartRenderer::BindRenderTargetColor( 0, pRT->GetDXSurface() );
  SmartRenderer::BindRenderTargetDepth( pDepthSurface );

  RenderStatesManager* const pStatesManager = GetStatesManager();
  pStatesManager->SetStateDirect(D3DRS_COLORWRITEENABLE, 0);
  HRESULT hr = GetBackend()->Clear(bcZBuffer, 0, NewFrustumEnabled() ? 0.0f : 1.0f, 0);
  hr = hr;

  pStatesManager->SetStateDirect(D3DRS_DEPTHBIAS, *(DWORD*)&depthBias);
  pStatesManager->SetStateDirect(D3DRS_SLOPESCALEDEPTHBIAS, *(DWORD*)&biasSlope);
  //pStatesManager->SetStateDirect(D3DRS_DEPTHBIAS, *(DWORD*)&s_depthBias);
  //pStatesManager->SetStateDirect(D3DRS_SLOPESCALEDEPTHBIAS, *(DWORD*)&s_biasSlope);
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF_DS::PostRender(void)
{
  RenderStatesManager* const pStatesManager = GetStatesManager();
  pStatesManager->SetStateDirect(D3DRS_DEPTHBIAS, 0);
  pStatesManager->SetStateDirect(D3DRS_SLOPESCALEDEPTHBIAS, 0);
  pStatesManager->SetStateDirect(D3DRS_COLORWRITEENABLE, 0x0f);
  return true;
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapPCF_DS::Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex)
{
  const float texelSize = 1.0f / pDepth->GetWidth();

  bool multiplyMode = bool(_constants[2].z);
  _constants[2].Set(texelSize, -texelSize, 0.0f, 0.0f);

  const float coeffScale = _constants[1].w / (4.0f + shadowPCF4_coeff);
  _constants[3].z = coeffScale * shadowPCF4_coeff;
  _constants[3].w = coeffScale;

  SHMatrix viewToSMSpace;
  Multiply(&viewToSMSpace, texScaleBiasMatrix, _viewToLightSpace);
  ShadowMap::Apply(viewToSMSpace, _constants, _depthTex);

  const char * const shaderName = shadowPCF4_enable ? (multiplyMode ? "ShadowPassPCFDST4" : "ShadowPassPCFDST4Add")
                                                    : (multiplyMode ? "ShadowPassPCFDST" : "ShadowPassPCFDSTAdd");
  ImmRenderer::ParamsVP quadParams( shaderName, _depthTex, &SamplerState::PRESET_CLAMP_POINT() );
  quadParams.SetVShader("DepthRead");

  GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST2, _constants[0] );
  GetRenderer()->SetVertexShaderConstantsMatrix( VSHADER_LOCALCONST3, viewToSMSpace );
  GetRenderer()->SetPixelShaderConstantsVector4( PSHADER_LOCALCONST4, viewToSMSpace.GetTrans4() );

  Render::GetStatesManager()->SetSampler(1, SamplerState::PRESET_CLAMP_BILINEAR(), pDepth);
  GetImmRenderer()->RenderScreenQuad(quadParams);
  return true;
}


//////////////////////////////////////////////////////////////////////////
//  class ShadowMapESM_Base
//////////////////////////////////////////////////////////////////////////
class ShadowMapESM_Base : public ShadowMap, public ReallocateHandler
{
  DipDescriptor    summedAreaDipDesc;

  DXVertexBufferDynamicRef::Wrapped pSummedAreaVB;
  DXVertexDeclarationRef    pSummedAreaVDecl;
  DXVertexShaderRef         pSAVertexShader;

  struct VertexStride
  {
    float x;
    float y;
    float u;
    float v;
  };

  bool needRefillSAMesh;

  void RebuildSummedAreaMesh();
  void FillSummedAreaMesh();

protected:
  DXPixelShaderRef pSAPixelShader;
  DXPixelShaderRef pSAPrePixelShader;

  Texture2DRef     pShadowTextureSummedArea1;

  ShadowMapESM_Base(void);

public:
  virtual ~ShadowMapESM_Base(void);
  virtual bool Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope);
  virtual void PreRender(void);
  virtual bool PostRender(void);
  virtual bool Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex);

  // from ReallocateHandler
  virtual void OnResourcesRelease()  { needRefillSAMesh = true; }
  virtual void OnResourcesReallocate() {}
};

//////////////////////////////////////////////////////////////////////////
void ShadowMapESM_Base::RebuildSummedAreaMesh()
{
  if(!pShadowTextureSummedArea1)
    return;

  pSAPixelShader = Render::ImmRenderer::Get()->GetPShader( "SAPixelShader" );
  pSAVertexShader = Render::ImmRenderer::Get()->GetVShader( "SAVertexShader" );

  Render::VertexFormatDescriptor formatDescriptor;
  formatDescriptor.AddVertexElement( Render::VertexElementDescriptor( 0, 0, Render::VERTEXELEMENTTYPE_FLOAT2, Render::VERETEXELEMENTUSAGE_POSITION, 0 ) );

  pSummedAreaVDecl = Render::SmartRenderer::GetVertexFormatDeclaration( formatDescriptor );

  summedAreaDipDesc.primitiveType            = Render::RENDERPRIMITIVE_TRIANGLESTRIP;
  summedAreaDipDesc.numVertices              = 4;
  summedAreaDipDesc.primitiveCount           = 2;
  summedAreaDipDesc.startIndex               = 0;
  summedAreaDipDesc.baseVertexIndex          = 0;
  summedAreaDipDesc.minIndex                 = 0;

  pSummedAreaVB.Resize( summedAreaDipDesc.numVertices * sizeof(VertexStride) );
  NI_VERIFY(::Get(pSummedAreaVB), "ShadowManager: Failed to create SA vertices!", return );

  needRefillSAMesh = true;
}

//////////////////////////////////////////////////////////////////////////
void ShadowMapESM_Base::FillSummedAreaMesh()
{
  if(!needRefillSAMesh || !::Get(pSummedAreaVB) )
    return;

  needRefillSAMesh = false;

  VertexStride* vertices = (VertexStride*) Render::LockVB( ::Get( pSummedAreaVB ), D3DLOCK_DISCARD );

  vertices[ 0 ].x = -1.f; vertices[ 0 ].y = -1.f; 
  vertices[ 1 ].x = -1.f; vertices[ 1 ].y =  1.f; 
  vertices[ 2 ].x =  1.f; vertices[ 2 ].y = -1.f; 
  vertices[ 3 ].x =  1.f; vertices[ 3 ].y =  1.f; 

  pSummedAreaVB->Unlock();
}


//////////////////////////////////////////////////////////////////////////
ShadowMapESM_Base::ShadowMapESM_Base(void)
  : needRefillSAMesh(true)
{
}

ShadowMapESM_Base::~ShadowMapESM_Base(void)
{
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapESM_Base::Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope)
{
  pShadowTextureSummedArea1 = CreateRenderTexture2D(_width, _height, FORMAT_R32F);

  RebuildSummedAreaMesh();

  return IsValidPtr(pShadowTextureSummedArea1);
}

//////////////////////////////////////////////////////////////////////////
void ShadowMapESM_Base::PreRender(void)
{
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapESM_Base::PostRender(void)
{
  FillSummedAreaMesh();

  RenderState renderState;
  renderState.SetBlendMode(NDb::BLENDMODE_OFF);
  renderState.SetAlphaTest(NDb::ONOFFSTATE_OFF);
  renderState.SetCulling(NDb::ONOFFSTATE_OFF);

  RenderStatesManager &statesManager = *GetStatesManager();
  statesManager.SetState(renderState);

  statesManager.SetStateDirect(D3DRS_ZENABLE, 0);
  statesManager.SetStateDirect(D3DRS_ZWRITEENABLE, 0);

  SamplerState samplerState( SamplerState::PRESET_CLAMP_POINT() );
  samplerState.SetAddressU(NDb::TEXTUREADDRESSTYPE_BORDER);
  samplerState.SetAddressV(NDb::TEXTUREADDRESSTYPE_BORDER);

  statesManager.SetSamplerState(0, samplerState);
  // SetSamplerStateDirect, а не SetSamplerState: BORDERCOLOR не входит в пресет, и
  // обычная запись его не инвалидировала бы (docs/PLAN_NVRHI.md §2.1).
  statesManager.SetSamplerStateDirect(0, D3DSAMP_BORDERCOLOR, 0x00000000);

  SmartRenderer::BindVertexShader( pSAVertexShader );
  SmartRenderer::BindVertexDeclaration( pSummedAreaVDecl );

  SmartRenderer::BindVertexBuffer( 0, ::Get( pSummedAreaVB ), sizeof( VertexStride ) );

  CVec4 screenScaleOffset;
  screenScaleOffset.x = 1.0f / pShadowTextureSummedArea1->GetWidth();
  screenScaleOffset.y = 1.0f / pShadowTextureSummedArea1->GetHeight();
  screenScaleOffset.z = 0.5f * screenScaleOffset.x;
  screenScaleOffset.w = 0.5f * screenScaleOffset.y;

  GetRenderer()->SetVertexShaderConstantsVector4( VSHADER_LOCALCONST2, CVec4(1, 1, 0, 0) );
  GetRenderer()->SetPixelShaderConstantsVector4(PSHADER_LOCALCONST0, screenScaleOffset);

  SmartRenderer::BindPixelShader(pSAPrePixelShader);
  SmartRenderer::DrawPrimitive( summedAreaDipDesc );

  return true;
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapESM_Base::Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex)
{
  const float texSize = pShadowTextureSummedArea1->GetWidth();
  _constants[2].x = texSize;
  _constants[2].y = 1.0f / texSize;
  ShadowMap::Apply(_viewToLightSpace, _constants, _depthTex);
  return false;
}

//////////////////////////////////////////////////////////////////////////
//  class ShadowMapESM
//////////////////////////////////////////////////////////////////////////
class ShadowMapESM : public ShadowMapESM_Base
{
protected:
  RenderSurfaceRef pDepth;
  Texture2DRef     pShadowTexture;

  ShadowMapESM(void);

public:
  virtual ~ShadowMapESM(void);
  virtual bool Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope);
  virtual void PreRender(void);
  virtual bool PostRender(void);
  virtual bool Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex);
};

//////////////////////////////////////////////////////////////////////////

ShadowMapESM::ShadowMapESM(void)
{
}

ShadowMapESM::~ShadowMapESM(void)
{
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapESM::Init(unsigned int _width, unsigned int _height, float _depthBias, float _biasSlope)
{
  pSAPrePixelShader = Render::ImmRenderer::Get()->GetPShader( "SAPrePixelShader" );

  pDepth = CreateDepthStencilSurface( _width, _height );
  pShadowTexture = CreateRenderTexture2D( _width, _height, FORMAT_R32F );
  return IsValidPtr(pDepth) && IsValidPtr(pShadowTexture) && ShadowMapESM_Base::Init(_width, _height, _depthBias, _biasSlope);
}

//////////////////////////////////////////////////////////////////////////
void ShadowMapESM::PreRender(void)
{
  SmartRenderer::BindRenderTargetColor( 0, pShadowTexture->GetSurface(0) );
  SmartRenderer::BindRenderTargetDepth( pDepth->GetDXSurface() );
  if( NewFrustumEnabled() )
    GetBackend()->Clear(bcTarget | bcZBuffer, 0, 0.f, 0);
  else
    GetRenderer()->Clear(Color(255,255,255,255));
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapESM::PostRender(void)
{
  SmartRenderer::BindRenderTarget(pShadowTextureSummedArea1);
  SmartRenderer::BindTexture(0, pShadowTexture);
  return ShadowMapESM_Base::PostRender();
}

//////////////////////////////////////////////////////////////////////////
bool ShadowMapESM::Apply(const SHMatrix& _viewToLightSpace, CVec4 (&_constants)[4], const Texture2DRef& _depthTex)
{
  ShadowMapESM_Base::Apply(_viewToLightSpace, _constants, _depthTex);

  //SamplerState samplerState( SamplerState::PRESET_CLAMP_TRILINEAR() );
  //samplerState.SetMinFilter(NDb::MINFILTERTYPE_ANISOTROPIC6X);

  GetStatesManager()->SetSampler(1, SamplerState::PRESET_CLAMP_BILINEAR(), pShadowTextureSummedArea1);
  GetImmRenderer()->RenderScreenQuad( ImmRenderer::ParamsVP("ShadowPassSAESM", _depthTex, &SamplerState::PRESET_CLAMP_POINT()) );

  return true;
}


//////////////////////////////////////////////////////////////////////////
ShadowMap* CreateShadowMap(NDb::ShadowMode _shadowMode)
{
  ShadowMap* pShadowMap = 0;
  const IConfigManager* const pConfigManager = GetIConfigManager();

  switch(_shadowMode)
  {
  case NDb::SHADOWMODE_PCF:
    if(!s_forceMRT && pConfigManager->HasDSM() )
    {
      pShadowMap = new ReallocateWrapper<ShadowMapPCF_DS>;
      break;
    }
    pShadowMap = new ShadowMapPCF;
    break;

  //case NDb::SHADOWMODE_SAESM:
  default:
    pShadowMap = new ReallocateWrapper<ShadowMapESM>;
    break;
  }

  return pShadowMap;
}

};  //namespace Render
