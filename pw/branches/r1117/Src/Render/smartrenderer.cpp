#include "stdafx.h"
#include "d3d9backend.h"   // seam этапа 2: MakeBackendHandle/MakeBackendViewport
#include "conststore.h"

#include "smartrenderer.h"

#include "shadercompiler.h"
#include "renderresourcemanager.h"
#include "uirenderer.h"
#include "../System/staticarray.h"
#include "texture.h"
#include "renderflagsconverter.h"
#include "GlobalMasks.h"
#include "DXWarnSignal.h"
#include <vector>

static DXWarnSignal s_DXWarnLevel;

static bool nullrender = false;
REGISTER_VAR( "nodip", nullrender, STORAGE_NONE );

// Кадр, на котором клиент сам снимает картинку (0 — выключено). См. блок в
// OnFrameStart: в shipping-сборке нечем ввести команду `screenshot`, а снимок
// самим клиентом — единственный способ проверить readback бэкенда на реальном
// устройстве. Включается autoexec.cfg, который клиент исполняет сам:
//   setvar auto_screenshot 3000
// (docs/PLAN_NVRHI.md §2.13).
static int s_autoScreenshotAt = 0;
REGISTER_VAR( "auto_screenshot", s_autoScreenshotAt, STORAGE_NONE );

// Шаг 7 плана NVRHI (docs/PLAN_NVRHI.md §2.9): сверка тени констант с тем, что
// реально стоит в устройстве. Без этого замера мы не знаем, можно ли на этапе
// 3 делать тень единственным источником констант — на uniform-буферах
// Get*ShaderConstantF ответить нечем. Включается так же, как auto_screenshot:
//   setvar verify_const_block 1
static int s_verifyConstBlock = 0;
REGISTER_VAR( "verify_const_block", s_verifyConstBlock, STORAGE_NONE );


// Статистика по шейдерам: сколько уникальных VS/PS/пар биндится за кадр и за
// сессию. Нужна для расчёта кэша графических пайплайнов (docs/PLAN_NVRHI.md,
// этап 0): без неё реальное число вариаций неизвестно — верхняя граница контента
// (49966 пар в .shd) ничего не говорит о потребляемом множестве.
// В shipping по умолчанию выключена (per-draw hash insert), включается сборкой
// с -DPW_RENDER_SHADER_STATS (build_client_x64_wine.sh: PW_EXTRA_DEFS).
// Переключатель USE_RENDER_SHADER_STATS вынесен в Render/stdafx.h — он нужен и
// batch.cpp (счётчик уникальных sortId).

#ifdef USE_RENDER_SHADER_STATS

namespace
{
  NDebug::DebugVar<int> g_numVSu( "numVSUniq", "Statistics" );
  NDebug::DebugVar<int> g_numPSu( "numPSUniq", "Statistics" );
  NDebug::DebugVar<int> g_numVSt( "numVSTotal", "Statistics" );
  NDebug::DebugVar<int> g_numPSt( "numPSTotal", "Statistics" );
  NDebug::DebugVar<int> g_numPairu("numShaderPairUniq", "Statistics" );
  NDebug::DebugVar<int> g_numPairt("numShaderPairTotal", "Statistics" );
  NDebug::DebugVar<int> g_numVS( "numVS", "Statistics", true );
  NDebug::DebugVar<int> g_numPS( "numPS", "Statistics", true );
  hash_set<void*>  psMap, vsMap, psMapTotal, vsMapTotal;
  // Ключ ядра пайплайна: пара (VS, PS). Полный ключ пайплайна —
  // (пара, RenderState, InputLayout, форматы RT); пара — его самая
  // широкая составляющая, поэтому её число и меряем.
  struct PairHash { size_t operator()(UINT64 _k) const { return size_t(_k ^ (_k >> 32)); } };
  hash_set<UINT64, PairHash> pairMap, pairMapTotal;
  UINT64 lastBoundPair = 0;
  // sortId = CRC материального состояния (техника + пины + сэмплеры +
  // рендер-состояния). Вместе с парой (VS,PS) — полный ключ пайплайна.
  struct IdHash { size_t operator()(unsigned int _k) const { return size_t(_k); } };
  hash_set<unsigned int, IdHash> sortMap, sortMapTotal;
}

// Вызывается из Batch::Prepare() (см. Render/renderstats.h)
void NoteSortId( unsigned int sortId )
{
  sortMap.insert( sortId );
  sortMapTotal.insert( sortId );
}

#endif // USE_RENDER_SHADER_STATS

static unsigned int  triangleCount = 0;
static unsigned int  dipCount = 0;

namespace Render
{
namespace SmartRenderer
{

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  struct InitIdxVB : public IntrusivePtrDeleter
  {
    typedef DXVertexBufferRef Type;

    void Init(Type& _result)
    {
      // Create & fill second vertex buffer
      _result = CreateVB(MAX_INSTANCES_COUNT * sizeof(int), RENDER_POOL_MANAGED);
      NI_VERIFY( _result, "Couldn't create instance idx VB", return );

      if( int * const pBuff = LockVB<int>(Get(_result), LOCK_DEFAULT) )
      {
        for(size_t i = 0; i < MAX_INSTANCES_COUNT; i++)
          pBuff[i] = i;

        BackendUnlockBuffer(Get(_result));
      }
      else
        _result.Attach(0);
    }
  };

  static ManagedResource<InitIdxVB> s_pInstanceIndexVB;

///////////////////////////////////// 
static nstl::hash_map< VertexFormatDescriptor, DXVertexDeclarationRef > VertexDeclaraionsCash;
typedef nstl::hash_map< VertexFormatDescriptor, DXVertexDeclarationRef >::iterator VertexDeclarationsCashIterator;

static const unsigned int MAX_STREAMS_COUNT = 16;
static StaticArray<IDirect3DVertexBuffer9*,MAX_STREAMS_COUNT> lastVertexBuffersPtrs;
static StaticArray<unsigned int,MAX_STREAMS_COUNT> lastVertexBuffersOffsets;
static IDirect3DIndexBuffer9* pLastIndexBuffer = 0; 

struct SamplerInfo
{
	const Texture *pTex;
	bool     bProtected;
};
static StaticArray<SamplerInfo, Sampler::MAX_VS_SAMPLER_INDEX+1> lastTexturesPtrs;

static IDirect3DVertexDeclaration9* pLastVertexDeclaration = 0;

static IDirect3DPixelShader9* pLastPixelShader = 0;
static IDirect3DVertexShader9* pLastVertexShader = 0;

#ifdef USE_RENDER_SHADER_STATS
namespace
{
  inline void NoteShaderPair()
  {
    // Ключ (VS,PS) одним словом: кастоинг поинтеров, не склеивание в 32 бита
    // (на x64 адреса не влезает в 32 бита, а склеинные бы сливались в pairMap и число пайплеинов
    // приходило бы недооценным). Вероятность коллизиии при ~10^4 ключей ~5e-12 для замера не влияет.
    const UINT64 vs = (UINT64)(size_t)pLastVertexShader;
    const UINT64 ps = (UINT64)(size_t)pLastPixelShader;
    const UINT64 key = vs * 0x9E3779B97F4A7C15ull ^ ps;
    if(key == lastBoundPair)
      return;
    lastBoundPair = key;
    pairMap.insert(key);
    pairMapTotal.insert(key);
  }
}
#endif

static const unsigned int MAX_COLOR_RT = 8;
static StaticArray<IDirect3DSurface9*,MAX_COLOR_RT> curColorSurface;
static DWORD curColorSurfaceWidth = 1, curColorSurfaceHeight = 1;
static DWORD curDepthSurfaceWidth = 1, curDepthSurfaceHeight = 1;
static IDirect3DSurface9* curDepthSurface( 0 );

static IDirect3DSurface9* pDefaultRT0( 0 );
static IDirect3DSurface9* pDefaultRT1( 0 );
static IDirect3DSurface9* pDefaultDepth( 0 );

static D3DVIEWPORT9 viewport = { 0 };

namespace {
  bool useViewport = false;
  bool instancingEnabled = false;


  class DeclMap : public DeviceDeleteHandler
  {
    typedef nstl::hash_map<void*, DXVertexDeclarationRef> Cache;

    Cache cache;

  public:
    virtual void OnDeviceDelete()
    {
      cache.clear();
    }

    IDirect3DVertexDeclaration9* Get(IDirect3DVertexDeclaration9 *_pDecl)
    {
      Cache::iterator it = cache.find(_pDecl);
      if(cache.end() != it)
        return ::Get(it->second);

      // Построение инстансного варианта раскладки — за бэкендом (для D3D9 это
      // сдвиг потоков + SHORT2/BLENDINDICES в поток 1; для Vulkan номер
      // экземпляра — встроенный SV_InstanceID, отдельного атрибута нет).
      // Здесь остаётся только кэш: вариант создаётся один раз на исходную
      // раскладку и живёт до удаления устройства (OnDeviceDelete выше).
      HInputLayout instanced = {};
      HRESULT hr = GetBackend()->CreateInstancedInputLayout(
          MakeBackendHandle<HInputLayout>(_pDecl), &instanced);
      NI_DX_THROW(hr, "CreateInstancedInputLayout");
      // Ручка создана бэкендом уже со счётчиком ссылок — принимаем без AddRef.
      cache[_pDecl] = DXVertexDeclarationRef(
          static_cast<IDirect3DVertexDeclaration9*>(instanced.p), false);
      return ::Get(cache[_pDecl]);
    }
  };

  static DeclMap s_instancedVDecl;
}


//
// This part of smart renderer handles batching and deferred rendering execution
//
struct Vertex2D
{
  Vertex2D(float x, float y, float z, DWORD color)
  {
    this->x = x;
    this->y = y;
    this->z = z;
    this->w = 1.0f;
    this->color = color;
  }
  float x, y, z, w;
  DWORD color;
};

std::vector<Vertex2D> g_deferredVerts;
void AddRect(float l, float t, float r, float b, int R, int G, int B)
{
  DWORD color = D3DCOLOR_XRGB(R, G, B);
  std::vector<Vertex2D>& verts = g_deferredVerts;
  verts.push_back(Vertex2D(l, t, 0.0f, color));
  verts.push_back(Vertex2D(r, t, 0.0f, color));
  verts.push_back(Vertex2D(l, b, 0.0f, color));
  verts.push_back(Vertex2D(l, b, 0.0f, color));
  verts.push_back(Vertex2D(r, t, 0.0f, color));
  verts.push_back(Vertex2D(r, b, 0.0f, color));
}

void AddLine(float sx, float sy, float ex, float ey, int R, int G, int B, float width /* 1.0f */)
{
  DWORD color = D3DCOLOR_XRGB(R, G, B);
  std::vector<Vertex2D>& verts = g_deferredVerts;
  verts.push_back(Vertex2D(sx, sy, 0.0f, color));
  verts.push_back(Vertex2D(sx + width, sy, 0.0f, color));
  verts.push_back(Vertex2D(ex, ey, 0.0f, color));
  verts.push_back(Vertex2D(ex, ey, 0.0f, color));
  verts.push_back(Vertex2D(sx + width, sy, 0.0f, color));
  verts.push_back(Vertex2D(ex + width, ey, 0.0f, color));
}

void RenderDeferred2D()
{
  if(g_deferredVerts.size() > 2)
  {
    // Use fixed-function transformation to avoid custom extra shader here.
    // Draw call uses currently bound font shader which is good enough, thus
    // this function might not work if shader is not the font shader anymore.

    std::vector<Vertex2D>& verts = g_deferredVerts;   

    GetDevice()->SetVertexShader(NULL);
    Render::GetStatesManager()->SetStateDirect(D3DRS_ALPHABLENDENABLE, FALSE);
    GetDevice()->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    GetDevice()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, verts.size() / 3, &verts[0], sizeof(Vertex2D));

    verts.clear();
  }
}



//////////////////////////////////////////////////////////////////////////
void GetTriangleAndDipCount(unsigned int& triangles, unsigned int& dips)
{
  triangles = triangleCount;
  dips = dipCount;
}

//////////////////////////////////////////////////////////////////////////
void ResetTriangleAndDipCount()
{
  triangleCount = 0;
  dipCount = 0;
}

// Lazy states application system
//
// Частоты потоков (механизм D3D9-инстансинга) ушли в бэкенд: SetInstanceCount +
// ленивая переустановка перед рисованием, см. docs/PLAN_NVRHI.md §2.10.

//////////////////////////////////////////////////////////////////////////
static void NullLastPointers()
{
  pLastIndexBuffer = 0;
  for (unsigned int i = 0; i < MAX_STREAMS_COUNT; ++i) 
  {
    lastVertexBuffersPtrs[i] = 0;
    lastVertexBuffersOffsets[i] = 0;
  }
  for (unsigned int i = 0; i < lastTexturesPtrs.capacity(); ++i)
  {
    lastTexturesPtrs[i].pTex = 0;
    lastTexturesPtrs[i].bProtected = false;
  }

  pLastPixelShader = 0;
  pLastVertexShader = 0;
  pLastVertexDeclaration = 0;

#ifdef USE_RENDER_SHADER_STATS
  lastBoundPair = 0; // иначе первая после сброса пара того же кадра не попадёт в pairMap
#endif

  curDepthSurface = 0;
  for (unsigned int i = 0; i < MAX_COLOR_RT; ++i) 
    curColorSurface[i] = 0;

  DisableHardwareInstancing();
}

//////////////////////////////////////////////////////////////////////////
void NullThePointers()
{
// Sorry for profiling code [11/17/2009 smirnov]
//  _DebugSamplerBindReset();

  NullLastPointers();

	pDefaultRT0 = 0;
	pDefaultRT1 = 0;
	pDefaultDepth = 0;
}


//////////////////////////////////////////////////////////////////////////
void OnFrameStart()
{
#ifdef USE_RENDER_SHADER_STATS
  // DebugVar-можно посмотреть только в игровой консоли, а замер нужно на
  // E2E-стенде (консоли нет) и в shipping-сборке меры. Раз в ~5 с печатаем
  // снимок в лог: какие ключи пайплайного кэша нужно готовить на этапе 3.
  static int s_statsTick = 0;
  if ( ++s_statsTick % 300 == 0 )
  {
    DebugTrace( "ShaderStats: pairTotal=%d pairFrame=%d vsTotal=%d vsFrame=%d psTotal=%d psFrame=%d sortTotal=%d sortFrame=%d",
      (int)pairMapTotal.size(), (int)pairMap.size(), (int)vsMapTotal.size(),
      (int)vsMap.size(), (int)psMapTotal.size(), (int)psMap.size(),
      (int)sortMapTotal.size(), (int)sortMap.size() );
    // Раскладка констант (docs/PLAN_NVRHI.md §2.7): какие c0..c255 реально
    // пишутся — по этому выбирается размер/число uniform-буферов на этапе 3.
    DebugTrace( "%s", GetConstStore().StatsLine() );
    // Побитовая сверка тени с состоянием устройства (шаг 7, §2.9). Делается
    // ДО Turn(), но Turn() счётчики не трогает тень — только статистику.
    // Смысл замера: mismatch>0 означает, что в клиенте остались пути записи
    // констант мимо Renderer::Set*ShaderConstants (или устройство
    // пересоздавалось — тогда сравнение невалидно в принципе).
    if ( s_verifyConstBlock )
    {
      IRenderBackend* pB = GetBackend();
      const ConstVerifyResult vs = VerifyConstStoreAgainstBackend( pB, csVertex );
      const ConstVerifyResult ps = VerifyConstStoreAgainstBackend( pB, csPixel );
      // Двумя строками: DebugTrace/PrintfFormatter принимает не больше 10
      // аргументов, а полей на стадию — как раз шесть.
      DebugTrace( "ConstVerify VS: checked=%u mismatch=%u first=c%u worst=c%u shadow=0x%08x device=0x%08x hr=0x%x",
                  vs.regsChecked, vs.mismatches, vs.firstMismatch, vs.worstRegister,
                  vs.worstShadowBits, vs.worstDeviceBits, (unsigned)vs.hr );
      DebugTrace( "ConstVerify PS: checked=%u mismatch=%u first=c%u worst=c%u shadow=0x%08x device=0x%08x hr=0x%x",
                  ps.regsChecked, ps.mismatches, ps.firstMismatch, ps.worstRegister,
                  ps.worstShadowBits, ps.worstDeviceBits, (unsigned)ps.hr );
    }
    GetConstStore().Stage( csVertex ).Turn();
    GetConstStore().Stage( csPixel ).Turn();
  }
  g_numPSu.SetValue( (int)psMap.size() );
  g_numVSu.SetValue( (int)vsMap.size() );
  g_numPSt.SetValue( (int)psMapTotal.size() );
  g_numVSt.SetValue( (int)vsMapTotal.size() );
  g_numPairu.SetValue( (int)pairMap.size() );
  g_numPairt.SetValue( (int)pairMapTotal.size() );

  psMap.clear(); vsMap.clear();
  pairMap.clear();
  sortMap.clear();
#endif // USE_RENDER_SHADER_STATS

  // Снимок кадра самим клиентом (docs/PLAN_NVRHI.md §2.13): см. s_autoScreenshotAt.
  {
    static int s_frameNo = 0;
    const int frame = ++s_frameNo;
    if ( s_autoScreenshotAt > 0 &&
         ( frame == s_autoScreenshotAt || frame == s_autoScreenshotAt + 100 ) )
    {
      Renderer* pR = GetRenderer();
      if ( !pR || !pR->GetColorSurface() )
      {
        DebugTrace( "AutoShot: frame=%d пропущен — устройства/поверхности кадра ещё нет", frame );
      }
      else
      {
        char name[96];
        // 1) поверхность, в которую нарисован кадр, с альфой — путь DumpScreenshot
        sprintf_s(name, "pw_auto_scene_%d.png", frame);
        DumpScreenshot(name, true);
        // 2) бэк-буфер целиком, альфа отбрасывается — путь SocialConnection
        sprintf_s(name, "pw_auto_bb_%d.png", frame);
        const bool bb = pR->SaveScreenshotToFile(name, 0, 0, 0, 0, false);
        // 3) кроп, заведомо выходящий за кадр, — проверяется ограничение
        //    прямоугольника (иначе это молча отказ или чтение чужой памяти)
        D3DSURFACE_DESC sd;
        bool crop = false;
        if ( SUCCEEDED( Get( pR->GetColorSurface() )->GetDesc(&sd) ) )
        {
          sprintf_s(name, "pw_auto_crop_%d.png", frame);
          crop = pR->SaveScreenshotToFile(name, (int)sd.Width - 200,
                                          (int)sd.Height - 200, 400, 400, true);
        }
        DebugTrace( "AutoShot: frame=%d bb=%d crop=%d (bb/crop — бэк-буфер, scene — поверхность кадра)",
                    frame, bb ? 1 : 0, crop ? 1 : 0 );
      }
    }
  }
  NullLastPointers();
}

bool IsResourceBound(const IUnknown* const ptr)
{
  if((ptr == pLastIndexBuffer)||(ptr == pLastVertexShader)||(ptr == pLastPixelShader)
     ||(ptr == pLastVertexDeclaration)||(ptr == Get(curDepthSurface) ))
    return true;

	for(UINT n = 0; n < MAX_COLOR_RT; ++n) 
		if( ptr == Get(curColorSurface[n]) )
      return true;

	for(UINT n = 0; n < lastTexturesPtrs.capacity(); ++n)
		if( const Texture *pTex = lastTexturesPtrs[n].pTex )
		  if( ptr == Get(pTex->GetDXTexture()) )
        return true;

  for(UINT n = 0; n < MAX_STREAMS_COUNT; ++n)
	  if( ptr == lastVertexBuffersPtrs[n] )
      return true;

    return false;
}


void Init()
{
	NullThePointers();

	RenderResourceManager::Init();
}

///
void BindVertexBufferRaw(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
	NI_VERIFY(streamNumber < MAX_STREAMS_COUNT, "Invalid vertex stream number!", return; );
	NI_VERIFY(buffer != 0, "Invalid vertex buffer!", return; );

	if (lastVertexBuffersPtrs[streamNumber] == buffer && lastVertexBuffersOffsets[streamNumber] == offset) 
		return;

	lastVertexBuffersPtrs[streamNumber] = buffer;
  lastVertexBuffersOffsets[streamNumber] = offset;
	HRESULT hr = GetBackend()->SetStreamSource(streamNumber, MakeBackendHandle<HBuffer>(buffer), offset, stride);
  NI_DX_THROW(hr, "BindVertexBufferRaw");
}

///
void BindVertexBuffer(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
  if(instancingEnabled && streamNumber)
    ++streamNumber;
  BindVertexBufferRaw(streamNumber, buffer, stride, offset);
}

void BindInstanceVB(unsigned int offset)
{
  BindVertexBufferRaw(1, s_pInstanceIndexVB.Get(), 4, offset);
}

///
void UnBindVertexBufferRaw(unsigned int streamNumber)
{
  NI_VERIFY(streamNumber < MAX_STREAMS_COUNT, "Invalid vertex stream number!", return; );

  if(!lastVertexBuffersPtrs[streamNumber]) 
    return;

  lastVertexBuffersPtrs[streamNumber] = 0;
  lastVertexBuffersOffsets[streamNumber] = 0;
  GetBackend()->SetStreamSource(streamNumber, HBuffer{}, 0, 0);
}

///
void UnBindVertexBuffer(unsigned int streamNumber)
{
  if(instancingEnabled && streamNumber)
    ++streamNumber;
  UnBindVertexBufferRaw(streamNumber);
}

///
void EnableHardwareInstancing(UINT _numInstances)
{
  NI_VERIFY(_numInstances, "Invalid instances number!", return; );

  // Счётчик уходит бэкенду (там D3D9-частоты потоков, в Vulkan — count в
  // drawIndexedInstanced). Локальный флаг отвечает за соглашение клиента о
  // раскладке: поток 1 занят под данные экземпляра, поэтому геометрийные
  // потоки сдвигаются (BindVertexBuffer) и берётся инстансный вариант
  // декларации (BindVertexDeclaration).
  instancingEnabled = true;
  GetBackend()->SetInstanceCount(_numInstances);
}

///
void DisableHardwareInstancing()
{
  instancingEnabled = false;
  GetBackend()->SetInstanceCount(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FVF (fixed-function input layout) в клиенте больше не используется:
// D3DFVF_XYZ разворачивается в обычную vertex declaration. Остался один
// fixed-function путь — RenderDeferred2D() ниже (debug-оверлеи), он требует
// собственной пары шейдеров, см. docs/PLAN_NVRHI.md.
void SetVertexFormatXYZ()
{
  static VertexFormatDescriptor descr = []
  {
    VertexFormatDescriptor d;
    d.AddVertexElement(VertexElementDescriptor(0, 0, VERTEXELEMENTTYPE_FLOAT3, VERETEXELEMENTUSAGE_POSITION, 0));
    return d;
  }();

  const DXVertexDeclarationRef & decl = GetVertexFormatDeclaration(descr);
  BindVertexDeclarationRaw(Get(decl));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _DEBUG
  //#define _DEBUG_VIEWPORT
#endif // _DEBUG

void DrawIndexedPrimitive(const DipDescriptor& _descr)
{
  if(nullrender)
    return;

#ifdef _DEBUG_VIEWPORT
  D3DVIEWPORT9 vp;
  GetDevice()->GetViewport(&vp);
#endif // _DEBUG_VIEWPORT

  NI_VERIFY(_descr.numVertices, "null dip", return; );
  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetBackend()->DrawIndexed( _descr.primitiveType,
    _descr.baseVertexIndex, _descr.minIndex, _descr.numVertices, _descr.startIndex, _descr.primitiveCount );

  //NI_ASSERT(hr == D3D_OK, NStr::StrFmt("DrawIndexedPrimitive() failed! code: 0x%08X, error: %s description: %s", hr, DXGetErrorStringA(hr), DXGetErrorDescriptionA(hr)) );
  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawIndexedPrimitive");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  /* {
    IDirect3DDevice9* const device = GetDevice();
    DWORD val;
    hr = device->GetRenderState(D3DRS_STENCILENABLE, &val);
    hr = device->GetRenderState(D3DRS_STENCILFAIL, &val);
    hr = device->GetRenderState(D3DRS_STENCILZFAIL, &val);
    hr = device->GetRenderState(D3DRS_STENCILPASS, &val);
    hr = device->GetRenderState(D3DRS_STENCILFUNC, &val);
    hr = device->GetRenderState(D3DRS_STENCILREF, &val);
    hr = device->GetRenderState(D3DRS_STENCILMASK, &val);
    hr = device->GetRenderState(D3DRS_STENCILWRITEMASK, &val);

    hr = device->GetRenderState(D3DRS_ZFUNC, &val);
    hr = hr;
  } */

  return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DrawIndexedPrimitiveUP(const DipDescriptor& _descr, const WORD* pIndexData,
                            const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  if(nullrender)
    return;

  NI_VERIFY(_descr.numVertices, "null dip", return; );
  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetBackend()->DrawIndexedUP( _descr.primitiveType,
                  _descr.minIndex, _descr.numVertices, _descr.primitiveCount,
                  pIndexData, true /* D3DFMT_INDEX16 */, pVertexStreamZeroData, VertexStreamZeroStride);

  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawIndexedPrimitiveUP");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DrawPrimitive(const DipDescriptor& _descr)
{
  if(nullrender)
    return;

#ifdef _DEBUG_VIEWPORT
  D3DVIEWPORT9 vp;
  GetDevice()->GetViewport(&vp);
#endif // _DEBUG_VIEWPORT

  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetBackend()->Draw( _descr.primitiveType,
    _descr.startIndex, _descr.primitiveCount );

  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawPrimitive");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  return;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DrawPrimitiveUP(const DipDescriptor& _descr, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
  if(nullrender)
    return;

  NI_VERIFY(_descr.primitiveCount, "null dip", return;);
  HRESULT hr = GetBackend()->DrawUP(_descr.primitiveType,
                                            _descr.primitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
  if(s_DXWarnLevel)
    NI_DX_WARN(hr, "DrawPrimitiveUP");
  triangleCount += _descr.primitiveCount;
  ++dipCount;

  return;
}

///
void BindIndexBuffer(IDirect3DIndexBuffer9* buffer)
{
	NI_VERIFY(buffer != 0, "Invalid index buffer!", return; );
	if (pLastIndexBuffer == buffer) 
		return;

	pLastIndexBuffer = buffer;
	HRESULT hr = GetBackend()->SetIndices(MakeBackendHandle<HBuffer>(pLastIndexBuffer));
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindIndexBuffer");
}

///
const DXVertexDeclarationRef & GetVertexFormatDeclaration(const VertexFormatDescriptor& descr)
{
	VertexDeclarationsCashIterator it = VertexDeclaraionsCash.find(descr);
	if ( it != VertexDeclaraionsCash.end() )
		return it->second;

	VertexDeclaraionsCash[descr] = GetRenderer()->CreateVertexFormatDeclaration(descr);
	return VertexDeclaraionsCash[descr];
}

///
void BindVertexDeclarationRaw(IDirect3DVertexDeclaration9 *pDecl)
{
	if(pLastVertexDeclaration == pDecl) 
		return;

  HRESULT hr = GetBackend()->SetInputLayout(MakeBackendHandle<HInputLayout>(pDecl));
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindVertexDeclarationRaw");
	pLastVertexDeclaration = pDecl;
}

///
void BindVertexDeclaration(DXVertexDeclarationRef const &pDecl)
{
	IDirect3DVertexDeclaration9 *decl = Get(pDecl);
  if(instancingEnabled)
    decl = s_instancedVDecl.Get(decl);

	BindVertexDeclarationRaw(decl);
}


///
void Release()
{
	VertexDeclaraionsCash.clear();
	RenderResourceManager::Release();

	NullThePointers();
}

///
void BindTexture(unsigned int samplerIndex, const Texture* pTexture, bool bProtect)
{
	if (!pTexture) // @BVS@MAT
		return;
	NI_ASSERT(!lastTexturesPtrs[samplerIndex].bProtected || bProtect, "Polluting protected sampler");
	if ( lastTexturesPtrs[samplerIndex].pTex == pTexture) 
		return;
	
	lastTexturesPtrs[samplerIndex].pTex       = pTexture;
#ifdef _DEBUG
	lastTexturesPtrs[samplerIndex].bProtected = bProtect;
#endif
  HRESULT hr = GetBackend()->SetTexture( samplerIndex, MakeBackendHandle<HTexture>(Get(pTexture->GetDXTexture())) );
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindTexture");
}

///
void UnBindTexture(unsigned int samplerIndex)
{
	if( !lastTexturesPtrs[samplerIndex].pTex ) 
		return;
	
	lastTexturesPtrs[samplerIndex].pTex = 0;
	GetBackend()->SetTexture(samplerIndex, HTexture{});
}

///
void UnBindTexture(const Texture* texture)
{
  for(uint n = 0; n <= Sampler::MAX_PS_SAMPLER_INDEX; ++n)
    if(lastTexturesPtrs[n].pTex == texture) {
      UnBindTexture(n);
      break;
    }
}

///
void BindVertexShader(IDirect3DVertexShader9 *shader)
{
	NI_ASSERT(shader, "pointer to vertex shader is not valid!");
	if(pLastVertexShader == shader)
		return;

#ifdef USE_RENDER_SHADER_STATS
  vsMap.insert(shader);
  vsMapTotal.insert(shader);
  g_numVS.AddValue( 1 );
#endif // USE_RENDER_SHADER_STATS

  pLastVertexShader = shader;
  GetBackend()->SetVertexShader(MakeBackendHandle<HShader>(shader));

#ifdef USE_RENDER_SHADER_STATS
  NoteShaderPair();
#endif
}

/// 
void BindPixelShader(IDirect3DPixelShader9 *shader)
{
	NI_ASSERT(shader, "pointer to pixel shader is not valid!");
	if(pLastPixelShader == shader)
    return;

#ifdef USE_RENDER_SHADER_STATS
  psMap.insert(shader);
  psMapTotal.insert(shader);
  g_numPS.AddValue( 1 );
#endif // USE_RENDER_SHADER_STATS

	pLastPixelShader = shader;
	GetBackend()->SetPixelShader(MakeBackendHandle<HShader>(shader));

#ifdef USE_RENDER_SHADER_STATS
  NoteShaderPair();
#endif
}


void BindRenderTargetColor( unsigned int renderTargetIndex, IDirect3DSurface9* pColorSurface )
{
  if ( pColorSurface != curColorSurface[renderTargetIndex] )
	{
    HRESULT hr = GetBackend()->SetRenderTarget( renderTargetIndex, MakeBackendHandle<HSurface>(pColorSurface) );
    curColorSurface[renderTargetIndex] = pColorSurface;
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "BindRenderTargetColor");

    if (0 == renderTargetIndex)
    {
      D3DSURFACE_DESC rtDesc;
      pColorSurface->GetDesc(&rtDesc);
      curColorSurfaceWidth = rtDesc.Width;
      curColorSurfaceHeight = rtDesc.Height;
    }
	}
}

void BindRenderTargetDepth( IDirect3DSurface9* pDepthStencilSurface )
{
	if ( pDepthStencilSurface != curDepthSurface )
	{
    HRESULT hr = GetBackend()->SetDepthStencil(MakeBackendHandle<HSurface>(pDepthStencilSurface));
		curDepthSurface = pDepthStencilSurface;
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "BindRenderTargetDepth");

    if(curDepthSurface) {
      D3DSURFACE_DESC desc;
      curDepthSurface->GetDesc(&desc);
      curDepthSurfaceWidth = desc.Width;
      curDepthSurfaceHeight = desc.Height;
    }
	}
}

void SetDefaultRenderTarget(const DXSurfaceRef &pRT0, const DXSurfaceRef &pDepth)
{
	pDefaultRT0 = ::Get(pRT0);
	pDefaultRT1 = 0;
	pDefaultDepth = ::Get(pDepth);
}

void SetDefaultRenderTarget(const DXSurfaceRef &pRT0, const DXSurfaceRef &pRT1, const DXSurfaceRef &pDepth)
{
	pDefaultRT0 = ::Get(pRT0);
	pDefaultRT1 = ::Get(pRT1);
	pDefaultDepth = ::Get(pDepth);
}

void BindRenderTargetDefault()
{
	BindRenderTargetColor(0, pDefaultRT0);
	BindRenderTargetColor(1, pDefaultRT1);
	BindRenderTargetDepth(pDefaultDepth);

  if(useViewport) {
    HRESULT hr = GetBackend()->SetViewport( MakeBackendViewport(viewport) ); // smirnov [2026-10-13] seam: viewport-кэш пока D3D9-типизирован, конвертация на месте
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "BindRenderTargetDefault");
  }
}

void BindRenderTarget(const Texture2DRef& pColorTexture)
{
  NI_VERIFY(pColorTexture, "Texture Ref Should be valid!", return;);
  const DXSurfaceRef &pDXColorSurface = pColorTexture->GetSurface(0);
  BindRenderTargetColor(0, pDXColorSurface);
	BindRenderTargetColor(1, 0);
	BindRenderTargetDepth(0);
  FixViewport();
}

void BindRenderTarget(const Texture2DRef& pColorTexture, const RenderSurfaceRef& pDepthSurface)
{
	const DXSurfaceRef &pDXColorSurface = pColorTexture->GetSurface(0);
	const DXSurfaceRef &pDXDepthSurface = pDepthSurface->GetDXSurface();
	BindRenderTargetColor(0, pDXColorSurface);
	BindRenderTargetColor(1, 0);
	BindRenderTargetDepth(pDXDepthSurface);
  FixViewport();
}

IDirect3DSurface9* GetRenderTarget(unsigned int renderTargetIndex)
{
	return curColorSurface[renderTargetIndex];
}

IDirect3DSurface9* GetRenderTargetDepth()
{
	return curDepthSurface;
}

int GetRenderTargetWidth()
{
  return curColorSurfaceWidth;
}

int GetRenderTargetHeight()
{
  return curColorSurfaceHeight;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// smirnov [2009/7/15]: it's better to place these functions into Render::Renderer
// just because SmartRenderer is a *CACHE* for dx resources like textures, targets 
// and buffers. It has nothing to do with other render functionality.
// BEGIN {

static int screenshotCount = 0;

void DumpScreenshot( const nstl::string& filename, bool keepAlpha )
{
  // Формат выбирается по расширению (png/bmp/dds); jpg/hdr сворачиваются в png
  // (их умел только D3DX, а lossy-формат для визуальной сверки кадров не годится),
  // см. Render/screenshot.h. Источник — поверхность, в которую нарисован кадр,
  // а не бэк-буфер: как и раньше, чтобы снимок включал всё нарисованное.
  // keepAlpha=false раньше делал копию в X8R8G8B8 через D3DXLoadSurfaceFromSurface;
  // теперь альфу отбрасывает пишущий код.
  // NI_DX_THROW разворачивается в if (...) без блоков, поэтому ветки обязаны
  // быть в фигурных скобках — иначе висячий else (C2181).
  if ( !GetRenderer()->SaveFrameToFile( filename.c_str(), keepAlpha, NULL, /*useBackBuffer=*/false ) ) {
    NI_DX_THROW( E_FAIL, "DumpScreenshot" );
  } else {
    ++screenshotCount;
  }
}

int GetScreenshotCount()
{
  return screenshotCount;
}

void SetMainViewport( int x, int y, int width, int height ) // smirnov [2009/7/15]: why "Main"?
{
  viewport.X = x;
  viewport.Y = y;
  viewport.Width = width;
  viewport.Height = height;
  viewport.MinZ = 0;
  viewport.MaxZ = 1;

  useViewport = true;
}

void GetMainViewport( int& x, int& y, int& width, int& height )
{
  BackendViewport vp = MakeBackendViewport(viewport);
  if(!useViewport)
    GetBackend()->GetViewport(vp);

  x = vp.x;
  y = vp.y;
  width = vp.width;
  height = vp.height;
}

void ResetMainViewport()
{
  HRESULT hr = GetBackend()->SetViewport( MakeBackendViewport(viewport) );
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "ResetMainViewport");
}

void FixViewport() // clip viewport size by depth-stencil's surface size
{
  if(!curDepthSurface)
    return;

  const bool smallViewPort = (curColorSurfaceWidth > viewport.Width || curColorSurfaceHeight > viewport.Height);
  if(useViewport && smallViewPort) {
    HRESULT hr = GetBackend()->SetViewport(MakeBackendViewport(viewport));
    if(s_DXWarnLevel > 1)
      NI_DX_WARN(hr, "FixViewport");
  }
}

void SetUseMainViewport( bool _useViewport ) // smirnov [2009/7/15]: make it return previous value
{
  useViewport = _useViewport; // smirnov [2009/7/15]: who will restore viewport to original value? will wait until next SetRenderTarget()?
}

bool UseMainViewport()
{
  return useViewport;
}

// } END

}; // namespace SmartRenderer
}; // namespace Render
