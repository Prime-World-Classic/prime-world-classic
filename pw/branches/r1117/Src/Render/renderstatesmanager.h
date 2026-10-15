#pragma once

#include "renderstates.h"
#include "smartrenderer.h"
#include "material.h"

namespace Render
{

enum StencilState
{
  STENCILSTATE_INVALID,
  STENCILSTATE_IGNORE,
  STENCILSTATE_WRITEBITS,
  STENCILSTATE_CHECKBITS,

  STENCILSTATE_COUNT
};

enum StencilBit
{
  STENCILBIT_SHADOWRECEIVER = 0x01,
  STENCILBIT_DECALRECEIVER = 0x02,
  STENCILBIT_RENDERALLOW = 0x04,
};

class RenderStatesManager : public Render::ReallocateHandler
{
protected:
  RenderStatesManager();

public:
	~RenderStatesManager();

	void SetState(RenderState state);
  RenderState const& GetCurrentState() const { return lastRenderState; }

	void SetStateDirect(D3DRENDERSTATETYPE State, DWORD Value);
  // Чтение текущего значения состояния рендера. Менеджер — единственная точка
  // записи состояния (docs/PLAN_NVRHI.md §2.1, §2.8), поэтому он же держит и его
  // тень: save/restore-паттернам (ShadowManager — DEPTHBIAS/SLOPESCALEDEPTHBIAS,
  // batch — SCISSORTESTENABLE) и DumpStates() на Vulkan-бэкенде читать состояние
  // будет неоткуда. Возвращает false, если состояние вне диапазона D3DRS_* или
  // его не удалось ни получить из тени, ни прочитать с устройства.
  bool GetStateDirectValue(D3DRENDERSTATETYPE State, DWORD & outValue);
	void SetStencilState(StencilState state, DWORD mask = 0xFFFFFFFF, DWORD bits = 0xFFFFFFFF);
  void SetStencilStateAddonBits(DWORD mask, DWORD bits);
	void SetSamplerState(unsigned index, SamplerState const& state);
  // Прямая запись в сэмплер в обход SetSamplerState(). Обязана инвалидировать
  // кэш lastSamplerState: без этого следующий SetSamplerState() с тем же
  // пресетом будет пропущен (сравнение решит, что сэмплер уже в нужном
  // состоянии) и запись уйдёт в никуда.
  void SetSamplerStateDirect(unsigned index, D3DSAMPLERSTATETYPE type, DWORD value);
  void SetSampler(unsigned index, SamplerState const& state, const Texture* texture)
  {
    SetSamplerState(index, state);
    SmartRenderer::BindTexture(index, texture);
  }

  // Next 2 functions return true at the case the lock state was changed
  bool LockStencil()   { return !stencilLock++; }
  bool UnlockStencil()
  {
    if(stencilLock > 0)
      return !--stencilLock;
    return false;
  }

	void UpdateStats();

  // Хэши состояний для замера полного ключа пайплайна (docs/PLAN_NVRHI.md,
  // шаг 8) и для будущих save/restore-паттернов на Vulkan-бэкенде. Считаются
  // на месте, без копирования тени. GetRenderStateShadowHash() — «сырые»
  // D3DRS_* значения (верхняя граница: на Vulkan таких номеров нет),
  // GetSamplerShadowHash()/GetStencilShadowKey() — сэмплеры и трафарет.
  // Семантика, из которой состояние порождено, доступна отдельно:
  // GetCurrentState().GetBitMask(). Разница между «семантическим» и «сырым»
  // ключом и есть цена прямых записей SetStateDirect: если полный ключ сильно
  // больше семантического, состояния меняются чаще, чем материал.
  UINT64 GetRenderStateShadowHash() const;
  UINT64 GetSamplerShadowHash() const;
  UINT64 GetStencilShadowKey() const;

  // debugging functionality
  static void DumpStates();

  // From Render::ReallocateHandler
  virtual void OnResourcesRelease();
  virtual void OnResourcesReallocate() { Init(); }

private:
  void Init();

private:
	RenderState lastRenderState;
	SamplerState lastSamplerState[16];
  StencilState lastStencilState;
  DWORD lastStencilBits;
  DWORD lastStencilMask;
  int   stencilLock;

  // Пишет состояние в устройство и обновляет тень. Все записи состояния внутри
  // менеджера обязаны идти через неё, иначе тень отстаёт от устройства и
  // GetStateDirectValue() врёт.
  void ApplyState(D3DRENDERSTATETYPE State, DWORD Value);

  // Тень состояний: индекс — числовое значение D3DRS_* (max в d3d9types.h = 209;
  // размер 256 — тот же, что у g_cachedStates).
  static const int kRenderStateShadowSize = 256;
  DWORD stateShadow[kRenderStateShadowSize];
  bool  stateShadowKnown[kRenderStateShadowSize];

	hash_map<unsigned long, DXStateBlockRef> renderStateCache;
  hash_map<unsigned long, DXStateBlockRef> samplerStateCache;
  DXStateBlockRef stencilStateCache[STENCILSTATE_COUNT];
};

}; // namespace Render