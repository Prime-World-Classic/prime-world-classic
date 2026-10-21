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

// 2026-10-21 (PLAN_NVRHI, «белая плашка»): мост к последнему затребованному
// материалу — определяется в multishader.cpp, см. NoteDipTrace ниже.
extern const char *GetLastDipMaterial();
extern unsigned int GetLastDipIndex();

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

// Период серии снимков (0 — только кадры at и at+100). Нужен для гейта
// паритета Vulkan/DXVK: номер кадра != время игры (разная частота кадров у
// бэкендов), поэтому сопоставить два прогона можно только по серии и
// наилучшему совпадению (docs/PLAN_NVRHI.md, гейт паритета).
static int s_autoScreenshotStep = 0;
REGISTER_VAR( "auto_screenshot_step", s_autoScreenshotStep, STORAGE_NONE );

// Снимки по ВРЕМЕНИ от первого кадра (секунды), а не по номеру кадра. Номер
// кадра двух прогонов не сопоставим: частота у DXVK и Vulkan различается в разы
// (замер 2026-10-20: DXVK ~5,5 к/с на этом стенде против ~40 у Vulkan), а мир
// живёт по сетевым тикам, то есть по часам. Период — auto_screenshot_step
// (секунды, по умолчанию 20).
static int s_autoScreenshotTime = 0;
REGISTER_VAR( "auto_screenshot_time", s_autoScreenshotTime, STORAGE_NONE );

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

  // Ключ пайплайна (шаг 8 очереди). Хэш, а не кортеж: для замера размера кэша
  // важна только мощность множества, а при ~10^4 ключей ложные слияния 64-бит
  // хэша пренебрежимы (~5e-12).
  hash_set<UINT64, PairHash> pipeSemMap, pipeSemMapTotal, pipeRawMap, pipeRawMapTotal;
  // Рисований за кадр: нужно для расчёта кольца uniform-буферов (шаг 7, §2.9 —
  // цена перекачки констант на рисование) и для того, чтобы число ключей
  // читалось как «переключений на кадр», а не абстракция.
  unsigned pipeDrawsFrame = 0, pipeDrawsTotal = 0;
  // Шаг 9 очереди: раскладка вершин как часть ключа. До шага 9 инстансный
  // вариант раскладки был отдельной декларацией (клиент подставлял её в
  // BindVertexDeclaration), и один материал давал в ключе две раскладки;
  // теперь декларация одна — сдвиг потоков и атрибут экземпляра живут в
  // бэкенде. Меряем число уникальных раскладок и число рисований инстансным
  // путём (покрывается ли инстансинг вообще на E2E-стенде).
  hash_set<UINT64, PairHash> pipeDeclMap, pipeDeclMapTotal;
  unsigned pipeInstDrawsFrame = 0, pipeInstDrawsTotal = 0;
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
  // Счётчик экземпляров 0..N-1 жил здесь (VB + подмена декларации) до шага 9
  // очереди: теперь и буфер, и вариант раскладки с атрибутом номера экземпляра
  // принадлежат бэкенду (docs/PLAN_NVRHI.md §2.10).

///////////////////////////////////// 
static nstl::hash_map< VertexFormatDescriptor, DXVertexDeclarationRef > VertexDeclaraionsCash;
typedef nstl::hash_map< VertexFormatDescriptor, DXVertexDeclarationRef >::iterator VertexDeclarationsCashIterator;

// Верхняя граница ЛОГИЧЕСКИХ номеров потоков, которые клиент просит забиндить
// (kInstanceStream — отдельный способ задать источник, см. renderbackend.h).
// Физические слоты и дедуп биндингов — за бэкендом: таблицы потоков здесь
// больше нет, потому что слот выбирает бэкенд.
static const unsigned int MAX_STREAMS_COUNT = 16;
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

// Форматы целей рисования. Нужны для ключа пайплайна (шаг 8 очереди,
// docs/PLAN_NVRHI.md): на Vulkan формат RT/DS входит в объект пайплайна, и
// без него число вариаций недооценено. Снимаются при назначении цели — это
// редкая операция в отличие от рисования. Замечание: формат бэкбуфера сюда не
// попадает (он ставится не через BindRenderTargetColor), но он постоянен,
// поэтому на число уникальных ключей не влияет.
static D3DFORMAT curColorSurfaceFormat[MAX_COLOR_RT] = { D3DFMT_UNKNOWN };
static D3DFORMAT curDepthSurfaceFormat = D3DFMT_UNKNOWN;
static DWORD curColorSurfaceWidth = 1, curColorSurfaceHeight = 1;
static DWORD curDepthSurfaceWidth = 1, curDepthSurfaceHeight = 1;
static IDirect3DSurface9* curDepthSurface( 0 );

static IDirect3DSurface9* pDefaultRT0( 0 );
static IDirect3DSurface9* pDefaultRT1( 0 );
static IDirect3DSurface9* pDefaultDepth( 0 );

static D3DVIEWPORT9 viewport = { 0 };

namespace {
  bool useViewport = false;
  // Признак включённого инстансного пути. На решение о раскладке потоков он
  // больше не влияет (сдвиг и атрибут экземпляра — за бэкендом, шаг 9 очереди);
  // остаётся как часть ключа пайплайна (NotePipeline) и для диагностики.
  bool instancingEnabled = false;
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
  // Таблица биндингов потоков живёт в бэкенде (он выбирает слоты) — её чистит
  // D3D9Backend::InvalidateBoundState.
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
    // Ключ пайплайна (шаг 8): sem — по семантике состояний, raw — по всем
    // теневым D3DRS_*. «Frame» — число уникальных ключей в одном кадре:
    // именно столько переключений пайплайна приходится на кадр, если кэш
    // устроен по этому ключу.
    DebugTrace( "PipeStats: semTotal=%d semFrame=%d rawTotal=%d rawFrame=%d drawsFrame=%d drawsTotal=%d declTotal=%d declFrame=%d instDrawsFrame=%d instDrawsTotal=%d",
      (int)pipeSemMapTotal.size(), (int)pipeSemMap.size(),
      (int)pipeRawMapTotal.size(), (int)pipeRawMap.size(),
      (int)pipeDrawsFrame, (int)pipeDrawsTotal,
      (int)pipeDeclMapTotal.size(), (int)pipeDeclMap.size(),
      (int)pipeInstDrawsFrame, (int)pipeInstDrawsTotal );
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
  pipeSemMap.clear();
  pipeRawMap.clear();
  pipeDeclMap.clear();
  pipeDrawsFrame = 0;
  pipeInstDrawsFrame = 0;
#endif // USE_RENDER_SHADER_STATS

  // Снимок кадра самим клиентом (docs/PLAN_NVRHI.md §2.13): см. s_autoScreenshotAt.
  {
    static int s_frameNo = 0;
    const int frame = ++s_frameNo;
    const int shotStep = s_autoScreenshotStep > 0 ? s_autoScreenshotStep : 0;
    const bool shotHit = s_autoScreenshotAt > 0 &&
        ( frame == s_autoScreenshotAt || frame == s_autoScreenshotAt + 100 ||
          ( shotStep > 0 && frame > s_autoScreenshotAt &&
            ( ( frame - s_autoScreenshotAt ) % shotStep ) == 0 ) );
    static unsigned s_shotTick = 0;
    static int s_lastShotSec = -1;
    if ( !s_shotTick ) s_shotTick = GetTickCount();
    const int elSec = (int)((GetTickCount() - s_shotTick) / 1000u);
    const int timeStep = shotStep > 0 ? shotStep : 20;
    const bool timeHit = s_autoScreenshotTime > 0 && elSec >= s_autoScreenshotTime &&
        ( ( elSec - s_autoScreenshotTime ) % timeStep ) == 0 && elSec != s_lastShotSec;
    if ( shotHit || timeHit )
    {
      if ( timeHit ) s_lastShotSec = elSec;
      char tag[32];
      if ( timeHit && !shotHit ) snprintf( tag, sizeof(tag), "t%d", elSec );
      else                      snprintf( tag, sizeof(tag), "%d", frame );
      Renderer* pR = GetRenderer();
      if ( !pR || !pR->GetColorSurface() )
      {
        DebugTrace( "AutoShot: frame=%d пропущен — устройства/поверхности кадра ещё нет", frame );
      }
      else
      {
        char name[96];
        // 1) поверхность, в которую нарисован кадр, с альфой — путь DumpScreenshot
        sprintf_s(name, "pw_auto_scene_%s.png", tag);
        DumpScreenshot(name, true);
        // 2) бэк-буфер целиком, альфа отбрасывается — путь SocialConnection
        sprintf_s(name, "pw_auto_bb_%s.png", tag);
        const bool bb = pR->SaveScreenshotToFile(name, 0, 0, 0, 0, false);
        // 3) кроп, заведомо выходящий за кадр, — проверяется ограничение
        //    прямоугольника (иначе это молча отказ или чтение чужой памяти)
        IRenderBackend::BackendTextureInfo sd;
        bool crop = false;
        // Описание поверхности — через seam: ручка бэкенда, GetDesc по ней =
        // вызов по чужой vtable под другим бэкендом (шаг 13.4c).
        if ( SUCCEEDED( GetBackend()->GetSurfaceInfo(MakeBackendHandle<HSurface>(Get(pR->GetColorSurface())), &sd) ) )
        {
          sprintf_s(name, "pw_auto_crop_%s.png", tag);
          crop = pR->SaveScreenshotToFile(name, (int)sd.width - 200,
                                          (int)sd.height - 200, 400, 400, true);
        }
        DebugTrace( "AutoShot: frame=%d tag=%s bb=%d crop=%d (bb/crop — бэк-буфер, scene — поверхность кадра)",
                    frame, tag, bb ? 1 : 0, crop ? 1 : 0 );
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

  if (GetBackend()->IsStreamBufferBound(ptr))
    return true;

    return false;
}


void Init()
{
	NullThePointers();

	RenderResourceManager::Init();
}

///
// Биндинг геометрийного потока: номер — ЛОГИЧЕСКИЙ, физический слот выбирает
// бэкенд (шаг 9 очереди). Дедуп тоже за бэкендом: клиентская таблица по
// логическим номерам разъехалась бы с устройством, когда поток уезжает выше.
void BindVertexBufferRaw(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
	NI_VERIFY(streamNumber < MAX_STREAMS_COUNT, "Invalid vertex stream number!", return; );
	NI_VERIFY(buffer != 0, "Invalid vertex buffer!", return; );

	HRESULT hr = GetBackend()->SetStreamSource(streamNumber, MakeBackendHandle<HBuffer>(buffer), offset, stride);
  NI_DX_THROW(hr, "BindVertexBufferRaw");
}

///
void BindVertexBuffer(unsigned int streamNumber, IDirect3DVertexBuffer9* buffer, unsigned int stride, unsigned int offset)
{
  BindVertexBufferRaw(streamNumber, buffer, stride, offset);
}

// Поток данных экземпляра. Счётчик 0..N-1 — встроенный (его VB и атрибут
// раскладки строит бэкенд); per-instance данные клиента — второй вариант
// (партиклы), их атрибуты уже описаны раскладкой потоком kInstanceStream.
void BindInstanceCounter()
{
  HRESULT hr = GetBackend()->SetInstanceSource(HBuffer(), 0, 0);
  NI_DX_THROW(hr, "BindInstanceCounter");
}

void BindInstanceSource(IDirect3DVertexBuffer9* pPerInstanceData, unsigned int stride, unsigned int offset)
{
  NI_VERIFY(pPerInstanceData != 0, "Invalid instance vertex buffer!", return; );
  HRESULT hr = GetBackend()->SetInstanceSource(MakeBackendHandle<HBuffer>(pPerInstanceData), stride, offset);
  NI_DX_THROW(hr, "BindInstanceSource");
}

///
void UnBindVertexBufferRaw(unsigned int streamNumber)
{
  NI_VERIFY(streamNumber < MAX_STREAMS_COUNT, "Invalid vertex stream number!", return; );

  GetBackend()->SetStreamSource(streamNumber, HBuffer{}, 0, 0);
}

///
void UnBindVertexBuffer(unsigned int streamNumber)
{
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

#ifdef USE_RENDER_SHADER_STATS
namespace
{
  // Полный ключ пайплайна — то, из чего на этапе 3 строится объект пайплайна.
  // Меряем две версии, чтобы увидеть цену «сырых» состояний:
  //  sem — пара (VS,PS) + раскладка + RenderState (семантика материалов) +
  //      сэмплеры + трафарет + форматы RT/DS + топология + признак инстансинга;
  //  raw — то же + хэш теневых D3DRS_* значений, т.е. всего, что пишут
  //      SetStateDirect (ShadowManager, PostFX, скачок, альфа-тест…).
  // Если raw заметно больше sem, состояния меняются чаще, чем материал, и
  // перед Vulkan-бэкендом их придётся нормализовать (иначе кэш пайплайнов
  // разрастается из-за чисел, которых на Vulkan нет).
  inline UINT64 PipeMix(UINT64 h, UINT64 v) { h ^= v; return h * 0x100000001B3ull; }

  inline void NotePipeline(ERenderPrimitiveType type)
  {
    RenderStatesManager* const sm = Render::GetStatesManager();
    UINT64 h = 0xcbf29ce484222325ull;
    h = PipeMix(h, (UINT64)(size_t)pLastVertexShader);
    h = PipeMix(h, (UINT64)(size_t)pLastPixelShader);
    h = PipeMix(h, (UINT64)(size_t)pLastVertexDeclaration);
    h = PipeMix(h, sm->GetCurrentState().GetBitMask());
    h = PipeMix(h, sm->GetSamplerShadowHash());
    h = PipeMix(h, sm->GetStencilShadowKey());
    h = PipeMix(h, (UINT64)type);
    h = PipeMix(h, instancingEnabled ? 1u : 0u);
    h = PipeMix(h, (UINT64)curColorSurfaceFormat[0]);
    h = PipeMix(h, (UINT64)curDepthSurfaceFormat);
    pipeSemMap.insert(h);
    pipeSemMapTotal.insert(h);
    pipeDeclMap.insert((UINT64)(size_t)pLastVertexDeclaration);
    pipeDeclMapTotal.insert((UINT64)(size_t)pLastVertexDeclaration);
    if (instancingEnabled) {
      ++pipeInstDrawsFrame;
      ++pipeInstDrawsTotal;
    }
    ++pipeDrawsFrame;
    ++pipeDrawsTotal;
    const UINT64 hraw = PipeMix(h, sm->GetRenderStateShadowHash());
    pipeRawMap.insert(hraw);
    pipeRawMapTotal.insert(hraw);
  }
}
#endif

// Модель состояний конвейера для бэкенда (шаг 13.4b). Перед каждым выводом:
// бэкенд строит по ней объект пайплайна, а менеджер пересобирает модель только
// после изменения состояний, поэтому здесь — копирование готовой структуры и
// хэш (замер шага 8: ~458 выводов на кадр в бою).
inline void NotePipelineState()
{
  RenderStatesManager* const sm = Render::GetStatesManager();
  if (sm)
    GetBackend()->SetPipelineState(sm->GetPipelineState(), sm->GetPipelineStateHash());
}

// 2026-10-21 (PLAN_NVRHI, «белая плашка»): трасса Dip'ов клиента.
// Вывод-плашка (UIFlashMaterial #0, 6 вершин, 2 примитива, вершинный цвет
// непрозрачный белый) есть в кадре Vulkan и отсутствует в кадре D3D9/DXVK при
// тех же константах и состоянии. Чтобы отличить «клиент не выпускает Dip» от
// «бэкенд его теряет», нужен список Dip'ов самого клиента — он одинаков для
// обоих бэкендов. Включается env PW_DRAW_TRACE=1; печатаются только мелкие
// Dip'ы (prim<=8), иначе лог разрастается текстом HUD.
static void NoteDipTrace(const DipDescriptor& _descr, bool _indexed)
{
  static int s_on = -1;
  if (s_on < 0)
  {
    char b[16];
    s_on = (GetEnvironmentVariableA("PW_DRAW_TRACE", b, sizeof(b) - 1) > 0) ? atoi(b) : 0;
  }
  if (!s_on)
    return;
  static unsigned s_n = 0u, s_logged = 0u;
  ++s_n;
  // Печатаем только семейство UIFlash* — интересующий нас вывод-плашка
  // (prim=2, 6 вершин) тонет в тысячах текстовых Dip'ов HUD.
  const char *m = ::GetLastDipMaterial();
  if (_descr.primitiveCount > 8u || !m || !strstr(m, "Flash") || s_logged >= 8000u)
    return;
  ++s_logged;
  // Сравнение с трассой Vulkan-бэкенда (VkTraceOrd): вывод-плашка
  // (UIFlashMaterial #0, prim=2, 6 вершин) выпускается клиентом в ОБОИХ
  // бэкендах, но белым прямоугольником появляется только в Vulkan. Значит
  // расходится либо состояние устройства, либо геометрия. Печатаем состояние
  // D3D9-устройства в тот же момент (в Vulkan-пути устройства нет — блок
  // пропускается).
  IDirect3DDevice9* dev = GetDevice();
  if (dev)
  {
    IDirect3DSurface9* rt = 0;
    D3DVIEWPORT9 vp = {0u, 0u, 0u, 0u, 0.f, 1.f};
    RECT sr = {0, 0, 0, 0};
    DWORD ab = 0, sb = 0, db = 0, ze = 0, zw = 0, zf = 0, st = 0, sc = 0, cu = 0, cw = 0, at = 0, sa = 0;
    IDirect3DVertexShader9* vs = 0;
    IDirect3DPixelShader9* ps = 0;
    IDirect3DVertexDeclaration9* dc = 0;
    IDirect3DVertexBuffer9* vb = 0;
    UINT voff = 0, vstr = 0;
    dev->GetRenderTarget(0, &rt);
    dev->GetViewport(&vp);
    dev->GetScissorRect(&sr);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    dev->GetRenderState(D3DRS_SRCBLEND, &sb);
    dev->GetRenderState(D3DRS_DESTBLEND, &db);
    dev->GetRenderState(D3DRS_ZENABLE, &ze);
    dev->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    dev->GetRenderState(D3DRS_ZFUNC, &zf);
    dev->GetRenderState(D3DRS_STENCILENABLE, &st);
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &sc);
    dev->GetRenderState(D3DRS_CULLMODE, &cu);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
    dev->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &sa);
    dev->GetVertexShader(&vs);
    dev->GetPixelShader(&ps);
    dev->GetVertexDeclaration(&dc);
    dev->GetStreamSource(0, &vb, &voff, &vstr);
    // DebugTrace (PrintfFormatter) принимает не больше 10 аргументов —
    // собираем строку сами.
    char dt[512];
    snprintf( dt, sizeof(dt), "rt=%p vp=%u,%u %ux%u z[%g..%g] sc=%d,%d..%d,%d scTest=%u ab=%u sb=%u db=%u",
      (void*)rt, vp.X, vp.Y, vp.Width, vp.Height, (double)vp.MinZ, (double)vp.MaxZ,
      sr.left, sr.top, sr.right, sr.bottom, sc, ab, sb, db );
    DebugTrace( "D3dDip: %s", dt );
    snprintf( dt, sizeof(dt), "z=%u/%u f=%u stencil=%u cull=%u cw=%u aTest=%u sep=%u",
      ze, zw, zf, st, cu, cw, at, sa );
    DebugTrace( "D3dDip2: %s", dt );
    snprintf( dt, sizeof(dt), "vs=%p ps=%p decl=%p vb=%p+%u str=%u",
      (void*)vs, (void*)ps, (void*)dc, (void*)vb, voff, vstr );
    DebugTrace( "D3dDip3: %s", dt );
    // цель — бэкбуфер или off-screen слой? в Vulkan-трассе (VkTraceOrd) те же
    // Dip'ы идут в bb=0 (off-screen поверхность #128), т.е. в UI-слой.
    IDirect3DSurface9* bb = 0;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
    {
      snprintf( dt, sizeof(dt), "back=%p rtIsBack=%u", (void*)bb, rt && bb && rt == bb ? 1u : 0u );
      DebugTrace( "D3dDip4: %s", dt );
    }
    if (bb) bb->Release();

    // bbox первых вершин потока 0 (float2, шаг 16): вывод-плашка опознаётся по
    // x[60..8900] y[60..3620] — ровно такие же координаты печатает трасса
    // Vulkan-бэкенда (VkTraceVtx) для белых выводов.
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    if (vb && vstr == 16u && !_indexed)
    {
      void* vp = 0;
      if (SUCCEEDED(vb->Lock(voff, 0, &vp, 0)) && vp)
      {
        const float* f = (const float*)vp;
        for (unsigned k = 0u; k < 6u; ++k)
        {
          const float x = f[(voff / 16u + _descr.startIndex + k) * 4u + 0u];
          const float y = f[(voff / 16u + _descr.startIndex + k) * 4u + 1u];
          if (x < bx0) bx0 = x; if (x > bx1) bx1 = x;
          if (y < by0) by0 = y; if (y > by1) by1 = y;
        }
        vb->Unlock();
      }
    }
    // размер потока: D3D9 обрезает Dip'ы, выходящие за конец буфера потока
    // (вершины за концом = примитивы не рисуются), Vulkan-бэкенд такой проверки
    // не делает
    {
      D3DVERTEXBUFFER_DESC vbd;
      if (vb && SUCCEEDED(vb->GetDesc(&vbd)))
        DebugTrace( "D3dVb2: vbSize=%u need=%u (off %u + (%u+%u)*%u)",
                    (unsigned)vbd.Size, (unsigned)(voff + (_descr.startIndex + _descr.numVertices) * vstr),
                    voff, _descr.startIndex, _descr.numVertices, vstr );
    }
    DebugTrace( "D3dVb: x[%.1f..%.1f] y[%.1f..%.1f]", bx0, bx1, by0, by1 );

    // readback цели ПРЯМО ПОСЛЕ вывода-плашки: белым ли лёг квад в D3D9-слое
    static IDirect3DSurface9* stg = 0;
    static unsigned stgW = 0, stgH = 0, stgFmt = 0, s_rbLogged = 0u;
    if (rt && cw == 15u && bx1 > 8000.f && by1 > 3000.f && s_rbLogged < 6u)
    {
      ++s_rbLogged;
      D3DSURFACE_DESC rd;
      if (SUCCEEDED(rt->GetDesc(&rd)))
      {
        if (!stg || stgW != rd.Width || stgH != rd.Height || stgFmt != (unsigned)rd.Format)
        {
          if (stg) stg->Release();
          stg = 0; stgW = stgH = stgFmt = 0;
          if (SUCCEEDED(dev->CreateOffscreenPlainSurface(rd.Width, rd.Height, rd.Format,
                                                         D3DPOOL_SYSTEMMEM, &stg, 0)))
          { stgW = rd.Width; stgH = rd.Height; stgFmt = (unsigned)rd.Format; }
        }
        HRESULT hrRb = stg ? dev->GetRenderTargetData(rt, stg) : (HRESULT)0;
        if (stg && SUCCEEDED(hrRb))
        {
          D3DLOCKED_RECT lr;
          if (SUCCEEDED(stg->LockRect(&lr, 0, D3DLOCK_READONLY)))
          {
            const unsigned char* base = (const unsigned char*)lr.pBits;
            const unsigned px[4][2] = {{170u,500u},{60u,450u},{300u,560u},{600u,300u}};
            unsigned v[4];
            for (int k = 0; k < 4; ++k)
            {
              const unsigned char* q = base + (size_t)px[k][1] * (size_t)lr.Pitch
                                     + (size_t)px[k][0] * 4u;
              v[k] = (unsigned)q[0] | ((unsigned)q[1] << 8) | ((unsigned)q[2] << 16)
                   | ((unsigned)q[3] << 24);
            }
            unsigned white = 0u, opaque = 0u;
            unsigned gx0 = 0xFFFFFFFFu, gy0 = 0xFFFFFFFFu, gx1 = 0u, gy1 = 0u, green = 0u;
            for (unsigned y = 0u; y < rd.Height; ++y)
            {
              const unsigned char* row = base + (size_t)y * (size_t)lr.Pitch;
              for (unsigned x = 0u; x + 4u <= rd.Width * 4u; x += 4u)
              {
                if (row[x+3u] == 0xFFu)
                {
                  ++opaque;
                  if (row[x] > 0xF0u && row[x+1u] > 0xF0u && row[x+2u] > 0xF0u)
                    ++white;
                }
                // BGRA: зелёный пиксель = 00 FF 00 (любая альфа)
                if (row[x] < 0x40u && row[x+1u] > 0xC0u && row[x+2u] < 0x40u)
                {
                  ++green;
                  const unsigned px = x >> 2;
                  if (px < gx0) gx0 = px;
                  if (px > gx1) gx1 = px;
                  if (y < gy0) gy0 = y;
                  if (y > gy1) gy1 = y;
                }
              }
            }
            snprintf( dt, sizeof(dt), "%ux%u fmt=%u белых=%u непрозр=%u зелёных=%u "
                      "зелёный bbox=(%u,%u)-(%u,%u) (170,500)=%08X (300,560)=%08X",
                      rd.Width, rd.Height, (unsigned)rd.Format, white, opaque, green,
                      gx0, gy0, gx1, gy1, v[0], v[2] );
            DebugTrace( "D3dRb: %s", dt );
            stg->UnlockRect();
          }
        }
        else
          DebugTrace( "D3dRb: нет readback hr=%08X", (unsigned)hrRb );
      }
    }
    if (rt) rt->Release();
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (dc) dc->Release();
    if (vb) vb->Release();
  }
  DebugTrace( "DipTrace: n=%u %s mat=%s #%u prim=%u type=%u nv=%u startIdx=%u",
    s_n, _indexed ? "idx" : "vtx",
    m ? m : "?",
    ::GetLastDipIndex(), _descr.primitiveCount, (unsigned)_descr.primitiveType,
    _descr.numVertices, _descr.startIndex );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
#ifdef USE_RENDER_SHADER_STATS
  NotePipeline(_descr.primitiveType);
#endif
  NotePipelineState();
  HRESULT hr = GetBackend()->DrawIndexed( _descr.primitiveType,
    _descr.baseVertexIndex, _descr.minIndex, _descr.numVertices, _descr.startIndex, _descr.primitiveCount );
  // трасса ПОСЛЕ Dip'а: readback цели должен показывать результат вывода
  NoteDipTrace(_descr, true);

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
#ifdef USE_RENDER_SHADER_STATS
  NotePipeline(_descr.primitiveType);
#endif
  NotePipelineState();
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
#ifdef USE_RENDER_SHADER_STATS
  NotePipeline(_descr.primitiveType);
#endif
  NotePipelineState();
  // Эксперимент 2026-10-21: вывод-плашка (UIFlashMaterial #0, 6 вершин,
  // x[60..8900] y[60..3620]) клиент выпускает и в D3D9, и в Vulkan, состояние
  // и вершины одинаковы, но белым он становится только в Vulkan. Если выключить
  // смешивание (квад пишется начисто) и квад появится в readback слоя — значит
  // геометрия в D3D9 на месте, а PS даёт альфу 0 (SRCALPHA => невидим).
  IDirect3DDevice9* devFO = 0;
  bool forced = false;
  static int s_fo = -1;
  if (s_fo < 0)
  {
    char fb[16];
    s_fo = (GetEnvironmentVariableA("PW_D3D_FORCEOPAQUE", fb, sizeof(fb) - 1) > 0) ? atoi(fb) : 0;
  }
  if (s_fo && _descr.primitiveCount == 2u && _descr.numVertices == 6u)
  {
    const char* dm = ::GetLastDipMaterial();
    if (dm && strstr(dm, "lash") && ::GetLastDipIndex() == 0u)
    {
      IDirect3DDevice9* dev2 = GetDevice();
      IDirect3DVertexBuffer9* vb2 = 0;
      UINT off2 = 0u, str2 = 0u;
      if (dev2 && SUCCEEDED(dev2->GetStreamSource(0, &vb2, &off2, &str2)) && vb2 && str2 == 16u)
      {
        void* vp2 = 0;
        if (SUCCEEDED(vb2->Lock(off2, 0, &vp2, 0)) && vp2)
        {
          const float* f2 = (const float*)vp2;
          float x1 = -1e30f, y1 = -1e30f;
          // дамп записей вершин (шаг 16): POSITION float2 в 0..7, дальше
          // 8..15 — то, что клиент кладёт следом (цвет / uv)
          DebugTrace( "D3dVtx: %08X %08X %08X %08X | %08X %08X %08X %08X",
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 0u) * 4u + 0u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 0u) * 4u + 1u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 0u) * 4u + 2u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 0u) * 4u + 3u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 1u) * 4u + 0u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 1u) * 4u + 1u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 1u) * 4u + 2u],
            ((const unsigned*)vp2)[(off2 / 16u + _descr.startIndex + 1u) * 4u + 3u] );
          for (unsigned k = 0u; k < 6u; ++k)
          {
            const float x = f2[(off2 / 16u + _descr.startIndex + k) * 4u + 0u];
            const float y = f2[(off2 / 16u + _descr.startIndex + k) * 4u + 1u];
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
          }
          vb2->Unlock();
          // PW_D3D_FORCEOPAQUE=3 — положительный контроль: перекрашиваем ВСЕ
          // Dip'ы UIFlashMaterial #0 (не только плашку): если зелёный появится
          // в readback, значит и запись в VB, и readback работают
          if (s_fo == 3 || (x1 > 8000.f && y1 > 3000.f))
          {
            // состояние восстанавливаем сразу после Dip'а: клиент кэширует
            // D3DRS_* и сам бы его не вернул
            devFO = dev2;
            devFO->SetRenderState(D3DRS_ALPHABLENDENABLE, 0u);
            forced = true;
            // PW_D3D_FORCEOPAQUE=2: перекрашиваем вершины в чистый зелёный
            // (COLOR0 = 0xFF00FF00 в смещении 8) — по нему readback ищет
            // прямоугольник, который D3D9 реально растеризовал из этого Dip'а
            if (s_fo >= 2)
            {
              for (unsigned k = 0u; k < 6u; ++k)
              {
                unsigned char* pc = (unsigned char*)vp2
                  + (off2 / 16u + _descr.startIndex + k) * 16u + 8u;
                pc[0] = 0x00; pc[1] = 0xFF; pc[2] = 0x00; pc[3] = 0xFF;
              }
            }
            DebugTrace( "D3dForce: ALPHABLENDENABLE=0 для плашки x1=%.1f y1=%.1f", x1, y1 );
          }
        }
      }
      if (vb2) vb2->Release();
    }
  }
  HRESULT hr = GetBackend()->Draw( _descr.primitiveType,
    _descr.startIndex, _descr.primitiveCount );
  if (forced)
    devFO->SetRenderState(D3DRS_ALPHABLENDENABLE, 1u);
  NoteDipTrace(_descr, false);

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
#ifdef USE_RENDER_SHADER_STATS
  NotePipeline(_descr.primitiveType);
#endif
  NotePipelineState();
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
  // Дедуп по самой раскладке убран: при инстансном пути со счётчиком бэкенд
  // подставляет её вариант (InstancedVariant), и сверять надо именно
  // подставленную раскладку — а её знает только бэкенд.
  HRESULT hr = GetBackend()->SetInputLayout(MakeBackendHandle<HInputLayout>(pDecl));
  if(s_DXWarnLevel > 1)
    NI_DX_WARN(hr, "BindVertexDeclarationRaw");
	pLastVertexDeclaration = pDecl;
}

///
void BindVertexDeclaration(DXVertexDeclarationRef const &pDecl)
{
	BindVertexDeclarationRaw(Get(pDecl));
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
      IRenderBackend::BackendTextureInfo rtInfo;
      if (SUCCEEDED(GetBackend()->GetSurfaceInfo(MakeBackendHandle<HSurface>(pColorSurface), &rtInfo)))
      {
        curColorSurfaceWidth = rtInfo.width;
        curColorSurfaceHeight = rtInfo.height;
      }
    }
    {
      // Цель может быть и NULL (снятие MRT) — формат тогда «неизвестен».
      // Поймано на E2E-стенде: BindRenderTargetColor(..., 0) ронял клиент на
      // GetDesc() (чтение по нулевому указателю, exception.dmp в сессии
      // 2026.10.15-02.28.44).
      IRenderBackend::BackendTextureInfo fmtInfo;
      HRESULT hrFmt = pColorSurface ? GetBackend()->GetSurfaceInfo(MakeBackendHandle<HSurface>(pColorSurface), &fmtInfo) : D3DERR_INVALIDCALL;
      D3DFORMAT fmt = D3DFMT_UNKNOWN;
      BackendFormatToD3D(fmtInfo.format, &fmt);
      curColorSurfaceFormat[renderTargetIndex] = SUCCEEDED(hrFmt) ? fmt : D3DFMT_UNKNOWN;
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
      // Описание — через seam: под NvrhiBackend в curDepthSurface лежит ручка
      // бэкенда, GetDesc() по ней = вызов по чужой vtable. Формат идёт через
      // словарь конвертера: он же кормит ключ состояний конвейера (:565),
      // поэтому расхождение с молчаливым desc.Format недопустимо.
      IRenderBackend::BackendTextureInfo info;
      if (SUCCEEDED(GetBackend()->GetSurfaceInfo(MakeBackendHandle<HSurface>(curDepthSurface), &info)))
      {
        curDepthSurfaceWidth  = info.width;
        curDepthSurfaceHeight = info.height;
        D3DFORMAT fmt = D3DFMT_UNKNOWN;
        if (BackendFormatToD3D(info.format, &fmt))
          curDepthSurfaceFormat = fmt;
        else
          DebugTrace("BindRenderTargetDepth: seam-формат %d вне словаря D3D9", (int)info.format);
      }
      else
        DebugTrace("BindRenderTargetDepth: GetSurfaceInfo не удалось");
    }
    else
      curDepthSurfaceFormat = D3DFMT_UNKNOWN;
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
