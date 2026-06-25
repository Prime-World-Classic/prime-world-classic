#pragma once

#include "../PF_GameLogic/DBStats.h"
#include "../PF_GameLogic/IFlashChat.h"

#if defined(PW_LINUX_DB_BOOTSTRAP)
namespace UI
{
  class FlashContainer2;
}
#else
#include "../UI/FlashInterface.h"
#endif

namespace Game
{

#if defined(PW_LINUX_DB_BOOTSTRAP)
struct LoadingFlashHeroState
{
  int slotId;
  NDb::EFaction faction;
  wstring playerName;
  string iconPath;
  int heroLevel;
  bool isMale;
  string classIcon;
  uint partyId;
  string flagIcon;
  wstring flagTooltip;
  bool isAnimatedAvatar;
  int leagueIndex;
  float loadProgress;
  bool isLeftGame;
  int force;
  bool hasPremium;
  NDb::EFaction originalFaction;
  int rating;
  int ratingAcc;
  bool isNovice;
  string rankIcon;
  wstring rankName;
  string rankAccIcon;
  wstring rankAccName;

  LoadingFlashHeroState()
    : slotId(-1),
      faction(NDb::FACTION_NEUTRAL),
      heroLevel(0),
      isMale(true),
      partyId(0),
      isAnimatedAvatar(true),
      leagueIndex(0),
      loadProgress(0.0f),
      isLeftGame(false),
      force(0),
      hasPremium(false),
      originalFaction(NDb::FACTION_NEUTRAL),
      rating(0),
      ratingAcc(0),
      isNovice(false)
  {
  }
};

struct LoadingFlashModeDescriptionState
{
  string modeImage;
  int id;

  LoadingFlashModeDescriptionState()
    : id(-1)
  {
  }
};

struct LoadingFlashChatChannelState
{
  NDb::EChatChannel channel;
  wstring channelName;
  uint channelColor;
  bool showChannelName;
  bool showPlayerName;
  bool canWrite2Channel;
  wstring shortcut;

  LoadingFlashChatChannelState()
    : channel(NDb::CHATCHANNEL_GLOBAL),
      channelColor(0),
      showChannelName(false),
      showPlayerName(false),
      canWrite2Channel(false)
  {
  }
};

struct LoadingFlashChatMessageState
{
  NDb::EChatChannel channel;
  wstring playerName;
  wstring message;
  int playerId;
  bool hasPlayerId;

  LoadingFlashChatMessageState()
    : channel(NDb::CHATCHANNEL_GLOBAL),
      playerId(-1),
      hasPlayerId(false)
  {
  }
};

struct LoadingFlashPlayerBindingState
{
  int playerId;
  string iconPath;
  int heroId;
  int teamId;
  bool hasIcon;
  bool hasHero;

  LoadingFlashPlayerBindingState()
    : playerId(-1),
      heroId(-1),
      teamId(-1),
      hasIcon(false),
      hasHero(false)
  {
  }
};

class LoadingFlashInterface : public BaseObjectST, public NGameX::IFlashChat
{
  NI_DECLARE_REFCOUNT_CLASS_2( LoadingFlashInterface, BaseObjectST, NGameX::IFlashChat );

public:
  LoadingFlashInterface( UI::FlashContainer2 * _flashWnd, const char* _className );

  void SetHeroIdentity( int heroID, NDb::EFaction faction, const wstring& heroName, const char * iconPath, int heroLevel, bool isMale, const char * classIcon, uint partyId, string & flagIcon, wstring & flagTooltip, bool isAnimatedAvatar, int leagueIndex);
  void SetHeroLoadProgress(int heroId, float loadProgress, bool isLeftGame);
  void SetOurHeroId(int heroID, NDb::EFaction faction);
  void SetHeroLevel(int heroId, int level);
  void SetPlayersFaction(NDb::EFaction leftFaction, NDb::EFaction rightFaction);
  void SetMapBack(const char* back, const char* logo);
  void SetForceColorTable(const vector<int> & forceTable,const vector<uint> & colorTable);
  void SetTeamForce(const wstring & forceLeft,const wstring & forceRight);
  void SetHeroForce(int heroId, int force);
  void SetHeroRaiting(int heroId, int raiting, float deltaWin, float deltaLose, bool isNovice, const char* rankIcon, const wstring & rankName);
  void SetHeroRaitingAcc(int heroId, int raiting, float deltaWin, float deltaLose, bool isNovice, const char* rankIcon, const wstring & rankName);
	void SetHeroPremium(int heroId, bool hasPremium, NDb::EFaction originalFraction);
  void AddModeDescription(const char * modeImage, int id);
  void SetLoadingStatusText( const wstring & statusText);
	void SetLocales(const char* imageLeft,const wstring & toolTipLeft, const char* imageRight, const wstring & toolTipRight);
  void SetLoadingState (bool isPreloading);
  void SetTip(const wstring & tip);
  void SwitchToSpectatorMode();

  virtual void AddChannel(NDb::EChatChannel channel,  const wstring & channelName, uint channelColor, bool showChannelName, bool showPlayerName, bool canWrite2Channel);
  virtual void AddChannelShortCut(NDb::EChatChannel channel, const wstring & shortcut);
  virtual void AddMessage(NDb::EChatChannel channel, const wstring & playerName, const wstring & message);
  virtual void AddMessage(NDb::EChatChannel channel, const wstring & playerName, const wstring & message, const int playerId);
  virtual void SetDefaultChannel(NDb::EChatChannel channelID);
  virtual void SetChatVisible(bool visible);
  virtual void SetChatOff(bool isChatOff);
  virtual void SetPlayerIcon(const int playerId, const string& path);
  virtual void SetPlayerHeroId(const int playerId, const int heroId, const int teamId);
  virtual void IgnoreUser(const int playerId);
  virtual void RemoveIgnore(const int playerId);

  void OpenCloseChat();
	void OpenChanel(int channelID);
  void OnEscape();

  const wstring& GetLoadingStatusText() const { return loadingStatusText; }
  const wstring& GetTipText() const { return tipText; }
  const string& GetMapBackground() const { return mapBackground; }
  const string& GetMapLogo() const { return mapLogo; }
  const vector<int>& GetForceTable() const { return forceTable; }
  const vector<uint>& GetColorTable() const { return colorTable; }
  const wstring& GetLeftTeamForce() const { return leftTeamForce; }
  const wstring& GetRightTeamForce() const { return rightTeamForce; }
  const string& GetLeftLocaleImage() const { return leftLocaleImage; }
  const string& GetRightLocaleImage() const { return rightLocaleImage; }
  const wstring& GetLeftLocaleTooltip() const { return leftLocaleTooltip; }
  const wstring& GetRightLocaleTooltip() const { return rightLocaleTooltip; }
  bool IsPreloading() const { return preloading; }
  bool IsSpectatorMode() const { return spectatorMode; }
  bool IsChatVisible() const { return chatVisible; }
  bool IsChatOff() const { return chatOff; }
  NDb::EChatChannel GetDefaultChannel() const { return defaultChannel; }
  int GetOurHeroId() const { return ourHeroId; }
  NDb::EFaction GetOurFaction() const { return ourFaction; }
  NDb::EFaction GetLeftFaction() const { return leftFaction; }
  NDb::EFaction GetRightFaction() const { return rightFaction; }
  const vector<LoadingFlashHeroState>& GetHeroes() const { return heroes; }
  const vector<LoadingFlashModeDescriptionState>& GetModeDescriptions() const { return modeDescriptions; }
  const vector<LoadingFlashChatChannelState>& GetChatChannels() const { return chatChannels; }
  const vector<LoadingFlashChatMessageState>& GetChatMessages() const { return chatMessages; }
  const vector<LoadingFlashPlayerBindingState>& GetPlayerBindings() const { return playerBindings; }

private:
  wstring loadingStatusText;
  wstring tipText;
  string mapBackground;
  string mapLogo;
  vector<int> forceTable;
  vector<uint> colorTable;
  wstring leftTeamForce;
  wstring rightTeamForce;
  string leftLocaleImage;
  string rightLocaleImage;
  wstring leftLocaleTooltip;
  wstring rightLocaleTooltip;
  vector<LoadingFlashHeroState> heroes;
  vector<LoadingFlashModeDescriptionState> modeDescriptions;
  vector<LoadingFlashChatChannelState> chatChannels;
  vector<LoadingFlashChatMessageState> chatMessages;
  vector<LoadingFlashPlayerBindingState> playerBindings;
  bool preloading;
  bool spectatorMode;
  bool chatVisible;
  bool chatOff;
  NDb::EChatChannel defaultChannel;
  int ourHeroId;
  NDb::EFaction ourFaction;
  NDb::EFaction leftFaction;
  NDb::EFaction rightFaction;
};
#else
  class LoadingFlashInterface:public UI::FlashInterface, public NGameX::IFlashChat
{
  NI_DECLARE_REFCOUNT_CLASS_2( LoadingFlashInterface, UI::FlashInterface, NGameX::IFlashChat );

public:
  LoadingFlashInterface( UI::FlashContainer2 * _flashWnd, const char* _className );

public:

  //public function SetHeroIdentity(heroID:int, fraction:int, heroName:String, iconPath:String,heroLevel:int,isMale:Boolean):void
  void SetHeroIdentity( int heroID, NDb::EFaction faction, const wstring& heroName, const char * iconPath, int heroLevel, bool isMale, const char * classIcon, uint partyId, string & flagIcon, wstring & flagTooltip, bool isAnimatedAvatar, int leagueIndex);
  
  //public function SetHeroLoadProgress(heroId:int, loadProgress:Number, isLeftGame:Boolean):void
  void SetHeroLoadProgress(int heroId, float loadProgress, bool isLeftGame);
  void SetOurHeroId(int heroID, NDb::EFaction faction);
  void SetHeroLevel(int heroId, int level);
  void SetPlayersFaction(NDb::EFaction leftFaction, NDb::EFaction rightFaction);
  void SetMapBack(const char* back, const char* logo); 

  void SetForceColorTable(const vector<int> & forceTable,const vector<uint> & colorTable);
  void SetTeamForce(const wstring & forceLeft,const wstring & forceRight);
  void SetHeroForce(int heroId, int force);
  void SetHeroRaiting(int heroId, int raiting, float deltaWin, float deltaLose, bool isNovice, const char* rankIcon, const wstring & rankName);
  void SetHeroRaitingAcc(int heroId, int raiting, float deltaWin, float deltaLose, bool isNovice, const char* rankIcon, const wstring & rankName);
	void SetHeroPremium(int heroId, bool hasPremium, NDb::EFaction originalFraction);

  void AddModeDescription(const char * modeImage, int id) ;

  void SetLoadingStatusText( const wstring & statusText);
	void SetLocales(const char* imageLeft,const wstring & toolTipLeft, const char* imageRight, const wstring & toolTipRight);
  void SetLoadingState (bool isPreloading);

  void SetTip(const wstring & tip);

  void SwitchToSpectatorMode();

  //chat
  virtual void AddChannel(NDb::EChatChannel channel,  const wstring & channelName, uint channelColor, bool showChannelName, bool showPlayerName, bool canWrite2Channel);
  virtual void AddChannelShortCut(NDb::EChatChannel channel, const wstring & shortcut);
  virtual void AddMessage(NDb::EChatChannel channel, const wstring & playerName, const wstring & message);
  virtual void AddMessage(NDb::EChatChannel channel, const wstring & playerName, const wstring & message, const int playerId);
  virtual void SetDefaultChannel(NDb::EChatChannel channelID);
  virtual void SetChatVisible(bool visible);
  virtual void SetChatOff(bool isChatOff);
  virtual void SetPlayerIcon(const int playerId, const string& path);
  virtual void SetPlayerHeroId(const int playerId, const int heroId, const int teamId);
  virtual void IgnoreUser(const int playerId);
  virtual void RemoveIgnore(const int playerId);

  void OpenCloseChat();
	void OpenChanel(int channelID);
  void OnEscape();
};
#endif

}
