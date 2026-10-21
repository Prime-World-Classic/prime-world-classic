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
// Вывод-плашка опознаётся bbox'ом вершин ДО матриц: в бою это x[0..3240]
// y[0..520] (замер VkSkipBB в Vulkan-трассе: пропуск ровно этого bbox снимает
// белых с 43 560 до 2). Прежний гейт x1>8000 && y1>3000 ловил квады ЭКРАНА
// ЗАГРУЗКИ (x[60..8900] y[60..3620]) — вывод «в D3D9 плашка не растеризуется»
// был получен на них, а не на выводах боевого кадра.
static bool IsPlateBbox(float x1, float y1)
{
  return x1 >= 3230.f && x1 <= 3250.f && y1 >= 510.f && y1 <= 530.f;
}

// readback цели и скан: белые/непрозрачные пиксели и bbox зелёных (зелёный =
// метка, которую ставит PW_D3D_FORCEOPAQUE=2/3). Вызывается и сразу после
// вывода-плашки, и с задержкой в несколько десятков Dip'ов — иначе не отличить
// «квад не растеризуется» от «квад нарисован, но его закрыли позже в кадре».
static unsigned ScanTarget(IDirect3DDevice9* dev, IDirect3DSurface9* rt, const char* tag)
{
  static IDirect3DSurface9* stg = 0;
  static unsigned stgW = 0, stgH = 0, stgFmt = 0;
  if (!dev || !rt)
    return 0u;
  D3DSURFACE_DESC rd;
  if (FAILED(rt->GetDesc(&rd)))
    return 0u;
  if (!stg || stgW != rd.Width || stgH != rd.Height || stgFmt != (unsigned)rd.Format)
  {
    if (stg) stg->Release();
    stg = 0; stgW = stgH = stgFmt = 0;
    if (SUCCEEDED(dev->CreateOffscreenPlainSurface(rd.Width, rd.Height, rd.Format,
                                                   D3DPOOL_SYSTEMMEM, &stg, 0)))
    { stgW = rd.Width; stgH = rd.Height; stgFmt = (unsigned)rd.Format; }
  }
  HRESULT hrRb = stg ? dev->GetRenderTargetData(rt, stg) : (HRESULT)0;
  if (!stg || FAILED(hrRb))
  {
    DebugTrace( "D3dRb%s: нет readback hr=%08X", tag, (unsigned)hrRb );
    return 0u;
  }
  D3DLOCKED_RECT lr;
  if (FAILED(stg->LockRect(&lr, 0, D3DLOCK_READONLY)))
    return 0u;
  const unsigned char* base = (const unsigned char*)lr.pBits;
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
  stg->UnlockRect();
  DebugTrace( "D3dRb%s: rt=%p %ux%u fmt=%u белых=%u непрозр=%u зелёных=%u",
              tag, (void*)rt, rd.Width, rd.Height,
              (unsigned)rd.Format, white, opaque, green );
  DebugTrace( "D3dRb%s: зелёный bbox=(%u,%u)-(%u,%u)", tag, gx0, gy0, gx1, gy1 );
  return green;
}

static void NoteDipTrace(const DipDescriptor& _descr, bool _indexed)
{
  // PW_D3D_TRACEDRAW=мин,макс[,задержка_сек] — дамп WORLD (c0..c3) и VIEW
  // (c4..c7) для Dip'ов, у которых ЧИСЛО ВЕРШИН ИЛИ ЧИСЛО ПРИМИТИВОВ попало в
  // диапазон. Диапазон по обоим полям потому, что число вершин двух сторон не
  // сопоставимо: D3D9 печатает _descr.numVertices, а seam — свой vtx (замер
  // ab14: один и тот же вывод — dx (nv=1559,prim=570), vk (nv=1710,prim=570);
  // совпали и W, и V дословно). Примитивы — надёжный ключ сопоставления.
  // Задержка — секунды от первого Dip'а процесса: без неё лимит трассы
  // съедался за 0,6 с загрузки карты и до героя очередь не доходила.
  // Пара на стороне seam'а — PW_VK_TRACEDRAW (nvrhibackend.cpp, DrawImpl).
  {
    static int s_lo = -1, s_hi = -1, s_delay = 0;
    static unsigned s_nd = 0u;
    static unsigned long long s_t0 = 0ull;
    if (s_lo < 0)
    {
      s_lo = 0; s_hi = -1;
      char b[48];
      if (GetEnvironmentVariableA( "PW_D3D_TRACEDRAW", b, sizeof(b) - 1 ) > 0)
      {
        int v[3] = { 0, 0, 0 };
        char* p = b;
        int i = 0;
        while (p && i < 3) { v[i++] = atoi(p); p = strchr(p, ','); if (p) ++p; }
        if (i >= 2) { s_lo = v[0]; s_hi = v[1]; }
        if (i == 3) { s_delay = v[2]; }
      }
    }
    if (!s_t0) s_t0 = GetTickCount64();
    if (s_hi >= s_lo && s_nd < 4000u
        && GetTickCount64() - s_t0 >= (unsigned long long)s_delay * 1000ull
        && ((_descr.numVertices >= (unsigned)s_lo && _descr.numVertices <= (unsigned)s_hi)
            || (_descr.primitiveCount >= (unsigned)s_lo && _descr.primitiveCount <= (unsigned)s_hi)))
    {
      IDirect3DDevice9* dev0 = GetDevice();
      // 104 float = c0..c25: c0..c3 WORLD, c4..c7 VIEW, c24..c25 — первые две
      // кости (BONES=c24, GlobalMasks.h:88) — сравнить с b24/b25 стороны seam'а
      float vc[1024] = { 0.f };
      if (dev0)
        (void)dev0->GetVertexShaderConstantF( 0, vc, 256 );
      // Дедуп по (nv,prim,W,V): в диапазоне 1600..1700 terrain-чанки BasicMaterial
      // дают 4000 одинаковых выводов за 18 с (замер ab13) и лимит трассы
      // съедают, не дойдя до героя. Печатаем каждый уникальный набор матриц
      // один раз.
      static unsigned long long s_seen[1024];
      static unsigned s_seenN = 0u;
      unsigned long long hv = 1469598103934665603ull;
      unsigned vals[2] = { _descr.numVertices, _descr.primitiveCount };
      const unsigned char* hb = (const unsigned char*)vals;
      for (unsigned k = 0u; k < sizeof(vals); ++k) { hv ^= hb[k]; hv *= 1099511628211ull; }
      hb = (const unsigned char*)vc;
      for (unsigned k = 0u; k < sizeof(vc); ++k) { hv ^= hb[k]; hv *= 1099511628211ull; }
      ++s_nd;   // лимит считается по ВСЕМ оттрассированным выводам: у
                // скелетных выводов кости меняются на каждом выводе, дедуп по
                // c-банку их не схлопывает (замер ab16: 100 тыс. строк, лог 97 МБ)
      bool fresh = true;
      for (unsigned k = 0u; k < s_seenN; ++k) if (s_seen[k] == hv) { fresh = false; break; }
      if (fresh && s_seenN < 1024u) s_seen[s_seenN++] = hv;
      if (fresh)
      {
      const char* mm = ::GetLastDipMaterial();
      // DebugTrace — это PrintfFormatter с маленьким лимитом аргументов
      // (17 не принимает), поэтому строка собирается snprintf'ом (тот же
      // приём, что у D3dProj/D3dC ниже)
      char dd[1400];
      int dn = snprintf(dd, sizeof(dd), "D3dDraw: nv=%u prim=%u idx=%u mat=%s "
                  "W=(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)"
                  "(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)",
                  _descr.numVertices, _descr.primitiveCount, _descr.startIndex,
                  mm ? mm : "-",
                  vc[0], vc[1], vc[2], vc[3], vc[4], vc[5], vc[6], vc[7],
                  vc[8], vc[9], vc[10], vc[11], vc[12], vc[13], vc[14], vc[15]);
      // snprintf возвращает ДЛИНУ ТОГО, ЧТО БЫЛО БЫ НАПИСАНО (не факт),
      // поэтому после каждого шага — зажим: иначе dd+dn вылезет за буфер.
      if (dn < 0 || dn >= (int)sizeof(dd)) dn = (int)sizeof(dd) - 1;
      dn += snprintf(dd + dn, sizeof(dd) - dn, " V=(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)"
                  "(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)",
                  vc[16], vc[17], vc[18], vc[19], vc[20], vc[21], vc[22], vc[23],
                  vc[24], vc[25], vc[26], vc[27], vc[28], vc[29], vc[30], vc[31]);
      if (dn < 0 || dn >= (int)sizeof(dd)) dn = (int)sizeof(dd) - 1;
      // VP = c8..c11 (VIEWPROJECTION, GlobalMasks.h:66) — именно её использует
      // BasicMaterial.hlsl:164 `mul(float4(WPos,1.0), ViewProjection)`, а НЕ
      // View (c4..c7): в ab15 совпали W и V, но размер модели от них не
      // зависит — масштаб задаёт VP, и её в трассе не было.
      dn += snprintf(dd + dn, sizeof(dd) - dn, " VP=(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)"
                  "(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)",
                  vc[32], vc[33], vc[34], vc[35], vc[36], vc[37], vc[38], vc[39],
                  vc[40], vc[41], vc[42], vc[43], vc[44], vc[45], vc[46], vc[47]);
      if (dn < 0 || dn >= (int)sizeof(dd)) dn = (int)sizeof(dd) - 1;
      // bnz — сколько регистров c24..c255 ненулевые; симметрично bnz у VkDraw
      unsigned bnz = 0u;
      for (int r = 24; r < 256; ++r)
      {
        bool nz = false;
        for (int k = 0; k < 4; ++k) if (vc[r * 4 + k] != 0.f) { nz = true; break; }
        if (nz) ++bnz;
      }
      // b20/b21 = c20/c21 (INSTANCE_DATA = c20, GlobalMasks.h:96) — матрица
      // первого экземпляра; inz — сколько регистров c20..c29 ненулевые
      unsigned inz = 0u;
      for (int r = 20; r < 30; ++r)
      {
        bool nz = false;
        for (int k = 0; k < 4; ++k) if (vc[r * 4 + k] != 0.f) { nz = true; break; }
        if (nz) ++inz;
      }
      // c20..c31 дословно — по ним восстанавливается раскладка элемента
      // instanceData (матрица + SH-константы) в c-банке
      {
        char tail[600];
        int tn = snprintf(tail, sizeof(tail), " bnz=%u inz=%u", bnz, inz);
        if (tn < 0 || tn >= (int)sizeof(tail)) tn = (int)sizeof(tail) - 1;
        for (int r = 20; r < 32 && tn < (int)sizeof(tail) - 70; ++r)
        {
          const int q = snprintf(tail + tn, sizeof(tail) - tn,
                                 " c%d=(%.6g,%.6g,%.6g,%.6g)", r,
                                 vc[r * 4], vc[r * 4 + 1], vc[r * 4 + 2], vc[r * 4 + 3]);
          if (q < 0 || q >= (int)(sizeof(tail) - tn)) break;
          tn += q;
        }
        snprintf(dd + dn, sizeof(dd) - dn, "%s", tail);
      }
      DebugTrace("%s", dd);
      }
    }
  }
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

    // Банк констант того же Dip'а в D3D9: WORLD = c0..c3, VIEW = c4..c7
    // (GlobalMasks.h:62,65), LOCALCONST0/1 = c20/c21. Сравниваем с VkTraceC/VkProj
    // на стороне seam'а. PW_D3D_TRACEPJ=x0,y0,x1,y1 — окно по ПРОЕКЦИИ в пиксели
    // вьюпортной строки (конвенция row = v*M), а не по вершинному bbox: один и
    // тот же Flash-квад масштабируется анимацией, и по вершинному bbox
    // плашка неотличима от полос опыта (прежний гейт x1>3000 && 400<y1<600
    // ловил именно полосы).
    static unsigned s_cLogged = 0u;
    static int s_pj[4] = { 0x7FFFFFFF, 0x7FFFFFFF, -1, -1 };
    static bool s_pjRead = false;
    if (!s_pjRead)
    {
      s_pjRead = true;
      char bp[64];
      if (GetEnvironmentVariableA( "PW_D3D_TRACEPJ", bp, sizeof(bp) - 1 ) > 0)
      {
        int v[4] = { 0, 0, 0, 0 };
        char* pch = bp;
        int i = 0;
        while (pch && i < 4) { v[i++] = atoi(pch); pch = strchr(pch, ','); if (pch) ++pch; }
        if (i == 4) { s_pj[0] = v[0]; s_pj[1] = v[1]; s_pj[2] = v[2]; s_pj[3] = v[3]; }
      }
    }
    double qx0 = 1e300, qx1 = -1e300, qy0 = 1e300, qy1 = -1e300, wmax = 0.0;
    unsigned np = 0u;
    float vc[32] = {0}, lc[8] = {0};
    dev->GetVertexShaderConstantF( 0, vc, 8 );
    dev->GetVertexShaderConstantF( 20, lc, 2 );
    {
      void* vpx = 0;
      if (vb && vstr == 16u && SUCCEEDED(vb->Lock(voff, 0, &vpx, 0)) && vpx)
      {
        const float* f = (const float*)vpx;
        for (unsigned k = 0u; k < _descr.numVertices && k < 64u; ++k)
        {
          const float* src = f + (voff / 16u + _descr.startIndex + k) * 4u;
          float v4[4] = { src[0], src[1], 0.f, 1.f }, q[4], r[4];
          for (int i = 0; i < 4; ++i)
            q[i] = vc[0*4+i]*v4[0] + vc[1*4+i]*v4[1] + vc[2*4+i]*v4[2] + vc[3*4+i]*v4[3];
          for (int i = 0; i < 4; ++i)
            r[i] = vc[16+0*4+i]*q[0] + vc[16+1*4+i]*q[1] + vc[16+2*4+i]*q[2] + vc[16+3*4+i]*q[3];
          if (r[3] == 0.f) continue;
          if ((double)fabs(r[3]) > wmax) wmax = r[3];
          double px = (r[0] / r[3] + 1.0) * 0.5 * vp.Width + vp.X;
          double py = (1.0 - r[1] / r[3]) * 0.5 * vp.Height + vp.Y;
          if (px < qx0) qx0 = px;
          if (px > qx1) qx1 = px;
          if (py < qy0) qy0 = py;
          if (py > qy1) qy1 = py;
          ++np;
        }
        vb->Unlock();
      }
    }
    // окно PW_D3D_TRACEPJ задаётся по ВЕРШИННОМУ bbox (тот же гейт, что у
    // PW_VK_TRACEPROJ на стороне seam'а): один и тот же Flash-квад
    // (0,0)-(3240,520) используется и полосами опыта, и плашкой, поэтому
    // печатаем проекцию для всех выводов этого bbox и сравниваем стороны офлайн
    const bool bbHit = s_pj[2] >= 0 && np
      && fabs((double)bx0 - (double)s_pj[0]) <= 40.0 && fabs((double)by0 - (double)s_pj[1]) <= 40.0
      && fabs((double)bx1 - (double)s_pj[2]) <= 40.0 && fabs((double)by1 - (double)s_pj[3]) <= 40.0;
    if ((bbHit || (s_pj[2] < 0 && IsPlateBbox(bx1, by1))) && s_cLogged < 4000u)
    {
      ++s_cLogged;
      unsigned raw8 = 0, raw12 = 0;
      {
        void* vp2 = 0;
        if (vb && vstr == 16u && SUCCEEDED(vb->Lock(voff, 0, &vp2, 0)) && vp2)
        {
          const unsigned char* b = (const unsigned char*)vp2
            + (size_t)(voff / 16u + _descr.startIndex) * 16u;
          memcpy( &raw8, b + 8, 4 );
          memcpy( &raw12, b + 12, 4 );
          vb->Unlock();
        }
      }
      // декларация потока не запрошена: в этом билде d3d9-заголовок клиента не
      // объявляет D3DVERTEXDECLARATION9/GetVertexShaderDeclaration (проверено
      // сборкой), а desc layout'а seam'а уже печатает VkIlMap
      snprintf( dt, sizeof(dt), "vp=%u,%u %ux%u nv=%u bb=(%.0f,%.0f)-(%.0f,%.0f) proj=(%.1f,%.1f)-(%.1f,%.1f) wmax=%.4g raw8=%08X raw12=%08X",
                vp.X, vp.Y, vp.Width, vp.Height, np,
                bx0, by0, bx1, by1, qx0, qy0, qx1, qy1, wmax, raw8, raw12 );
      DebugTrace( "D3dProj: %s", dt );
      snprintf( dt, sizeof(dt), "W=(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g) "
                "(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g)",
                vc[0], vc[1], vc[2], vc[3], vc[4], vc[5], vc[6], vc[7],
                vc[8], vc[9], vc[10], vc[11], vc[12], vc[13], vc[14], vc[15] );
      DebugTrace( "D3dC: %s", dt );
      snprintf( dt, sizeof(dt), "V=(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g) "
                "(%.6g,%.6g,%.6g,%.6g)(%.6g,%.6g,%.6g,%.6g) c20=(%.6g,%.6g,%.6g,%.6g) "
                "c21=(%.6g,%.6g,%.6g,%.6g)",
                vc[16], vc[17], vc[18], vc[19], vc[20], vc[21], vc[22], vc[23],
                vc[24], vc[25], vc[26], vc[27], vc[28], vc[29], vc[30], vc[31],
                lc[0], lc[1], lc[2], lc[3], lc[4], lc[5], lc[6], lc[7] );
      DebugTrace( "D3dC: %s", dt );
    }
    // readback цели ПРЯМО ПОСЛЕ вывода-плашки: белым ли лёг квад в D3D9-слое.
    // Тот же квад перекрашивается в зелёный (PW_D3D_FORCEOPAQUE=2/3), поэтому
    // ищем зелёный. Отдельно читаем ту же поверхность через несколько десятков
    // Dip'ов: если квад рисуется, но его затирают позже в кадре, зелёный
    // появится только с задержкой.
    static unsigned s_rbLogged = 0u;
    static IDirect3DSurface9* s_pendRt = 0;
    static int s_pendN = -1;
    if (rt && cw == 15u && IsPlateBbox(bx1, by1))
    {
      if (s_rbLogged < 4u)
      {
        ++s_rbLogged;
        ScanTarget( dev, rt, "" );
      }
      if (!s_pendRt)
      {
        s_pendRt = rt;
        s_pendRt->AddRef();
        s_pendN = 400;
      }
    }
    else if (s_pendRt && s_pendN > 0)
    {
      s_pendN -= 20;
      if (s_pendN <= 0)
      {
        ScanTarget( dev, s_pendRt, "later" );
        s_pendRt->Release();
        s_pendRt = 0;
        s_pendN = -1;
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
  bool transposed = false;
  float savedM[32];
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
          if (s_fo == 3 || IsPlateBbox(x1, y1))
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
            // PW_D3D_FORCEOPAQUE=4: та же подмена + транспонирование c0..c7
            // (WORLD/VIEW). Клиент кладёт перенос в ПОСЛЕДНЮЮ СТРОКУ, т.е.
            // матрицы под строковые векторы (v*M), а HLSL пишет mul(M, v) —
            // столбцовое умножение, для таких матриц w ~ 3e7 и квад схлопывается.
            // Если после транспонирования зелёный квад появится в readback —
            // расхождение конвенций подтверждено, правку искать в мосте
            // c-банк->SSBO, а не в шейдере.
            if (s_fo == 4)
            {
              float m[32];
              if (SUCCEEDED(dev2->GetVertexShaderConstantF(0, m, 8)))
              {
                float t[32];
                for (int r = 0; r < 4; ++r)
                  for (int c = 0; c < 4; ++c)
                  {
                    t[0 * 16 + c * 4 + r] = m[0 * 16 + r * 4 + c];
                    t[1 * 16 + c * 4 + r] = m[1 * 16 + r * 4 + c];
                  }
                if (SUCCEEDED(dev2->SetVertexShaderConstantF(0, t, 8)))
                {
                  memcpy(savedM, m, sizeof(savedM));
                  transposed = true;
                }
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
  if (transposed)
    devFO->SetVertexShaderConstantF(0, savedM, 8);
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
