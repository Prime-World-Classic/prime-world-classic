#include "stdafx.h"
#include "ConfigManager.h"
#include "configdatabase.h"
#include "renderer.h"        // GetBackend() — caps приходят от бэкенда (шаг 4)
#include "renderbackend.h"   // BackendSystemInfo
#include "System/CmdLineLite.h"


static bool s_readableDepth = true;
static bool s_stencilEnable = true;


HRESULT GetVideoMemoryViaDirectDraw(HMONITOR hMonitor, DWORD* pdwAvailableVidMem); // returns total video memory minus driver-reserved memory.
HRESULT GetVideoMemoryViaWMI(HMONITOR hMonitor, DWORD* pdwAdapterRam);


namespace Render
{

class ConfigManagerImpl : public ConfigManager, public IConfigManager
{
public:
  ConfigManagerImpl(void);
  ~ConfigManagerImpl(void) {}

  HRESULT CheckDeviceCaps(IDirect3DDevice9* _pDevice);

  // SetPerfomanceLevel - level 0 is of most quality
  void SetPerfomanceLevel(ResourceType _type, UINT _level);
  // Get adapter video memory in MB; _pAvailableVidMem shows total video memory minus driver-reserved memory
  virtual void GetVidMem(HMONITOR _hMonitor, UINT* _pAvailableVidMem, UINT* _pAdapterRAM);

  const IConfigManager::SysInfo& GetSysInfo() const { return sysInfo; }
  UINT GetPerfomanceLevel(ResourceType _type) const { return perfLevels[_type]; }
  UINT FindAcceptableTechnique(const TechniqueDesc **_pDesc, UINT numDesc) const;
  bool StencilSupported(ERenderFormat _depthFormat) const;
  ERenderFormat GetDepthFormatPreffered() const;
  bool CheckFeatureSupport(DWORD _features) const;
  bool CheckFeatureSupportExact(DWORD _features) const;
  DWORD      GetAllFeatures() const { return caps; }
  bool ReadableDepthEnabled() const { return s_readableDepth && ReadableDepthSupported(); }
  const ConfigParams& GetConfigParams() const { return config_params; }

  bool ReadableDepthSupported() const;

protected:
  DWORD caps;
  UINT perfLevels[NUM_RESOURCE_TYPES];
  vector<StringPair> config_params;
  IConfigManager::SysInfo sysInfo;
};

ConfigManagerImpl::ConfigManagerImpl() : caps()
{
  ZeroMemory( perfLevels, sizeof(perfLevels) );
}


// SetFlag удалён: флаги caps больше не выставляются по одному — маску
// приносит бэкенд целиком (BackendSystemInfo::features, шаг 4).

// Дамп разрешённых пинов config.txt (docs/PLAN_NVRHI.md §2.5).
// config.txt — это матчер: из идентификатора адаптера, caps и переданных
// 1024/64/1700 он разрешает список команд, которые затем исполняются как
// setvar. На этапе 3 вместо D3DCAPS9 придёт синтезированный BackendSystemInfo,
// и обязан разрешиться ТОТ ЖЕ список. Сравнивать будет не с чем, если не снять
// эталон, пока D3D9 жив, — отсюда этот дамп.
// Ключ командной строки, а не только dev-переменная: CheckDeviceCaps() работает
// ДО NGlobal::ExecuteConfig("autoexec.cfg") (тот исполняется в PseudoWinMain
// позже инициализации рендера), поэтому setvar из autoexec.cfg до этого места
// не доходит — проверено прогоном. Переменная оставлена для интерактивной
// отладки, когда порядок не важен.
static bool s_dumpConfigPins = false;
REGISTER_DEV_VAR( "dump_config_pins", s_dumpConfigPins, STORAGE_NONE );

static bool DumpConfigPinsRequested()
{
  return s_dumpConfigPins
      || CmdLineLite::Instance().IsKeyDefined( "dumpConfigPins" )
      || CmdLineLite::Instance().IsKeyDefined( "-dumpConfigPins" );
}

// Форс флагов depth-текстур (hasD16T/hasD24X/hasD24S) — замена отсутствующему
// GPU-железу по части корректности: без этих битов HasDSM() (единый гейт
// аппаратных карт теней) путь shadow maps не исполняется вообще.
//
// НА E2E-СТЕНДЕ (tiny10, «Microsoft Basic Render Driver») БЕСПОЛЕЗЕН И ОПАСЕН:
// устройство не умеет depth-форматы как текстуру — CheckDeviceFormat
// (DEPTHSTENCIL, TEXTURE) = no для D16/D24X8/D24S8, а реальная попытка
// CreateTexture даёт 0x8876086C (замер tools/dsm_probe, 2026-10-15,
// docs/PLAN_NVRHI.md §2.15 п.9). Форс превратил бы «путь не исполняется» в
// NI_DX_THROW на создании ресурса. Механизм оставлен для машин с реальным
// D3D9-железом и для прогонов поверх NvrhiBackend (этап 3), где depth-текстуры
// есть; в E2E на tiny10 не включается и новый визуальный эталон не создаётся.
static bool s_forceDsm = false;
REGISTER_DEV_VAR( "force_dsm", s_forceDsm, STORAGE_NONE );

static bool ForceDsmRequested()
{
  return s_forceDsm
      || CmdLineLite::Instance().IsKeyDefined( "forceDsm" )
      || CmdLineLite::Instance().IsKeyDefined( "-forceDsm" );
}


HRESULT ConfigManagerImpl::CheckDeviceCaps(IDirect3DDevice9* _pDevice)
{
  ASSERT(_pDevice);

  HRESULT hr = E_FAIL;

  // Возможности приходят от бэкенда (шаг 4): D3DCAPS9 здесь больше НЕ источник
  // caps — он остался только матчеру config.txt (AdapterOrdinal/DeviceType/маски
  // caps), у которого нейтрального словаря пока нет (§2.5).
  BackendSystemInfo backendInfo = {};
  hr = GetBackend()->GetSystemInfo( backendInfo );
  NI_DX_THROW( hr, "IRenderBackend::GetSystemInfo failed" );

  sysInfo.MaxVertexShaderConst = backendInfo.maxVertexShaderConst;
  sysInfo.IsNullRef            = backendInfo.isNullRef;
  caps                         = backendInfo.features;
  if( ForceDsmRequested() )
    caps |= hasD16T | hasD24X | hasD24S;    // см. комментарий у dev-var'а

  D3DCAPS9 caps9;
  _pDevice->GetDeviceCaps(&caps9);

  // Диагностика порядка запуска: дамп пинов завязан на ключ командной строки, а
  // CmdLineLite инициализируется в PseudoWinMain — проверяем, что к моменту
  // создания устройства он уже заполнен.
  DebugTrace( "CheckDeviceCaps: args=%d dumpConfigPins=%d",
              CmdLineLite::Instance().ArgsCount(),
              (int)CmdLineLite::Instance().IsKeyDefined( "-dumpConfigPins" ) );

  D3DADAPTER_IDENTIFIER9 adapterID;
  IntrusivePtr<IDirect3D9> pD3D;

  {
    IDirect3D9 *p;
    hr = _pDevice->GetDirect3D( &p );
    NI_DX_THROW(hr, "Corrupted D3D device");
    pD3D.Attach(p);
  }   
  
  hr = pD3D->GetAdapterIdentifier(caps9.AdapterOrdinal, 0, &adapterID);

  sysInfo.IsNullRef = (D3DDEVTYPE_NULLREF == caps9.DeviceType);
  //if(D3DDEVTYPE_REF == caps9.DeviceType) // Probably this mean it is NULLREF actually. OR this mean we are under PerfHUD.
  //  sysInfo.IsNullRef = !strstr(adapterID.Description, "PerfHUD");

  if(D3DDEVTYPE_REF == caps9.DeviceType)  // Probably this mean it is NULLREF actually.
    sysInfo.IsNullRef = NGlobal::GetVar("nullrender").GetInt64();

  if( IConfigDatabase* const pCDB = IConfigDatabase::Create() )
  {
    SOUND_DEVICE sndDev;
    if( pCDB->Load("config.txt", sndDev, adapterID, caps9, 1024, 64, 1700) )
        config_params = pCDB->GetAggregateProperties();
    pCDB->Release();

    if( DumpConfigPinsRequested() )
    {
      // Входы матчера: без них список пинов нельзя воспроизвести на другом
      // железе (или при другом синтезе caps). 1024/64/1700 — жёстко зашитые
      // SystemMemory/VideoMemory/CPUSpeed, их решение менять не будем (§2.5).
      // Идентичность и возможности печатаются из BackendSystemInfo — т.е. из
      // того же источника, что теперь читает клиент (шаг 4); сравнение с
      // эталоном 2026-10-14 (§2.5) работает без изменений.
      // Форматтер клиента %llu не понимает (в Render такого нет нигде) —
      // версия драйвера печатается двумя половинками.
      DebugTrace( "ConfigPins: adapter=\"%s\" vendor=0x%04x device=0x%04x driver=%u.%u "
                  "mem=1024 vid=64 cpu=1700 MaxVertexShaderConst=%u IsNullRef=%d caps=0x%x pins=%d",
                  backendInfo.description, backendInfo.vendorId, backendInfo.deviceId,
                  (unsigned)(backendInfo.driverVersion >> 32),
                  (unsigned)(backendInfo.driverVersion & 0xffffffffULL),
                  sysInfo.MaxVertexShaderConst, (int)sysInfo.IsNullRef, caps,
                  (int)config_params.size() );
      for( size_t i = 0; i < config_params.size(); ++i )
        DebugTrace( "ConfigPin: %s %s", config_params[i].first.c_str(), config_params[i].second.c_str() );
    }

    for(int i = config_params.size(); --i >= 0;) {
      const StringPair& curProperty = config_params[i];
      if( NGlobal::IsCommandRegistred(curProperty.first) )
        if( curProperty.second.empty() )
          NGlobal::RunCommand( NStr::ToUnicode(curProperty.first) );
        else
          NGlobal::RunCommand( NStr::ToUnicode(curProperty.first) + wstring(L" ")
                              + NStr::ToUnicode(curProperty.second) );
    }
  }

  return hr;
}


void ConfigManagerImpl::SetPerfomanceLevel(ResourceType _type, UINT _level)
{
  perfLevels[_type] = _level;
}


void ConfigManagerImpl::GetVidMem(HMONITOR _hMonitor, UINT* _pAvailableVidMem, UINT* _pAdapterRAM)
{
  DWORD dwAvailableVidMem;
  HRESULT hr = GetVideoMemoryViaDirectDraw( _hMonitor, &dwAvailableVidMem );
  *_pAvailableVidMem = SUCCEEDED(hr) ? dwAvailableVidMem / 1024 / 1024 : 0;

  DWORD dwAdapterRAM;
  hr = GetVideoMemoryViaWMI( _hMonitor, &dwAdapterRAM );
  *_pAdapterRAM = SUCCEEDED(hr) ? dwAdapterRAM / 1024 / 1024 : 0;
}


UINT ConfigManagerImpl::FindAcceptableTechnique(const TechniqueDesc **_pDesc, UINT numDesc) const
{
  ASSERT(numDesc > 0);
  UINT n = 0;
  do {
    const TechniqueDesc &desc = *(_pDesc[n]);
    if( desc.flags != (desc.flags & caps) )
      continue;
    break;
  } while(++n < numDesc);
  return n;
}


bool ConfigManagerImpl::CheckFeatureSupport(DWORD _features) const
{
  return _features & caps;
}


bool ConfigManagerImpl::CheckFeatureSupportExact(DWORD _features) const
{
  return _features == (_features & caps);
}


bool ConfigManagerImpl::ReadableDepthSupported() const
{
  // Читаемая глубина была завязана на vendor-форматы (INTZ/RAWZ/RESZ).
  // Под Vulkan/DX12 depth-буфер читается как depth SRV всегда, так что
  // гейт сводится к наличию depth-текстуры (она же — гейт аппаратных
  // карт теней).
  return 0 != (caps & (hasD16T|hasD24X|hasD24S));
}


bool ConfigManagerImpl::StencilSupported(ERenderFormat _depthFormat) const
{
  return s_stencilEnable && (FORMAT_D24S8 == _depthFormat);
}


ERenderFormat ConfigManagerImpl::GetDepthFormatPreffered() const
{
  // Vendor-форматы (INTZ/RAWZ/DF16) удалены: единый читаемый формат
  // глубины со stencil — D24S8 (под Vulkan/DX12 -> D24_UNORM_S8_UINT).
  DebugTrace("DepthFormat: D24S8");
  return FORMAT_D24S8;
}

///////////////////////////////////////////////////////////////////////////////


ConfigManager* GetConfigManager()
{
  static ConfigManagerImpl manager;
  return &manager;
}

const IConfigManager* GetIConfigManager()
{
  return static_cast<ConfigManagerImpl*>( GetConfigManager() );
}

}; // namespace Render


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//static bool EnableReadableDepth( const char *name, const vector<wstring> &args )
//{
//  if( args.empty() )
//    return false;
//
//  const bool enable( 0 != NStr::ToULong(NStr::ToMBCS(args[0])) );
//  s_readableDepth = enable && static_cast<Render::ConfigManagerImpl*>(Render::GetConfigManager())->ReadableDepthSupported();
//
//  return enable == s_readableDepth;
//}
//
//REGISTER_CMD( readableDepthEnable, EnableReadableDepth );

REGISTER_VAR("readableDepthEnable", s_readableDepth, STORAGE_NONE);
REGISTER_DEV_VAR("stencilEnable", s_stencilEnable, STORAGE_NONE);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
