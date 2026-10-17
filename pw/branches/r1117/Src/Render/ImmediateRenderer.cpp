#include "stdafx.h"
#include "ImmediateRenderer.h"


#include "primitive.h"
#include "renderresourcemanager.h"
#include "shadercompiler.h"
#include "renderbackend.h"   // seam этапа 2: GetBackend()/BackendViewport
#include "shadercontainer.h" // Materials::ShaderTargetTag — выбор слоя шейдеров
#include "GlobalMasks.h"

namespace Render
{

static CTRect<float> s_fullScreen(-1.0f, -1.0f, +1.0f, +1.0f);

ImmRenderer* ImmRenderer::s_pInstance = NULL;

namespace {
struct ImmRendererVertex
{
	float x, y;
	float u, v;
};

// Шаг 14 (срез A): блобы OnlineShaders — SM3 DXIL, а Vulkan-бэкенд умеет
// только SPIR-V. Рядом с блобом лежит sidecar `<блоб>.spv` (пишет
// tools/online_shaders/build.py из HLSL-портов в OnlineShaders/port/; сам
// блоб не меняется — D3D9-путь обязан остаться побайтово прежним).
// Слой объявляет БЭКЕНД, а не dev-var: иначе цель, которую бэкенд не умеет
// создавать, даёт молчаливый фолбэк. Замер: за 100 с прогона 2869 обращений к
// VertexShader.vsb/DiffuseMap.psb — это композит кадра (экран-квад
// RenderScreenQuad), без него кадр чёрный при полностью исправном выводе.
// Возвращает открытый sidecar (или пустой указатель, если слоя нет / бэкенд
// его не объявил). Пустой CObj() здесь корректен: политика CObj добавляет
// ссылку при копировании.
static CObj<Stream> OpenSpvSidecar(char const* pFileName, unsigned* pSizeOut)
{
	if (GetBackend()->GetShaderTargetTag() != (unsigned)Materials::stSpirv)
		return CObj<Stream>();
	nstl::string spv(pFileName);
	spv += ".spv";
	CObj<Stream> pSpv(RootFileSystem::OpenFile(spv.c_str(), FILEACCESS_READ, FILEOPEN_OPEN_EXISTING));
	if (!pSpv || !pSpv->IsOk() || pSpv->GetSize() == 0)
	{
		DebugTrace("OnlineShader: %s — SPIR-V sidecar не открылся, беру DXBC", spv.c_str());
		return CObj<Stream>();
	}
	*pSizeOut = (unsigned)pSpv->GetSize();
	return pSpv;
}

DXVertexShaderRef LoadVertexShader(char const* pFileName)
{
	// Сначала SPIR-V-слой, если бэкенд его объявил.
	unsigned spvSize = 0;
	CObj<Stream> pSpv(OpenSpvSidecar(pFileName, &spvSize));
	if (pSpv)
	{
		DXVertexShaderRef shSpv = GetRenderer()->CreateVertexShader(
			(unsigned char const*)pSpv->GetBuffer(), spvSize);
		if (Get(shSpv))
			return shSpv;
		DebugTrace("OnlineShader: %s.spv (%u Б) — бэкенд не создал VS из SPIR-V", pFileName, spvSize);
	}
	CObj<Stream> pStream(RootFileSystem::OpenFile(pFileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING));
	NI_VERIFY(pStream && pStream->IsOk(), NStr::StrFmt("Cannot open file for reading: %s", pFileName), return DXVertexShaderRef(); );
	// Без имени файла сбой неотличим от «шейдер вообще не просили».
	DXVertexShaderRef sh = GetRenderer()->CreateVertexShader((unsigned char const*)pStream->GetBuffer());
	if (!Get(sh))
		DebugTrace("OnlineShader: %s (%u Б) — бэкенд не создал VS", pFileName,
			(unsigned)pStream->GetSize());
	return sh;
}

DXPixelShaderRef LoadPixelShader(char const* pFileName)
{
	unsigned spvSize = 0;
	CObj<Stream> pSpv(OpenSpvSidecar(pFileName, &spvSize));
	if (pSpv)
	{
		DXPixelShaderRef shSpv = GetRenderer()->CreatePixelShader(
			(unsigned char const*)pSpv->GetBuffer(), spvSize);
		if (Get(shSpv))
			return shSpv;
		DebugTrace("OnlineShader: %s.spv (%u Б) — бэкенд не создал PS из SPIR-V", pFileName, spvSize);
	}
	CObj<Stream> pStream(RootFileSystem::OpenFile(pFileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING));
	NI_VERIFY(pStream && pStream->IsOk(), NStr::StrFmt("Cannot open file for reading: %s", pFileName), return DXPixelShaderRef(); );
	DXPixelShaderRef sh = GetRenderer()->CreatePixelShader((unsigned char const*)pStream->GetBuffer());
	if (!Get(sh))
		DebugTrace("OnlineShader: %s (%u Б) — бэкенд не создал PS", pFileName,
			(unsigned)pStream->GetSize());
	return sh;
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ImmRenderer::BasicParams descendants

void ImmRenderer::BasicParams::BindMaterial( const CTRect<float> *rect /*= NULL*/ ) const
{
  BackendViewport vp;
  GetBackend()->GetViewport(vp);

  if(!rect)
    rect = &s_fullScreen;

  CVec4 madValueUV(uvRect.x2 - uvRect.x1, uvRect.y2 - uvRect.y1, uvRect.x1, uvRect.y1);
  CVec4 madValuePos;
  madValuePos.x = 0.5f * (rect->maxx - rect->minx);
  madValuePos.y = 0.5f * (rect->maxy - rect->miny);
  madValuePos.z = 0.5f * (rect->maxx + rect->minx) - 1.0f / vp.width;  // NOT 0.5f/width because full range is -1..1
  madValuePos.w = 0.5f * (rect->maxy + rect->miny) + 1.0f / vp.height; // sign inversion occurs in screen-space to viewport conversion

  GetRenderer()->SetVertexShaderConstantsVector4(VSHADER_LOCALCONST1, madValuePos);
  GetRenderer()->SetVertexShaderConstantsVector4(VSHADER_LOCALCONST0, madValueUV);
}

void ImmRenderer::Params::BindMaterial( const CTRect<float> *rect /*= NULL*/ ) const
{
  ImmRenderer::BasicParams::BindMaterial(rect);

  if(pTexture) {
    const SamplerState& state = pCustomSamplerState ? *pCustomSamplerState : SamplerState::PRESET_CLAMP_BILINEAR();
    GetStatesManager()->SetSampler(0, state, pTexture);
  }

  SmartRenderer::BindVertexShader( pVertexShader );
  SmartRenderer::BindPixelShader( pPixelShader );
}


void ImmRenderer::ParamsVP::SetUV()
{
  int x, y, w, h;
  SmartRenderer::GetMainViewport(x, y, w, h);

  ASSERT(pTexture);
  const float scaleW = 1.f / pTexture->GetWidth();
  const float scaleH = 1.f / pTexture->GetHeight();
  uvRect.Set( scaleW * x, scaleH * y, scaleW * (x + w), scaleH * (y + h) );
}

void ImmRenderer::MaterialParams::BindMaterial( const CTRect<float> *rect ) const
{
  ImmRenderer::BasicParams::BindMaterial(rect);
  pMaterial->PrepareRenderer();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class ImmRenderer

void ImmRenderer::Init()
{
	NI_ASSERT(s_pInstance == NULL, "ImmRenderer already inited");
	s_pInstance = new ImmRenderer();
}

void ImmRenderer::Term()
{
	SAFE_DELETE(s_pInstance);
}

ImmRenderer* ImmRenderer::Get()
{
	return s_pInstance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImmRenderer::ImmRenderer()
{
  // create index and vertex buffers
	unsigned int indices[] = {0, 1, 2, 0, 2, 3};
	ImmRendererVertex vertices[] = {
		+1.0f, +1.0f, 1.0f, 0.0f,
		-1.0f, +1.0f, 0.0f, 0.0f,
		-1.0f, -1.0f, 0.0f, 1.0f,
		+1.0f, -1.0f, 1.0f, 1.0f
	};
	pIB = CreateIB(sizeof(indices), RENDER_POOL_MANAGED, indices);
	pFullVB = CreateVB(sizeof(vertices), RENDER_POOL_MANAGED, vertices);
	pRectVB.Resize( sizeof(vertices) );

	// dip descriptor
	VertexFormatDescriptor formatDescriptor;
	formatDescriptor.AddVertexElement(VertexElementDescriptor(0, 0,	VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_POSITION, 0));
	formatDescriptor.AddVertexElement(VertexElementDescriptor(0, 8, VERTEXELEMENTTYPE_FLOAT2, VERETEXELEMENTUSAGE_TEXCOORD, 0));
	pVDecl = SmartRenderer::GetVertexFormatDeclaration(formatDescriptor);
	dip.primitiveType = RENDERPRIMITIVE_TRIANGLELIST;
	dip.baseVertexIndex = 0;
	dip.numVertices = ARRAY_SIZE(vertices);
	dip.primitiveCount = ARRAY_SIZE(indices) / 3;
	dip.startIndex = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImmRenderer::~ImmRenderer()
{
	// references are automatically destroyed
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ImmRenderer::RenderScreenQuad( const CTPoint<float> poly[4], const BasicParams & params )
{
	// prepare the mesh
	IDirect3DVertexBuffer9* pVB = ::Get( pFullVB );
	if( poly )
		if( float* pData = LockVB<float>(::Get(pRectVB), LOCK_DISCARD) )
    {
		  *(pData++) = poly[0].x;
		  *(pData++) = poly[0].y;
		  *(pData++) = 1.0f;
		  *(pData++) = 0.0f;
		  *(pData++) = poly[1].x;
		  *(pData++) = poly[1].y;
		  *(pData++) = 0.0f;
		  *(pData++) = 0.0f;
		  *(pData++) = poly[2].x;
		  *(pData++) = poly[2].y;
		  *(pData++) = 0.0f;
		  *(pData++) = 1.0f;
		  *(pData++) = poly[3].x;
		  *(pData++) = poly[3].y;
		  *(pData++) = 1.0f;
		  *(pData++) = 1.0f;
		  BackendUnlockBuffer( ::Get( pRectVB ) );
		  pVB = ::Get( pRectVB );
	  }

	// render
  params.BindMaterial();
	SmartRenderer::BindIndexBuffer( pIB );
  SmartRenderer::BindVertexDeclaration( pVDecl );
	SmartRenderer::BindVertexBuffer( 0, pVB, sizeof(ImmRendererVertex) );
	SmartRenderer::DrawIndexedPrimitive( dip );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImmRenderer::RenderScreenQuad(CTRect<float> const& rect, BasicParams const& params)
{
 	// render
  params.BindMaterial(&rect);
	SmartRenderer::BindIndexBuffer( pIB );
  SmartRenderer::BindVertexDeclaration( pVDecl );
	SmartRenderer::BindVertexBuffer( 0, pFullVB, sizeof(ImmRendererVertex) );
	SmartRenderer::DrawIndexedPrimitive( dip );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImmRenderer::RenderScreenQuad(CTRect<float> const& rect, BasicParams const& params, DXVertexDeclarationRef const &_pVDecl)
{
 	// render
  params.BindMaterial(&rect);
	SmartRenderer::BindIndexBuffer( pIB );
  SmartRenderer::BindVertexDeclarationRaw( ::Get(_pVDecl) );
	SmartRenderer::BindVertexBufferRaw( 0, ::Get(pFullVB), sizeof(ImmRendererVertex) );
	SmartRenderer::DrawIndexedPrimitive( dip );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImmRenderer::RenderScreenQuad(CTRect<int> const& rect, BasicParams const& params)
{
  BackendViewport vp;
  GetBackend()->GetViewport(vp);

	// convert coordinates
	CTRect<float> rectFloat;
	rectFloat.minx = 2.0f * float(rect.minx) / float(vp.width) - 1.0f;
	rectFloat.maxx = 2.0f * float(rect.maxx) / float(vp.width) - 1.0f;
	rectFloat.miny = -2.0f * float(rect.maxy) / float(vp.height) + 1.0f;
	rectFloat.maxy = -2.0f * float(rect.miny) / float(vp.height) + 1.0f;

	// forward
	RenderScreenQuad(rectFloat, params);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImmRenderer::RenderScreenQuad(BasicParams const& params)
{
	  RenderScreenQuad(s_fullScreen, params);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImmRenderer::RenderScreenQuad(BasicParams const& params, DXVertexDeclarationRef const &_pVDecl)
{
	  RenderScreenQuad(s_fullScreen, params, _pVDecl);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DXPixelShaderRef const& ImmRenderer::GetPShader(char const* pName)
{
	DXPixelShaderRef& shader = shaderPCache[pName];
	if (!shader)
	{
		string path(ONLINE_SHADERS_PATH);
		path += pName;
		path += ".psb";
		shader = LoadPixelShader(path.c_str());
	}

	return shader;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DXVertexShaderRef const& ImmRenderer::GetVShader(char const* pName)
{
  if(!pName)
    pName = "VertexShader";

  DXVertexShaderRef& shader = shaderVCache[pName];
  if (!shader)
  {
    string path(ONLINE_SHADERS_PATH);
    path += pName;
    path += ".vsb";
    shader = LoadVertexShader(path.c_str());
  }

  return shader;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImmRenderer::ReloadShaders()
{
	shaderPCache.clear();
  shaderVCache.clear();
}

}; // namespace Render
