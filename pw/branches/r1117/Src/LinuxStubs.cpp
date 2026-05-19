#include "stdafx.h"

#ifdef NV_LINUX_PLATFORM
void* g_sdlWindow = NULL;

#include <string>
#include "System/nstring.h"
#include "System/nvector.h"
#include "libdb/dbid.h"
#include "System/MainFrame.h"
#include "Game/PF/Server/Statistic/GameStatClient.h"
#include "Render/vertexformatdescriptor.h"
#include "Render/dxutils.h"

namespace NFile {
  nstl::string GetTempFileName() { return "/tmp/pw_tmp"; }
}

struct SCallStackEntry {};
void CollectCallStack(nstl::vector<SCallStackEntry>*) {}

namespace NBSU {
  bool IsIgnore(const char*, int) { return false; }
  void WriteAssertLogFile(const tm&, const char*, const nstl::vector<SCallStackEntry>&, bool) {}
  void AddIgnore(const char*, const char*, int) {}
  void ShowAssertionDlg(const char*, int, const char*, const nstl::vector<SCallStackEntry>&, bool) {}
}

extern "C" {
  char* __unDName(char* outputString, const char* name, int maxStringLength, void* (*pAlloc)(size_t), void (*pFree)(void*), unsigned short disableFlags) {
    if (outputString && maxStringLength > 0) outputString[0] = 0;
    return outputString;
  }
}

namespace StatisticService { namespace RPC { struct SessionClientResultsPlayer { static const int ID = 0; }; const int SessionClientResultsPlayer::ID; } }
namespace StatisticService { namespace RPC { struct SessionClientResults { static const int ID = 0; }; const int SessionClientResults::ID; } }

namespace StatisticService { 
  bool GameStatClient::Ready() const { return false; } 
  bool GameStatClient::IsMethodInabled(int) const { return false; } 
  void GameStatClient::PostRawMessage(unsigned int, const TMessageData&) {} 
}

namespace Input { int GetVerbosityLevel() { return 0; } }

namespace Protection {
  void CallFunctionInProtectedSpace(void (*func)(void const*), void const* param) { func(param); }
  void CheckReadOnlyAndExecutable() {}
  void CheckSystemDlls() {}
}

bool g_localGameRun = false;
bool g_playerPwcChatMute = false;

nstl::vector<std::pair<int, int>> playersKills;

bool g_needNotifyLobbyClients = false;
extern std::string g_protocolToken;
int g_playerPartyId = 0;
nstl::string g_mapId;
int g_playersCount = 0;
nstl::string g_sessionName;
nstl::string g_devLogin;
int g_playerHeroId = 0;

int GetSkinByHeroPersistentId(const std::string&, int) { return 0; }

bool GetVideoMemoryViaDirectDraw(void* pGuid, unsigned int* pMem) { return false; }
bool GetVideoMemoryViaWMI(void* pGuid, unsigned int* pMem) { return false; }

namespace Render {
  template<class T> void SharedD3DBufferST<T>::QuerySize(unsigned int) {}
  template void SharedD3DBufferST<DXVertexBufferDynamicRef>::QuerySize(unsigned int);
  template void SharedD3DBufferST<DXIndexBufferDynamicRef_<16u>>::QuerySize(unsigned int);
  template void SharedD3DBufferST<DXIndexBufferDynamicRef_<32u>>::QuerySize(unsigned int);
}

extern "C" {
  D3DXVECTOR3* WINAPI D3DXVec3TransformCoord(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM) { return pOut; }
  D3DXVECTOR3* WINAPI D3DXVec3TransformNormal(D3DXVECTOR3* pOut, const D3DXVECTOR3* pV, const D3DXMATRIX* pM) { return pOut; }
  D3DXVECTOR3* WINAPI D3DXVec3TransformCoordArray(D3DXVECTOR3* pOut, UINT OutStride, const D3DXVECTOR3* pV, UINT VStride, const D3DXMATRIX* pM, UINT n) { return pOut; }
  D3DXVECTOR2* WINAPI D3DXVec2TransformNormalArray(D3DXVECTOR2* pOut, UINT OutStride, const D3DXVECTOR2* pV, UINT VStride, const D3DXMATRIX* pM, UINT n) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX* pOut, const D3DXMATRIX* pM) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX* pOut, FLOAT* pDeterminant, const D3DXMATRIX* pM) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixOrthoOffCenterLH(D3DXMATRIX* pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX* pOut, const D3DXMATRIX* pM1, const D3DXMATRIX* pM2) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixTranslation(D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixPerspectiveLH(D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixOrthoLH(D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf) { return pOut; }
  D3DXMATRIX* WINAPI D3DXMatrixLookAtLH(D3DXMATRIX* pOut, const D3DXVECTOR3* pEye, const D3DXVECTOR3* pAt, const D3DXVECTOR3* pUp) { return pOut; }
  HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData, UINT SrcDataSize, UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO* pSrcInfo, PALETTEENTRY* pPalette, LPDIRECT3DTEXTURE9* ppTexture) { return 0; }
  HRESULT WINAPI D3DXCreateCubeTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData, UINT SrcDataSize, UINT Size, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO* pSrcInfo, PALETTEENTRY* pPalette, LPDIRECT3DCUBETEXTURE9* ppCubeTexture) { return 0; }
  HRESULT WINAPI D3DXSHProjectCubeMap(UINT Order, LPDIRECT3DCUBETEXTURE9 pCubeMap, FLOAT* pROut, FLOAT* pGOut, FLOAT* pBOut) { return 0; }
  HRESULT WINAPI D3DXSHEvalDirectionalLight(UINT Order, const D3DXVECTOR3* pDir, FLOAT RIntensity, FLOAT GIntensity, FLOAT BIntensity, FLOAT* pROut, FLOAT* pGOut, FLOAT* pBOut) { return 0; }
}

namespace NFile {
  bool EraseFile(const nstl::string&) { return false; }
  bool ExecuteFile(const nstl::string&) { return false; }
}

extern "C" {
  int FilterAsync(const wchar_t* p) { return 0; }
  bool GetFilteredAsync(int& id, wchar_t* p, int max_len) { return false; }
}

namespace NSoundScene {
  struct SoundDescription { SoundDescription(); };
  SoundDescription::SoundDescription() {}
  void PlaySound(const SoundDescription&, float*) {}
  void DeleteSound(int) {}
}

namespace NWorld {
  struct PFResourcesCollection { void CollectConsumables(); };
  void PFResourcesCollection::CollectConsumables() {}
}

#include "Network/TransportAddress.h"
#include "Network/TransportDefaults.h"
namespace Transport {
  const char* AddressToString(const Address&) { return ""; }
  int GetServerIndex(const FixedString<64u, char>& ) { return 0; }
  bool GetServiceClass(const TServiceId&, TServiceId&) { return false; }
}

unsigned int Transport::Defaults::GetOpenChannelTimeout() { return 0; }
unsigned int Transport::Defaults::GetPingPeriod() { return 0; }

namespace PF_Minigames {
  struct EaselEventSounds { EaselEventSounds(); };
  EaselEventSounds::EaselEventSounds() {}
  struct EaselEventNullSounds { EaselEventNullSounds(); };
  EaselEventNullSounds::EaselEventNullSounds() {}
}

class CObjectBase;
namespace NSoundScene { class ISimpleSound; }
template <typename T> CObjectBase* CastToObjectBaseImpl(T*, void*) { return 0; }
template CObjectBase* CastToObjectBaseImpl<NSoundScene::ISimpleSound>(NSoundScene::ISimpleSound*, void*);

#include <System/BlockData/src/BlockDataCommon.h>
#include <boost/thread.hpp>
namespace nvl {
  template<> boost::thread_specific_ptr< CPtr< CPoolableMTEntity< bds::CBinaryBlock >::CStack > > CPoolableMTEntity< bds::CBinaryBlock >::m_Pool(0);
  template<> boost::thread_specific_ptr< CPtr< CPoolableMTEntity< bds::CTextBlock >::CStack > > CPoolableMTEntity< bds::CTextBlock >::m_Pool(0);
  template<> boost::thread_specific_ptr< CPtr< CPoolableMTEntity< bds::CTerminatorBase::CRegisterDataFlowEvent >::CStack > > CPoolableMTEntity< bds::CTerminatorBase::CRegisterDataFlowEvent >::m_Pool(0);
  template<> boost::thread_specific_ptr< CPtr< CPoolableMTEntity< bds::CDataFlowProxy >::CStack > > CPoolableMTEntity< bds::CDataFlowProxy >::m_Pool(0);
}

#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "Render/DBRender.h"
#include "Render/GLRenderer.h"
#include "Render/renderer.h"
#include "Render/smartrenderer.h"
#include "Render/ImmediateRenderer.h"
#include "Render/uirenderer.h"

struct SPluginSettings;
extern int __stdcall PseudoWinMain( void* hInstance, void* hWnd, char* lpCmdLine, SPluginSettings* pluginSett );

extern "C" {
void StartPWApplication(void* hWnd, int argc, char** argv) {
  printf("==================================================\n");

  printf("Starting SDL_Init...\n");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      printf("Failed to initialize SDL2: %s\n", SDL_GetError());
      _exit(1);
  }
  printf("SDL_Init succeeded.\n");

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  printf("Creating SDL window...\n");
  SDL_Window* win = SDL_CreateWindow("Prime World Native Linux Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_OPENGL);
  if (!win) {
      printf("Failed to create SDL2 window: %s\n", SDL_GetError());
      _exit(1);
  }
  printf("SDL window created: %p\n", win);
  g_sdlWindow = win;

  printf("Creating GL context...\n");
  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
      printf("Failed to create GL context: %s\n", SDL_GetError());
      _exit(1);
  }
  printf("GL context created: %p\n", glContext);

  // Set the window for the renderer
  {
      typedef void (*PFNSETWINDOW)(void*);
      // We can't easily call GLDirect3DDevice9::SetSDLWindow directly without header mess, 
      // but we know it's there. Actually, let's just use a global or something.
      // For now, let's assume the renderer will find it.
  }
  
  SDL_ShowWindow(win);
  SDL_RaiseWindow(win);

  SDL_ShowWindow(win);
  
  printf("SDL2 Window and GL Context created successfully. Booting bypass...\n");

  Render::Renderer::Init((uintptr_t)win);
  Render::RenderMode renderMode;
  renderMode.width = 1024;
  renderMode.height = 768;
  renderMode.isFullScreen = false;
  renderMode.vsyncCount = 0;

  if (!Render::Renderer::Get()->Start(renderMode)) {
      printf("Renderer::Start failed\n");
      _exit(1);
  }

  // Set SDL window explicitly because Renderer doesn't know about it natively
  ((GLDirect3DDevice9*)Render::Renderer::Get()->GetDevice())->SetSDLWindow(win);

  Render::ImmRenderer::Init();
  Render::SmartRenderer::Init();

  // Start the actual game engine
  PseudoWinMain( 0, (void*)win, (char*)"", 0 );

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
  _exit(0);
}
void StartPWPlugin(void* hWnd, int width, int height, bool fullscreen, const char* sessionLogin) {}
}

namespace profiler3ui {
  void Init() {}
  void Shutdown() {}
}

extern "C" {
bool SteamAPI_Init() { return false; }
void SteamAPI_Shutdown() {}
void* SteamUtils() { return nullptr; }
}

namespace CensorFilter {
  int LoadDictionary(const wchar_t*, bool) { return 0; }
}

extern "C" void LoadDictionary() {}

namespace CrashRptWrapper {
  void InstallForProcess(char const*, bool, bool, char const*, char const*, bool, bool) {}
  void UninstallFromProcess() {}
}

namespace NBSU {
  void InitUnhandledExceptionHandler() {}
}

namespace NFile {
  void CheckFileAccess( const char * _fileName, bool _readOnly ) {}
  void DeleteOldFiles(char const*, double) {}
}

void RegisterReplayFileExtentionAssociation() {}

namespace NDebug {
  unsigned int GetVirtualAllocCount() { return 0; }
  unsigned int GetTotalVirtualAllocSize() { return 0; }
  unsigned int GetUnfreeVirtualAllocCount() { return 0; }
  unsigned int GetTotalHeapAllocSize() { return 0; }
}

#include "NivalInput/HwInputInterface.h"
#include "NivalInput/Binds.h"

namespace Input {
  class LinuxHwInput : public IHwInput, public CObjectBase {
    OBJECT_BASIC_METHODS(LinuxHwInput);
    mutable nstl::map<nstl::string, int> names;
    mutable nstl::map<int, nstl::string> idToName;
  public:
    LinuxHwInput() {}
    virtual int FindControlZ( const char * name ) const { return FindControl(nstl::string(name)); }
    virtual int FindControl( const nstl::string & name ) const {
        if (names.find(name) == names.end()) {
            int id = names.size();
            fprintf(stderr, "LinuxHwInput: registering control %s as %d\n", name.c_str(), id);
            names[name] = id;
            idToName[id] = name;
        }
        return names[name];
    }
    virtual const nstl::string & ControlName( int id ) const { 
        if (idToName.find(id) != idToName.end()) return idToName[id];
        static nstl::string empty; return empty; 
    }
    virtual bool GetReadableControlName( int id, nstl::string & name ) const { 
        if (idToName.find(id) != idToName.end()) {
            name = idToName[id];
            return true;
        }
        return false; 
    }
    virtual bool ControlIsAKey( int id ) const { 
        const nstl::string& name = ControlName(id);
        if (name.substr(0, 10) == "MOUSE_AXIS") return false;
        return true; 
    }
    virtual void Poll( nstl::vector<HwEvent> & events ) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    {
                        const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(event.key.keysym.scancode));
                        // Map common SDL names to game names
                        nstl::string ctrlName = name;
                        std::transform(ctrlName.begin(), ctrlName.end(), ctrlName.begin(), ::toupper);
                        if (ctrlName == "ESCAPE") ctrlName = "ESC";
                        else if (ctrlName == "LEFT CTRL") ctrlName = "LCTRL";
                        else if (ctrlName == "RIGHT CTRL") ctrlName = "RCTRL";
                        else if (ctrlName == "LEFT SHIFT") ctrlName = "LSHIFT";
                        else if (ctrlName == "RIGHT SHIFT") ctrlName = "RSHIFT";
                        else if (ctrlName == "LEFT ALT") ctrlName = "LALT";
                        else if (ctrlName == "RIGHT ALT") ctrlName = "RALT";
                        else if (ctrlName == "RETURN") ctrlName = "ENTER";
                        
                        events.push_back(HwEvent(FindControl(ctrlName), event.type == SDL_KEYDOWN));
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    {
                        char name[32];
                        sprintf(name, "MOUSE_BUTTON%d", event.button.button - 1);
                        events.push_back(HwEvent(FindControl(name), event.type == SDL_MOUSEBUTTONDOWN));
                    }
                    break;
                case SDL_MOUSEMOTION:
                    {
                        events.push_back(HwEvent(FindControl("MOUSE_AXIS_X"), (float)event.motion.x, true));
                        events.push_back(HwEvent(FindControl("MOUSE_AXIS_Y"), (float)event.motion.y, true));
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    {
                        events.push_back(HwEvent(FindControl("MOUSE_AXIS_Z"), (float)event.wheel.y, false));
                    }
                    break;
                case SDL_QUIT:
                    NMainFrame::Exit();
                    break;
            }
        }
    }
    virtual void OnApplicationFocus( bool focused ) {}
  };

  IHwInput * CreateHwInput( HWND hWnd, HINSTANCE hInstance, bool debugMouse, bool nonExclusiveMode ) {
    return new LinuxHwInput();
  }
}

#include "System/Commands.h"

namespace {
  nstl::string s_stat_client_url;
  int s_mmaking_pcbt_mode = 0;
  int s_rdp_log_events = 0;
  int s_rs_keepalive_timeout = 0;
  bool s_mmaking_enabled = false;
  int s_mmaking_sex = 0;
  bool s_lobby_customization = false;
  bool s_enable_cursor_clip = false;
  bool s_debug_disable_world_crc = false;

  REGISTER_VAR( "stat_client_url", s_stat_client_url, STORAGE_NONE );
  REGISTER_VAR( "mmaking_pcbt_mode", s_mmaking_pcbt_mode, STORAGE_NONE );
  REGISTER_VAR( "rdp_log_events", s_rdp_log_events, STORAGE_NONE );
  REGISTER_VAR( "rs_keepalive_timeout", s_rs_keepalive_timeout, STORAGE_NONE );
  REGISTER_VAR( "mmaking_enabled", s_mmaking_enabled, STORAGE_NONE );
  REGISTER_VAR( "mmaking_sex", s_mmaking_sex, STORAGE_NONE );
  REGISTER_VAR( "lobby_customization", s_lobby_customization, STORAGE_NONE );
  REGISTER_VAR( "enable_cursor_clip", s_enable_cursor_clip, STORAGE_NONE );
  REGISTER_VAR( "debug_disable_world_crc", s_debug_disable_world_crc, STORAGE_NONE );

  bool CommandLobby( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: lobby command called\n");
      return true; 
  }
  REGISTER_CMD( lobby, CommandLobby );

  bool CommandLogin( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: login command called\n");
      return true; 
  }
  REGISTER_CMD( login, CommandLogin );

  bool CommandCustom( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: custom command called\n");
      return true; 
  }
  REGISTER_CMD( custom, CommandCustom );

  bool CommandSettings( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: settings command called\n");
      return true; 
  }
  REGISTER_CMD( settings, CommandSettings );

  bool CommandReady( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: ready command called\n");
      return true; 
  }
  REGISTER_CMD( ready, CommandReady );

  bool CommandAddGold( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: add_gold command called\n");
      return true; 
  }
  REGISTER_CMD( add_gold, CommandAddGold );

  bool CommandWaitWorldStep( const char *, const nstl::vector<nstl::wstring> & ) { 
      printf("STUB: waitWorldStep command called\n");
      return true; 
  }
  REGISTER_CMD( waitWorldStep, CommandWaitWorldStep );
}

#include "Game/PF/HybridServer/PeeredTypes.h"
namespace Peered { int ClientInfo::ID = 0; int SpectatorInfo::ID = 0; }

namespace Game {
  class LocalCmdScheduler;
}

namespace ni_detail {
  template <typename T, typename U> T* CastToBaseObjectImpl(U*, void*) { return nullptr; }
  template BaseObjectMT* CastToBaseObjectImpl<BaseObjectMT, Game::LocalCmdScheduler>(Game::LocalCmdScheduler*, void*);
}

#endif // NV_LINUX_PLATFORM
