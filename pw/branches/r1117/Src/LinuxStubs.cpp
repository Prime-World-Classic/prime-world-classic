#include "stdafx.h"

#ifdef NV_LINUX_PLATFORM
#include <string>
#include "System/nstring.h"
#include "System/nvector.h"
#include "libdb/dbid.h"
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
  bool VertexFormatDescriptor::operator==(const VertexFormatDescriptor&) const { return false; }
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
  printf(" Prime World Linux Native Client (SDL2)\n");
  printf("==================================================\n");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      printf("Failed to initialize SDL2: %s\n", SDL_GetError());
      _exit(1);
  }

  SDL_Window* win = SDL_CreateWindow("Prime World Native Linux Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  if (!win) {
      printf("Failed to create SDL2 window: %s\n", SDL_GetError());
      _exit(1);
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(win);
  if (!glContext) {
      printf("Failed to create GL context: %s\n", SDL_GetError());
      _exit(1);
  }
  
  printf("SDL2 Window and GL Context created successfully. Booting bypass...\n");

  Render::Renderer::Init((unsigned int)(uintptr_t)win);
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

  // Initialize UIRenderer
  Render::GetUIRenderer()->Initialize();

  // Infinite loop
  bool running = true;
  while(running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      }
    }
    
    Render::Renderer::Get()->ClearColorOnly(Render::Color(0, 128, 255, 255));
    Render::Renderer::Get()->BeginScene();

    Render::GetUIRenderer()->StartFrame();
    Render::GetUIRenderer()->BeginQueue();

    // Draw a test quad
    Render::UIQuad quad;
    quad.tl = CVec2(100.0f, 100.0f);
    quad.br = CVec2(500.0f, 500.0f);
    quad.uv = CVec2(0.0f, 0.0f);
    quad.uvl = CVec2(1.0f, 1.0f);
    quad.uv2 = CVec2(0.0f, 0.0f);
    quad.uvl2 = CVec2(1.0f, 1.0f);
    quad.ext = false;
    
    Render::SMaterialParams matParams;
    matParams.color0 = Render::Color(255, 0, 0, 255); // Red

    Render::UIRenderMaterial dummyMat;
    dummyMat.CreateDefaultMaterial();

    Render::GetUIRenderer()->AddQuad(quad, dummyMat.GetRenderMaterial(), matParams);

    Render::GetUIRenderer()->EndQueue();

    // Now tell UIRenderer to actually render the queued parts
    Render::GetUIRenderer()->Render(Render::ERenderWhat::_2D, NULL, NULL);

    Render::Renderer::Get()->EndScene();
    Render::Renderer::Get()->Present();

    fflush(stdout);
    fflush(stderr);
  }

  Render::GetUIRenderer()->Release();
  Render::Renderer::Term();

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
  void DeleteOldFiles(char const*, double) {}
}

void RegisterReplayFileExtentionAssociation() {}

namespace NDebug {
  unsigned int GetVirtualAllocCount() { return 0; }
  unsigned int GetTotalVirtualAllocSize() { return 0; }
  unsigned int GetUnfreeVirtualAllocCount() { return 0; }
  unsigned int GetTotalHeapAllocSize() { return 0; }
}

namespace Input {
  class IHwInput {};
  IHwInput* CreateHwInput(void*, void*, bool, bool) { return nullptr; }
}

#include "PW_Client/LocalGameContext.h"
namespace Game {
  LocalGameContext::LocalGameContext(bool) {}
  LocalGameContext::~LocalGameContext() {}
  void LocalGameContext::Start() {}
  void LocalGameContext::CreateGame(const char*, int) {}
  void LocalGameContext::ChangeCustomGameSettings(lobby::ETeam::Enum, lobby::ETeam::Enum, const nstl::string&) {}
  void LocalGameContext::SetReady(lobby::EGameMemberReadiness::Enum) {}
  void LocalGameContext::SetDeveloperParty(int) {}
  int LocalGameContext::Poll(float) { return 0; }
  void LocalGameContext::Shutdown() {}
  void LocalGameContext::OnAltTab(bool) {}
  void LocalGameContext::SetTimeScale(float) {}
  void LocalGameContext::OnCombatScreenStarted(NCore::IWorldBase*, const NGameX::ReplayInfo&) {}
  void LocalGameContext::OnVictory(const StatisticService::RPC::SessionClientResults&, const NGameX::ReplayInfo&) {}
  void LocalGameContext::LeaveGame() {}
  bool LocalGameContext::IsGameReady() { return true; }
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