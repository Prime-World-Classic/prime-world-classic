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
  bool IsPreloading() const { return preloading; }
  bool IsSpectatorMode() const { return spectatorMode; }
  int GetOurHeroId() const { return ourHeroId; }
  NDb::EFaction GetOurFaction() const { return ourFaction; }
  NDb::EFaction GetLeftFaction() const { return leftFaction; }
  NDb::EFaction GetRightFaction() const { return rightFaction; }
  const vector<LoadingFlashHeroState>& GetHeroes() const { return heroes; }

private:
  wstring loadingStatusText;
  wstring tipText;
  string mapBackground;
  string mapLogo;
  vector<LoadingFlashHeroState> heroes;
  bool preloading;
  bool spectatorMode;
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
