#include "stdafx.h"
#include "IgnoreListController.h"

#include "IgnoreListStorage.h"

#include "AdventureFlashInterface.h"
#include "FlashFSCommands.h"
#include "AdventureScreen.h"
#include "ui/FlashContainer2.h"
#include "Client/GameChatClient/IGameChatClient.h"

#include "DBStats.h"

#include "PFWorld.h"
#include "PFPlayer.h"

#include "PlayerInfoHelper.hpp"

#include "System/ConfigFiles.h"

// Pings of these players are not shown (button "!" in the statistics window): account ids (auid) "id,id,...",
// so the mute survives a nickname change and is on again in the next battles
static string g_pingMutedUsers;
REGISTER_VAR( "ping_muted_users", g_pingMutedUsers, STORAGE_USER );

static void ParsePingMutedUsers( nstl::set<NGameX::TUserId> & users )
{
  users.clear();
  unsigned long long value = 0;
  bool digits = false;
  for ( int i = 0; i <= g_pingMutedUsers.length(); ++i )
  {
    const char c = ( i < g_pingMutedUsers.length() ) ? g_pingMutedUsers[i] : ',';
    if ( c >= '0' && c <= '9' )
    {
      value = value * 10 + ( c - '0' );
      digits = true;
      continue;
    }
    if ( digits && value != 0 )
      users.insert( value );
    value = 0;
    digits = false;
  }
}

namespace NGameX
{

//////////////////////////////////////////////////////////////////////////

Strong<IPlayerIdMapper> PlayerIdMapper::Create(const NCore::TPlayersStartInfo& playerStartInfos)
{
  class Impl
    : public IPlayerIdMapper
    , public BaseObjectST
  {
    NI_DECLARE_REFCOUNT_CLASS_2(Impl, IPlayerIdMapper, BaseObjectST);
  public:
    explicit Impl(const NCore::TPlayersStartInfo& playerStartInfos)
      : playerStartInfos(playerStartInfos)
      , nicknameDummy()
    {

    }

    virtual TPlayerId GetPlayerId(const TUserId userId) const
    {
      const NCore::PlayerStartInfo* const psi = PlayerInfoHelper::FindPlayerInfoByUserId(playerStartInfos, userId);

      if (!psi)
        return -1;

      return psi->playerID;
    }

    virtual TUserId GetUserId(const TPlayerId playerId) const
    {
      const NCore::PlayerStartInfo* const psi = PlayerInfoHelper::FindPlayerInfoByPlayerId(playerStartInfos, playerId);

      if (!psi)
        return 0ULL;

      return psi->userID;
    }

    virtual const TUserNickname& GetUserNicknameByPlayerId(const TPlayerId playerId) const
    {
      const NCore::PlayerStartInfo* const psi = PlayerInfoHelper::FindPlayerInfoByPlayerId(playerStartInfos, playerId);

      if (!psi)
        return nicknameDummy;

      return psi->nickname;
    }
  private:
    Impl() {}

    const NCore::TPlayersStartInfo playerStartInfos;
    const TUserNickname nicknameDummy;
  };

  Impl* const p = new Impl(playerStartInfos);

  return p;
}

//////////////////////////////////////////////////////////////////////////

IgnoreListController::IgnoreListController(const CreateStruct& cs)
  : ignoreListStorage(cs.ignoreListStorage)
  , playerIdMapper(cs.playerIdMapper)
  , flashInterface(cs.flashInterface)
  , uiData(cs.uiData)
{
  DoInit(cs.flashContainer);
}

void IgnoreListController::OnUserAddedToIgnoreList(const TUserId auid)
{
  if (!flashInterface)
    return;

  flashInterface->IgnoreUser(GetPlayerIdByUserId(auid));
}

void IgnoreListController::OnUserRemovedFromIgnoreList(const TUserId auid)
{
  if (!flashInterface)
    return;

  flashInterface->RemoveIgnore(GetPlayerIdByUserId(auid));
}

bool IgnoreListController::IsIgnored(const TUserId auid) const
{
  if (!ignoreListStorage)
    return false;

  return ignoreListStorage->ContainsUser(auid);
}

void IgnoreListController::InvalidateFlashInterface()
{
  if (!flashInterface)
    return;

  const IgnoreList::TUserSet& ignoredUsers = ignoreListStorage->GetIgnoredUsers();

  IgnoreList::TUserSet::const_iterator it = ignoredUsers.begin();
  IgnoreList::TUserSet::const_iterator it_end = ignoredUsers.end();
  for (; it != it_end; ++it)
  {
    flashInterface->IgnoreUser(GetPlayerIdByUserId(*it));
  }

  // pings muted in the previous battles
  nstl::set<TUserId> pingMuted;
  ParsePingMutedUsers(pingMuted);
  for (nstl::set<TUserId>::const_iterator itMuted = pingMuted.begin(); itMuted != pingMuted.end(); ++itMuted)
  {
    const TPlayerId playerId = GetPlayerIdByUserId(*itMuted);
    if (playerId < 0)
      continue;
    if (AdventureScreen* const screen = AdventureScreen::Instance())
      screen->SetPlayerPingsMuted(playerId, true, true);
  }
}

void IgnoreListController::SavePingMute(const TUserId userId, const bool muted)
{
  if (userId == 0ULL) // bots, local games without accounts: for this battle only
    return;

  nstl::set<TUserId> users;
  ParsePingMutedUsers(users);
  if (muted)
    users.insert(userId);
  else
    users.erase(userId);

  string text;
  for (nstl::set<TUserId>::const_iterator it = users.begin(); it != users.end(); ++it)
    text += NStr::StrFmt(text.empty() ? "%llu" : ",%llu", (unsigned long long)(*it));
  if (text == g_pingMutedUsers)
    return;
  g_pingMutedUsers = text;
  NGlobal::SaveConfig(NProfile::GetFullFilePath("user.cfg", NProfile::FOLDER_USER), STORAGE_USER);
}

void IgnoreListController::OnFastReconnect(IgnoreListStorage *_ignoreListStorage)
{
  ignoreListStorage = _ignoreListStorage;

  if (ignoreListStorage)
    ignoreListStorage->AddListener(this);
}

void IgnoreListController::OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID, const char* args, const wchar_t * argsW )
{
  using namespace FlashFSCommands;

  if (strcmp(listenerID, "PingMuteUser") == 0)
  {
    int playerId = -1;
    int muted = 0;
    sscanf_s(args, "%d %d", &playerId, &muted);
    if (AdventureScreen* const screen = AdventureScreen::Instance())
      screen->SetPlayerPingsMuted(playerId, muted != 0, false);
    SavePingMute(GetUserIdByPlayerId(playerId), muted != 0);
    return;
  }

  if (!ignoreListStorage)
    return;

  const TPlayerId playerId = NStr::ToInt(args);
  const TUserId userId = GetUserIdByPlayerId(playerId);

  if (userId == 0U)
    return;

  switch (ConvertToFSCommand(listenerID))
  {
  case IgnoreUser:
    DevTrace("IgnoreList: Ignore #%llu", userId);
    ignoreListStorage->AddUserNickname(userId, GetUserNicknameByPlayerId(playerId));
    ignoreListStorage->AddUser(userId);
    break;
  case RemoveIgnoreFromUser:
    DevTrace("IgnoreList: Forgive #%llu", userId);
    ignoreListStorage->RemoveUser(userId);
    break;
  }
}

NGameX::TPlayerId IgnoreListController::GetPlayerIdByUserId(const TUserId userId) const
{
  if (!playerIdMapper)
    return -1;
  if (userId == 0ULL)
    return -1;

  return playerIdMapper->GetPlayerId(userId);
}

NGameX::TUserId IgnoreListController::GetUserIdByPlayerId(const TPlayerId playerId) const
{
  if (!playerIdMapper)
    return 0ULL;
  if (playerId < 0)
    return 0ULL;

  return playerIdMapper->GetUserId(playerId);
}

const TUserNickname& IgnoreListController::GetUserNicknameByPlayerId(const TPlayerId playerId) const
{
  static const TUserNickname l_dummy;

  if (!playerIdMapper)
    return l_dummy;
  if (playerId < 0)
    return l_dummy;

  return playerIdMapper->GetUserNicknameByPlayerId(playerId);
}

void IgnoreListController::DoInit(UI::FlashContainer2* const flashContainer)
{
  using namespace FlashFSCommands;

  if (!ignoreListStorage)
    return;

  ignoreListStorage->AddListener(this);

  flashContainer->AddFSListner(ConvertToString(IgnoreUser), this);
  flashContainer->AddFSListner(ConvertToString(RemoveIgnoreFromUser), this);
  flashContainer->AddFSListner("PingMuteUser", this); // "<player id> <0|1>": pings of a player off (statistics window)
}

}

NI_DEFINE_REFCOUNT(NGameX::IPlayerIdMapper)
NI_DEFINE_REFCOUNT(NGameX::IgnoreListController)
