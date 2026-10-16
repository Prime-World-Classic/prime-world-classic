#include "stdafx.h"

#include "renderflagsconverter.h"
#include "renderstatesmanager.h"
#include "../System/BitData.h"
#include "GlobalMasks.h"
#include "IConfigManager.h"
#include "MaterialSpec.h"

//#define USE_STATEBLOCK_CACHE

#ifdef _DEBUG
#define USE_STATESTATS
#endif

#ifdef USE_STATESTATS
#define STATESTATS(cmd) cmd
#else
#define STATESTATS(cmd) (void)0
#endif

namespace Render
{

namespace {
#ifdef USE_STATESTATS
	static int g_statesDebugAll;
	static int g_statesDebugSet;
	static int g_statesDebugAllSampler;
	static int g_statesDebugSetSampler;
  static int g_statesDebugAllStencil;
  static int g_statesDebugSetStencil;
	static int g_statesDebugDirect;
	static NDebug::StringDebugVar g_statesDebug("States", "Render");
#endif // USE_STATESTATS

	static CArray1Bit g_cachedStates;
}

RenderStatesManager::RenderStatesManager()
  : m_pipeStateDirty(true), m_pipeStateHash(0)
{
	g_cachedStates.SetSize(256);
	g_cachedStates.FillZero();
	g_cachedStates.SetData(D3DRS_ALPHABLENDENABLE);
	g_cachedStates.SetData(D3DRS_SRCBLEND);
	g_cachedStates.SetData(D3DRS_DESTBLEND);
	g_cachedStates.SetData(D3DRS_ALPHATESTENABLE);
	g_cachedStates.SetData(D3DRS_ALPHAREF);
	g_cachedStates.SetData(D3DRS_ALPHAFUNC);
	g_cachedStates.SetData(D3DRS_CULLMODE);
	//g_cachedStates.SetData(D3DRS_SEPARATEALPHABLENDENABLE);
	//g_cachedStates.SetData(D3DRS_SRCBLENDALPHA);
	//g_cachedStates.SetData(D3DRS_DESTBLENDALPHA);

  stencilLock = 0;

  Init();
}

RenderStatesManager::~RenderStatesManager()
{
}

void RenderStatesManager::OnResourcesRelease()
{
  // Значения тени относятся к теряемому устройству — после восстановления их
  // нужно читать заново.
  std::fill( stateShadowKnown, stateShadowKnown + kRenderStateShadowSize, false );
  renderStateCache.clear();
  samplerStateCache.clear();
  std::fill( stencilStateCache, stencilStateCache + STENCILSTATE_COUNT, DXStateBlockRef() );
}

void RenderStatesManager::Init()
{
  // Тень состояния пуста: неизвестные значения подтягиваются с устройства при
  // первом запросе (GetStateDirectValue), остальные — при записи.
  std::fill( stateShadowKnown, stateShadowKnown + kRenderStateShadowSize, false );
  for (int s = 0; s < kBackendSamplerSlots; ++s)
  {
    std::fill( samplerShadowKnown[s], samplerShadowKnown[s] + kSamplerShadowSize, false );
    std::fill( samplerShadow[s], samplerShadow[s] + kSamplerShadowSize, DWORD(0) );
  }
  lastRenderState.alwaysFailCompare = 1;
  for (int i = 0; i < ARRAY_SIZE(lastSamplerState); ++i)
    lastSamplerState[i].alwaysFailCompare = 1;
  m_pipeStateDirty = true;
}

void RenderStatesManager::SetState(RenderState state)
{
	STATESTATS(g_statesDebugAll++);
  if(GetRuntimePins().RenderModeValue != NDb::RENDERMODEPIN_RENDERNORMAL)
    state.SetEmissive(NDb::ONOFFSTATE_OFF);
	if(lastRenderState == state)
		return;
	STATESTATS(g_statesDebugSet++);
	lastRenderState = state;

	IDirect3DDevice9* device = GetDevice();

#ifdef USE_STATEBLOCK_CACHE
	// Check cache first
	DXStateBlockRef& cached = renderStateCache[state.bitmask];
	if (cached)
	{
		cached->Apply();
		return;
	}
	
	// Begin caching
	device->BeginStateBlock();
#endif // USE_STATEBLOCK_CACHE

	// Set blend state
  const bool emissive = (state.GetEmissive() == NDb::ONOFFSTATE_ON);
  const D3DBLEND SrcAlpha = /*emissive ? D3DBLEND_ONE :*/ D3DBLEND_SRCALPHA;
  const D3DBLEND InvSrcAlpha = /*emissive ? D3DBLEND_SRCALPHA :*/ D3DBLEND_INVSRCALPHA;
	ApplyState(D3DRS_ALPHABLENDENABLE, state.GetBlendMode() != NDb::BLENDMODE_OFF ? TRUE : FALSE);
	switch (state.GetBlendMode())
	{
	case NDb::BLENDMODE_OFF:
		break;
  case NDb::BLENDMODE_LERPBYALPHA:
    ApplyState(D3DRS_SRCBLEND,  SrcAlpha);
    ApplyState(D3DRS_DESTBLEND, InvSrcAlpha);
    break;
  case NDb::BLENDMODE_PREMULTIPLIEDLERP:
    ApplyState(D3DRS_SRCBLEND,  D3DBLEND_ONE);
    ApplyState(D3DRS_DESTBLEND, InvSrcAlpha);
    break;
	case NDb::BLENDMODE_ADDCOLOR:
		ApplyState(D3DRS_SRCBLEND,  D3DBLEND_ONE);
		ApplyState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		break;
	case NDb::BLENDMODE_ADDCOLORMULALPHA:
		ApplyState(D3DRS_SRCBLEND,  SrcAlpha);
		ApplyState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		break;
	case NDb::BLENDMODE_MULCOLOR:
		ApplyState(D3DRS_SRCBLEND,  D3DBLEND_ZERO);
		ApplyState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
		break;
  case NDb::BLENDMODE_MULINVCOLOR:
    ApplyState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
    ApplyState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    break;
	default:
		NI_ALWAYS_ASSERT("Invalid blend mode");
	}

	// Set alpha test states
  ApplyState(D3DRS_ALPHATESTENABLE, state.GetAlphaTest() == NDb::ONOFFSTATE_ON);
  ApplyState(D3DRS_ALPHAFUNC, emissive ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL);
  DWORD alphaRef = state.GetAlphaTestRef();
  if( emissive )
    alphaRef = 255 - alphaRef * BLOOM_ENCODING_THRESHOLD; 
	ApplyState(D3DRS_ALPHAREF, alphaRef );

	// Set culling state
	ApplyState(D3DRS_CULLMODE, state.GetCulling() == NDb::ONOFFSTATE_ON ? D3DCULL_CW : D3DCULL_NONE);

	//// Mark emissive pixels in alpha channel
	//device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
	//device->SetRenderState(D3DRS_SRCBLENDALPHA, emissive ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
	//device->SetRenderState(D3DRS_DESTBLENDALPHA,  emissive ? D3DBLEND_ZERO : D3DBLEND_ONE);

#ifdef USE_STATEBLOCK_CACHE
	// Store compiled states in cache
	IDirect3DStateBlock9 *pStateBlock = 0;
	device->EndStateBlock(&pStateBlock);
	cached.Attach(pStateBlock);
#endif // USE_STATEBLOCK_CACHE
}

void RenderStatesManager::ApplyState(D3DRENDERSTATETYPE State, DWORD Value)
{
	GetDevice()->SetRenderState(State, Value);

	const DWORD idx = static_cast<DWORD>(State);
	if (idx < kRenderStateShadowSize)
	{
		stateShadow[idx] = Value;
		stateShadowKnown[idx] = true;
	}
	MarkStateDirty();
}

// Только тень: состояние уже легло на устройство блоком состояний, повторная
// запись трогать нельзя (иначе пропадает смысл кэша), но и отставать тень не
// обязана — по ней строится модель для бэкенда.
void RenderStatesManager::ShadowState(D3DRENDERSTATETYPE State, DWORD Value)
{
	const DWORD idx = static_cast<DWORD>(State);
	if (idx < kRenderStateShadowSize)
	{
		stateShadow[idx] = Value;
		stateShadowKnown[idx] = true;
	}
	MarkStateDirty();
}

DWORD RenderStatesManager::ShadowValue(D3DRENDERSTATETYPE State) const
{
	const DWORD idx = static_cast<DWORD>(State);
	if (idx >= kRenderStateShadowSize)
		return 0;
	if (!stateShadowKnown[idx])
	{
		DWORD v = 0;
		if (FAILED(GetDevice()->GetRenderState(State, &v)))
			return 0;
		// тень догоняем не const-путём: модель обязана отражать устройство
		const_cast<DWORD*>(stateShadow)[idx] = v;
		const_cast<bool*>(stateShadowKnown)[idx] = true;
		return v;
	}
	return stateShadow[idx];
}

void RenderStatesManager::ShadowSampler(unsigned slot, D3DSAMPLERSTATETYPE type, DWORD value)
{
	const int t = static_cast<int>(type) - kSamplerShadowBase;
	if (slot < kBackendSamplerSlots && t >= 0 && t < kSamplerShadowSize)
	{
		samplerShadow[slot][t] = value;
		samplerShadowKnown[slot][t] = true;
	}
	MarkStateDirty();
}

DWORD RenderStatesManager::SamplerShadowValue(unsigned slot, D3DSAMPLERSTATETYPE type) const
{
	const int t = static_cast<int>(type) - kSamplerShadowBase;
	if (slot >= kBackendSamplerSlots || t < 0 || t >= kSamplerShadowSize)
		return 0;
	if (!samplerShadowKnown[slot][t])
	{
		DWORD v = 0;
		if (FAILED(GetDevice()->GetSamplerState(slot, type, &v)))
			return 0;
		const_cast<DWORD*>(samplerShadow[slot])[t] = v;
		const_cast<bool*>(samplerShadowKnown[slot])[t] = true;
		return v;
	}
	return samplerShadow[slot][t];
}

bool RenderStatesManager::GetStateDirectValue(D3DRENDERSTATETYPE State, DWORD & outValue)
{
	const DWORD idx = static_cast<DWORD>(State);
	if (idx >= kRenderStateShadowSize)
		return false;

	if (!stateShadowKnown[idx])
	{
		// Состояние ни разу не писали через менеджера — берём текущее с устройства
		// (один раз на состояние), дальше живём по тени.
		if (FAILED(GetDevice()->GetRenderState(State, &stateShadow[idx])))
			return false;
		stateShadowKnown[idx] = true;
	}

	outValue = stateShadow[idx];
	return true;
}

void RenderStatesManager::SetStateDirect(D3DRENDERSTATETYPE State, DWORD Value)
{
	// invalidate cache if one of cached states is set directly
	if (g_cachedStates.GetData(State))
		lastRenderState.alwaysFailCompare = 1;

	// Запись идёт через ApplyState: устройство получает тот же вызов, что раньше,
	// но тень больше не отстаёт. Без этого модель состояний (шаг 13.4b) и хэш
	// «raw» из шага 8 не видели бы прямых записей — а это как раз ShadowManager
	// (DEPTHBIAS/SLOPESCALEDEPTHBIAS), PostFX и альфа-тест.
	ApplyState(State, Value);
	STATESTATS(g_statesDebugDirect++);
}

void RenderStatesManager::SetSamplerStateDirect(unsigned index, D3DSAMPLERSTATETYPE type, DWORD value)
{
	// Инвалидация кэша сэмплеров — см. комментарий в заголовке. Приём тот же,
	// что в конструкторе и в SetStateDirect: выставляем alwaysFailCompare.
	if (index < ARRAY_SIZE(lastSamplerState))
		lastSamplerState[index].alwaysFailCompare = 1;

	GetDevice()->SetSamplerState(index, type, value);
	ShadowSampler(index, type, value);
	STATESTATS(g_statesDebugDirect++);
}

void RenderStatesManager::SetSamplerState(unsigned index, SamplerState const& state)
{
	STATESTATS(g_statesDebugAllSampler++);
	if (index < ARRAY_SIZE(lastSamplerState) && lastSamplerState[index] == state)
		return;
	STATESTATS(g_statesDebugSetSampler++);

  if (index < ARRAY_SIZE(lastSamplerState))
    lastSamplerState[index] = state;

	// texture address
	static const D3DTEXTUREADDRESS tblAddress[] = {D3DTADDRESS_WRAP, D3DTADDRESS_CLAMP, D3DTADDRESS_MIRROR, D3DTADDRESS_BORDER};
  // minification filter
  static const D3DTEXTUREFILTERTYPE tblMinFilter[] = {D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC, D3DTEXF_ANISOTROPIC, 
    D3DTEXF_ANISOTROPIC, D3DTEXF_ANISOTROPIC, D3DTEXF_ANISOTROPIC, D3DTEXF_ANISOTROPIC};
  // magnification filter
  static const D3DTEXTUREFILTERTYPE tblMagFilter[] = {D3DTEXF_POINT, D3DTEXF_LINEAR};
  // mip-mapping filter
  static const D3DTEXTUREFILTERTYPE tblMipFilter[] = {D3DTEXF_NONE, D3DTEXF_POINT, D3DTEXF_LINEAR};

  IDirect3DDevice9* device = GetDevice();

  // Тень сэмплера ведём по пресету и ДО ветки кэша состояний: при попадании в
  // кэш отдельные SetSamplerState не выполняются (их делает блок), поэтому
  // иначе тень отстаёт от устройства, а по ней строится модель для бэкенда.
  if (index < ARRAY_SIZE(lastSamplerState))
  {
    ShadowSampler(index, D3DSAMP_ADDRESSU, tblAddress[state.addressU]);
    ShadowSampler(index, D3DSAMP_ADDRESSV, tblAddress[state.addressV]);
    ShadowSampler(index, D3DSAMP_MINFILTER, tblMinFilter[state.minFilter]);
    ShadowSampler(index, D3DSAMP_MAGFILTER, tblMagFilter[state.magFilter]);
    ShadowSampler(index, D3DSAMP_MIPFILTER, tblMipFilter[state.mipFilter]);
  }

#ifdef USE_STATEBLOCK_CACHE
  // Check cache first
  NI_ASSERT(index < 256, "RenderStatesManager::SetSamplerState: Too big sampler index");
  NI_ASSERT(0 == (state.bitmask & 0xFF000000), "RenderStatesManager::SetSamplerState: wrong sampler state");
  const DWORD idx = state.bitmask | (index << 24);
  DXStateBlockRef& cached = samplerStateCache[idx];
  if (cached)
  {
    cached->Apply();
#ifdef _DEBUG
    DWORD value;
#pragma warning( push )
#pragma warning(disable:4389) // '==' : signed/unsigned mismatch 
    device->GetSamplerState(index, D3DSAMP_ADDRESSU, &value);
    NI_ASSERT(tblAddress[state.addressU] == value, "RenderStatesManager::SetSamplerState: wrong state block");
    device->GetSamplerState(index, D3DSAMP_ADDRESSV, &value);
    NI_ASSERT(tblAddress[state.addressV] == value, "RenderStatesManager::SetSamplerState: wrong state block");
    //device->GetSamplerState(index, D3DSAMP_MAXANISOTROPY, &value);
    //NI_ASSERT(state.minFilter - NDb::MINFILTERTYPE_ANISOTROPIC1X + 1 == value, "RenderStatesManager::SetSamplerState: wrong state block");
    device->GetSamplerState(index, D3DSAMP_MINFILTER, &value);
    NI_ASSERT(tblMinFilter[state.minFilter] == value, "RenderStatesManager::SetSamplerState: wrong state block");
    device->GetSamplerState(index, D3DSAMP_MAGFILTER, &value);
    NI_ASSERT(tblMagFilter[state.magFilter] == value, "RenderStatesManager::SetSamplerState: wrong state block");
    device->GetSamplerState(index, D3DSAMP_MIPFILTER, &value);
    NI_ASSERT(tblMipFilter[state.mipFilter] == value, "RenderStatesManager::SetSamplerState: wrong state block");
#pragma warning( pop )
#endif
    return;
  }

  // Begin caching
  device->BeginStateBlock();
#endif // USE_STATEBLOCK_CACHE

	ASSERT(state.addressU < ARRAY_SIZE(tblAddress) && state.addressV < ARRAY_SIZE(tblAddress));
	device->SetSamplerState(index, D3DSAMP_ADDRESSU, tblAddress[state.addressU]);
	device->SetSamplerState(index, D3DSAMP_ADDRESSV, tblAddress[state.addressV]);

	// anisotropy
	//if (state.minFilter >= NDb::MINFILTERTYPE_ANISOTROPIC1X)
	  //device->SetSamplerState(index, D3DSAMP_MAXANISOTROPY, state.minFilter - NDb::MINFILTERTYPE_ANISOTROPIC1X + 1);

	ASSERT(state.minFilter < ARRAY_SIZE(tblMinFilter));
	device->SetSamplerState(index, D3DSAMP_MINFILTER, tblMinFilter[state.minFilter]);

	ASSERT(state.magFilter < ARRAY_SIZE(tblMagFilter));
	device->SetSamplerState(index, D3DSAMP_MAGFILTER, tblMagFilter[state.magFilter]);

	ASSERT(state.mipFilter < ARRAY_SIZE(tblMipFilter));
	device->SetSamplerState(index, D3DSAMP_MIPFILTER, tblMipFilter[state.mipFilter]);

#ifdef USE_STATEBLOCK_CACHE
  // Store compiled states in cache
  IDirect3DStateBlock9 *pStateBlock = 0;
  device->EndStateBlock(&pStateBlock);
  cached.Attach(pStateBlock);
#endif // USE_STATEBLOCK_CACHE
}

void RenderStatesManager::SetStencilState(StencilState state, DWORD mask /*= 0xFFFFFFFF*/, DWORD bits /*= 0xFFFFFFFF*/)
{
  if(stencilLock)
    return;
  STATESTATS(g_statesDebugAllStencil++);

    if( bits == DWORD(-1) )
      bits = mask;  // Wine bugs workaround

  IDirect3DDevice9* const device = GetDevice();

  if(state > STENCILSTATE_IGNORE)
  {
    if( bits != lastStencilBits )
      ApplyState(D3DRS_STENCILREF, lastStencilBits = bits);

    if( mask != lastStencilMask )
    {
      lastStencilMask = mask;
      ApplyState(D3DRS_STENCILMASK, lastStencilMask);
      ApplyState(D3DRS_STENCILWRITEMASK, lastStencilMask);
    }
  }

  if( state == lastStencilState )
    return;
  STATESTATS(g_statesDebugSetStencil++);

  lastStencilState = state;
  if (state == STENCILSTATE_INVALID)
    return;


  static struct StencilStateParams {
    StencilState state;
    DWORD enable;
    DWORD pass;
    DWORD func;
  } presets[STENCILSTATE_COUNT] = {
    {STENCILSTATE_INVALID,   FALSE, D3DSTENCILOP_KEEP,    D3DCMP_ALWAYS},
    {STENCILSTATE_IGNORE,    FALSE, D3DSTENCILOP_KEEP,    D3DCMP_ALWAYS},
    {STENCILSTATE_WRITEBITS, TRUE,  D3DSTENCILOP_REPLACE, D3DCMP_ALWAYS},
    {STENCILSTATE_CHECKBITS, TRUE,  D3DSTENCILOP_KEEP,    D3DCMP_EQUAL},
  };

#ifdef USE_STATEBLOCK_CACHE
  // Check cache first
  DXStateBlockRef& cached = stencilStateCache[state];
  if (cached)
  {
    cached->Apply();
    // Блок вернул состояния устройства; тень обязана отразить то же самое, иначе
    // модель для бэкенда разойдётся с тем, что реально действует.
    NI_ASSERT(presets[state].state == state, "Array of StencilStateParams corrupted.");
    ShadowState(D3DRS_STENCILENABLE, presets[state].enable);
    ShadowState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    ShadowState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    ShadowState(D3DRS_STENCILPASS, presets[state].pass);
    ShadowState(D3DRS_STENCILFUNC, presets[state].func);
    return;
  }

  // Begin caching
  device->BeginStateBlock();
#endif // USE_STATEBLOCK_CACHE

  NI_ASSERT(presets[state].state == state, "Array of StencilStateParams corrupted.");

  StencilStateParams& params = presets[state];

  ApplyState(D3DRS_STENCILENABLE, params.enable);
  ApplyState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
  ApplyState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
  ApplyState(D3DRS_STENCILPASS, params.pass);
  ApplyState(D3DRS_STENCILFUNC, params.func);

#ifdef USE_STATEBLOCK_CACHE
	// Store compiled states in cache
	IDirect3DStateBlock9 *pStateBlock = 0;
	device->EndStateBlock(&pStateBlock);
	cached.Attach(pStateBlock);
#endif // USE_STATEBLOCK_CACHE
}

void RenderStatesManager::SetStencilStateAddonBits(DWORD mask, DWORD bits)
{
  if (lastStencilState == STENCILSTATE_WRITEBITS)
    ApplyState(D3DRS_STENCILREF, lastStencilBits & ~mask | bits & mask);
}

void RenderStatesManager::UpdateStats()
{
#ifdef USE_STATESTATS
  g_statesDebug.SetValue("RS: %i/%i + %i; SS: %i/%i; StS: %i/%i", g_statesDebugSet, g_statesDebugAll, g_statesDebugDirect,
                         g_statesDebugSetSampler, g_statesDebugAllSampler, g_statesDebugSetStencil, g_statesDebugAllStencil);
	g_statesDebugAll = 0;
	g_statesDebugSet = 0;
	g_statesDebugAllSampler = 0;
	g_statesDebugSetSampler = 0;
  g_statesDebugAllStencil = 0;
  g_statesDebugSetStencil = 0;
	g_statesDebugDirect = 0;
#endif // USE_STATESTATS
}

namespace {
// FNV-1a: дешёвый и стабильный. Для замера числа уникальных ключей (~10^4 за
// бой) вероятность ложного слияния пренебрежима — тот же аргумент, что и для
// ключа пары шейдеров в smartrenderer.cpp.
inline UINT64 StateHashMix(UINT64 h, UINT64 v) { h ^= v; return h * 0x100000001B3ull; }
const UINT64 kStateHashSeed = 0xcbf29ce484222325ull;
} // namespace

// Хэши для замера ключа пайплайна (docs/PLAN_NVRHI.md, шаг 8).
UINT64 RenderStatesManager::GetRenderStateShadowHash() const
{
  // Только известные записи: «неизвестно» в тени — не ноль, подставлять под
  // него ноль значит занижать ключ.
  UINT64 h = kStateHashSeed;
  for (int i = 0; i < kRenderStateShadowSize; ++i)
    if (stateShadowKnown[i])
      h = StateHashMix(StateHashMix(h, (UINT64)i), stateShadow[i]);
  return h;
}

UINT64 RenderStatesManager::GetSamplerShadowHash() const
{
  UINT64 h = kStateHashSeed;
  for (unsigned i = 0; i < ARRAY_SIZE(lastSamplerState); ++i)
    h = StateHashMix(StateHashMix(h, i), lastSamplerState[i].GetBitmask());
  return h;
}

UINT64 RenderStatesManager::GetStencilShadowKey() const
{
  UINT64 h = kStateHashSeed;
  h = StateHashMix(h, (UINT64)lastStencilState);
  h = StateHashMix(h, lastStencilBits);
  h = StateHashMix(h, lastStencilMask);
  return h;
}

// Модель состояний конвейера (шаг 13.4b). Значения берутся из теней; то, что
// менеджер ни разу не писал, один раз читается с устройства (ShadowValue /
// SamplerShadowValue) — модель обязана отражать реальное состояние, а не «нули
// по умолчанию»: D3D9 по умолчанию пишет COLORWRITEENABLE=RGB без альфы,
// CULLMODE=CW и ZFUNC=LESS, и подстановка нуля вместо этого молча меняла бы
// картинку на Vulkan.
void RenderStatesManager::BuildPipelineState() const
{
  BackendPipelineState& st = m_pipeState;
  st.Reset();

  st.cullMode          = ShadowValue(D3DRS_CULLMODE);
  st.fillMode          = ShadowValue(D3DRS_FILLMODE);
  st.scissorTestEnable = ShadowValue(D3DRS_SCISSORTESTENABLE) != 0;

  st.zEnable      = ShadowValue(D3DRS_ZENABLE) != 0;
  st.zWriteEnable = ShadowValue(D3DRS_ZWRITEENABLE) != 0;
  st.zFunc        = ShadowValue(D3DRS_ZFUNC);
  {
    const DWORD ssdb = ShadowValue(D3DRS_SLOPESCALEDEPTHBIAS);
    float f = 0.f;
    memcpy(&f, &ssdb, sizeof(f));   // D3D9 хранит это состояние как биты float в DWORD
    st.slopeScaleDepthBias = f;
    st.depthBiasRaw = ShadowValue(D3DRS_DEPTHBIAS);
  }

  st.stencilEnable    = ShadowValue(D3DRS_STENCILENABLE) != 0;
  st.stencilFunc      = ShadowValue(D3DRS_STENCILFUNC);
  st.stencilRef       = ShadowValue(D3DRS_STENCILREF);
  st.stencilReadMask  = ShadowValue(D3DRS_STENCILMASK);
  st.stencilWriteMask = ShadowValue(D3DRS_STENCILWRITEMASK);
  st.stencilFail      = ShadowValue(D3DRS_STENCILFAIL);
  st.stencilZFail     = ShadowValue(D3DRS_STENCILZFAIL);
  st.stencilPass      = ShadowValue(D3DRS_STENCILPASS);

  st.alphaBlendEnable   = ShadowValue(D3DRS_ALPHABLENDENABLE) != 0;
  st.separateAlphaBlend = ShadowValue(D3DRS_SEPARATEALPHABLENDENABLE) != 0;
  st.srcBlend  = ShadowValue(D3DRS_SRCBLEND);
  st.destBlend = ShadowValue(D3DRS_DESTBLEND);
  st.blendOp   = ShadowValue(D3DRS_BLENDOP);
  st.srcBlendAlpha  = ShadowValue(D3DRS_SRCBLENDALPHA);
  st.destBlendAlpha = ShadowValue(D3DRS_DESTBLENDALPHA);
  st.blendOpAlpha   = ShadowValue(D3DRS_BLENDOPALPHA);
  st.colorWriteEnable = ShadowValue(D3DRS_COLORWRITEENABLE);

  st.alphaTestEnable = ShadowValue(D3DRS_ALPHATESTENABLE) != 0;
  st.alphaRef  = ShadowValue(D3DRS_ALPHAREF);
  st.alphaFunc = ShadowValue(D3DRS_ALPHAFUNC);

  st.clipPlaneEnable = ShadowValue(D3DRS_CLIPPLANEENABLE);

  for (unsigned s = 0; s < kBackendSamplerSlots; ++s)
  {
    BackendSamplerState& ss = st.sampler[s];
    ss.minFilter     = SamplerShadowValue(s, D3DSAMP_MINFILTER);
    ss.magFilter     = SamplerShadowValue(s, D3DSAMP_MAGFILTER);
    ss.mipFilter     = SamplerShadowValue(s, D3DSAMP_MIPFILTER);
    ss.addressU      = SamplerShadowValue(s, D3DSAMP_ADDRESSU);
    ss.addressV      = SamplerShadowValue(s, D3DSAMP_ADDRESSV);
    ss.addressW      = SamplerShadowValue(s, D3DSAMP_ADDRESSW);
    ss.borderColor   = SamplerShadowValue(s, D3DSAMP_BORDERCOLOR);
    ss.maxAnisotropy = SamplerShadowValue(s, D3DSAMP_MAXANISOTROPY);
    ss.maxMipLevel   = SamplerShadowValue(s, D3DSAMP_MAXMIPLEVEL);
    ss.lodBias       = (int)SamplerShadowValue(s, D3DSAMP_MIPMAPLODBIAS);
    for (int t = 0; t < kSamplerShadowSize; ++t)
      if (samplerShadowKnown[s][t]) { ss.set = true; break; }
  }

  // Хэш — по полям, а не по байтам структуры: выравнивание и мусор в
  // заполнителях сделали бы ключ нестабильным.
  UINT64 h = kStateHashSeed;
  const unsigned* fields[] = {
    &st.cullMode, &st.fillMode, &st.zFunc, &st.depthBiasRaw,
    &st.stencilFunc, &st.stencilRef, &st.stencilReadMask, &st.stencilWriteMask,
    &st.stencilFail, &st.stencilZFail, &st.stencilPass,
    &st.srcBlend, &st.destBlend, &st.blendOp,
    &st.srcBlendAlpha, &st.destBlendAlpha, &st.blendOpAlpha, &st.colorWriteEnable,
    &st.alphaRef, &st.alphaFunc, &st.clipPlaneEnable,
  };
  h = StateHashMix(h, st.scissorTestEnable ? 1u : 0u);
  h = StateHashMix(h, st.zEnable ? 1u : 0u);
  h = StateHashMix(h, st.zWriteEnable ? 1u : 0u);
  h = StateHashMix(h, st.stencilEnable ? 1u : 0u);
  h = StateHashMix(h, st.alphaBlendEnable ? 1u : 0u);
  h = StateHashMix(h, st.separateAlphaBlend ? 1u : 0u);
  h = StateHashMix(h, st.alphaTestEnable ? 1u : 0u);
  for (unsigned i = 0; i < ARRAY_SIZE(fields); ++i)
    h = StateHashMix(StateHashMix(h, i), *fields[i]);
  float ssdbBits = st.slopeScaleDepthBias;
  unsigned ssdb;
  memcpy(&ssdb, &ssdbBits, sizeof(ssdb));
  h = StateHashMix(h, ssdb);
  for (unsigned s = 0; s < kBackendSamplerSlots; ++s) {
    const BackendSamplerState& ss = st.sampler[s];
    h = StateHashMix(h, s);
    h = StateHashMix(h, ss.set ? 1u : 0u);
    h = StateHashMix(h, ss.minFilter); h = StateHashMix(h, ss.magFilter);
    h = StateHashMix(h, ss.mipFilter); h = StateHashMix(h, ss.addressU);
    h = StateHashMix(h, ss.addressV);  h = StateHashMix(h, ss.addressW);
    h = StateHashMix(h, ss.borderColor); h = StateHashMix(h, ss.maxAnisotropy);
    h = StateHashMix(h, (unsigned)ss.maxMipLevel); h = StateHashMix(h, (unsigned)ss.lodBias);
  }
  m_pipeStateHash = h;
  m_pipeStateDirty = false;
}

const BackendPipelineState& RenderStatesManager::GetPipelineState() const
{
  if (m_pipeStateDirty)
    BuildPipelineState();
  return m_pipeState;
}

unsigned long long RenderStatesManager::GetPipelineStateHash() const
{
  if (m_pipeStateDirty)
    BuildPipelineState();
  return m_pipeStateHash;
}

namespace {
// Таблица только для консольной команды dump_states. Держим в ней ровно те
// состояния, которые реально выставляет клиент (см. SetState /
// SetStateDirect-вызовы); легаси-фикс-фунction (fog, material sources,
// тесселяция, WRAP*, point scale, CCW-стencil, vertex blend) из неё убраны:
// они нигде не устанавливаются, а под Vulkan/DX12 аналога не имеют.
#define ENUM_PAIR(e) {e, #e}
  struct {
    D3DRENDERSTATETYPE state;
    char const* pName;
  } g_renderStates[] = {
    ENUM_PAIR(D3DRS_ZENABLE),
    ENUM_PAIR(D3DRS_FILLMODE),
    ENUM_PAIR(D3DRS_ZWRITEENABLE),
    ENUM_PAIR(D3DRS_ALPHATESTENABLE),
    ENUM_PAIR(D3DRS_SRCBLEND),
    ENUM_PAIR(D3DRS_DESTBLEND),
    ENUM_PAIR(D3DRS_CULLMODE),
    ENUM_PAIR(D3DRS_ZFUNC),
    ENUM_PAIR(D3DRS_ALPHAREF),
    ENUM_PAIR(D3DRS_ALPHAFUNC),
    ENUM_PAIR(D3DRS_ALPHABLENDENABLE),
    ENUM_PAIR(D3DRS_STENCILENABLE),
    ENUM_PAIR(D3DRS_STENCILFAIL),
    ENUM_PAIR(D3DRS_STENCILZFAIL),
    ENUM_PAIR(D3DRS_STENCILPASS),
    ENUM_PAIR(D3DRS_STENCILFUNC),
    ENUM_PAIR(D3DRS_STENCILREF),
    ENUM_PAIR(D3DRS_STENCILMASK),
    ENUM_PAIR(D3DRS_STENCILWRITEMASK),
    ENUM_PAIR(D3DRS_LIGHTING),
    ENUM_PAIR(D3DRS_CLIPPLANEENABLE),
    ENUM_PAIR(D3DRS_POINTSIZE),
    ENUM_PAIR(D3DRS_COLORWRITEENABLE),
    ENUM_PAIR(D3DRS_BLENDOP),
    ENUM_PAIR(D3DRS_SCISSORTESTENABLE),
    ENUM_PAIR(D3DRS_SLOPESCALEDEPTHBIAS),
    ENUM_PAIR(D3DRS_COLORWRITEENABLE1),
    ENUM_PAIR(D3DRS_DEPTHBIAS),
    ENUM_PAIR(D3DRS_SEPARATEALPHABLENDENABLE),
    ENUM_PAIR(D3DRS_SRCBLENDALPHA),
    ENUM_PAIR(D3DRS_DESTBLENDALPHA),
    ENUM_PAIR(D3DRS_BLENDOPALPHA),
  };

  struct {
    D3DSAMPLERSTATETYPE state;
    char const* pName;
  } g_samplerStates[] = {
    ENUM_PAIR(D3DSAMP_ADDRESSU),
    ENUM_PAIR(D3DSAMP_ADDRESSV),
    ENUM_PAIR(D3DSAMP_BORDERCOLOR),
    ENUM_PAIR(D3DSAMP_MAGFILTER),
    ENUM_PAIR(D3DSAMP_MINFILTER),
    ENUM_PAIR(D3DSAMP_MIPFILTER),
    ENUM_PAIR(D3DSAMP_MIPMAPLODBIAS),
    ENUM_PAIR(D3DSAMP_MAXMIPLEVEL),
  };
#undef ENUM_PAIR
}// anonymous namespace

void RenderStatesManager::DumpStates()
{
  for (int i = 0; i < ARRAY_SIZE(g_renderStates); i++)
  {
    DWORD val = 0;
    // DumpStates() — статическая функция, тень живёт в экземпляре менеджера.
    if (GetStatesManager()->GetStateDirectValue(g_renderStates[i].state, val))
    {
      DebugTrace("0x%08X = %s", val, g_renderStates[i].pName);
    }
    else
    {
      DebugTrace("  FAILED = %s", g_renderStates[i].pName);
    }
  }

  for (int j = 0; j < 8; j++)
  {
    DebugTrace("Sampler States (%i)", j);
    for (int i = 0; i < ARRAY_SIZE(g_samplerStates); i++)
    {
      DWORD val;
      HRESULT hr = GetDevice()->GetSamplerState(j, g_samplerStates[i].state, &val);
      if (SUCCEEDED(hr))
      {
        DebugTrace("0x%08X = (%i) %s", val, j, g_samplerStates[i].pName);
      }
      else
      {
        DebugTrace("  FAILED = (%i) %s", j, g_samplerStates[i].pName);
      }
    }
  }

  CVec4 values[32];
  DebugTrace("Vertex Shader Constants");
  HRESULT hr = GetDevice()->GetVertexShaderConstantF(0, (float*)values, ARRAY_SIZE(values));
  if (SUCCEEDED(hr))
  {
    for (int i = 0; i < ARRAY_SIZE(values); i++)
    {
      DebugTrace("%02i 0x%08X 0x%08X 0x%08X 0x%08X", i, *(DWORD*)&values[i].x, *(DWORD*)&values[i].y, *(DWORD*)&values[i].z, *(DWORD*)&values[i].w);
    }
  }
  else
  {
    DebugTrace("FAILED");
  }

  DebugTrace("Pixel Shader Constants");
  hr = GetDevice()->GetPixelShaderConstantF(0, (float*)values, ARRAY_SIZE(values));
  if (SUCCEEDED(hr))
  {
    for (int i = 0; i < ARRAY_SIZE(values); i++)
    {
      DebugTrace("%02i 0x%08X 0x%08X 0x%08X 0x%08X", i, *(DWORD*)&values[i].x, *(DWORD*)&values[i].y, *(DWORD*)&values[i].z, *(DWORD*)&values[i].w);
    }
  }
  else
  {
    DebugTrace("FAILED");
  }
}

// Сверка тени с фактическим состоянием устройства (шаг 13.4b).
//
// Модель состояний конвейера строится ИЗ ТЕНИ, поэтому тень обязана отражать
// каждую запись. Вызовы, пишущие прямо в устройство
// (GetDevice()->SetRenderState / SetSamplerState), тень минуют: на D3D9 это
// безвредно (устройство и так получает значение), а на Vulkan такое состояние
// молча не доезжает. Именно так 2026-10-16 нашлись дыры в SetStateDirect и
// SetSamplerStateDirect. Список состояний — тот же, что у dump_states.
unsigned RenderStatesManager::VerifyShadowAgainstDevice(unsigned maxReports) const
{
	unsigned mismatch = 0;

	for (int i = 0; i < ARRAY_SIZE(g_renderStates); i++)
	{
		const int idx = static_cast<int>(g_renderStates[i].state);
		if (idx < 0 || idx >= kRenderStateShadowSize || !stateShadowKnown[idx])
			continue;
		DWORD dev = 0;
		if (FAILED(GetDevice()->GetRenderState(g_renderStates[i].state, &dev)))
			continue;
		if (dev == stateShadow[idx])
			continue;
		++mismatch;
		if (mismatch <= maxReports)
			DebugTrace("StateShadow: %s тень 0x%08X, устройство 0x%08X — запись мимо менеджера",
				g_renderStates[i].pName, stateShadow[idx], dev);
	}

	for (unsigned slot = 0; slot < kBackendSamplerSlots; ++slot)
		for (int i = 0; i < ARRAY_SIZE(g_samplerStates); i++)
		{
			const int t = static_cast<int>(g_samplerStates[i].state) - kSamplerShadowBase;
			if (t < 0 || t >= kSamplerShadowSize || !samplerShadowKnown[slot][t])
				continue;
			DWORD dev = 0;
			if (FAILED(GetDevice()->GetSamplerState(slot, g_samplerStates[i].state, &dev)))
				continue;
			if (dev == samplerShadow[slot][t])
				continue;
			++mismatch;
			if (mismatch <= maxReports)
				DebugTrace("StateShadow: сэмплер %u %s тень 0x%08X, устройство 0x%08X",
					slot, g_samplerStates[i].pName, samplerShadow[slot][t], dev);
		}

	return mismatch;
}

}; // namespace Render