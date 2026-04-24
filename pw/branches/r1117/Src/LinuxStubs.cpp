#include "stdafx.h"

#ifdef NV_LINUX_PLATFORM
#include <string>
#include <vector>
#include "libdb/dbid.h"
#include "Game/PF/Server/Statistic/GameStatClient.h"
#include "Render/vertexformatdescriptor.h"
#include "Render/dxutils.h"

namespace NFile {
  std::string GetTempFileName() { return "/tmp/pw_tmp"; }
}

struct SCallStackEntry {};
void CollectCallStack(std::vector<SCallStackEntry>*) {}

namespace NBSU {
  bool IsIgnore(const char*, int) { return false; }
  void WriteAssertLogFile(const tm&, const char*, const std::vector<SCallStackEntry>&, bool) {}
  void AddIgnore(const char*, const char*, int) {}
  void ShowAssertionDlg(const char*, int, const char*, const std::vector<SCallStackEntry>&, bool) {}
}

extern "C" {
  char* __unDName(char* outputString, const char* name, int maxStringLength, void* (*pAlloc)(size_t), void (*pFree)(void*), unsigned short disableFlags) {
    if (outputString && maxStringLength > 0) outputString[0] = 0;
    return outputString;
  }
}

namespace roll { struct SAwardInfo { static const int ID = 0; }; const int SAwardInfo::ID; }
namespace StatisticService { namespace RPC { struct SessionClientResultsPlayer { static const int ID = 0; }; const int SessionClientResultsPlayer::ID; } }
namespace StatisticService { namespace RPC { struct SessionClientResults { static const int ID = 0; }; const int SessionClientResults::ID; } }

namespace Peered { struct ClientInfo { static const int ID = 0; }; const int ClientInfo::ID; }
namespace Peered { struct SpectatorInfo { static const int ID = 0; }; const int SpectatorInfo::ID; }

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

int g_sessionStatus = 0;
bool g_needNotifyLobbyClients = false;
std::string g_protocolToken;
int g_playerPartyId = 0;
nstl::string g_mapId;
int g_playersCount = 0;
nstl::string g_sessionName;
nstl::string g_devLogin;
int g_playerHeroId = 0;
void* g_usersData = 0;

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

#endif // NV_LINUX_PLATFORM