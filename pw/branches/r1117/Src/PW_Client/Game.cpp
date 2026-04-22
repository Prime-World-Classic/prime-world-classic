#if defined(__linux__)

#include "System/systemStdAfx.h"
#include "System/CmdLineLite.h"
#include "System/Commands.h"
#include "System/ConfigFiles.h"
#include "System/Crc32Checksum.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/FilePath.h"
#include "System/FileSystem/FileUtils.h"
#include "System/FileSystem/WinFileSystem.h"
#include "System/ImageDDS.h"
#include "System/HPTimer.h"
#include "System/MainFrame.h"
#include "System/ProfileManager.h"
#include "System/Texts.h"
#include "NivalInput/Binds.h"
#include "NivalInput/InputEvent.h"
#include "NivalInput/HwInputInterface.h"
#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "PF_GameLogic/DBAdvMap.h"
#include "PF_GameLogic/DBGuild.h"
#include "PF_GameLogic/DBHeroRanks.h"
#include "PF_GameLogic/DBHeroesList.h"
#include "PF_GameLogic/DbMapList.h"
#include "PF_GameLogic/DBSmartChat.h"
#include "PF_GameLogic/DBSound.h"
#include "PF_GameLogic/DBServer.h"
#include "PF_GameLogic/DBSessionMessages.h"
#include "PF_GameLogic/DBSessionRoots.h"
#include "PF_GameLogic/DBStats.h"
#include "PF_GameLogic/DBTalent.h"
#include "PF_GameLogic/DBUnit.h"
#include "PF_GameLogic/DBVisualRoots.h"
#include "PF_GameLogic/GameMaps.h"
#include "PF_GameLogic/PFAdvMap.h"
#include "PF_GameLogic/WebLauncher.h"
#define PW_LINUX_DB_BOOTSTRAP 1
#include "LoadingFlashInterface.h"
#include "LoadingHeroes.h"
#include "LoadingScreenLogic.h"
#include "LoadingStatusHandler.h"
#undef PW_LINUX_DB_BOOTSTRAP
#include "UI/DBUI.h"
#include "libdb/Db.h"
#include "Version.h"
#include "Vendor/JsonCpp/include/json/json.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <png.h>

#include <ctype.h>
#include <errno.h>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace
{
namespace fs = std::filesystem;

struct LinuxClientEnvironment
{
  fs::path executablePath;
  fs::path gameRoot;
  fs::path baseDir;
  fs::path binDir;
  fs::path userDir;
  fs::path logsDir;
  bool engineReady;

  LinuxClientEnvironment() : engineReady(false)
  {
  }
};

struct LinuxClientLaunchSettings
{
  unsigned long width;
  unsigned long height;
  bool widthFromParent;
  bool heightFromParent;
  double runSeconds;
  double demoCycleSeconds;
  bool spectator;
  bool tutorial;
  std::string localeOverride;
  std::string mapSelector;
  std::string heroSelector;
  int artworkMode;
  fs::path replayFile;

  LinuxClientLaunchSettings()
    : width(1280),
      height(720),
      widthFromParent(false),
      heightFromParent(false),
      runSeconds(-1.0),
      demoCycleSeconds(0.0),
      spectator(false),
      tutorial(false),
      artworkMode(0)
  {
  }
};

struct LinuxWindowOverlay
{
  Display* display;
  ::Window window;
  GC gc;
  XFontStruct* fontStruct;
  Pixmap artworkPixmap;
  Font font;
  GLuint artworkTexture;
  GLuint fontDisplayListBase;
  unsigned long background;
  unsigned long accent;
  unsigned long foreground;
  unsigned long muted;
  unsigned long panelBackground;
  unsigned long panelBorder;
  unsigned int artworkWidth;
  unsigned int artworkHeight;
  bool openglReady;
  bool fontDisplayListsReady;
  bool ready;

  LinuxWindowOverlay()
    : display(nullptr),
      window(0),
      gc(0),
      fontStruct(nullptr),
      artworkPixmap(0),
      font(0),
      artworkTexture(0),
      fontDisplayListBase(0),
      background(0),
      accent(0),
      foreground(0),
      muted(0),
      panelBackground(0),
      panelBorder(0),
      artworkWidth(0),
      artworkHeight(0),
      openglReady(false),
      fontDisplayListsReady(false),
      ready(false)
  {
  }
};

typedef vector<NMainFrame::SWindowsMsg> TLinuxMainFrameMessages;

class LinuxHwInput : public Input::IHwInput, public CObjectBase
{
  OBJECT_BASIC_METHODS(LinuxHwInput);

public:
  LinuxHwInput()
    : focused(true),
      hasMousePosition(false),
      lastMouseX(0),
      lastMouseY(0)
  {
    RegisterDefaultControls();
  }

  virtual int FindControlZ(const char* name) const
  {
    return name ? FindControl(string(name)) : -1;
  }

  virtual int FindControl(const string& name) const
  {
    const TControlsByName::const_iterator it = controlsByName.find(name);
    return it == controlsByName.end() ? -1 : it->second;
  }

  virtual const string& ControlName(int id) const
  {
    static string emptyName;
    return id >= 0 && id < controls.size() ? controls[id].name : emptyName;
  }

  virtual bool GetReadableControlName(int id, string& name) const
  {
    if (id < 0 || id >= controls.size())
    {
      return false;
    }

    name = controls[id].name;
    return true;
  }

  virtual bool ControlIsAKey(int id) const
  {
    return id >= 0 && id < controls.size() ? controls[id].isKey : false;
  }

  virtual void Poll(vector<Input::HwEvent>& events)
  {
    events = pendingEvents;
    pendingEvents.clear();
  }

  virtual void OnApplicationFocus(bool appFocused)
  {
    focused = appFocused;
    if (!focused)
    {
      pendingEvents.clear();
      hasMousePosition = false;
    }
  }

  void SetFrameMessages(const TLinuxMainFrameMessages& messages)
  {
    pendingEvents.clear();

    if (!focused)
    {
      return;
    }

    for (int i = 0; i < messages.size(); ++i)
    {
      TranslateMessage(messages[i]);
    }
  }

  size_t ControlCount() const
  {
    return controls.size();
  }

private:
  struct SControl
  {
    string name;
    bool isKey;

    SControl(const char* controlName, bool key)
      : name(controlName),
        isKey(key)
    {
    }

    SControl()
      : isKey(true)
    {
    }
  };

  typedef map<string, int> TControlsByName;

  vector<SControl> controls;
  TControlsByName controlsByName;
  vector<Input::HwEvent> pendingEvents;
  bool focused;
  bool hasMousePosition;
  int lastMouseX;
  int lastMouseY;

  void RegisterDefaultControls()
  {
    static const char* keyboardControls[] =
    {
      "ESC", "TAB", "BACKSPACE", "ENTER", "NUM_ENTER", "SPACE",
      "LCTRL", "RCTRL", "LSHIFT", "RSHIFT", "LALT", "RALT",
      "UP", "DOWN", "LEFT", "RIGHT", "HOME", "END", "PG_UP", "PG_DOWN",
      "INSERT", "DELETE", "-", "[", "]", "`"
    };

    for (size_t i = 0; i < sizeof(keyboardControls) / sizeof(keyboardControls[0]); ++i)
    {
      RegisterControl(keyboardControls[i], true);
    }

    for (char digit = '0'; digit <= '9'; ++digit)
    {
      char name[2] = {digit, 0};
      RegisterControl(name, true);
    }

    for (char letter = 'A'; letter <= 'Z'; ++letter)
    {
      char name[2] = {letter, 0};
      RegisterControl(name, true);
    }

    for (int index = 1; index <= 12; ++index)
    {
      char name[8] = {0};
      snprintf(name, sizeof(name), "F%d", index);
      RegisterControl(name, true);
    }

    RegisterControl("MOUSE_AXIS_X", false);
    RegisterControl("MOUSE_AXIS_Y", false);
    RegisterControl("MOUSE_AXIS_Z", false);
    RegisterControl("MOUSE_BUTTON0", true);
    RegisterControl("MOUSE_BUTTON1", true);
    RegisterControl("MOUSE_BUTTON2", true);
  }

  void RegisterControl(const char* name, bool isKey)
  {
    if (!name || controlsByName.find(name) != controlsByName.end())
    {
      return;
    }

    const int id = controls.size();
    controls.push_back(SControl(name, isKey));
    controlsByName[name] = id;
  }

  void AppendKeyEvent(const char* controlName, bool activated)
  {
    const int controlId = FindControlZ(controlName);
    if (controlId >= 0)
    {
      pendingEvents.push_back(Input::HwEvent(controlId, activated));
    }
  }

  void AppendDeltaEvent(const char* controlName, float delta)
  {
    const int controlId = FindControlZ(controlName);
    if (controlId >= 0 && delta != 0.0f)
    {
      pendingEvents.push_back(Input::HwEvent(controlId, delta, false));
    }
  }

  static bool TranslateKeySymToControlName(int keySym, string* controlName)
  {
    switch (keySym)
    {
      case XK_Escape:
        *controlName = "ESC";
        return true;

      case XK_BackSpace:
        *controlName = "BACKSPACE";
        return true;

      case XK_Tab:
      case XK_ISO_Left_Tab:
        *controlName = "TAB";
        return true;

      case XK_Return:
        *controlName = "ENTER";
        return true;

      case XK_KP_Enter:
        *controlName = "NUM_ENTER";
        return true;

      case XK_space:
        *controlName = "SPACE";
        return true;

      case XK_Control_L:
        *controlName = "LCTRL";
        return true;

      case XK_Control_R:
        *controlName = "RCTRL";
        return true;

      case XK_Shift_L:
        *controlName = "LSHIFT";
        return true;

      case XK_Shift_R:
        *controlName = "RSHIFT";
        return true;

      case XK_Alt_L:
      case XK_Meta_L:
        *controlName = "LALT";
        return true;

      case XK_Alt_R:
      case XK_Meta_R:
        *controlName = "RALT";
        return true;

      case XK_Up:
        *controlName = "UP";
        return true;

      case XK_Down:
        *controlName = "DOWN";
        return true;

      case XK_Left:
        *controlName = "LEFT";
        return true;

      case XK_Right:
        *controlName = "RIGHT";
        return true;

      case XK_Home:
        *controlName = "HOME";
        return true;

      case XK_End:
        *controlName = "END";
        return true;

      case XK_Prior:
        *controlName = "PG_UP";
        return true;

      case XK_Next:
        *controlName = "PG_DOWN";
        return true;

      case XK_Insert:
        *controlName = "INSERT";
        return true;

      case XK_Delete:
        *controlName = "DELETE";
        return true;

      case XK_minus:
        *controlName = "-";
        return true;

      case XK_bracketleft:
        *controlName = "[";
        return true;

      case XK_bracketright:
        *controlName = "]";
        return true;

      case XK_grave:
        *controlName = "`";
        return true;

      default:
        break;
    }

    if (keySym >= XK_0 && keySym <= XK_9)
    {
      char name[2] = {static_cast<char>(keySym), 0};
      *controlName = name;
      return true;
    }

    if (keySym >= XK_A && keySym <= XK_Z)
    {
      char name[2] = {static_cast<char>(keySym), 0};
      *controlName = name;
      return true;
    }

    if (keySym >= XK_a && keySym <= XK_z)
    {
      char name[2] = {static_cast<char>(keySym - ('a' - 'A')), 0};
      *controlName = name;
      return true;
    }

    if (keySym >= XK_F1 && keySym <= XK_F12)
    {
      char name[8] = {0};
      snprintf(name, sizeof(name), "F%d", static_cast<int>(keySym - XK_F1) + 1);
      *controlName = name;
      return true;
    }

    return false;
  }

  void TranslateMessage(const NMainFrame::SWindowsMsg& message)
  {
    string controlName;
    switch (message.msg)
    {
      case NMainFrame::SWindowsMsg::KEY_DOWN:
      case NMainFrame::SWindowsMsg::KEY_UP:
        if (TranslateKeySymToControlName(message.nKey, &controlName))
        {
          AppendKeyEvent(controlName.c_str(), message.msg == NMainFrame::SWindowsMsg::KEY_DOWN);
        }
        break;

      case NMainFrame::SWindowsMsg::MOUSE_LB_DOWN:
        AppendKeyEvent("MOUSE_BUTTON0", true);
        break;

      case NMainFrame::SWindowsMsg::MOUSE_LB_UP:
        AppendKeyEvent("MOUSE_BUTTON0", false);
        break;

      case NMainFrame::SWindowsMsg::MOUSE_RB_DOWN:
        AppendKeyEvent("MOUSE_BUTTON1", true);
        break;

      case NMainFrame::SWindowsMsg::MOUSE_RB_UP:
        AppendKeyEvent("MOUSE_BUTTON1", false);
        break;

      case NMainFrame::SWindowsMsg::MOUSE_MB_DOWN:
        AppendKeyEvent("MOUSE_BUTTON2", true);
        break;

      case NMainFrame::SWindowsMsg::MOUSE_MB_UP:
        AppendKeyEvent("MOUSE_BUTTON2", false);
        break;

      case NMainFrame::SWindowsMsg::MOUSE_WHEEL:
        AppendDeltaEvent("MOUSE_AXIS_Z", static_cast<float>(GET_WHEEL_DELTA_WPARAM(message.dwFlags)));
        break;

      case NMainFrame::SWindowsMsg::MOUSE_MOVE:
        if (hasMousePosition)
        {
          AppendDeltaEvent("MOUSE_AXIS_X", static_cast<float>(message.x - lastMouseX));
          AppendDeltaEvent("MOUSE_AXIS_Y", static_cast<float>(message.y - lastMouseY));
        }
        lastMouseX = message.x;
        lastMouseY = message.y;
        hasMousePosition = true;
        break;

      case NMainFrame::SWindowsMsg::MOUSE_OUT:
        hasMousePosition = false;
        break;

      default:
        break;
    }
  }
};

struct LinuxInputState
{
  CObj<LinuxHwInput> hwInput;
  CObj<Input::Binds> binds;
  vector<Input::Event> frameEvents;
  TLinuxMainFrameMessages rawMessages;
  std::vector<std::string> recentEvents;
  std::vector<std::string> warnings;
  NHPTimer::STime lastUpdateTime;
  size_t totalEvents;
  size_t bindStringCount;
  size_t bindContextCount;
  size_t commandBindingHits;
  size_t hardwareControlCount;
  bool initialized;
  bool inputConfigLoaded;
  bool inputOverrideLoaded;

  LinuxInputState()
    : lastUpdateTime(0),
      totalEvents(0),
      bindStringCount(0),
      bindContextCount(0),
      commandBindingHits(0),
      hardwareControlCount(0),
      initialized(false),
      inputConfigLoaded(false),
      inputOverrideLoaded(false)
  {
  }
};

struct LinuxContentProbe
{
  bool dataMounted;
  bool localizationMounted;
  std::string locale;
  fs::path localizationRoot;
  size_t uiXdbCount;
  size_t uiScreenFileCount;
  size_t socialXdbCount;
  size_t localizationFileCount;
  std::string dataSampleFile;
  int dataSampleSize;
  std::string localizationSampleFile;
  int localizationSampleSize;
  std::vector<std::string> warnings;

  LinuxContentProbe()
    : dataMounted(false),
      localizationMounted(false),
      uiXdbCount(0),
      uiScreenFileCount(0),
      socialXdbCount(0),
      localizationFileCount(0),
      dataSampleSize(0),
      localizationSampleSize(0)
  {
  }
};

struct LinuxLoadingScreenPreview
{
  bool layoutFound;
  bool localizationLoaded;
  bool artworkLoaded;
  unsigned long width;
  unsigned long height;
  unsigned long artworkWidth;
  unsigned long artworkHeight;
  std::string flashAsset;
  std::string artworkFile;
  int flashAssetSize;
  std::vector<std::pair<std::string, std::string>> localizedProperties;
  std::vector<std::string> warnings;

  LinuxLoadingScreenPreview()
    : layoutFound(false),
      localizationLoaded(false),
      artworkLoaded(false),
      width(0),
      height(0),
      artworkWidth(0),
      artworkHeight(0),
      flashAssetSize(0)
  {
  }
};

struct LinuxLoadingStatusEntry
{
  std::string key;
  std::string text;
};

struct LinuxLoadingLocaleEntry
{
  std::string locale;
  std::string tooltip;
  std::string imageRef;
};

struct LinuxLoadingModeEntry
{
  std::string id;
  std::string tooltip;
  std::string iconRef;
};

struct LinuxLoadingUiPreview
{
  bool ready;
  bool minimapReady;
  bool smartChatReady;
  bool maneuversModeReady;
  bool guardModeReady;
  bool guildModeReady;
  bool customModeReady;
  size_t statusCount;
  size_t tipCount;
  size_t localeCount;
  size_t forceColorCount;
  size_t reportTypeCount;
  size_t countryFlagCount;
  size_t chatChannelCount;
  size_t bindCount;
  size_t minimapIconCount;
  size_t smartChatCategoryCount;
  size_t smartChatMessageCount;
  int recentPlayers;
  std::string dbid;
  std::string minimapDbid;
  std::string smartChatDbid;
  std::string sampleTip;
  std::string premiumTooltip;
  std::vector<LinuxLoadingStatusEntry> statuses;
  std::vector<std::string> tips;
  std::vector<LinuxLoadingLocaleEntry> locales;
  std::vector<LinuxLoadingModeEntry> modes;
  std::vector<std::string> statusSamples;
  std::vector<std::string> localeSamples;
  std::vector<std::string> forceColorSamples;
  std::vector<std::string> modeSamples;
  std::vector<std::string> chatChannelSamples;
  std::vector<std::string> reportTypeSamples;
  std::vector<std::string> smartChatSamples;
  std::vector<std::string> warnings;

  LinuxLoadingUiPreview()
    : ready(false),
      minimapReady(false),
      smartChatReady(false),
      maneuversModeReady(false),
      guardModeReady(false),
      guildModeReady(false),
      customModeReady(false),
      statusCount(0),
      tipCount(0),
      localeCount(0),
      forceColorCount(0),
      reportTypeCount(0),
      countryFlagCount(0),
      chatChannelCount(0),
      bindCount(0),
      minimapIconCount(0),
      smartChatCategoryCount(0),
      smartChatMessageCount(0),
      recentPlayers(0)
  {
  }
};

struct LinuxLoadingUiState
{
  size_t statusIndex;
  size_t tipIndex;
  size_t currentLocaleIndex;
  size_t enemyLocaleIndex;
  size_t modeIndex;
  size_t runtimeEventIndex;
  size_t changeCount;
  std::string source;
  std::string runtimeEvent;
  std::string runtimeStatusKey;
  std::string runtimeStatusText;

  LinuxLoadingUiState()
    : statusIndex(0),
      tipIndex(0),
      currentLocaleIndex(0),
      enemyLocaleIndex(0),
      modeIndex(0),
      runtimeEventIndex(0),
      changeCount(0),
      source("default")
  {
  }
};

enum LinuxLoadingRuntimeEventKind
{
  LINUX_LOADING_RUNTIME_LOGIN,
  LINUX_LOADING_RUNTIME_GAMESTAT,
  LINUX_LOADING_RUNTIME_LOBBY,
  LINUX_LOADING_RUNTIME_INGAME,
  LINUX_LOADING_RUNTIME_REPLAY
};

struct LinuxLoadingRuntimeEvent
{
  LinuxLoadingRuntimeEventKind kind;
  int code;
  const char* label;
};

struct LinuxLoadingRuntimeDriver
{
  bool ready;
  Strong<NGameX::LoadingStatusHandler> handler;
  Strong<Game::LoadingFlashInterface> flashInterface;
  std::vector<LinuxLoadingRuntimeEvent> events;
  std::vector<std::string> samples;
  std::vector<std::string> warnings;

  LinuxLoadingRuntimeDriver()
    : ready(false)
  {
  }
};

struct LinuxLoadingRuntimeHeroEntry
{
  int slotId;
  int team;
  bool human;
  bool leftGame;
  bool hasPremium;
  bool isNovice;
  float progress;
  int heroLevel;
  int force;
  int rating;
  int ratingAcc;
  int leagueIndex;
  unsigned int partyId;
  std::string locale;
  std::string flagId;
  std::string flagIcon;
  std::string playerName;
  std::string heroTitle;
  std::string iconPath;
  std::string classIcon;

  LinuxLoadingRuntimeHeroEntry()
    : slotId(-1),
      team(0),
      human(false),
      leftGame(false),
      hasPremium(false),
      isNovice(false),
      progress(0.0f),
      heroLevel(0),
      force(0),
      rating(0),
      ratingAcc(0),
      leagueIndex(0),
      partyId(0)
  {
  }
};

struct LinuxLoadingHeroesRuntimePreview
{
  bool ready;
  bool spectatorMode;
  int ourHeroId;
  size_t humanCount;
  size_t botCount;
  size_t disconnectedCount;
  size_t premiumCount;
  size_t noviceCount;
  size_t localeCount;
  size_t flaggedCount;
  size_t ratedCount;
  std::vector<LinuxLoadingRuntimeHeroEntry> heroes;
  std::vector<std::string> samples;
  std::vector<std::string> metaSamples;
  std::vector<std::string> warnings;

  LinuxLoadingHeroesRuntimePreview()
    : ready(false),
      spectatorMode(false),
      ourHeroId(-1),
      humanCount(0),
      botCount(0),
      disconnectedCount(0),
      premiumCount(0),
      noviceCount(0),
      localeCount(0),
      flaggedCount(0),
      ratedCount(0)
  {
  }
};

std::string ToStdString(const nstl::string& value);
NDb::Ptr<NDb::DBUIData> ResolveLoadingUiDataResource();
template <typename T>
void AppendSampleValue(std::vector<std::string>* samples, const T& value, size_t limit);

struct LinuxLoadingArtwork
{
  int width;
  int height;
  std::vector<unsigned char> rgba;
  bool ready;

  LinuxLoadingArtwork()
    : width(0),
      height(0),
      ready(false)
  {
  }
};

struct LinuxTextureAssetPreview
{
  std::string reference;
  std::string descriptorFile;
  std::string sourceFile;
  unsigned long width;
  unsigned long height;
  bool descriptorResolved;
  bool sourceResolved;
  bool artworkLoaded;
  LinuxLoadingArtwork artwork;
  std::vector<std::string> warnings;

  LinuxTextureAssetPreview()
    : width(0),
      height(0),
      descriptorResolved(false),
      sourceResolved(false),
      artworkLoaded(false)
  {
  }
};

struct LinuxHeroAbilityPreview
{
  std::string dbid;
  std::string name;
  std::string description;
  std::string type;
  bool isAttack;
  LinuxTextureAssetPreview icon;

  LinuxHeroAbilityPreview()
    : isAttack(false)
  {
  }
};

struct LinuxHeroTalentPreview
{
  size_t levelIndex;
  size_t slotIndex;
  std::string dbid;
  std::string persistentId;
  std::string name;
  std::string description;
  std::string rarity;
  std::string status;
  bool locked;
  LinuxTextureAssetPreview icon;

  LinuxHeroTalentPreview()
    : levelIndex(0),
      slotIndex(0),
      locked(false)
  {
  }
};

struct LinuxSelectedHeroDbPreview
{
  bool ready;
  bool found;
  bool attackReady;
  bool statsReady;
  bool targetingReady;
  bool uniqueResourceReady;
  size_t abilityCount;
  size_t activeAbilityCount;
  size_t passiveAbilityCount;
  size_t autocastAbilityCount;
  size_t channellingAbilityCount;
  size_t defaultTalentSetCount;
  size_t defaultTalentLevelCount;
  size_t defaultTalentSlotCount;
  size_t defaultTalentReadyCount;
  size_t defaultTalentIconCount;
  size_t statsCount;
  size_t levelUpgradeCount;
  size_t recommendedStatCount;
  size_t sceneObjectCount;
  size_t summonedUnitGroupCount;
  size_t skinCount;
  float targetingRange;
  float chaseRange;
  float aggroRange;
  std::string dbid;
  std::string persistentId;
  std::string title;
  std::string description;
  std::string heroRace;
  std::string attackAbilityDbid;
  std::string attackAbilityName;
  std::string uniqueResourceName;
  std::string uniqueResourceTooltip;
  LinuxTextureAssetPreview portrait;
  std::vector<std::string> statSamples;
  std::vector<std::string> recommendedStatSamples;
  std::vector<std::string> talentSamples;
  std::vector<LinuxHeroAbilityPreview> featuredAbilities;
  std::vector<LinuxHeroTalentPreview> defaultTalentPreviews;
  std::vector<std::string> warnings;

  LinuxSelectedHeroDbPreview()
    : ready(false),
      found(false),
      attackReady(false),
      statsReady(false),
      targetingReady(false),
      uniqueResourceReady(false),
      abilityCount(0),
      activeAbilityCount(0),
      passiveAbilityCount(0),
      autocastAbilityCount(0),
      channellingAbilityCount(0),
      defaultTalentSetCount(0),
      defaultTalentLevelCount(0),
      defaultTalentSlotCount(0),
      defaultTalentReadyCount(0),
      defaultTalentIconCount(0),
      statsCount(0),
      levelUpgradeCount(0),
      recommendedStatCount(0),
      sceneObjectCount(0),
      summonedUnitGroupCount(0),
      skinCount(0),
      targetingRange(0.0f),
      chaseRange(0.0f),
      aggroRange(0.0f)
  {
  }
};

struct LinuxMapCatalogEntry
{
  std::string descriptor;
  std::string mapType;
  std::string category;
  std::string title;
  std::string description;
  std::string mapRef;
  std::string mapSettingsRef;
  std::string scoringTableRef;
  std::string imageRef;
  std::string loadingBackRef;
  std::string loadingLogoRef;
  std::string firstWinVisualInfoRef;
  int teamSize;
  bool productionMode;

  LinuxMapCatalogEntry()
    : teamSize(0),
      productionMode(false)
  {
  }
};

struct LinuxMapCatalog
{
  size_t descriptorCount;
  size_t productionDescriptorCount;
  size_t pvpCount;
  size_t pveCount;
  size_t tutorialCount;
  std::vector<LinuxMapCatalogEntry> entries;
  std::vector<LinuxMapCatalogEntry> featuredEntries;
  std::vector<std::string> warnings;

  LinuxMapCatalog()
    : descriptorCount(0),
      productionDescriptorCount(0),
      pvpCount(0),
      pveCount(0),
      tutorialCount(0)
  {
  }
};

struct LinuxMapBrowserState
{
  size_t selectedIndex;
  size_t selectionChanges;
  std::string selectionSource;

  LinuxMapBrowserState()
    : selectedIndex(0),
      selectionChanges(0),
      selectionSource("default")
  {
  }
};

struct LinuxMapSettingsPreview
{
  bool resolved;
  std::string source;
  std::string reference;
  std::string parentRef;
  std::vector<std::string> chainReferences;
  std::vector<std::string> chainFiles;
  size_t dictionaryResourceCount;
  std::vector<std::string> dictionaryKeysPreview;
  std::string scriptFile;
  std::string dictionaryRef;
  std::string dialogsCollectionRef;
  std::string hintsCollectionRef;
  std::string questsCollectionRef;
  std::string overrideBotsSettingsRef;
  std::string overrideGlyphSettingsRef;
  std::string heroRespawnParamsRef;
  int battleStartDelay;
  int emblemHeroNeeds;
  int force;
  int minRequiredHeroForce;
  int maxRequiredHeroForce;
  int startPrimePerTeam;
  int towersVulnerabilityDelay;
  bool enableAllScriptFunctions;
  bool enableAnnouncements;
  bool enablePortalTalent;
  bool enableStatistics;
  bool showAllHeroes;
  bool fullPartyOnly;
  std::vector<std::string> warnings;

  LinuxMapSettingsPreview()
    : resolved(false),
      dictionaryResourceCount(0),
      battleStartDelay(-1),
      emblemHeroNeeds(-1),
      force(-1),
      minRequiredHeroForce(-1),
      maxRequiredHeroForce(-1),
      startPrimePerTeam(-1),
      towersVulnerabilityDelay(-1),
      enableAllScriptFunctions(false),
      enableAnnouncements(false),
      enablePortalTalent(false),
      enableStatistics(false),
      showAllHeroes(false),
      fullPartyOnly(false)
  {
  }
};

struct LinuxTacticalMapMarker
{
  std::string kind;
  std::string objectRef;
  std::string label;
  std::string scriptName;
  float translateX;
  float translateY;
  int team;

  LinuxTacticalMapMarker()
    : translateX(0.0f),
      translateY(0.0f),
      team(0)
  {
  }
};

struct LinuxTacticalMapPreview
{
  bool ready;
  float minX;
  float maxX;
  float minY;
  float maxY;
  size_t towerCount;
  size_t heroSpawnCount;
  size_t laneSpawnerCount;
  size_t neutralSpawnerCount;
  size_t bossCount;
  size_t shopCount;
  size_t fountainCount;
  size_t glyphCount;
  size_t mainBuildingCount;
  size_t minigameCount;
  size_t flagCount;
  std::vector<LinuxTacticalMapMarker> markers;
  std::vector<std::string> warnings;

  LinuxTacticalMapPreview()
    : ready(false),
      minX(0.0f),
      maxX(0.0f),
      minY(0.0f),
      maxY(0.0f),
      towerCount(0),
      heroSpawnCount(0),
      laneSpawnerCount(0),
      neutralSpawnerCount(0),
      bossCount(0),
      shopCount(0),
      fountainCount(0),
      glyphCount(0),
      mainBuildingCount(0),
      minigameCount(0),
      flagCount(0)
  {
  }
};

struct LinuxSelectedMapPreview
{
  size_t selectedIndex;
  std::string descriptor;
  bool ready;
  bool mapResolved;
  bool mapSettingsResolved;
  bool scoringTableResolved;
  std::string mapFile;
  std::string mapSettingsFile;
  std::string scoringTableFile;
  std::string terrainRef;
  std::string cameraSettingsRef;
  std::string lightEnvironmentRef;
  std::string nightLightEnvironmentRef;
  std::string minimapFirstRef;
  std::string minimapSecondRef;
  std::string minimapNeutralRef;
  std::string scriptFile;
  std::string dictionaryRef;
  std::string dialogsCollectionRef;
  std::string hintsCollectionRef;
  std::string questsCollectionRef;
  size_t objectCount;
  size_t lockMapObjectCount;
  size_t scriptedObjectCount;
  LinuxTextureAssetPreview loadingBack;
  LinuxTextureAssetPreview loadingLogo;
  LinuxTextureAssetPreview minimapFirst;
  LinuxTextureAssetPreview minimapSecond;
  LinuxTextureAssetPreview minimapNeutral;
  LinuxMapSettingsPreview settings;
  LinuxTacticalMapPreview tactical;
  std::vector<std::string> warnings;

  LinuxSelectedMapPreview()
    : selectedIndex(static_cast<size_t>(-1)),
      ready(false),
      mapResolved(false),
      mapSettingsResolved(false),
      scoringTableResolved(false),
      objectCount(0),
      lockMapObjectCount(0),
      scriptedObjectCount(0)
  {
  }
};

enum ELinuxArtworkMode
{
  LINUX_ARTWORK_AUTO = 0,
  LINUX_ARTWORK_LOADING,
  LINUX_ARTWORK_MAP_BACK,
  LINUX_ARTWORK_MAP_BACK_WITH_LOGO,
  LINUX_ARTWORK_MAP_LOGO,
  LINUX_ARTWORK_MINIMAP_FIRST,
  LINUX_ARTWORK_MINIMAP_SECOND,
  LINUX_ARTWORK_MINIMAP_NEUTRAL,
  LINUX_ARTWORK_COUNT
};

int ParseArtworkMode(const std::string& value)
{
  size_t begin = 0;
  while (begin < value.size() && isspace(static_cast<unsigned char>(value[begin])))
  {
    ++begin;
  }

  size_t end = value.size();
  while (end > begin && isspace(static_cast<unsigned char>(value[end - 1])))
  {
    --end;
  }

  std::string normalized = value.substr(begin, end - begin);
  for (size_t i = 0; i < normalized.size(); ++i)
  {
    normalized[i] = static_cast<char>(tolower(static_cast<unsigned char>(normalized[i])));
  }

  if (normalized.empty() || normalized == "auto")
  {
    return LINUX_ARTWORK_AUTO;
  }
  if (normalized == "loading")
  {
    return LINUX_ARTWORK_LOADING;
  }
  if (normalized == "back" || normalized == "map-back")
  {
    return LINUX_ARTWORK_MAP_BACK;
  }
  if (normalized == "back+logo" || normalized == "map-back+logo" || normalized == "back-logo")
  {
    return LINUX_ARTWORK_MAP_BACK_WITH_LOGO;
  }
  if (normalized == "logo" || normalized == "map-logo")
  {
    return LINUX_ARTWORK_MAP_LOGO;
  }
  if (normalized == "minimap1" || normalized == "minimap-first")
  {
    return LINUX_ARTWORK_MINIMAP_FIRST;
  }
  if (normalized == "minimap2" || normalized == "minimap-second")
  {
    return LINUX_ARTWORK_MINIMAP_SECOND;
  }
  if (normalized == "minimapn" || normalized == "minimap-neutral")
  {
    return LINUX_ARTWORK_MINIMAP_NEUTRAL;
  }
  return LINUX_ARTWORK_AUTO;
}

struct LinuxArtworkSelectionState
{
  int mode;
  size_t changeCount;
  std::string source;

  LinuxArtworkSelectionState()
    : mode(LINUX_ARTWORK_AUTO),
      changeCount(0),
      source("default")
  {
  }
};

struct LinuxHeroCatalogEntry
{
  std::string id;
  std::string persistentId;
  std::string title;
  std::string alternateTitle;
  std::string description;
  std::string alternateDescription;
  std::string gender;
  std::string iconRef;
  size_t skinCount;
  std::vector<std::string> featuredSkinNames;
  bool legal;

  LinuxHeroCatalogEntry()
    : skinCount(0),
      legal(false)
  {
  }
};

struct LinuxHeroCatalog
{
  std::vector<LinuxHeroCatalogEntry> entries;
  std::vector<std::string> warnings;
};

struct LinuxLocalMatchSlot
{
  size_t heroIndex;
  std::string heroId;
  std::string heroTitle;
  bool manualHero;
  bool human;
  int team;

  LinuxLocalMatchSlot()
    : heroIndex(static_cast<size_t>(-1)),
      manualHero(false),
      human(false),
      team(0)
  {
  }
};

struct LinuxLocalMatchPreview
{
  size_t selectedHeroIndex;
  size_t selectedSlotIndex;
  size_t generationCount;
  size_t shuffleOffset;
  size_t requestedTeamSize;
  size_t teamSize;
  int humanTeam;
  std::string generationSource;
  bool ready;
  std::vector<size_t> slotHeroOverrides;
  std::vector<LinuxLocalMatchSlot> lineup;
  std::vector<std::string> warnings;

  LinuxLocalMatchPreview()
    : selectedHeroIndex(0),
      selectedSlotIndex(0),
      generationCount(0),
      shuffleOffset(0),
      requestedTeamSize(0),
      teamSize(0),
      humanTeam(2),
      generationSource("default"),
      ready(false)
  {
  }
};

struct LinuxEngineMapStartSlot
{
  size_t spawnIndex;
  size_t lineupIndex;
  int playerId;
  int team;
  int originalTeam;
  int userId;
  bool filled;
  bool human;
  bool manualHero;
  bool hasPremium;
  bool isNovice;
  bool isAnimatedAvatar;
  unsigned int heroChecksum;
  unsigned int partyId;
  int heroLevel;
  int heroExp;
  int heroRating;
  int ownLeaguePlace;
  int leagueIndex;
  std::string heroId;
  std::string heroTitle;
  std::string heroSkin;
  std::string locale;
  std::string flagId;
  std::string nickname;
  std::string scriptName;
  std::vector<int> leaguePlaces;
  float translateX;
  float translateY;

  LinuxEngineMapStartSlot()
    : spawnIndex(0),
      lineupIndex(static_cast<size_t>(-1)),
      playerId(-1),
      team(0),
      originalTeam(0),
      userId(-1),
      filled(false),
      human(false),
      manualHero(false),
      hasPremium(false),
      isNovice(false),
      isAnimatedAvatar(true),
      heroChecksum(0),
      partyId(0),
      heroLevel(0),
      heroExp(0),
      heroRating(0),
      ownLeaguePlace(0),
      leagueIndex(0),
      translateX(0.0f),
      translateY(0.0f)
  {
  }
};

struct LinuxEngineMapStartPreview
{
  bool ready;
  bool usedRealMapLoader;
  bool builtMapStartInfo;
  size_t totalSpawners;
  size_t team1Spawners;
  size_t team2Spawners;
  size_t assignedSlots;
  size_t overflowPlayers;
  size_t humanPlayers;
  size_t botPlayers;
  int maxPlayersPerTeam;
  int randomSeed;
  std::string mapDescriptor;
  std::string source;
  std::vector<LinuxEngineMapStartSlot> slots;
  std::vector<std::string> warnings;

  LinuxEngineMapStartPreview()
    : ready(false),
      usedRealMapLoader(false),
      builtMapStartInfo(false),
      totalSpawners(0),
      team1Spawners(0),
      team2Spawners(0),
      assignedSlots(0),
      overflowPlayers(0),
      humanPlayers(0),
      botPlayers(0),
      maxPlayersPerTeam(0),
      randomSeed(0)
  {
  }
};

struct LinuxRootFileSystemPreview
{
  bool mounted;
  bool dataRegistered;
  bool localizationRegistered;
  bool dbCacheReady;
  std::string sampleFile;
  int sampleFileSize;
  std::string localizationFile;
  int localizationFileSize;
  std::string textRefFile;
  std::string textRefValue;
  std::vector<std::string> warnings;

  LinuxRootFileSystemPreview()
    : mounted(false),
      dataRegistered(false),
      localizationRegistered(false),
      dbCacheReady(false),
      sampleFileSize(0),
      localizationFileSize(0)
  {
  }
};

struct LinuxSessionRootPreview
{
  bool ready;
  bool uiRootReady;
  bool uiUnitCategoriesReady;
  bool uiUnitCategoriesParamsReady;
  bool logicAiReady;
  bool logicBotsSettingsReady;
  bool logicPortalReady;
  bool logicScoringReady;
  bool logicGlyphsReady;
  bool logicLevelUpsReady;
  bool logicDefaultFormulasReady;
  bool logicUnitsReady;
  bool logicGuildBuffsReady;
  bool logicHeroRanksReady;
  bool logicLevelToExperienceReady;
  bool logicRootReady;
  bool heroesDbReady;
  bool mapListReady;
  bool visualRootReady;
  bool visualEffectsReady;
  bool visualUiEventsReady;
  bool visualTeamColoringReady;
  bool visualEmoteSettingsReady;
  bool audioRootReady;
  bool rollSettingsReady;
  bool rollPvpReady;
  bool rollGuildLevelsReady;
  bool sessionMessagesReady;
  bool botsAiEnabled;
  bool botsMidOnly;
  bool logicKillExperienceModifiersReady;
  size_t uiUnitCategoryCount;
  size_t uiUnitCategoryParamCount;
  size_t logicTeamNameCount;
  size_t logicConsumableGroupCount;
  size_t logicGlyphCount;
  size_t logicLevelUpCount;
  size_t logicLevelUpPointTotal;
  size_t logicScoringAchievementCount;
  size_t logicScoringHeroTitleCount;
  size_t logicScoringDescriptionCount;
  size_t logicScoringTeleporterCount;
  size_t logicFloatFormulaCount;
  size_t logicBoolFormulaCount;
  size_t logicIntFormulaCount;
  size_t logicUnitParameterCount;
  size_t logicUnitDefaultStatsCount;
  size_t logicUnitTargetingCount;
  size_t logicGuildBuffCount;
  size_t logicGuildShopBonusCount;
  size_t logicHeroRankCount;
  size_t logicLevelCount;
  size_t logicHeroCount;
  size_t logicLegalHeroCount;
  size_t logicMapCount;
  size_t visualCameraCount;
  size_t visualAnimSetCount;
  size_t visualWinLoseCount;
  size_t visualSelfAuraCount;
  size_t visualAuraCount;
  size_t visualUiEventCount;
  size_t rollPvpContainerCount;
  size_t rollPvpPremiumContainerCount;
  size_t rollGuildLevelCount;
  size_t rollRatingModifierCount;
  size_t rollFullPartyModifierCount;
  int logicCreepsWavesDelay;
  int logicCreepLevelCap;
  int logicBaseEmblemHeroNeeds;
  int logicLevelFirstExp;
  int logicLevelLastExp;
  int logicHeroRanksHighLevelsMMRating;
  int logicBotsTimeToGo;
  int logicBotsTimeToTeleport;
  int rollPvpScoreCap;
  int rollPvpContainersOnWin;
  int rollRequiredLevelForExclusiveTalents;
  int rollRequiredRatingForExclusiveTalents;
  float visualWallTargetZoneWidth;
  std::string rootDbid;
  std::string logicRootDbid;
  std::string uiRootDbid;
  std::string visualRootDbid;
  std::string audioRootDbid;
  std::string rollSettingsDbid;
  std::string logicPortalDbid;
  std::string logicScoringDbid;
  std::string logicGlyphsDbid;
  std::string logicLevelUpsDbid;
  std::string logicDefaultFormulasDbid;
  std::string logicUnitsDbid;
  std::string logicGuildBuffsDbid;
  std::string visualTeamColoringDbid;
  std::string visualEmoteSettingsDbid;
  std::string rollPvpModeName;
  std::string sessionMessagesDbid;
  std::string dxErrorTitle;
  std::string hardwareErrorMessage;
  std::vector<std::string> uiUnitCategorySamples;
  std::vector<std::string> logicTeamNameSamples;
  std::vector<std::string> logicGlyphSamples;
  std::vector<std::string> logicScoreSamples;
  std::vector<std::string> logicGuildBuffSamples;
  std::vector<std::string> logicRankSamples;
  std::vector<std::string> heroSamples;
  std::vector<std::string> mapSamples;
  std::vector<std::string> visualCameraSamples;
  std::vector<std::string> visualUiEventSamples;
  std::vector<std::string> rollGuildLevelSamples;
  std::vector<std::string> warnings;

  LinuxSessionRootPreview()
    : ready(false),
      uiRootReady(false),
      uiUnitCategoriesReady(false),
      uiUnitCategoriesParamsReady(false),
      logicAiReady(false),
      logicBotsSettingsReady(false),
      logicPortalReady(false),
      logicScoringReady(false),
      logicGlyphsReady(false),
      logicLevelUpsReady(false),
      logicDefaultFormulasReady(false),
      logicUnitsReady(false),
      logicGuildBuffsReady(false),
      logicHeroRanksReady(false),
      logicLevelToExperienceReady(false),
      logicRootReady(false),
      heroesDbReady(false),
      mapListReady(false),
      visualRootReady(false),
      visualEffectsReady(false),
      visualUiEventsReady(false),
      visualTeamColoringReady(false),
      visualEmoteSettingsReady(false),
      audioRootReady(false),
      rollSettingsReady(false),
      rollPvpReady(false),
      rollGuildLevelsReady(false),
      sessionMessagesReady(false),
      botsAiEnabled(false),
      botsMidOnly(false),
      logicKillExperienceModifiersReady(false),
      uiUnitCategoryCount(0),
      uiUnitCategoryParamCount(0),
      logicTeamNameCount(0),
      logicConsumableGroupCount(0),
      logicGlyphCount(0),
      logicLevelUpCount(0),
      logicLevelUpPointTotal(0),
      logicScoringAchievementCount(0),
      logicScoringHeroTitleCount(0),
      logicScoringDescriptionCount(0),
      logicScoringTeleporterCount(0),
      logicFloatFormulaCount(0),
      logicBoolFormulaCount(0),
      logicIntFormulaCount(0),
      logicUnitParameterCount(0),
      logicUnitDefaultStatsCount(0),
      logicUnitTargetingCount(0),
      logicGuildBuffCount(0),
      logicGuildShopBonusCount(0),
      logicHeroRankCount(0),
      logicLevelCount(0),
      logicHeroCount(0),
      logicLegalHeroCount(0),
      logicMapCount(0),
      visualCameraCount(0),
      visualAnimSetCount(0),
      visualWinLoseCount(0),
      visualSelfAuraCount(0),
      visualAuraCount(0),
      visualUiEventCount(0),
      rollPvpContainerCount(0),
      rollPvpPremiumContainerCount(0),
      rollGuildLevelCount(0),
      rollRatingModifierCount(0),
      rollFullPartyModifierCount(0),
      logicCreepsWavesDelay(0),
      logicCreepLevelCap(0),
      logicBaseEmblemHeroNeeds(0),
      logicLevelFirstExp(0),
      logicLevelLastExp(0),
      logicHeroRanksHighLevelsMMRating(0),
      logicBotsTimeToGo(0),
      logicBotsTimeToTeleport(0),
      rollPvpScoreCap(0),
      rollPvpContainersOnWin(0),
      rollRequiredLevelForExclusiveTalents(0),
      rollRequiredRatingForExclusiveTalents(0),
      visualWallTargetZoneWidth(0.0f)
  {
  }
};

struct LinuxResourceCatalogPreview
{
  bool ready;
  size_t talentCount;
  size_t consumableCount;
  size_t marketingItemCount;
  std::vector<std::string> talentSamples;
  std::vector<std::string> consumableSamples;
  std::vector<std::string> marketingSamples;
  std::vector<std::string> warnings;

  LinuxResourceCatalogPreview()
    : ready(false),
      talentCount(0),
      consumableCount(0),
      marketingItemCount(0)
  {
  }
};

struct LinuxUiRootPreview
{
  bool ready;
  bool preferencesReady;
  bool votingReady;
  size_t screenCount;
  size_t cursorCount;
  size_t scriptCount;
  size_t contentGroupCount;
  size_t contentEntryCount;
  size_t constantCount;
  size_t substituteCount;
  size_t styleAliasCount;
  size_t fontStyleCount;
  std::string dbid;
  std::vector<std::string> screenSamples;
  std::vector<std::string> contentSamples;
  std::vector<std::string> constantSamples;
  std::vector<std::string> warnings;

  LinuxUiRootPreview()
    : ready(false),
      preferencesReady(false),
      votingReady(false),
      screenCount(0),
      cursorCount(0),
      scriptCount(0),
      contentGroupCount(0),
      contentEntryCount(0),
      constantCount(0),
      substituteCount(0),
      styleAliasCount(0),
      fontStyleCount(0)
  {
  }
};

struct LinuxSoundRootPreview
{
  bool ready;
  bool timerSoundsReady;
  bool heartbeatReady;
  bool ambientReady;
  bool preferencesVolumeReady;
  bool lastHitReady;
  size_t sceneCount;
  size_t sceneGroupCount;
  size_t ambienceGroupCount;
  std::string dbid;
  std::string heartbeatEvent;
  std::string ambientEvent;
  std::vector<std::string> cueSamples;
  std::vector<std::string> categorySamples;
  std::vector<std::string> ambienceSamples;
  std::vector<std::string> warnings;

  LinuxSoundRootPreview()
    : ready(false),
      timerSoundsReady(false),
      heartbeatReady(false),
      ambientReady(false),
      preferencesVolumeReady(false),
      lastHitReady(false),
      sceneCount(0),
      sceneGroupCount(0),
      ambienceGroupCount(0)
  {
  }
};

struct LinuxConfigBootstrapPreview
{
  bool commandsRegistered;
  bool defaultLoaded;
  bool userLoaded;
  bool langLoaded;
  bool socialLoaded;
  bool gameLoaded;
  bool spectatorLoaded;
  size_t placeholderCommandCount;
  std::string language;
  std::string gfxFullscreen;
  std::string gfxResolution;
  std::string localGame;
  std::string loginAddress;
  std::string socialLoginAddress;
  std::string statClientUrl;
  std::vector<std::string> warnings;

  LinuxConfigBootstrapPreview()
    : commandsRegistered(false),
      defaultLoaded(false),
      userLoaded(false),
      langLoaded(false),
      socialLoaded(false),
      gameLoaded(false),
      spectatorLoaded(false),
      placeholderCommandCount(0)
  {
  }
};

struct LinuxLaunchPreview
{
  bool launcherRequested;
  bool launcherFetchAttempted;
  bool launcherFetchSucceeded;
  bool parentWindowLaunch;
  bool protocolPresent;
  bool protocolValid;
  bool versionMatches;
  bool mapIdProvided;
  bool sessionLoginProvided;
  int mirrorIndex;
  std::string source;
  std::string protocolLine;
  std::string method;
  std::string token;
  std::string version;
  std::string mapId;
  std::string sessionLogin;
  std::string serverName;
  std::string uid;
  std::string snid;
  std::string snuid;
  std::string launcherResponse;
  std::vector<std::string> warnings;

  LinuxLaunchPreview()
    : launcherRequested(false),
      launcherFetchAttempted(false),
      launcherFetchSucceeded(false),
      parentWindowLaunch(false),
      protocolPresent(false),
      protocolValid(false),
      versionMatches(false),
      mapIdProvided(false),
      sessionLoginProvided(false),
      mirrorIndex(-1)
  {
  }
};

struct LinuxSessionPlayerPreview
{
  std::string nickname;
  int userId;
  int heroWebId;
  int teamId;
  int partyId;
  int skinId;
  std::string heroPersistentId;
  bool currentPlayer;

  LinuxSessionPlayerPreview()
    : userId(0),
      heroWebId(0),
      teamId(0),
      partyId(0),
      skinId(0),
      currentPlayer(false)
  {
  }
};

struct LinuxSessionPreview
{
  bool fileProvided;
  bool loaded;
  bool valid;
  bool mapIdProvided;
  bool playerInfoLoaded;
  bool usersDataLoaded;
  std::string source;
  std::string filePath;
  std::string method;
  std::string mapId;
  std::string currentNickname;
  std::string currentHeroPersistentId;
  int currentUserId;
  int currentHeroWebId;
  int currentTeamId;
  int currentPartyId;
  int currentSkinId;
  std::vector<LinuxSessionPlayerPreview> players;
  std::vector<std::string> warnings;

  LinuxSessionPreview()
    : fileProvided(false),
      loaded(false),
      valid(false),
      mapIdProvided(false),
      playerInfoLoaded(false),
      usersDataLoaded(false),
      currentUserId(0),
      currentHeroWebId(0),
      currentTeamId(0),
      currentPartyId(0),
      currentSkinId(0)
  {
  }
};

bool ReadTextFile(const fs::path& path, std::string* content);
std::string ReadNamedKey(const char* key, const char* alternateKey = 0);
std::string TrimAscii(const std::string& text);
std::string ToAsciiLower(std::string value);

const char* ResolveSessionHeroPersistentId(int heroWebId)
{
  static const char* const heroes[] = {
    "prince",
    "snowqueen",
    "faceless",
    "warlord",
    "thundergod",
    "invisible",
    "mowgly",
    "inventor",
    "artist",
    "highlander",
    "marine",
    "firefox",
    "healer",
    "night",
    "rockman",
    "assassin",
    "unicorn",
    "hunter",
    "ghostlord",
    "ratcatcher",
    "archeress",
    "werewolf",
    "frogenglut",
    "witchdoctor",
    "manawyrm",
    "bard",
    "naga",
    "mage",
    "fairy",
    "witcher",
    "alchemist",
    "demonolog",
    "vampire",
    "witch",
    "crusader_A",
    "crusader_B",
    "monster",
    "angel",
    "freeze",
    "gunslinger",
    "reaper",
    "fluffy",
    "rifleman",
    "magicgirl",
    "pinkgirl",
    "ironknight",
    "fallenangel",
    "bladedancer",
    "ent",
    "plaguedoctor",
    "katana",
    "plane",
    "zealot",
    "wraithking",
    "dryad",
    "stalker",
    "gunner",
    "chronicle",
    "brewer",
    "shadow",
    "wendigo",
    "trickster",
    "banshee",
    "shaman",
    "bomber"
  };

  if (heroWebId < 1 || heroWebId > static_cast<int>(sizeof(heroes) / sizeof(heroes[0])))
  {
    return 0;
  }

  return heroes[heroWebId - 1];
}

bool ParseJsonDocument(const std::string& text, Json::Value* root, std::string* error)
{
  if (!root)
  {
    if (error)
    {
      *error = "json root is missing";
    }
    return false;
  }

  Json::Reader reader;
  if (reader.parse(text, *root, false))
  {
    return true;
  }

  if (error)
  {
    *error = TrimAscii(reader.getFormattedErrorMessages());
  }
  return false;
}

std::string ReadJsonStringValue(const Json::Value& value, const char* key)
{
  if (!key || !key[0] || !value.isObject())
  {
    return "";
  }

  const Json::Value field = value.get(key, Json::Value());
  return field.isString() ? TrimAscii(field.asString()) : "";
}

int ReadJsonIntValue(const Json::Value& value, const char* key, int fallback = 0)
{
  if (!key || !key[0] || !value.isObject())
  {
    return fallback;
  }

  const Json::Value field = value.get(key, Json::Value());
  if (field.isInt() || field.isUInt())
  {
    return field.asInt();
  }
  if (field.isDouble())
  {
    return static_cast<int>(field.asDouble());
  }
  return fallback;
}

bool ParseSessionPlayerPreview(
  const Json::Value& value,
  LinuxSessionPlayerPreview* player,
  std::vector<std::string>* warnings
)
{
  if (!player || !value.isObject())
  {
    return false;
  }

  player->nickname = ReadJsonStringValue(value, "nickname");
  player->userId = ReadJsonIntValue(value, "id");
  player->heroWebId = ReadJsonIntValue(value, "hero");
  player->teamId = ReadJsonIntValue(value, "team");
  player->partyId = ReadJsonIntValue(value, "party");
  player->skinId = ReadJsonIntValue(value, "skin");

  if (const char* persistentId = ResolveSessionHeroPersistentId(player->heroWebId))
  {
    player->heroPersistentId = persistentId;
  }
  else if (player->heroWebId > 0 && warnings)
  {
    warnings->push_back(std::string("Unknown session hero web id: ") + NStr::StrFmt("%d", player->heroWebId));
  }

  return player->userId > 0 || !player->nickname.empty() || player->teamId > 0;
}

size_t CountSessionTeamPlayers(const LinuxSessionPreview& preview, int teamId)
{
  size_t count = 0;
  for (size_t i = 0; i < preview.players.size(); ++i)
  {
    if (preview.players[i].teamId == teamId)
    {
      ++count;
    }
  }
  return count;
}

void ProbeSessionPreview(LinuxClientLaunchSettings* settings, LinuxSessionPreview* preview)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxSessionPreview();
  preview->source = "none";

  std::string sessionPath = ReadNamedKey("--session-json", "sessionJson");
  if (sessionPath.empty())
  {
    sessionPath = ReadNamedKey("session_json");
  }

  preview->fileProvided = !sessionPath.empty();
  if (!preview->fileProvided)
  {
    return;
  }

  preview->source = "session-json";
  fs::path resolvedPath = fs::path(sessionPath);
  if (resolvedPath.is_relative())
  {
    resolvedPath = fs::current_path() / resolvedPath;
  }
  preview->filePath = resolvedPath.string();

  std::string sessionText;
  if (!ReadTextFile(resolvedPath, &sessionText))
  {
    preview->warnings.push_back("Session JSON file is not readable: " + preview->filePath);
    return;
  }

  preview->loaded = true;

  Json::Value root;
  std::string parseError;
  if (!ParseJsonDocument(sessionText, &root, &parseError) || !root.isObject())
  {
    preview->warnings.push_back("Session JSON parse failed: " + parseError);
    return;
  }

  preview->method = ReadJsonStringValue(root, "method");
  preview->mapId = ReadJsonStringValue(root, "mapId");
  preview->mapIdProvided = !preview->mapId.empty();
  if (settings && settings->mapSelector.empty() && preview->mapIdProvided)
  {
    settings->mapSelector = preview->mapId;
  }

  const Json::Value playerInfo = root.get("playerInfo", Json::Value());
  if (playerInfo.isObject())
  {
    preview->playerInfoLoaded = true;
    preview->currentNickname = ReadJsonStringValue(playerInfo, "nickname");
    preview->currentUserId = ReadJsonIntValue(playerInfo, "id");
    preview->currentHeroWebId = ReadJsonIntValue(playerInfo, "hero");
    preview->currentTeamId = ReadJsonIntValue(playerInfo, "team");
    preview->currentPartyId = ReadJsonIntValue(playerInfo, "party");
    preview->currentSkinId = ReadJsonIntValue(playerInfo, "skin");
    if (const char* persistentId = ResolveSessionHeroPersistentId(preview->currentHeroWebId))
    {
      preview->currentHeroPersistentId = persistentId;
    }
    else if (preview->currentHeroWebId > 0)
    {
      preview->warnings.push_back(
        std::string("Current session hero web id is unknown: ") + NStr::StrFmt("%d", preview->currentHeroWebId)
      );
    }
  }
  else
  {
    preview->warnings.push_back("Session JSON missing playerInfo object");
  }

  const Json::Value usersData = root.get("usersData", Json::Value());
  if (usersData.isArray())
  {
    preview->usersDataLoaded = true;
    for (Json::ArrayIndex i = 0; i < usersData.size(); ++i)
    {
      LinuxSessionPlayerPreview player;
      if (!ParseSessionPlayerPreview(usersData[i], &player, &preview->warnings))
      {
        continue;
      }

      const bool idMatches = preview->currentUserId > 0 && player.userId == preview->currentUserId;
      const bool nicknameMatches =
        !preview->currentNickname.empty() &&
        ToAsciiLower(player.nickname) == ToAsciiLower(preview->currentNickname);
      player.currentPlayer = idMatches || nicknameMatches;
      preview->players.push_back(player);
    }
  }
  else
  {
    preview->warnings.push_back("Session JSON missing usersData array");
  }

  bool currentPlayerFound = false;
  for (size_t i = 0; i < preview->players.size(); ++i)
  {
    if (preview->players[i].currentPlayer)
    {
      currentPlayerFound = true;
      if (preview->currentTeamId <= 0)
      {
        preview->currentTeamId = preview->players[i].teamId;
      }
      if (preview->currentHeroWebId <= 0)
      {
        preview->currentHeroWebId = preview->players[i].heroWebId;
      }
      if (preview->currentHeroPersistentId.empty())
      {
        preview->currentHeroPersistentId = preview->players[i].heroPersistentId;
      }
      if (preview->currentNickname.empty())
      {
        preview->currentNickname = preview->players[i].nickname;
      }
      break;
    }
  }

  if (!currentPlayerFound && preview->playerInfoLoaded)
  {
    LinuxSessionPlayerPreview currentPlayer;
    currentPlayer.nickname = preview->currentNickname;
    currentPlayer.userId = preview->currentUserId;
    currentPlayer.heroWebId = preview->currentHeroWebId;
    currentPlayer.teamId = preview->currentTeamId;
    currentPlayer.partyId = preview->currentPartyId;
    currentPlayer.skinId = preview->currentSkinId;
    currentPlayer.heroPersistentId = preview->currentHeroPersistentId;
    currentPlayer.currentPlayer = true;
    preview->players.push_back(currentPlayer);
  }

  preview->valid =
    preview->loaded &&
    preview->playerInfoLoaded &&
    preview->usersDataLoaded &&
    !preview->players.empty();
}

void InitializeCmdLine(int argc, char** argv)
{
  std::vector<const char*> args(argc);
  for (int i = 0; i < argc; ++i)
  {
    args[i] = argv[i];
  }

  CmdLineLite::Instance().Init(argc, args.empty() ? 0 : &args[0]);
}

double ReadRunSeconds(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  return CmdLineLite::Instance().GetFloatKey("--seconds", -1.0f);
}

double ReadDemoCycleSeconds(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  const double value = CmdLineLite::Instance().GetFloatKey("--demo-cycle", 0.0f);
  return value > 0.0 ? value : 0.0;
}

const char* ReadStringArg(int argc, char** argv, const char* key)
{
  (void)argc;
  (void)argv;
  return CmdLineLite::Instance().GetStringKey(key, 0);
}

std::string ReadNamedKey(const char* key, const char* alternateKey)
{
  const char* value = 0;
  if (key)
  {
    value = CmdLineLite::Instance().GetStringKey(key, 0);
  }
  if ((!value || !value[0]) && alternateKey)
  {
    value = CmdLineLite::Instance().GetStringKey(alternateKey, 0);
  }
  return value ? value : "";
}

bool IsNamedKeyDefined(const char* key, const char* alternateKey = 0)
{
  if (key && CmdLineLite::Instance().IsKeyDefined(key))
  {
    return true;
  }
  return alternateKey && CmdLineLite::Instance().IsKeyDefined(alternateKey);
}

std::string ReadLocaleOverride(int argc, char** argv)
{
  const char* locale = ReadStringArg(argc, argv, "--locale");
  return locale ? locale : "";
}

std::string ReadMapSelector(int argc, char** argv)
{
  const char* value = ReadStringArg(argc, argv, "--map");
  return value ? value : "";
}

std::string ReadHeroSelector(int argc, char** argv)
{
  const char* value = ReadStringArg(argc, argv, "--hero");
  return value ? value : "";
}

unsigned long ReadWindowSize(int argc, char** argv, const char* key, unsigned long defaultValue)
{
  (void)argc;
  (void)argv;
  const int value = CmdLineLite::Instance().GetIntKey(key, static_cast<int>(defaultValue));
  if (value > 0)
  {
    return static_cast<unsigned long>(value);
  }
  return defaultValue;
}

bool TryReadWindowSize(const char* key, unsigned long* value)
{
  if (!key || !value || !CmdLineLite::Instance().IsKeyDefined(key))
  {
    return false;
  }

  const int parsed = CmdLineLite::Instance().GetIntKey(key, -1);
  if (parsed <= 0)
  {
    return false;
  }

  *value = static_cast<unsigned long>(parsed);
  return true;
}

bool TryAdoptParentWindowSize(LinuxClientLaunchSettings* settings)
{
  if (!settings)
  {
    return false;
  }

  bool changed = false;
  if (!CmdLineLite::Instance().IsKeyDefined("--width") && TryReadWindowSize("parentWidth", &settings->width))
  {
    settings->widthFromParent = true;
    changed = true;
  }

  if (!CmdLineLite::Instance().IsKeyDefined("--height") && TryReadWindowSize("parentHeight", &settings->height))
  {
    settings->heightFromParent = true;
    changed = true;
  }

  return changed;
}

fs::path ReadExecutablePath()
{
  char buffer[4096] = {0};
  const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (size <= 0)
  {
    return fs::path();
  }

  buffer[size] = 0;
  return fs::path(buffer);
}

bool IsGameRootCandidate(const fs::path& root)
{
  return fs::exists(root / "Data") && fs::exists(root / "Src");
}

fs::path DetectGameRoot(int argc, char** argv)
{
  std::vector<fs::path> candidates;

  if (const char* rootArg = ReadStringArg(argc, argv, "--root"))
  {
    candidates.push_back(fs::path(rootArg));
  }

  const fs::path currentDir = fs::current_path();
  const fs::path executablePath = ReadExecutablePath();
  const fs::path executableDir = executablePath.empty() ? fs::path() : executablePath.parent_path();

  candidates.push_back(currentDir);
  candidates.push_back(currentDir / "pw" / "branches" / "r1117");

  if (!executableDir.empty())
  {
    candidates.push_back(executableDir);
    candidates.push_back(executableDir.parent_path());
    candidates.push_back(executableDir / "pw" / "branches" / "r1117");
  }

  for (size_t i = 0; i < candidates.size(); ++i)
  {
    std::error_code error;
    const fs::path normalized = fs::weakly_canonical(candidates[i], error);
    const fs::path candidate = error ? candidates[i] : normalized;
    if (IsGameRootCandidate(candidate))
    {
      return candidate;
    }
  }

  return fs::path();
}

fs::path DetectLogsDir()
{
  if (const char* home = getenv("HOME"))
  {
    return fs::path(home) / ".local" / "state" / "primeworld-classic" / "logs";
  }

  return fs::temp_directory_path() / "primeworld-classic" / "logs";
}

fs::path DetectReplayFile()
{
  static const char* const kReplayExtensions[] = {
    ".rpl",
    ".replay",
    ".rep",
    ".pwrpl",
    ".pwrp"
  };

  for (int i = 0; i < CmdLineLite::Instance().ArgsCount(); ++i)
  {
    const char* arg = CmdLineLite::Instance().Argument(i);
    if (!arg || !arg[0] || arg[0] == '-')
    {
      continue;
    }

    const fs::path candidate = fs::path(arg);
    std::error_code error;
    if (fs::exists(candidate, error) && !error && candidate.has_extension())
    {
      const std::string extension = ToAsciiLower(candidate.extension().string());
      for (size_t extensionIndex = 0; extensionIndex < sizeof(kReplayExtensions) / sizeof(kReplayExtensions[0]); ++extensionIndex)
      {
        if (extension == kReplayExtensions[extensionIndex])
        {
          return candidate;
        }
      }
    }
  }

  return fs::path();
}

bool InitializeEngineEnvironment(const fs::path& gameRoot, LinuxClientEnvironment* environment)
{
  if (gameRoot.empty())
  {
    return false;
  }

  const fs::path binDir = fs::exists(gameRoot / "Bin") ? gameRoot / "Bin" : gameRoot;

  std::error_code error;
  fs::current_path(binDir, error);
  if (error)
  {
    return false;
  }

  NFile::InitBaseDir();
  NProfile::Init(PRODUCT_TITLE);

  environment->baseDir = fs::path(NFile::GetBaseDir().c_str());
  environment->binDir = fs::path(NFile::GetBinDir().c_str());
  environment->userDir = fs::path(NProfile::GetFullFolderPath(NProfile::FOLDER_USER).c_str());
  environment->logsDir = fs::path(NProfile::GetFullFolderPath(NProfile::FOLDER_LOGS).c_str());

  const string logsPath = environment->logsDir.c_str();
  if (!NFile::DoesFolderExist(logsPath))
  {
    NFile::CreatePath(logsPath);
  }

  environment->engineReady = NFile::DoesFolderExist(logsPath);
  return environment->engineReady;
}

std::string TrimAscii(const std::string& text)
{
  size_t begin = 0;
  while (begin < text.size() && isspace(static_cast<unsigned char>(text[begin])))
  {
    ++begin;
  }

  size_t end = text.size();
  while (end > begin && isspace(static_cast<unsigned char>(text[end - 1])))
  {
    --end;
  }

  return text.substr(begin, end - begin);
}

std::string BuildVersionString()
{
  char buffer[64] = {0};
  snprintf(
    buffer,
    sizeof(buffer),
    "%d.%d.%d",
    VERSION_MAJOR,
    VERSION_MINOR,
    VERSION_PATCH
  );
  return buffer;
}

std::vector<std::string> SplitString(const std::string& text, char delimiter)
{
  std::vector<std::string> tokens;
  size_t begin = 0;
  while (begin <= text.size())
  {
    const size_t end = text.find(delimiter, begin);
    const std::string token = text.substr(
      begin,
      end == std::string::npos ? std::string::npos : end - begin
    );
    if (!token.empty())
    {
      tokens.push_back(token);
    }
    if (end == std::string::npos)
    {
      break;
    }
    begin = end + 1;
  }
  return tokens;
}

std::string MaskSensitiveValue(const std::string& value, size_t prefixLength = 6, size_t suffixLength = 4)
{
  if (value.empty())
  {
    return "<none>";
  }

  if (value.size() <= prefixLength + suffixLength + 3)
  {
    return value;
  }

  return value.substr(0, prefixLength) + "..." + value.substr(value.size() - suffixLength);
}

std::string ExtractHttpBody(const std::string& response)
{
  size_t separator = response.find("\r\n\r\n");
  if (separator != std::string::npos)
  {
    return response.substr(separator + 4);
  }

  separator = response.find("\n\n");
  if (separator != std::string::npos)
  {
    return response.substr(separator + 2);
  }

  return response;
}

bool FetchLauncherProtocolJson(std::string* responseBody, std::string* error)
{
  if (!responseBody)
  {
    if (error)
    {
      *error = "response buffer is missing";
    }
    return false;
  }

  const int socketFd = socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd < 0)
  {
    if (error)
    {
      *error = std::string("socket() failed: ") + strerror(errno);
    }
    return false;
  }

  timeval timeout = {};
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(34980);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
  {
    if (error)
    {
      *error = std::string("connect(127.0.0.1:34980) failed: ") + strerror(errno);
    }
    close(socketFd);
    return false;
  }

  static const char requestBody[] = "getConnectionData";
  char request[512] = {0};
  snprintf(
    request,
    sizeof(request),
    "POST /getConnectionData HTTP/1.1\r\n"
    "Host: 127.0.0.1\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: %lu\r\n"
    "Connection: close\r\n\r\n"
    "%s",
    static_cast<unsigned long>(strlen(requestBody)),
    requestBody
  );

  const size_t requestLength = strlen(request);
  size_t totalSent = 0;
  while (totalSent < requestLength)
  {
    const ssize_t sent = send(
      socketFd,
      request + totalSent,
      requestLength - totalSent,
      0
    );
    if (sent <= 0)
    {
      if (error)
      {
        *error = std::string("send() failed: ") + strerror(errno);
      }
      close(socketFd);
      return false;
    }
    totalSent += static_cast<size_t>(sent);
  }

  std::string response;
  char buffer[4096] = {0};
  for (;;)
  {
    const ssize_t received = recv(socketFd, buffer, sizeof(buffer), 0);
    if (received == 0)
    {
      break;
    }
    if (received < 0)
    {
      if (error)
      {
        *error = std::string("recv() failed: ") + strerror(errno);
      }
      close(socketFd);
      return false;
    }
    response.append(buffer, buffer + received);
  }

  close(socketFd);
  *responseBody = TrimAscii(ExtractHttpBody(response));
  if (responseBody->empty())
  {
    if (error)
    {
      *error = "launcher response body is empty";
    }
    return false;
  }
  return true;
}

std::string DecodeJsonStringLiteral(const std::string& value)
{
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i)
  {
    const char ch = value[i];
    if (ch != '\\' || i + 1 >= value.size())
    {
      decoded.push_back(ch);
      continue;
    }

    const char escape = value[++i];
    switch (escape)
    {
      case '\\':
      case '/':
      case '"':
        decoded.push_back(escape);
        break;

      case 'b':
        decoded.push_back('\b');
        break;

      case 'f':
        decoded.push_back('\f');
        break;

      case 'n':
        decoded.push_back('\n');
        break;

      case 'r':
        decoded.push_back('\r');
        break;

      case 't':
        decoded.push_back('\t');
        break;

      default:
        decoded.push_back(escape);
        break;
    }
  }
  return decoded;
}

std::string ExtractJsonStringField(const std::string& json, const char* fieldName)
{
  if (!fieldName || !fieldName[0])
  {
    return "";
  }

  const std::string fieldMarker = std::string("\"") + fieldName + "\"";
  size_t fieldPos = json.find(fieldMarker);
  while (fieldPos != std::string::npos)
  {
    size_t colonPos = json.find(':', fieldPos + fieldMarker.size());
    if (colonPos == std::string::npos)
    {
      return "";
    }

    size_t valuePos = colonPos + 1;
    while (valuePos < json.size() && isspace(static_cast<unsigned char>(json[valuePos])))
    {
      ++valuePos;
    }

    if (valuePos >= json.size() || json[valuePos] != '"')
    {
      fieldPos = json.find(fieldMarker, fieldPos + fieldMarker.size());
      continue;
    }

    ++valuePos;
    std::string rawValue;
    bool escaped = false;
    while (valuePos < json.size())
    {
      const char ch = json[valuePos++];
      if (!escaped && ch == '"')
      {
        return DecodeJsonStringLiteral(rawValue);
      }
      if (ch == '\\' && !escaped)
      {
        escaped = true;
        rawValue.push_back(ch);
        continue;
      }

      rawValue.push_back(ch);
      escaped = false;
    }
    return "";
  }

  return "";
}

bool ParseLaunchProtocolLine(const std::string& protocolLine, LinuxLaunchPreview* preview)
{
  if (!preview)
  {
    return false;
  }

  preview->protocolLine = TrimAscii(protocolLine);
  preview->protocolPresent = !preview->protocolLine.empty();
  preview->protocolValid = false;
  preview->versionMatches = false;
  preview->mirrorIndex = -1;

  if (!preview->protocolPresent)
  {
    return false;
  }

  const std::vector<std::string> tokens = SplitString(preview->protocolLine, '/');
  if (tokens.size() < 5)
  {
    preview->warnings.push_back("Launch protocol has too few tokens");
    return false;
  }

  preview->method = tokens[1];
  preview->token = tokens[2];
  preview->version = tokens[3];
  preview->mirrorIndex = atoi(tokens[4].c_str());
  preview->protocolValid = preview->method == "runGame" || preview->method == "reconnect";
  preview->versionMatches = preview->version == BuildVersionString();

  if (!preview->protocolValid)
  {
    preview->warnings.push_back("Unsupported launch protocol method: " + preview->method);
  }
  if (!preview->versionMatches)
  {
    preview->warnings.push_back(
      "Launch protocol version mismatch: " + preview->version + " vs " + BuildVersionString()
    );
  }
  if (preview->token.empty())
  {
    preview->warnings.push_back("Launch protocol token is empty");
  }

  return preview->protocolValid;
}

void ProbeLaunchPreview(LinuxClientLaunchSettings* settings, LinuxLaunchPreview* preview)
{
  if (!settings || !preview)
  {
    return;
  }

  *preview = LinuxLaunchPreview();
  preview->parentWindowLaunch =
    CmdLineLite::Instance().IsKeyDefined("parentWidth") ||
    CmdLineLite::Instance().IsKeyDefined("parentSessionLogin");
  preview->sessionLogin = ReadNamedKey("parentSessionLogin", "-session_login");
  preview->sessionLoginProvided = !preview->sessionLogin.empty();
  preview->mapId = ReadNamedKey("mapId", "--mapId");
  preview->mapIdProvided = !preview->mapId.empty();
  preview->serverName = ReadNamedKey("serverName", "--server");
  preview->uid = ReadNamedKey("uid");
  preview->snid = ReadNamedKey("--snid", "snid");
  preview->snuid = ReadNamedKey("--snuid", "snuid");

  const std::string protocolLine = ReadNamedKey("protocol", "--protocol");
  if (!protocolLine.empty())
  {
    preview->source = "command-line-protocol";
    ParseLaunchProtocolLine(protocolLine, preview);
  }
  else if (IsNamedKeyDefined("linux", "--linux"))
  {
    preview->launcherRequested = true;
    preview->launcherFetchAttempted = true;

    std::string responseBody;
    std::string error;
    if (FetchLauncherProtocolJson(&responseBody, &error))
    {
      preview->launcherFetchSucceeded = true;
      preview->launcherResponse = responseBody;
      preview->source = "launcher-socket";

      const std::string discoveredProtocol = ExtractJsonStringField(responseBody, "protocol");
      if (!discoveredProtocol.empty())
      {
        ParseLaunchProtocolLine(discoveredProtocol, preview);
      }
      else
      {
        preview->warnings.push_back("Launcher response missing protocol field");
      }
    }
    else
    {
      preview->source = "launcher-socket";
      preview->warnings.push_back("Launcher protocol fetch failed: " + error);
    }
  }

  if (preview->source.empty())
  {
    preview->source = preview->parentWindowLaunch ? "parent-window" : "manual-shell";
  }

  if (settings->mapSelector.empty() && preview->mapIdProvided)
  {
    settings->mapSelector = preview->mapId;
  }
}

std::string JoinWideParams(const vector<wstring>& params)
{
  std::string joined;
  for (size_t i = 0; i < params.size(); ++i)
  {
    if (!joined.empty())
    {
      joined += ' ';
    }

    joined += NStr::ToMBCS(params[i]).c_str();
  }

  return joined;
}

typedef map<string, string> TLinuxCommandBindings;

TLinuxCommandBindings& LinuxCommandBindings()
{
  static TLinuxCommandBindings bindings;
  return bindings;
}

int& LinuxCommandBindingCounter()
{
  static int nextId = 1;
  return nextId;
}

bool LinuxBindCommand(const char* name, const vector<wstring>& paramsSet, const wstring& context)
{
  if (paramsSet.size() <= 1)
  {
    return true;
  }

  Input::Binds* binds = Input::BindsManager::Instance()->GetBinds();
  if (binds)
  {
    vector<wstring> bindStringParams(paramsSet);
    bindStringParams.back() = L"\"" + bindStringParams.back() + L"\"";
    binds->RegisterBindString(context, name, bindStringParams);
  }

  vector<string> keys;
  keys.reserve(paramsSet.size() - 1);
  for (int i = 0; i < paramsSet.size() - 1; ++i)
  {
    if (paramsSet[i] == L"+")
    {
      continue;
    }

    keys.push_back(string());
    NStr::ToMBCS(&keys.back(), paramsSet[i]);
    NStr::TrimBoth(keys.back(), "\t\n\r\'");
  }

  string command = NStr::ToMBCS(paramsSet.back());
  if (keys.empty() || command.empty())
  {
    return true;
  }

  const string bindId = NStr::StrFmt("linux_command_bind_event_%d", LinuxCommandBindingCounter()++);
  LinuxCommandBindings()[bindId] = command;

  if (binds)
  {
    binds->ParseDefineCommand(bindId, keys);
  }

  return true;
}

bool RunLinuxCommandBinding(const Input::Event& event)
{
  if (!event.Command())
  {
    return false;
  }

  const TLinuxCommandBindings::const_iterator it = LinuxCommandBindings().find(event.Command()->Name());
  if (it == LinuxCommandBindings().end())
  {
    return false;
  }

  vector<string> commands;
  NStr::SplitString(it->second, &commands, ';');
  for (int i = 0; i < commands.size(); ++i)
  {
    string command(commands[i]);
    NStr::TrimBoth(command, " \t\r\n");
    if (!command.empty())
    {
      NGlobal::RunCommand(NStr::ToUnicode(command));
    }
  }

  return true;
}

REGISTER_CMD_EX(bind_command, LinuxBindCommand)

bool LinuxBootstrapConfigCommand(const char* name, const vector<wstring>& params)
{
  const std::string value = JoinWideParams(params);
  NGlobal::SetVar(name, value.empty() ? NGlobal::VariantValue("1") : NGlobal::VariantValue(value.c_str()), STORAGE_GLOBAL);
  return true;
}

std::string ExtractConfigCommandName(const std::string& line)
{
  const std::string trimmed = TrimAscii(line);
  if (trimmed.empty() || trimmed[0] == ';' || trimmed.compare(0, 2, "//") == 0)
  {
    return "";
  }

  size_t pos = 0;
  while (pos < trimmed.size() && !isspace(static_cast<unsigned char>(trimmed[pos])))
  {
    ++pos;
  }

  return trimmed.substr(0, pos);
}

std::string ExtractConfigExecTarget(const std::string& line)
{
  const std::string trimmed = TrimAscii(line);
  if (trimmed.compare(0, 4, "exec") != 0)
  {
    return "";
  }

  std::string value = TrimAscii(trimmed.substr(4));
  if (value.empty())
  {
    return "";
  }

  if (value[0] == '\"')
  {
    const size_t quotePos = value.find('\"', 1);
    return quotePos == std::string::npos ? TrimAscii(value.substr(1)) : value.substr(1, quotePos - 1);
  }

  const size_t spacePos = value.find_first_of(" \t\r\n");
  return spacePos == std::string::npos ? value : value.substr(0, spacePos);
}

bool ReadTextFile(const fs::path& path, std::string* content);
std::string NormalizeRootFileSystemPath(std::string fileName);
bool ReadRootFileUtf16Text(const string& fileName, std::string* text);
bool ReadTextRefFromDataRoot(const fs::path& dataRoot, const std::string& textRef, std::string* value);
std::vector<std::string> ExtractItemBlocks(const std::string& text);

void CollectBootstrapCommands(
  const fs::path& profilesRoot,
  const std::string& fileName,
  std::set<std::string>* commands,
  std::set<std::string>* visitedFiles
)
{
  const fs::path filePath = profilesRoot / fileName;
  const std::string visitKey = filePath.lexically_normal().string();
  if (!visitedFiles->insert(visitKey).second)
  {
    return;
  }

  std::string content;
  if (!ReadTextFile(filePath, &content))
  {
    return;
  }

  size_t lineStart = 0;
  while (lineStart <= content.size())
  {
    const size_t lineEnd = content.find('\n', lineStart);
    const std::string line = content.substr(
      lineStart,
      lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart
    );
    const std::string commandName = ExtractConfigCommandName(line);

    if (!commandName.empty())
    {
      if (commandName == "exec")
      {
        const std::string nestedFile = ExtractConfigExecTarget(line);
        if (!nestedFile.empty())
        {
          CollectBootstrapCommands(profilesRoot, nestedFile, commands, visitedFiles);
        }
      }
      else if (commandName != "setvar")
      {
        commands->insert(commandName);
      }
    }

    if (lineEnd == std::string::npos)
    {
      break;
    }

    lineStart = lineEnd + 1;
  }
}

std::string ReadConfigVar(const char* name)
{
  const string value = NGlobal::GetVar(name, NGlobal::VariantValue("")).Get<string>();
  return std::string(value.c_str());
}

void RegisterLinuxBootstrapCommands(
  const LinuxClientEnvironment& environment,
  LinuxConfigBootstrapPreview* preview
)
{
  if (environment.baseDir.empty())
  {
    preview->warnings.push_back("Config bootstrap skipped: base dir unavailable");
    return;
  }

  const fs::path profilesRoot = environment.baseDir / "Profiles";
  if (!fs::exists(profilesRoot) || !fs::is_directory(profilesRoot))
  {
    preview->warnings.push_back("Config bootstrap skipped: Profiles directory missing");
    return;
  }

  std::set<std::string> commands;
  std::set<std::string> visitedFiles;
  CollectBootstrapCommands(profilesRoot, "default.cfg", &commands, &visitedFiles);
  CollectBootstrapCommands(profilesRoot, "social.cfg", &commands, &visitedFiles);
  CollectBootstrapCommands(profilesRoot, "game.cfg", &commands, &visitedFiles);
  CollectBootstrapCommands(profilesRoot, "spectator.cfg", &commands, &visitedFiles);

  for (std::set<std::string>::const_iterator it = commands.begin(); it != commands.end(); ++it)
  {
    const string commandName((*it).c_str());
    if (NGlobal::IsCommandRegistred(commandName))
    {
      continue;
    }

    if (NGlobal::RegisterCmd(commandName, LinuxBootstrapConfigCommand))
    {
      ++preview->placeholderCommandCount;
    }
  }

  preview->commandsRegistered = true;
}

void BootstrapConfigState(
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  LinuxConfigBootstrapPreview* preview
)
{
  if (!environment.engineReady)
  {
    preview->warnings.push_back("Config bootstrap skipped: engine environment not ready");
    return;
  }

  RegisterLinuxBootstrapCommands(environment, preview);

  preview->defaultLoaded = NGlobal::ExecuteConfig("default.cfg", NProfile::FOLDER_GLOBAL);
  preview->userLoaded = NGlobal::ExecuteConfig("user.cfg", NProfile::FOLDER_USER);
  preview->langLoaded = NGlobal::ExecuteConfig("lang.cfg", NProfile::FOLDER_PLAYER);
  preview->socialLoaded = NGlobal::ExecuteConfig("social.cfg", NProfile::FOLDER_GLOBAL);
  preview->gameLoaded = NGlobal::ExecuteConfig("game.cfg", NProfile::FOLDER_GLOBAL);

  if (settings.spectator)
  {
    preview->spectatorLoaded = NGlobal::ExecuteConfig("spectator.cfg", NProfile::FOLDER_GLOBAL);
  }

  if (!preview->defaultLoaded)
  {
    preview->warnings.push_back("default.cfg not loaded");
  }

  if (!preview->socialLoaded)
  {
    preview->warnings.push_back("social.cfg not loaded");
  }

  if (!preview->gameLoaded)
  {
    preview->warnings.push_back("game.cfg not loaded");
  }

  if (settings.spectator && !preview->spectatorLoaded)
  {
    preview->warnings.push_back("spectator.cfg not loaded");
  }

  preview->language = ReadConfigVar("language");
  preview->gfxFullscreen = ReadConfigVar("gfx_fullscreen");
  preview->gfxResolution = ReadConfigVar("gfx_resolution");
  preview->localGame = ReadConfigVar("local_game");
  preview->loginAddress = ReadConfigVar("login_address");
  preview->socialLoginAddress = ReadConfigVar("social_login_address");
  preview->statClientUrl = ReadConfigVar("stat_client_url");
}

size_t CountBindStrings(const Input::TBinds& bindStrings)
{
  size_t total = 0;
  for (Input::TBinds::const_iterator it = bindStrings.begin(); it != bindStrings.end(); ++it)
  {
    total += it->second.size();
  }
  return total;
}

void InitializeInputState(const LinuxClientEnvironment& environment, LinuxInputState* state)
{
  if (!environment.engineReady)
  {
    state->warnings.push_back("Input bootstrap skipped: engine environment not ready");
    return;
  }

  state->hwInput = new LinuxHwInput();
  state->binds = new Input::Binds(state->hwInput.GetPtr());
  Input::BindsManager::Instance()->SetBinds(state->binds.GetPtr());
  state->hardwareControlCount = state->hwInput->ControlCount();

  NHPTimer::GetTime(state->lastUpdateTime);
  state->initialized = IsValid(state->hwInput) && IsValid(state->binds);
}

void FinalizeInputState(LinuxInputState* state)
{
  if (!state->initialized || !IsValid(state->binds))
  {
    return;
  }

  state->inputOverrideLoaded = NGlobal::ExecuteConfig("input_new.cfg", NProfile::FOLDER_USER, L"input");

  const Input::TBinds& bindStrings = state->binds->GetBindStrings();
  state->bindContextCount = bindStrings.size();
  state->bindStringCount = CountBindStrings(bindStrings);
  state->inputConfigLoaded = state->bindStringCount > 0;

  if (!state->inputConfigLoaded)
  {
    state->warnings.push_back("input.cfg did not register any binds");
  }
}

std::string NormalizeLocaleName(const std::string& locale)
{
  std::string normalized = locale;
  const size_t dotPos = normalized.find('.');
  if (dotPos != std::string::npos)
  {
    normalized.erase(dotPos);
  }

  const size_t atPos = normalized.find('@');
  if (atPos != std::string::npos)
  {
    normalized.erase(atPos);
  }

  for (size_t i = 0; i < normalized.size(); ++i)
  {
    if (normalized[i] == '_')
    {
      normalized[i] = '-';
    }
  }

  if (normalized.size() >= 5 && normalized[2] == '-')
  {
    normalized[0] = static_cast<char>(tolower(static_cast<unsigned char>(normalized[0])));
    normalized[1] = static_cast<char>(tolower(static_cast<unsigned char>(normalized[1])));
    normalized[3] = static_cast<char>(toupper(static_cast<unsigned char>(normalized[3])));
    normalized[4] = static_cast<char>(toupper(static_cast<unsigned char>(normalized[4])));
  }

  return normalized;
}

void AddLocaleCandidate(std::vector<std::string>* candidates, const std::string& locale)
{
  const std::string normalized = NormalizeLocaleName(locale);
  if (normalized.empty())
  {
    return;
  }

  for (size_t i = 0; i < candidates->size(); ++i)
  {
    if ((*candidates)[i] == normalized)
    {
      return;
    }
  }

  candidates->push_back(normalized);
}

fs::path DetectLocalizationRoot(
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxConfigBootstrapPreview& configPreview,
  std::string* selectedLocale
)
{
  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  if (baseRoot.empty())
  {
    return fs::path();
  }

  const fs::path localizationRoot = baseRoot / "Localization";
  if (!fs::exists(localizationRoot) || !fs::is_directory(localizationRoot))
  {
    return fs::path();
  }

  std::vector<std::string> candidates;
  AddLocaleCandidate(&candidates, settings.localeOverride);
  AddLocaleCandidate(&candidates, configPreview.language);

  const char* envKeys[] = {"PW_LOCALE", "LC_ALL", "LC_MESSAGES", "LANGUAGE", "LANG"};
  for (size_t i = 0; i < sizeof(envKeys) / sizeof(envKeys[0]); ++i)
  {
    if (const char* value = getenv(envKeys[i]))
    {
      AddLocaleCandidate(&candidates, value);
    }
  }

  AddLocaleCandidate(&candidates, "en-US");
  AddLocaleCandidate(&candidates, "ru-RU");

  fs::path partialRoot;
  std::string partialLocale;
  for (size_t i = 0; i < candidates.size(); ++i)
  {
    const fs::path candidateRoot = localizationRoot / candidates[i];
    if (fs::exists(candidateRoot) && fs::is_directory(candidateRoot))
    {
      if (fs::exists(candidateRoot / "PvX" / "strings.xml"))
      {
        *selectedLocale = candidates[i];
        return candidateRoot;
      }

      if (partialRoot.empty())
      {
        partialRoot = candidateRoot;
        partialLocale = candidates[i];
      }
    }
  }

  std::error_code error;
  for (fs::directory_iterator it(localizationRoot, error); !error && it != fs::directory_iterator(); ++it)
  {
    if (it->is_directory())
    {
      if (fs::exists(it->path() / "PvX" / "strings.xml"))
      {
        *selectedLocale = it->path().filename().string();
        return it->path();
      }

      if (partialRoot.empty())
      {
        partialRoot = it->path();
        partialLocale = it->path().filename().string();
      }
    }
  }

  if (!partialRoot.empty())
  {
    *selectedLocale = partialLocale;
    return partialRoot;
  }

  return fs::path();
}

size_t CountFilesInTree(const fs::path& root, const char* extension)
{
  if (!fs::exists(root) || !fs::is_directory(root))
  {
    return 0;
  }

  size_t count = 0;
  std::error_code error;
  for (fs::recursive_directory_iterator it(root, error); !error && it != fs::recursive_directory_iterator(); ++it)
  {
    if (!it->is_regular_file())
    {
      continue;
    }

    if (!extension || it->path().extension() == extension)
    {
      ++count;
    }
  }

  return count;
}

bool ProbeMountedFile(
  WinFileSystem& fileSystem,
  const std::vector<std::string>& candidates,
  std::string* fileName,
  int* fileSize
)
{
  for (size_t i = 0; i < candidates.size(); ++i)
  {
    CObj<Stream> stream = fileSystem.OpenFile(string(candidates[i].c_str()), FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
    if (!IsValid(stream) || !stream->IsOk())
    {
      continue;
    }

    *fileName = candidates[i];
    *fileSize = stream->GetSize();
    return true;
  }

  return false;
}

void ProbeContentRoots(
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxConfigBootstrapPreview& configPreview,
  LinuxContentProbe* probe
)
{
  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  if (baseRoot.empty())
  {
    probe->warnings.push_back("Content root unavailable");
    return;
  }

  const fs::path dataRoot = baseRoot / "Data";
  if (fs::exists(dataRoot) && fs::is_directory(dataRoot))
  {
    WinFileSystem dataFileSystem(string(dataRoot.string().c_str()), false);
    probe->dataMounted = true;
    probe->uiXdbCount = CountFilesInTree(dataRoot / "UI", ".xdb");
    probe->uiScreenFileCount = CountFilesInTree(dataRoot / "UI" / "Screens", 0);
    probe->socialXdbCount = CountFilesInTree(dataRoot / "Social", ".xdb");
    ProbeMountedFile(
      dataFileSystem,
      std::vector<std::string>{
        "UI/Events/UICustomEvents.UIEVCOLL.xdb",
        "UI/Cameras/game_camera.CAMS.xdb",
        "Glyphs/WardGlyph.SSO.xdb"
      },
      &probe->dataSampleFile,
      &probe->dataSampleSize
    );
  }
  else
  {
    probe->warnings.push_back("Data root missing");
  }

  probe->localizationRoot = DetectLocalizationRoot(environment, settings, configPreview, &probe->locale);
  if (!probe->localizationRoot.empty())
  {
    WinFileSystem localizationFileSystem(string(probe->localizationRoot.string().c_str()), false);
    probe->localizationMounted = true;
    probe->localizationFileCount = CountFilesInTree(probe->localizationRoot, 0);
    ProbeMountedFile(
      localizationFileSystem,
      std::vector<std::string>{
        "PvX/strings.xml",
        "main.swf",
        "pw_loading_screen.png"
      },
      &probe->localizationSampleFile,
      &probe->localizationSampleSize
    );
  }
  else
  {
    probe->warnings.push_back("Localization root missing");
  }

  if (!configPreview.language.empty() &&
      settings.localeOverride.empty() &&
      !probe->locale.empty() &&
      NormalizeLocaleName(configPreview.language) != probe->locale)
  {
    probe->warnings.push_back(
      "Config language fallback: " + NormalizeLocaleName(configPreview.language) + " -> " + probe->locale
    );
  }
}

bool ReadTextFile(const fs::path& path, std::string* content)
{
  std::ifstream file(path.string().c_str(), std::ios::binary);
  if (!file.is_open())
  {
    return false;
  }

  content->assign(
    std::istreambuf_iterator<char>(file),
    std::istreambuf_iterator<char>()
  );

  if (content->size() >= 3 &&
      static_cast<unsigned char>((*content)[0]) == 0xEF &&
      static_cast<unsigned char>((*content)[1]) == 0xBB &&
      static_cast<unsigned char>((*content)[2]) == 0xBF)
  {
    content->erase(0, 3);
  }

  return true;
}

bool LoadPngArtwork(const fs::path& path, LinuxLoadingArtwork* artwork, std::string* error)
{
  if (path.empty())
  {
    if (error)
    {
      *error = "file path is empty";
    }
    return false;
  }

  png_image image = {};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, path.string().c_str()))
  {
    if (error)
    {
      *error = image.message ? image.message : "png_image_begin_read_from_file failed";
    }
    return false;
  }

  image.format = PNG_FORMAT_RGBA;
  artwork->rgba.resize(PNG_IMAGE_SIZE(image));

  if (!png_image_finish_read(&image, 0, artwork->rgba.empty() ? 0 : &artwork->rgba[0], 0, 0))
  {
    if (error)
    {
      *error = image.message ? image.message : "png_image_finish_read failed";
    }
    png_image_free(&image);
    artwork->rgba.clear();
    return false;
  }

  artwork->width = static_cast<int>(image.width);
  artwork->height = static_cast<int>(image.height);
  artwork->ready = artwork->width > 0 && artwork->height > 0 && !artwork->rgba.empty();
  png_image_free(&image);

  if (!artwork->ready && error)
  {
    *error = "decoded PNG is empty";
  }

  return artwork->ready;
}

bool LoadDdsArtwork(const fs::path& path, LinuxLoadingArtwork* artwork, std::string* error)
{
  FileStream stream(string(path.string().c_str()), FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
  Stream* const streamPtr = std::addressof(static_cast<Stream&>(stream));
  if (!stream.IsOk())
  {
    if (error)
    {
      *error = "failed to open DDS file";
    }
    return false;
  }

  if (!NImage::RecognizeFormatDDS(streamPtr))
  {
    if (error)
    {
      *error = "file is not a DDS texture";
    }
    return false;
  }

  CArray2D<DWORD> pixels;
  if (!NImage::LoadImageDDS(&pixels, streamPtr) || pixels.IsEmpty())
  {
    if (error)
    {
      *error = "DDS decode failed";
    }
    return false;
  }

  artwork->width = pixels.GetSizeX();
  artwork->height = pixels.GetSizeY();
  artwork->rgba.resize(static_cast<size_t>(artwork->width) * static_cast<size_t>(artwork->height) * 4U);

  for (int y = 0; y < artwork->height; ++y)
  {
    for (int x = 0; x < artwork->width; ++x)
    {
      const DWORD argb = pixels[y][x];
      const size_t pixelIndex = static_cast<size_t>(y * artwork->width + x) * 4U;
      artwork->rgba[pixelIndex + 0] = static_cast<unsigned char>((argb >> 16) & 0xFFU);
      artwork->rgba[pixelIndex + 1] = static_cast<unsigned char>((argb >> 8) & 0xFFU);
      artwork->rgba[pixelIndex + 2] = static_cast<unsigned char>(argb & 0xFFU);
      artwork->rgba[pixelIndex + 3] = static_cast<unsigned char>((argb >> 24) & 0xFFU);
    }
  }

  artwork->ready = artwork->width > 0 && artwork->height > 0 && !artwork->rgba.empty();
  if (!artwork->ready && error)
  {
    *error = "decoded DDS is empty";
  }

  return artwork->ready;
}

bool ReadUtf16TextFile(const fs::path& path, std::string* text)
{
  std::ifstream file(path.string().c_str(), std::ios::binary);
  if (!file.is_open())
  {
    return false;
  }

  std::string content = std::string(
    std::istreambuf_iterator<char>(file),
    std::istreambuf_iterator<char>()
  );

  if (content.size() < 2)
  {
    return false;
  }

  const unsigned char bom0 = static_cast<unsigned char>(content[0]);
  const unsigned char bom1 = static_cast<unsigned char>(content[1]);
  if (bom0 != 0xFF || bom1 != 0xFE)
  {
    return false;
  }

  wstring wideText;
  wideText.reserve((content.size() - 2) / 2);

  for (size_t i = 2; i + 1 < content.size(); i += 2)
  {
    const unsigned short codeUnit =
      static_cast<unsigned short>(static_cast<unsigned char>(content[i])) |
      (static_cast<unsigned short>(static_cast<unsigned char>(content[i + 1])) << 8);

    if (codeUnit == 0)
    {
      continue;
    }

    wideText.push_back(static_cast<wchar_t>(codeUnit));
  }

  string utf8Text;
  NStr::UnicodeToUTF8(&utf8Text, wideText);
  *text = TrimAscii(std::string(utf8Text.c_str()));
  return !text->empty();
}

std::string ExtractTagValue(const std::string& text, const char* tag, size_t startPos)
{
  const std::string openTag = std::string("<") + tag + ">";
  const std::string closeTag = std::string("</") + tag + ">";

  const size_t openPos = text.find(openTag, startPos);
  if (openPos == std::string::npos)
  {
    return "";
  }

  const size_t valuePos = openPos + openTag.size();
  const size_t closePos = text.find(closeTag, valuePos);
  if (closePos == std::string::npos)
  {
    return "";
  }

  return text.substr(valuePos, closePos - valuePos);
}

std::string ExtractTagHref(const std::string& text, const char* tag, size_t startPos)
{
  const std::string openTag = std::string("<") + tag;
  const size_t openPos = text.find(openTag, startPos);
  if (openPos == std::string::npos)
  {
    return "";
  }

  const size_t hrefPos = text.find("href=\"", openPos);
  if (hrefPos == std::string::npos)
  {
    return "";
  }

  const size_t valuePos = hrefPos + strlen("href=\"");
  const size_t valueEnd = text.find("\"", valuePos);
  if (valueEnd == std::string::npos)
  {
    return "";
  }

  return text.substr(valuePos, valueEnd - valuePos);
}

std::string ExtractTagTextRef(const std::string& text, const char* tag, size_t startPos)
{
  const std::string openTag = std::string("<") + tag;
  const size_t openPos = text.find(openTag, startPos);
  if (openPos == std::string::npos)
  {
    return "";
  }

  const size_t textRefPos = text.find("textref=\"", openPos);
  if (textRefPos == std::string::npos)
  {
    return "";
  }

  const size_t valuePos = textRefPos + strlen("textref=\"");
  const size_t valueEnd = text.find("\"", valuePos);
  if (valueEnd == std::string::npos)
  {
    return "";
  }

  return text.substr(valuePos, valueEnd - valuePos);
}

bool TryExtractTagValue(
  const std::string& text,
  const char* tag,
  size_t startPos,
  std::string* value
)
{
  if (!value)
  {
    return false;
  }

  const std::string openTag = std::string("<") + tag + ">";
  const size_t openPos = text.find(openTag, startPos);
  if (openPos == std::string::npos)
  {
    return false;
  }

  const size_t valuePos = openPos + openTag.size();
  const std::string closeTag = std::string("</") + tag + ">";
  const size_t closePos = text.find(closeTag, valuePos);
  if (closePos == std::string::npos)
  {
    return false;
  }

  *value = text.substr(valuePos, closePos - valuePos);
  return true;
}

bool TryExtractTagHref(
  const std::string& text,
  const char* tag,
  size_t startPos,
  std::string* value
)
{
  if (!value)
  {
    return false;
  }

  const std::string openTag = std::string("<") + tag;
  const size_t openPos = text.find(openTag, startPos);
  if (openPos == std::string::npos)
  {
    return false;
  }

  const size_t tagEnd = text.find('>', openPos);
  if (tagEnd == std::string::npos)
  {
    return false;
  }

  const size_t hrefPos = text.find("href=\"", openPos);
  if (hrefPos == std::string::npos || hrefPos > tagEnd)
  {
    *value = "";
    return true;
  }

  const size_t valuePos = hrefPos + strlen("href=\"");
  const size_t valueEnd = text.find("\"", valuePos);
  if (valueEnd == std::string::npos || valueEnd > tagEnd)
  {
    return false;
  }

  *value = text.substr(valuePos, valueEnd - valuePos);
  return true;
}

std::string ExtractRootTagAttribute(const std::string& text, const char* attribute)
{
  if (!attribute || !*attribute)
  {
    return "";
  }

  size_t pos = 0;
  while ((pos = text.find('<', pos)) != std::string::npos)
  {
    if (pos + 1 >= text.size())
    {
      return "";
    }

    const char next = text[pos + 1];
    if (next == '?' || next == '!' || next == '/')
    {
      ++pos;
      continue;
    }

    const size_t tagEnd = text.find('>', pos + 1);
    if (tagEnd == std::string::npos)
    {
      return "";
    }

    const std::string marker = std::string(attribute) + "=\"";
    const size_t attrPos = text.find(marker, pos);
    if (attrPos == std::string::npos || attrPos > tagEnd)
    {
      return "";
    }

    const size_t valuePos = attrPos + marker.size();
    const size_t valueEnd = text.find('"', valuePos);
    if (valueEnd == std::string::npos || valueEnd > tagEnd)
    {
      return "";
    }

    return text.substr(valuePos, valueEnd - valuePos);
  }

  return "";
}

void ReplaceAll(std::string* text, const char* from, const char* to)
{
  size_t pos = 0;
  const size_t fromLength = strlen(from);
  const size_t toLength = strlen(to);

  while ((pos = text->find(from, pos)) != std::string::npos)
  {
    text->replace(pos, fromLength, to);
    pos += toLength;
  }
}

std::string DecodeXmlEntities(std::string value)
{
  ReplaceAll(&value, "&lt;", "<");
  ReplaceAll(&value, "&gt;", ">");
  ReplaceAll(&value, "&amp;", "&");
  ReplaceAll(&value, "&quot;", "\"");
  ReplaceAll(&value, "&apos;", "'");
  return value;
}

std::string CollapseWhitespace(const std::string& text)
{
  std::string collapsed;
  collapsed.reserve(text.size());

  bool lastWasSpace = false;
  for (size_t i = 0; i < text.size(); ++i)
  {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (isspace(ch))
    {
      if (!lastWasSpace)
      {
        collapsed.push_back(' ');
        lastWasSpace = true;
      }
      continue;
    }

    collapsed.push_back(static_cast<char>(ch));
    lastWasSpace = false;
  }

  while (!collapsed.empty() && collapsed[0] == ' ')
  {
    collapsed.erase(collapsed.begin());
  }

  while (!collapsed.empty() && collapsed[collapsed.size() - 1] == ' ')
  {
    collapsed.erase(collapsed.end() - 1);
  }

  return collapsed;
}

std::string SanitizeLocalizedText(std::string value)
{
  value = DecodeXmlEntities(value);
  ReplaceAll(&value, "<br>", " | ");
  ReplaceAll(&value, "<br/>", " | ");
  ReplaceAll(&value, "<space:1>", " ");
  ReplaceAll(&value, "<space:2>", "  ");
  return CollapseWhitespace(value);
}

std::string NormalizeTextRefForLocalization(std::string textRef)
{
  while (!textRef.empty() && (textRef[0] == '/' || textRef[0] == '\\'))
  {
    textRef.erase(textRef.begin());
  }

  for (size_t i = 0; i < textRef.size(); ++i)
  {
    if (textRef[i] == '/')
    {
      textRef[i] = '\\';
    }
  }

  return textRef;
}

bool ExtractLocalizedFileText(const std::string& xml, const std::string& fileName, std::string* value)
{
  const std::string marker = "<file name=\"" + fileName + "\">";
  const size_t filePos = xml.find(marker);
  if (filePos == std::string::npos)
  {
    return false;
  }

  const size_t fileEnd = xml.find("</file>", filePos);
  if (fileEnd == std::string::npos)
  {
    return false;
  }

  const std::string fileBlock = xml.substr(filePos, fileEnd - filePos);
  const std::string prefix = ExtractTagValue(fileBlock, "prefix", 0);
  const std::string text = ExtractTagValue(fileBlock, "text", 0);
  const std::string suffix = ExtractTagValue(fileBlock, "suffix", 0);
  *value = SanitizeLocalizedText(prefix + text + suffix);
  return !value->empty();
}

std::string TruncateForOverlay(const std::string& value, size_t maxLength)
{
  if (value.size() <= maxLength)
  {
    return value;
  }

  if (maxLength <= 3)
  {
    return value.substr(0, maxLength);
  }

  return value.substr(0, maxLength - 3) + "...";
}

void ProbeLoadingScreenAssets(
  const LinuxClientEnvironment& environment,
  const LinuxContentProbe& contentProbe,
  LinuxLoadingScreenPreview* preview
)
{
  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  if (baseRoot.empty())
  {
    preview->warnings.push_back("Loading screen root unavailable");
    return;
  }

  const fs::path layoutPath = baseRoot / "Data" / "UI" / "Screens" / "Loading" / "LoadingScreen.xdb";
  std::string layoutXml;
  if (!ReadTextFile(layoutPath, &layoutXml))
  {
    preview->warnings.push_back("LoadingScreen.xdb missing");
    return;
  }

  preview->layoutFound = true;
  const size_t flashLayoutPos = layoutXml.find("<UIFlashLayout2>");
  const size_t flashLayoutEnd = layoutXml.find("</UIFlashLayout2>", flashLayoutPos);
  if (flashLayoutPos == std::string::npos || flashLayoutEnd == std::string::npos)
  {
    preview->warnings.push_back("Flash layout block missing");
    return;
  }

  const std::string flashLayout = layoutXml.substr(flashLayoutPos, flashLayoutEnd - flashLayoutPos);
  preview->flashAsset = ExtractTagValue(flashLayout, "srcFileName", 0);

  const size_t sizePos = flashLayout.find("<size>");
  const size_t sizeEnd = flashLayout.find("</size>", sizePos);
  if (sizePos != std::string::npos && sizeEnd != std::string::npos)
  {
    const std::string sizeBlock = flashLayout.substr(sizePos, sizeEnd - sizePos);
    preview->width = static_cast<unsigned long>(atoi(ExtractTagValue(sizeBlock, "x", 0).c_str()));
    preview->height = static_cast<unsigned long>(atoi(ExtractTagValue(sizeBlock, "y", 0).c_str()));
  }

  if (!preview->flashAsset.empty())
  {
    fs::path flashAssetPath = baseRoot / "Data";
    std::string relativeFlashAsset = preview->flashAsset;
    while (!relativeFlashAsset.empty() && (relativeFlashAsset[0] == '/' || relativeFlashAsset[0] == '\\'))
    {
      relativeFlashAsset.erase(relativeFlashAsset.begin());
    }
    flashAssetPath /= relativeFlashAsset;

    std::error_code error;
    const uintmax_t flashSize = fs::file_size(flashAssetPath, error);
    if (!error)
    {
      preview->flashAssetSize = static_cast<int>(flashSize);
    }
  }

  if (contentProbe.localizationRoot.empty())
  {
    preview->warnings.push_back("Localization data unavailable");
    return;
  }

  std::string localizationXml;
  if (!ReadTextFile(contentProbe.localizationRoot / "PvX" / "strings.xml", &localizationXml))
  {
    preview->warnings.push_back("PvX/strings.xml missing");
    return;
  }

  preview->localizationLoaded = true;

  struct SPropertyTarget
  {
    const char* name;
    const char* label;
  };

  static const SPropertyTarget targets[] = {
    {"ExitButtonText", "Exit"},
    {"ContextMenu_Message", "Message"},
    {"ChatBar_CantFindPlayer", "Lookup"},
    {"PremiumAccountTooltip", "Premium"},
    {"PreferenceMenu_ReceiveRegularChatMessages", "Chat toggle"}
  };

  const size_t propertiesPos = flashLayout.find("<properties>");
  const size_t propertiesEnd = flashLayout.find("</properties>", propertiesPos);
  if (propertiesPos == std::string::npos || propertiesEnd == std::string::npos)
  {
    preview->warnings.push_back("Loading screen properties missing");
    return;
  }

  size_t itemPos = propertiesPos;
  while ((itemPos = flashLayout.find("<Item>", itemPos)) != std::string::npos && itemPos < propertiesEnd)
  {
    const size_t itemEnd = flashLayout.find("</Item>", itemPos);
    if (itemEnd == std::string::npos)
    {
      break;
    }

    const std::string itemBlock = flashLayout.substr(itemPos, itemEnd - itemPos);
    const std::string propertyName = ExtractTagValue(itemBlock, "propertyName", 0);

    const size_t textrefPos = itemBlock.find("textref=\"");
    std::string textRef;
    if (textrefPos != std::string::npos)
    {
      const size_t valuePos = textrefPos + strlen("textref=\"");
      const size_t valueEnd = itemBlock.find("\"", valuePos);
      if (valueEnd != std::string::npos)
      {
        textRef = itemBlock.substr(valuePos, valueEnd - valuePos);
      }
    }

    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
    {
      if (propertyName != targets[i].name || textRef.empty())
      {
        continue;
      }

      std::string localizedValue;
      if (ExtractLocalizedFileText(localizationXml, NormalizeTextRefForLocalization(textRef), &localizedValue))
      {
        preview->localizedProperties.push_back(
          std::make_pair(std::string(targets[i].label), localizedValue)
        );
      }
    }

    itemPos = itemEnd + strlen("</Item>");
  }
}

void LoadLoadingScreenArtwork(
  const LinuxContentProbe& contentProbe,
  LinuxLoadingScreenPreview* preview,
  LinuxLoadingArtwork* artwork
)
{
  if (contentProbe.localizationRoot.empty())
  {
    preview->warnings.push_back("Localized loading artwork root unavailable");
    return;
  }

  const fs::path candidates[] = {
    contentProbe.localizationRoot / "pw_loading_screen.png",
    contentProbe.localizationRoot / "PvX" / "pw_loading_screen.png",
    contentProbe.localizationRoot / "LoadingBackground.png"
  };

  fs::path artworkPath;
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
  {
    if (fs::exists(candidates[i]) && fs::is_regular_file(candidates[i]))
    {
      artworkPath = candidates[i];
      break;
    }
  }

  if (artworkPath.empty())
  {
    preview->warnings.push_back("Localized loading artwork PNG missing");
    return;
  }

  std::string error;
  if (!LoadPngArtwork(artworkPath, artwork, &error))
  {
    preview->warnings.push_back("Failed to decode loading artwork: " + error);
    return;
  }

  preview->artworkLoaded = true;
  preview->artworkFile = artworkPath.string();
  preview->artworkWidth = static_cast<unsigned long>(artwork->width);
  preview->artworkHeight = static_cast<unsigned long>(artwork->height);
}

std::string NormalizeDataRefPath(std::string path)
{
  while (!path.empty() && (path[0] == '/' || path[0] == '\\'))
  {
    path.erase(path.begin());
  }

  for (size_t i = 0; i < path.size(); ++i)
  {
    if (path[i] == '\\')
    {
      path[i] = '/';
    }
  }

  return path;
}

std::string EnsurePathSuffix(std::string value, const char* suffix)
{
  if (!suffix || !*suffix)
  {
    return value;
  }

  const size_t suffixLength = strlen(suffix);
  if (value.size() >= suffixLength &&
      value.compare(value.size() - suffixLength, suffixLength, suffix) == 0)
  {
    return value;
  }

  return value + suffix;
}

fs::path ResolveDataRefPath(
  const LinuxClientEnvironment& environment,
  const std::string& reference,
  const char* suffix
)
{
  if (reference.empty())
  {
    return fs::path();
  }

  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  if (baseRoot.empty())
  {
    return fs::path();
  }

  return baseRoot / "Data" / EnsurePathSuffix(NormalizeDataRefPath(reference), suffix);
}

std::string CategorizeMapDescriptor(const fs::path& relativePath)
{
  const std::string normalized = NormalizeDataRefPath(relativePath.generic_string());
  if (normalized.find("Maps/Tutorial/") == 0)
  {
    return "Tutorial";
  }
  if (normalized.find("Maps/PvE/") == 0)
  {
    return "PvE";
  }
  if (normalized.find("Maps/Multiplayer/") == 0)
  {
    return "Multiplayer";
  }
  if (normalized.find("Maps/Test/") == 0)
  {
    return "Test";
  }
  return "Other";
}

std::string ToAsciiLower(std::string value)
{
  for (size_t i = 0; i < value.size(); ++i)
  {
    value[i] = static_cast<char>(tolower(static_cast<unsigned char>(value[i])));
  }
  return value;
}

std::string NormalizeLoadingLocaleCode(std::string value)
{
  value = ToAsciiLower(TrimAscii(value));
  std::string normalized;
  normalized.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i)
  {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    if (isalpha(ch))
    {
      normalized.push_back(static_cast<char>(ch));
    }
  }
  return normalized;
}

size_t FindLoadingLocaleIndex(const LinuxLoadingUiPreview& preview, const std::string& locale)
{
  const std::string normalizedTarget = NormalizeLoadingLocaleCode(locale);
  if (normalizedTarget.empty())
  {
    return static_cast<size_t>(-1);
  }

  for (size_t i = 0; i < preview.locales.size(); ++i)
  {
    const std::string normalizedLocale = NormalizeLoadingLocaleCode(preview.locales[i].locale);
    if (normalizedLocale == normalizedTarget)
    {
      return i;
    }
    if (normalizedLocale.size() >= 2 && normalizedTarget.size() >= 2 &&
        normalizedLocale.substr(0, 2) == normalizedTarget.substr(0, 2))
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

size_t FindLoadingStatusIndex(const LinuxLoadingUiPreview& preview, const char* key)
{
  if (!key || !*key)
  {
    return static_cast<size_t>(-1);
  }

  for (size_t i = 0; i < preview.statuses.size(); ++i)
  {
    if (preview.statuses[i].key == key)
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

size_t FindLoadingModeIndex(const LinuxLoadingUiPreview& preview, const char* id)
{
  if (!id || !*id)
  {
    return static_cast<size_t>(-1);
  }

  for (size_t i = 0; i < preview.modes.size(); ++i)
  {
    if (preview.modes[i].id == id)
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

void InitializeLoadingUiState(
  const LinuxContentProbe& contentProbe,
  const LinuxLoadingUiPreview& preview,
  LinuxLoadingUiState* state
)
{
  if (!state)
  {
    return;
  }

  *state = LinuxLoadingUiState();

  if (!preview.statuses.empty())
  {
    const size_t connectingIndex = FindLoadingStatusIndex(preview, "connecting");
    if (connectingIndex != static_cast<size_t>(-1))
    {
      state->statusIndex = connectingIndex;
    }
  }

  if (!preview.modes.empty())
  {
    const size_t preferredMode = FindLoadingModeIndex(preview, "guild");
    if (preferredMode != static_cast<size_t>(-1))
    {
      state->modeIndex = preferredMode;
    }
  }

  if (!preview.locales.empty())
  {
    const size_t localeIndex = FindLoadingLocaleIndex(preview, contentProbe.locale);
    if (localeIndex != static_cast<size_t>(-1))
    {
      state->currentLocaleIndex = localeIndex;
    }

    state->enemyLocaleIndex = state->currentLocaleIndex;
    if (preview.locales.size() > 1)
    {
      state->enemyLocaleIndex = (state->currentLocaleIndex + 1) % preview.locales.size();
    }
  }
}

void DispatchLoadingRuntimeEvent(
  NGameX::LoadingStatusHandler* handler,
  const LinuxLoadingRuntimeEvent& event
)
{
  if (!handler)
  {
    return;
  }

  switch (event.kind)
  {
    case LINUX_LOADING_RUNTIME_LOGIN:
      handler->OnLoginStatus(static_cast<Login::ELoginResult::Enum>(event.code));
      break;

    case LINUX_LOADING_RUNTIME_GAMESTAT:
      handler->OnGameStatStatus(static_cast<Game::EGameStatStatus::Enum>(event.code));
      break;

    case LINUX_LOADING_RUNTIME_LOBBY:
      handler->OnLobbyStatus(static_cast<lobby::EClientStatus::Enum>(event.code));
      break;

    case LINUX_LOADING_RUNTIME_INGAME:
      handler->OnLobbyInGameStatus(static_cast<lobby::EOperationResult::Enum>(event.code));
      break;

    case LINUX_LOADING_RUNTIME_REPLAY:
      handler->OnReplayStatus(static_cast<Game::EReplayStatus::Enum>(event.code));
      break;
  }
}

void SyncLoadingRuntimeState(
  const LinuxLoadingUiPreview& preview,
  LinuxLoadingRuntimeDriver* driver,
  LinuxLoadingUiState* state,
  size_t eventIndex,
  const char* source
)
{
  if (!driver || !driver->ready || !state || driver->events.empty())
  {
    return;
  }

  if (eventIndex >= driver->events.size())
  {
    eventIndex %= driver->events.size();
  }

  state->runtimeEventIndex = eventIndex;
  const LinuxLoadingRuntimeEvent& event = driver->events[eventIndex];
  DispatchLoadingRuntimeEvent(driver->handler, event);

  state->runtimeEvent = event.label ? event.label : "";
  state->runtimeStatusKey = ToStdString(driver->handler->GetLastStatusId());
  state->runtimeStatusText = SanitizeLocalizedText(
    ToStdString(NStr::ToMBCS(driver->flashInterface->GetLoadingStatusText())));
  state->source = source ? source : "runtime";

  if (!state->runtimeStatusKey.empty())
  {
    const size_t runtimeStatusIndex = FindLoadingStatusIndex(preview, state->runtimeStatusKey.c_str());
    if (runtimeStatusIndex != static_cast<size_t>(-1))
    {
      state->statusIndex = runtimeStatusIndex;
      if (runtimeStatusIndex < preview.statuses.size() && !preview.statuses[runtimeStatusIndex].text.empty())
      {
        state->runtimeStatusText = preview.statuses[runtimeStatusIndex].text;
      }
    }
  }
}

void InitializeLoadingRuntimeDriver(
  const LinuxLoadingUiPreview& preview,
  LinuxLoadingRuntimeDriver* driver,
  LinuxLoadingUiState* state
)
{
  if (!driver || !state)
  {
    return;
  }

  *driver = LinuxLoadingRuntimeDriver();

  NDb::Ptr<NDb::DBUIData> uiData = ResolveLoadingUiDataResource();
  if (!uiData)
  {
    driver->warnings.push_back("Loading runtime skipped because UI/Content/_.UIDT.xdb was not resolved");
    return;
  }

  driver->flashInterface = new Game::LoadingFlashInterface(0, "LinuxBootstrapLoading");
  driver->handler = new NGameX::LoadingStatusHandler(uiData);
  driver->handler->SetFlashInterface(driver->flashInterface);

  static const LinuxLoadingRuntimeEvent kRuntimeEvents[] =
  {
    { LINUX_LOADING_RUNTIME_LOGIN, Login::ELoginResult::NoResult, "login: connecting" },
    { LINUX_LOADING_RUNTIME_LOGIN, Login::ELoginResult::NoConnection, "login: no connection" },
    { LINUX_LOADING_RUNTIME_LOGIN, Login::ELoginResult::AccessDenied, "login: access denied" },
    { LINUX_LOADING_RUNTIME_GAMESTAT, Game::EGameStatStatus::Waiting, "gamestat: waiting" },
    { LINUX_LOADING_RUNTIME_GAMESTAT, Game::EGameStatStatus::Failed, "gamestat: failed" },
    { LINUX_LOADING_RUNTIME_LOBBY, lobby::EClientStatus::WaitingEntrance, "lobby: connecting" },
    { LINUX_LOADING_RUNTIME_LOBBY, lobby::EClientStatus::RequestingServerInstance, "lobby: waiting server" },
    { LINUX_LOADING_RUNTIME_LOBBY, lobby::EClientStatus::Connected, "lobby: connected" },
    { LINUX_LOADING_RUNTIME_INGAME, lobby::EOperationResult::InProgress, "ingame: entering" },
    { LINUX_LOADING_RUNTIME_INGAME, lobby::EOperationResult::Ok, "ingame: ready" },
    { LINUX_LOADING_RUNTIME_INGAME, lobby::EOperationResult::RevisionDiffers, "ingame: wrong revision" },
    { LINUX_LOADING_RUNTIME_REPLAY, Game::EReplayStatus::WrongVersion, "replay: wrong version" }
  };

  driver->events.assign(kRuntimeEvents, kRuntimeEvents + ARRAY_SIZE(kRuntimeEvents));
  for (size_t i = 0; i < driver->events.size(); ++i)
  {
    driver->samples.push_back(driver->events[i].label ? driver->events[i].label : "<unnamed>");
  }

  driver->ready = true;
  SyncLoadingRuntimeState(preview, driver, state, 0, "runtime-default");
}

std::string NormalizeMapSelector(std::string value)
{
  value = TrimAscii(value);
  for (size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] == '\\')
    {
      value[i] = '/';
    }
  }

  while (!value.empty() && value[0] == '/')
  {
    value.erase(value.begin());
  }

  const std::string lowerValue = ToAsciiLower(value);
  if (lowerValue.find("data/") == 0)
  {
    value.erase(0, 5);
  }

  return ToAsciiLower(value);
}

size_t CountLiteralOccurrences(const std::string& text, const char* literal)
{
  if (!literal || !*literal)
  {
    return 0;
  }

  const size_t literalLength = strlen(literal);
  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(literal, pos)) != std::string::npos)
  {
    ++count;
    pos += literalLength;
  }

  return count;
}

size_t CountNonEmptyTagValues(const std::string& text, const char* tag)
{
  const std::string openTag = std::string("<") + tag + ">";
  const std::string closeTag = std::string("</") + tag + ">";

  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(openTag, pos)) != std::string::npos)
  {
    const size_t valuePos = pos + openTag.size();
    const size_t closePos = text.find(closeTag, valuePos);
    if (closePos == std::string::npos)
    {
      break;
    }

    if (!TrimAscii(text.substr(valuePos, closePos - valuePos)).empty())
    {
      ++count;
    }

    pos = closePos + closeTag.size();
  }

  return count;
}

int DetectTacticalMarkerTeam(const std::string& href, const std::string& scriptName)
{
  const std::string combined = ToAsciiLower(href + " " + scriptName);
  if (combined.find("teama") != std::string::npos ||
      combined.find("_a.") != std::string::npos ||
      combined.find("_a_") != std::string::npos ||
      combined.find("buildinga") != std::string::npos)
  {
    return 1;
  }

  if (combined.find("teamb") != std::string::npos ||
      combined.find("_b.") != std::string::npos ||
      combined.find("_b_") != std::string::npos ||
      combined.find("buildingb") != std::string::npos)
  {
    return 2;
  }

  return 0;
}

std::string ClassifyTacticalMarker(const std::string& href, const std::string& scriptName)
{
  const std::string lowerHref = ToAsciiLower(href);
  const std::string lowerScript = ToAsciiLower(scriptName);

  if (lowerHref.find(".hplh") != std::string::npos)
  {
    return "hero-spawn";
  }
  if (lowerHref.find(".towr") != std::string::npos)
  {
    return "tower";
  }
  if (lowerHref.find(".cspn") != std::string::npos)
  {
    return "lane-spawner";
  }
  if (lowerHref.find(".ncspn") != std::string::npos)
  {
    if (lowerHref.find("boss") != std::string::npos || lowerScript.find("boss") != std::string::npos)
    {
      return "boss";
    }
    return "neutral-spawner";
  }
  if (lowerHref.find(".shop") != std::string::npos)
  {
    return "shop";
  }
  if (lowerHref.find(".fntn") != std::string::npos)
  {
    return "fountain";
  }
  if (lowerHref.find(".glspn") != std::string::npos)
  {
    return "glyph";
  }
  if (lowerHref.find(".mbld") != std::string::npos || lowerScript.find("mainbuilding") != std::string::npos)
  {
    return "main-building";
  }
  if (lowerHref.find(".mini") != std::string::npos)
  {
    return "minigame";
  }
  if (lowerHref.find(".flag") != std::string::npos)
  {
    return "flag";
  }

  return "";
}

std::string DescribeTacticalMarker(const std::string& kind, const std::string& href, const std::string& scriptName)
{
  if (!scriptName.empty())
  {
    return scriptName;
  }

  if (!href.empty())
  {
    return fs::path(NormalizeDataRefPath(href)).stem().string();
  }

  return kind;
}

void ProbeTacticalMapPreview(const std::string& mapXml, LinuxTacticalMapPreview* preview)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxTacticalMapPreview();

  const size_t objectsPos = mapXml.find("<objects>");
  const size_t objectsEnd = mapXml.find("</objects>", objectsPos);
  if (objectsPos == std::string::npos || objectsEnd == std::string::npos)
  {
    preview->warnings.push_back("Map tactical preview missing objects block");
    return;
  }

  const std::string objectsBlock = mapXml.substr(objectsPos, objectsEnd - objectsPos);
  const std::vector<std::string> items = ExtractItemBlocks(objectsBlock);
  bool boundsInitialized = false;

  for (size_t i = 0; i < items.size(); ++i)
  {
    const std::string& item = items[i];
    const std::string objectRef = ExtractTagHref(item, "gameObject", 0);
    const std::string scriptName = TrimAscii(ExtractTagValue(item, "scriptName", 0));
    const std::string kind = ClassifyTacticalMarker(objectRef, scriptName);
    if (kind.empty())
    {
      continue;
    }

    const float translateX = static_cast<float>(atof(ExtractTagValue(item, "translateX", 0).c_str()));
    const float translateY = static_cast<float>(atof(ExtractTagValue(item, "translateY", 0).c_str()));
    const int team = DetectTacticalMarkerTeam(objectRef, scriptName);

    if (kind == "tower")
    {
      ++preview->towerCount;
    }
    else if (kind == "hero-spawn")
    {
      ++preview->heroSpawnCount;
    }
    else if (kind == "lane-spawner")
    {
      ++preview->laneSpawnerCount;
    }
    else if (kind == "neutral-spawner")
    {
      ++preview->neutralSpawnerCount;
    }
    else if (kind == "boss")
    {
      ++preview->bossCount;
    }
    else if (kind == "shop")
    {
      ++preview->shopCount;
    }
    else if (kind == "fountain")
    {
      ++preview->fountainCount;
    }
    else if (kind == "glyph")
    {
      ++preview->glyphCount;
    }
    else if (kind == "main-building")
    {
      ++preview->mainBuildingCount;
    }
    else if (kind == "minigame")
    {
      ++preview->minigameCount;
    }
    else if (kind == "flag")
    {
      ++preview->flagCount;
    }

    if (kind == "flag")
    {
      continue;
    }

    LinuxTacticalMapMarker marker;
    marker.kind = kind;
    marker.objectRef = objectRef;
    marker.label = DescribeTacticalMarker(kind, objectRef, scriptName);
    marker.scriptName = scriptName;
    marker.translateX = translateX;
    marker.translateY = translateY;
    marker.team = team;
    preview->markers.push_back(marker);

    if (!boundsInitialized)
    {
      preview->minX = preview->maxX = translateX;
      preview->minY = preview->maxY = translateY;
      boundsInitialized = true;
    }
    else
    {
      preview->minX = std::min(preview->minX, translateX);
      preview->maxX = std::max(preview->maxX, translateX);
      preview->minY = std::min(preview->minY, translateY);
      preview->maxY = std::max(preview->maxY, translateY);
    }
  }

  preview->ready = !preview->markers.empty();
  if (!preview->ready)
  {
    preview->warnings.push_back("No tactical markers recognized in map objects");
    return;
  }

  if (preview->maxX - preview->minX < 1.0f || preview->maxY - preview->minY < 1.0f)
  {
    preview->warnings.push_back("Tactical marker bounds are degenerate");
  }
}

bool ParseMapCatalogEntry(
  const fs::path& dataRoot,
  const fs::path& descriptorPath,
  LinuxMapCatalogEntry* entry
)
{
  std::string xml;
  if (!ReadTextFile(descriptorPath, &xml))
  {
    return false;
  }

  const fs::path relativePath = fs::relative(descriptorPath, dataRoot);
  entry->descriptor = NormalizeDataRefPath(relativePath.generic_string());
  entry->mapType = ExtractTagValue(xml, "mapType", 0);
  entry->teamSize = atoi(ExtractTagValue(xml, "teamSize", 0).c_str());
  entry->productionMode = ExtractTagValue(xml, "productionMode", 0) == "true";
  entry->category = CategorizeMapDescriptor(relativePath);
  entry->mapRef = ExtractTagHref(xml, "map", 0);
  entry->mapSettingsRef = ExtractTagHref(xml, "mapSettings", 0);
  entry->scoringTableRef = ExtractTagHref(xml, "scoringTable", 0);
  entry->imageRef = ExtractTagHref(xml, "image", 0);
  entry->firstWinVisualInfoRef = ExtractTagHref(xml, "FirstWinVisualInfo", 0);

  const size_t titlePos = xml.find("<title ");
  if (titlePos != std::string::npos)
  {
    ReadTextRefFromDataRoot(dataRoot, ExtractTagTextRef(xml, "title", titlePos), &entry->title);
  }

  const size_t descriptionPos = xml.find("<description ");
  if (descriptionPos != std::string::npos)
  {
    ReadTextRefFromDataRoot(dataRoot, ExtractTagTextRef(xml, "description", descriptionPos), &entry->description);
  }

  const size_t loadingBackgroundPos = xml.find("<loadingBackgroundImages>");
  if (loadingBackgroundPos != std::string::npos)
  {
    entry->loadingBackRef = ExtractTagHref(xml, "back", loadingBackgroundPos);
    entry->loadingLogoRef = ExtractTagHref(xml, "logo", loadingBackgroundPos);
  }

  if (entry->title.empty())
  {
    entry->title = descriptorPath.stem().string();
  }

  return true;
}

void ProbeMapCatalog(const LinuxClientEnvironment& environment, LinuxMapCatalog* catalog)
{
  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  if (baseRoot.empty())
  {
    catalog->warnings.push_back("Map catalog base root unavailable");
    return;
  }

  const fs::path dataRoot = baseRoot / "Data";
  const fs::path mapsRoot = dataRoot / "Maps";
  if (!fs::exists(mapsRoot) || !fs::is_directory(mapsRoot))
  {
    catalog->warnings.push_back("Data/Maps is missing");
    return;
  }

  std::vector<LinuxMapCatalogEntry> entries;
  for (fs::recursive_directory_iterator it(mapsRoot), end; it != end; ++it)
  {
    if (!it->is_regular_file())
    {
      continue;
    }

    const fs::path path = it->path();
    if (path.extension() != ".xdb" || path.filename().string().find(".ADMPDSCR.xdb") == std::string::npos)
    {
      continue;
    }

    LinuxMapCatalogEntry entry;
    if (!ParseMapCatalogEntry(dataRoot, path, &entry))
    {
      catalog->warnings.push_back("Failed to parse " + path.filename().string());
      continue;
    }

    ++catalog->descriptorCount;
    if (entry.productionMode)
    {
      ++catalog->productionDescriptorCount;
    }
    if (entry.category == "Tutorial")
    {
      ++catalog->tutorialCount;
    }
    if (entry.category == "PvE")
    {
      ++catalog->pveCount;
    }
    if (entry.mapType == "PvP")
    {
      ++catalog->pvpCount;
    }

    entries.push_back(entry);
  }

  std::sort(entries.begin(), entries.end(), [](const LinuxMapCatalogEntry& left, const LinuxMapCatalogEntry& right) {
    if (left.category != right.category)
    {
      return left.category < right.category;
    }
    return left.title < right.title;
  });

  catalog->entries = entries;

  const size_t maxFeaturedEntries = 6;
  for (size_t i = 0; i < entries.size() && i < maxFeaturedEntries; ++i)
  {
    catalog->featuredEntries.push_back(entries[i]);
  }
}

bool ReadTextRefFromDataRoot(const fs::path& dataRoot, const std::string& textRef, std::string* value)
{
  if (textRef.empty())
  {
    return false;
  }

  const std::string rootFileName = NormalizeRootFileSystemPath(textRef);
  SFileInfo info;
  if (RootFileSystem::GetFileInfo(&info, rootFileName.c_str()) && IsValid(info.pOwner) &&
      ReadRootFileUtf16Text(rootFileName.c_str(), value))
  {
    return true;
  }

  const fs::path textPath = dataRoot / NormalizeDataRefPath(textRef);
  return ReadUtf16TextFile(textPath, value);
}

bool ParseHeroCatalogEntry(
  const fs::path& dataRoot,
  const fs::path& heroRoot,
  LinuxHeroCatalogEntry* entry
)
{
  if (!entry)
  {
    return false;
  }

  const fs::path heroFile = heroRoot / "_.HROB.xdb";
  std::string xml;
  if (!ReadTextFile(heroFile, &xml))
  {
    return false;
  }

  *entry = LinuxHeroCatalogEntry();
  entry->id = heroRoot.filename().string();
  entry->persistentId = ExtractTagValue(xml, "persistentId", 0);
  entry->gender = ExtractTagValue(xml, "gender", 0);
  entry->iconRef = ExtractTagHref(xml, "heroImageA", 0);
  if (entry->iconRef.empty())
  {
    entry->iconRef = ExtractTagHref(xml, "heroImageB", 0);
  }

  const std::string heroNameARef = ExtractTagTextRef(xml, "heroNameA", 0);
  const std::string heroNameBRef = ExtractTagTextRef(xml, "heroNameB", 0);
  const std::string heroDescriptionARef = ExtractTagTextRef(xml, "heroDescriptionA", 0);
  const std::string heroDescriptionBRef = ExtractTagTextRef(xml, "heroDescriptionB", 0);

  ReadTextRefFromDataRoot(dataRoot, heroNameARef, &entry->title);
  ReadTextRefFromDataRoot(dataRoot, heroNameBRef, &entry->alternateTitle);
  ReadTextRefFromDataRoot(dataRoot, heroDescriptionARef, &entry->description);
  ReadTextRefFromDataRoot(dataRoot, heroDescriptionBRef, &entry->alternateDescription);

  if (entry->title.empty())
  {
    entry->title = !entry->alternateTitle.empty() ? entry->alternateTitle :
      (!entry->persistentId.empty() ? entry->persistentId : entry->id);
  }

  if (entry->description.empty())
  {
    entry->description = entry->alternateDescription;
  }

  entry->legal = ExtractTagValue(xml, "legal", 0) == "true";

  std::set<std::string> uniqueSkinNames;
  std::error_code error;
  for (fs::directory_iterator it(heroRoot, error); !error && it != fs::directory_iterator(); ++it)
  {
    if (!it->is_directory())
    {
      continue;
    }

    size_t directorySkinCount = 0;
    std::error_code nestedError;
    for (fs::directory_iterator nested(it->path(), nestedError); !nestedError && nested != fs::directory_iterator(); ++nested)
    {
      if (!nested->is_regular_file())
      {
        continue;
      }

      const std::string fileName = nested->path().filename().string();
      if (fileName.find("_heroName.txt") == std::string::npos)
      {
        continue;
      }

      ++directorySkinCount;
      std::string skinName;
      if (ReadUtf16TextFile(nested->path(), &skinName) && !skinName.empty())
      {
        uniqueSkinNames.insert(skinName);
      }
    }

    if (directorySkinCount > 0)
    {
      ++entry->skinCount;
    }
  }

  for (std::set<std::string>::const_iterator it = uniqueSkinNames.begin(); it != uniqueSkinNames.end(); ++it)
  {
    if (entry->featuredSkinNames.size() >= 3)
    {
      break;
    }
    entry->featuredSkinNames.push_back(*it);
  }

  return true;
}

void ProbeHeroCatalog(const LinuxClientEnvironment& environment, LinuxHeroCatalog* catalog)
{
  if (!catalog)
  {
    return;
  }

  catalog->entries.clear();
  catalog->warnings.clear();

  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  const fs::path heroesRoot = baseRoot / "Data" / "Heroes";
  if (!fs::exists(heroesRoot) || !fs::is_directory(heroesRoot))
  {
    catalog->warnings.push_back("Hero catalog root missing");
    return;
  }

  std::error_code error;
  for (fs::directory_iterator it(heroesRoot, error); !error && it != fs::directory_iterator(); ++it)
  {
    if (!it->is_directory())
    {
      continue;
    }

    const fs::path heroRoot = it->path();
    if (!fs::exists(heroRoot / "_.HROB.xdb"))
    {
      continue;
    }

    LinuxHeroCatalogEntry entry;
    if (!ParseHeroCatalogEntry(baseRoot / "Data", heroRoot, &entry))
    {
      catalog->warnings.push_back("Failed to parse hero " + heroRoot.filename().string());
      continue;
    }

    if (entry.legal)
    {
      catalog->entries.push_back(entry);
    }
  }

  std::sort(catalog->entries.begin(), catalog->entries.end(), [](const LinuxHeroCatalogEntry& left, const LinuxHeroCatalogEntry& right) {
    return left.title < right.title;
  });
}

size_t FindHeroCatalogIndex(const LinuxHeroCatalog& catalog, const std::string& selector)
{
  const std::string normalizedSelector = ToAsciiLower(TrimAscii(selector));
  if (normalizedSelector.empty())
  {
    return static_cast<size_t>(-1);
  }

  for (size_t i = 0; i < catalog.entries.size(); ++i)
  {
    const LinuxHeroCatalogEntry& entry = catalog.entries[i];
    if (ToAsciiLower(entry.id) == normalizedSelector ||
        ToAsciiLower(entry.persistentId) == normalizedSelector ||
        ToAsciiLower(entry.title) == normalizedSelector ||
        ToAsciiLower(entry.alternateTitle) == normalizedSelector)
    {
      return i;
    }
  }

  for (size_t i = 0; i < catalog.entries.size(); ++i)
  {
    const LinuxHeroCatalogEntry& entry = catalog.entries[i];
    if (ToAsciiLower(entry.id).find(normalizedSelector) != std::string::npos ||
        ToAsciiLower(entry.persistentId).find(normalizedSelector) != std::string::npos ||
        ToAsciiLower(entry.title).find(normalizedSelector) != std::string::npos ||
        ToAsciiLower(entry.alternateTitle).find(normalizedSelector) != std::string::npos)
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

std::string ResolveHeroCatalogId(const LinuxHeroCatalogEntry& entry)
{
  return entry.persistentId.empty() ? entry.id : entry.persistentId;
}

size_t FindHeroCatalogIndexByChecksum(const LinuxHeroCatalog& catalog, unsigned int heroChecksum)
{
  if (heroChecksum == 0)
  {
    return static_cast<size_t>(-1);
  }

  for (size_t i = 0; i < catalog.entries.size(); ++i)
  {
    const std::string heroId = ResolveHeroCatalogId(catalog.entries[i]);
    if (!heroId.empty() && Crc32Checksum().AddString(heroId.c_str()).Get() == heroChecksum)
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

size_t ResolveSelectedHeroCatalogIndex(
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview
)
{
  size_t selectedHeroIndex = localMatchPreview.selectedHeroIndex;
  if (localMatchPreview.ready && localMatchPreview.selectedSlotIndex < localMatchPreview.lineup.size())
  {
    const size_t slotHeroIndex = localMatchPreview.lineup[localMatchPreview.selectedSlotIndex].heroIndex;
    if (slotHeroIndex < heroCatalog.entries.size())
    {
      selectedHeroIndex = slotHeroIndex;
    }
  }

  return selectedHeroIndex < heroCatalog.entries.size() ? selectedHeroIndex : static_cast<size_t>(-1);
}

int ConvertOverlayTeamToDisplayTeam(int team)
{
  return team == 1 || team == 2 ? team : 0;
}

int ConvertCoreTeamToDisplayTeam(NCore::ETeam::Enum team)
{
  switch (team)
  {
    case NCore::ETeam::Team1:
      return 1;

    case NCore::ETeam::Team2:
      return 2;

    default:
      return 0;
  }
}

NCore::ETeam::Enum ConvertDisplayTeamToCoreTeam(int team)
{
  switch (team)
  {
    case 1:
      return NCore::ETeam::Team1;

    case 2:
      return NCore::ETeam::Team2;

    default:
      return static_cast<NCore::ETeam::Enum>(0);
  }
}

int ConvertDbTeamToDisplayTeam(NDb::ETeamID team)
{
  switch (team)
  {
    case NDb::TEAMID_A:
      return 1;

    case NDb::TEAMID_B:
      return 2;

    default:
      return 0;
  }
}

lobby::ETeam::Enum ConvertDisplayTeamToLobbyTeam(int team)
{
  switch (team)
  {
    case 1:
      return lobby::ETeam::Team1;

    case 2:
      return lobby::ETeam::Team2;

    default:
      return static_cast<lobby::ETeam::Enum>(-1);
  }
}

wstring BuildLinuxPreviewNickname(const LinuxSessionPreview& sessionPreview, bool human, size_t botIndex)
{
  if (human && !sessionPreview.currentNickname.empty())
  {
    return NStr::ToUnicode(string(sessionPreview.currentNickname.c_str()));
  }

  if (human)
  {
    return L"Linux Player";
  }

  return NStr::StrFmtW(L"Bot %lu", static_cast<unsigned long>(botIndex + 1));
}

const LinuxEngineMapStartSlot* FindEngineMapStartSlotByUserId(
  const LinuxEngineMapStartPreview& preview,
  int userId
)
{
  for (size_t i = 0; i < preview.slots.size(); ++i)
  {
    if (preview.slots[i].filled && preview.slots[i].userId == userId)
    {
      return &preview.slots[i];
    }
  }

  return 0;
}

void ResolveLoadingFlagPresentation(
  const NDb::DBUIData* uiData,
  const std::string& flagId,
  string* flagIcon,
  wstring* flagTooltip
)
{
  if (flagIcon)
  {
    flagIcon->clear();
  }
  if (flagTooltip)
  {
    flagTooltip->clear();
  }

  if (!uiData)
  {
    return;
  }

  if (!flagId.empty())
  {
    for (int i = 0; i < uiData->countryFlags.size(); ++i)
    {
      if (uiData->countryFlags[i].id == flagId.c_str())
      {
        if (flagIcon && uiData->countryFlags[i].icon)
        {
          *flagIcon = uiData->countryFlags[i].icon->textureFileName;
        }
        if (flagTooltip)
        {
          *flagTooltip = uiData->countryFlags[i].tooltip.GetText();
        }
        return;
      }
    }

    for (int i = 0; i < uiData->customFlags.size(); ++i)
    {
      const NDb::Ptr<NDb::CustomFlag>& customFlag = uiData->customFlags[i];
      if (customFlag && customFlag->id == flagId.c_str())
      {
        if (flagIcon && customFlag->icon)
        {
          *flagIcon = customFlag->icon->textureFileName;
        }
        if (flagTooltip)
        {
          *flagTooltip = customFlag->tooltip.GetText();
        }
        return;
      }
    }

    for (int i = 0; i < uiData->adminFlags.size(); ++i)
    {
      const NDb::Ptr<NDb::CustomFlag>& adminFlag = uiData->adminFlags[i];
      if (adminFlag && adminFlag->id == flagId.c_str())
      {
        if (flagIcon && adminFlag->icon)
        {
          *flagIcon = adminFlag->icon->textureFileName;
        }
        if (flagTooltip)
        {
          *flagTooltip = adminFlag->tooltip.GetText();
        }
        return;
      }
    }
  }

  if (!uiData->countryFlags.empty())
  {
    if (flagIcon && uiData->countryFlags[0].icon)
    {
      *flagIcon = uiData->countryFlags[0].icon->textureFileName;
    }
    if (flagTooltip)
    {
      *flagTooltip = uiData->countryFlags[0].tooltip.GetText();
    }
  }
}

void ResolveLoadingBotFlagPresentation(
  const NDb::DBUIData* uiData,
  NCore::ETeam::Enum team,
  string* flagIcon,
  wstring* flagTooltip
)
{
  if (flagIcon)
  {
    flagIcon->clear();
  }
  if (flagTooltip)
  {
    flagTooltip->clear();
  }

  if (!uiData)
  {
    return;
  }

  const bool freeze = team == NCore::ETeam::Team1;
  const NDb::CountryFlag& botFlag = freeze ? uiData->botFlags.doctDefaultFlag : uiData->botFlags.adornianDefaultFlag;
  if (flagIcon && botFlag.icon)
  {
    *flagIcon = botFlag.icon->textureFileName;
  }
  if (flagTooltip)
  {
    *flagTooltip = botFlag.tooltip.GetText();
  }
}

void ProbeLoadingHeroesRuntimePreview(
  const LinuxSessionPreview& sessionPreview,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  LinuxLoadingHeroesRuntimePreview* preview
)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxLoadingHeroesRuntimePreview();

  if (!engineMapStartPreview.ready || engineMapStartPreview.slots.empty())
  {
    preview->warnings.push_back("Loading heroes runtime skipped because engine map start preview is unavailable");
    return;
  }

  NDb::Ptr<NDb::SessionRoot> sessionRoot = NDb::SessionRoot::GetRoot();
  if (!IsValid(sessionRoot) || !IsValid(sessionRoot->logicRoot) || !IsValid(sessionRoot->logicRoot->heroes))
  {
    preview->warnings.push_back("Loading heroes runtime skipped because SessionRoot heroes DB is unavailable");
    return;
  }

  NDb::Ptr<NDb::AdvMapDescription> advMapDescription;
  if (!selectedMapPreview.descriptor.empty())
  {
    advMapDescription = NDb::Get<NDb::AdvMapDescription>(NDb::DBID(selectedMapPreview.descriptor.c_str()));
  }

  Strong<Game::LoadingFlashInterface> flashInterface = new Game::LoadingFlashInterface(0, "LinuxBootstrapLoadingHeroes");
  Strong<Game::LoadingHeroes> loadingHeroes = new Game::LoadingHeroes(flashInterface, sessionRoot->logicRoot->heroes);
  NDb::Ptr<NDb::DBUIData> loadingUiData = ResolveLoadingUiDataResource();
  std::map<int, const LinuxEngineMapStartSlot*> runtimeSlots;
  if (IsValid(advMapDescription))
  {
    loadingHeroes->SetMapDescription(advMapDescription);
  }
  else
  {
    preview->warnings.push_back("Loading heroes runtime could not resolve selected map descriptor");
  }

  bool ourHeroAssigned = false;
  size_t botIndex = 0;
  for (size_t i = 0; i < engineMapStartPreview.slots.size(); ++i)
  {
    const LinuxEngineMapStartSlot& slot = engineMapStartPreview.slots[i];
    if (!slot.filled)
    {
      continue;
    }

    const int runtimeUserId =
      slot.userId != -1 ? slot.userId :
      (slot.playerId != -1 ? slot.playerId : -1000 - static_cast<int>(i));
    const NCore::ETeam::Enum team = ConvertDisplayTeamToCoreTeam(slot.team);
    const NCore::ETeam::Enum originalTeam = ConvertDisplayTeamToCoreTeam(slot.originalTeam ? slot.originalTeam : slot.team);

    if (slot.human && !ourHeroAssigned)
    {
      loadingHeroes->SetOurUserId(runtimeUserId, team, originalTeam);
      ourHeroAssigned = true;
    }

    runtimeSlots[runtimeUserId] = &slot;

    Game::HeroInfo heroInfo;
    const string heroSkin(slot.heroSkin.c_str());
    string flagIcon;
    wstring flagTooltip;
    heroInfo.isBot = !slot.human;
    heroInfo.partyId = 0;
    heroInfo.team = team;
    heroInfo.originalTeam = originalTeam;
    heroInfo.skinId = heroSkin;
    heroInfo.heroId = slot.heroChecksum;
    heroInfo.userId = runtimeUserId;
    heroInfo.isAnimatedAvatar = slot.isAnimatedAvatar;
    heroInfo.exp = slot.heroExp;
    heroInfo.raiting = slot.heroRating;
    heroInfo.isNovice = slot.isNovice;
    heroInfo.isPremium = slot.hasPremium;
    heroInfo.partyId = slot.partyId;
    heroInfo.locale = string(slot.locale.c_str());
    heroInfo.leagueIndex = slot.leagueIndex;
    heroInfo.ownLeaguePlace = slot.ownLeaguePlace;
    for (size_t leaguePlaceIndex = 0; leaguePlaceIndex < slot.leaguePlaces.size(); ++leaguePlaceIndex)
    {
      heroInfo.leaguePlaces.push_back(slot.leaguePlaces[leaguePlaceIndex]);
    }

    if (slot.human)
    {
      ResolveLoadingFlagPresentation(loadingUiData, slot.flagId, &flagIcon, &flagTooltip);
    }
    else
    {
      ResolveLoadingBotFlagPresentation(loadingUiData, team, &flagIcon, &flagTooltip);
    }

    wstring playerName =
      !slot.nickname.empty() ?
        NStr::ToUnicode(string(slot.nickname.c_str())) :
        BuildLinuxPreviewNickname(sessionPreview, slot.human, botIndex);
    loadingHeroes->AddUser(
      runtimeUserId,
      playerName,
      true,
      team,
      originalTeam,
      heroInfo,
      flagIcon,
      flagTooltip,
      heroSkin,
      0);

    if (!slot.human)
    {
      loadingHeroes->AddBot(runtimeUserId);
      ++botIndex;
    }

    const float progress = slot.human ? 0.35f : std::min(0.95f, 0.45f + 0.08f * static_cast<float>(botIndex));
    loadingHeroes->SetPlayerProgress(runtimeUserId, progress);
    flashInterface->SetHeroLevel(runtimeUserId, slot.heroLevel);
    flashInterface->SetHeroPremium(runtimeUserId, slot.hasPremium, Game::ConvertToFaction(originalTeam));
    flashInterface->SetHeroRaiting(runtimeUserId, slot.heroRating, 0.0f, 0.0f, slot.isNovice, "", L"");
  }

  preview->ourHeroId = flashInterface->GetOurHeroId();
  preview->spectatorMode = flashInterface->IsSpectatorMode();

  const vector<Game::LoadingFlashHeroState>& capturedHeroes = flashInterface->GetHeroes();
  for (size_t i = 0; i < capturedHeroes.size(); ++i)
  {
    const Game::LoadingFlashHeroState& captured = capturedHeroes[i];
    LinuxLoadingRuntimeHeroEntry entry;
    entry.slotId = captured.slotId;
    entry.progress = captured.loadProgress / 100.0f;
    entry.leftGame = captured.isLeftGame;
    entry.hasPremium = captured.hasPremium;
    entry.isNovice = captured.isNovice;
    entry.heroLevel = captured.heroLevel;
    entry.force = captured.force;
    entry.rating = captured.rating;
    entry.ratingAcc = captured.ratingAcc;
    entry.leagueIndex = captured.leagueIndex;
    entry.partyId = captured.partyId;
    entry.flagIcon = ToStdString(captured.flagIcon);
    entry.playerName = ToStdString(NStr::ToMBCS(captured.playerName));
    entry.iconPath = ToStdString(captured.iconPath);
    entry.classIcon = ToStdString(captured.classIcon);

    std::map<int, const LinuxEngineMapStartSlot*>::const_iterator runtimeSlot = runtimeSlots.find(captured.slotId);
    if (runtimeSlot != runtimeSlots.end() && runtimeSlot->second)
    {
      entry.team = runtimeSlot->second->team;
      entry.human = runtimeSlot->second->human;
      entry.heroTitle = runtimeSlot->second->heroTitle;
      entry.locale = runtimeSlot->second->locale;
      entry.flagId = runtimeSlot->second->flagId;
    }

    if (entry.human)
    {
      ++preview->humanCount;
    }
    else
    {
      ++preview->botCount;
    }
    if (entry.leftGame)
    {
      ++preview->disconnectedCount;
    }
    if (entry.hasPremium)
    {
      ++preview->premiumCount;
    }
    if (entry.isNovice)
    {
      ++preview->noviceCount;
    }
    if (!entry.locale.empty())
    {
      ++preview->localeCount;
    }
    if (!entry.flagIcon.empty() || !entry.flagId.empty())
    {
      ++preview->flaggedCount;
    }
    if (entry.rating != 0 || entry.ratingAcc != 0)
    {
      ++preview->ratedCount;
    }

    preview->heroes.push_back(entry);

    std::string sample =
      (entry.playerName.empty() ? std::string("<anon>") : entry.playerName) + " / " +
      (entry.heroTitle.empty() ? std::string("<hero unresolved>") : entry.heroTitle) + " / " +
      NStr::StrFmt("%d%%", static_cast<int>(entry.progress * 100.0f + 0.5f));
    if (entry.leftGame)
    {
      sample += " / left";
    }
    AppendSampleValue(&preview->samples, sample, 6);

    std::string metaSample = entry.playerName.empty() ? std::string("<anon>") : entry.playerName;
    if (!entry.locale.empty())
    {
      metaSample += " / " + entry.locale;
    }
    if (entry.partyId != 0)
    {
      metaSample += NStr::StrFmt(" / party=%u", entry.partyId);
    }
    if (entry.heroLevel > 0)
    {
      metaSample += NStr::StrFmt(" / lvl=%d", entry.heroLevel);
    }
    if (entry.rating != 0)
    {
      metaSample += NStr::StrFmt(" / rating=%d", entry.rating);
    }
    if (entry.leagueIndex != 0)
    {
      metaSample += NStr::StrFmt(" / league=%d", entry.leagueIndex);
    }
    if (!entry.flagId.empty())
    {
      metaSample += " / flag=" + entry.flagId;
    }
    if (entry.hasPremium)
    {
      metaSample += " / premium";
    }
    if (entry.isNovice)
    {
      metaSample += " / novice";
    }
    AppendSampleValue(&preview->metaSamples, metaSample, 6);
  }

  preview->ready = !preview->heroes.empty();
  if (!preview->ready)
  {
    preview->warnings.push_back("Loading heroes runtime produced no hero slots");
  }
}

const char* DescribeArtworkMode(int mode)
{
  switch (mode)
  {
    case LINUX_ARTWORK_AUTO:
      return "auto";

    case LINUX_ARTWORK_LOADING:
      return "loading";

    case LINUX_ARTWORK_MAP_BACK:
      return "map-back";

    case LINUX_ARTWORK_MAP_BACK_WITH_LOGO:
      return "map-back+logo";

    case LINUX_ARTWORK_MAP_LOGO:
      return "map-logo";

    case LINUX_ARTWORK_MINIMAP_FIRST:
      return "minimap-first";

    case LINUX_ARTWORK_MINIMAP_SECOND:
      return "minimap-second";

    case LINUX_ARTWORK_MINIMAP_NEUTRAL:
      return "minimap-neutral";

    default:
      return "unknown";
  }
}

void StepArtworkMode(LinuxArtworkSelectionState* state, int delta, const char* source)
{
  if (!state || delta == 0)
  {
    return;
  }

  int nextMode = state->mode + delta;
  while (nextMode < 0)
  {
    nextMode += LINUX_ARTWORK_COUNT;
  }
  while (nextMode >= LINUX_ARTWORK_COUNT)
  {
    nextMode -= LINUX_ARTWORK_COUNT;
  }

  if (nextMode != state->mode)
  {
    state->mode = nextMode;
    ++state->changeCount;
    state->source = source ? source : "runtime";
  }
}

size_t SelectNextHeroIndex(
  const LinuxHeroCatalog& catalog,
  const std::set<size_t>& usedIndices,
  size_t startIndex
)
{
  if (catalog.entries.empty())
  {
    return static_cast<size_t>(-1);
  }

  for (size_t offset = 0; offset < catalog.entries.size(); ++offset)
  {
    const size_t index = (startIndex + offset) % catalog.entries.size();
    if (usedIndices.find(index) == usedIndices.end())
    {
      return index;
    }
  }

  return startIndex % catalog.entries.size();
}

size_t ResolveSelectedMapMaxTeamSize(
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState
)
{
  if (mapCatalog.entries.empty() || mapBrowserState.selectedIndex >= mapCatalog.entries.size())
  {
    return 1;
  }

  const LinuxMapCatalogEntry& selectedMap = mapCatalog.entries[mapBrowserState.selectedIndex];
  return selectedMap.teamSize > 0 ? static_cast<size_t>(selectedMap.teamSize) : 1;
}

void ResizeLocalMatchOverrides(LinuxLocalMatchPreview* preview, size_t totalSlots)
{
  if (!preview)
  {
    return;
  }

  preview->slotHeroOverrides.resize(totalSlots, static_cast<size_t>(-1));
  if (preview->selectedSlotIndex >= totalSlots)
  {
    preview->selectedSlotIndex = totalSlots > 0 ? totalSlots - 1 : 0;
  }
}

size_t ResolveHumanSlotIndex(const LinuxLocalMatchPreview& preview)
{
  if (preview.teamSize == 0)
  {
    return 0;
  }

  return preview.humanTeam == 1 ? 0 : preview.teamSize;
}

bool IsHumanSlotIndex(const LinuxLocalMatchPreview& preview, size_t slotIndex)
{
  return preview.teamSize > 0 && slotIndex == ResolveHumanSlotIndex(preview);
}

size_t CountManualHeroOverrides(const LinuxLocalMatchPreview& preview)
{
  size_t count = 0;
  for (size_t i = 0; i < preview.slotHeroOverrides.size(); ++i)
  {
    if (preview.slotHeroOverrides[i] != static_cast<size_t>(-1))
    {
      ++count;
    }
  }
  return count;
}

void RegenerateLocalMatchPreview(
  const LinuxHeroCatalog& heroCatalog,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  LinuxLocalMatchPreview* preview,
  const char* source
)
{
  if (!preview)
  {
    return;
  }

  preview->ready = false;
  preview->lineup.clear();
  preview->warnings.clear();
  preview->teamSize = 0;

  if (heroCatalog.entries.empty())
  {
    preview->warnings.push_back("No legal heroes found in Data/Heroes");
    return;
  }

  if (mapCatalog.entries.empty() || mapBrowserState.selectedIndex >= mapCatalog.entries.size())
  {
    preview->warnings.push_back("Map selection unavailable");
    return;
  }

  if (preview->selectedHeroIndex >= heroCatalog.entries.size())
  {
    preview->selectedHeroIndex = 0;
  }

  const LinuxMapCatalogEntry& selectedMap = mapCatalog.entries[mapBrowserState.selectedIndex];
  const size_t maxTeamSize = selectedMap.teamSize > 0 ? static_cast<size_t>(selectedMap.teamSize) : 1;
  if (preview->requestedTeamSize == 0)
  {
    preview->requestedTeamSize = maxTeamSize;
  }
  if (preview->requestedTeamSize > maxTeamSize)
  {
    preview->requestedTeamSize = maxTeamSize;
  }

  preview->teamSize = std::max(preview->requestedTeamSize, static_cast<size_t>(1));
  const size_t totalSlots = preview->teamSize * 2;
  ResizeLocalMatchOverrides(preview, totalSlots);

  std::set<size_t> usedIndices;
  usedIndices.insert(preview->selectedHeroIndex);

  size_t nextHeroSeed = (preview->selectedHeroIndex + 1 + preview->shuffleOffset) % heroCatalog.entries.size();
  for (size_t flatSlotIndex = 0; flatSlotIndex < totalSlots; ++flatSlotIndex)
  {
    LinuxLocalMatchSlot slot;
    slot.team = flatSlotIndex < preview->teamSize ? 1 : 2;
    slot.human = IsHumanSlotIndex(*preview, flatSlotIndex);

    if (slot.human)
    {
      slot.heroIndex = preview->selectedHeroIndex;
    }
    else if (flatSlotIndex < preview->slotHeroOverrides.size() &&
             preview->slotHeroOverrides[flatSlotIndex] != static_cast<size_t>(-1) &&
             preview->slotHeroOverrides[flatSlotIndex] < heroCatalog.entries.size())
    {
      slot.heroIndex = preview->slotHeroOverrides[flatSlotIndex];
      slot.manualHero = true;
    }
    else
    {
      slot.heroIndex = SelectNextHeroIndex(heroCatalog, usedIndices, nextHeroSeed);
      nextHeroSeed = (slot.heroIndex + 1) % heroCatalog.entries.size();
    }

    if (slot.heroIndex != static_cast<size_t>(-1) && slot.heroIndex < heroCatalog.entries.size())
    {
      usedIndices.insert(slot.heroIndex);
      const LinuxHeroCatalogEntry& hero = heroCatalog.entries[slot.heroIndex];
      slot.heroId = hero.persistentId.empty() ? hero.id : hero.persistentId;
      slot.heroTitle = hero.title;
    }
    else
    {
      slot.heroId = "<none>";
      slot.heroTitle = "<none>";
    }

    preview->lineup.push_back(slot);
  }

  preview->generationSource = source ? source : "runtime";
  ++preview->generationCount;
  preview->ready = !preview->lineup.empty();
}

void InitializeLocalMatchPreview(
  const LinuxHeroCatalog& heroCatalog,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  LinuxLocalMatchPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  preview->selectedHeroIndex = 0;
  preview->selectedSlotIndex = 0;
  preview->generationCount = 0;
  preview->shuffleOffset = 0;
  preview->requestedTeamSize = 0;
  preview->humanTeam = 2;
  preview->generationSource = "startup";
  preview->ready = false;
  preview->slotHeroOverrides.clear();
  preview->lineup.clear();
  preview->warnings.clear();

  if (heroCatalog.entries.empty())
  {
    preview->warnings.push_back("Hero catalog is empty");
    return;
  }

  RegenerateLocalMatchPreview(heroCatalog, mapCatalog, mapBrowserState, preview, "startup");
  preview->selectedSlotIndex = ResolveHumanSlotIndex(*preview);
}

void ApplyLaunchSelections(
  const LinuxClientLaunchSettings& settings,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  LinuxArtworkSelectionState* artworkState,
  LinuxLocalMatchPreview* localMatchPreview
)
{
  if (artworkState)
  {
    artworkState->mode = settings.artworkMode >= 0 && settings.artworkMode < LINUX_ARTWORK_COUNT ?
      settings.artworkMode : LINUX_ARTWORK_AUTO;
    artworkState->source = settings.artworkMode == LINUX_ARTWORK_AUTO ? "default" : "command-line";
  }

  if (!localMatchPreview)
  {
    return;
  }

  const size_t selectedHeroIndex = FindHeroCatalogIndex(heroCatalog, settings.heroSelector);
  if (selectedHeroIndex != static_cast<size_t>(-1))
  {
    localMatchPreview->selectedHeroIndex = selectedHeroIndex;
    localMatchPreview->generationSource = "command-line";
    RegenerateLocalMatchPreview(heroCatalog, mapCatalog, mapBrowserState, localMatchPreview, "command-line");
    localMatchPreview->selectedSlotIndex = ResolveHumanSlotIndex(*localMatchPreview);
    return;
  }

  if (!settings.heroSelector.empty())
  {
    localMatchPreview->warnings.push_back("Hero selector not found: " + settings.heroSelector);
  }
}

bool LoadArtworkFile(const fs::path& sourcePath, LinuxLoadingArtwork* artwork, std::string* error)
{
  const std::string extension = ToAsciiLower(sourcePath.extension().string());
  if (extension == ".png")
  {
    return LoadPngArtwork(sourcePath, artwork, error);
  }

  if (extension == ".dds")
  {
    return LoadDdsArtwork(sourcePath, artwork, error);
  }

  if (error)
  {
    *error = "unsupported artwork extension " + extension;
  }
  return false;
}

void ProbeTextureAsset(
  const LinuxClientEnvironment& environment,
  const std::string& reference,
  LinuxTextureAssetPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxTextureAssetPreview();
  preview->reference = reference;
  if (reference.empty())
  {
    return;
  }

  const fs::path descriptorPath = ResolveDataRefPath(environment, reference, ".xdb");
  preview->descriptorFile = descriptorPath.string();
  if (descriptorPath.empty() || !fs::exists(descriptorPath))
  {
    preview->warnings.push_back("Texture descriptor missing for " + reference);
    return;
  }

  preview->descriptorResolved = true;

  std::string xml;
  if (!ReadTextFile(descriptorPath, &xml))
  {
    preview->warnings.push_back("Texture descriptor unreadable for " + reference);
    return;
  }

  const size_t sizePos = xml.find("<size>");
  const size_t sizeEnd = xml.find("</size>", sizePos);
  if (sizePos != std::string::npos && sizeEnd != std::string::npos)
  {
    const std::string sizeBlock = xml.substr(sizePos, sizeEnd - sizePos);
    preview->width = static_cast<unsigned long>(atoi(ExtractTagValue(sizeBlock, "width", 0).c_str()));
    preview->height = static_cast<unsigned long>(atoi(ExtractTagValue(sizeBlock, "height", 0).c_str()));
  }

  const std::string textureFileName = ExtractTagValue(xml, "textureFileName", 0);
  const std::string srcFileName = ExtractTagValue(xml, "srcFileName", 0);

  fs::path payloadPath;
  if (!textureFileName.empty())
  {
    payloadPath = ResolveDataRefPath(environment, textureFileName, 0);
  }

  if ((payloadPath.empty() || !fs::exists(payloadPath)) && !srcFileName.empty())
  {
    payloadPath = ResolveDataRefPath(environment, srcFileName, 0);
  }

  if (payloadPath.empty() || !fs::exists(payloadPath))
  {
    preview->warnings.push_back("Texture payload missing for " + reference);
    return;
  }

  preview->sourceResolved = true;
  preview->sourceFile = payloadPath.string();
  std::string error;
  if (!LoadArtworkFile(payloadPath, &preview->artwork, &error))
  {
    preview->warnings.push_back("Texture payload load failed for " + reference + ": " + error);
    return;
  }

  preview->artworkLoaded = true;
  if (!preview->width)
  {
    preview->width = static_cast<unsigned long>(preview->artwork.width);
  }
  if (!preview->height)
  {
    preview->height = static_cast<unsigned long>(preview->artwork.height);
  }
}

void CopyArtwork(const LinuxLoadingArtwork& source, LinuxLoadingArtwork* target)
{
  target->width = source.width;
  target->height = source.height;
  target->rgba = source.rgba;
  target->ready = source.ready;
}

unsigned char BlendChannel(unsigned char source, unsigned char destination, unsigned char alpha);

void CompositeArtwork(LinuxLoadingArtwork* base, const LinuxLoadingArtwork& overlay, int offsetX, int offsetY)
{
  if (!base || !base->ready || !overlay.ready)
  {
    return;
  }

  for (int y = 0; y < overlay.height; ++y)
  {
    const int targetY = y + offsetY;
    if (targetY < 0 || targetY >= base->height)
    {
      continue;
    }

    for (int x = 0; x < overlay.width; ++x)
    {
      const int targetX = x + offsetX;
      if (targetX < 0 || targetX >= base->width)
      {
        continue;
      }

      const size_t srcIndex = static_cast<size_t>(y * overlay.width + x) * 4U;
      const size_t dstIndex = static_cast<size_t>(targetY * base->width + targetX) * 4U;
      const unsigned char alpha = overlay.rgba[srcIndex + 3];
      if (!alpha)
      {
        continue;
      }

      base->rgba[dstIndex + 0] = BlendChannel(overlay.rgba[srcIndex + 0], base->rgba[dstIndex + 0], alpha);
      base->rgba[dstIndex + 1] = BlendChannel(overlay.rgba[srcIndex + 1], base->rgba[dstIndex + 1], alpha);
      base->rgba[dstIndex + 2] = BlendChannel(overlay.rgba[srcIndex + 2], base->rgba[dstIndex + 2], alpha);

      const unsigned int dstAlpha = static_cast<unsigned int>(base->rgba[dstIndex + 3]);
      base->rgba[dstIndex + 3] = static_cast<unsigned char>(
        static_cast<unsigned int>(alpha) + (dstAlpha * (255U - static_cast<unsigned int>(alpha)) + 127U) / 255U
      );
    }
  }
}

void ScaleArtworkToFit(
  const LinuxLoadingArtwork& source,
  int maxWidth,
  int maxHeight,
  LinuxLoadingArtwork* target
)
{
  if (!target)
  {
    return;
  }

  *target = LinuxLoadingArtwork();
  if (!source.ready || source.width <= 0 || source.height <= 0)
  {
    return;
  }

  if (maxWidth <= 0 || maxHeight <= 0 ||
      (source.width <= maxWidth && source.height <= maxHeight))
  {
    CopyArtwork(source, target);
    return;
  }

  const double widthScale = static_cast<double>(maxWidth) / static_cast<double>(source.width);
  const double heightScale = static_cast<double>(maxHeight) / static_cast<double>(source.height);
  const double scale = std::min(widthScale, heightScale);
  const int scaledWidth = std::max(1, static_cast<int>(source.width * scale));
  const int scaledHeight = std::max(1, static_cast<int>(source.height * scale));

  target->width = scaledWidth;
  target->height = scaledHeight;
  target->rgba.resize(static_cast<size_t>(scaledWidth) * static_cast<size_t>(scaledHeight) * 4U);

  for (int y = 0; y < scaledHeight; ++y)
  {
    const int sourceY = std::min(source.height - 1, y * source.height / scaledHeight);
    for (int x = 0; x < scaledWidth; ++x)
    {
      const int sourceX = std::min(source.width - 1, x * source.width / scaledWidth);
      const size_t srcIndex = static_cast<size_t>(sourceY * source.width + sourceX) * 4U;
      const size_t dstIndex = static_cast<size_t>(y * scaledWidth + x) * 4U;
      target->rgba[dstIndex + 0] = source.rgba[srcIndex + 0];
      target->rgba[dstIndex + 1] = source.rgba[srcIndex + 1];
      target->rgba[dstIndex + 2] = source.rgba[srcIndex + 2];
      target->rgba[dstIndex + 3] = source.rgba[srcIndex + 3];
    }
  }

  target->ready = !target->rgba.empty();
}

void BlendArtworkPixel(
  LinuxLoadingArtwork* artwork,
  int x,
  int y,
  unsigned char red,
  unsigned char green,
  unsigned char blue,
  unsigned char alpha
)
{
  if (!artwork || !artwork->ready || x < 0 || y < 0 || x >= artwork->width || y >= artwork->height || !alpha)
  {
    return;
  }

  const size_t pixelIndex = static_cast<size_t>(y * artwork->width + x) * 4U;
  artwork->rgba[pixelIndex + 0] = BlendChannel(red, artwork->rgba[pixelIndex + 0], alpha);
  artwork->rgba[pixelIndex + 1] = BlendChannel(green, artwork->rgba[pixelIndex + 1], alpha);
  artwork->rgba[pixelIndex + 2] = BlendChannel(blue, artwork->rgba[pixelIndex + 2], alpha);

  const unsigned int dstAlpha = static_cast<unsigned int>(artwork->rgba[pixelIndex + 3]);
  artwork->rgba[pixelIndex + 3] = static_cast<unsigned char>(
    static_cast<unsigned int>(alpha) + (dstAlpha * (255U - static_cast<unsigned int>(alpha)) + 127U) / 255U
  );
}

void FillArtworkRect(
  LinuxLoadingArtwork* artwork,
  int x,
  int y,
  int width,
  int height,
  unsigned char red,
  unsigned char green,
  unsigned char blue,
  unsigned char alpha
)
{
  if (!artwork || !artwork->ready || width <= 0 || height <= 0)
  {
    return;
  }

  for (int row = 0; row < height; ++row)
  {
    for (int column = 0; column < width; ++column)
    {
      BlendArtworkPixel(artwork, x + column, y + row, red, green, blue, alpha);
    }
  }
}

void StrokeArtworkRect(
  LinuxLoadingArtwork* artwork,
  int x,
  int y,
  int width,
  int height,
  int thickness,
  unsigned char red,
  unsigned char green,
  unsigned char blue,
  unsigned char alpha
)
{
  if (!artwork || !artwork->ready || width <= 0 || height <= 0 || thickness <= 0)
  {
    return;
  }

  FillArtworkRect(artwork, x, y, width, thickness, red, green, blue, alpha);
  FillArtworkRect(artwork, x, y + height - thickness, width, thickness, red, green, blue, alpha);
  FillArtworkRect(artwork, x, y + thickness, thickness, height - thickness * 2, red, green, blue, alpha);
  FillArtworkRect(
    artwork,
    x + width - thickness,
    y + thickness,
    thickness,
    height - thickness * 2,
    red,
    green,
    blue,
    alpha
  );
}

void DrawArtworkMarker(
  LinuxLoadingArtwork* artwork,
  int centerX,
  int centerY,
  int halfSize,
  unsigned char red,
  unsigned char green,
  unsigned char blue,
  unsigned char alpha
)
{
  FillArtworkRect(
    artwork,
    centerX - halfSize,
    centerY - halfSize,
    halfSize * 2 + 1,
    halfSize * 2 + 1,
    red,
    green,
    blue,
    alpha
  );
}

void CompositeTacticalMapInset(
  const LinuxSelectedMapPreview& preview,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  LinuxLoadingArtwork* artwork
)
{
  if (!artwork || !artwork->ready || !preview.tactical.ready)
  {
    return;
  }

  const float rangeX = preview.tactical.maxX - preview.tactical.minX;
  const float rangeY = preview.tactical.maxY - preview.tactical.minY;
  if (rangeX < 1.0f || rangeY < 1.0f)
  {
    return;
  }

  const int panelSize = std::min(220, std::min(std::max(140, artwork->width / 3), std::max(140, artwork->height / 2)));
  const int panelX = artwork->width - panelSize - 18;
  const int panelY = 18;
  const int padding = 16;
  const int mapWidth = panelSize - padding * 2;
  const int mapHeight = panelSize - padding * 2;
  const int innerX = panelX + padding;
  const int innerY = panelY + padding;

  FillArtworkRect(artwork, panelX, panelY, panelSize, panelSize, 14, 20, 28, 212);
  StrokeArtworkRect(artwork, panelX, panelY, panelSize, panelSize, 2, 102, 122, 145, 240);
  FillArtworkRect(artwork, innerX, innerY, mapWidth, mapHeight, 22, 31, 42, 220);
  StrokeArtworkRect(artwork, innerX, innerY, mapWidth, mapHeight, 1, 66, 87, 110, 240);

  for (size_t i = 0; i < preview.tactical.markers.size(); ++i)
  {
    const LinuxTacticalMapMarker& marker = preview.tactical.markers[i];
    const float normalizedX = (marker.translateX - preview.tactical.minX) / rangeX;
    const float normalizedY = 1.0f - ((marker.translateY - preview.tactical.minY) / rangeY);
    const int pixelX = innerX + static_cast<int>(normalizedX * static_cast<float>(mapWidth - 1));
    const int pixelY = innerY + static_cast<int>(normalizedY * static_cast<float>(mapHeight - 1));

    unsigned char red = 184;
    unsigned char green = 194;
    unsigned char blue = 204;
    int halfSize = 2;

    if (marker.kind == "tower")
    {
      red = 240;
      green = 197;
      blue = 92;
    }
    else if (marker.kind == "hero-spawn")
    {
      halfSize = 3;
      if (marker.team == 1)
      {
        red = 214;
        green = 92;
        blue = 92;
      }
      else if (marker.team == 2)
      {
        red = 92;
        green = 156;
        blue = 224;
      }
      else
      {
        red = 224;
        green = 224;
        blue = 224;
      }
    }
    else if (marker.kind == "lane-spawner")
    {
      red = 136;
      green = 188;
      blue = 88;
    }
    else if (marker.kind == "neutral-spawner")
    {
      red = 123;
      green = 167;
      blue = 111;
    }
    else if (marker.kind == "boss")
    {
      red = 255;
      green = 143;
      blue = 72;
      halfSize = 3;
    }
    else if (marker.kind == "shop")
    {
      red = 93;
      green = 211;
      blue = 147;
      halfSize = 3;
    }
    else if (marker.kind == "fountain")
    {
      halfSize = 3;
      if (marker.team == 1)
      {
        red = 220;
        green = 92;
        blue = 120;
      }
      else if (marker.team == 2)
      {
        red = 92;
        green = 187;
        blue = 234;
      }
    }
    else if (marker.kind == "glyph")
    {
      red = 96;
      green = 214;
      blue = 224;
      halfSize = 3;
    }
    else if (marker.kind == "main-building")
    {
      halfSize = 4;
      if (marker.team == 1)
      {
        red = 232;
        green = 118;
        blue = 118;
      }
      else if (marker.team == 2)
      {
        red = 114;
        green = 176;
        blue = 236;
      }
    }
    else if (marker.kind == "minigame")
    {
      red = 208;
      green = 136;
      blue = 230;
      halfSize = 3;
    }

    DrawArtworkMarker(artwork, pixelX, pixelY, halfSize, red, green, blue, 232);
  }

  for (size_t i = 0; i < engineMapStartPreview.slots.size(); ++i)
  {
    const LinuxEngineMapStartSlot& slot = engineMapStartPreview.slots[i];
    const float normalizedX = (slot.translateX - preview.tactical.minX) / rangeX;
    const float normalizedY = 1.0f - ((slot.translateY - preview.tactical.minY) / rangeY);
    const int pixelX = innerX + static_cast<int>(normalizedX * static_cast<float>(mapWidth - 1));
    const int pixelY = innerY + static_cast<int>(normalizedY * static_cast<float>(mapHeight - 1));

    if (slot.filled)
    {
      DrawArtworkMarker(
        artwork,
        pixelX,
        pixelY,
        slot.human ? 2 : 1,
        slot.human ? 255 : 244,
        slot.manualHero ? 222 : 244,
        slot.manualHero ? 132 : 244,
        255
      );
    }

    if (slot.lineupIndex == localMatchPreview.selectedSlotIndex)
    {
      StrokeArtworkRect(artwork, pixelX - 5, pixelY - 5, 11, 11, 1, 255, 255, 255, 255);
    }
  }
}

bool CompositeLocalMatchPortraits(
  const LinuxClientEnvironment& environment,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  LinuxLoadingArtwork* artwork
)
{
  if (!artwork || !artwork->ready || !localMatchPreview.ready || heroCatalog.entries.empty())
  {
    return false;
  }

  const int gap = std::max(6, std::min(18, artwork->height / 32));
  const int panelPadding = std::max(5, std::min(14, artwork->height / 40));
  const int availableHeight = std::max(artwork->height - 2 * gap, 1);
  const int maxPortraitSizeByHeight =
    std::max(32, (availableHeight - static_cast<int>(localMatchPreview.teamSize > 0 ? (localMatchPreview.teamSize - 1) * gap : 0)) /
      std::max(static_cast<int>(localMatchPreview.teamSize), 1));
  const int portraitSize = std::max(36, std::min(std::min(112, artwork->width / 5), maxPortraitSizeByHeight));
  const int blockHeight = static_cast<int>(localMatchPreview.teamSize) * portraitSize +
    static_cast<int>(localMatchPreview.teamSize > 0 ? (localMatchPreview.teamSize - 1) * gap : 0);
  const int startY = std::max(gap, (artwork->height - blockHeight) / 2);
  const int leftX = gap;
  const int rightX = std::max(gap, artwork->width - portraitSize - gap);
  std::map<size_t, LinuxTextureAssetPreview> portraitCache;
  size_t portraitsComposited = 0;

  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    if (slot.heroIndex >= heroCatalog.entries.size())
    {
      continue;
    }

    const size_t teamSlotIndex = i % std::max(localMatchPreview.teamSize, static_cast<size_t>(1));
    const int portraitX = slot.team == 1 ? leftX : rightX;
    const int portraitY = startY + static_cast<int>(teamSlotIndex) * (portraitSize + gap);
    const int panelX = portraitX - panelPadding;
    const int panelY = portraitY - panelPadding;
    const int panelSize = portraitSize + panelPadding * 2;

    FillArtworkRect(artwork, panelX, panelY, panelSize, panelSize, 17, 22, 28, 176);

    if (slot.human)
    {
      StrokeArtworkRect(artwork, panelX, panelY, panelSize, panelSize, 3, 237, 191, 77, 255);
    }
    else if (slot.team == 1)
    {
      StrokeArtworkRect(artwork, panelX, panelY, panelSize, panelSize, 2, 199, 86, 86, 232);
    }
    else
    {
      StrokeArtworkRect(artwork, panelX, panelY, panelSize, panelSize, 2, 87, 164, 224, 232);
    }

    if (slot.manualHero)
    {
      StrokeArtworkRect(artwork, panelX + 4, panelY + 4, panelSize - 8, panelSize - 8, 2, 248, 214, 96, 224);
    }

    if (i == localMatchPreview.selectedSlotIndex)
    {
      StrokeArtworkRect(artwork, panelX - 3, panelY - 3, panelSize + 6, panelSize + 6, 3, 255, 255, 255, 255);
    }

    LinuxTextureAssetPreview portraitPreview;
    std::map<size_t, LinuxTextureAssetPreview>::const_iterator cached = portraitCache.find(slot.heroIndex);
    if (cached == portraitCache.end())
    {
      const LinuxHeroCatalogEntry& heroEntry = heroCatalog.entries[slot.heroIndex];
      if (!heroEntry.iconRef.empty())
      {
        ProbeTextureAsset(environment, heroEntry.iconRef, &portraitPreview);
      }
      portraitCache[slot.heroIndex] = portraitPreview;
    }
    else
    {
      portraitPreview = cached->second;
    }

    if (!portraitPreview.artworkLoaded)
    {
      FillArtworkRect(artwork, portraitX, portraitY, portraitSize, portraitSize, 42, 53, 66, 208);
      continue;
    }

    LinuxLoadingArtwork scaledPortrait;
    ScaleArtworkToFit(portraitPreview.artwork, portraitSize, portraitSize, &scaledPortrait);
    const int centeredX = portraitX + (portraitSize - scaledPortrait.width) / 2;
    const int centeredY = portraitY + (portraitSize - scaledPortrait.height) / 2;
    CompositeArtwork(artwork, scaledPortrait, centeredX, centeredY);
    ++portraitsComposited;
  }

  return portraitsComposited > 0;
}

void ResolveLoadingStatusAccent(
  const std::string& statusKey,
  unsigned char* red,
  unsigned char* green,
  unsigned char* blue
)
{
  if (!red || !green || !blue)
  {
    return;
  }

  *red = 78;
  *green = 124;
  *blue = 182;

  const std::string key = ToAsciiLower(statusKey);
  if (key.find("ok") != std::string::npos)
  {
    *red = 82;
    *green = 166;
    *blue = 112;
    return;
  }
  if (key.find("entering") != std::string::npos || key.find("waiting") != std::string::npos)
  {
    *red = 224;
    *green = 176;
    *blue = 82;
    return;
  }
  if (key.find("fail") != std::string::npos ||
      key.find("error") != std::string::npos ||
      key.find("denied") != std::string::npos ||
      key.find("refused") != std::string::npos ||
      key.find("wrong") != std::string::npos)
  {
    *red = 194;
    *green = 78;
    *blue = 78;
    return;
  }
  if (key.find("replay") != std::string::npos)
  {
    *red = 140;
    *green = 104;
    *blue = 196;
  }
}

bool ResolveLoadingUiArtwork(
  const LinuxClientEnvironment& environment,
  const std::string& reference,
  int maxWidth,
  int maxHeight,
  LinuxLoadingArtwork* artwork
)
{
  if (!artwork)
  {
    return false;
  }

  *artwork = LinuxLoadingArtwork();
  if (reference.empty())
  {
    return false;
  }

  LinuxTextureAssetPreview preview;
  ProbeTextureAsset(environment, reference, &preview);
  if (!preview.artworkLoaded)
  {
    return false;
  }

  ScaleArtworkToFit(preview.artwork, maxWidth, maxHeight, artwork);
  return artwork->ready;
}

void CompositeLoadingUiAssets(
  const LinuxClientEnvironment& environment,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxLoadingUiPreview& loadingUiPreview,
  const LinuxLoadingUiState& loadingUiState,
  LinuxLoadingArtwork* artwork
)
{
  if (!artwork || !artwork->ready || !loadingUiPreview.ready)
  {
    return;
  }

  std::string statusKey;
  if (!loadingUiPreview.statuses.empty() && loadingUiState.statusIndex < loadingUiPreview.statuses.size())
  {
    statusKey = loadingUiPreview.statuses[loadingUiState.statusIndex].key;
  }

  unsigned char accentRed = 78;
  unsigned char accentGreen = 124;
  unsigned char accentBlue = 182;
  ResolveLoadingStatusAccent(statusKey, &accentRed, &accentGreen, &accentBlue);

  FillArtworkRect(artwork, 0, 0, artwork->width, 10, accentRed, accentGreen, accentBlue, 164);
  FillArtworkRect(artwork, 0, artwork->height - 8, artwork->width, 8, accentRed, accentGreen, accentBlue, 132);

  const int margin = std::max(12, std::min(20, artwork->width / 40));
  const int localeMax = std::max(34, std::min(64, artwork->height / 5));
  const int modeMax = std::max(36, std::min(72, artwork->height / 4));

  int enemyLocaleX = artwork->width - localeMax - margin;
  const int localeY = margin + 8;
  if (selectedMapPreview.tactical.ready)
  {
    const int tacticalSize = std::min(220, std::min(std::max(140, artwork->width / 3), std::max(140, artwork->height / 2)));
    const int tacticalX = artwork->width - tacticalSize - 18;
    enemyLocaleX = std::max(margin, tacticalX - localeMax - margin);
  }

  auto compositeBadge = [&](const LinuxLoadingArtwork& badge, int originX, int originY, bool leftAligned)
  {
    const int frameX = leftAligned ? originX - 6 : originX - 6;
    const int frameY = originY - 6;
    FillArtworkRect(artwork, frameX, frameY, badge.width + 12, badge.height + 12, 14, 20, 28, 188);
    StrokeArtworkRect(artwork, frameX, frameY, badge.width + 12, badge.height + 12, 2, 241, 229, 211, 224);
    CompositeArtwork(artwork, badge, originX, originY);
  };

  if (!loadingUiPreview.locales.empty())
  {
    if (loadingUiState.currentLocaleIndex < loadingUiPreview.locales.size())
    {
      LinuxLoadingArtwork currentLocaleArtwork;
      if (ResolveLoadingUiArtwork(
            environment,
            loadingUiPreview.locales[loadingUiState.currentLocaleIndex].imageRef,
            localeMax,
            localeMax,
            &currentLocaleArtwork))
      {
        compositeBadge(currentLocaleArtwork, margin, localeY, true);
      }
    }

    if (loadingUiState.enemyLocaleIndex < loadingUiPreview.locales.size())
    {
      LinuxLoadingArtwork enemyLocaleArtwork;
      if (ResolveLoadingUiArtwork(
            environment,
            loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].imageRef,
            localeMax,
            localeMax,
            &enemyLocaleArtwork))
      {
        compositeBadge(enemyLocaleArtwork, enemyLocaleX, localeY, false);
      }
    }
  }

  if (!loadingUiPreview.modes.empty() && loadingUiState.modeIndex < loadingUiPreview.modes.size())
  {
    LinuxLoadingArtwork modeArtwork;
    if (ResolveLoadingUiArtwork(
          environment,
          loadingUiPreview.modes[loadingUiState.modeIndex].iconRef,
          modeMax,
          modeMax,
          &modeArtwork))
    {
      const int modeX = std::max(margin, (artwork->width - modeArtwork.width) / 2);
      compositeBadge(modeArtwork, modeX, margin + 6, true);
    }
  }
}

void ProbeSelectedMapPreview(
  const LinuxClientEnvironment& environment,
  const LinuxMapCatalog& catalog,
  const LinuxMapBrowserState& browser,
  LinuxSelectedMapPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  if (catalog.entries.empty() || browser.selectedIndex >= catalog.entries.size())
  {
    *preview = LinuxSelectedMapPreview();
    return;
  }

  const LinuxMapCatalogEntry& entry = catalog.entries[browser.selectedIndex];
  if (preview->ready &&
      preview->selectedIndex == browser.selectedIndex &&
      preview->descriptor == entry.descriptor)
  {
    return;
  }

  *preview = LinuxSelectedMapPreview();
  preview->ready = true;
  preview->selectedIndex = browser.selectedIndex;
  preview->descriptor = entry.descriptor;

  std::string mapXml;
  std::string mapSettingsRefFromMap;
  const fs::path mapPath = ResolveDataRefPath(environment, entry.mapRef, ".xdb");
  preview->mapFile = mapPath.string();
  if (!mapPath.empty() && fs::exists(mapPath))
  {
    preview->mapResolved = true;

    if (ReadTextFile(mapPath, &mapXml))
    {
      preview->terrainRef = ExtractTagHref(mapXml, "terrain", 0);
      preview->cameraSettingsRef = ExtractTagHref(mapXml, "cameraSettings", 0);
      preview->lightEnvironmentRef = ExtractTagHref(mapXml, "lightEnvironment", 0);
      preview->nightLightEnvironmentRef = ExtractTagHref(mapXml, "nightLightEnvironment", 0);
      mapSettingsRefFromMap = ExtractTagHref(mapXml, "mapSettings", 0);
      ProbeTacticalMapPreview(mapXml, &preview->tactical);

      const size_t minimapPos = mapXml.find("<minimapImages");
      if (minimapPos != std::string::npos)
      {
        preview->minimapFirstRef = ExtractTagHref(mapXml, "firstMap", minimapPos);
        preview->minimapSecondRef = ExtractTagHref(mapXml, "secondMap", minimapPos);
        preview->minimapNeutralRef = ExtractTagHref(mapXml, "neutralMap", minimapPos);
      }

      const size_t objectsPos = mapXml.find("<objects>");
      const size_t objectsEnd = mapXml.find("</objects>", objectsPos);
      if (objectsPos != std::string::npos && objectsEnd != std::string::npos)
      {
        const std::string objectsBlock = mapXml.substr(objectsPos, objectsEnd - objectsPos);
        preview->objectCount = CountLiteralOccurrences(objectsBlock, "<gameObject ");
        preview->lockMapObjectCount = CountLiteralOccurrences(objectsBlock, "<lockMap>true</lockMap>");
        preview->scriptedObjectCount = CountNonEmptyTagValues(objectsBlock, "scriptName");
      }
    }
    else
    {
      preview->warnings.push_back("Selected map XML unreadable");
    }
  }
  else
  {
    preview->warnings.push_back("Selected map file missing");
  }

  preview->settings.source = !entry.mapSettingsRef.empty() ? "descriptor" :
    (!mapSettingsRefFromMap.empty() ? "map" : "missing");
  preview->settings.reference = !entry.mapSettingsRef.empty() ? entry.mapSettingsRef : mapSettingsRefFromMap;

  if (!preview->settings.reference.empty())
  {
    const fs::path mapSettingsPath = ResolveDataRefPath(environment, preview->settings.reference, ".xdb");
    preview->mapSettingsFile = mapSettingsPath.string();

    std::set<std::string> visitedSettings;
    std::function<void(const std::string&)> resolveSettings = [&](const std::string& settingsRef)
    {
      if (settingsRef.empty())
      {
        return;
      }

      const std::string normalizedSettingsRef = NormalizeDataRefPath(settingsRef);
      if (!visitedSettings.insert(normalizedSettingsRef).second)
      {
        preview->settings.warnings.push_back("Map settings inheritance loop: " + normalizedSettingsRef);
        return;
      }

      const fs::path settingsPath = ResolveDataRefPath(environment, normalizedSettingsRef, ".xdb");
      if (settingsPath.empty() || !fs::exists(settingsPath))
      {
        preview->settings.warnings.push_back("Map settings file missing: " + normalizedSettingsRef);
        return;
      }

      std::string settingsXml;
      if (!ReadTextFile(settingsPath, &settingsXml))
      {
        preview->settings.warnings.push_back("Map settings XML unreadable: " + settingsPath.string());
        return;
      }

      const std::string parentRef = ExtractRootTagAttribute(settingsXml, "parent");
      if (!parentRef.empty())
      {
        preview->settings.parentRef = parentRef;
        resolveSettings(parentRef);
      }

      preview->settings.resolved = true;
      preview->settings.chainReferences.push_back(normalizedSettingsRef);
      preview->settings.chainFiles.push_back(settingsPath.string());

      std::string value;
      if (TryExtractTagValue(settingsXml, "scriptFileName", 0, &value))
      {
        preview->settings.scriptFile = TrimAscii(value);
      }
      if (TryExtractTagHref(settingsXml, "dictionary", 0, &value))
      {
        preview->settings.dictionaryRef = value;
        preview->settings.dictionaryResourceCount = 0;
        preview->settings.dictionaryKeysPreview.clear();

        const size_t resourcesPos = settingsXml.find("<resources>");
        const size_t resourcesEnd = settingsXml.find("</resources>", resourcesPos);
        if (resourcesPos != std::string::npos && resourcesEnd != std::string::npos)
        {
          const std::string resourcesBlock = settingsXml.substr(resourcesPos, resourcesEnd - resourcesPos);
          preview->settings.dictionaryResourceCount = CountLiteralOccurrences(resourcesBlock, "<Item>");

          std::vector<std::string> blocks = ExtractItemBlocks(resourcesBlock);
          for (size_t i = 0; i < blocks.size() && preview->settings.dictionaryKeysPreview.size() < 4; ++i)
          {
            const std::string key = TrimAscii(ExtractTagValue(blocks[i], "key", 0));
            if (!key.empty())
            {
              preview->settings.dictionaryKeysPreview.push_back(key);
            }
          }
        }
      }
      if (TryExtractTagHref(settingsXml, "dialogsCollection", 0, &value))
      {
        preview->settings.dialogsCollectionRef = value;
      }
      if (TryExtractTagHref(settingsXml, "hintsCollection", 0, &value))
      {
        preview->settings.hintsCollectionRef = value;
      }
      if (TryExtractTagHref(settingsXml, "questsCollection", 0, &value))
      {
        preview->settings.questsCollectionRef = value;
      }
      if (TryExtractTagHref(settingsXml, "overrideBotsSettings", 0, &value))
      {
        preview->settings.overrideBotsSettingsRef = value;
      }
      if (TryExtractTagHref(settingsXml, "overrideGlyphSettings", 0, &value))
      {
        preview->settings.overrideGlyphSettingsRef = value;
      }
      if (TryExtractTagHref(settingsXml, "heroRespawnParams", 0, &value))
      {
        preview->settings.heroRespawnParamsRef = value;
      }
      if (TryExtractTagValue(settingsXml, "battleStartDelay", 0, &value))
      {
        preview->settings.battleStartDelay = atoi(TrimAscii(value).c_str());
      }
      if (TryExtractTagValue(settingsXml, "emblemHeroNeeds", 0, &value))
      {
        preview->settings.emblemHeroNeeds = atoi(TrimAscii(value).c_str());
      }
      if (TryExtractTagValue(settingsXml, "force", 0, &value))
      {
        preview->settings.force = atoi(TrimAscii(value).c_str());
      }
      if (TryExtractTagValue(settingsXml, "minRequiredHeroForce", 0, &value))
      {
        preview->settings.minRequiredHeroForce = atoi(TrimAscii(value).c_str());
      }
      if (TryExtractTagValue(settingsXml, "maxRequiredHeroForce", 0, &value))
      {
        preview->settings.maxRequiredHeroForce = atoi(TrimAscii(value).c_str());
      }
      if (TryExtractTagValue(settingsXml, "towersVulnerabilityDelay", 0, &value))
      {
        preview->settings.towersVulnerabilityDelay = atoi(TrimAscii(value).c_str());
      }
      if (TryExtractTagValue(settingsXml, "enableAllScriptFunctions", 0, &value))
      {
        preview->settings.enableAllScriptFunctions = TrimAscii(value) == "true";
      }
      if (TryExtractTagValue(settingsXml, "enableAnnouncements", 0, &value))
      {
        preview->settings.enableAnnouncements = TrimAscii(value) == "true";
      }
      if (TryExtractTagValue(settingsXml, "enablePortalTalent", 0, &value))
      {
        preview->settings.enablePortalTalent = TrimAscii(value) == "true";
      }
      if (TryExtractTagValue(settingsXml, "enableStatistics", 0, &value))
      {
        preview->settings.enableStatistics = TrimAscii(value) == "true";
      }
      if (TryExtractTagValue(settingsXml, "showAllHeroes", 0, &value))
      {
        preview->settings.showAllHeroes = TrimAscii(value) == "true";
      }
      if (TryExtractTagValue(settingsXml, "fullPartyOnly", 0, &value))
      {
        preview->settings.fullPartyOnly = TrimAscii(value) == "true";
      }

      const size_t primePos = settingsXml.find("<primeSettings>");
      const size_t primeEnd = settingsXml.find("</primeSettings>", primePos);
      if (primePos != std::string::npos && primeEnd != std::string::npos)
      {
        const std::string primeBlock = settingsXml.substr(primePos, primeEnd - primePos);
        if (TryExtractTagValue(primeBlock, "startPrimePerTeam", 0, &value))
        {
          preview->settings.startPrimePerTeam = atoi(TrimAscii(value).c_str());
        }
      }
    };

    resolveSettings(preview->settings.reference);
    preview->mapSettingsResolved = preview->settings.resolved;
    if (!preview->settings.chainFiles.empty())
    {
      preview->mapSettingsFile = preview->settings.chainFiles.back();
    }
    preview->scriptFile = preview->settings.scriptFile;
    preview->dictionaryRef = preview->settings.dictionaryRef;
    preview->dialogsCollectionRef = preview->settings.dialogsCollectionRef;
    preview->hintsCollectionRef = preview->settings.hintsCollectionRef;
    preview->questsCollectionRef = preview->settings.questsCollectionRef;
  }
  else
  {
    preview->warnings.push_back("Selected map settings reference missing");
  }

  const fs::path scoringPath = ResolveDataRefPath(environment, entry.scoringTableRef, ".xdb");
  preview->scoringTableFile = scoringPath.string();
  if (!scoringPath.empty() && fs::exists(scoringPath))
  {
    preview->scoringTableResolved = true;
  }
  else
  {
    preview->warnings.push_back("Selected scoring table missing");
  }

  ProbeTextureAsset(environment, entry.loadingBackRef, &preview->loadingBack);
  ProbeTextureAsset(environment, entry.loadingLogoRef, &preview->loadingLogo);
  ProbeTextureAsset(environment, preview->minimapFirstRef, &preview->minimapFirst);
  ProbeTextureAsset(environment, preview->minimapSecondRef, &preview->minimapSecond);
  ProbeTextureAsset(environment, preview->minimapNeutralRef, &preview->minimapNeutral);
}

int DetectHeroPlaceholderTeam(const std::string& href)
{
  const std::string hrefLower = ToAsciiLower(href);
  if (hrefLower.find("teama.hplh") != std::string::npos)
  {
    return 1;
  }
  if (hrefLower.find("teamb.hplh") != std::string::npos)
  {
    return 2;
  }
  return 0;
}

size_t MatchLocalLineupIndexForEngineSlot(
  const LinuxLocalMatchPreview& localMatchPreview,
  int team,
  bool human,
  const std::string& heroId,
  std::vector<bool>* usedLineupEntries
)
{
  if (!usedLineupEntries)
  {
    return static_cast<size_t>(-1);
  }

  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    if ((*usedLineupEntries)[i] || !slot.human || !human)
    {
      continue;
    }

    if (slot.team == team)
    {
      return i;
    }
  }

  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    if ((*usedLineupEntries)[i] || slot.human != human)
    {
      continue;
    }

    if (slot.team == team && !heroId.empty() && slot.heroId == heroId)
    {
      return i;
    }
  }

  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    if ((*usedLineupEntries)[i])
    {
      continue;
    }

    if (slot.team == team && slot.human == human)
    {
      return i;
    }
  }

  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    if (!(*usedLineupEntries)[i] && slot.team == team)
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

void BuildLinuxPreviewGameLineup(
  const LinuxSessionPreview& sessionPreview,
  const LinuxLocalMatchPreview& localMatchPreview,
  lobby::TGameLineUp* gameLineUp
)
{
  if (!gameLineUp)
  {
    return;
  }

  gameLineUp->clear();
  gameLineUp->reserve(localMatchPreview.lineup.size());

  int nextBotUserId = -1;
  size_t botIndex = 0;
  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    lobby::SGameMember member;

    const bool human = slot.human;
    member.user.userId = human && sessionPreview.currentUserId > 0 ? sessionPreview.currentUserId : nextBotUserId--;
    member.user.zzimaSex = human ? lobby::ESex::Male : lobby::ESex::Male;
    member.user.nickname = BuildLinuxPreviewNickname(sessionPreview, human, botIndex);
    if (!human)
    {
      ++botIndex;
    }

    member.context.playerType = human ? lobby::EPlayerType::Human : lobby::EPlayerType::Computer;
    member.context.team = ConvertDisplayTeamToLobbyTeam(ConvertOverlayTeamToDisplayTeam(slot.team));
    member.context.original_team = member.context.team;
    member.context.hero = slot.heroId.c_str();
    member.context.botSkin.clear();

    gameLineUp->push_back(member);
  }
}

struct LinuxSyntheticClientInfo
{
  int clientId;
  NCore::PlayerInfo info;

  LinuxSyntheticClientInfo()
    : clientId(0)
  {
  }
};

void BuildLinuxPreviewClientInfos(
  const LinuxSessionPreview& sessionPreview,
  const LinuxLocalMatchPreview& localMatchPreview,
  const std::string& defaultLocale,
  const lobby::TGameLineUp& gameLineUp,
  std::vector<LinuxSyntheticClientInfo>* clientInfos
)
{
  if (!clientInfos)
  {
    return;
  }

  clientInfos->clear();
  clientInfos->reserve(gameLineUp.size());

  const std::string normalizedLocale = NormalizeLocaleName(defaultLocale);
  for (size_t i = 0; i < localMatchPreview.lineup.size() && i < gameLineUp.size(); ++i)
  {
    const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
    if (!slot.human)
    {
      continue;
    }

    const lobby::SGameMember& member = gameLineUp[i];
    LinuxSyntheticClientInfo clientInfo;
    clientInfo.clientId = member.user.userId;

    NCore::PlayerInfo& info = clientInfo.info;
    info.heroId = Crc32Checksum().AddString(slot.heroId.c_str()).Get();
    info.customGame = true;
    info.isAnimatedAvatar = true;

    if (!normalizedLocale.empty())
    {
      info.locale = normalizedLocale.c_str();
    }

    if (sessionPreview.currentUserId > 0 &&
        member.user.userId == sessionPreview.currentUserId &&
        sessionPreview.currentPartyId > 0)
    {
      info.partyId = static_cast<uint>(sessionPreview.currentPartyId);
    }

    const nstl::wstring userNickname(member.user.nickname.c_str());
    decltype(g_usersData)::iterator userData = g_usersData.find(userNickname);
    if (userData != g_usersData.end())
    {
      info.heroRating = userData->second.currentRating;
      info.ratingDeltaPrediction.onVictory = userData->second.victoryRating - userData->second.currentRating;
      info.ratingDeltaPrediction.onDefeat = userData->second.lossRating - userData->second.currentRating;
      if (userData->second.partyId > 0)
      {
        info.partyId = static_cast<uint>(userData->second.partyId);
      }
    }

    map<int, WebLauncherPostRequest::PlayerMetaInfo>::iterator meta = userIdToMetaMap.find(member.user.userId);
    if (meta != userIdToMetaMap.end())
    {
      info.leagueIndex = meta->second.leagueIdx;
      info.flagId = meta->second.flagId;
    }

    clientInfos->push_back(clientInfo);
  }
}

const LinuxSyntheticClientInfo* FindLinuxPreviewClientInfo(
  int clientId,
  const std::vector<LinuxSyntheticClientInfo>& clientInfos
)
{
  for (size_t i = 0; i < clientInfos.size(); ++i)
  {
    if (clientInfos[i].clientId == clientId)
    {
      return &clientInfos[i];
    }
  }

  return 0;
}

void MergeLinuxPreviewPlayerInfoMetadata(
  NCore::PlayerInfo* destination,
  const NCore::PlayerInfo& source
)
{
  if (!destination)
  {
    return;
  }

  if (!source.heroSkin.empty())
  {
    destination->heroSkin = source.heroSkin;
  }
  if (!source.locale.empty())
  {
    destination->locale = source.locale;
  }
  if (!source.flagId.empty())
  {
    destination->flagId = source.flagId;
  }
  if (!source.flagCustomPicture.empty())
  {
    destination->flagCustomPicture = source.flagCustomPicture;
  }
  if (!source.flagCustomTooltip.empty())
  {
    destination->flagCustomTooltip = source.flagCustomTooltip;
  }
  if (!source.leaguePlaces.empty())
  {
    destination->leaguePlaces = source.leaguePlaces;
  }

  destination->partyId = source.partyId;
  destination->heroLevel = source.heroLevel;
  destination->heroExp = source.heroExp;
  destination->heroRating = source.heroRating;
  destination->hasPremium = source.hasPremium;
  destination->basket = source.basket;
  destination->isAnimatedAvatar = source.isAnimatedAvatar;
  destination->leagueIndex = source.leagueIndex;
  destination->ownLeaguePlace = source.ownLeaguePlace;
  destination->ratingDeltaPrediction = source.ratingDeltaPrediction;
}

bool TryBuildEngineMapStartPreviewFromRealMapLoader(
  const LinuxSessionPreview& sessionPreview,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxLocalMatchPreview& localMatchPreview,
  const std::string& defaultLocale,
  LinuxEngineMapStartPreview* preview
)
{
  if (!preview)
  {
    return false;
  }

  if (mapCatalog.entries.empty() || mapBrowserState.selectedIndex >= mapCatalog.entries.size())
  {
    preview->warnings.push_back("Map browser selection unavailable");
    return false;
  }

  const LinuxMapCatalogEntry& selectedEntry = mapCatalog.entries[mapBrowserState.selectedIndex];
  preview->mapDescriptor = selectedEntry.descriptor;

  NDb::Ptr<NDb::AdvMapDescription> dbMapDescription = NDb::Get<NDb::AdvMapDescription>(NDb::DBID(selectedEntry.descriptor.c_str()));
  if (!IsValid(dbMapDescription))
  {
    preview->warnings.push_back("Failed to load selected map descriptor from DB cache");
    return false;
  }

  NDb::Ptr<NDb::AdvMap> dbMap = dbMapDescription->map;
  if (!IsValid(dbMap))
  {
    preview->warnings.push_back("Selected map descriptor has no map resource");
    return false;
  }

  preview->mapDescriptor = dbMapDescription->GetDBID().GetFileName().c_str();
  preview->source = "db+FillMapStartInfo";
  preview->usedRealMapLoader = true;

  for (size_t i = 0; i < dbMap->objects.size(); ++i)
  {
    const NDb::AdvMapObject& mapObject = dbMap->objects[i];
    if (!IsValid(mapObject.gameObject) || mapObject.gameObject->GetObjectTypeID() != NDb::HeroPlaceHolder::typeId)
    {
      continue;
    }

    const NDb::HeroPlaceHolder* placeholder = dynamic_cast<const NDb::HeroPlaceHolder*>(mapObject.gameObject.GetPtr());
    if (!placeholder)
    {
      continue;
    }

    LinuxEngineMapStartSlot slot;
    slot.spawnIndex = preview->slots.size();
    slot.team = ConvertDbTeamToDisplayTeam(placeholder->teamId);
    slot.scriptName = mapObject.scriptName.c_str();
    slot.translateX = mapObject.offset.GetPlace().pos.x;
    slot.translateY = mapObject.offset.GetPlace().pos.y;
    preview->slots.push_back(slot);
  }

  preview->totalSpawners = preview->slots.size();
  for (size_t i = 0; i < preview->slots.size(); ++i)
  {
    if (preview->slots[i].team == 1)
    {
      ++preview->team1Spawners;
    }
    else if (preview->slots[i].team == 2)
    {
      ++preview->team2Spawners;
    }
  }

  if (preview->slots.empty())
  {
    preview->warnings.push_back("No hero placeholders found in DB map objects");
    return false;
  }

  StrongMT<NWorld::IMapLoader> mapLoader = NWorld::CreatePWFillMapStartInfo(dbMapDescription.GetPtr());
  if (!IsValid(mapLoader))
  {
    preview->warnings.push_back("Failed to construct real map loader for selected map");
    return false;
  }

  lobby::TGameLineUp gameLineUp;
  BuildLinuxPreviewGameLineup(sessionPreview, localMatchPreview, &gameLineUp);
  std::vector<LinuxSyntheticClientInfo> clientInfos;
  BuildLinuxPreviewClientInfos(sessionPreview, localMatchPreview, defaultLocale, gameLineUp, &clientInfos);

  lobby::SGameParameters gameParams;
  gameParams.gameType = lobby::EGameType::Custom;
  gameParams.mapId = selectedEntry.descriptor.c_str();
  gameParams.maxPlayersPerTeam = mapLoader->GetMaxPlayersPerTeam();
  gameParams.randomSeed = static_cast<int>(time(0) & 0x7fffffff);
  gameParams.manoeuvresFaction = ConvertDisplayTeamToLobbyTeam(localMatchPreview.humanTeam);
  gameParams.hadPreGameLobby = true;
  gameParams.customGame = true;

  NCore::MapStartInfo mapStartInfo;
  if (!mapLoader->FillMapStartInfo(mapStartInfo, gameLineUp, gameParams))
  {
    preview->warnings.push_back("Real map loader failed to build MapStartInfo");
    return false;
  }
  for (size_t slotIndex = 0; slotIndex < mapStartInfo.playersInfo.size(); ++slotIndex)
  {
    NCore::PlayerStartInfo& player = mapStartInfo.playersInfo[slotIndex];
    if (player.playerType != NCore::EPlayerType::Human)
    {
      continue;
    }

    const LinuxSyntheticClientInfo* clientInfo = FindLinuxPreviewClientInfo(player.userID, clientInfos);
    if (!clientInfo)
    {
      continue;
    }

    MergeLinuxPreviewPlayerInfoMetadata(&player.playerInfo, clientInfo->info);
  }

  preview->builtMapStartInfo = true;
  preview->ready = true;
  preview->maxPlayersPerTeam = mapLoader->GetMaxPlayersPerTeam();
  preview->randomSeed = mapStartInfo.randomSeed;

  if (mapStartInfo.playersInfo.size() != preview->slots.size())
  {
    preview->warnings.push_back(
      NStr::StrFmt(
        "MapStartInfo slot count differs from DB hero placeholders: %lu vs %lu",
        static_cast<unsigned long>(mapStartInfo.playersInfo.size()),
        static_cast<unsigned long>(preview->slots.size())
      )
    );
  }

  std::vector<bool> usedLineupEntries(localMatchPreview.lineup.size(), false);
  const size_t assignableSlots = std::min(preview->slots.size(), static_cast<size_t>(mapStartInfo.playersInfo.size()));
  for (size_t slotIndex = 0; slotIndex < assignableSlots; ++slotIndex)
  {
    LinuxEngineMapStartSlot& slot = preview->slots[slotIndex];
    const NCore::PlayerStartInfo& player = mapStartInfo.playersInfo[slotIndex];

    slot.playerId = player.playerID;
    slot.originalTeam = ConvertCoreTeamToDisplayTeam(player.originalTeamID);
    slot.userId = player.userID;
    slot.filled = player.playerType != NCore::EPlayerType::Invalid;
    slot.human = player.playerType == NCore::EPlayerType::Human;
    slot.heroChecksum = player.playerInfo.heroId;
    slot.nickname = NStr::ToMBCS(player.nickname).c_str();
    slot.heroSkin = player.playerInfo.heroSkin.c_str();
    slot.heroLevel = player.playerInfo.heroLevel;
    slot.heroExp = player.playerInfo.heroExp;
    slot.heroRating = static_cast<int>(player.playerInfo.heroRating);
    slot.hasPremium = player.playerInfo.hasPremium;
    slot.isNovice = player.playerInfo.basket == NCore::EBasket::Newbie;
    slot.isAnimatedAvatar = player.playerInfo.isAnimatedAvatar;
    slot.partyId = player.playerInfo.partyId;
    slot.locale = player.playerInfo.locale.c_str();
    slot.flagId = player.playerInfo.flagId.c_str();
    slot.leagueIndex = player.playerInfo.leagueIndex;
    slot.ownLeaguePlace = player.playerInfo.ownLeaguePlace;
    slot.leaguePlaces.clear();
    for (size_t leaguePlaceIndex = 0; leaguePlaceIndex < player.playerInfo.leaguePlaces.size(); ++leaguePlaceIndex)
    {
      slot.leaguePlaces.push_back(player.playerInfo.leaguePlaces[leaguePlaceIndex]);
    }

    if (!slot.filled)
    {
      continue;
    }

    ++preview->assignedSlots;
    if (slot.human)
    {
      ++preview->humanPlayers;
    }
    else
    {
      ++preview->botPlayers;
    }

    const size_t heroIndex = FindHeroCatalogIndexByChecksum(heroCatalog, slot.heroChecksum);
    if (heroIndex != static_cast<size_t>(-1))
    {
      const LinuxHeroCatalogEntry& heroEntry = heroCatalog.entries[heroIndex];
      slot.heroId = ResolveHeroCatalogId(heroEntry);
      slot.heroTitle = heroEntry.title;
    }

    const size_t lineupIndex = MatchLocalLineupIndexForEngineSlot(
      localMatchPreview,
      slot.team,
      slot.human,
      slot.heroId,
      &usedLineupEntries
    );
    if (lineupIndex != static_cast<size_t>(-1))
    {
      usedLineupEntries[lineupIndex] = true;
      slot.lineupIndex = lineupIndex;
      slot.manualHero = localMatchPreview.lineup[lineupIndex].manualHero;
      if (slot.heroId.empty())
      {
        slot.heroId = localMatchPreview.lineup[lineupIndex].heroId;
      }
      if (slot.heroTitle.empty())
      {
        slot.heroTitle = localMatchPreview.lineup[lineupIndex].heroTitle;
      }
    }
    else if (slot.heroId.empty())
    {
      preview->warnings.push_back(
        NStr::StrFmt(
          "Failed to match engine slot %lu hero checksum %u back to local lineup",
          static_cast<unsigned long>(slotIndex),
          slot.heroChecksum
        )
      );
    }
  }

  if (localMatchPreview.lineup.size() > preview->assignedSlots)
  {
    preview->overflowPlayers = localMatchPreview.lineup.size() - preview->assignedSlots;
  }

  return true;
}

void ProbeEngineMapStartPreviewFromMapXml(
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxLocalMatchPreview& localMatchPreview,
  LinuxEngineMapStartPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxEngineMapStartPreview();
  if (mapCatalog.entries.empty() || mapBrowserState.selectedIndex >= mapCatalog.entries.size())
  {
    preview->warnings.push_back("Map browser selection unavailable");
    return;
  }

  preview->mapDescriptor = mapCatalog.entries[mapBrowserState.selectedIndex].descriptor;
  if (!selectedMapPreview.mapResolved || selectedMapPreview.mapFile.empty())
  {
    preview->warnings.push_back("Selected map file unavailable for engine slot preview");
    return;
  }

  std::string mapXml;
  if (!ReadTextFile(fs::path(selectedMapPreview.mapFile), &mapXml))
  {
    preview->warnings.push_back("Selected map XML unreadable for engine slot preview");
    return;
  }

  const size_t objectsPos = mapXml.find("<objects>");
  const size_t objectsEnd = mapXml.find("</objects>", objectsPos);
  if (objectsPos == std::string::npos || objectsEnd == std::string::npos)
  {
    preview->warnings.push_back("Map objects block missing");
    return;
  }

  const std::vector<std::string> itemBlocks = ExtractItemBlocks(mapXml.substr(objectsPos, objectsEnd - objectsPos));
  for (size_t i = 0; i < itemBlocks.size(); ++i)
  {
    const std::string& itemBlock = itemBlocks[i];
    const std::string href = ExtractTagHref(itemBlock, "gameObject", 0);
    const int team = DetectHeroPlaceholderTeam(href);
    if (team == 0)
    {
      continue;
    }

    LinuxEngineMapStartSlot slot;
    slot.spawnIndex = preview->slots.size();
    slot.team = team;
    slot.scriptName = ExtractTagValue(itemBlock, "scriptName", 0);
    const std::string offsetBlock = ExtractTagValue(itemBlock, "offset", 0);
    slot.translateX = static_cast<float>(atof(ExtractTagValue(offsetBlock, "translateX", 0).c_str()));
    slot.translateY = static_cast<float>(atof(ExtractTagValue(offsetBlock, "translateY", 0).c_str()));
    preview->slots.push_back(slot);
  }

  preview->totalSpawners = preview->slots.size();
  for (size_t i = 0; i < preview->slots.size(); ++i)
  {
    if (preview->slots[i].team == 1)
    {
      ++preview->team1Spawners;
    }
    else if (preview->slots[i].team == 2)
    {
      ++preview->team2Spawners;
    }
  }

  if (preview->slots.empty())
  {
    preview->warnings.push_back("No TeamA/TeamB hero placeholders found in map");
    return;
  }

  preview->ready = true;
  for (size_t lineupIndex = 0; lineupIndex < localMatchPreview.lineup.size(); ++lineupIndex)
  {
    const LinuxLocalMatchSlot& lineupSlot = localMatchPreview.lineup[lineupIndex];
    size_t firstFreeSlot = static_cast<size_t>(-1);
    size_t firstCorrectSlot = static_cast<size_t>(-1);

    for (size_t slotIndex = 0; slotIndex < preview->slots.size(); ++slotIndex)
    {
      LinuxEngineMapStartSlot& slot = preview->slots[slotIndex];
      if (slot.filled)
      {
        continue;
      }

      if (firstFreeSlot == static_cast<size_t>(-1))
      {
        firstFreeSlot = slotIndex;
      }

      if (slot.team == lineupSlot.team)
      {
        firstCorrectSlot = slotIndex;
        break;
      }
    }

    const size_t targetSlotIndex =
      firstCorrectSlot != static_cast<size_t>(-1) ? firstCorrectSlot : firstFreeSlot;
    if (targetSlotIndex == static_cast<size_t>(-1))
    {
      ++preview->overflowPlayers;
      continue;
    }

    LinuxEngineMapStartSlot& targetSlot = preview->slots[targetSlotIndex];
    targetSlot.filled = true;
    targetSlot.lineupIndex = lineupIndex;
    targetSlot.human = lineupSlot.human;
    targetSlot.manualHero = lineupSlot.manualHero;
    targetSlot.heroId = lineupSlot.heroId;
    targetSlot.heroTitle = lineupSlot.heroTitle;
    ++preview->assignedSlots;
  }

  if (preview->overflowPlayers > 0)
  {
    preview->warnings.push_back(
      NStr::StrFmt("Lineup overflowed hero placeholders by %lu player(s)", static_cast<unsigned long>(preview->overflowPlayers))
    );
  }
}

void ProbeEngineMapStartPreview(
  const LinuxSessionPreview& sessionPreview,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxLocalMatchPreview& localMatchPreview,
  const std::string& defaultLocale,
  LinuxEngineMapStartPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxEngineMapStartPreview();
  if (TryBuildEngineMapStartPreviewFromRealMapLoader(
        sessionPreview,
        heroCatalog,
        mapCatalog,
        mapBrowserState,
        localMatchPreview,
        defaultLocale,
        preview))
  {
    return;
  }

  std::vector<std::string> primaryWarnings = preview->warnings;
  ProbeEngineMapStartPreviewFromMapXml(
    selectedMapPreview,
    mapCatalog,
    mapBrowserState,
    localMatchPreview,
    preview
  );
  preview->source = preview->source.empty() ? "xml-fallback" : preview->source;
  preview->usedRealMapLoader = false;
  preview->builtMapStartInfo = false;
  preview->warnings.insert(preview->warnings.end(), primaryWarnings.begin(), primaryWarnings.end());
}

bool CompositeSelectedHeroAbilityIcons(
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  LinuxLoadingArtwork* artwork
);

bool CompositeSelectedHeroTalentGrid(
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  LinuxLoadingArtwork* artwork
);

bool BuildSelectedMapDisplayArtwork(
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxLoadingArtwork& fallbackArtwork,
  const LinuxLoadingUiPreview& loadingUiPreview,
  const LinuxLoadingUiState& loadingUiState,
  const LinuxSelectedMapPreview& preview,
  const LinuxArtworkSelectionState& artworkState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  LinuxLoadingArtwork* displayArtwork,
  std::string* displaySource
)
{
  if (!displayArtwork)
  {
    return false;
  }

  *displayArtwork = LinuxLoadingArtwork();
  if (displaySource)
  {
    *displaySource = "none";
  }

  LinuxLoadingArtwork composedArtwork;
  bool usingFallbackArtwork = false;
  const int requestedMode = artworkState.mode;
  const bool autoMode = requestedMode == LINUX_ARTWORK_AUTO;

  if ((requestedMode == LINUX_ARTWORK_MAP_BACK_WITH_LOGO || autoMode) && preview.loadingBack.artworkLoaded)
  {
    CopyArtwork(preview.loadingBack.artwork, &composedArtwork);
    if (preview.loadingLogo.artworkLoaded)
    {
      int logoMargin = composedArtwork.height / 18;
      if (logoMargin < 24)
      {
        logoMargin = 24;
      }

      const int logoX = (composedArtwork.width - preview.loadingLogo.artwork.width) / 2;
      int logoY = composedArtwork.height - preview.loadingLogo.artwork.height - logoMargin;
      if (logoY < 0)
      {
        logoY = 0;
      }
      CompositeArtwork(&composedArtwork, preview.loadingLogo.artwork, logoX, logoY);
    }
    if (displaySource)
    {
      *displaySource = preview.loadingLogo.artworkLoaded ? "selected map back+logo" : "selected map back";
    }
  }
  else if (requestedMode == LINUX_ARTWORK_MAP_BACK && preview.loadingBack.artworkLoaded)
  {
    CopyArtwork(preview.loadingBack.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected map back";
    }
  }
  else if (requestedMode == LINUX_ARTWORK_MAP_LOGO && preview.loadingLogo.artworkLoaded)
  {
    CopyArtwork(preview.loadingLogo.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected map logo";
    }
  }
  else if (requestedMode == LINUX_ARTWORK_MINIMAP_FIRST && preview.minimapFirst.artworkLoaded)
  {
    CopyArtwork(preview.minimapFirst.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected minimap first";
    }
  }
  else if (requestedMode == LINUX_ARTWORK_MINIMAP_SECOND && preview.minimapSecond.artworkLoaded)
  {
    CopyArtwork(preview.minimapSecond.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected minimap second";
    }
  }
  else if (requestedMode == LINUX_ARTWORK_MINIMAP_NEUTRAL && preview.minimapNeutral.artworkLoaded)
  {
    CopyArtwork(preview.minimapNeutral.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected minimap neutral";
    }
  }
  else if ((requestedMode == LINUX_ARTWORK_LOADING || autoMode) && fallbackArtwork.ready)
  {
    CopyArtwork(fallbackArtwork, &composedArtwork);
    usingFallbackArtwork = true;
    if (displaySource)
    {
      *displaySource = "localized loading artwork";
    }
  }
  else if (preview.loadingBack.artworkLoaded)
  {
    CopyArtwork(preview.loadingBack.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected map back (fallback)";
    }
  }
  else if (preview.loadingLogo.artworkLoaded)
  {
    CopyArtwork(preview.loadingLogo.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected map logo (fallback)";
    }
  }
  else if (preview.minimapFirst.artworkLoaded)
  {
    CopyArtwork(preview.minimapFirst.artwork, &composedArtwork);
    if (displaySource)
    {
      *displaySource = "selected minimap first (fallback)";
    }
  }
  else if (fallbackArtwork.ready)
  {
    CopyArtwork(fallbackArtwork, &composedArtwork);
    usingFallbackArtwork = true;
    if (displaySource)
    {
      *displaySource = "localized loading artwork (fallback)";
    }
  }
  else
  {
    return false;
  }

  const int maxWidth = settings.width > 48 ? static_cast<int>(settings.width - 48) : static_cast<int>(settings.width);
  const int maxHeight = settings.height > 260 ? static_cast<int>(settings.height / 2) : static_cast<int>(settings.height);
  ScaleArtworkToFit(composedArtwork, maxWidth, maxHeight, displayArtwork);
  if (displayArtwork->ready)
  {
    CompositeTacticalMapInset(preview, localMatchPreview, engineMapStartPreview, displayArtwork);
    if (displaySource && !displaySource->empty() && preview.tactical.ready)
    {
      *displaySource += " + tactical inset";
    }
  }
  if (displayArtwork->ready &&
      CompositeLocalMatchPortraits(environment, heroCatalog, localMatchPreview, displayArtwork) &&
      displaySource && !displaySource->empty())
  {
    *displaySource += " + lineup portraits";
  }

  if (displayArtwork->ready &&
      CompositeSelectedHeroAbilityIcons(selectedHeroPreview, displayArtwork) &&
      displaySource && !displaySource->empty())
  {
    *displaySource += " + hero abilities";
  }

  if (displayArtwork->ready &&
      CompositeSelectedHeroTalentGrid(selectedHeroPreview, displayArtwork) &&
      displaySource && !displaySource->empty())
  {
    *displaySource += " + hero talents";
  }

  if (displayArtwork->ready)
  {
    CompositeLoadingUiAssets(environment, preview, loadingUiPreview, loadingUiState, displayArtwork);
    if (displaySource && !displaySource->empty())
    {
      *displaySource += " + loading ui assets";
    }
  }

  if (!displayArtwork->ready && usingFallbackArtwork)
  {
    return false;
  }

  return displayArtwork->ready;
}

void MoveMapSelection(
  const LinuxMapCatalog& catalog,
  LinuxMapBrowserState* browser,
  int delta,
  const char* source
)
{
  if (catalog.entries.empty() || !browser || delta == 0)
  {
    return;
  }

  int nextIndex = static_cast<int>(browser->selectedIndex) + delta;
  if (nextIndex < 0)
  {
    nextIndex = 0;
  }
  if (nextIndex >= static_cast<int>(catalog.entries.size()))
  {
    nextIndex = static_cast<int>(catalog.entries.size()) - 1;
  }

  const size_t normalizedIndex = static_cast<size_t>(nextIndex);
  if (normalizedIndex != browser->selectedIndex)
  {
    browser->selectedIndex = normalizedIndex;
    ++browser->selectionChanges;
    browser->selectionSource = source ? source : "runtime";
  }
}

void SelectAbsoluteMapIndex(
  const LinuxMapCatalog& catalog,
  LinuxMapBrowserState* browser,
  size_t index,
  const char* source
)
{
  if (catalog.entries.empty() || !browser)
  {
    return;
  }

  const size_t clampedIndex = index < catalog.entries.size() ? index : catalog.entries.size() - 1;
  if (browser->selectedIndex != clampedIndex)
  {
    browser->selectedIndex = clampedIndex;
    ++browser->selectionChanges;
    browser->selectionSource = source ? source : "runtime";
  }
}

size_t FindMapCatalogIndex(const LinuxMapCatalog& catalog, const std::string& selector)
{
  const std::string selectorLower = ToAsciiLower(TrimAscii(selector));
  if (selectorLower.empty())
  {
    return static_cast<size_t>(-1);
  }

  const std::string normalizedSelector = NormalizeMapSelector(selector);
  for (size_t i = 0; i < catalog.entries.size(); ++i)
  {
    const LinuxMapCatalogEntry& entry = catalog.entries[i];
    const std::string descriptorLower = ToAsciiLower(entry.descriptor);
    if (descriptorLower.find(selectorLower) != std::string::npos ||
        NormalizeMapSelector(entry.descriptor).find(normalizedSelector) != std::string::npos ||
        ToAsciiLower(entry.title).find(selectorLower) != std::string::npos)
    {
      return i;
    }
  }

  return static_cast<size_t>(-1);
}

void InitializeMapBrowserState(
  const LinuxClientLaunchSettings& settings,
  const LinuxMapCatalog& catalog,
  LinuxMapBrowserState* browser
)
{
  if (!browser || catalog.entries.empty())
  {
    return;
  }

  browser->selectedIndex = 0;
  browser->selectionChanges = 0;
  browser->selectionSource = "default";

  if (settings.mapSelector.empty())
  {
    return;
  }

  const size_t selectedIndex = FindMapCatalogIndex(catalog, settings.mapSelector);
  if (selectedIndex != static_cast<size_t>(-1))
  {
    browser->selectedIndex = selectedIndex;
    browser->selectionSource = "command-line";
    return;
  }

  browser->selectionSource = "command-line-miss";
}

void ApplySessionSelections(
  LinuxSessionPreview* sessionPreview,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxMapCatalog& mapCatalog,
  LinuxMapBrowserState* mapBrowserState,
  LinuxLocalMatchPreview* localMatchPreview
)
{
  if (!sessionPreview || !sessionPreview->valid || !mapBrowserState || !localMatchPreview)
  {
    return;
  }

  if (sessionPreview->mapIdProvided)
  {
    const size_t sessionMapIndex = FindMapCatalogIndex(mapCatalog, sessionPreview->mapId);
    if (sessionMapIndex != static_cast<size_t>(-1))
    {
      if (mapBrowserState->selectedIndex != sessionMapIndex)
      {
        mapBrowserState->selectedIndex = sessionMapIndex;
        ++mapBrowserState->selectionChanges;
      }
      mapBrowserState->selectionSource = "session-json";
    }
    else
    {
      sessionPreview->warnings.push_back("Session map selector not found: " + sessionPreview->mapId);
    }
  }

  if (heroCatalog.entries.empty() || mapCatalog.entries.empty() || mapBrowserState->selectedIndex >= mapCatalog.entries.size())
  {
    return;
  }

  if (sessionPreview->currentTeamId == 1 || sessionPreview->currentTeamId == 2)
  {
    localMatchPreview->humanTeam = sessionPreview->currentTeamId;
  }
  else
  {
    sessionPreview->warnings.push_back("Session current player team is missing or invalid");
  }

  if (!sessionPreview->currentHeroPersistentId.empty())
  {
    const size_t heroIndex = FindHeroCatalogIndex(heroCatalog, sessionPreview->currentHeroPersistentId);
    if (heroIndex != static_cast<size_t>(-1))
    {
      localMatchPreview->selectedHeroIndex = heroIndex;
    }
    else
    {
      sessionPreview->warnings.push_back(
        "Session hero is not present in the legal hero catalog: " + sessionPreview->currentHeroPersistentId
      );
    }
  }

  const size_t maxTeamSize = ResolveSelectedMapMaxTeamSize(mapCatalog, *mapBrowserState);
  const size_t team1Count = CountSessionTeamPlayers(*sessionPreview, 1);
  const size_t team2Count = CountSessionTeamPlayers(*sessionPreview, 2);
  const size_t requestedTeamSize = std::max(std::max(team1Count, team2Count), static_cast<size_t>(1));
  localMatchPreview->requestedTeamSize = std::min(requestedTeamSize, maxTeamSize);
  if (requestedTeamSize > maxTeamSize)
  {
    sessionPreview->warnings.push_back(
      std::string("Session team size exceeds selected map limit: ") +
      NStr::StrFmt("%lu > %lu", static_cast<unsigned long>(requestedTeamSize), static_cast<unsigned long>(maxTeamSize))
    );
  }

  const size_t totalSlots = std::max(localMatchPreview->requestedTeamSize, static_cast<size_t>(1)) * 2;
  localMatchPreview->slotHeroOverrides.assign(totalSlots, static_cast<size_t>(-1));

  std::vector<size_t> team1Heroes;
  std::vector<size_t> team2Heroes;
  for (size_t i = 0; i < sessionPreview->players.size(); ++i)
  {
    const LinuxSessionPlayerPreview& player = sessionPreview->players[i];
    if (player.currentPlayer)
    {
      continue;
    }

    if (player.teamId != 1 && player.teamId != 2)
    {
      continue;
    }

    if (player.heroPersistentId.empty())
    {
      continue;
    }

    const size_t heroIndex = FindHeroCatalogIndex(heroCatalog, player.heroPersistentId);
    if (heroIndex == static_cast<size_t>(-1))
    {
      sessionPreview->warnings.push_back(
        "Session teammate hero is not present in the legal hero catalog: " + player.heroPersistentId
      );
      continue;
    }

    if (player.teamId == 1)
    {
      team1Heroes.push_back(heroIndex);
    }
    else
    {
      team2Heroes.push_back(heroIndex);
    }
  }

  const size_t teamSize = std::max(localMatchPreview->requestedTeamSize, static_cast<size_t>(1));
  if (localMatchPreview->humanTeam == 1)
  {
    for (size_t i = 0; i < team1Heroes.size() && i + 1 < teamSize; ++i)
    {
      localMatchPreview->slotHeroOverrides[i + 1] = team1Heroes[i];
    }
    for (size_t i = 0; i < team2Heroes.size() && teamSize + i < totalSlots; ++i)
    {
      localMatchPreview->slotHeroOverrides[teamSize + i] = team2Heroes[i];
    }
  }
  else
  {
    for (size_t i = 0; i < team1Heroes.size() && i < teamSize; ++i)
    {
      localMatchPreview->slotHeroOverrides[i] = team1Heroes[i];
    }
    for (size_t i = 0; i < team2Heroes.size() && teamSize + i + 1 < totalSlots; ++i)
    {
      localMatchPreview->slotHeroOverrides[teamSize + i + 1] = team2Heroes[i];
    }
  }

  RegenerateLocalMatchPreview(heroCatalog, mapCatalog, *mapBrowserState, localMatchPreview, "session-json");
  localMatchPreview->selectedSlotIndex = ResolveHumanSlotIndex(*localMatchPreview);
}

std::string NormalizeRootFileSystemPath(std::string fileName)
{
  while (!fileName.empty() && (fileName[0] == '/' || fileName[0] == '\\'))
  {
    fileName.erase(fileName.begin());
  }

  return fileName;
}

bool ReadRootFileBytes(const string& fileName, std::string* content)
{
  CObj<Stream> stream = RootFileSystem::OpenFile(fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
  if (!IsValid(stream) || !stream->IsOk())
  {
    return false;
  }

  content->resize(stream->GetSize());
  if (!content->empty())
  {
    stream->Read(&(*content)[0], content->size());
  }

  return true;
}

std::vector<std::string> ExtractItemBlocks(const std::string& text)
{
  std::vector<std::string> items;
  size_t searchPos = 0;
  while (true)
  {
    const size_t itemPos = text.find("<Item>", searchPos);
    if (itemPos == std::string::npos)
    {
      break;
    }

    const size_t itemEnd = text.find("</Item>", itemPos);
    if (itemEnd == std::string::npos)
    {
      break;
    }

    items.push_back(text.substr(itemPos, itemEnd - itemPos));
    searchPos = itemEnd + strlen("</Item>");
  }

  return items;
}

bool ReadRootFileUtf16Text(const string& fileName, std::string* text)
{
  std::string content;
  if (!ReadRootFileBytes(fileName, &content) || content.size() < 2)
  {
    return false;
  }

  const unsigned char bom0 = static_cast<unsigned char>(content[0]);
  const unsigned char bom1 = static_cast<unsigned char>(content[1]);
  if (bom0 != 0xFF || bom1 != 0xFE)
  {
    return false;
  }

  wstring wideText;
  wideText.reserve((content.size() - 2) / 2);

  for (size_t i = 2; i + 1 < content.size(); i += 2)
  {
    const unsigned short codeUnit =
      static_cast<unsigned short>(static_cast<unsigned char>(content[i])) |
      (static_cast<unsigned short>(static_cast<unsigned char>(content[i + 1])) << 8);

    if (codeUnit == 0)
    {
      continue;
    }

    wideText.push_back(static_cast<wchar_t>(codeUnit));
  }

  string utf8Text;
  NStr::UnicodeToUTF8(&utf8Text, wideText);
  *text = SanitizeLocalizedText(std::string(utf8Text.c_str()));
  return !text->empty();
}

std::string ToStdString(const nstl::string& value)
{
  return std::string(value.c_str());
}

std::string ReadDbLocalizedText(const CTextRef& textRef)
{
  const std::string directText = SanitizeLocalizedText(std::string(NStr::ToMBCS(textRef.GetText()).c_str()));
  if (!directText.empty())
  {
    return directText;
  }

  const string source = textRef.GetSource();
  if (source.empty())
  {
    return directText;
  }

  std::string fallbackText;
  if (ReadRootFileUtf16Text(source, &fallbackText))
  {
    return fallbackText;
  }

  const string normalizedSource = NormalizeDataRefPath(std::string(source.c_str())).c_str();
  if (normalizedSource != source && ReadRootFileUtf16Text(normalizedSource, &fallbackText))
  {
    return fallbackText;
  }

  return directText;
}

template <typename T>
std::string DescribeDbResource(const NDb::Ptr<T>& resource)
{
  return resource ? ToStdString(resource->GetDBID().GetFormatted()) : std::string();
}

bool HasFmodEvent(const NDb::DBFMODEventDesc& eventDesc)
{
  return !eventDesc.projectName.empty() || !eventDesc.groupName.empty() || !eventDesc.eventName.empty();
}

std::string DescribeFmodEvent(const NDb::DBFMODEventDesc& eventDesc)
{
  std::string description;
  if (!eventDesc.projectName.empty())
  {
    description += ToStdString(eventDesc.projectName);
  }
  if (!eventDesc.groupName.empty())
  {
    if (!description.empty())
    {
      description += "/";
    }
    description += ToStdString(eventDesc.groupName);
  }
  if (!eventDesc.eventName.empty())
  {
    if (!description.empty())
    {
      description += "/";
    }
    description += ToStdString(eventDesc.eventName);
  }
  return description;
}

template <typename T>
void AppendSampleValue(std::vector<std::string>* samples, const T& value, size_t limit)
{
  if (!samples)
  {
    return;
  }

  const std::string normalized = TrimAscii(std::string(value.c_str()));
  if (normalized.empty())
  {
    return;
  }

  if (samples->size() >= limit)
  {
    return;
  }

  for (size_t i = 0; i < samples->size(); ++i)
  {
    if ((*samples)[i] == normalized)
    {
      return;
    }
  }

  samples->push_back(normalized);
}

std::string JoinPreviewSamples(const std::vector<std::string>& samples)
{
  std::string joined;
  for (size_t i = 0; i < samples.size(); ++i)
  {
    if (!joined.empty())
    {
      joined += ", ";
    }
    joined += samples[i];
  }
  return joined;
}

bool IsMatchingSessionRootHero(const NDb::Hero& hero, const LinuxHeroCatalogEntry& entry)
{
  const std::string selectedId = ToAsciiLower(ResolveHeroCatalogId(entry));
  const std::string selectedRawId = ToAsciiLower(entry.id);
  const std::string selectedTitle = ToAsciiLower(entry.title);
  const std::string selectedAlternateTitle = ToAsciiLower(entry.alternateTitle);
  const std::string heroPersistentId = ToAsciiLower(ToStdString(hero.persistentId));
  const std::string heroId = ToAsciiLower(ToStdString(hero.id));

  if (!selectedId.empty() && (selectedId == heroPersistentId || selectedId == heroId))
  {
    return true;
  }

  if (!selectedRawId.empty() && (selectedRawId == heroPersistentId || selectedRawId == heroId))
  {
    return true;
  }

  std::string heroTitle = ToAsciiLower(ReadDbLocalizedText(hero.heroNameA));
  if (heroTitle.empty())
  {
    heroTitle = ToAsciiLower(ReadDbLocalizedText(hero.heroNameB));
  }

  return (!selectedTitle.empty() && selectedTitle == heroTitle) ||
    (!selectedAlternateTitle.empty() && selectedAlternateTitle == heroTitle);
}

void AddHeroAbilityPreview(
  const LinuxClientEnvironment& environment,
  const NDb::Ability* ability,
  bool isAttack,
  bool countAsHeroAbility,
  LinuxSelectedHeroDbPreview* preview
)
{
  if (!preview || !ability)
  {
    return;
  }

  if (countAsHeroAbility)
  {
    ++preview->abilityCount;

    switch (ability->type)
    {
      case NDb::ABILITYTYPE_ACTIVE:
      case NDb::ABILITYTYPE_MULTIACTIVE:
      case NDb::ABILITYTYPE_SIMPLE:
        ++preview->activeAbilityCount;
        break;

      case NDb::ABILITYTYPE_PASSIVE:
        ++preview->passiveAbilityCount;
        break;

      case NDb::ABILITYTYPE_AUTOCASTABLE:
      case NDb::ABILITYTYPE_SWITCHABLE:
        ++preview->autocastAbilityCount;
        break;

      case NDb::ABILITYTYPE_CHANNELLING:
        ++preview->channellingAbilityCount;
        break;

      default:
        break;
    }
  }

  if (preview->featuredAbilities.size() >= 4)
  {
    return;
  }

  LinuxHeroAbilityPreview abilityPreview;
  abilityPreview.dbid = ToStdString(ability->GetDBID().GetFormatted());
  abilityPreview.name = ReadDbLocalizedText(ability->name);
  if (abilityPreview.name.empty())
  {
    const std::string dbFileName = ToStdString(ability->GetDBID().GetFileName());
    abilityPreview.name = fs::path(NormalizeDataRefPath(dbFileName)).stem().string();
  }

  abilityPreview.description = ReadDbLocalizedText(ability->shortDescription);
  if (abilityPreview.description.empty())
  {
    abilityPreview.description = ReadDbLocalizedText(ability->description);
  }

  abilityPreview.type = NDb::EnumToString(ability->type);
  abilityPreview.isAttack = isAttack;

  const std::string iconRef = DescribeDbResource(ability->image);
  if (!iconRef.empty())
  {
    ProbeTextureAsset(environment, iconRef, &abilityPreview.icon);
  }

  preview->featuredAbilities.push_back(abilityPreview);
}

void AddHeroTalentPreview(
  const LinuxClientEnvironment& environment,
  const NDb::Talent* talent,
  size_t levelIndex,
  size_t slotIndex,
  NDb::ETalentSlotStatus status,
  LinuxSelectedHeroDbPreview* preview
)
{
  if (!preview || !talent)
  {
    return;
  }

  if (preview->defaultTalentPreviews.size() >= 24)
  {
    return;
  }

  LinuxHeroTalentPreview talentPreview;
  talentPreview.levelIndex = levelIndex;
  talentPreview.slotIndex = slotIndex;
  talentPreview.dbid = ToStdString(talent->GetDBID().GetFormatted());
  talentPreview.persistentId = ToStdString(talent->persistentId);
  talentPreview.name = ReadDbLocalizedText(talent->name);
  if (talentPreview.name.empty())
  {
    const std::string dbFileName = ToStdString(talent->GetDBID().GetFileName());
    talentPreview.name = fs::path(NormalizeDataRefPath(dbFileName)).stem().string();
  }

  talentPreview.description = ReadDbLocalizedText(talent->shortDescription);
  if (talentPreview.description.empty())
  {
    talentPreview.description = ReadDbLocalizedText(talent->description);
  }

  talentPreview.rarity = NDb::EnumToString(talent->rarity);
  talentPreview.status = NDb::EnumToString(status);
  talentPreview.locked = status != NDb::TALENTSLOTSTATUS_NORMAL;

  const std::string iconRef = DescribeDbResource(talent->image);
  if (!iconRef.empty())
  {
    ProbeTextureAsset(environment, iconRef, &talentPreview.icon);
    if (talentPreview.icon.artworkLoaded)
    {
      ++preview->defaultTalentIconCount;
    }
  }

  preview->defaultTalentPreviews.push_back(talentPreview);
}

void ProbeSelectedHeroDbPreview(
  const LinuxClientEnvironment& environment,
  const LinuxSessionRootPreview& sessionRootPreview,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  LinuxSelectedHeroDbPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  *preview = LinuxSelectedHeroDbPreview();

  const size_t selectedHeroIndex = ResolveSelectedHeroCatalogIndex(heroCatalog, localMatchPreview);
  if (selectedHeroIndex == static_cast<size_t>(-1))
  {
    preview->warnings.push_back("Selected hero was not resolved from the local lineup");
    return;
  }

  const LinuxHeroCatalogEntry& selectedEntry = heroCatalog.entries[selectedHeroIndex];
  preview->ready = true;
  preview->persistentId = ResolveHeroCatalogId(selectedEntry);
  preview->title = selectedEntry.title;
  preview->description = selectedEntry.description;

  if (!sessionRootPreview.ready || !sessionRootPreview.logicRootReady || !sessionRootPreview.heroesDbReady)
  {
    preview->warnings.push_back("SessionRoot hero DB is unavailable");
    return;
  }

  const NDb::Ptr<NDb::SessionRoot>& sessionRoot = NDb::SessionRoot::GetRoot();
  if (!sessionRoot || !sessionRoot->logicRoot || !sessionRoot->logicRoot->heroes)
  {
    preview->warnings.push_back("SessionRoot::GetRoot() returned no hero list");
    return;
  }

  NDb::Ptr<NDb::Hero> hero;
  const vector<NDb::Ptr<NDb::Hero>>& heroes = sessionRoot->logicRoot->heroes->heroes;
  for (int i = 0; i < heroes.size(); ++i)
  {
    if (heroes[i] && IsMatchingSessionRootHero(*heroes[i], selectedEntry))
    {
      hero = heroes[i];
      break;
    }
  }

  if (!hero)
  {
    preview->warnings.push_back("Selected hero was not found in SessionRoot->logicRoot->heroes");
    return;
  }

  preview->found = true;
  preview->dbid = ToStdString(hero->GetDBID().GetFormatted());
  const std::string heroPersistentId = ToStdString(hero->persistentId);
  if (!heroPersistentId.empty())
  {
    preview->persistentId = heroPersistentId;
  }
  preview->heroRace = NDb::EnumToString(hero->heroRace);

  const std::string heroTitle = ReadDbLocalizedText(hero->heroNameA);
  const std::string heroAlternateTitle = ReadDbLocalizedText(hero->heroNameB);
  const std::string heroDescription = ReadDbLocalizedText(hero->description);
  if (!heroTitle.empty())
  {
    preview->title = heroTitle;
  }
  else if (!heroAlternateTitle.empty())
  {
    preview->title = heroAlternateTitle;
  }
  if (!heroDescription.empty())
  {
    preview->description = heroDescription;
  }

  preview->sceneObjectCount = hero->heroSceneObjects.size();
  preview->summonedUnitGroupCount = hero->summonedUnits.size();
  preview->skinCount = hero->heroSkins.size();
  preview->recommendedStatCount = hero->recommendedStats.size();

  if (hero->heroImageA)
  {
    ProbeTextureAsset(environment, DescribeDbResource(hero->heroImageA), &preview->portrait);
  }
  else if (hero->heroImageB)
  {
    ProbeTextureAsset(environment, DescribeDbResource(hero->heroImageB), &preview->portrait);
  }
  else if (hero->image)
  {
    ProbeTextureAsset(environment, DescribeDbResource(hero->image), &preview->portrait);
  }

  if (hero->attackAbility)
  {
    preview->attackReady = true;
    preview->attackAbilityDbid = DescribeDbResource(hero->attackAbility);
    preview->attackAbilityName = ReadDbLocalizedText(hero->attackAbility->name);
    if (preview->attackAbilityName.empty())
    {
      preview->attackAbilityName = fs::path(
        NormalizeDataRefPath(ToStdString(hero->attackAbility->GetDBID().GetFileName()))).stem().string();
    }

    AddHeroAbilityPreview(environment, hero->attackAbility.GetPtr(), true, false, preview);
  }

  for (int i = 0; i < hero->abilities.size(); ++i)
  {
    AddHeroAbilityPreview(environment, hero->abilities[i].GetPtr(), false, true, preview);
  }

  if (hero->stats)
  {
    preview->statsReady = true;
    preview->statsCount = hero->stats->stats.size();
    for (int i = 0; i < hero->stats->stats.size(); ++i)
    {
      const NDb::UnitStat& stat = hero->stats->stats[i];
      AppendSampleValue(&preview->statSamples, std::string(NDb::EnumToString(stat.statId)), 6);
      preview->levelUpgradeCount += stat.levelUpgrades.size();
    }
  }

  if (hero->targetingParams)
  {
    preview->targetingReady = true;
    preview->targetingRange = hero->targetingParams->targetingRange;
    preview->chaseRange = hero->targetingParams->chaseRange;
    preview->aggroRange = hero->targetingParams->aggroRange;
  }

  if (hero->uniqueResource)
  {
    preview->uniqueResourceReady = true;
    preview->uniqueResourceName = ReadDbLocalizedText(hero->uniqueResource->name);
    preview->uniqueResourceTooltip = ReadDbLocalizedText(hero->uniqueResource->tooltip);
  }

  for (int i = 0; i < hero->recommendedStats.size(); ++i)
  {
    AppendSampleValue(&preview->recommendedStatSamples, std::string(NDb::EnumToString(hero->recommendedStats[i])), 4);
  }

  preview->defaultTalentSetCount = hero->defaultTalentsSets.size();
  for (int setIndex = 0; setIndex < hero->defaultTalentsSets.size(); ++setIndex)
  {
    const NDb::Ptr<NDb::TalentsSet>& talentsSet = hero->defaultTalentsSets[setIndex];
    if (!talentsSet)
    {
      continue;
    }

    ++preview->defaultTalentReadyCount;
    preview->defaultTalentLevelCount += talentsSet->levels.size();
    for (int levelIndex = 0; levelIndex < talentsSet->levels.size(); ++levelIndex)
    {
      const NDb::TalentsLevel& level = talentsSet->levels[levelIndex];
      preview->defaultTalentSlotCount += level.talents.size();
      for (int slotIndex = 0; slotIndex < level.talents.size(); ++slotIndex)
      {
        if (!level.talents[slotIndex].talent)
        {
          continue;
        }

        const std::string talentName = ReadDbLocalizedText(level.talents[slotIndex].talent->name);
        AppendSampleValue(&preview->talentSamples, talentName, 5);
        AddHeroTalentPreview(
          environment,
          level.talents[slotIndex].talent.GetPtr(),
          static_cast<size_t>(levelIndex),
          static_cast<size_t>(slotIndex),
          level.talents[slotIndex].status,
          preview
        );
      }
    }
  }
}

void ResolveAbilityPreviewAccent(
  const LinuxHeroAbilityPreview& abilityPreview,
  unsigned char* red,
  unsigned char* green,
  unsigned char* blue
)
{
  if (!red || !green || !blue)
  {
    return;
  }

  if (abilityPreview.isAttack)
  {
    *red = 236;
    *green = 188;
    *blue = 78;
    return;
  }

  const std::string type = ToAsciiLower(abilityPreview.type);
  if (type.find("passive") != std::string::npos)
  {
    *red = 92;
    *green = 176;
    *blue = 112;
    return;
  }
  if (type.find("autocast") != std::string::npos || type.find("switchable") != std::string::npos)
  {
    *red = 224;
    *green = 144;
    *blue = 76;
    return;
  }
  if (type.find("channelling") != std::string::npos)
  {
    *red = 148;
    *green = 112;
    *blue = 216;
    return;
  }

  *red = 88;
  *green = 156;
  *blue = 224;
}

void ResolveTalentPreviewAccent(
  const LinuxHeroTalentPreview& talentPreview,
  unsigned char* red,
  unsigned char* green,
  unsigned char* blue
)
{
  if (!red || !green || !blue)
  {
    return;
  }

  if (talentPreview.locked)
  {
    *red = 132;
    *green = 132;
    *blue = 132;
    return;
  }

  const std::string rarity = ToAsciiLower(talentPreview.rarity);
  if (rarity.find("excellent") != std::string::npos || rarity.find("outstanding") != std::string::npos)
  {
    *red = 230;
    *green = 122;
    *blue = 74;
    return;
  }
  if (rarity.find("magnificent") != std::string::npos || rarity.find("exclusive") != std::string::npos)
  {
    *red = 216;
    *green = 188;
    *blue = 84;
    return;
  }
  if (rarity.find("good") != std::string::npos)
  {
    *red = 92;
    *green = 176;
    *blue = 112;
    return;
  }

  *red = 88;
  *green = 156;
  *blue = 224;
}

bool CompositeSelectedHeroAbilityIcons(
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  LinuxLoadingArtwork* artwork
)
{
  if (!artwork || !artwork->ready || selectedHeroPreview.featuredAbilities.empty())
  {
    return false;
  }

  std::vector<const LinuxHeroAbilityPreview*> visibleAbilities;
  for (size_t i = 0; i < selectedHeroPreview.featuredAbilities.size(); ++i)
  {
    if (selectedHeroPreview.featuredAbilities[i].icon.artworkLoaded)
    {
      visibleAbilities.push_back(&selectedHeroPreview.featuredAbilities[i]);
    }
  }

  if (visibleAbilities.empty())
  {
    return false;
  }

  const int gap = std::max(6, std::min(12, artwork->width / 96));
  const int iconSize = std::max(
    36,
    std::min(
      58,
      std::min(
        (artwork->width - 80 - static_cast<int>((visibleAbilities.size() - 1) * gap)) /
          std::max(static_cast<int>(visibleAbilities.size()), 1),
        artwork->height / 6
      )
    )
  );
  const int panelPadding = std::max(6, iconSize / 7);
  const int totalWidth = static_cast<int>(visibleAbilities.size()) * iconSize +
    static_cast<int>(visibleAbilities.size() > 0 ? (visibleAbilities.size() - 1) * gap : 0);
  const int panelWidth = totalWidth + panelPadding * 2;
  const int panelHeight = iconSize + panelPadding * 2;
  const int panelX = std::max(16, (artwork->width - panelWidth) / 2);
  const int panelY = std::max(18, artwork->height - panelHeight - 22);

  FillArtworkRect(artwork, panelX, panelY, panelWidth, panelHeight, 14, 20, 28, 188);
  StrokeArtworkRect(artwork, panelX, panelY, panelWidth, panelHeight, 2, 241, 229, 211, 232);

  for (size_t i = 0; i < visibleAbilities.size(); ++i)
  {
    const LinuxHeroAbilityPreview& abilityPreview = *visibleAbilities[i];
    const int slotX = panelX + panelPadding + static_cast<int>(i) * (iconSize + gap);
    const int slotY = panelY + panelPadding;

    unsigned char red = 88;
    unsigned char green = 156;
    unsigned char blue = 224;
    ResolveAbilityPreviewAccent(abilityPreview, &red, &green, &blue);

    FillArtworkRect(artwork, slotX, slotY, iconSize, iconSize, 24, 32, 42, 208);
    StrokeArtworkRect(artwork, slotX, slotY, iconSize, iconSize, 2, red, green, blue, 240);

    LinuxLoadingArtwork scaledIcon;
    ScaleArtworkToFit(abilityPreview.icon.artwork, iconSize - 8, iconSize - 8, &scaledIcon);
    if (scaledIcon.ready)
    {
      const int iconX = slotX + (iconSize - scaledIcon.width) / 2;
      const int iconY = slotY + (iconSize - scaledIcon.height) / 2;
      CompositeArtwork(artwork, scaledIcon, iconX, iconY);
    }
  }

  return true;
}

bool CompositeSelectedHeroTalentGrid(
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  LinuxLoadingArtwork* artwork
)
{
  if (!artwork || !artwork->ready || selectedHeroPreview.defaultTalentPreviews.empty())
  {
    return false;
  }

  std::vector<const LinuxHeroTalentPreview*> visibleTalents;
  size_t maxLevelIndex = 0;
  size_t maxSlotIndex = 0;
  for (size_t i = 0; i < selectedHeroPreview.defaultTalentPreviews.size(); ++i)
  {
    const LinuxHeroTalentPreview& talentPreview = selectedHeroPreview.defaultTalentPreviews[i];
    if (!talentPreview.icon.artworkLoaded)
    {
      continue;
    }

    visibleTalents.push_back(&talentPreview);
    maxLevelIndex = std::max(maxLevelIndex, talentPreview.levelIndex);
    maxSlotIndex = std::max(maxSlotIndex, talentPreview.slotIndex);
  }

  if (visibleTalents.empty())
  {
    return false;
  }

  const int rows = std::max(1, static_cast<int>(maxLevelIndex + 1));
  const int columns = std::max(1, static_cast<int>(maxSlotIndex + 1));
  const int gap = std::max(2, std::min(5, artwork->width / 220));
  const int iconSize = std::max(
    18,
    std::min(
      26,
      std::min(
        (artwork->width - 48 - std::max(0, columns - 1) * gap) / std::max(columns, 1),
        (artwork->height - 48 - std::max(0, rows - 1) * gap) / std::max(rows, 1)
      )
    )
  );
  const int panelPadding = std::max(4, iconSize / 5);
  const int panelWidth = columns * iconSize + std::max(0, columns - 1) * gap + panelPadding * 2;
  const int panelHeight = rows * iconSize + std::max(0, rows - 1) * gap + panelPadding * 2;
  const int panelX = std::max(12, artwork->width - panelWidth - 16);
  const int panelY = 14;

  FillArtworkRect(artwork, panelX, panelY, panelWidth, panelHeight, 14, 18, 26, 196);
  StrokeArtworkRect(artwork, panelX, panelY, panelWidth, panelHeight, 2, 230, 221, 204, 224);

  for (size_t i = 0; i < visibleTalents.size(); ++i)
  {
    const LinuxHeroTalentPreview& talentPreview = *visibleTalents[i];
    const int slotX = panelX + panelPadding + static_cast<int>(talentPreview.slotIndex) * (iconSize + gap);
    const int slotY = panelY + panelPadding + static_cast<int>(talentPreview.levelIndex) * (iconSize + gap);

    unsigned char red = 88;
    unsigned char green = 156;
    unsigned char blue = 224;
    ResolveTalentPreviewAccent(talentPreview, &red, &green, &blue);

    FillArtworkRect(artwork, slotX, slotY, iconSize, iconSize, 22, 28, 38, 214);
    StrokeArtworkRect(artwork, slotX, slotY, iconSize, iconSize, 2, red, green, blue, 236);

    LinuxLoadingArtwork scaledIcon;
    ScaleArtworkToFit(talentPreview.icon.artwork, iconSize - 4, iconSize - 4, &scaledIcon);
    if (scaledIcon.ready)
    {
      const int iconX = slotX + (iconSize - scaledIcon.width) / 2;
      const int iconY = slotY + (iconSize - scaledIcon.height) / 2;
      CompositeArtwork(artwork, scaledIcon, iconX, iconY);
    }

    if (talentPreview.locked)
    {
      FillArtworkRect(artwork, slotX, slotY, iconSize, iconSize, 12, 12, 12, 112);
    }
  }

  return true;
}

std::string DescribeLoadingMode(
  const char* label,
  const NDb::Ptr<NDb::AdvMapModeDescription>& description
)
{
  if (!description)
  {
    return std::string();
  }

  std::string tooltip = ReadDbLocalizedText(description->tooltip);
  if (tooltip.empty())
  {
    tooltip = DescribeDbResource(description);
  }
  if (tooltip.empty())
  {
    tooltip = "<empty>";
  }
  return std::string(label) + "=" + tooltip;
}

void AppendLoadingModeEntry(
  std::vector<LinuxLoadingModeEntry>* entries,
  const char* id,
  const NDb::Ptr<NDb::AdvMapModeDescription>& description
)
{
  if (!entries || !id || !*id || !description)
  {
    return;
  }

  LinuxLoadingModeEntry entry;
  entry.id = id;
  entry.tooltip = ReadDbLocalizedText(description->tooltip);
  entry.iconRef = DescribeDbResource(description->icon);
  entries->push_back(entry);
}

void ProbeLoadingUiPreview(
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  LinuxLoadingUiPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  if (!rootFileSystemPreview.dbCacheReady)
  {
    preview->warnings.push_back("Loading UI DB skipped because DB resource cache is unavailable");
    return;
  }

  static const char* kUiDataCandidates[] =
  {
    "/UI/Content/_.UIDT",
    "UI/Content/_.UIDT",
    "UI/Content/_.UIDT.xdb"
  };

  NDb::Ptr<NDb::DBUIData> uiData;
  for (size_t i = 0; i < ARRAY_SIZE(kUiDataCandidates); ++i)
  {
    uiData = NDb::Get<NDb::DBUIData>(NDb::DBID(kUiDataCandidates[i]));
    if (uiData)
    {
      break;
    }
  }

  if (!uiData)
  {
    preview->warnings.push_back("UI/Content/_.UIDT.xdb was not resolved from RootFileSystem");
    return;
  }

  preview->ready = true;
  preview->dbid = ToStdString(uiData->GetDBID().GetFormatted());
  preview->minimapReady = IsValid(uiData->minimap);
  preview->smartChatReady = IsValid(uiData->smartChat);
  preview->maneuversModeReady = IsValid(uiData->mapModeCustomDescriptions.maneuvers);
  preview->guardModeReady = IsValid(uiData->mapModeCustomDescriptions.guardBattle);
  preview->guildModeReady = IsValid(uiData->mapModeCustomDescriptions.guildBattle);
  preview->customModeReady = IsValid(uiData->mapModeCustomDescriptions.customBattle);
  preview->statusCount = uiData->loadingScreenStatuses.size();
  preview->tipCount = uiData->tips.size();
  preview->localeCount = uiData->locales.size();
  preview->forceColorCount = uiData->forceColors.forceColors.size();
  preview->reportTypeCount = uiData->reportTypes.size();
  preview->countryFlagCount = uiData->countryFlags.size();
  preview->chatChannelCount = uiData->chatChannelDescriptions.size();
  preview->bindCount = uiData->binds.bindList.size();
  preview->recentPlayers = uiData->RecentPlayers;
  preview->minimapDbid = DescribeDbResource(uiData->minimap);
  preview->smartChatDbid = DescribeDbResource(uiData->smartChat);

  if (uiData->minimap)
  {
    preview->minimapIconCount = uiData->minimap->icons.size();
  }

  if (uiData->smartChat)
  {
    preview->smartChatCategoryCount = uiData->smartChat->categories.size();
    preview->smartChatMessageCount = uiData->smartChat->messages.size();

    for (int i = 0; i < uiData->smartChat->categories.size(); ++i)
    {
      const NDb::Ptr<NDb::SmartChatCategory>& category = uiData->smartChat->categories[i];
      if (!category)
      {
        continue;
      }

      std::string sample = ReadDbLocalizedText(category->name);
      if (sample.empty())
      {
        sample = ToStdString(category->commandId);
      }
      AppendSampleValue(&preview->smartChatSamples, sample, 4);
    }

  for (int i = 0; i < uiData->smartChat->messages.size(); ++i)
  {
      const NDb::Ptr<NDb::SmartChatMessage>& message = uiData->smartChat->messages[i];
      if (!message)
      {
        continue;
      }

      std::string sample = ReadDbLocalizedText(message->text);
      if (sample.empty())
      {
        sample = ToStdString(message->commandId);
      }
      AppendSampleValue(&preview->smartChatSamples, sample, 6);
    }
  }

  preview->premiumTooltip = ReadDbLocalizedText(uiData->premiumVisualInfo.premiumTooltipAddition);

  for (int i = 0; i < uiData->tips.size(); ++i)
  {
    const std::string tipText = ReadDbLocalizedText(uiData->tips[i].tipText);
    if (tipText.empty())
    {
      continue;
    }

    preview->tips.push_back(tipText);
    if (preview->sampleTip.empty())
    {
      preview->sampleTip = tipText;
    }
  }

  for (int i = 0; i < uiData->loadingScreenStatuses.size(); ++i)
  {
    LinuxLoadingStatusEntry entry;
    entry.key = ToStdString(uiData->loadingScreenStatuses[i].key);
    entry.text = ReadDbLocalizedText(uiData->loadingScreenStatuses[i].text);
    preview->statuses.push_back(entry);
  }

  static const char* kLoadingStatusKeys[] =
  {
    "connecting",
    "lobby_ok",
    "inGame_enteringGame",
    "stat_failed",
    "replay_wrongVersion"
  };

  for (size_t keyIndex = 0; keyIndex < ARRAY_SIZE(kLoadingStatusKeys); ++keyIndex)
  {
    for (int i = 0; i < uiData->loadingScreenStatuses.size(); ++i)
    {
      const NDb::StringTextRefPair& status = uiData->loadingScreenStatuses[i];
      if (status.key != kLoadingStatusKeys[keyIndex])
      {
        continue;
      }

      const std::string localizedText = ReadDbLocalizedText(status.text);
      AppendSampleValue(
        &preview->statusSamples,
        std::string(kLoadingStatusKeys[keyIndex]) + "=" + (localizedText.empty() ? "<empty>" : localizedText),
        6
      );
      break;
    }
  }

  if (preview->statusSamples.empty())
  {
    for (size_t i = 0; i < preview->statuses.size(); ++i)
    {
      AppendSampleValue(
        &preview->statusSamples,
        preview->statuses[i].key + "=" + (preview->statuses[i].text.empty() ? "<empty>" : preview->statuses[i].text),
        5
      );
    }
  }

  for (int i = 0; i < uiData->locales.size(); ++i)
  {
    const NDb::Locale& locale = uiData->locales[i];
    LinuxLoadingLocaleEntry entry;
    entry.locale = ToStdString(locale.locale);
    entry.tooltip = ReadDbLocalizedText(locale.tooltip);
    entry.imageRef = DescribeDbResource(locale.localeImage);
    preview->locales.push_back(entry);
    AppendSampleValue(
      &preview->localeSamples,
      entry.locale + "=" + entry.tooltip,
      5
    );
  }

  for (int i = 0; i < uiData->forceColors.forceColors.size(); ++i)
  {
    AppendSampleValue(
      &preview->forceColorSamples,
      std::string(NStr::StrFmt("%d", uiData->forceColors.forceColors[i].force)),
      6
    );
  }

  AppendSampleValue(
    &preview->modeSamples,
    DescribeLoadingMode("maneuvers", uiData->mapModeCustomDescriptions.maneuvers),
    6
  );
  AppendLoadingModeEntry(&preview->modes, "maneuvers", uiData->mapModeCustomDescriptions.maneuvers);
  AppendSampleValue(
    &preview->modeSamples,
    DescribeLoadingMode("guard", uiData->mapModeCustomDescriptions.guardBattle),
    6
  );
  AppendLoadingModeEntry(&preview->modes, "guard", uiData->mapModeCustomDescriptions.guardBattle);
  AppendSampleValue(
    &preview->modeSamples,
    DescribeLoadingMode("guild", uiData->mapModeCustomDescriptions.guildBattle),
    6
  );
  AppendLoadingModeEntry(&preview->modes, "guild", uiData->mapModeCustomDescriptions.guildBattle);
  AppendSampleValue(
    &preview->modeSamples,
    DescribeLoadingMode("custom", uiData->mapModeCustomDescriptions.customBattle),
    6
  );
  AppendLoadingModeEntry(&preview->modes, "custom", uiData->mapModeCustomDescriptions.customBattle);

  for (int i = 0; i < uiData->chatChannelDescriptions.size(); ++i)
  {
    const NDb::ChatChannelDescription& channel = uiData->chatChannelDescriptions[i];
    std::string channelName = ReadDbLocalizedText(channel.channelName);
    if (channelName.empty())
    {
      channelName = ReadDbLocalizedText(channel.castleChannelName);
    }
    if (!channelName.empty())
    {
      AppendSampleValue(&preview->chatChannelSamples, channelName, 5);
    }
  }

  for (int i = 0; i < uiData->reportTypes.size(); ++i)
  {
    AppendSampleValue(&preview->reportTypeSamples, ReadDbLocalizedText(uiData->reportTypes[i].name), 5);
  }
}

NDb::Ptr<NDb::DBUIData> ResolveLoadingUiDataResource()
{
  static const char* kUiDataCandidates[] =
  {
    "/UI/Content/_.UIDT",
    "UI/Content/_.UIDT",
    "UI/Content/_.UIDT.xdb"
  };

  for (size_t i = 0; i < ARRAY_SIZE(kUiDataCandidates); ++i)
  {
    NDb::Ptr<NDb::DBUIData> uiData = NDb::Get<NDb::DBUIData>(NDb::DBID(kUiDataCandidates[i]));
    if (uiData)
    {
      return uiData;
    }
  }

  return NDb::Ptr<NDb::DBUIData>();
}

void ProbeSessionRootPreview(
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  LinuxSessionRootPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  NDb::SessionRoot::InitRoot(0);

  if (!rootFileSystemPreview.dbCacheReady)
  {
    preview->warnings.push_back("SessionRoot skipped because DB resource cache is unavailable");
    return;
  }

  static const char* kSessionRootCandidates[] =
  {
    "/Session.ROOT",
    "Session.ROOT"
  };

  NDb::Ptr<NDb::SessionRoot> sessionRoot;
  for (size_t i = 0; i < ARRAY_SIZE(kSessionRootCandidates); ++i)
  {
    sessionRoot = NDb::Get<NDb::SessionRoot>(NDb::DBID(kSessionRootCandidates[i]));
    if (sessionRoot)
    {
      break;
    }
  }

  if (!sessionRoot)
  {
    preview->warnings.push_back("Session.ROOT was not resolved from RootFileSystem");
    return;
  }

  NDb::SessionRoot::InitRoot(sessionRoot);
  preview->ready = true;
  preview->rootDbid = ToStdString(sessionRoot->GetDBID().GetFormatted());
  preview->uiRootReady = IsValid(sessionRoot->uiRoot);
  preview->logicRootReady = IsValid(sessionRoot->logicRoot);
  preview->visualRootReady = IsValid(sessionRoot->visualRoot);
  preview->audioRootReady = IsValid(sessionRoot->audioRoot);
  preview->rollSettingsReady = IsValid(sessionRoot->rollSettings);
  preview->sessionMessagesReady = IsValid(sessionRoot->sessionMessages);
  preview->uiRootDbid = DescribeDbResource(sessionRoot->uiRoot);
  preview->logicRootDbid = DescribeDbResource(sessionRoot->logicRoot);
  preview->visualRootDbid = DescribeDbResource(sessionRoot->visualRoot);
  preview->audioRootDbid = DescribeDbResource(sessionRoot->audioRoot);
  preview->rollSettingsDbid = DescribeDbResource(sessionRoot->rollSettings);
  preview->sessionMessagesDbid = DescribeDbResource(sessionRoot->sessionMessages);

  if (sessionRoot->uiRoot)
  {
    preview->uiUnitCategoriesReady = IsValid(sessionRoot->uiRoot->unitCategories);
    preview->uiUnitCategoriesParamsReady = IsValid(sessionRoot->uiRoot->unitCategoriesParams);

    if (sessionRoot->uiRoot->unitCategories)
    {
      preview->uiUnitCategoryCount = sessionRoot->uiRoot->unitCategories->elements.size();
      for (int i = 0; i < sessionRoot->uiRoot->unitCategories->elements.size(); ++i)
      {
        AppendSampleValue(
          &preview->uiUnitCategorySamples,
          string(NDb::EnumToString(sessionRoot->uiRoot->unitCategories->elements[i])),
          6
        );
      }
    }

    if (sessionRoot->uiRoot->unitCategoriesParams)
    {
      preview->uiUnitCategoryParamCount = sessionRoot->uiRoot->unitCategoriesParams->elements.size();
    }
  }

  if (sessionRoot->logicRoot)
  {
    preview->logicAiReady = IsValid(sessionRoot->logicRoot->aiLogic);
    preview->heroesDbReady = IsValid(sessionRoot->logicRoot->heroes);
    preview->mapListReady = IsValid(sessionRoot->logicRoot->mapList);

    if (sessionRoot->logicRoot->heroes)
    {
      const vector<NDb::Ptr<NDb::Hero>>& heroes = sessionRoot->logicRoot->heroes->heroes;
      for (int i = 0; i < heroes.size(); ++i)
      {
        const NDb::Ptr<NDb::Hero>& hero = heroes[i];
        if (!hero)
        {
          continue;
        }

        ++preview->logicHeroCount;
        if (hero->legal)
        {
          ++preview->logicLegalHeroCount;
          AppendSampleValue(
            &preview->heroSamples,
            hero->persistentId.empty() ? hero->id : hero->persistentId,
            4
          );
        }
      }
    }

    if (sessionRoot->logicRoot->mapList)
    {
      const vector<NDb::Ptr<NDb::AdvMapDescription>>& maps = sessionRoot->logicRoot->mapList->maps;
      for (int i = 0; i < maps.size(); ++i)
      {
        const NDb::Ptr<NDb::AdvMapDescription>& mapDescription = maps[i];
        if (!mapDescription)
        {
          continue;
        }

        ++preview->logicMapCount;
        std::string mapTitle = ReadDbLocalizedText(mapDescription->title);
        if (mapTitle.empty())
        {
          mapTitle = fs::path(NormalizeDataRefPath(ToStdString(mapDescription->GetDBID().GetFileName()))).stem().string();
        }
        AppendSampleValue(&preview->mapSamples, mapTitle, 4);
      }
    }

    if (sessionRoot->logicRoot->aiLogic)
    {
      const NDb::Ptr<NDb::AILogicParameters>& aiLogic = sessionRoot->logicRoot->aiLogic;
      preview->logicTeamNameCount = aiLogic->teamsNames.size();
      preview->logicCreepsWavesDelay = aiLogic->creepsWavesDelay;
      preview->logicCreepLevelCap = aiLogic->creepLevelCap;
      preview->logicBaseEmblemHeroNeeds = aiLogic->baseEmblemHeroNeeds;
      preview->logicPortalReady = IsValid(aiLogic->portal);
      preview->logicPortalDbid = DescribeDbResource(aiLogic->portal);
      preview->logicBotsSettingsReady = IsValid(aiLogic->botsSettings);
      preview->logicConsumableGroupCount = aiLogic->consumableGroups ?
        aiLogic->consumableGroups->groups.size() : 0;
      preview->logicLevelToExperienceReady = IsValid(aiLogic->levelToExperienceTable);
      preview->logicHeroRanksReady = IsValid(aiLogic->heroRanks);
      preview->logicGlyphsReady = IsValid(sessionRoot->logicRoot->glyphsDB);
      preview->logicGlyphsDbid = DescribeDbResource(sessionRoot->logicRoot->glyphsDB);
      preview->logicLevelUpsReady = IsValid(sessionRoot->logicRoot->heroesLevelups);
      preview->logicLevelUpsDbid = DescribeDbResource(sessionRoot->logicRoot->heroesLevelups);
      preview->logicDefaultFormulasReady = IsValid(sessionRoot->logicRoot->defaultFormulas);
      preview->logicDefaultFormulasDbid = DescribeDbResource(sessionRoot->logicRoot->defaultFormulas);
      preview->logicUnitsReady = IsValid(sessionRoot->logicRoot->unitLogicParameters);
      preview->logicUnitsDbid = DescribeDbResource(sessionRoot->logicRoot->unitLogicParameters);
      preview->logicGuildBuffsReady = IsValid(sessionRoot->logicRoot->guildBuffsCollection);
      preview->logicGuildBuffsDbid = DescribeDbResource(sessionRoot->logicRoot->guildBuffsCollection);
      preview->logicScoringReady = IsValid(sessionRoot->logicRoot->scoringTable);
      preview->logicScoringDbid = DescribeDbResource(sessionRoot->logicRoot->scoringTable);

      for (int i = 0; i < aiLogic->teamsNames.size(); ++i)
      {
        const std::string teamName = ReadDbLocalizedText(aiLogic->teamsNames[i]);
        AppendSampleValue(&preview->logicTeamNameSamples, teamName, 4);
      }

      if (aiLogic->botsSettings)
      {
        preview->botsAiEnabled = aiLogic->botsSettings->enableBotsAI;
        preview->botsMidOnly = aiLogic->botsSettings->midOnly;
        preview->logicBotsTimeToGo = aiLogic->botsSettings->timeToGo;
        preview->logicBotsTimeToTeleport = aiLogic->botsSettings->timeToTeleport;
      }

      if (aiLogic->levelToExperienceTable)
      {
        const vector<int>& levels = aiLogic->levelToExperienceTable->Levels;
        preview->logicLevelCount = levels.size();
        if (!levels.empty())
        {
          preview->logicLevelFirstExp = levels.front();
          preview->logicLevelLastExp = levels.back();
        }
      }

      if (aiLogic->heroRanks)
      {
        preview->logicHeroRankCount = aiLogic->heroRanks->ranks.size();
        preview->logicHeroRanksHighLevelsMMRating = aiLogic->heroRanks->highLevelsMMRating;
        for (int i = 0; i < aiLogic->heroRanks->ranks.size(); ++i)
        {
          const NDb::Rank& rank = aiLogic->heroRanks->ranks[i];
          std::string rankName = ReadDbLocalizedText(rank.rankGroupNameA);
          if (rankName.empty())
          {
            rankName = ReadDbLocalizedText(rank.rankNameA);
          }
          if (rankName.empty())
          {
            rankName = ReadDbLocalizedText(rank.rankGroupNameB);
          }
          if (rankName.empty())
          {
            rankName = ReadDbLocalizedText(rank.rankNameB);
          }
          if (rankName.empty())
          {
            rankName = NStr::StrFmt("rating=%d", rank.rating);
          }
          AppendSampleValue(&preview->logicRankSamples, rankName, 5);
        }
      }
    }

    if (sessionRoot->logicRoot->glyphsDB)
    {
      preview->logicGlyphCount = sessionRoot->logicRoot->glyphsDB->glyphs.size();
      for (int i = 0; i < sessionRoot->logicRoot->glyphsDB->glyphs.size(); ++i)
      {
        const NDb::GlyphEntry& glyphEntry = sessionRoot->logicRoot->glyphsDB->glyphs[i];
        if (!glyphEntry.glyph)
        {
          continue;
        }

        std::string glyphName = ToStdString(glyphEntry.glyph->GetDBID().GetFormatted());
        const size_t suffixPos = glyphName.find(".xdb");
        if (suffixPos != std::string::npos)
        {
          glyphName.erase(suffixPos);
        }
        AppendSampleValue(&preview->logicGlyphSamples, glyphName, 5);
      }
    }

    if (sessionRoot->logicRoot->heroesLevelups)
    {
      preview->logicLevelUpCount = sessionRoot->logicRoot->heroesLevelups->developmentPoints.size();
      preview->logicKillExperienceModifiersReady =
        IsValid(sessionRoot->logicRoot->heroesLevelups->killExperienceModifiers);
      for (int i = 0; i < sessionRoot->logicRoot->heroesLevelups->developmentPoints.size(); ++i)
      {
        preview->logicLevelUpPointTotal += sessionRoot->logicRoot->heroesLevelups->developmentPoints[i];
      }
    }

    if (sessionRoot->logicRoot->defaultFormulas)
    {
      preview->logicFloatFormulaCount = sessionRoot->logicRoot->defaultFormulas->floatFormulas.size();
      preview->logicBoolFormulaCount = sessionRoot->logicRoot->defaultFormulas->boolFormulas.size();
      preview->logicIntFormulaCount = sessionRoot->logicRoot->defaultFormulas->intFormulas.size();
    }

    if (sessionRoot->logicRoot->unitLogicParameters)
    {
      preview->logicUnitParameterCount = sessionRoot->logicRoot->unitLogicParameters->unitParameters.size();
      for (int i = 0; i < sessionRoot->logicRoot->unitLogicParameters->unitParameters.size(); ++i)
      {
        const NDb::Ptr<NDb::UnitLogicParameters>& unitParameters =
          sessionRoot->logicRoot->unitLogicParameters->unitParameters[i];
        if (!unitParameters)
        {
          continue;
        }

        if (unitParameters->defaultStats)
        {
          ++preview->logicUnitDefaultStatsCount;
        }
        if (unitParameters->targetingPars)
        {
          ++preview->logicUnitTargetingCount;
        }
      }
    }

    if (sessionRoot->logicRoot->guildBuffsCollection)
    {
      preview->logicGuildBuffCount = sessionRoot->logicRoot->guildBuffsCollection->buffs.size();
      for (int i = 0; i < sessionRoot->logicRoot->guildBuffsCollection->buffs.size(); ++i)
      {
        const NDb::Ptr<NDb::GuildBuff>& guildBuff = sessionRoot->logicRoot->guildBuffsCollection->buffs[i];
        if (!guildBuff)
        {
          continue;
        }

        preview->logicGuildShopBonusCount += guildBuff->bonuses.size();
        AppendSampleValue(&preview->logicGuildBuffSamples, ToStdString(guildBuff->persistentId), 5);
      }
    }

    if (sessionRoot->logicRoot->scoringTable)
    {
      preview->logicScoringAchievementCount = sessionRoot->logicRoot->scoringTable->achievementsList.size();
      preview->logicScoringHeroTitleCount = sessionRoot->logicRoot->scoringTable->heroTitles.size();
      preview->logicScoringDescriptionCount = sessionRoot->logicRoot->scoringTable->scoreDescriptions.size();
      preview->logicScoringTeleporterCount = sessionRoot->logicRoot->scoringTable->teleporterAbilities.size();

      for (int i = 0; i < sessionRoot->logicRoot->scoringTable->scoreDescriptions.size(); ++i)
      {
        const std::string scoreName =
          ReadDbLocalizedText(sessionRoot->logicRoot->scoringTable->scoreDescriptions[i].name);
        AppendSampleValue(&preview->logicScoreSamples, scoreName, 5);
      }

      if (preview->logicScoreSamples.empty())
      {
        for (int i = 0; i < sessionRoot->logicRoot->scoringTable->heroTitles.size(); ++i)
        {
          const NDb::HeroTitle& heroTitle = sessionRoot->logicRoot->scoringTable->heroTitles[i];
          std::string title = ReadDbLocalizedText(heroTitle.maleName);
          if (title.empty())
          {
            title = ReadDbLocalizedText(heroTitle.femaleName);
          }
          AppendSampleValue(&preview->logicScoreSamples, title, 5);
        }
      }
    }
  }

  if (sessionRoot->visualRoot)
  {
    preview->visualEffectsReady = IsValid(sessionRoot->visualRoot->effects);
    preview->visualUiEventsReady = IsValid(sessionRoot->visualRoot->uiEvents);
    preview->visualTeamColoringReady = IsValid(sessionRoot->visualRoot->teamColoringScheme);
    preview->visualEmoteSettingsReady = IsValid(sessionRoot->visualRoot->emoteSettings);
    preview->visualCameraCount = sessionRoot->visualRoot->cameras.size();
    preview->visualAnimSetCount = sessionRoot->visualRoot->animSets.sets.size();
    preview->visualWinLoseCount = sessionRoot->visualRoot->winLoseEffects.size();
    preview->visualSelfAuraCount = sessionRoot->visualRoot->selfAuraEffects.size();
    preview->visualAuraCount = sessionRoot->visualRoot->auraEffects.auraEffects.size();
    preview->visualWallTargetZoneWidth = sessionRoot->visualRoot->wallTargetZoneWidth;
    preview->visualTeamColoringDbid = DescribeDbResource(sessionRoot->visualRoot->teamColoringScheme);
    preview->visualEmoteSettingsDbid = DescribeDbResource(sessionRoot->visualRoot->emoteSettings);

    for (int i = 0; i < sessionRoot->visualRoot->cameras.size(); ++i)
    {
      const NDb::Ptr<NDb::AdventureCameraSettings>& camera = sessionRoot->visualRoot->cameras[i];
      if (!camera)
      {
        continue;
      }

      std::string cameraName = ToStdString(camera->name);
      if (cameraName.empty())
      {
        cameraName = DescribeDbResource(camera);
      }
      AppendSampleValue(&preview->visualCameraSamples, cameraName, 5);
    }

    if (sessionRoot->visualRoot->uiEvents)
    {
      const NDb::Ptr<NDb::UIEventsCustom>& uiEvents = sessionRoot->visualRoot->uiEvents;
      const auto appendUiEventSample =
        [&](const NDb::Ptr<NDb::UIEvent>& eventRef, const char* label)
        {
          if (!eventRef)
          {
            return;
          }

          ++preview->visualUiEventCount;
          AppendSampleValue(&preview->visualUiEventSamples, string(label), 5);
        };

      appendUiEventSample(uiEvents->evMiss, "Miss");
      appendUiEventSample(uiEvents->evGetNafta, "GetNafta");
      appendUiEventSample(uiEvents->evHeroKill, "HeroKill");
      appendUiEventSample(uiEvents->evTowerDestroy, "TowerDestroy");
      appendUiEventSample(uiEvents->evVictory, "Victory");
      appendUiEventSample(uiEvents->evDefeat, "Defeat");
      appendUiEventSample(uiEvents->evHeroDisconnected, "HeroDisconnected");
      appendUiEventSample(uiEvents->evHeroReconnected, "HeroReconnected");
      appendUiEventSample(uiEvents->evStartAiForPlayer, "StartAiForPlayer");
      appendUiEventSample(uiEvents->evAdminMutedNotify, "AdminMutedNotify");
    }
  }

  if (sessionRoot->rollSettings)
  {
    preview->rollPvpReady = IsValid(sessionRoot->rollSettings->pvp);
    preview->rollGuildLevelsReady = IsValid(sessionRoot->rollSettings->guildLevels);
    preview->rollRatingModifierCount = sessionRoot->rollSettings->ratingModifiers.size();
    preview->rollFullPartyModifierCount = sessionRoot->rollSettings->fullPartyRatingModifiers.size();
    preview->rollRequiredLevelForExclusiveTalents =
      sessionRoot->rollSettings->requiredLevelForExclusiveTalents;
    preview->rollRequiredRatingForExclusiveTalents =
      sessionRoot->rollSettings->requiredRatingForExclusiveTalents;

    if (sessionRoot->rollSettings->pvp)
    {
      preview->rollPvpModeName = ToStdString(sessionRoot->rollSettings->pvp->modeName);
      preview->rollPvpContainerCount = sessionRoot->rollSettings->pvp->containers.size();
      preview->rollPvpPremiumContainerCount = sessionRoot->rollSettings->pvp->premiumContainers.size();
      preview->rollPvpScoreCap = sessionRoot->rollSettings->pvp->scoresCap;
      preview->rollPvpContainersOnWin = sessionRoot->rollSettings->pvp->containersOnWin;
    }

    if (sessionRoot->rollSettings->guildLevels)
    {
      preview->rollGuildLevelCount = sessionRoot->rollSettings->guildLevels->levels.size();
      for (int i = 0; i < sessionRoot->rollSettings->guildLevels->levels.size(); ++i)
      {
        AppendSampleValue(
          &preview->rollGuildLevelSamples,
          ReadDbLocalizedText(sessionRoot->rollSettings->guildLevels->levels[i].title),
          4
        );
      }
    }
  }

  if (sessionRoot->sessionMessages)
  {
    preview->dxErrorTitle = ReadDbLocalizedText(sessionRoot->sessionMessages->dxErrorMessages.title);
    preview->hardwareErrorMessage =
      ReadDbLocalizedText(sessionRoot->sessionMessages->clientHardwareErrorMessages.errorMessage);
  }
}

void ProbeUiRootPreview(
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  LinuxUiRootPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  if (!rootFileSystemPreview.dbCacheReady)
  {
    preview->warnings.push_back("UIRoot skipped because DB resource cache is unavailable");
    return;
  }

  static const char* kUiRootCandidates[] =
  {
    "/UI/UIRoot",
    "UI/UIRoot"
  };

  NDb::Ptr<NDb::UIRoot> uiRoot;
  for (size_t i = 0; i < ARRAY_SIZE(kUiRootCandidates); ++i)
  {
    uiRoot = NDb::Get<NDb::UIRoot>(NDb::DBID(kUiRootCandidates[i]));
    if (uiRoot)
    {
      break;
    }
  }

  if (!uiRoot)
  {
    preview->warnings.push_back("UI/UIRoot was not resolved from RootFileSystem");
    return;
  }

  preview->ready = true;
  preview->dbid = ToStdString(uiRoot->GetDBID().GetFormatted());
  preview->preferencesReady = IsValid(uiRoot->preferences);
  preview->votingReady = IsValid(uiRoot->votingForSurrender);
  preview->screenCount = uiRoot->screens.size();
  preview->cursorCount = uiRoot->cursors.size();
  preview->scriptCount = uiRoot->scripts.size();
  preview->contentGroupCount = uiRoot->contents.size();
  preview->constantCount = uiRoot->consts.size();
  preview->substituteCount = uiRoot->substitutes.size();
  preview->styleAliasCount = uiRoot->styleAliases.size();
  preview->fontStyleCount = uiRoot->fontStyles.size();

  for (int i = 0; i < uiRoot->screens.size(); ++i)
  {
    const NDb::UIScreenDesc& screen = uiRoot->screens[i];
    std::string screenId = ToStdString(screen.screenId);
    if (screenId.empty() && screen.baseLayout)
    {
      screenId = fs::path(NormalizeDataRefPath(ToStdString(screen.baseLayout->GetDBID().GetFileName()))).stem().string();
    }
    AppendSampleValue(&preview->screenSamples, screenId, 5);
  }

  for (int i = 0; i < uiRoot->contents.size(); ++i)
  {
    const NDb::UIContentGroup& contentGroup = uiRoot->contents[i];
    preview->contentEntryCount += contentGroup.resources.size();
    AppendSampleValue(&preview->contentSamples, ToStdString(contentGroup.groupId), 5);
  }

  for (int i = 0; i < uiRoot->consts.size(); ++i)
  {
    AppendSampleValue(&preview->constantSamples, ToStdString(uiRoot->consts[i].name), 5);
  }
}

void ProbeSoundRootPreview(
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  LinuxSoundRootPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  NDb::SoundRoot::InitRoot(0);

  if (!rootFileSystemPreview.dbCacheReady)
  {
    preview->warnings.push_back("SoundRoot skipped because DB resource cache is unavailable");
    return;
  }

  static const char* kSoundRootCandidates[] =
  {
    "/Audio/SoundRoot",
    "Audio/SoundRoot"
  };

  NDb::Ptr<NDb::SoundRoot> soundRoot;
  for (size_t i = 0; i < ARRAY_SIZE(kSoundRootCandidates); ++i)
  {
    soundRoot = NDb::Get<NDb::SoundRoot>(NDb::DBID(kSoundRootCandidates[i]));
    if (soundRoot)
    {
      break;
    }
  }

  if (!soundRoot)
  {
    preview->warnings.push_back("Audio/SoundRoot was not resolved from RootFileSystem");
    return;
  }

  NDb::SoundRoot::InitRoot(soundRoot.GetPtr());
  preview->ready = true;
  preview->dbid = ToStdString(soundRoot->GetDBID().GetFormatted());
  preview->sceneCount = soundRoot->sceneScenes.size();
  preview->ambienceGroupCount = soundRoot->ambienceGroups.size();
  preview->heartbeatReady = HasFmodEvent(soundRoot->heartbeat);
  preview->ambientReady = HasFmodEvent(soundRoot->ambient);
  preview->preferencesVolumeReady = HasFmodEvent(soundRoot->preferencesVolumeHasChanged);
  preview->lastHitReady = HasFmodEvent(soundRoot->lastHit);
  preview->timerSoundsReady =
    HasFmodEvent(soundRoot->timerSounds.startTimerSound) ||
    HasFmodEvent(soundRoot->timerSounds.startEventA) ||
    HasFmodEvent(soundRoot->timerSounds.startEventB) ||
    HasFmodEvent(soundRoot->timerSounds.deadTimerSound);
  preview->heartbeatEvent = DescribeFmodEvent(soundRoot->heartbeat);
  preview->ambientEvent = DescribeFmodEvent(soundRoot->ambient);

  for (int i = 0; i < soundRoot->sceneScenes.size(); ++i)
  {
    const NDb::SoundSceneDesc& scene = soundRoot->sceneScenes[i];
    preview->sceneGroupCount += scene.soundSceneGroups.size();
    AppendSampleValue(&preview->cueSamples, scene.cue.cueName, 5);
    for (int j = 0; j < scene.soundSceneGroups.size(); ++j)
    {
      AppendSampleValue(&preview->categorySamples, scene.soundSceneGroups[j].categoryName, 5);
    }
  }

  for (int i = 0; i < soundRoot->ambienceGroups.size(); ++i)
  {
    const NDb::SoundAmbientDesc& ambienceGroup = soundRoot->ambienceGroups[i];
    std::string sample = ToStdString(ambienceGroup.paramName);
    if (sample.empty())
    {
      sample = ToStdString(ambienceGroup.reverbPresetName);
    }
    AppendSampleValue(&preview->ambienceSamples, sample, 5);
  }
}

void ProbeResourceCatalogPreview(
  const LinuxClientEnvironment& environment,
  LinuxResourceCatalogPreview* preview
)
{
  if (!preview)
  {
    return;
  }

  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  const fs::path dataRoot = baseRoot / "Data";
  if (baseRoot.empty() || !fs::exists(dataRoot) || !fs::is_directory(dataRoot))
  {
    preview->warnings.push_back("Resource catalog scan skipped because Data root is unavailable");
    return;
  }

  std::vector<std::string> talentFiles;
  std::vector<std::string> consumableFiles;
  std::vector<std::string> marketingFiles;

  std::error_code error;
  for (fs::recursive_directory_iterator it(dataRoot, error), end; !error && it != end; it.increment(error))
  {
    if (!it->is_regular_file())
    {
      continue;
    }

    const fs::path path = it->path();
    const std::string fileName = path.filename().string();
    if (fileName.size() >= strlen(".TALENT.xdb") &&
        fileName.compare(fileName.size() - strlen(".TALENT.xdb"), strlen(".TALENT.xdb"), ".TALENT.xdb") == 0)
    {
      talentFiles.push_back(fs::relative(path, dataRoot).generic_string());
      continue;
    }

    if (fileName.size() >= strlen(".ARCT.xdb") &&
        fileName.compare(fileName.size() - strlen(".ARCT.xdb"), strlen(".ARCT.xdb"), ".ARCT.xdb") == 0)
    {
      consumableFiles.push_back(fs::relative(path, dataRoot).generic_string());
      continue;
    }

    if (fileName.size() >= strlen(".ROLLITEM.xdb") &&
        fileName.compare(fileName.size() - strlen(".ROLLITEM.xdb"), strlen(".ROLLITEM.xdb"), ".ROLLITEM.xdb") == 0)
    {
      marketingFiles.push_back(fs::relative(path, dataRoot).generic_string());
    }
  }

  if (error)
  {
    preview->warnings.push_back("Resource catalog scan stopped early: " + error.message());
  }

  std::sort(talentFiles.begin(), talentFiles.end());
  std::sort(consumableFiles.begin(), consumableFiles.end());
  std::sort(marketingFiles.begin(), marketingFiles.end());

  preview->ready = true;
  preview->talentCount = talentFiles.size();
  preview->consumableCount = consumableFiles.size();
  preview->marketingItemCount = marketingFiles.size();

  for (int i = 0; i < talentFiles.size() && preview->talentSamples.size() < 4; ++i)
  {
    AppendSampleValue(&preview->talentSamples, fs::path(talentFiles[i].c_str()).stem().string(), 4);
  }

  for (int i = 0; i < consumableFiles.size() && preview->consumableSamples.size() < 4; ++i)
  {
    AppendSampleValue(&preview->consumableSamples, fs::path(consumableFiles[i].c_str()).stem().string(), 4);
  }

  for (int i = 0; i < marketingFiles.size() && preview->marketingSamples.size() < 4; ++i)
  {
    AppendSampleValue(&preview->marketingSamples, fs::path(marketingFiles[i].c_str()).stem().string(), 4);
  }
}

void ProbeRootFileSystem(
  const LinuxClientEnvironment& environment,
  const LinuxContentProbe& contentProbe,
  LinuxRootFileSystemPreview* preview
)
{
  NDb::SetResourceCache(0);

  const fs::path baseRoot = environment.baseDir.empty() ? environment.gameRoot : environment.baseDir;
  if (baseRoot.empty())
  {
    preview->warnings.push_back("RootFileSystem base root unavailable");
    return;
  }

  RootFileSystem::ClearFileSystems();

  const fs::path dataRoot = baseRoot / "Data";
  if (fs::exists(dataRoot) && fs::is_directory(dataRoot))
  {
    RootFileSystem::RegisterFileSystem(new WinFileSystem(string(dataRoot.string().c_str()), false));
    preview->dataRegistered = true;
  }

  if (!contentProbe.localizationRoot.empty())
  {
    RootFileSystem::RegisterFileSystem(new WinFileSystem(string(contentProbe.localizationRoot.string().c_str()), false));
    preview->localizationRegistered = true;
  }

  preview->mounted = preview->dataRegistered || preview->localizationRegistered;
  if (!preview->mounted)
  {
    preview->warnings.push_back("No file systems registered");
    return;
  }

  NDb::DbResourceCache* resourceCache =
    NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor());
  if (resourceCache)
  {
    NDb::SetResourceCache(resourceCache);
    preview->dbCacheReady = true;
  }
  else
  {
    preview->warnings.push_back("Failed to initialize DB resource cache");
  }

  preview->sampleFile = NormalizeRootFileSystemPath("UI/Screens/Loading/LoadingScreen.xdb");
  std::string sampleContent;
  if (ReadRootFileBytes(preview->sampleFile.c_str(), &sampleContent))
  {
    preview->sampleFileSize = static_cast<int>(sampleContent.size());
  }
  else
  {
    preview->warnings.push_back("RootFileSystem cannot open loading layout");
  }

  preview->localizationFile = NormalizeRootFileSystemPath("PvX/strings.xml");
  SFileInfo localizationInfo;
  if (RootFileSystem::GetFileInfo(&localizationInfo, preview->localizationFile.c_str()))
  {
    preview->localizationFileSize = static_cast<int>(contentProbe.localizationSampleSize);
  }
  else
  {
    preview->warnings.push_back("RootFileSystem cannot open localized strings");
  }

  preview->textRefFile = NormalizeRootFileSystemPath("UI/Screens/Loading/LoadingScreen_a16201965d0a4e9e8be4442b00fce3ab_properties_5_propertyValue.txt");
  if (!ReadRootFileUtf16Text(preview->textRefFile.c_str(), &preview->textRefValue))
  {
    preview->warnings.push_back("RootFileSystem cannot decode loading textref");
  }
}

std::string FormatCurrentTime()
{
  time_t now = time(0);
  struct tm localTime = {};
  localtime_r(&now, &localTime);

  char buffer[64] = {0};
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
  return std::string(buffer);
}

fs::path ResolveLogsDir(const LinuxClientEnvironment& environment)
{
  if (environment.engineReady)
  {
    return environment.logsDir;
  }

  return DetectLogsDir();
}

unsigned long ResolveColor(Display* display, int screen, const char* name, unsigned long fallback)
{
  XColor exact = {};
  XColor color = {};
  if (XAllocNamedColor(display, DefaultColormap(display, screen), name, &color, &exact))
  {
    return color.pixel;
  }

  return fallback;
}

bool InitializeWindowOverlay(LinuxWindowOverlay* overlay)
{
  overlay->display = static_cast<Display*>(NMainFrame::GetNativeDisplay());
  if (!overlay->display)
  {
    return false;
  }

  overlay->window = static_cast<::Window>(reinterpret_cast<uintptr_t>(NMainFrame::GetWnd()));
  if (!overlay->window)
  {
    overlay->display = nullptr;
    return false;
  }

  overlay->gc = XCreateGC(overlay->display, overlay->window, 0, nullptr);
  if (!overlay->gc)
  {
    overlay->display = nullptr;
    return false;
  }

  const int screen = DefaultScreen(overlay->display);
  overlay->background = ResolveColor(overlay->display, screen, "#11161c", BlackPixel(overlay->display, screen));
  overlay->accent = ResolveColor(overlay->display, screen, "#1b2631", BlackPixel(overlay->display, screen));
  overlay->foreground = ResolveColor(overlay->display, screen, "#f4efe6", WhitePixel(overlay->display, screen));
  overlay->muted = ResolveColor(overlay->display, screen, "#c7d0d8", WhitePixel(overlay->display, screen));
  overlay->panelBackground = ResolveColor(overlay->display, screen, "#202a35", BlackPixel(overlay->display, screen));
  overlay->panelBorder = ResolveColor(overlay->display, screen, "#5f7386", WhitePixel(overlay->display, screen));

  XFontStruct* font = XLoadQueryFont(overlay->display, "9x15");
  if (!font)
  {
    font = XLoadQueryFont(overlay->display, "fixed");
  }
  if (font)
  {
    overlay->fontStruct = font;
    overlay->font = font->fid;
    XSetFont(overlay->display, overlay->gc, overlay->font);
  }

  if (NMainFrame::InitOpenGLContext() && NMainFrame::MakeOpenGLContextCurrent())
  {
    overlay->openglReady = true;
    if (overlay->font)
    {
      overlay->fontDisplayListBase = glGenLists(256);
      if (overlay->fontDisplayListBase != 0)
      {
        glXUseXFont(overlay->font, 0, 256, overlay->fontDisplayListBase);
        overlay->fontDisplayListsReady = true;
      }
    }
  }

  overlay->ready = true;
  return true;
}

int FindMaskShift(unsigned long mask)
{
  int shift = 0;
  while (mask && (mask & 1UL) == 0)
  {
    mask >>= 1;
    ++shift;
  }
  return shift;
}

unsigned long ScaleChannelToMask(unsigned long mask, unsigned char value)
{
  if (!mask)
  {
    return 0;
  }

  const int shift = FindMaskShift(mask);
  const unsigned long maxValue = mask >> shift;
  const unsigned long scaledValue = (static_cast<unsigned long>(value) * maxValue + 127) / 255;
  return (scaledValue << shift) & mask;
}

unsigned long PackVisualPixel(Visual* visual, unsigned char red, unsigned char green, unsigned char blue)
{
  return ScaleChannelToMask(visual->red_mask, red) |
         ScaleChannelToMask(visual->green_mask, green) |
         ScaleChannelToMask(visual->blue_mask, blue);
}

unsigned char BlendChannel(unsigned char source, unsigned char destination, unsigned char alpha)
{
  const unsigned int inverseAlpha = 255U - static_cast<unsigned int>(alpha);
  return static_cast<unsigned char>(
    (static_cast<unsigned int>(source) * static_cast<unsigned int>(alpha) +
     static_cast<unsigned int>(destination) * inverseAlpha + 127U) / 255U
  );
}

bool UploadArtworkPixmap(LinuxWindowOverlay* overlay, const LinuxLoadingArtwork& artwork)
{
  if (!overlay->ready || !artwork.ready || artwork.width <= 0 || artwork.height <= 0)
  {
    return false;
  }

  if (overlay->openglReady)
  {
    if (!NMainFrame::MakeOpenGLContextCurrent())
    {
      return false;
    }

    if (!overlay->artworkTexture)
    {
      glGenTextures(1, &overlay->artworkTexture);
    }
    if (!overlay->artworkTexture)
    {
      return false;
    }

    glBindTexture(GL_TEXTURE_2D, overlay->artworkTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RGBA,
      artwork.width,
      artwork.height,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      &artwork.rgba[0]
    );
    glBindTexture(GL_TEXTURE_2D, 0);

    if (overlay->artworkPixmap)
    {
      XFreePixmap(overlay->display, overlay->artworkPixmap);
      overlay->artworkPixmap = 0;
    }

    overlay->artworkWidth = static_cast<unsigned int>(artwork.width);
    overlay->artworkHeight = static_cast<unsigned int>(artwork.height);
    return true;
  }

  XWindowAttributes attributes = {};
  if (!XGetWindowAttributes(overlay->display, overlay->window, &attributes))
  {
    return false;
  }

  if (attributes.visual->c_class != TrueColor && attributes.visual->c_class != DirectColor)
  {
    return false;
  }

  if (overlay->artworkPixmap)
  {
    XFreePixmap(overlay->display, overlay->artworkPixmap);
    overlay->artworkPixmap = 0;
  }

  XImage* image = XCreateImage(
    overlay->display,
    attributes.visual,
    static_cast<unsigned int>(attributes.depth),
    ZPixmap,
    0,
    0,
    artwork.width,
    artwork.height,
    32,
    0
  );
  if (!image)
  {
    return false;
  }

  image->data = static_cast<char*>(malloc(image->bytes_per_line * image->height));
  if (!image->data)
  {
    image->f.destroy_image(image);
    return false;
  }
  memset(image->data, 0, image->bytes_per_line * image->height);

  const unsigned char backgroundRed = 0x11;
  const unsigned char backgroundGreen = 0x16;
  const unsigned char backgroundBlue = 0x1c;

  for (int y = 0; y < artwork.height; ++y)
  {
    for (int x = 0; x < artwork.width; ++x)
    {
      const size_t pixelIndex = static_cast<size_t>(y * artwork.width + x) * 4U;
      const unsigned char sourceRed = artwork.rgba[pixelIndex + 0];
      const unsigned char sourceGreen = artwork.rgba[pixelIndex + 1];
      const unsigned char sourceBlue = artwork.rgba[pixelIndex + 2];
      const unsigned char sourceAlpha = artwork.rgba[pixelIndex + 3];

      const unsigned char red = BlendChannel(sourceRed, backgroundRed, sourceAlpha);
      const unsigned char green = BlendChannel(sourceGreen, backgroundGreen, sourceAlpha);
      const unsigned char blue = BlendChannel(sourceBlue, backgroundBlue, sourceAlpha);

      XPutPixel(image, x, y, PackVisualPixel(attributes.visual, red, green, blue));
    }
  }

  overlay->artworkPixmap = XCreatePixmap(
    overlay->display,
    overlay->window,
    static_cast<unsigned int>(artwork.width),
    static_cast<unsigned int>(artwork.height),
    static_cast<unsigned int>(attributes.depth)
  );
  if (!overlay->artworkPixmap)
  {
    XDestroyImage(image);
    return false;
  }

  XPutImage(
    overlay->display,
    overlay->artworkPixmap,
    overlay->gc,
    image,
    0,
    0,
    0,
    0,
    static_cast<unsigned int>(artwork.width),
    static_cast<unsigned int>(artwork.height)
  );
  XDestroyImage(image);

  overlay->artworkWidth = static_cast<unsigned int>(artwork.width);
  overlay->artworkHeight = static_cast<unsigned int>(artwork.height);
  return true;
}

void ShutdownWindowOverlay(LinuxWindowOverlay* overlay)
{
  if (overlay->openglReady && NMainFrame::MakeOpenGLContextCurrent())
  {
    if (overlay->artworkTexture)
    {
      glDeleteTextures(1, &overlay->artworkTexture);
      overlay->artworkTexture = 0;
    }

    if (overlay->fontDisplayListsReady && overlay->fontDisplayListBase != 0)
    {
      glDeleteLists(overlay->fontDisplayListBase, 256);
      overlay->fontDisplayListBase = 0;
      overlay->fontDisplayListsReady = false;
    }
  }

  if (overlay->display && overlay->artworkPixmap)
  {
    XFreePixmap(overlay->display, overlay->artworkPixmap);
    overlay->artworkPixmap = 0;
  }

  if (overlay->display && overlay->gc)
  {
    XFreeGC(overlay->display, overlay->gc);
    overlay->gc = 0;
  }

  if (overlay->display && overlay->fontStruct)
  {
    XFreeFont(overlay->display, overlay->fontStruct);
    overlay->fontStruct = nullptr;
  }

  overlay->display = nullptr;
  overlay->window = 0;
  overlay->artworkWidth = 0;
  overlay->artworkHeight = 0;
  overlay->openglReady = false;
  overlay->ready = false;
}

std::string DescribeInputEvent(const Input::Event& event)
{
  char buffer[256] = {0};
  const char* name = event.Command() ? event.Command()->Name().c_str() : "<null>";

  switch (event.Type())
  {
    case Input::EEventType::Activation:
      snprintf(buffer, sizeof(buffer), "%s activated", name);
      break;

    case Input::EEventType::ScalarDelta:
      snprintf(buffer, sizeof(buffer), "%s delta=%.2f", name, event.Delta());
      break;

    case Input::EEventType::ScalarValue:
      snprintf(buffer, sizeof(buffer), "%s value=%.2f", name, event.Value());
      break;

    case Input::EEventType::System:
      snprintf(
        buffer,
        sizeof(buffer),
        "%s a=%d b=%d",
        name,
        event.SysParams().first,
        event.SysParams().second
      );
      break;

    default:
      snprintf(buffer, sizeof(buffer), "%s", name);
      break;
  }

  return std::string(buffer);
}

void DrainMainFrameMessages(TLinuxMainFrameMessages* messages)
{
  messages->clear();

  NMainFrame::SWindowsMsg message;
  while (NMainFrame::GetMessage(&message))
  {
    messages->push_back(message);
  }
}

void UpdateInputState(LinuxInputState* state)
{
  state->frameEvents.clear();
  state->rawMessages.clear();
  DrainMainFrameMessages(&state->rawMessages);

  if (!state->initialized || !IsValid(state->hwInput) || !IsValid(state->binds))
  {
    return;
  }

  state->hwInput->SetFrameMessages(state->rawMessages);

  NHPTimer::STime now = 0;
  NHPTimer::GetTime(now);
  float deltaSeconds = state->lastUpdateTime ? NHPTimer::Time2Seconds(now - state->lastUpdateTime) : (1.0f / 60.0f);
  if (deltaSeconds <= 0.0f)
  {
    deltaSeconds = 1.0f / 60.0f;
  }
  state->lastUpdateTime = now;

  state->binds->Update(deltaSeconds, NMainFrame::IsAppActive());

  vector<Input::Event>& bindEvents = state->binds->GetEvents();
  for (int i = 0; i < bindEvents.size(); ++i)
  {
    state->frameEvents.push_back(bindEvents[i]);
  }

  state->totalEvents += state->frameEvents.size();

  for (size_t i = 0; i < state->frameEvents.size(); ++i)
  {
    const Input::Event& event = state->frameEvents[i];
    if (RunLinuxCommandBinding(event))
    {
      ++state->commandBindingHits;
    }

    state->recentEvents.push_back(DescribeInputEvent(event));
  }

  state->binds->ClearEvents();

  const size_t maxRecentEvents = 6;
  if (state->recentEvents.size() > maxRecentEvents)
  {
    state->recentEvents.erase(state->recentEvents.begin(), state->recentEvents.end() - maxRecentEvents);
  }
}

void UpdateMapBrowserState(
  const LinuxInputState& inputState,
  const LinuxMapCatalog& catalog,
  LinuxMapBrowserState* browser
)
{
  if (!browser || catalog.entries.empty())
  {
    return;
  }

  const int pageStep = 8;
  for (size_t i = 0; i < inputState.rawMessages.size(); ++i)
  {
    const NMainFrame::SWindowsMsg& message = inputState.rawMessages[i];
    if (message.msg == NMainFrame::SWindowsMsg::MOUSE_WHEEL)
    {
      const int wheelDelta = GET_WHEEL_DELTA_WPARAM(message.dwFlags);
      if (wheelDelta > 0)
      {
        MoveMapSelection(catalog, browser, -1, "mouse-wheel");
      }
      else if (wheelDelta < 0)
      {
        MoveMapSelection(catalog, browser, 1, "mouse-wheel");
      }
      continue;
    }

    if (message.msg != NMainFrame::SWindowsMsg::KEY_DOWN)
    {
      continue;
    }

    switch (message.nKey)
    {
      case XK_Up:
        MoveMapSelection(catalog, browser, -1, "keyboard");
        break;

      case XK_Down:
        MoveMapSelection(catalog, browser, 1, "keyboard");
        break;

      case XK_Prior:
        MoveMapSelection(catalog, browser, -pageStep, "keyboard");
        break;

      case XK_Next:
        MoveMapSelection(catalog, browser, pageStep, "keyboard");
        break;

      case XK_Home:
        SelectAbsoluteMapIndex(catalog, browser, 0, "keyboard");
        break;

      case XK_End:
        SelectAbsoluteMapIndex(catalog, browser, catalog.entries.size() - 1, "keyboard");
        break;

      default:
        break;
    }
  }
}

void UpdateArtworkSelectionState(
  const LinuxInputState& inputState,
  LinuxArtworkSelectionState* artworkState
)
{
  if (!artworkState)
  {
    return;
  }

  for (size_t i = 0; i < inputState.rawMessages.size(); ++i)
  {
    const NMainFrame::SWindowsMsg& message = inputState.rawMessages[i];
    if (message.msg != NMainFrame::SWindowsMsg::KEY_DOWN)
    {
      continue;
    }

    switch (message.nKey)
    {
      case XK_Left:
        StepArtworkMode(artworkState, -1, "keyboard");
        break;

      case XK_Right:
        StepArtworkMode(artworkState, 1, "keyboard");
        break;

      default:
        break;
    }
  }
}

template <typename T>
void StepWrappedSelection(const std::vector<T>& items, size_t* index, int delta)
{
  if (!index || items.empty() || delta == 0)
  {
    return;
  }

  const int itemCount = static_cast<int>(items.size());
  int nextIndex = static_cast<int>(*index) + delta;
  while (nextIndex < 0)
  {
    nextIndex += itemCount;
  }
  while (nextIndex >= itemCount)
  {
    nextIndex -= itemCount;
  }
  *index = static_cast<size_t>(nextIndex);
}

void UpdateLoadingUiState(
  const LinuxInputState& inputState,
  const LinuxLoadingUiPreview& preview,
  LinuxLoadingRuntimeDriver* runtimeDriver,
  LinuxLoadingUiState* state
)
{
  if (!state)
  {
    return;
  }

  for (size_t i = 0; i < inputState.rawMessages.size(); ++i)
  {
    const NMainFrame::SWindowsMsg& message = inputState.rawMessages[i];
    if (message.msg != NMainFrame::SWindowsMsg::KEY_DOWN)
    {
      continue;
    }

    bool changed = false;
    switch (message.nKey)
    {
      case XK_s:
      case XK_S:
        if (runtimeDriver && runtimeDriver->ready && !runtimeDriver->events.empty())
        {
          const size_t nextEventIndex = (state->runtimeEventIndex + 1) % runtimeDriver->events.size();
          SyncLoadingRuntimeState(preview, runtimeDriver, state, nextEventIndex, "keyboard-runtime-status");
          changed = true;
        }
        else if (!preview.statuses.empty())
        {
          StepWrappedSelection(preview.statuses, &state->statusIndex, 1);
          state->source = "keyboard-status";
          changed = true;
        }
        break;

      case XK_t:
      case XK_T:
        if (!preview.tips.empty())
        {
          StepWrappedSelection(preview.tips, &state->tipIndex, 1);
          state->source = "keyboard-tip";
          changed = true;
        }
        break;

      case XK_l:
      case XK_L:
        if (!preview.locales.empty())
        {
          StepWrappedSelection(preview.locales, &state->currentLocaleIndex, 1);
          if (state->currentLocaleIndex == state->enemyLocaleIndex && preview.locales.size() > 1)
          {
            state->enemyLocaleIndex = (state->currentLocaleIndex + 1) % preview.locales.size();
          }
          state->source = "keyboard-locale";
          changed = true;
        }
        break;

      case XK_e:
      case XK_E:
        if (!preview.locales.empty())
        {
          StepWrappedSelection(preview.locales, &state->enemyLocaleIndex, 1);
          if (state->enemyLocaleIndex == state->currentLocaleIndex && preview.locales.size() > 1)
          {
            state->enemyLocaleIndex = (state->enemyLocaleIndex + 1) % preview.locales.size();
          }
          state->source = "keyboard-enemy-locale";
          changed = true;
        }
        break;

      case XK_m:
      case XK_M:
        if (!preview.modes.empty())
        {
          StepWrappedSelection(preview.modes, &state->modeIndex, 1);
          state->source = "keyboard-mode";
          changed = true;
        }
        break;

      default:
        break;
    }

    if (changed)
    {
      ++state->changeCount;
    }
  }
}

void MoveSelectedHero(
  const LinuxHeroCatalog& heroCatalog,
  LinuxLocalMatchPreview* preview,
  int delta,
  const char* source
)
{
  if (!preview || heroCatalog.entries.empty() || delta == 0)
  {
    return;
  }

  int nextIndex = static_cast<int>(preview->selectedHeroIndex) + delta;
  while (nextIndex < 0)
  {
    nextIndex += static_cast<int>(heroCatalog.entries.size());
  }
  while (nextIndex >= static_cast<int>(heroCatalog.entries.size()))
  {
    nextIndex -= static_cast<int>(heroCatalog.entries.size());
  }

  if (preview->selectedHeroIndex != static_cast<size_t>(nextIndex))
  {
    preview->selectedHeroIndex = static_cast<size_t>(nextIndex);
    preview->generationSource = source ? source : "runtime";
  }
}

void ToggleHumanTeam(LinuxLocalMatchPreview* preview, const char* source)
{
  if (!preview)
  {
    return;
  }

  preview->humanTeam = preview->humanTeam == 1 ? 2 : 1;
  preview->selectedSlotIndex = ResolveHumanSlotIndex(*preview);
  preview->generationSource = source ? source : "runtime";
}

void MoveSelectedLineupSlot(LinuxLocalMatchPreview* preview, int delta, const char* source)
{
  if (!preview || preview->lineup.empty() || delta == 0)
  {
    return;
  }

  int nextIndex = static_cast<int>(preview->selectedSlotIndex) + delta;
  const int slotCount = static_cast<int>(preview->lineup.size());
  while (nextIndex < 0)
  {
    nextIndex += slotCount;
  }
  while (nextIndex >= slotCount)
  {
    nextIndex -= slotCount;
  }

  if (preview->selectedSlotIndex != static_cast<size_t>(nextIndex))
  {
    preview->selectedSlotIndex = static_cast<size_t>(nextIndex);
    preview->generationSource = source ? source : "runtime";
  }
}

void AdjustRequestedTeamSize(
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  LinuxLocalMatchPreview* preview,
  int delta,
  const char* source
)
{
  if (!preview || delta == 0)
  {
    return;
  }

  const size_t maxTeamSize = ResolveSelectedMapMaxTeamSize(mapCatalog, mapBrowserState);
  const size_t currentSize = preview->requestedTeamSize > 0 ? preview->requestedTeamSize : maxTeamSize;
  int nextSize = static_cast<int>(currentSize) + delta;
  if (nextSize < 1)
  {
    nextSize = 1;
  }
  if (nextSize > static_cast<int>(maxTeamSize))
  {
    nextSize = static_cast<int>(maxTeamSize);
  }

  if (preview->requestedTeamSize != static_cast<size_t>(nextSize))
  {
    preview->requestedTeamSize = static_cast<size_t>(nextSize);
    preview->generationSource = source ? source : "runtime";
  }
}

void StepSelectedSlotHero(
  const LinuxHeroCatalog& heroCatalog,
  LinuxLocalMatchPreview* preview,
  int delta,
  const char* source
)
{
  if (!preview || heroCatalog.entries.empty() || delta == 0)
  {
    return;
  }

  size_t currentHeroIndex = preview->selectedHeroIndex;
  if (preview->ready && preview->selectedSlotIndex < preview->lineup.size())
  {
    currentHeroIndex = preview->lineup[preview->selectedSlotIndex].heroIndex;
  }

  int nextIndex = static_cast<int>(currentHeroIndex) + delta;
  const int heroCount = static_cast<int>(heroCatalog.entries.size());
  while (nextIndex < 0)
  {
    nextIndex += heroCount;
  }
  while (nextIndex >= heroCount)
  {
    nextIndex -= heroCount;
  }

  if (preview->ready && IsHumanSlotIndex(*preview, preview->selectedSlotIndex))
  {
    if (preview->selectedHeroIndex != static_cast<size_t>(nextIndex))
    {
      preview->selectedHeroIndex = static_cast<size_t>(nextIndex);
      preview->generationSource = source ? source : "runtime";
    }
    return;
  }

  ResizeLocalMatchOverrides(preview, std::max(preview->teamSize, static_cast<size_t>(1)) * 2);
  if (preview->selectedSlotIndex < preview->slotHeroOverrides.size() &&
      preview->slotHeroOverrides[preview->selectedSlotIndex] != static_cast<size_t>(nextIndex))
  {
    preview->slotHeroOverrides[preview->selectedSlotIndex] = static_cast<size_t>(nextIndex);
    preview->generationSource = source ? source : "runtime";
  }
}

void ClearSelectedSlotOverride(LinuxLocalMatchPreview* preview, const char* source)
{
  if (!preview)
  {
    return;
  }

  const size_t totalSlots = std::max(preview->teamSize, static_cast<size_t>(1)) * 2;
  ResizeLocalMatchOverrides(preview, totalSlots);
  if (preview->selectedSlotIndex < preview->slotHeroOverrides.size() &&
      !IsHumanSlotIndex(*preview, preview->selectedSlotIndex) &&
      preview->slotHeroOverrides[preview->selectedSlotIndex] != static_cast<size_t>(-1))
  {
    preview->slotHeroOverrides[preview->selectedSlotIndex] = static_cast<size_t>(-1);
    preview->generationSource = source ? source : "runtime";
  }
}

bool UpdateLocalMatchPreviewState(
  const LinuxInputState& inputState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  LinuxLocalMatchPreview* preview
)
{
  if (!preview)
  {
    return false;
  }

  bool changed = false;
  for (size_t i = 0; i < inputState.rawMessages.size(); ++i)
  {
    const NMainFrame::SWindowsMsg& message = inputState.rawMessages[i];
    if (message.msg != NMainFrame::SWindowsMsg::KEY_DOWN)
    {
      continue;
    }

    switch (message.nKey)
    {
      case XK_comma:
      case XK_less:
        MoveSelectedLineupSlot(preview, -1, "keyboard-slot");
        changed = true;
        break;

      case XK_period:
      case XK_greater:
        MoveSelectedLineupSlot(preview, 1, "keyboard-slot");
        changed = true;
        break;

      case XK_bracketleft:
        StepSelectedSlotHero(heroCatalog, preview, -1, "keyboard-hero");
        changed = true;
        break;

      case XK_bracketright:
        StepSelectedSlotHero(heroCatalog, preview, 1, "keyboard-hero");
        changed = true;
        break;

      case XK_minus:
      case XK_KP_Subtract:
        AdjustRequestedTeamSize(mapCatalog, mapBrowserState, preview, -1, "keyboard-size");
        changed = true;
        break;

      case XK_equal:
      case XK_plus:
      case XK_KP_Add:
        AdjustRequestedTeamSize(mapCatalog, mapBrowserState, preview, 1, "keyboard-size");
        changed = true;
        break;

      case XK_Tab:
        ToggleHumanTeam(preview, "keyboard-team");
        changed = true;
        break;

      case XK_BackSpace:
      case XK_Delete:
        ClearSelectedSlotOverride(preview, "keyboard-clear");
        changed = true;
        break;

      case XK_Return:
      case XK_KP_Enter:
        ++preview->shuffleOffset;
        preview->generationSource = "keyboard-shuffle";
        changed = true;
        break;

      default:
        break;
    }
  }

  if (changed)
  {
    RegenerateLocalMatchPreview(heroCatalog, mapCatalog, mapBrowserState, preview, preview->generationSource.c_str());
  }

  return changed;
}

std::vector<std::string> BuildOverlayLines(
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxLaunchPreview& launchPreview,
  const LinuxSessionPreview& sessionPreview,
  const LinuxConfigBootstrapPreview& configPreview,
  const LinuxContentProbe& contentProbe,
  const LinuxLoadingScreenPreview& loadingPreview,
  const LinuxLoadingUiPreview& loadingUiPreview,
  const LinuxLoadingRuntimeDriver& loadingRuntimeDriver,
  const LinuxLoadingHeroesRuntimePreview& loadingHeroesRuntimePreview,
  const LinuxLoadingUiState& loadingUiState,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxArtworkSelectionState& artworkState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  const LinuxUiRootPreview& uiRootPreview,
  const LinuxSessionRootPreview& sessionRootPreview,
  const LinuxSoundRootPreview& soundRootPreview,
  const LinuxResourceCatalogPreview& resourceCatalogPreview,
  const LinuxInputState& inputState,
  double elapsedSeconds
)
{
  char buffer[512] = {0};
  std::vector<std::string> lines;

  lines.push_back("Native Linux client shell");
  lines.push_back("Renderer status: pending native backend replacement for Direct3D 9");

  snprintf(
    buffer,
    sizeof(buffer),
    "Launch source: %s | protocol=%s | parent=%s | session=%s",
    launchPreview.source.empty() ? "<none>" : launchPreview.source.c_str(),
    launchPreview.protocolValid ? launchPreview.method.c_str() :
      (launchPreview.protocolPresent ? "invalid" : "none"),
    launchPreview.parentWindowLaunch ? "yes" : "no",
    launchPreview.sessionLoginProvided ? "yes" : "no"
  );
  lines.push_back(buffer);

  if (launchPreview.protocolPresent)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Launch protocol: version=%s match=%s mirror=%d token=%s",
      launchPreview.version.empty() ? "<none>" : launchPreview.version.c_str(),
      launchPreview.versionMatches ? "yes" : "no",
      launchPreview.mirrorIndex,
      MaskSensitiveValue(launchPreview.token).c_str()
    );
    lines.push_back(buffer);
  }

  if (launchPreview.mapIdProvided || launchPreview.sessionLoginProvided || !launchPreview.serverName.empty())
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Launch targets: mapId=%s server=%s uid=%s",
      launchPreview.mapIdProvided ? launchPreview.mapId.c_str() : "<none>",
      launchPreview.serverName.empty() ? "<none>" : launchPreview.serverName.c_str(),
      launchPreview.uid.empty() ? "<none>" : launchPreview.uid.c_str()
    );
    lines.push_back(buffer);
  }

  for (size_t i = 0; i < launchPreview.warnings.size(); ++i)
  {
    lines.push_back("Launch warning: " + launchPreview.warnings[i]);
  }

  if (sessionPreview.fileProvided)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Session import: %s | loaded=%s valid=%s players=%lu method=%s",
      sessionPreview.filePath.empty() ? "<none>" : fs::path(sessionPreview.filePath).filename().string().c_str(),
      sessionPreview.loaded ? "yes" : "no",
      sessionPreview.valid ? "yes" : "no",
      static_cast<unsigned long>(sessionPreview.players.size()),
      sessionPreview.method.empty() ? "<none>" : sessionPreview.method.c_str()
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Session teams: T1=%lu T2=%lu current=T%d %s",
      static_cast<unsigned long>(CountSessionTeamPlayers(sessionPreview, 1)),
      static_cast<unsigned long>(CountSessionTeamPlayers(sessionPreview, 2)),
      sessionPreview.currentTeamId,
      sessionPreview.currentHeroPersistentId.empty() ? "<hero unresolved>" : sessionPreview.currentHeroPersistentId.c_str()
    );
    lines.push_back(buffer);

    if (!sessionPreview.currentNickname.empty() || sessionPreview.mapIdProvided)
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Session current: %s | map=%s",
        sessionPreview.currentNickname.empty() ? "<none>" : sessionPreview.currentNickname.c_str(),
        sessionPreview.mapIdProvided ? sessionPreview.mapId.c_str() : "<none>"
      );
      lines.push_back(buffer);
    }
  }

  for (size_t i = 0; i < sessionPreview.warnings.size(); ++i)
  {
    lines.push_back("Session warning: " + sessionPreview.warnings[i]);
  }

  if (!mapCatalog.entries.empty() && mapBrowserState.selectedIndex < mapCatalog.entries.size())
  {
    const LinuxMapCatalogEntry& selectedEntry = mapCatalog.entries[mapBrowserState.selectedIndex];

    snprintf(
      buffer,
      sizeof(buffer),
      "Map browser: %lu/%lu via %s | Up/Down PgUp/PgDn Home/End wheel | Left/Right preview",
      static_cast<unsigned long>(mapBrowserState.selectedIndex + 1),
      static_cast<unsigned long>(mapCatalog.entries.size()),
      mapBrowserState.selectionSource.c_str()
    );
    lines.push_back(buffer);

    lines.push_back("Selected map: " + TruncateForOverlay(selectedEntry.title, 84));

    snprintf(
      buffer,
      sizeof(buffer),
      "Selected details: category=%s type=%s team=%d production=%s",
      selectedEntry.category.empty() ? "<none>" : selectedEntry.category.c_str(),
      selectedEntry.mapType.empty() ? "<none>" : selectedEntry.mapType.c_str(),
      selectedEntry.teamSize,
      selectedEntry.productionMode ? "yes" : "no"
    );
    lines.push_back(buffer);

    if (!selectedEntry.description.empty())
    {
      lines.push_back("Selected description: " + TruncateForOverlay(selectedEntry.description, 96));
    }

    if (selectedMapPreview.ready)
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Selected files: map=%s settings=%s scoring=%s objects=%lu scripted=%lu",
        selectedMapPreview.mapResolved ? "yes" : "no",
        selectedMapPreview.mapSettingsResolved ? "yes" : "no",
        selectedMapPreview.scoringTableResolved ? "yes" : "no",
        static_cast<unsigned long>(selectedMapPreview.objectCount),
        static_cast<unsigned long>(selectedMapPreview.scriptedObjectCount)
      );
      lines.push_back(buffer);

      if (!selectedMapPreview.scriptFile.empty())
      {
        lines.push_back("Map script: " + TruncateForOverlay(selectedMapPreview.scriptFile, 92));
      }

      if (selectedMapPreview.tactical.ready)
      {
        snprintf(
          buffer,
          sizeof(buffer),
          "Objectives: towers=%lu spawns=%lu lane=%lu neutral=%lu bosses=%lu shops=%lu glyphs=%lu",
          static_cast<unsigned long>(selectedMapPreview.tactical.towerCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.heroSpawnCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.laneSpawnerCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.neutralSpawnerCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.bossCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.shopCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.glyphCount)
        );
        lines.push_back(buffer);

        snprintf(
          buffer,
          sizeof(buffer),
          "Strategic: fountains=%lu main=%lu minigame=%lu flags=%lu | inset follows real map objects",
          static_cast<unsigned long>(selectedMapPreview.tactical.fountainCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.mainBuildingCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.minigameCount),
          static_cast<unsigned long>(selectedMapPreview.tactical.flagCount)
        );
        lines.push_back(buffer);
      }

      if (selectedMapPreview.settings.resolved)
      {
        snprintf(
          buffer,
          sizeof(buffer),
          "Map settings: source=%s chain=%lu delay=%d prime=%d force=%d dict=%lu",
          selectedMapPreview.settings.source.empty() ? "<none>" : selectedMapPreview.settings.source.c_str(),
          static_cast<unsigned long>(selectedMapPreview.settings.chainFiles.size()),
          selectedMapPreview.settings.battleStartDelay,
          selectedMapPreview.settings.startPrimePerTeam,
          selectedMapPreview.settings.force,
          static_cast<unsigned long>(selectedMapPreview.settings.dictionaryResourceCount)
        );
        lines.push_back(buffer);

        snprintf(
          buffer,
          sizeof(buffer),
          "Map rules: announcements=%s stats=%s portal=%s showHeroes=%s fullParty=%s",
          selectedMapPreview.settings.enableAnnouncements ? "yes" : "no",
          selectedMapPreview.settings.enableStatistics ? "yes" : "no",
          selectedMapPreview.settings.enablePortalTalent ? "yes" : "no",
          selectedMapPreview.settings.showAllHeroes ? "yes" : "no",
          selectedMapPreview.settings.fullPartyOnly ? "yes" : "no"
        );
        lines.push_back(buffer);

        if (!selectedMapPreview.settings.dictionaryKeysPreview.empty())
        {
          std::string dictionaryLine = "Dictionary preload: ";
          for (size_t i = 0; i < selectedMapPreview.settings.dictionaryKeysPreview.size(); ++i)
          {
            if (i > 0)
            {
              dictionaryLine += ", ";
            }
            dictionaryLine += selectedMapPreview.settings.dictionaryKeysPreview[i];
          }
          lines.push_back(TruncateForOverlay(dictionaryLine, 96));
        }
      }

      if (!selectedMapPreview.cameraSettingsRef.empty())
      {
        lines.push_back("Map camera: " + TruncateForOverlay(selectedMapPreview.cameraSettingsRef, 92));
      }

      snprintf(
        buffer,
        sizeof(buffer),
        "Artwork mode: %s via %s | changes=%lu",
        DescribeArtworkMode(artworkState.mode),
        artworkState.source.c_str(),
        static_cast<unsigned long>(artworkState.changeCount)
      );
      lines.push_back(buffer);

      if (selectedMapPreview.loadingBack.artworkLoaded || selectedMapPreview.loadingLogo.artworkLoaded)
      {
        const std::string backName = selectedMapPreview.loadingBack.sourceFile.empty() ?
          "<none>" : fs::path(selectedMapPreview.loadingBack.sourceFile).filename().string();
        const std::string logoName = selectedMapPreview.loadingLogo.sourceFile.empty() ?
          "<none>" : fs::path(selectedMapPreview.loadingLogo.sourceFile).filename().string();
        snprintf(
          buffer,
          sizeof(buffer),
          "Map loading art: back=%s %lux%lu logo=%s %lux%lu",
          backName.c_str(),
          selectedMapPreview.loadingBack.width,
          selectedMapPreview.loadingBack.height,
          logoName.c_str(),
          selectedMapPreview.loadingLogo.width,
          selectedMapPreview.loadingLogo.height
        );
        lines.push_back(buffer);
      }

      if (selectedMapPreview.minimapFirst.artworkLoaded ||
          selectedMapPreview.minimapSecond.artworkLoaded ||
          selectedMapPreview.minimapNeutral.artworkLoaded)
      {
        snprintf(
          buffer,
          sizeof(buffer),
          "Map minimaps: first=%s second=%s neutral=%s",
          selectedMapPreview.minimapFirst.artworkLoaded ? fs::path(selectedMapPreview.minimapFirst.sourceFile).filename().string().c_str() : "<none>",
          selectedMapPreview.minimapSecond.artworkLoaded ? fs::path(selectedMapPreview.minimapSecond.sourceFile).filename().string().c_str() : "<none>",
          selectedMapPreview.minimapNeutral.artworkLoaded ? fs::path(selectedMapPreview.minimapNeutral.sourceFile).filename().string().c_str() : "<none>"
        );
        lines.push_back(buffer);
      }
    }

    size_t selectedHeroIndex = localMatchPreview.selectedHeroIndex;
    if (localMatchPreview.ready && localMatchPreview.selectedSlotIndex < localMatchPreview.lineup.size())
    {
      selectedHeroIndex = localMatchPreview.lineup[localMatchPreview.selectedSlotIndex].heroIndex;
    }

    if (!heroCatalog.entries.empty() && selectedHeroIndex < heroCatalog.entries.size())
    {
      const LinuxHeroCatalogEntry& selectedHero = heroCatalog.entries[selectedHeroIndex];
      const bool selectedSlotReady =
        localMatchPreview.ready && localMatchPreview.selectedSlotIndex < localMatchPreview.lineup.size();
      const LinuxLocalMatchSlot* selectedSlot = selectedSlotReady ?
        &localMatchPreview.lineup[localMatchPreview.selectedSlotIndex] : 0;
      snprintf(
        buffer,
        sizeof(buffer),
        "Lineup slot: %lu/%lu T%d %s %s | ,/. slot [/] hero Backspace auto -/+ size Tab side Enter shuffle",
        static_cast<unsigned long>(localMatchPreview.selectedSlotIndex + 1),
        static_cast<unsigned long>(std::max(localMatchPreview.lineup.size(), static_cast<size_t>(1))),
        selectedSlot ? selectedSlot->team : localMatchPreview.humanTeam,
        selectedSlot && selectedSlot->human ? "Human" : "Bot",
        selectedSlot && selectedSlot->manualHero ? "manual" : (selectedSlot && selectedSlot->human ? "locked" : "auto")
      );
      lines.push_back(buffer);

      snprintf(
        buffer,
        sizeof(buffer),
        "Selected hero: [%s] %s | skins=%lu",
        selectedHero.persistentId.empty() ? selectedHero.id.c_str() : selectedHero.persistentId.c_str(),
        selectedHero.title.c_str(),
        static_cast<unsigned long>(selectedHero.skinCount)
      );
      lines.push_back(buffer);

      if (!selectedHero.description.empty())
      {
        lines.push_back("Hero description: " + TruncateForOverlay(selectedHero.description, 96));
      }

      if (!selectedHero.featuredSkinNames.empty())
      {
        std::string skinLine = "Featured skins: ";
        for (size_t i = 0; i < selectedHero.featuredSkinNames.size(); ++i)
        {
          if (i > 0)
          {
            skinLine += ", ";
          }
          skinLine += selectedHero.featuredSkinNames[i];
        }
        lines.push_back(TruncateForOverlay(skinLine, 96));
      }

      if (selectedHeroPreview.ready)
      {
        snprintf(
          buffer,
          sizeof(buffer),
          "Hero DB: %s attack=%s abilities=%lu talents=%lu/%lu/%lu stats=%lu upgrades=%lu",
          selectedHeroPreview.found ? "ready" : "missing",
          selectedHeroPreview.attackReady ? "yes" : "no",
          static_cast<unsigned long>(selectedHeroPreview.abilityCount),
          static_cast<unsigned long>(selectedHeroPreview.defaultTalentSetCount),
          static_cast<unsigned long>(selectedHeroPreview.defaultTalentLevelCount),
          static_cast<unsigned long>(selectedHeroPreview.defaultTalentSlotCount),
          static_cast<unsigned long>(selectedHeroPreview.statsCount),
          static_cast<unsigned long>(selectedHeroPreview.levelUpgradeCount)
        );
        lines.push_back(buffer);

        snprintf(
          buffer,
          sizeof(buffer),
          "Hero DB details: race=%s target=%.1f chase=%.1f aggro=%.1f resource=%s",
          selectedHeroPreview.heroRace.empty() ? "<none>" : selectedHeroPreview.heroRace.c_str(),
          selectedHeroPreview.targetingRange,
          selectedHeroPreview.chaseRange,
          selectedHeroPreview.aggroRange,
          selectedHeroPreview.uniqueResourceName.empty() ? "<none>" : selectedHeroPreview.uniqueResourceName.c_str()
        );
        lines.push_back(buffer);

        if (!selectedHeroPreview.recommendedStatSamples.empty())
        {
          lines.push_back(
            "Hero recommended stats: " +
            TruncateForOverlay(JoinPreviewSamples(selectedHeroPreview.recommendedStatSamples), 88));
        }

        if (!selectedHeroPreview.statSamples.empty())
        {
          lines.push_back(
            "Hero stat samples: " +
            TruncateForOverlay(JoinPreviewSamples(selectedHeroPreview.statSamples), 88));
        }

        if (!selectedHeroPreview.talentSamples.empty())
        {
          lines.push_back(
            "Hero talent samples: " +
            TruncateForOverlay(JoinPreviewSamples(selectedHeroPreview.talentSamples), 88));
        }

        if (selectedHeroPreview.defaultTalentIconCount > 0)
        {
          snprintf(
            buffer,
            sizeof(buffer),
            "Hero talent icons: %lu loaded across %lu default entries",
            static_cast<unsigned long>(selectedHeroPreview.defaultTalentIconCount),
            static_cast<unsigned long>(selectedHeroPreview.defaultTalentPreviews.size())
          );
          lines.push_back(buffer);
        }

        if (!selectedHeroPreview.featuredAbilities.empty())
        {
          std::vector<std::string> abilitySamples;
          for (size_t i = 0; i < selectedHeroPreview.featuredAbilities.size(); ++i)
          {
            const LinuxHeroAbilityPreview& abilityPreview = selectedHeroPreview.featuredAbilities[i];
            std::string sample = abilityPreview.isAttack ? "Attack" : abilityPreview.name;
            if (!abilityPreview.isAttack && !abilityPreview.type.empty())
            {
              sample += " [" + abilityPreview.type + "]";
            }
            AppendSampleValue(&abilitySamples, sample, 4);
          }

          if (!abilitySamples.empty())
          {
            lines.push_back(
              "Hero abilities: " +
              TruncateForOverlay(JoinPreviewSamples(abilitySamples), 88));
          }
        }

        if (!selectedHeroPreview.uniqueResourceTooltip.empty())
        {
          lines.push_back(
            "Hero resource tip: " +
            TruncateForOverlay(selectedHeroPreview.uniqueResourceTooltip, 92));
        }

        for (size_t i = 0; i < selectedHeroPreview.warnings.size(); ++i)
        {
          lines.push_back("Hero DB warning: " + selectedHeroPreview.warnings[i]);
        }
      }
    }

    if (localMatchPreview.ready)
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Local lineup: teamSize=%lu/%d generated=%lu via %s manual=%lu",
        static_cast<unsigned long>(localMatchPreview.teamSize),
        selectedEntry.teamSize,
        static_cast<unsigned long>(localMatchPreview.generationCount),
        localMatchPreview.generationSource.c_str(),
        static_cast<unsigned long>(CountManualHeroOverrides(localMatchPreview))
      );
      lines.push_back(buffer);

      for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
      {
        const LinuxLocalMatchSlot& slot = localMatchPreview.lineup[i];
        snprintf(
          buffer,
          sizeof(buffer),
          "%s T%d %s %s%s",
          i == localMatchPreview.selectedSlotIndex ? " >" : "  ",
          slot.team,
          slot.human ? "Human" : "Bot  ",
          slot.heroTitle.c_str(),
          slot.manualHero ? " [manual]" : ""
        );
        lines.push_back(buffer);
      }
    }

    if (engineMapStartPreview.ready)
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Engine start slots: total=%lu filled=%lu free=%lu overflow=%lu teams=%lu/%lu via %s",
        static_cast<unsigned long>(engineMapStartPreview.totalSpawners),
        static_cast<unsigned long>(engineMapStartPreview.assignedSlots),
        static_cast<unsigned long>(engineMapStartPreview.totalSpawners - engineMapStartPreview.assignedSlots),
        static_cast<unsigned long>(engineMapStartPreview.overflowPlayers),
        static_cast<unsigned long>(engineMapStartPreview.team1Spawners),
        static_cast<unsigned long>(engineMapStartPreview.team2Spawners),
        engineMapStartPreview.source.empty() ? "<none>" : engineMapStartPreview.source.c_str()
      );
      lines.push_back(buffer);

      snprintf(
        buffer,
        sizeof(buffer),
        "Engine start players: humans=%lu bots=%lu maxTeam=%d seed=%d",
        static_cast<unsigned long>(engineMapStartPreview.humanPlayers),
        static_cast<unsigned long>(engineMapStartPreview.botPlayers),
        engineMapStartPreview.maxPlayersPerTeam,
        engineMapStartPreview.randomSeed
      );
      lines.push_back(buffer);

      size_t selectedSpawnIndex = static_cast<size_t>(-1);
      for (size_t i = 0; i < engineMapStartPreview.slots.size(); ++i)
      {
        if (engineMapStartPreview.slots[i].lineupIndex == localMatchPreview.selectedSlotIndex)
        {
          selectedSpawnIndex = i;
          break;
        }
      }

      if (selectedSpawnIndex != static_cast<size_t>(-1))
      {
        const LinuxEngineMapStartSlot& selectedStartSlot = engineMapStartPreview.slots[selectedSpawnIndex];
        snprintf(
          buffer,
          sizeof(buffer),
          "Selected spawn slot: S%02lu T%d %s %s %s @ %.1f, %.1f",
          static_cast<unsigned long>(selectedStartSlot.spawnIndex),
          selectedStartSlot.team,
          selectedStartSlot.filled ? (selectedStartSlot.human ? "Human" : "Bot") : "Empty",
          selectedStartSlot.heroTitle.empty() ? "<none>" : selectedStartSlot.heroTitle.c_str(),
          selectedStartSlot.nickname.empty() ? "<anon>" : selectedStartSlot.nickname.c_str(),
          selectedStartSlot.translateX,
          selectedStartSlot.translateY
        );
        lines.push_back(buffer);
      }
    }

    lines.push_back("Nearby maps:");
    const size_t start = mapBrowserState.selectedIndex > 2 ? mapBrowserState.selectedIndex - 2 : 0;
    const size_t end = std::min(mapCatalog.entries.size(), start + 5);
    for (size_t i = start; i < end; ++i)
    {
      const LinuxMapCatalogEntry& entry = mapCatalog.entries[i];
      lines.push_back(
        std::string(i == mapBrowserState.selectedIndex ? "  > " : "    ") +
        TruncateForOverlay(entry.title, 64)
      );
    }
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "Engine bootstrap: %s | focus=%s | uptime=%.1fs",
    environment.engineReady ? "ready" : "not ready",
    NMainFrame::IsAppActive() ? "active" : "inactive",
    elapsedSeconds
  );
  lines.push_back(buffer);

  snprintf(
    buffer,
    sizeof(buffer),
    "Window: %lux%lu | source=%s | timeout=%s",
    settings.width,
    settings.height,
    (settings.widthFromParent || settings.heightFromParent) ? "parent" : "default/cli",
    settings.runSeconds > 0.0 ? NStr::StrFmt("%.1fs", settings.runSeconds) : "none"
  );
  lines.push_back(buffer);

  snprintf(
    buffer,
    sizeof(buffer),
    "Flags: spectator=%s tutorial=%s replay=%s",
    settings.spectator ? "yes" : "no",
    settings.tutorial ? "yes" : "no",
    settings.replayFile.empty() ? "no" : "yes"
  );
  lines.push_back(buffer);
  if (settings.demoCycleSeconds > 0.0)
  {
    snprintf(buffer, sizeof(buffer), "Demo cycle: %.1fs auto-advance", settings.demoCycleSeconds);
    lines.push_back(buffer);
  }
  snprintf(
    buffer,
    sizeof(buffer),
    "Hero catalog: legal=%lu warnings=%lu",
    static_cast<unsigned long>(heroCatalog.entries.size()),
    static_cast<unsigned long>(heroCatalog.warnings.size())
  );
  lines.push_back(buffer);
  snprintf(
    buffer,
    sizeof(buffer),
    "Input pipeline: X11 -> LinuxHwInput -> Input::Binds (%lu total events)",
    static_cast<unsigned long>(inputState.totalEvents)
  );
  lines.push_back(buffer);
  snprintf(
    buffer,
    sizeof(buffer),
    "Input bootstrap: ready=%s input.cfg=%s input_new.cfg=%s controls=%lu binds=%lu contexts=%lu commands=%lu",
    inputState.initialized ? "yes" : "no",
    inputState.inputConfigLoaded ? "yes" : "no",
    inputState.inputOverrideLoaded ? "yes" : "no",
    static_cast<unsigned long>(inputState.hardwareControlCount),
    static_cast<unsigned long>(inputState.bindStringCount),
    static_cast<unsigned long>(inputState.bindContextCount),
    static_cast<unsigned long>(inputState.commandBindingHits)
  );
  lines.push_back(buffer);
  snprintf(
    buffer,
    sizeof(buffer),
    "Config bootstrap: default=%s social=%s game=%s spectator=%s placeholders=%lu",
    configPreview.defaultLoaded ? "yes" : "no",
    configPreview.socialLoaded ? "yes" : "no",
    configPreview.gameLoaded ? "yes" : "no",
    settings.spectator ? (configPreview.spectatorLoaded ? "yes" : "no") : "n/a",
    static_cast<unsigned long>(configPreview.placeholderCommandCount)
  );
  lines.push_back(buffer);
  snprintf(
    buffer,
    sizeof(buffer),
    "Config vars: language=%s fullscreen=%s resolution=%s local_game=%s",
    configPreview.language.empty() ? "<none>" : configPreview.language.c_str(),
    configPreview.gfxFullscreen.empty() ? "<none>" : configPreview.gfxFullscreen.c_str(),
    configPreview.gfxResolution.empty() ? "<none>" : configPreview.gfxResolution.c_str(),
    configPreview.localGame.empty() ? "<none>" : configPreview.localGame.c_str()
  );
  lines.push_back(buffer);
  if (!configPreview.loginAddress.empty() || !configPreview.socialLoginAddress.empty())
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Config network: login=%s social=%s",
      configPreview.loginAddress.empty() ? "<none>" : configPreview.loginAddress.c_str(),
      configPreview.socialLoginAddress.empty() ? "<none>" : configPreview.socialLoginAddress.c_str()
    );
    lines.push_back(buffer);
  }
  snprintf(
    buffer,
    sizeof(buffer),
    "Content probe: data=%s localization=%s locale=%s",
    contentProbe.dataMounted ? "mounted" : "missing",
    contentProbe.localizationMounted ? "mounted" : "missing",
    contentProbe.locale.empty() ? "<none>" : contentProbe.locale.c_str()
  );
  lines.push_back(buffer);

  if (contentProbe.dataMounted)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Data assets: UI xdb=%lu UI screens=%lu Social xdb=%lu",
      static_cast<unsigned long>(contentProbe.uiXdbCount),
      static_cast<unsigned long>(contentProbe.uiScreenFileCount),
      static_cast<unsigned long>(contentProbe.socialXdbCount)
    );
    lines.push_back(buffer);

    if (!contentProbe.dataSampleFile.empty())
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Data sample: %s (%d bytes)",
        contentProbe.dataSampleFile.c_str(),
        contentProbe.dataSampleSize
      );
      lines.push_back(buffer);
    }
  }

  if (contentProbe.localizationMounted)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Localization: %s (%lu files)",
      contentProbe.localizationRoot.string().c_str(),
      static_cast<unsigned long>(contentProbe.localizationFileCount)
    );
    lines.push_back(buffer);

    if (!contentProbe.localizationSampleFile.empty())
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Locale sample: %s (%d bytes)",
        contentProbe.localizationSampleFile.c_str(),
        contentProbe.localizationSampleSize
      );
      lines.push_back(buffer);
    }
  }

  for (size_t i = 0; i < contentProbe.warnings.size(); ++i)
  {
    lines.push_back("Content warning: " + contentProbe.warnings[i]);
  }

  if (loadingPreview.layoutFound)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Loading UI: %lux%lu | flash=%s",
      loadingPreview.width,
      loadingPreview.height,
      loadingPreview.flashAsset.empty() ? "<missing>" : loadingPreview.flashAsset.c_str()
    );
    lines.push_back(buffer);

    if (loadingPreview.flashAssetSize > 0)
    {
      snprintf(buffer, sizeof(buffer), "Loading asset size: %d bytes", loadingPreview.flashAssetSize);
      lines.push_back(buffer);
    }
  }

  if (loadingPreview.artworkLoaded)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Loading artwork: %lux%lu | %s",
      loadingPreview.artworkWidth,
      loadingPreview.artworkHeight,
      fs::path(loadingPreview.artworkFile).filename().string().c_str()
    );
    lines.push_back(buffer);
  }

  for (size_t i = 0; i < loadingPreview.localizedProperties.size() && i < 3; ++i)
  {
    lines.push_back(
      "Loading text " + loadingPreview.localizedProperties[i].first + ": " +
      TruncateForOverlay(loadingPreview.localizedProperties[i].second, 92)
    );
  }

  for (size_t i = 0; i < loadingPreview.warnings.size(); ++i)
  {
    lines.push_back("Loading warning: " + loadingPreview.warnings[i]);
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "Loading DB: ready=%s statuses=%lu tips=%lu locales=%lu forceColors=%lu",
    loadingUiPreview.ready ? "yes" : "no",
    static_cast<unsigned long>(loadingUiPreview.statusCount),
    static_cast<unsigned long>(loadingUiPreview.tipCount),
    static_cast<unsigned long>(loadingUiPreview.localeCount),
    static_cast<unsigned long>(loadingUiPreview.forceColorCount)
  );
  lines.push_back(buffer);

  if (loadingUiPreview.ready)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Loading extras: minimap=%s icons=%lu smartChat=%s chat=%lu reports=%lu binds=%lu",
      loadingUiPreview.minimapReady ? "yes" : "no",
      static_cast<unsigned long>(loadingUiPreview.minimapIconCount),
      loadingUiPreview.smartChatReady ? "yes" : "no",
      static_cast<unsigned long>(loadingUiPreview.chatChannelCount),
      static_cast<unsigned long>(loadingUiPreview.reportTypeCount),
      static_cast<unsigned long>(loadingUiPreview.bindCount)
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Loading modes: maneuvers=%s guard=%s guild=%s custom=%s recentPlayers=%d",
      loadingUiPreview.maneuversModeReady ? "yes" : "no",
      loadingUiPreview.guardModeReady ? "yes" : "no",
      loadingUiPreview.guildModeReady ? "yes" : "no",
      loadingUiPreview.customModeReady ? "yes" : "no",
      loadingUiPreview.recentPlayers
    );
    lines.push_back(buffer);

    if (!loadingUiPreview.statusSamples.empty())
    {
      lines.push_back("Loading status samples: " +
        TruncateForOverlay(JoinPreviewSamples(loadingUiPreview.statusSamples), 84));
    }

    if (!loadingRuntimeDriver.samples.empty())
    {
      lines.push_back("Loading runtime samples: " +
        TruncateForOverlay(JoinPreviewSamples(loadingRuntimeDriver.samples), 84));
    }

    if (loadingHeroesRuntimePreview.ready)
    {
      snprintf(
        buffer,
        sizeof(buffer),
        "Loading runtime heroes: count=%lu humans=%lu bots=%lu disconnected=%lu premium=%lu locale=%lu",
        static_cast<unsigned long>(loadingHeroesRuntimePreview.heroes.size()),
        static_cast<unsigned long>(loadingHeroesRuntimePreview.humanCount),
        static_cast<unsigned long>(loadingHeroesRuntimePreview.botCount),
        static_cast<unsigned long>(loadingHeroesRuntimePreview.disconnectedCount),
        static_cast<unsigned long>(loadingHeroesRuntimePreview.premiumCount),
        static_cast<unsigned long>(loadingHeroesRuntimePreview.localeCount)
      );
      lines.push_back(buffer);
    }

    if (!loadingHeroesRuntimePreview.samples.empty())
    {
      lines.push_back("Loading hero samples: " +
        TruncateForOverlay(JoinPreviewSamples(loadingHeroesRuntimePreview.samples), 84));
    }

    if (!loadingHeroesRuntimePreview.metaSamples.empty())
    {
      lines.push_back("Loading hero meta: " +
        TruncateForOverlay(JoinPreviewSamples(loadingHeroesRuntimePreview.metaSamples), 84));
    }

    if (!loadingUiPreview.localeSamples.empty())
    {
      lines.push_back("Loading locales: " +
        TruncateForOverlay(JoinPreviewSamples(loadingUiPreview.localeSamples), 84));
    }

    if (!loadingUiPreview.modeSamples.empty())
    {
      lines.push_back("Loading mode samples: " +
        TruncateForOverlay(JoinPreviewSamples(loadingUiPreview.modeSamples), 84));
    }

    if (!loadingUiPreview.statuses.empty() && loadingUiState.statusIndex < loadingUiPreview.statuses.size())
    {
      const LinuxLoadingStatusEntry& status = loadingUiPreview.statuses[loadingUiState.statusIndex];
      lines.push_back(
        "Loading status: " + status.key + " -> " +
        TruncateForOverlay(status.text.empty() ? "<empty>" : status.text, 84)
      );
    }

    if (!loadingUiState.runtimeEvent.empty())
    {
      lines.push_back(
        "Loading runtime: " + loadingUiState.runtimeEvent + " -> " +
        TruncateForOverlay(
          loadingUiState.runtimeStatusText.empty() ? "<empty>" : loadingUiState.runtimeStatusText,
          84
        )
      );
    }

    if (!loadingUiPreview.locales.empty())
    {
      if (loadingUiState.currentLocaleIndex < loadingUiPreview.locales.size())
      {
        const LinuxLoadingLocaleEntry& currentLocale = loadingUiPreview.locales[loadingUiState.currentLocaleIndex];
        lines.push_back(
          "Loading locale current: " + currentLocale.locale + " -> " +
          TruncateForOverlay(currentLocale.tooltip.empty() ? "<empty>" : currentLocale.tooltip, 76)
        );
      }

      if (loadingUiState.enemyLocaleIndex < loadingUiPreview.locales.size())
      {
        const LinuxLoadingLocaleEntry& enemyLocale = loadingUiPreview.locales[loadingUiState.enemyLocaleIndex];
        lines.push_back(
          "Loading locale enemy: " + enemyLocale.locale + " -> " +
          TruncateForOverlay(enemyLocale.tooltip.empty() ? "<empty>" : enemyLocale.tooltip, 78)
        );
      }
    }

    if (!loadingUiPreview.modes.empty() && loadingUiState.modeIndex < loadingUiPreview.modes.size())
    {
      const LinuxLoadingModeEntry& mode = loadingUiPreview.modes[loadingUiState.modeIndex];
      lines.push_back(
        "Loading mode active: " + mode.id + " -> " +
        TruncateForOverlay(mode.tooltip.empty() ? "<empty>" : mode.tooltip, 80)
      );
    }

    if (!loadingUiPreview.sampleTip.empty())
    {
      lines.push_back("Loading tip: " + TruncateForOverlay(loadingUiPreview.sampleTip, 92));
    }

    if (!loadingUiPreview.tips.empty() && loadingUiState.tipIndex < loadingUiPreview.tips.size())
    {
      lines.push_back("Loading tip active: " +
        TruncateForOverlay(loadingUiPreview.tips[loadingUiState.tipIndex], 92));
    }

    lines.push_back("Loading controls: S status | T tip | L current locale | E enemy locale | M mode");
  }

  for (size_t i = 0; i < loadingRuntimeDriver.warnings.size(); ++i)
  {
    lines.push_back("Loading runtime warning: " + loadingRuntimeDriver.warnings[i]);
  }

  for (size_t i = 0; i < loadingHeroesRuntimePreview.warnings.size(); ++i)
  {
    lines.push_back("Loading heroes warning: " + loadingHeroesRuntimePreview.warnings[i]);
  }

  for (size_t i = 0; i < loadingUiPreview.warnings.size(); ++i)
  {
    lines.push_back("Loading DB warning: " + loadingUiPreview.warnings[i]);
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "Map catalog: total=%lu production=%lu pvp=%lu pve=%lu tutorial=%lu",
    static_cast<unsigned long>(mapCatalog.descriptorCount),
    static_cast<unsigned long>(mapCatalog.productionDescriptorCount),
    static_cast<unsigned long>(mapCatalog.pvpCount),
    static_cast<unsigned long>(mapCatalog.pveCount),
    static_cast<unsigned long>(mapCatalog.tutorialCount)
  );
  lines.push_back(buffer);

  for (size_t i = 0; i < mapCatalog.warnings.size(); ++i)
  {
    lines.push_back("Map warning: " + mapCatalog.warnings[i]);
  }

  for (size_t i = 0; i < selectedMapPreview.warnings.size(); ++i)
  {
    lines.push_back("Selected map warning: " + selectedMapPreview.warnings[i]);
  }

  for (size_t i = 0; i < selectedMapPreview.loadingBack.warnings.size(); ++i)
  {
    lines.push_back("Selected back warning: " + selectedMapPreview.loadingBack.warnings[i]);
  }

  for (size_t i = 0; i < selectedMapPreview.loadingLogo.warnings.size(); ++i)
  {
    lines.push_back("Selected logo warning: " + selectedMapPreview.loadingLogo.warnings[i]);
  }

  for (size_t i = 0; i < selectedMapPreview.minimapFirst.warnings.size(); ++i)
  {
    lines.push_back("Selected minimap first warning: " + selectedMapPreview.minimapFirst.warnings[i]);
  }

  for (size_t i = 0; i < selectedMapPreview.minimapSecond.warnings.size(); ++i)
  {
    lines.push_back("Selected minimap second warning: " + selectedMapPreview.minimapSecond.warnings[i]);
  }

  for (size_t i = 0; i < selectedMapPreview.minimapNeutral.warnings.size(); ++i)
  {
    lines.push_back("Selected minimap neutral warning: " + selectedMapPreview.minimapNeutral.warnings[i]);
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "RootFS: mounted=%s data=%s localization=%s",
    rootFileSystemPreview.mounted ? "yes" : "no",
    rootFileSystemPreview.dataRegistered ? "yes" : "no",
    rootFileSystemPreview.localizationRegistered ? "yes" : "no"
  );
  lines.push_back(buffer);
  lines.push_back("Text refs: RootFS-first with direct-file fallback");

  if (!rootFileSystemPreview.sampleFile.empty() && rootFileSystemPreview.sampleFileSize > 0)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "RootFS sample: %s (%d bytes)",
      rootFileSystemPreview.sampleFile.c_str(),
      rootFileSystemPreview.sampleFileSize
    );
    lines.push_back(buffer);
  }

  if (!rootFileSystemPreview.textRefValue.empty())
  {
    lines.push_back("RootFS textref: " + TruncateForOverlay(rootFileSystemPreview.textRefValue, 92));
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "UIRoot: ready=%s screens=%lu cursors=%lu contents=%lu consts=%lu",
    uiRootPreview.ready ? "yes" : "no",
    static_cast<unsigned long>(uiRootPreview.screenCount),
    static_cast<unsigned long>(uiRootPreview.cursorCount),
    static_cast<unsigned long>(uiRootPreview.contentGroupCount),
    static_cast<unsigned long>(uiRootPreview.constantCount)
  );
  lines.push_back(buffer);

  if (uiRootPreview.ready)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "UI metadata: scripts=%lu fonts=%lu aliases=%lu prefs=%s voting=%s",
      static_cast<unsigned long>(uiRootPreview.scriptCount),
      static_cast<unsigned long>(uiRootPreview.fontStyleCount),
      static_cast<unsigned long>(uiRootPreview.styleAliasCount),
      uiRootPreview.preferencesReady ? "yes" : "no",
      uiRootPreview.votingReady ? "yes" : "no"
    );
    lines.push_back(buffer);

    if (!uiRootPreview.screenSamples.empty())
    {
      lines.push_back("UI screen samples: " +
        TruncateForOverlay(JoinPreviewSamples(uiRootPreview.screenSamples), 88));
    }

    if (!uiRootPreview.contentSamples.empty())
    {
      lines.push_back("UI content groups: " +
        TruncateForOverlay(JoinPreviewSamples(uiRootPreview.contentSamples), 88));
    }
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "SessionRoot: ready=%s ui=%s logic=%s visual=%s roll=%s messages=%s",
    sessionRootPreview.ready ? "yes" : "no",
    sessionRootPreview.uiRootReady ? "yes" : "no",
    sessionRootPreview.logicRootReady ? "yes" : "no",
    sessionRootPreview.visualRootReady ? "yes" : "no",
    sessionRootPreview.rollSettingsReady ? "yes" : "no",
    sessionRootPreview.sessionMessagesReady ? "yes" : "no"
  );
  lines.push_back(buffer);

  if (sessionRootPreview.ready)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Session logic: heroes=%lu legal=%lu maps=%lu ai=%s bots=%s",
      static_cast<unsigned long>(sessionRootPreview.logicHeroCount),
      static_cast<unsigned long>(sessionRootPreview.logicLegalHeroCount),
      static_cast<unsigned long>(sessionRootPreview.logicMapCount),
      sessionRootPreview.logicAiReady ? "yes" : "no",
      sessionRootPreview.logicBotsSettingsReady ? "yes" : "no"
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Session rules: unitCats=%lu/%lu teamNames=%lu portal=%s ranks=%lu levels=%lu",
      static_cast<unsigned long>(sessionRootPreview.uiUnitCategoryCount),
      static_cast<unsigned long>(sessionRootPreview.uiUnitCategoryParamCount),
      static_cast<unsigned long>(sessionRootPreview.logicTeamNameCount),
      sessionRootPreview.logicPortalReady ? "yes" : "no",
      static_cast<unsigned long>(sessionRootPreview.logicHeroRankCount),
      static_cast<unsigned long>(sessionRootPreview.logicLevelCount)
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Session gameplay: scoring=%s glyphs=%lu levelups=%lu formulas=%lu/%lu/%lu units=%lu guild=%lu",
      sessionRootPreview.logicScoringReady ? "yes" : "no",
      static_cast<unsigned long>(sessionRootPreview.logicGlyphCount),
      static_cast<unsigned long>(sessionRootPreview.logicLevelUpCount),
      static_cast<unsigned long>(sessionRootPreview.logicFloatFormulaCount),
      static_cast<unsigned long>(sessionRootPreview.logicBoolFormulaCount),
      static_cast<unsigned long>(sessionRootPreview.logicIntFormulaCount),
      static_cast<unsigned long>(sessionRootPreview.logicUnitParameterCount),
      static_cast<unsigned long>(sessionRootPreview.logicGuildBuffCount)
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Session AI: waves=%d levelCap=%d emblem=%d botsAI=%s midOnly=%s tGo=%d tTp=%d",
      sessionRootPreview.logicCreepsWavesDelay,
      sessionRootPreview.logicCreepLevelCap,
      sessionRootPreview.logicBaseEmblemHeroNeeds,
      sessionRootPreview.botsAiEnabled ? "yes" : "no",
      sessionRootPreview.botsMidOnly ? "yes" : "no",
      sessionRootPreview.logicBotsTimeToGo,
      sessionRootPreview.logicBotsTimeToTeleport
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Session scoring: achievements=%lu titles=%lu scores=%lu teleports=%lu rollMods=%lu/%lu",
      static_cast<unsigned long>(sessionRootPreview.logicScoringAchievementCount),
      static_cast<unsigned long>(sessionRootPreview.logicScoringHeroTitleCount),
      static_cast<unsigned long>(sessionRootPreview.logicScoringDescriptionCount),
      static_cast<unsigned long>(sessionRootPreview.logicScoringTeleporterCount),
      static_cast<unsigned long>(sessionRootPreview.rollRatingModifierCount),
      static_cast<unsigned long>(sessionRootPreview.rollFullPartyModifierCount)
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Session visual: cameras=%lu animSets=%lu winLose=%lu auras=%lu/%lu uiEvents=%lu",
      static_cast<unsigned long>(sessionRootPreview.visualCameraCount),
      static_cast<unsigned long>(sessionRootPreview.visualAnimSetCount),
      static_cast<unsigned long>(sessionRootPreview.visualWinLoseCount),
      static_cast<unsigned long>(sessionRootPreview.visualSelfAuraCount),
      static_cast<unsigned long>(sessionRootPreview.visualAuraCount),
      static_cast<unsigned long>(sessionRootPreview.visualUiEventCount)
    );
    lines.push_back(buffer);

    snprintf(
      buffer,
      sizeof(buffer),
      "Roll/bootstrap: pvp=%s mode=%s win=%d cap=%d prem=%lu guild=%lu excl=%d/%d",
      sessionRootPreview.rollPvpReady ? "yes" : "no",
      sessionRootPreview.rollPvpModeName.empty() ? "<none>" : sessionRootPreview.rollPvpModeName.c_str(),
      sessionRootPreview.rollPvpContainersOnWin,
      sessionRootPreview.rollPvpScoreCap,
      static_cast<unsigned long>(sessionRootPreview.rollPvpPremiumContainerCount),
      static_cast<unsigned long>(sessionRootPreview.rollGuildLevelCount),
      sessionRootPreview.rollRequiredLevelForExclusiveTalents,
      sessionRootPreview.rollRequiredRatingForExclusiveTalents
    );
    lines.push_back(buffer);

    if (!sessionRootPreview.uiUnitCategorySamples.empty())
    {
      lines.push_back("Session unit categories: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.uiUnitCategorySamples), 88));
    }

    if (!sessionRootPreview.heroSamples.empty())
    {
      lines.push_back("Session hero samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.heroSamples), 88));
    }

    if (!sessionRootPreview.mapSamples.empty())
    {
      lines.push_back("Session map samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.mapSamples), 88));
    }

    if (!sessionRootPreview.logicTeamNameSamples.empty())
    {
      lines.push_back("Session team names: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.logicTeamNameSamples), 88));
    }

    if (!sessionRootPreview.logicGlyphSamples.empty())
    {
      lines.push_back("Session glyph samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.logicGlyphSamples), 88));
    }

    if (!sessionRootPreview.logicScoreSamples.empty())
    {
      lines.push_back("Session score samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.logicScoreSamples), 88));
    }

    if (!sessionRootPreview.logicGuildBuffSamples.empty())
    {
      lines.push_back("Session guild buffs: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.logicGuildBuffSamples), 88));
    }

    if (!sessionRootPreview.logicRankSamples.empty())
    {
      lines.push_back("Session rank samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.logicRankSamples), 88));
    }

    if (!sessionRootPreview.visualCameraSamples.empty())
    {
      lines.push_back("Session camera samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.visualCameraSamples), 88));
    }

    if (!sessionRootPreview.visualUiEventSamples.empty())
    {
      lines.push_back("Session UI event samples: " +
        TruncateForOverlay(JoinPreviewSamples(sessionRootPreview.visualUiEventSamples), 88));
    }

    if (!sessionRootPreview.dxErrorTitle.empty())
    {
      lines.push_back("Session DX title: " + TruncateForOverlay(sessionRootPreview.dxErrorTitle, 92));
    }
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "SoundRoot: ready=%s scenes=%lu groups=%lu ambience=%lu",
    soundRootPreview.ready ? "yes" : "no",
    static_cast<unsigned long>(soundRootPreview.sceneCount),
    static_cast<unsigned long>(soundRootPreview.sceneGroupCount),
    static_cast<unsigned long>(soundRootPreview.ambienceGroupCount)
  );
  lines.push_back(buffer);

  if (soundRootPreview.ready)
  {
    snprintf(
      buffer,
      sizeof(buffer),
      "Sound events: heartbeat=%s ambient=%s prefs=%s lastHit=%s",
      soundRootPreview.heartbeatReady ? "yes" : "no",
      soundRootPreview.ambientReady ? "yes" : "no",
      soundRootPreview.preferencesVolumeReady ? "yes" : "no",
      soundRootPreview.lastHitReady ? "yes" : "no"
    );
    lines.push_back(buffer);

    if (!soundRootPreview.cueSamples.empty())
    {
      lines.push_back("Sound cue samples: " +
        TruncateForOverlay(JoinPreviewSamples(soundRootPreview.cueSamples), 88));
    }
  }

  snprintf(
    buffer,
    sizeof(buffer),
    "Resource catalogs: ready=%s talents=%lu consumables=%lu marketing=%lu",
    resourceCatalogPreview.ready ? "yes" : "no",
    static_cast<unsigned long>(resourceCatalogPreview.talentCount),
    static_cast<unsigned long>(resourceCatalogPreview.consumableCount),
    static_cast<unsigned long>(resourceCatalogPreview.marketingItemCount)
  );
  lines.push_back(buffer);

  if (!resourceCatalogPreview.talentSamples.empty())
  {
    lines.push_back("Talent samples: " +
      TruncateForOverlay(JoinPreviewSamples(resourceCatalogPreview.talentSamples), 92));
  }

  if (!resourceCatalogPreview.consumableSamples.empty())
  {
    lines.push_back("Consumable samples: " +
      TruncateForOverlay(JoinPreviewSamples(resourceCatalogPreview.consumableSamples), 92));
  }

  for (size_t i = 0; i < rootFileSystemPreview.warnings.size(); ++i)
  {
    lines.push_back("RootFS warning: " + rootFileSystemPreview.warnings[i]);
  }

  for (size_t i = 0; i < uiRootPreview.warnings.size(); ++i)
  {
    lines.push_back("UIRoot warning: " + uiRootPreview.warnings[i]);
  }

  for (size_t i = 0; i < sessionRootPreview.warnings.size(); ++i)
  {
    lines.push_back("SessionRoot warning: " + sessionRootPreview.warnings[i]);
  }

  for (size_t i = 0; i < soundRootPreview.warnings.size(); ++i)
  {
    lines.push_back("SoundRoot warning: " + soundRootPreview.warnings[i]);
  }

  for (size_t i = 0; i < resourceCatalogPreview.warnings.size(); ++i)
  {
    lines.push_back("Resource warning: " + resourceCatalogPreview.warnings[i]);
  }

  for (size_t i = 0; i < configPreview.warnings.size(); ++i)
  {
    lines.push_back("Config warning: " + configPreview.warnings[i]);
  }

  for (size_t i = 0; i < inputState.warnings.size(); ++i)
  {
    lines.push_back("Input warning: " + inputState.warnings[i]);
  }

  for (size_t i = 0; i < heroCatalog.warnings.size(); ++i)
  {
    lines.push_back("Hero warning: " + heroCatalog.warnings[i]);
  }

  for (size_t i = 0; i < localMatchPreview.warnings.size(); ++i)
  {
    lines.push_back("Local match warning: " + localMatchPreview.warnings[i]);
  }

  for (size_t i = 0; i < engineMapStartPreview.warnings.size(); ++i)
  {
    lines.push_back("Engine start warning: " + engineMapStartPreview.warnings[i]);
  }

  lines.push_back(environment.gameRoot.empty() ? "Game root: <not found>" : "Game root: " + environment.gameRoot.string());
  lines.push_back(environment.baseDir.empty() ? "Base dir: <not initialized>" : "Base dir: " + environment.baseDir.string());
  lines.push_back(environment.binDir.empty() ? "Bin dir: <not initialized>" : "Bin dir: " + environment.binDir.string());
  lines.push_back(environment.userDir.empty() ? "User dir: <not initialized>" : "User dir: " + environment.userDir.string());
  lines.push_back(environment.logsDir.empty() ? "Logs: <not initialized>" : "Logs: " + environment.logsDir.string());

  if (!settings.replayFile.empty())
  {
    lines.push_back("Replay file: " + settings.replayFile.string());
  }

  if (!inputState.recentEvents.empty())
  {
    lines.push_back("Recent bind events:");
    for (size_t i = 0; i < inputState.recentEvents.size(); ++i)
    {
      lines.push_back("  " + inputState.recentEvents[i]);
    }
  }

  lines.push_back("Close the window to exit. Native renderer/gameplay port is still in progress.");
  return lines;
}

void SetOpenGlColor(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha = 255)
{
  glColor4f(
    static_cast<float>(red) / 255.0f,
    static_cast<float>(green) / 255.0f,
    static_cast<float>(blue) / 255.0f,
    static_cast<float>(alpha) / 255.0f
  );
}

void DrawOpenGlRect(int x, int y, int width, int height)
{
  glBegin(GL_QUADS);
  glVertex2i(x, y);
  glVertex2i(x + width, y);
  glVertex2i(x + width, y + height);
  glVertex2i(x, y + height);
  glEnd();
}

void DrawOpenGlText(LinuxWindowOverlay* overlay, int x, int y, const std::string& text)
{
  if (!overlay->fontDisplayListsReady || text.empty())
  {
    return;
  }

  glRasterPos2i(x, y);
  glListBase(overlay->fontDisplayListBase);
  glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, text.c_str());
}

bool DrawWindowOverlayOpenGl(
  LinuxWindowOverlay* overlay,
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxLaunchPreview& launchPreview,
  const LinuxSessionPreview& sessionPreview,
  const LinuxConfigBootstrapPreview& configPreview,
  const LinuxContentProbe& contentProbe,
  const LinuxLoadingScreenPreview& loadingPreview,
  const LinuxLoadingUiPreview& loadingUiPreview,
  const LinuxLoadingRuntimeDriver& loadingRuntimeDriver,
  const LinuxLoadingHeroesRuntimePreview& loadingHeroesRuntimePreview,
  const LinuxLoadingUiState& loadingUiState,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxArtworkSelectionState& artworkState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  const LinuxUiRootPreview& uiRootPreview,
  const LinuxSessionRootPreview& sessionRootPreview,
  const LinuxSoundRootPreview& soundRootPreview,
  const LinuxResourceCatalogPreview& resourceCatalogPreview,
  const LinuxInputState& inputState,
  double elapsedSeconds,
  int width,
  int height
)
{
  if (!overlay->openglReady || !NMainFrame::MakeOpenGLContextCurrent())
  {
    return false;
  }

  const std::vector<std::string> lines = BuildOverlayLines(
    environment,
    settings,
    launchPreview,
    sessionPreview,
    configPreview,
    contentProbe,
    loadingPreview,
    loadingUiPreview,
    loadingRuntimeDriver,
    loadingHeroesRuntimePreview,
    loadingUiState,
    mapCatalog,
    mapBrowserState,
    selectedMapPreview,
    artworkState,
    heroCatalog,
    localMatchPreview,
    selectedHeroPreview,
    engineMapStartPreview,
    rootFileSystemPreview,
    uiRootPreview,
    sessionRootPreview,
    soundRootPreview,
    resourceCatalogPreview,
    inputState,
    elapsedSeconds
  );

  const int headerHeight = 56;
  int panelTop = headerHeight + 24;

  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glClearColor(17.0f / 255.0f, 22.0f / 255.0f, 28.0f / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  SetOpenGlColor(27, 38, 49);
  DrawOpenGlRect(0, 0, width, headerHeight);

  std::string header = "Prime World Classic - native Linux OpenGL bootstrap";
  if (launchPreview.protocolValid)
  {
    header += " - ";
    header += launchPreview.method;
  }
  if (!mapCatalog.entries.empty() && mapBrowserState.selectedIndex < mapCatalog.entries.size())
  {
    header += " - ";
    header += mapCatalog.entries[mapBrowserState.selectedIndex].title;
  }

  if (overlay->artworkTexture && overlay->artworkWidth > 0 && overlay->artworkHeight > 0)
  {
    const int artworkX = width > static_cast<int>(overlay->artworkWidth) ?
      (width - static_cast<int>(overlay->artworkWidth)) / 2 : 16;
    const int artworkY = headerHeight + 20;

    SetOpenGlColor(95, 115, 134);
    DrawOpenGlRect(
      artworkX - 4,
      artworkY - 4,
      static_cast<int>(overlay->artworkWidth) + 8,
      static_cast<int>(overlay->artworkHeight) + 8
    );

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, overlay->artworkTexture);
    SetOpenGlColor(255, 255, 255);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2i(artworkX, artworkY);
    glTexCoord2f(1.0f, 0.0f); glVertex2i(artworkX + static_cast<int>(overlay->artworkWidth), artworkY);
    glTexCoord2f(1.0f, 1.0f); glVertex2i(
      artworkX + static_cast<int>(overlay->artworkWidth),
      artworkY + static_cast<int>(overlay->artworkHeight));
    glTexCoord2f(0.0f, 1.0f); glVertex2i(artworkX, artworkY + static_cast<int>(overlay->artworkHeight));
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    panelTop = artworkY + static_cast<int>(overlay->artworkHeight) + 24;
  }

  const int panelLeft = 16;
  const int panelWidth = width > 32 ? width - 32 : width;
  int panelHeight = height - panelTop - 16;
  if (panelHeight < 120)
  {
    panelTop = headerHeight + 20;
    panelHeight = height - panelTop - 16;
  }

  if (panelWidth > 0 && panelHeight > 0)
  {
    SetOpenGlColor(32, 42, 53, 240);
    DrawOpenGlRect(panelLeft, panelTop, panelWidth, panelHeight);

    SetOpenGlColor(95, 115, 134);
    glBegin(GL_LINE_LOOP);
    glVertex2i(panelLeft, panelTop);
    glVertex2i(panelLeft + panelWidth - 1, panelTop);
    glVertex2i(panelLeft + panelWidth - 1, panelTop + panelHeight - 1);
    glVertex2i(panelLeft, panelTop + panelHeight - 1);
    glEnd();
  }

  std::vector<std::string> visibleLines = lines;
  const int lineHeight = 18;
  const int maxLines = panelHeight > 28 ? (panelHeight - 28) / lineHeight : 0;
  if (maxLines > 0 && static_cast<int>(visibleLines.size()) > maxLines)
  {
    const size_t hiddenLines = visibleLines.size() - static_cast<size_t>(maxLines) + 1;
    visibleLines.resize(static_cast<size_t>(maxLines));
    visibleLines[maxLines - 1] = NStr::StrFmt(
      "... %lu more lines in linux-client-shell.log",
      static_cast<unsigned long>(hiddenLines)
    );
  }

  SetOpenGlColor(244, 239, 230);
  DrawOpenGlText(overlay, 20, 34, header);

  int y = panelTop + 26;
  for (size_t i = 0; i < visibleLines.size(); ++i)
  {
    if (i < 2)
    {
      SetOpenGlColor(244, 239, 230);
    }
    else
    {
      SetOpenGlColor(199, 208, 216);
    }
    DrawOpenGlText(overlay, panelLeft + 14, y, visibleLines[i]);
    y += lineHeight;
  }

  NMainFrame::SwapOpenGLBuffers();
  return true;
}

void DrawWindowOverlay(
  LinuxWindowOverlay* overlay,
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxLaunchPreview& launchPreview,
  const LinuxSessionPreview& sessionPreview,
  const LinuxConfigBootstrapPreview& configPreview,
  const LinuxContentProbe& contentProbe,
  const LinuxLoadingScreenPreview& loadingPreview,
  const LinuxLoadingUiPreview& loadingUiPreview,
  const LinuxLoadingRuntimeDriver& loadingRuntimeDriver,
  const LinuxLoadingHeroesRuntimePreview& loadingHeroesRuntimePreview,
  const LinuxLoadingUiState& loadingUiState,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxArtworkSelectionState& artworkState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  const LinuxUiRootPreview& uiRootPreview,
  const LinuxSessionRootPreview& sessionRootPreview,
  const LinuxSoundRootPreview& soundRootPreview,
  const LinuxResourceCatalogPreview& resourceCatalogPreview,
  const LinuxInputState& inputState,
  double elapsedSeconds
)
{
  if (!overlay->ready)
  {
    return;
  }

  XWindowAttributes attributes = {};
  if (!XGetWindowAttributes(overlay->display, overlay->window, &attributes))
  {
    return;
  }

  const std::vector<std::string> lines = BuildOverlayLines(
    environment,
    settings,
    launchPreview,
    sessionPreview,
    configPreview,
    contentProbe,
    loadingPreview,
    loadingUiPreview,
    loadingRuntimeDriver,
    loadingHeroesRuntimePreview,
    loadingUiState,
    mapCatalog,
    mapBrowserState,
    selectedMapPreview,
    artworkState,
    heroCatalog,
    localMatchPreview,
    selectedHeroPreview,
    engineMapStartPreview,
    rootFileSystemPreview,
    uiRootPreview,
    sessionRootPreview,
    soundRootPreview,
    resourceCatalogPreview,
    inputState,
    elapsedSeconds
  );
  const int width = attributes.width > 0 ? attributes.width : static_cast<int>(settings.width);
  const int height = attributes.height > 0 ? attributes.height : static_cast<int>(settings.height);
  if (DrawWindowOverlayOpenGl(
        overlay,
        environment,
        settings,
        launchPreview,
        sessionPreview,
        configPreview,
        contentProbe,
        loadingPreview,
        loadingUiPreview,
        loadingRuntimeDriver,
        loadingHeroesRuntimePreview,
        loadingUiState,
        mapCatalog,
        mapBrowserState,
        selectedMapPreview,
        artworkState,
        heroCatalog,
        localMatchPreview,
        selectedHeroPreview,
        engineMapStartPreview,
        rootFileSystemPreview,
        uiRootPreview,
        sessionRootPreview,
        soundRootPreview,
        resourceCatalogPreview,
        inputState,
        elapsedSeconds,
        width,
        height))
  {
    return;
  }

  const int headerHeight = 56;
  int panelTop = headerHeight + 24;

  XSetForeground(overlay->display, overlay->gc, overlay->background);
  XFillRectangle(overlay->display, overlay->window, overlay->gc, 0, 0, width, height);

  XSetForeground(overlay->display, overlay->gc, overlay->accent);
  XFillRectangle(overlay->display, overlay->window, overlay->gc, 0, 0, width, headerHeight);

  XSetForeground(overlay->display, overlay->gc, overlay->foreground);
  std::string header = "Prime World Classic - native Linux client bootstrap";
  if (launchPreview.protocolValid)
  {
    header += " - ";
    header += launchPreview.method;
  }
  if (!mapCatalog.entries.empty() && mapBrowserState.selectedIndex < mapCatalog.entries.size())
  {
    header += " - ";
    header += mapCatalog.entries[mapBrowserState.selectedIndex].title;
  }
  XDrawString(overlay->display, overlay->window, overlay->gc, 20, 34, header.c_str(), static_cast<int>(header.size()));
  XStoreName(overlay->display, overlay->window, header.c_str());

  if (overlay->artworkPixmap && overlay->artworkWidth > 0 && overlay->artworkHeight > 0)
  {
    const int artworkX = width > static_cast<int>(overlay->artworkWidth) ?
      (width - static_cast<int>(overlay->artworkWidth)) / 2 : 16;
    const int artworkY = headerHeight + 20;

    XSetForeground(overlay->display, overlay->gc, overlay->panelBorder);
    XFillRectangle(
      overlay->display,
      overlay->window,
      overlay->gc,
      artworkX - 4,
      artworkY - 4,
      overlay->artworkWidth + 8,
      overlay->artworkHeight + 8
    );
    XCopyArea(
      overlay->display,
      overlay->artworkPixmap,
      overlay->window,
      overlay->gc,
      0,
      0,
      overlay->artworkWidth,
      overlay->artworkHeight,
      artworkX,
      artworkY
    );

    panelTop = artworkY + static_cast<int>(overlay->artworkHeight) + 24;
  }

  const int panelLeft = 16;
  const int panelWidth = width > 32 ? width - 32 : width;
  int panelHeight = height - panelTop - 16;
  if (panelHeight < 120)
  {
    panelTop = headerHeight + 20;
    panelHeight = height - panelTop - 16;
  }

  if (panelWidth > 0 && panelHeight > 0)
  {
    XSetForeground(overlay->display, overlay->gc, overlay->panelBackground);
    XFillRectangle(overlay->display, overlay->window, overlay->gc, panelLeft, panelTop, panelWidth, panelHeight);
    XSetForeground(overlay->display, overlay->gc, overlay->panelBorder);
    XDrawRectangle(
      overlay->display,
      overlay->window,
      overlay->gc,
      panelLeft,
      panelTop,
      panelWidth - 1,
      panelHeight - 1
    );
  }

  std::vector<std::string> visibleLines = lines;
  const int lineHeight = 18;
  const int maxLines = panelHeight > 28 ? (panelHeight - 28) / lineHeight : 0;
  if (maxLines > 0 && static_cast<int>(visibleLines.size()) > maxLines)
  {
    const size_t hiddenLines = visibleLines.size() - static_cast<size_t>(maxLines) + 1;
    visibleLines.resize(static_cast<size_t>(maxLines));
    visibleLines[maxLines - 1] = NStr::StrFmt(
      "... %lu more lines in linux-client-shell.log",
      static_cast<unsigned long>(hiddenLines)
    );
  }

  int y = panelTop + 26;
  for (size_t i = 0; i < visibleLines.size(); ++i)
  {
    XSetForeground(overlay->display, overlay->gc, i < 2 ? overlay->foreground : overlay->muted);
    XDrawString(
      overlay->display,
      overlay->window,
      overlay->gc,
      panelLeft + 14,
      y,
      visibleLines[i].c_str(),
      static_cast<int>(visibleLines[i].size())
    );
    y += lineHeight;
  }

  XFlush(overlay->display);
}

void WriteStartupLog(
  const LinuxClientEnvironment& environment,
  const LinuxClientLaunchSettings& settings,
  const LinuxWindowOverlay& overlay,
  const LinuxLaunchPreview& launchPreview,
  const LinuxSessionPreview& sessionPreview,
  const LinuxConfigBootstrapPreview& configPreview,
  const LinuxContentProbe& contentProbe,
  const LinuxLoadingScreenPreview& loadingPreview,
  const LinuxLoadingUiPreview& loadingUiPreview,
  const LinuxLoadingRuntimeDriver& loadingRuntimeDriver,
  const LinuxLoadingHeroesRuntimePreview& loadingHeroesRuntimePreview,
  const LinuxLoadingUiState& loadingUiState,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxArtworkSelectionState& artworkState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxSelectedHeroDbPreview& selectedHeroPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview,
  const LinuxRootFileSystemPreview& rootFileSystemPreview,
  const LinuxUiRootPreview& uiRootPreview,
  const LinuxSessionRootPreview& sessionRootPreview,
  const LinuxSoundRootPreview& soundRootPreview,
  const LinuxResourceCatalogPreview& resourceCatalogPreview,
  const LinuxInputState& inputState
)
{
  const fs::path logsDir = ResolveLogsDir(environment);
  const fs::path logFilePath = logsDir / "linux-client-shell.log";

  std::error_code error;
  fs::create_directories(logsDir, error);

  std::ofstream logFile(logFilePath.string().c_str(), std::ios::app);
  if (!logFile.is_open())
  {
    return;
  }

  const fs::path currentDir = fs::current_path(error);
  const fs::path resolvedCurrentDir = error ? fs::path() : currentDir;

  logFile << "[" << FormatCurrentTime() << "] startup\n";
  logFile << "  executable=" << (environment.executablePath.empty() ? "<unknown>" : environment.executablePath.string()) << "\n";
  logFile << "  root=" << (environment.gameRoot.empty() ? "<not found>" : environment.gameRoot.string()) << "\n";
  logFile << "  engine=" << (environment.engineReady ? "ready" : "not-initialized") << "\n";
  logFile << "  base=" << (environment.baseDir.empty() ? "<not initialized>" : environment.baseDir.string()) << "\n";
  logFile << "  bin=" << (environment.binDir.empty() ? "<not initialized>" : environment.binDir.string()) << "\n";
  logFile << "  user=" << (environment.userDir.empty() ? "<not initialized>" : environment.userDir.string()) << "\n";
  logFile << "  logs=" << logsDir.string() << "\n";
  logFile << "  cwd=" << (resolvedCurrentDir.empty() ? "<unknown>" : resolvedCurrentDir.string()) << "\n";
  logFile << "  overlayBackend=" << (overlay.openglReady ? "OpenGL" : "X11") << "\n";
  logFile << "  size=" << settings.width << "x" << settings.height << "\n";
  logFile << "  sizeFromParent=" << ((settings.widthFromParent || settings.heightFromParent) ? "yes" : "no") << "\n";
  logFile << "  spectator=" << (settings.spectator ? "yes" : "no") << "\n";
  logFile << "  tutorial=" << (settings.tutorial ? "yes" : "no") << "\n";
  logFile << "  launchSource=" << (launchPreview.source.empty() ? "<none>" : launchPreview.source) << "\n";
  logFile << "  launchProtocolPresent=" << (launchPreview.protocolPresent ? "yes" : "no") << "\n";
  logFile << "  launchProtocolValid=" << (launchPreview.protocolValid ? "yes" : "no") << "\n";
  logFile << "  launchMethod=" << (launchPreview.method.empty() ? "<none>" : launchPreview.method) << "\n";
  logFile << "  launchVersion=" << (launchPreview.version.empty() ? "<none>" : launchPreview.version) << "\n";
  logFile << "  launchVersionMatches=" << (launchPreview.versionMatches ? "yes" : "no") << "\n";
  logFile << "  launchMirrorIndex=" << launchPreview.mirrorIndex << "\n";
  logFile << "  launchToken=" << MaskSensitiveValue(launchPreview.token) << "\n";
  logFile << "  launchMapId=" << (launchPreview.mapId.empty() ? "<none>" : launchPreview.mapId) << "\n";
  logFile << "  launchSessionLogin=" << (launchPreview.sessionLogin.empty() ? "<none>" : launchPreview.sessionLogin) << "\n";
  logFile << "  launchServerName=" << (launchPreview.serverName.empty() ? "<none>" : launchPreview.serverName) << "\n";
  logFile << "  launchUid=" << (launchPreview.uid.empty() ? "<none>" : launchPreview.uid) << "\n";
  logFile << "  launchSnid=" << (launchPreview.snid.empty() ? "<none>" : launchPreview.snid) << "\n";
  logFile << "  launchSnuid=" << (launchPreview.snuid.empty() ? "<none>" : launchPreview.snuid) << "\n";
  logFile << "  launchLauncherRequested=" << (launchPreview.launcherRequested ? "yes" : "no") << "\n";
  logFile << "  launchLauncherFetchAttempted=" << (launchPreview.launcherFetchAttempted ? "yes" : "no") << "\n";
  logFile << "  launchLauncherFetchSucceeded=" << (launchPreview.launcherFetchSucceeded ? "yes" : "no") << "\n";
  logFile << "  sessionFileProvided=" << (sessionPreview.fileProvided ? "yes" : "no") << "\n";
  logFile << "  sessionLoaded=" << (sessionPreview.loaded ? "yes" : "no") << "\n";
  logFile << "  sessionValid=" << (sessionPreview.valid ? "yes" : "no") << "\n";
  logFile << "  sessionSource=" << (sessionPreview.source.empty() ? "<none>" : sessionPreview.source) << "\n";
  logFile << "  sessionFilePath=" << (sessionPreview.filePath.empty() ? "<none>" : sessionPreview.filePath) << "\n";
  logFile << "  sessionMethod=" << (sessionPreview.method.empty() ? "<none>" : sessionPreview.method) << "\n";
  logFile << "  sessionMapId=" << (sessionPreview.mapId.empty() ? "<none>" : sessionPreview.mapId) << "\n";
  logFile << "  sessionCurrentNickname=" << (sessionPreview.currentNickname.empty() ? "<none>" : sessionPreview.currentNickname) << "\n";
  logFile << "  sessionCurrentUserId=" << sessionPreview.currentUserId << "\n";
  logFile << "  sessionCurrentHeroWebId=" << sessionPreview.currentHeroWebId << "\n";
  logFile << "  sessionCurrentHeroId="
          << (sessionPreview.currentHeroPersistentId.empty() ? "<none>" : sessionPreview.currentHeroPersistentId) << "\n";
  logFile << "  sessionCurrentTeamId=" << sessionPreview.currentTeamId << "\n";
  logFile << "  sessionCurrentPartyId=" << sessionPreview.currentPartyId << "\n";
  logFile << "  sessionCurrentSkinId=" << sessionPreview.currentSkinId << "\n";
  logFile << "  sessionPlayersCount=" << sessionPreview.players.size() << "\n";
  logFile << "  sessionTeam1Players=" << CountSessionTeamPlayers(sessionPreview, 1) << "\n";
  logFile << "  sessionTeam2Players=" << CountSessionTeamPlayers(sessionPreview, 2) << "\n";
  for (size_t i = 0; i < sessionPreview.players.size(); ++i)
  {
    logFile << "  sessionPlayer[" << i << "].nickname=" << sessionPreview.players[i].nickname << "\n";
    logFile << "  sessionPlayer[" << i << "].userId=" << sessionPreview.players[i].userId << "\n";
    logFile << "  sessionPlayer[" << i << "].teamId=" << sessionPreview.players[i].teamId << "\n";
    logFile << "  sessionPlayer[" << i << "].partyId=" << sessionPreview.players[i].partyId << "\n";
    logFile << "  sessionPlayer[" << i << "].heroWebId=" << sessionPreview.players[i].heroWebId << "\n";
    logFile << "  sessionPlayer[" << i << "].heroId="
            << (sessionPreview.players[i].heroPersistentId.empty() ? "<none>" : sessionPreview.players[i].heroPersistentId)
            << "\n";
    logFile << "  sessionPlayer[" << i << "].current=" << (sessionPreview.players[i].currentPlayer ? "yes" : "no") << "\n";
  }
  for (size_t i = 0; i < sessionPreview.warnings.size(); ++i)
  {
    logFile << "  sessionWarning=" << sessionPreview.warnings[i] << "\n";
  }
  logFile << "  configCommandsRegistered=" << (configPreview.commandsRegistered ? "yes" : "no") << "\n";
  logFile << "  configPlaceholderCommands=" << configPreview.placeholderCommandCount << "\n";
  logFile << "  configDefaultLoaded=" << (configPreview.defaultLoaded ? "yes" : "no") << "\n";
  logFile << "  configUserLoaded=" << (configPreview.userLoaded ? "yes" : "no") << "\n";
  logFile << "  configLangLoaded=" << (configPreview.langLoaded ? "yes" : "no") << "\n";
  logFile << "  configSocialLoaded=" << (configPreview.socialLoaded ? "yes" : "no") << "\n";
  logFile << "  configGameLoaded=" << (configPreview.gameLoaded ? "yes" : "no") << "\n";
  logFile << "  configSpectatorLoaded=" << (configPreview.spectatorLoaded ? "yes" : "no") << "\n";
  logFile << "  configLanguage=" << (configPreview.language.empty() ? "<none>" : configPreview.language) << "\n";
  logFile << "  configFullscreen=" << (configPreview.gfxFullscreen.empty() ? "<none>" : configPreview.gfxFullscreen) << "\n";
  logFile << "  configResolution=" << (configPreview.gfxResolution.empty() ? "<none>" : configPreview.gfxResolution) << "\n";
  logFile << "  configLocalGame=" << (configPreview.localGame.empty() ? "<none>" : configPreview.localGame) << "\n";
  logFile << "  configLoginAddress=" << (configPreview.loginAddress.empty() ? "<none>" : configPreview.loginAddress) << "\n";
  logFile << "  configSocialLoginAddress=" << (configPreview.socialLoginAddress.empty() ? "<none>" : configPreview.socialLoginAddress) << "\n";
  logFile << "  configStatClientUrl=" << (configPreview.statClientUrl.empty() ? "<none>" : configPreview.statClientUrl) << "\n";
  logFile << "  locale=" << (contentProbe.locale.empty() ? "<none>" : contentProbe.locale) << "\n";
  logFile << "  dataMounted=" << (contentProbe.dataMounted ? "yes" : "no") << "\n";
  logFile << "  localizationMounted=" << (contentProbe.localizationMounted ? "yes" : "no") << "\n";
  logFile << "  uiXdbCount=" << contentProbe.uiXdbCount << "\n";
  logFile << "  uiScreenFileCount=" << contentProbe.uiScreenFileCount << "\n";
  logFile << "  socialXdbCount=" << contentProbe.socialXdbCount << "\n";
  logFile << "  localizationFileCount=" << contentProbe.localizationFileCount << "\n";
  logFile << "  dataSample=" << (contentProbe.dataSampleFile.empty() ? "<none>" : contentProbe.dataSampleFile) << "\n";
  logFile << "  dataSampleSize=" << contentProbe.dataSampleSize << "\n";
  logFile << "  localizationSample=" << (contentProbe.localizationSampleFile.empty() ? "<none>" : contentProbe.localizationSampleFile) << "\n";
  logFile << "  localizationSampleSize=" << contentProbe.localizationSampleSize << "\n";
  logFile << "  loadingLayout=" << (loadingPreview.layoutFound ? "yes" : "no") << "\n";
  logFile << "  loadingFlash=" << (loadingPreview.flashAsset.empty() ? "<none>" : loadingPreview.flashAsset) << "\n";
  logFile << "  loadingFlashSize=" << loadingPreview.flashAssetSize << "\n";
  logFile << "  loadingLayoutSize=" << loadingPreview.width << "x" << loadingPreview.height << "\n";
  logFile << "  loadingArtwork=" << (loadingPreview.artworkLoaded ? "yes" : "no") << "\n";
  logFile << "  loadingArtworkFile=" << (loadingPreview.artworkFile.empty() ? "<none>" : loadingPreview.artworkFile) << "\n";
  logFile << "  loadingArtworkSize=" << loadingPreview.artworkWidth << "x" << loadingPreview.artworkHeight << "\n";
  logFile << "  loadingUiDbReady=" << (loadingUiPreview.ready ? "yes" : "no") << "\n";
  logFile << "  loadingUiDbid=" << (loadingUiPreview.dbid.empty() ? "<none>" : loadingUiPreview.dbid) << "\n";
  logFile << "  loadingUiStatuses=" << loadingUiPreview.statusCount << "\n";
  logFile << "  loadingUiTips=" << loadingUiPreview.tipCount << "\n";
  logFile << "  loadingUiLocales=" << loadingUiPreview.localeCount << "\n";
  logFile << "  loadingUiForceColors=" << loadingUiPreview.forceColorCount << "\n";
  logFile << "  loadingUiReportTypes=" << loadingUiPreview.reportTypeCount << "\n";
  logFile << "  loadingUiCountryFlags=" << loadingUiPreview.countryFlagCount << "\n";
  logFile << "  loadingUiChatChannels=" << loadingUiPreview.chatChannelCount << "\n";
  logFile << "  loadingUiBinds=" << loadingUiPreview.bindCount << "\n";
  logFile << "  loadingUiMinimapReady=" << (loadingUiPreview.minimapReady ? "yes" : "no") << "\n";
  logFile << "  loadingUiMinimapDbid=" << (loadingUiPreview.minimapDbid.empty() ? "<none>" : loadingUiPreview.minimapDbid) << "\n";
  logFile << "  loadingUiMinimapIcons=" << loadingUiPreview.minimapIconCount << "\n";
  logFile << "  loadingUiSmartChatReady=" << (loadingUiPreview.smartChatReady ? "yes" : "no") << "\n";
  logFile << "  loadingUiSmartChatDbid=" << (loadingUiPreview.smartChatDbid.empty() ? "<none>" : loadingUiPreview.smartChatDbid) << "\n";
  logFile << "  loadingUiSmartChatCategories=" << loadingUiPreview.smartChatCategoryCount << "\n";
  logFile << "  loadingUiSmartChatMessages=" << loadingUiPreview.smartChatMessageCount << "\n";
  logFile << "  loadingUiModeManeuvers=" << (loadingUiPreview.maneuversModeReady ? "yes" : "no") << "\n";
  logFile << "  loadingUiModeGuard=" << (loadingUiPreview.guardModeReady ? "yes" : "no") << "\n";
  logFile << "  loadingUiModeGuild=" << (loadingUiPreview.guildModeReady ? "yes" : "no") << "\n";
  logFile << "  loadingUiModeCustom=" << (loadingUiPreview.customModeReady ? "yes" : "no") << "\n";
  logFile << "  loadingUiRecentPlayers=" << loadingUiPreview.recentPlayers << "\n";
  logFile << "  loadingUiSampleTip=" << (loadingUiPreview.sampleTip.empty() ? "<none>" : loadingUiPreview.sampleTip) << "\n";
  logFile << "  loadingUiPremiumTooltip="
          << (loadingUiPreview.premiumTooltip.empty() ? "<none>" : loadingUiPreview.premiumTooltip) << "\n";
  logFile << "  loadingRuntimeReady=" << (loadingRuntimeDriver.ready ? "yes" : "no") << "\n";
  logFile << "  loadingHeroesRuntimeReady=" << (loadingHeroesRuntimePreview.ready ? "yes" : "no") << "\n";
  logFile << "  loadingHeroesRuntimeCount=" << loadingHeroesRuntimePreview.heroes.size() << "\n";
  logFile << "  loadingHeroesRuntimeHumanCount=" << loadingHeroesRuntimePreview.humanCount << "\n";
  logFile << "  loadingHeroesRuntimeBotCount=" << loadingHeroesRuntimePreview.botCount << "\n";
  logFile << "  loadingHeroesRuntimeDisconnectedCount=" << loadingHeroesRuntimePreview.disconnectedCount << "\n";
  logFile << "  loadingHeroesRuntimePremiumCount=" << loadingHeroesRuntimePreview.premiumCount << "\n";
  logFile << "  loadingHeroesRuntimeNoviceCount=" << loadingHeroesRuntimePreview.noviceCount << "\n";
  logFile << "  loadingHeroesRuntimeLocaleCount=" << loadingHeroesRuntimePreview.localeCount << "\n";
  logFile << "  loadingHeroesRuntimeFlaggedCount=" << loadingHeroesRuntimePreview.flaggedCount << "\n";
  logFile << "  loadingHeroesRuntimeRatedCount=" << loadingHeroesRuntimePreview.ratedCount << "\n";
  logFile << "  loadingHeroesRuntimeOurHeroId=" << loadingHeroesRuntimePreview.ourHeroId << "\n";
  logFile << "  loadingUiStateSource=" << loadingUiState.source << "\n";
  logFile << "  loadingUiStateChanges=" << loadingUiState.changeCount << "\n";
  logFile << "  loadingUiRuntimeEventIndex=" << loadingUiState.runtimeEventIndex << "\n";
  logFile << "  loadingUiRuntimeEvent="
          << (loadingUiState.runtimeEvent.empty() ? "<none>" : loadingUiState.runtimeEvent) << "\n";
  logFile << "  loadingUiRuntimeStatusKey="
          << (loadingUiState.runtimeStatusKey.empty() ? "<none>" : loadingUiState.runtimeStatusKey) << "\n";
  logFile << "  loadingUiRuntimeStatusText="
          << (loadingUiState.runtimeStatusText.empty() ? "<empty>" : loadingUiState.runtimeStatusText) << "\n";
  if (!loadingUiPreview.statuses.empty() && loadingUiState.statusIndex < loadingUiPreview.statuses.size())
  {
    logFile << "  loadingUiActiveStatusKey=" << loadingUiPreview.statuses[loadingUiState.statusIndex].key << "\n";
    logFile << "  loadingUiActiveStatusText="
            << (loadingUiPreview.statuses[loadingUiState.statusIndex].text.empty() ?
              "<empty>" : loadingUiPreview.statuses[loadingUiState.statusIndex].text) << "\n";
  }
  if (!loadingUiPreview.tips.empty() && loadingUiState.tipIndex < loadingUiPreview.tips.size())
  {
    logFile << "  loadingUiActiveTip=" << loadingUiPreview.tips[loadingUiState.tipIndex] << "\n";
  }
  for (size_t i = 0; i < loadingRuntimeDriver.samples.size(); ++i)
  {
    logFile << "  loadingRuntimeSample[" << i << "]=" << loadingRuntimeDriver.samples[i] << "\n";
  }
  for (size_t i = 0; i < loadingHeroesRuntimePreview.samples.size(); ++i)
  {
    logFile << "  loadingHeroesRuntimeSample[" << i << "]=" << loadingHeroesRuntimePreview.samples[i] << "\n";
  }
  for (size_t i = 0; i < loadingHeroesRuntimePreview.metaSamples.size(); ++i)
  {
    logFile << "  loadingHeroesRuntimeMetaSample[" << i << "]=" << loadingHeroesRuntimePreview.metaSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingHeroesRuntimePreview.heroes.size(); ++i)
  {
    logFile << "  loadingHeroesRuntimeHero[" << i << "].slotId=" << loadingHeroesRuntimePreview.heroes[i].slotId << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].team=" << loadingHeroesRuntimePreview.heroes[i].team << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].human=" << (loadingHeroesRuntimePreview.heroes[i].human ? "yes" : "no") << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].leftGame=" << (loadingHeroesRuntimePreview.heroes[i].leftGame ? "yes" : "no") << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].hasPremium=" << (loadingHeroesRuntimePreview.heroes[i].hasPremium ? "yes" : "no") << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].isNovice=" << (loadingHeroesRuntimePreview.heroes[i].isNovice ? "yes" : "no") << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].progress=" << loadingHeroesRuntimePreview.heroes[i].progress << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].heroLevel=" << loadingHeroesRuntimePreview.heroes[i].heroLevel << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].rating=" << loadingHeroesRuntimePreview.heroes[i].rating << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].ratingAcc=" << loadingHeroesRuntimePreview.heroes[i].ratingAcc << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].partyId=" << loadingHeroesRuntimePreview.heroes[i].partyId << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].locale="
            << (loadingHeroesRuntimePreview.heroes[i].locale.empty() ? "<none>" : loadingHeroesRuntimePreview.heroes[i].locale) << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].flagId="
            << (loadingHeroesRuntimePreview.heroes[i].flagId.empty() ? "<none>" : loadingHeroesRuntimePreview.heroes[i].flagId) << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].flagIcon="
            << (loadingHeroesRuntimePreview.heroes[i].flagIcon.empty() ? "<none>" : loadingHeroesRuntimePreview.heroes[i].flagIcon) << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].playerName="
            << (loadingHeroesRuntimePreview.heroes[i].playerName.empty() ? "<none>" : loadingHeroesRuntimePreview.heroes[i].playerName) << "\n";
    logFile << "  loadingHeroesRuntimeHero[" << i << "].heroTitle="
            << (loadingHeroesRuntimePreview.heroes[i].heroTitle.empty() ? "<none>" : loadingHeroesRuntimePreview.heroes[i].heroTitle) << "\n";
  }
  if (!loadingUiPreview.locales.empty() && loadingUiState.currentLocaleIndex < loadingUiPreview.locales.size())
  {
    logFile << "  loadingUiActiveCurrentLocale=" << loadingUiPreview.locales[loadingUiState.currentLocaleIndex].locale << "\n";
    logFile << "  loadingUiActiveCurrentLocaleTooltip="
            << (loadingUiPreview.locales[loadingUiState.currentLocaleIndex].tooltip.empty() ?
              "<empty>" : loadingUiPreview.locales[loadingUiState.currentLocaleIndex].tooltip) << "\n";
  }
  if (!loadingUiPreview.locales.empty() && loadingUiState.enemyLocaleIndex < loadingUiPreview.locales.size())
  {
    logFile << "  loadingUiActiveEnemyLocale=" << loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].locale << "\n";
    logFile << "  loadingUiActiveEnemyLocaleTooltip="
            << (loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].tooltip.empty() ?
              "<empty>" : loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].tooltip) << "\n";
  }
  if (!loadingUiPreview.modes.empty() && loadingUiState.modeIndex < loadingUiPreview.modes.size())
  {
    logFile << "  loadingUiActiveMode=" << loadingUiPreview.modes[loadingUiState.modeIndex].id << "\n";
    logFile << "  loadingUiActiveModeTooltip="
            << (loadingUiPreview.modes[loadingUiState.modeIndex].tooltip.empty() ?
              "<empty>" : loadingUiPreview.modes[loadingUiState.modeIndex].tooltip) << "\n";
  }
  logFile << "  artworkMode=" << DescribeArtworkMode(artworkState.mode) << "\n";
  logFile << "  artworkModeSource=" << artworkState.source << "\n";
  logFile << "  artworkModeChanges=" << artworkState.changeCount << "\n";
  logFile << "  launchHeroSelector=" << (settings.heroSelector.empty() ? "<none>" : settings.heroSelector) << "\n";
  logFile << "  demoCycleSeconds=" << settings.demoCycleSeconds << "\n";
  logFile << "  mapCatalogCount=" << mapCatalog.descriptorCount << "\n";
  logFile << "  mapCatalogProductionCount=" << mapCatalog.productionDescriptorCount << "\n";
  logFile << "  mapCatalogPvpCount=" << mapCatalog.pvpCount << "\n";
  logFile << "  mapCatalogPveCount=" << mapCatalog.pveCount << "\n";
  logFile << "  mapCatalogTutorialCount=" << mapCatalog.tutorialCount << "\n";
  logFile << "  mapSelectedIndex=" << mapBrowserState.selectedIndex << "\n";
  logFile << "  mapSelectionSource=" << mapBrowserState.selectionSource << "\n";
  logFile << "  mapSelectionChanges=" << mapBrowserState.selectionChanges << "\n";
  if (!mapCatalog.entries.empty() && mapBrowserState.selectedIndex < mapCatalog.entries.size())
  {
    const LinuxMapCatalogEntry& selectedEntry = mapCatalog.entries[mapBrowserState.selectedIndex];
    logFile << "  mapSelectedDescriptor=" << selectedEntry.descriptor << "\n";
    logFile << "  mapSelectedTitle=" << selectedEntry.title << "\n";
    logFile << "  mapSelectedType=" << selectedEntry.mapType << "\n";
    logFile << "  mapSelectedCategory=" << selectedEntry.category << "\n";
    logFile << "  mapSelectedTeamSize=" << selectedEntry.teamSize << "\n";
    logFile << "  mapSelectedProduction=" << (selectedEntry.productionMode ? "yes" : "no") << "\n";
  }
  logFile << "  mapFile=" << (selectedMapPreview.mapFile.empty() ? "<none>" : selectedMapPreview.mapFile) << "\n";
  logFile << "  mapSettingsFile=" << (selectedMapPreview.mapSettingsFile.empty() ? "<none>" : selectedMapPreview.mapSettingsFile) << "\n";
  logFile << "  mapSettingsSource=" << (selectedMapPreview.settings.source.empty() ? "<none>" : selectedMapPreview.settings.source) << "\n";
  logFile << "  mapSettingsReference=" << (selectedMapPreview.settings.reference.empty() ? "<none>" : selectedMapPreview.settings.reference) << "\n";
  logFile << "  mapSettingsParentRef=" << (selectedMapPreview.settings.parentRef.empty() ? "<none>" : selectedMapPreview.settings.parentRef) << "\n";
  logFile << "  mapSettingsChainCount=" << selectedMapPreview.settings.chainFiles.size() << "\n";
  logFile << "  mapScoringTableFile=" << (selectedMapPreview.scoringTableFile.empty() ? "<none>" : selectedMapPreview.scoringTableFile) << "\n";
  logFile << "  mapObjectCount=" << selectedMapPreview.objectCount << "\n";
  logFile << "  mapLockObjectCount=" << selectedMapPreview.lockMapObjectCount << "\n";
  logFile << "  mapScriptedObjectCount=" << selectedMapPreview.scriptedObjectCount << "\n";
  logFile << "  mapTacticalReady=" << (selectedMapPreview.tactical.ready ? "yes" : "no") << "\n";
  logFile << "  mapTacticalBounds=" << selectedMapPreview.tactical.minX << "," << selectedMapPreview.tactical.minY
          << " -> " << selectedMapPreview.tactical.maxX << "," << selectedMapPreview.tactical.maxY << "\n";
  logFile << "  mapTacticalTowerCount=" << selectedMapPreview.tactical.towerCount << "\n";
  logFile << "  mapTacticalHeroSpawnCount=" << selectedMapPreview.tactical.heroSpawnCount << "\n";
  logFile << "  mapTacticalLaneSpawnerCount=" << selectedMapPreview.tactical.laneSpawnerCount << "\n";
  logFile << "  mapTacticalNeutralSpawnerCount=" << selectedMapPreview.tactical.neutralSpawnerCount << "\n";
  logFile << "  mapTacticalBossCount=" << selectedMapPreview.tactical.bossCount << "\n";
  logFile << "  mapTacticalShopCount=" << selectedMapPreview.tactical.shopCount << "\n";
  logFile << "  mapTacticalFountainCount=" << selectedMapPreview.tactical.fountainCount << "\n";
  logFile << "  mapTacticalGlyphCount=" << selectedMapPreview.tactical.glyphCount << "\n";
  logFile << "  mapTacticalMainBuildingCount=" << selectedMapPreview.tactical.mainBuildingCount << "\n";
  logFile << "  mapTacticalMinigameCount=" << selectedMapPreview.tactical.minigameCount << "\n";
  logFile << "  mapTacticalFlagCount=" << selectedMapPreview.tactical.flagCount << "\n";
  logFile << "  mapTerrainRef=" << (selectedMapPreview.terrainRef.empty() ? "<none>" : selectedMapPreview.terrainRef) << "\n";
  logFile << "  mapCameraSettingsRef=" << (selectedMapPreview.cameraSettingsRef.empty() ? "<none>" : selectedMapPreview.cameraSettingsRef) << "\n";
  logFile << "  mapLightEnvironmentRef=" << (selectedMapPreview.lightEnvironmentRef.empty() ? "<none>" : selectedMapPreview.lightEnvironmentRef) << "\n";
  logFile << "  mapNightLightEnvironmentRef=" << (selectedMapPreview.nightLightEnvironmentRef.empty() ? "<none>" : selectedMapPreview.nightLightEnvironmentRef) << "\n";
  logFile << "  mapScriptFile=" << (selectedMapPreview.scriptFile.empty() ? "<none>" : selectedMapPreview.scriptFile) << "\n";
  logFile << "  mapDictionaryRef=" << (selectedMapPreview.dictionaryRef.empty() ? "<none>" : selectedMapPreview.dictionaryRef) << "\n";
  logFile << "  mapDialogsCollectionRef=" << (selectedMapPreview.dialogsCollectionRef.empty() ? "<none>" : selectedMapPreview.dialogsCollectionRef) << "\n";
  logFile << "  mapHintsCollectionRef=" << (selectedMapPreview.hintsCollectionRef.empty() ? "<none>" : selectedMapPreview.hintsCollectionRef) << "\n";
  logFile << "  mapQuestsCollectionRef=" << (selectedMapPreview.questsCollectionRef.empty() ? "<none>" : selectedMapPreview.questsCollectionRef) << "\n";
  logFile << "  mapOverrideBotsSettingsRef=" << (selectedMapPreview.settings.overrideBotsSettingsRef.empty() ? "<none>" : selectedMapPreview.settings.overrideBotsSettingsRef) << "\n";
  logFile << "  mapOverrideGlyphSettingsRef=" << (selectedMapPreview.settings.overrideGlyphSettingsRef.empty() ? "<none>" : selectedMapPreview.settings.overrideGlyphSettingsRef) << "\n";
  logFile << "  mapHeroRespawnParamsRef=" << (selectedMapPreview.settings.heroRespawnParamsRef.empty() ? "<none>" : selectedMapPreview.settings.heroRespawnParamsRef) << "\n";
  logFile << "  mapBattleStartDelay=" << selectedMapPreview.settings.battleStartDelay << "\n";
  logFile << "  mapEmblemHeroNeeds=" << selectedMapPreview.settings.emblemHeroNeeds << "\n";
  logFile << "  mapForce=" << selectedMapPreview.settings.force << "\n";
  logFile << "  mapMinRequiredHeroForce=" << selectedMapPreview.settings.minRequiredHeroForce << "\n";
  logFile << "  mapMaxRequiredHeroForce=" << selectedMapPreview.settings.maxRequiredHeroForce << "\n";
  logFile << "  mapStartPrimePerTeam=" << selectedMapPreview.settings.startPrimePerTeam << "\n";
  logFile << "  mapTowersVulnerabilityDelay=" << selectedMapPreview.settings.towersVulnerabilityDelay << "\n";
  logFile << "  mapEnableAllScriptFunctions=" << (selectedMapPreview.settings.enableAllScriptFunctions ? "yes" : "no") << "\n";
  logFile << "  mapEnableAnnouncements=" << (selectedMapPreview.settings.enableAnnouncements ? "yes" : "no") << "\n";
  logFile << "  mapEnablePortalTalent=" << (selectedMapPreview.settings.enablePortalTalent ? "yes" : "no") << "\n";
  logFile << "  mapEnableStatistics=" << (selectedMapPreview.settings.enableStatistics ? "yes" : "no") << "\n";
  logFile << "  mapShowAllHeroes=" << (selectedMapPreview.settings.showAllHeroes ? "yes" : "no") << "\n";
  logFile << "  mapFullPartyOnly=" << (selectedMapPreview.settings.fullPartyOnly ? "yes" : "no") << "\n";
  logFile << "  mapDictionaryResourceCount=" << selectedMapPreview.settings.dictionaryResourceCount << "\n";
  logFile << "  mapLoadingBackRef=" << (selectedMapPreview.loadingBack.reference.empty() ? "<none>" : selectedMapPreview.loadingBack.reference) << "\n";
  logFile << "  mapLoadingBackFile=" << (selectedMapPreview.loadingBack.sourceFile.empty() ? "<none>" : selectedMapPreview.loadingBack.sourceFile) << "\n";
  logFile << "  mapLoadingBackSize=" << selectedMapPreview.loadingBack.width << "x" << selectedMapPreview.loadingBack.height << "\n";
  logFile << "  mapLoadingLogoRef=" << (selectedMapPreview.loadingLogo.reference.empty() ? "<none>" : selectedMapPreview.loadingLogo.reference) << "\n";
  logFile << "  mapLoadingLogoFile=" << (selectedMapPreview.loadingLogo.sourceFile.empty() ? "<none>" : selectedMapPreview.loadingLogo.sourceFile) << "\n";
  logFile << "  mapLoadingLogoSize=" << selectedMapPreview.loadingLogo.width << "x" << selectedMapPreview.loadingLogo.height << "\n";
  logFile << "  mapMinimapFirstRef=" << (selectedMapPreview.minimapFirst.reference.empty() ? "<none>" : selectedMapPreview.minimapFirst.reference) << "\n";
  logFile << "  mapMinimapFirstFile=" << (selectedMapPreview.minimapFirst.sourceFile.empty() ? "<none>" : selectedMapPreview.minimapFirst.sourceFile) << "\n";
  logFile << "  mapMinimapSecondRef=" << (selectedMapPreview.minimapSecond.reference.empty() ? "<none>" : selectedMapPreview.minimapSecond.reference) << "\n";
  logFile << "  mapMinimapSecondFile=" << (selectedMapPreview.minimapSecond.sourceFile.empty() ? "<none>" : selectedMapPreview.minimapSecond.sourceFile) << "\n";
  logFile << "  mapMinimapNeutralRef=" << (selectedMapPreview.minimapNeutral.reference.empty() ? "<none>" : selectedMapPreview.minimapNeutral.reference) << "\n";
  logFile << "  mapMinimapNeutralFile=" << (selectedMapPreview.minimapNeutral.sourceFile.empty() ? "<none>" : selectedMapPreview.minimapNeutral.sourceFile) << "\n";
  logFile << "  heroCatalogCount=" << heroCatalog.entries.size() << "\n";
  size_t selectedHeroIndex = localMatchPreview.selectedHeroIndex;
  if (localMatchPreview.ready && localMatchPreview.selectedSlotIndex < localMatchPreview.lineup.size())
  {
    selectedHeroIndex = localMatchPreview.lineup[localMatchPreview.selectedSlotIndex].heroIndex;
  }
  if (!heroCatalog.entries.empty() && selectedHeroIndex < heroCatalog.entries.size())
  {
    const LinuxHeroCatalogEntry& selectedHero = heroCatalog.entries[selectedHeroIndex];
    logFile << "  localHeroId=" << (selectedHero.persistentId.empty() ? selectedHero.id : selectedHero.persistentId) << "\n";
    logFile << "  localHeroTitle=" << selectedHero.title << "\n";
    logFile << "  localHeroGender=" << (selectedHero.gender.empty() ? "<none>" : selectedHero.gender) << "\n";
    logFile << "  localHeroSkinCount=" << selectedHero.skinCount << "\n";
  }
  logFile << "  selectedHeroDbReady=" << (selectedHeroPreview.ready ? "yes" : "no") << "\n";
  logFile << "  selectedHeroDbFound=" << (selectedHeroPreview.found ? "yes" : "no") << "\n";
  logFile << "  selectedHeroDbid=" << (selectedHeroPreview.dbid.empty() ? "<none>" : selectedHeroPreview.dbid) << "\n";
  logFile << "  selectedHeroPersistentId="
          << (selectedHeroPreview.persistentId.empty() ? "<none>" : selectedHeroPreview.persistentId) << "\n";
  logFile << "  selectedHeroTitle=" << (selectedHeroPreview.title.empty() ? "<none>" : selectedHeroPreview.title) << "\n";
  logFile << "  selectedHeroRace=" << (selectedHeroPreview.heroRace.empty() ? "<none>" : selectedHeroPreview.heroRace) << "\n";
  logFile << "  selectedHeroAttackReady=" << (selectedHeroPreview.attackReady ? "yes" : "no") << "\n";
  logFile << "  selectedHeroAttackDbid="
          << (selectedHeroPreview.attackAbilityDbid.empty() ? "<none>" : selectedHeroPreview.attackAbilityDbid) << "\n";
  logFile << "  selectedHeroAttackName="
          << (selectedHeroPreview.attackAbilityName.empty() ? "<none>" : selectedHeroPreview.attackAbilityName) << "\n";
  logFile << "  selectedHeroAbilityCount=" << selectedHeroPreview.abilityCount << "\n";
  logFile << "  selectedHeroActiveAbilityCount=" << selectedHeroPreview.activeAbilityCount << "\n";
  logFile << "  selectedHeroPassiveAbilityCount=" << selectedHeroPreview.passiveAbilityCount << "\n";
  logFile << "  selectedHeroAutocastAbilityCount=" << selectedHeroPreview.autocastAbilityCount << "\n";
  logFile << "  selectedHeroChannellingAbilityCount=" << selectedHeroPreview.channellingAbilityCount << "\n";
  logFile << "  selectedHeroStatsReady=" << (selectedHeroPreview.statsReady ? "yes" : "no") << "\n";
  logFile << "  selectedHeroStatsCount=" << selectedHeroPreview.statsCount << "\n";
  logFile << "  selectedHeroLevelUpgradeCount=" << selectedHeroPreview.levelUpgradeCount << "\n";
  logFile << "  selectedHeroRecommendedStatsCount=" << selectedHeroPreview.recommendedStatCount << "\n";
  logFile << "  selectedHeroTargetingReady=" << (selectedHeroPreview.targetingReady ? "yes" : "no") << "\n";
  logFile << "  selectedHeroTargetingRange=" << selectedHeroPreview.targetingRange << "\n";
  logFile << "  selectedHeroChaseRange=" << selectedHeroPreview.chaseRange << "\n";
  logFile << "  selectedHeroAggroRange=" << selectedHeroPreview.aggroRange << "\n";
  logFile << "  selectedHeroTalentsSets=" << selectedHeroPreview.defaultTalentSetCount << "\n";
  logFile << "  selectedHeroTalentLevels=" << selectedHeroPreview.defaultTalentLevelCount << "\n";
  logFile << "  selectedHeroTalentSlots=" << selectedHeroPreview.defaultTalentSlotCount << "\n";
  logFile << "  selectedHeroTalentSetsReady=" << selectedHeroPreview.defaultTalentReadyCount << "\n";
  logFile << "  selectedHeroTalentIconsLoaded=" << selectedHeroPreview.defaultTalentIconCount << "\n";
  logFile << "  selectedHeroSceneObjects=" << selectedHeroPreview.sceneObjectCount << "\n";
  logFile << "  selectedHeroSummonedGroups=" << selectedHeroPreview.summonedUnitGroupCount << "\n";
  logFile << "  selectedHeroSkinDbCount=" << selectedHeroPreview.skinCount << "\n";
  logFile << "  selectedHeroUniqueResourceReady=" << (selectedHeroPreview.uniqueResourceReady ? "yes" : "no") << "\n";
  logFile << "  selectedHeroUniqueResourceName="
          << (selectedHeroPreview.uniqueResourceName.empty() ? "<none>" : selectedHeroPreview.uniqueResourceName) << "\n";
  logFile << "  selectedHeroUniqueResourceTooltip="
          << (selectedHeroPreview.uniqueResourceTooltip.empty() ? "<none>" : selectedHeroPreview.uniqueResourceTooltip) << "\n";
  logFile << "  selectedHeroPortraitRef="
          << (selectedHeroPreview.portrait.reference.empty() ? "<none>" : selectedHeroPreview.portrait.reference) << "\n";
  logFile << "  selectedHeroPortraitFile="
          << (selectedHeroPreview.portrait.sourceFile.empty() ? "<none>" : selectedHeroPreview.portrait.sourceFile) << "\n";
  logFile << "  localMatchReady=" << (localMatchPreview.ready ? "yes" : "no") << "\n";
  logFile << "  localMatchHumanTeam=" << localMatchPreview.humanTeam << "\n";
  logFile << "  localMatchSelectedSlot=" << localMatchPreview.selectedSlotIndex << "\n";
  logFile << "  localMatchRequestedTeamSize=" << localMatchPreview.requestedTeamSize << "\n";
  logFile << "  localMatchTeamSize=" << localMatchPreview.teamSize << "\n";
  logFile << "  localMatchManualOverrides=" << CountManualHeroOverrides(localMatchPreview) << "\n";
  logFile << "  localMatchGenerationCount=" << localMatchPreview.generationCount << "\n";
  logFile << "  localMatchGenerationSource=" << localMatchPreview.generationSource << "\n";
  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    logFile << "  localMatchSlot[" << i << "].team=" << localMatchPreview.lineup[i].team << "\n";
    logFile << "  localMatchSlot[" << i << "].human=" << (localMatchPreview.lineup[i].human ? "yes" : "no") << "\n";
    logFile << "  localMatchSlot[" << i << "].manual=" << (localMatchPreview.lineup[i].manualHero ? "yes" : "no") << "\n";
    logFile << "  localMatchSlot[" << i << "].heroId=" << localMatchPreview.lineup[i].heroId << "\n";
    logFile << "  localMatchSlot[" << i << "].heroTitle=" << localMatchPreview.lineup[i].heroTitle << "\n";
  }
  for (size_t i = 0; i < selectedHeroPreview.statSamples.size(); ++i)
  {
    logFile << "  selectedHeroStatSample[" << i << "]=" << selectedHeroPreview.statSamples[i] << "\n";
  }
  for (size_t i = 0; i < selectedHeroPreview.recommendedStatSamples.size(); ++i)
  {
    logFile << "  selectedHeroRecommendedStatSample[" << i << "]="
            << selectedHeroPreview.recommendedStatSamples[i] << "\n";
  }
  for (size_t i = 0; i < selectedHeroPreview.talentSamples.size(); ++i)
  {
    logFile << "  selectedHeroTalentSample[" << i << "]=" << selectedHeroPreview.talentSamples[i] << "\n";
  }
  for (size_t i = 0; i < selectedHeroPreview.featuredAbilities.size(); ++i)
  {
    logFile << "  selectedHeroAbility[" << i << "].dbid="
            << (selectedHeroPreview.featuredAbilities[i].dbid.empty() ?
              "<none>" : selectedHeroPreview.featuredAbilities[i].dbid) << "\n";
    logFile << "  selectedHeroAbility[" << i << "].name="
            << (selectedHeroPreview.featuredAbilities[i].name.empty() ?
              "<none>" : selectedHeroPreview.featuredAbilities[i].name) << "\n";
    logFile << "  selectedHeroAbility[" << i << "].type="
            << (selectedHeroPreview.featuredAbilities[i].type.empty() ?
              "<none>" : selectedHeroPreview.featuredAbilities[i].type) << "\n";
    logFile << "  selectedHeroAbility[" << i << "].isAttack="
            << (selectedHeroPreview.featuredAbilities[i].isAttack ? "yes" : "no") << "\n";
    logFile << "  selectedHeroAbility[" << i << "].iconRef="
            << (selectedHeroPreview.featuredAbilities[i].icon.reference.empty() ?
              "<none>" : selectedHeroPreview.featuredAbilities[i].icon.reference) << "\n";
    logFile << "  selectedHeroAbility[" << i << "].iconFile="
            << (selectedHeroPreview.featuredAbilities[i].icon.sourceFile.empty() ?
              "<none>" : selectedHeroPreview.featuredAbilities[i].icon.sourceFile) << "\n";
  }
  for (size_t i = 0; i < selectedHeroPreview.defaultTalentPreviews.size(); ++i)
  {
    logFile << "  selectedHeroTalent[" << i << "].level="
            << selectedHeroPreview.defaultTalentPreviews[i].levelIndex << "\n";
    logFile << "  selectedHeroTalent[" << i << "].slot="
            << selectedHeroPreview.defaultTalentPreviews[i].slotIndex << "\n";
    logFile << "  selectedHeroTalent[" << i << "].dbid="
            << (selectedHeroPreview.defaultTalentPreviews[i].dbid.empty() ?
              "<none>" : selectedHeroPreview.defaultTalentPreviews[i].dbid) << "\n";
    logFile << "  selectedHeroTalent[" << i << "].name="
            << (selectedHeroPreview.defaultTalentPreviews[i].name.empty() ?
              "<none>" : selectedHeroPreview.defaultTalentPreviews[i].name) << "\n";
    logFile << "  selectedHeroTalent[" << i << "].rarity="
            << (selectedHeroPreview.defaultTalentPreviews[i].rarity.empty() ?
              "<none>" : selectedHeroPreview.defaultTalentPreviews[i].rarity) << "\n";
    logFile << "  selectedHeroTalent[" << i << "].status="
            << (selectedHeroPreview.defaultTalentPreviews[i].status.empty() ?
              "<none>" : selectedHeroPreview.defaultTalentPreviews[i].status) << "\n";
    logFile << "  selectedHeroTalent[" << i << "].iconRef="
            << (selectedHeroPreview.defaultTalentPreviews[i].icon.reference.empty() ?
              "<none>" : selectedHeroPreview.defaultTalentPreviews[i].icon.reference) << "\n";
    logFile << "  selectedHeroTalent[" << i << "].iconFile="
            << (selectedHeroPreview.defaultTalentPreviews[i].icon.sourceFile.empty() ?
              "<none>" : selectedHeroPreview.defaultTalentPreviews[i].icon.sourceFile) << "\n";
  }
  logFile << "  engineStartReady=" << (engineMapStartPreview.ready ? "yes" : "no") << "\n";
  logFile << "  engineStartSource=" << (engineMapStartPreview.source.empty() ? "<none>" : engineMapStartPreview.source) << "\n";
  logFile << "  engineStartUsedRealMapLoader=" << (engineMapStartPreview.usedRealMapLoader ? "yes" : "no") << "\n";
  logFile << "  engineStartBuiltMapStartInfo=" << (engineMapStartPreview.builtMapStartInfo ? "yes" : "no") << "\n";
  logFile << "  engineStartMapDescriptor=" << (engineMapStartPreview.mapDescriptor.empty() ? "<none>" : engineMapStartPreview.mapDescriptor) << "\n";
  logFile << "  engineStartTotalSpawners=" << engineMapStartPreview.totalSpawners << "\n";
  logFile << "  engineStartTeam1Spawners=" << engineMapStartPreview.team1Spawners << "\n";
  logFile << "  engineStartTeam2Spawners=" << engineMapStartPreview.team2Spawners << "\n";
  logFile << "  engineStartAssignedSlots=" << engineMapStartPreview.assignedSlots << "\n";
  logFile << "  engineStartOverflowPlayers=" << engineMapStartPreview.overflowPlayers << "\n";
  logFile << "  engineStartHumanPlayers=" << engineMapStartPreview.humanPlayers << "\n";
  logFile << "  engineStartBotPlayers=" << engineMapStartPreview.botPlayers << "\n";
  logFile << "  engineStartMaxPlayersPerTeam=" << engineMapStartPreview.maxPlayersPerTeam << "\n";
  logFile << "  engineStartRandomSeed=" << engineMapStartPreview.randomSeed << "\n";
  for (size_t i = 0; i < engineMapStartPreview.slots.size(); ++i)
  {
    logFile << "  engineStartSlot[" << i << "].playerId=" << engineMapStartPreview.slots[i].playerId << "\n";
    logFile << "  engineStartSlot[" << i << "].team=" << engineMapStartPreview.slots[i].team << "\n";
    logFile << "  engineStartSlot[" << i << "].originalTeam=" << engineMapStartPreview.slots[i].originalTeam << "\n";
    logFile << "  engineStartSlot[" << i << "].userId=" << engineMapStartPreview.slots[i].userId << "\n";
    logFile << "  engineStartSlot[" << i << "].filled=" << (engineMapStartPreview.slots[i].filled ? "yes" : "no") << "\n";
    logFile << "  engineStartSlot[" << i << "].lineupIndex=" << engineMapStartPreview.slots[i].lineupIndex << "\n";
    logFile << "  engineStartSlot[" << i << "].human=" << (engineMapStartPreview.slots[i].human ? "yes" : "no") << "\n";
    logFile << "  engineStartSlot[" << i << "].manual=" << (engineMapStartPreview.slots[i].manualHero ? "yes" : "no") << "\n";
    logFile << "  engineStartSlot[" << i << "].heroChecksum=" << engineMapStartPreview.slots[i].heroChecksum << "\n";
    logFile << "  engineStartSlot[" << i << "].heroId=" << (engineMapStartPreview.slots[i].heroId.empty() ? "<none>" : engineMapStartPreview.slots[i].heroId) << "\n";
    logFile << "  engineStartSlot[" << i << "].heroTitle=" << (engineMapStartPreview.slots[i].heroTitle.empty() ? "<none>" : engineMapStartPreview.slots[i].heroTitle) << "\n";
    logFile << "  engineStartSlot[" << i << "].heroSkin="
            << (engineMapStartPreview.slots[i].heroSkin.empty() ? "<none>" : engineMapStartPreview.slots[i].heroSkin) << "\n";
    logFile << "  engineStartSlot[" << i << "].nickname="
            << (engineMapStartPreview.slots[i].nickname.empty() ? "<none>" : engineMapStartPreview.slots[i].nickname) << "\n";
    logFile << "  engineStartSlot[" << i << "].heroLevel=" << engineMapStartPreview.slots[i].heroLevel << "\n";
    logFile << "  engineStartSlot[" << i << "].heroExp=" << engineMapStartPreview.slots[i].heroExp << "\n";
    logFile << "  engineStartSlot[" << i << "].heroRating=" << engineMapStartPreview.slots[i].heroRating << "\n";
    logFile << "  engineStartSlot[" << i << "].premium=" << (engineMapStartPreview.slots[i].hasPremium ? "yes" : "no") << "\n";
    logFile << "  engineStartSlot[" << i << "].novice=" << (engineMapStartPreview.slots[i].isNovice ? "yes" : "no") << "\n";
    logFile << "  engineStartSlot[" << i << "].partyId=" << engineMapStartPreview.slots[i].partyId << "\n";
    logFile << "  engineStartSlot[" << i << "].locale="
            << (engineMapStartPreview.slots[i].locale.empty() ? "<none>" : engineMapStartPreview.slots[i].locale) << "\n";
    logFile << "  engineStartSlot[" << i << "].flagId="
            << (engineMapStartPreview.slots[i].flagId.empty() ? "<none>" : engineMapStartPreview.slots[i].flagId) << "\n";
    logFile << "  engineStartSlot[" << i << "].leagueIndex=" << engineMapStartPreview.slots[i].leagueIndex << "\n";
    logFile << "  engineStartSlot[" << i << "].ownLeaguePlace=" << engineMapStartPreview.slots[i].ownLeaguePlace << "\n";
    logFile << "  engineStartSlot[" << i << "].scriptName=" << (engineMapStartPreview.slots[i].scriptName.empty() ? "<none>" : engineMapStartPreview.slots[i].scriptName) << "\n";
    logFile << "  engineStartSlot[" << i << "].position="
            << engineMapStartPreview.slots[i].translateX << ","
            << engineMapStartPreview.slots[i].translateY << "\n";
  }
  logFile << "  rootFsMounted=" << (rootFileSystemPreview.mounted ? "yes" : "no") << "\n";
  logFile << "  rootFsDataRegistered=" << (rootFileSystemPreview.dataRegistered ? "yes" : "no") << "\n";
  logFile << "  rootFsLocalizationRegistered=" << (rootFileSystemPreview.localizationRegistered ? "yes" : "no") << "\n";
  logFile << "  rootFsDbCacheReady=" << (rootFileSystemPreview.dbCacheReady ? "yes" : "no") << "\n";
  logFile << "  rootFsSample=" << (rootFileSystemPreview.sampleFile.empty() ? "<none>" : rootFileSystemPreview.sampleFile) << "\n";
  logFile << "  rootFsSampleSize=" << rootFileSystemPreview.sampleFileSize << "\n";
  logFile << "  rootFsLocalizationSample=" << (rootFileSystemPreview.localizationFile.empty() ? "<none>" : rootFileSystemPreview.localizationFile) << "\n";
  logFile << "  rootFsLocalizationSampleSize=" << rootFileSystemPreview.localizationFileSize << "\n";
  logFile << "  rootFsTextRef=" << (rootFileSystemPreview.textRefFile.empty() ? "<none>" : rootFileSystemPreview.textRefFile) << "\n";
  logFile << "  rootFsTextValue=" << (rootFileSystemPreview.textRefValue.empty() ? "<none>" : rootFileSystemPreview.textRefValue) << "\n";
  logFile << "  uiRootReady=" << (uiRootPreview.ready ? "yes" : "no") << "\n";
  logFile << "  uiRootDbid=" << (uiRootPreview.dbid.empty() ? "<none>" : uiRootPreview.dbid) << "\n";
  logFile << "  uiRootScreens=" << uiRootPreview.screenCount << "\n";
  logFile << "  uiRootCursors=" << uiRootPreview.cursorCount << "\n";
  logFile << "  uiRootScripts=" << uiRootPreview.scriptCount << "\n";
  logFile << "  uiRootContentGroups=" << uiRootPreview.contentGroupCount << "\n";
  logFile << "  uiRootContentEntries=" << uiRootPreview.contentEntryCount << "\n";
  logFile << "  uiRootConstants=" << uiRootPreview.constantCount << "\n";
  logFile << "  uiRootSubstitutes=" << uiRootPreview.substituteCount << "\n";
  logFile << "  uiRootStyleAliases=" << uiRootPreview.styleAliasCount << "\n";
  logFile << "  uiRootFontStyles=" << uiRootPreview.fontStyleCount << "\n";
  logFile << "  uiRootPreferences=" << (uiRootPreview.preferencesReady ? "yes" : "no") << "\n";
  logFile << "  uiRootVoting=" << (uiRootPreview.votingReady ? "yes" : "no") << "\n";
  for (size_t i = 0; i < uiRootPreview.screenSamples.size(); ++i)
  {
    logFile << "  uiRootScreenSample[" << i << "]=" << uiRootPreview.screenSamples[i] << "\n";
  }
  for (size_t i = 0; i < uiRootPreview.contentSamples.size(); ++i)
  {
    logFile << "  uiRootContentSample[" << i << "]=" << uiRootPreview.contentSamples[i] << "\n";
  }
  for (size_t i = 0; i < uiRootPreview.constantSamples.size(); ++i)
  {
    logFile << "  uiRootConstantSample[" << i << "]=" << uiRootPreview.constantSamples[i] << "\n";
  }
  logFile << "  sessionRootReady=" << (sessionRootPreview.ready ? "yes" : "no") << "\n";
  logFile << "  sessionRootDbid=" << (sessionRootPreview.rootDbid.empty() ? "<none>" : sessionRootPreview.rootDbid) << "\n";
  logFile << "  sessionRootUiReady=" << (sessionRootPreview.uiRootReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootUiDbid=" << (sessionRootPreview.uiRootDbid.empty() ? "<none>" : sessionRootPreview.uiRootDbid) << "\n";
  logFile << "  sessionRootUiUnitCategories=" << (sessionRootPreview.uiUnitCategoriesReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootUiUnitCategoriesParams=" << (sessionRootPreview.uiUnitCategoriesParamsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootUiUnitCategoryCount=" << sessionRootPreview.uiUnitCategoryCount << "\n";
  logFile << "  sessionRootUiUnitCategoryParamCount=" << sessionRootPreview.uiUnitCategoryParamCount << "\n";
  logFile << "  sessionRootLogicReady=" << (sessionRootPreview.logicRootReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootLogicDbid=" << (sessionRootPreview.logicRootDbid.empty() ? "<none>" : sessionRootPreview.logicRootDbid) << "\n";
  logFile << "  sessionRootAiReady=" << (sessionRootPreview.logicAiReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootHeroesDbReady=" << (sessionRootPreview.heroesDbReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootMapListReady=" << (sessionRootPreview.mapListReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootTeamNames=" << sessionRootPreview.logicTeamNameCount << "\n";
  logFile << "  sessionRootConsumableGroups=" << sessionRootPreview.logicConsumableGroupCount << "\n";
  logFile << "  sessionRootPortalReady=" << (sessionRootPreview.logicPortalReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootPortalDbid=" << (sessionRootPreview.logicPortalDbid.empty() ? "<none>" : sessionRootPreview.logicPortalDbid) << "\n";
  logFile << "  sessionRootScoringReady=" << (sessionRootPreview.logicScoringReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootScoringDbid=" << (sessionRootPreview.logicScoringDbid.empty() ? "<none>" : sessionRootPreview.logicScoringDbid) << "\n";
  logFile << "  sessionRootScoringAchievements=" << sessionRootPreview.logicScoringAchievementCount << "\n";
  logFile << "  sessionRootScoringHeroTitles=" << sessionRootPreview.logicScoringHeroTitleCount << "\n";
  logFile << "  sessionRootScoringDescriptions=" << sessionRootPreview.logicScoringDescriptionCount << "\n";
  logFile << "  sessionRootScoringTeleporters=" << sessionRootPreview.logicScoringTeleporterCount << "\n";
  logFile << "  sessionRootGlyphsReady=" << (sessionRootPreview.logicGlyphsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootGlyphsDbid=" << (sessionRootPreview.logicGlyphsDbid.empty() ? "<none>" : sessionRootPreview.logicGlyphsDbid) << "\n";
  logFile << "  sessionRootGlyphCount=" << sessionRootPreview.logicGlyphCount << "\n";
  logFile << "  sessionRootLevelUpsReady=" << (sessionRootPreview.logicLevelUpsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootLevelUpsDbid=" << (sessionRootPreview.logicLevelUpsDbid.empty() ? "<none>" : sessionRootPreview.logicLevelUpsDbid) << "\n";
  logFile << "  sessionRootLevelUpsCount=" << sessionRootPreview.logicLevelUpCount << "\n";
  logFile << "  sessionRootLevelUpPointTotal=" << sessionRootPreview.logicLevelUpPointTotal << "\n";
  logFile << "  sessionRootKillExperienceModifiers=" << (sessionRootPreview.logicKillExperienceModifiersReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootDefaultFormulasReady=" << (sessionRootPreview.logicDefaultFormulasReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootDefaultFormulasDbid=" << (sessionRootPreview.logicDefaultFormulasDbid.empty() ? "<none>" : sessionRootPreview.logicDefaultFormulasDbid) << "\n";
  logFile << "  sessionRootFloatFormulas=" << sessionRootPreview.logicFloatFormulaCount << "\n";
  logFile << "  sessionRootBoolFormulas=" << sessionRootPreview.logicBoolFormulaCount << "\n";
  logFile << "  sessionRootIntFormulas=" << sessionRootPreview.logicIntFormulaCount << "\n";
  logFile << "  sessionRootUnitsReady=" << (sessionRootPreview.logicUnitsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootUnitsDbid=" << (sessionRootPreview.logicUnitsDbid.empty() ? "<none>" : sessionRootPreview.logicUnitsDbid) << "\n";
  logFile << "  sessionRootUnitParameterCount=" << sessionRootPreview.logicUnitParameterCount << "\n";
  logFile << "  sessionRootUnitDefaultStatsCount=" << sessionRootPreview.logicUnitDefaultStatsCount << "\n";
  logFile << "  sessionRootUnitTargetingCount=" << sessionRootPreview.logicUnitTargetingCount << "\n";
  logFile << "  sessionRootGuildBuffsReady=" << (sessionRootPreview.logicGuildBuffsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootGuildBuffsDbid=" << (sessionRootPreview.logicGuildBuffsDbid.empty() ? "<none>" : sessionRootPreview.logicGuildBuffsDbid) << "\n";
  logFile << "  sessionRootGuildBuffCount=" << sessionRootPreview.logicGuildBuffCount << "\n";
  logFile << "  sessionRootGuildShopBonusCount=" << sessionRootPreview.logicGuildShopBonusCount << "\n";
  logFile << "  sessionRootBotsSettingsReady=" << (sessionRootPreview.logicBotsSettingsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootBotsAiEnabled=" << (sessionRootPreview.botsAiEnabled ? "yes" : "no") << "\n";
  logFile << "  sessionRootBotsMidOnly=" << (sessionRootPreview.botsMidOnly ? "yes" : "no") << "\n";
  logFile << "  sessionRootBotsTimeToGo=" << sessionRootPreview.logicBotsTimeToGo << "\n";
  logFile << "  sessionRootBotsTimeToTeleport=" << sessionRootPreview.logicBotsTimeToTeleport << "\n";
  logFile << "  sessionRootCreepsWavesDelay=" << sessionRootPreview.logicCreepsWavesDelay << "\n";
  logFile << "  sessionRootCreepLevelCap=" << sessionRootPreview.logicCreepLevelCap << "\n";
  logFile << "  sessionRootBaseEmblemHeroNeeds=" << sessionRootPreview.logicBaseEmblemHeroNeeds << "\n";
  logFile << "  sessionRootHeroRanksReady=" << (sessionRootPreview.logicHeroRanksReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootHeroRanksCount=" << sessionRootPreview.logicHeroRankCount << "\n";
  logFile << "  sessionRootHeroRanksHighLevelsMMRating=" << sessionRootPreview.logicHeroRanksHighLevelsMMRating << "\n";
  logFile << "  sessionRootLevelToExperienceReady=" << (sessionRootPreview.logicLevelToExperienceReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootLevelCount=" << sessionRootPreview.logicLevelCount << "\n";
  logFile << "  sessionRootLevelFirstExp=" << sessionRootPreview.logicLevelFirstExp << "\n";
  logFile << "  sessionRootLevelLastExp=" << sessionRootPreview.logicLevelLastExp << "\n";
  logFile << "  sessionRootHeroCount=" << sessionRootPreview.logicHeroCount << "\n";
  logFile << "  sessionRootLegalHeroCount=" << sessionRootPreview.logicLegalHeroCount << "\n";
  logFile << "  sessionRootMapCount=" << sessionRootPreview.logicMapCount << "\n";
  logFile << "  sessionRootVisualReady=" << (sessionRootPreview.visualRootReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootVisualDbid=" << (sessionRootPreview.visualRootDbid.empty() ? "<none>" : sessionRootPreview.visualRootDbid) << "\n";
  logFile << "  sessionRootVisualEffects=" << (sessionRootPreview.visualEffectsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootVisualUiEvents=" << (sessionRootPreview.visualUiEventsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootVisualUiEventCount=" << sessionRootPreview.visualUiEventCount << "\n";
  logFile << "  sessionRootVisualTeamColoring=" << (sessionRootPreview.visualTeamColoringReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootVisualTeamColoringDbid="
          << (sessionRootPreview.visualTeamColoringDbid.empty() ? "<none>" : sessionRootPreview.visualTeamColoringDbid) << "\n";
  logFile << "  sessionRootVisualEmoteSettings=" << (sessionRootPreview.visualEmoteSettingsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootVisualEmoteSettingsDbid="
          << (sessionRootPreview.visualEmoteSettingsDbid.empty() ? "<none>" : sessionRootPreview.visualEmoteSettingsDbid) << "\n";
  logFile << "  sessionRootVisualCameraCount=" << sessionRootPreview.visualCameraCount << "\n";
  logFile << "  sessionRootVisualAnimSetCount=" << sessionRootPreview.visualAnimSetCount << "\n";
  logFile << "  sessionRootVisualWinLoseCount=" << sessionRootPreview.visualWinLoseCount << "\n";
  logFile << "  sessionRootVisualSelfAuraCount=" << sessionRootPreview.visualSelfAuraCount << "\n";
  logFile << "  sessionRootVisualAuraCount=" << sessionRootPreview.visualAuraCount << "\n";
  logFile << "  sessionRootVisualWallTargetZoneWidth=" << sessionRootPreview.visualWallTargetZoneWidth << "\n";
  logFile << "  sessionRootAudioReady=" << (sessionRootPreview.audioRootReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootAudioDbid=" << (sessionRootPreview.audioRootDbid.empty() ? "<none>" : sessionRootPreview.audioRootDbid) << "\n";
  logFile << "  sessionRootRollSettingsReady=" << (sessionRootPreview.rollSettingsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootRollSettingsDbid="
          << (sessionRootPreview.rollSettingsDbid.empty() ? "<none>" : sessionRootPreview.rollSettingsDbid) << "\n";
  logFile << "  sessionRootRollPvpReady=" << (sessionRootPreview.rollPvpReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootRollPvpMode=" << (sessionRootPreview.rollPvpModeName.empty() ? "<none>" : sessionRootPreview.rollPvpModeName) << "\n";
  logFile << "  sessionRootRollPvpContainers=" << sessionRootPreview.rollPvpContainerCount << "\n";
  logFile << "  sessionRootRollPvpPremiumContainers=" << sessionRootPreview.rollPvpPremiumContainerCount << "\n";
  logFile << "  sessionRootRollPvpScoreCap=" << sessionRootPreview.rollPvpScoreCap << "\n";
  logFile << "  sessionRootRollPvpContainersOnWin=" << sessionRootPreview.rollPvpContainersOnWin << "\n";
  logFile << "  sessionRootRollGuildLevelsReady=" << (sessionRootPreview.rollGuildLevelsReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootRollGuildLevelCount=" << sessionRootPreview.rollGuildLevelCount << "\n";
  logFile << "  sessionRootRollRatingModifiers=" << sessionRootPreview.rollRatingModifierCount << "\n";
  logFile << "  sessionRootRollFullPartyModifiers=" << sessionRootPreview.rollFullPartyModifierCount << "\n";
  logFile << "  sessionRootRollRequiredLevelExclusive=" << sessionRootPreview.rollRequiredLevelForExclusiveTalents << "\n";
  logFile << "  sessionRootRollRequiredRatingExclusive=" << sessionRootPreview.rollRequiredRatingForExclusiveTalents << "\n";
  logFile << "  sessionRootMessagesReady=" << (sessionRootPreview.sessionMessagesReady ? "yes" : "no") << "\n";
  logFile << "  sessionRootMessagesDbid="
          << (sessionRootPreview.sessionMessagesDbid.empty() ? "<none>" : sessionRootPreview.sessionMessagesDbid) << "\n";
  logFile << "  sessionRootDxTitle="
          << (sessionRootPreview.dxErrorTitle.empty() ? "<none>" : sessionRootPreview.dxErrorTitle) << "\n";
  logFile << "  sessionRootHardwareError="
          << (sessionRootPreview.hardwareErrorMessage.empty() ? "<none>" : sessionRootPreview.hardwareErrorMessage) << "\n";
  for (size_t i = 0; i < sessionRootPreview.uiUnitCategorySamples.size(); ++i)
  {
    logFile << "  sessionRootUiUnitCategorySample[" << i << "]="
            << sessionRootPreview.uiUnitCategorySamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.logicTeamNameSamples.size(); ++i)
  {
    logFile << "  sessionRootTeamNameSample[" << i << "]="
            << sessionRootPreview.logicTeamNameSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.logicGlyphSamples.size(); ++i)
  {
    logFile << "  sessionRootGlyphSample[" << i << "]="
            << sessionRootPreview.logicGlyphSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.logicScoreSamples.size(); ++i)
  {
    logFile << "  sessionRootScoreSample[" << i << "]="
            << sessionRootPreview.logicScoreSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.logicGuildBuffSamples.size(); ++i)
  {
    logFile << "  sessionRootGuildBuffSample[" << i << "]="
            << sessionRootPreview.logicGuildBuffSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.logicRankSamples.size(); ++i)
  {
    logFile << "  sessionRootRankSample[" << i << "]="
            << sessionRootPreview.logicRankSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.heroSamples.size(); ++i)
  {
    logFile << "  sessionRootHeroSample[" << i << "]=" << sessionRootPreview.heroSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.mapSamples.size(); ++i)
  {
    logFile << "  sessionRootMapSample[" << i << "]=" << sessionRootPreview.mapSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.visualCameraSamples.size(); ++i)
  {
    logFile << "  sessionRootVisualCameraSample[" << i << "]="
            << sessionRootPreview.visualCameraSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.visualUiEventSamples.size(); ++i)
  {
    logFile << "  sessionRootVisualUiEventSample[" << i << "]="
            << sessionRootPreview.visualUiEventSamples[i] << "\n";
  }
  for (size_t i = 0; i < sessionRootPreview.rollGuildLevelSamples.size(); ++i)
  {
    logFile << "  sessionRootRollGuildLevelSample[" << i << "]="
            << sessionRootPreview.rollGuildLevelSamples[i] << "\n";
  }
  logFile << "  soundRootReady=" << (soundRootPreview.ready ? "yes" : "no") << "\n";
  logFile << "  soundRootDbid=" << (soundRootPreview.dbid.empty() ? "<none>" : soundRootPreview.dbid) << "\n";
  logFile << "  soundRootScenes=" << soundRootPreview.sceneCount << "\n";
  logFile << "  soundRootSceneGroups=" << soundRootPreview.sceneGroupCount << "\n";
  logFile << "  soundRootAmbienceGroups=" << soundRootPreview.ambienceGroupCount << "\n";
  logFile << "  soundRootTimerSounds=" << (soundRootPreview.timerSoundsReady ? "yes" : "no") << "\n";
  logFile << "  soundRootHeartbeat=" << (soundRootPreview.heartbeatReady ? "yes" : "no") << "\n";
  logFile << "  soundRootHeartbeatEvent="
          << (soundRootPreview.heartbeatEvent.empty() ? "<none>" : soundRootPreview.heartbeatEvent) << "\n";
  logFile << "  soundRootAmbient=" << (soundRootPreview.ambientReady ? "yes" : "no") << "\n";
  logFile << "  soundRootAmbientEvent="
          << (soundRootPreview.ambientEvent.empty() ? "<none>" : soundRootPreview.ambientEvent) << "\n";
  logFile << "  soundRootPreferencesVolume=" << (soundRootPreview.preferencesVolumeReady ? "yes" : "no") << "\n";
  logFile << "  soundRootLastHit=" << (soundRootPreview.lastHitReady ? "yes" : "no") << "\n";
  for (size_t i = 0; i < soundRootPreview.cueSamples.size(); ++i)
  {
    logFile << "  soundRootCueSample[" << i << "]=" << soundRootPreview.cueSamples[i] << "\n";
  }
  for (size_t i = 0; i < soundRootPreview.categorySamples.size(); ++i)
  {
    logFile << "  soundRootCategorySample[" << i << "]=" << soundRootPreview.categorySamples[i] << "\n";
  }
  for (size_t i = 0; i < soundRootPreview.ambienceSamples.size(); ++i)
  {
    logFile << "  soundRootAmbienceSample[" << i << "]=" << soundRootPreview.ambienceSamples[i] << "\n";
  }
  logFile << "  resourceCatalogReady=" << (resourceCatalogPreview.ready ? "yes" : "no") << "\n";
  logFile << "  resourceCatalogTalents=" << resourceCatalogPreview.talentCount << "\n";
  logFile << "  resourceCatalogConsumables=" << resourceCatalogPreview.consumableCount << "\n";
  logFile << "  resourceCatalogMarketingItems=" << resourceCatalogPreview.marketingItemCount << "\n";
  for (size_t i = 0; i < resourceCatalogPreview.talentSamples.size(); ++i)
  {
    logFile << "  resourceTalentSample[" << i << "]=" << resourceCatalogPreview.talentSamples[i] << "\n";
  }
  for (size_t i = 0; i < resourceCatalogPreview.consumableSamples.size(); ++i)
  {
    logFile << "  resourceConsumableSample[" << i << "]=" << resourceCatalogPreview.consumableSamples[i] << "\n";
  }
  for (size_t i = 0; i < resourceCatalogPreview.marketingSamples.size(); ++i)
  {
    logFile << "  resourceMarketingSample[" << i << "]=" << resourceCatalogPreview.marketingSamples[i] << "\n";
  }
  logFile << "  replay=" << (settings.replayFile.empty() ? "<none>" : settings.replayFile.string()) << "\n";
  logFile << "  inputInitialized=" << (inputState.initialized ? "yes" : "no") << "\n";
  logFile << "  inputConfigLoaded=" << (inputState.inputConfigLoaded ? "yes" : "no") << "\n";
  logFile << "  inputOverrideLoaded=" << (inputState.inputOverrideLoaded ? "yes" : "no") << "\n";
  logFile << "  inputHardwareControls=" << inputState.hardwareControlCount << "\n";
  logFile << "  inputBindContexts=" << inputState.bindContextCount << "\n";
  logFile << "  inputBindStrings=" << inputState.bindStringCount << "\n";

  for (size_t i = 0; i < contentProbe.warnings.size(); ++i)
  {
    logFile << "  warning=" << contentProbe.warnings[i] << "\n";
  }

  if (!launchPreview.launcherResponse.empty())
  {
    logFile << "  launchLauncherResponse=" << launchPreview.launcherResponse << "\n";
  }

  for (size_t i = 0; i < launchPreview.warnings.size(); ++i)
  {
    logFile << "  launchWarning=" << launchPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < loadingPreview.localizedProperties.size(); ++i)
  {
    logFile << "  loadingText[" << loadingPreview.localizedProperties[i].first << "]="
            << loadingPreview.localizedProperties[i].second << "\n";
  }

  for (size_t i = 0; i < loadingPreview.warnings.size(); ++i)
  {
    logFile << "  loadingWarning=" << loadingPreview.warnings[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.statusSamples.size(); ++i)
  {
    logFile << "  loadingUiStatusSample[" << i << "]=" << loadingUiPreview.statusSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.localeSamples.size(); ++i)
  {
    logFile << "  loadingUiLocaleSample[" << i << "]=" << loadingUiPreview.localeSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.forceColorSamples.size(); ++i)
  {
    logFile << "  loadingUiForceColorSample[" << i << "]=" << loadingUiPreview.forceColorSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.modeSamples.size(); ++i)
  {
    logFile << "  loadingUiModeSample[" << i << "]=" << loadingUiPreview.modeSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.chatChannelSamples.size(); ++i)
  {
    logFile << "  loadingUiChatChannelSample[" << i << "]=" << loadingUiPreview.chatChannelSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.reportTypeSamples.size(); ++i)
  {
    logFile << "  loadingUiReportTypeSample[" << i << "]=" << loadingUiPreview.reportTypeSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.smartChatSamples.size(); ++i)
  {
    logFile << "  loadingUiSmartChatSample[" << i << "]=" << loadingUiPreview.smartChatSamples[i] << "\n";
  }
  for (size_t i = 0; i < loadingUiPreview.warnings.size(); ++i)
  {
    logFile << "  loadingUiWarning=" << loadingUiPreview.warnings[i] << "\n";
  }
  for (size_t i = 0; i < loadingRuntimeDriver.warnings.size(); ++i)
  {
    logFile << "  loadingRuntimeWarning=" << loadingRuntimeDriver.warnings[i] << "\n";
  }

  for (size_t i = 0; i < mapCatalog.featuredEntries.size(); ++i)
  {
    logFile << "  map[" << i << "].descriptor=" << mapCatalog.featuredEntries[i].descriptor << "\n";
    logFile << "  map[" << i << "].category=" << mapCatalog.featuredEntries[i].category << "\n";
    logFile << "  map[" << i << "].type=" << mapCatalog.featuredEntries[i].mapType << "\n";
    logFile << "  map[" << i << "].teamSize=" << mapCatalog.featuredEntries[i].teamSize << "\n";
    logFile << "  map[" << i << "].production=" << (mapCatalog.featuredEntries[i].productionMode ? "yes" : "no") << "\n";
    logFile << "  map[" << i << "].title=" << mapCatalog.featuredEntries[i].title << "\n";
    logFile << "  map[" << i << "].description=" << mapCatalog.featuredEntries[i].description << "\n";
  }

  for (size_t i = 0; i < mapCatalog.warnings.size(); ++i)
  {
    logFile << "  mapWarning=" << mapCatalog.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.warnings.size(); ++i)
  {
    logFile << "  selectedMapWarning=" << selectedMapPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.settings.chainFiles.size(); ++i)
  {
    logFile << "  mapSettingsChain[" << i << "].ref=" << selectedMapPreview.settings.chainReferences[i] << "\n";
    logFile << "  mapSettingsChain[" << i << "].file=" << selectedMapPreview.settings.chainFiles[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.settings.dictionaryKeysPreview.size(); ++i)
  {
    logFile << "  mapDictionaryKey[" << i << "]=" << selectedMapPreview.settings.dictionaryKeysPreview[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.settings.warnings.size(); ++i)
  {
    logFile << "  mapSettingsWarning=" << selectedMapPreview.settings.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.tactical.markers.size() && i < 16; ++i)
  {
    logFile << "  mapMarker[" << i << "].kind=" << selectedMapPreview.tactical.markers[i].kind << "\n";
    logFile << "  mapMarker[" << i << "].label=" << selectedMapPreview.tactical.markers[i].label << "\n";
    logFile << "  mapMarker[" << i << "].team=" << selectedMapPreview.tactical.markers[i].team << "\n";
    logFile << "  mapMarker[" << i << "].position="
            << selectedMapPreview.tactical.markers[i].translateX << ","
            << selectedMapPreview.tactical.markers[i].translateY << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.tactical.warnings.size(); ++i)
  {
    logFile << "  mapTacticalWarning=" << selectedMapPreview.tactical.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.loadingBack.warnings.size(); ++i)
  {
    logFile << "  selectedMapBackWarning=" << selectedMapPreview.loadingBack.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.loadingLogo.warnings.size(); ++i)
  {
    logFile << "  selectedMapLogoWarning=" << selectedMapPreview.loadingLogo.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.minimapFirst.warnings.size(); ++i)
  {
    logFile << "  selectedMinimapFirstWarning=" << selectedMapPreview.minimapFirst.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.minimapSecond.warnings.size(); ++i)
  {
    logFile << "  selectedMinimapSecondWarning=" << selectedMapPreview.minimapSecond.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedMapPreview.minimapNeutral.warnings.size(); ++i)
  {
    logFile << "  selectedMinimapNeutralWarning=" << selectedMapPreview.minimapNeutral.warnings[i] << "\n";
  }

  for (size_t i = 0; i < rootFileSystemPreview.warnings.size(); ++i)
  {
    logFile << "  rootFsWarning=" << rootFileSystemPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < uiRootPreview.warnings.size(); ++i)
  {
    logFile << "  uiRootWarning=" << uiRootPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < sessionRootPreview.warnings.size(); ++i)
  {
    logFile << "  sessionRootWarning=" << sessionRootPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < soundRootPreview.warnings.size(); ++i)
  {
    logFile << "  soundRootWarning=" << soundRootPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < resourceCatalogPreview.warnings.size(); ++i)
  {
    logFile << "  resourceCatalogWarning=" << resourceCatalogPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < configPreview.warnings.size(); ++i)
  {
    logFile << "  configWarning=" << configPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < inputState.warnings.size(); ++i)
  {
    logFile << "  inputWarning=" << inputState.warnings[i] << "\n";
  }

  for (size_t i = 0; i < heroCatalog.warnings.size(); ++i)
  {
    logFile << "  heroWarning=" << heroCatalog.warnings[i] << "\n";
  }

  for (size_t i = 0; i < localMatchPreview.warnings.size(); ++i)
  {
    logFile << "  localMatchWarning=" << localMatchPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < selectedHeroPreview.warnings.size(); ++i)
  {
    logFile << "  selectedHeroWarning=" << selectedHeroPreview.warnings[i] << "\n";
  }

  for (size_t i = 0; i < engineMapStartPreview.warnings.size(); ++i)
  {
    logFile << "  engineStartWarning=" << engineMapStartPreview.warnings[i] << "\n";
  }
  for (size_t i = 0; i < loadingHeroesRuntimePreview.warnings.size(); ++i)
  {
    logFile << "  loadingHeroesRuntimeWarning=" << loadingHeroesRuntimePreview.warnings[i] << "\n";
  }
}

void AppendRuntimeInputLog(
  const LinuxClientEnvironment& environment,
  const LinuxInputState& inputState,
  const LinuxLaunchPreview& launchPreview,
  const LinuxSessionPreview& sessionPreview,
  const LinuxMapCatalog& mapCatalog,
  const LinuxMapBrowserState& mapBrowserState,
  const LinuxSelectedMapPreview& selectedMapPreview,
  const LinuxArtworkSelectionState& artworkState,
  const LinuxHeroCatalog& heroCatalog,
  const LinuxLocalMatchPreview& localMatchPreview,
  const LinuxEngineMapStartPreview& engineMapStartPreview
)
{
  const fs::path logFilePath = ResolveLogsDir(environment) / "linux-client-shell.log";
  std::ofstream logFile(logFilePath.string().c_str(), std::ios::app);
  if (!logFile.is_open())
  {
    return;
  }

  logFile << "[" << FormatCurrentTime() << "] input-summary\n";
  logFile << "  launchSource=" << (launchPreview.source.empty() ? "<none>" : launchPreview.source) << "\n";
  logFile << "  launchMethod=" << (launchPreview.method.empty() ? "<none>" : launchPreview.method) << "\n";
  logFile << "  launchToken=" << MaskSensitiveValue(launchPreview.token) << "\n";
  logFile << "  launchMapId=" << (launchPreview.mapId.empty() ? "<none>" : launchPreview.mapId) << "\n";
  logFile << "  sessionValid=" << (sessionPreview.valid ? "yes" : "no") << "\n";
  logFile << "  sessionCurrentNickname=" << (sessionPreview.currentNickname.empty() ? "<none>" : sessionPreview.currentNickname) << "\n";
  logFile << "  sessionCurrentHeroId="
          << (sessionPreview.currentHeroPersistentId.empty() ? "<none>" : sessionPreview.currentHeroPersistentId) << "\n";
  logFile << "  sessionCurrentTeamId=" << sessionPreview.currentTeamId << "\n";
  logFile << "  sessionPlayersCount=" << sessionPreview.players.size() << "\n";
  logFile << "  inputTotalEvents=" << inputState.totalEvents << "\n";
  logFile << "  inputCommandBindingsTriggered=" << inputState.commandBindingHits << "\n";

  for (size_t i = 0; i < inputState.recentEvents.size(); ++i)
  {
    logFile << "  inputRecent[" << i << "]=" << inputState.recentEvents[i] << "\n";
  }

  if (!mapCatalog.entries.empty() && mapBrowserState.selectedIndex < mapCatalog.entries.size())
  {
    const LinuxMapCatalogEntry& selectedEntry = mapCatalog.entries[mapBrowserState.selectedIndex];
    logFile << "  mapSelectedIndex=" << mapBrowserState.selectedIndex << "\n";
    logFile << "  mapSelectionSource=" << mapBrowserState.selectionSource << "\n";
    logFile << "  mapSelectionChanges=" << mapBrowserState.selectionChanges << "\n";
    logFile << "  mapSelectedDescriptor=" << selectedEntry.descriptor << "\n";
  logFile << "  mapSelectedTitle=" << selectedEntry.title << "\n";
  }
  logFile << "  mapSelectedScriptFile=" << (selectedMapPreview.scriptFile.empty() ? "<none>" : selectedMapPreview.scriptFile) << "\n";
  logFile << "  mapSettingsSource=" << (selectedMapPreview.settings.source.empty() ? "<none>" : selectedMapPreview.settings.source) << "\n";
  logFile << "  mapSettingsChainCount=" << selectedMapPreview.settings.chainFiles.size() << "\n";
  logFile << "  mapBattleStartDelay=" << selectedMapPreview.settings.battleStartDelay << "\n";
  logFile << "  mapStartPrimePerTeam=" << selectedMapPreview.settings.startPrimePerTeam << "\n";
  logFile << "  mapDictionaryResourceCount=" << selectedMapPreview.settings.dictionaryResourceCount << "\n";
  logFile << "  mapTacticalTowerCount=" << selectedMapPreview.tactical.towerCount << "\n";
  logFile << "  mapTacticalHeroSpawnCount=" << selectedMapPreview.tactical.heroSpawnCount << "\n";
  logFile << "  mapTacticalBossCount=" << selectedMapPreview.tactical.bossCount << "\n";
  logFile << "  mapTacticalShopCount=" << selectedMapPreview.tactical.shopCount << "\n";
  logFile << "  mapTacticalGlyphCount=" << selectedMapPreview.tactical.glyphCount << "\n";
  logFile << "  mapSelectedLoadingBackFile=" << (selectedMapPreview.loadingBack.sourceFile.empty() ? "<none>" : selectedMapPreview.loadingBack.sourceFile) << "\n";
  logFile << "  mapSelectedLoadingLogoFile=" << (selectedMapPreview.loadingLogo.sourceFile.empty() ? "<none>" : selectedMapPreview.loadingLogo.sourceFile) << "\n";
  logFile << "  artworkMode=" << DescribeArtworkMode(artworkState.mode) << "\n";
  logFile << "  artworkModeSource=" << artworkState.source << "\n";
  logFile << "  heroCatalogCount=" << heroCatalog.entries.size() << "\n";
  size_t selectedHeroIndex = localMatchPreview.selectedHeroIndex;
  if (localMatchPreview.ready && localMatchPreview.selectedSlotIndex < localMatchPreview.lineup.size())
  {
    selectedHeroIndex = localMatchPreview.lineup[localMatchPreview.selectedSlotIndex].heroIndex;
  }
  if (!heroCatalog.entries.empty() && selectedHeroIndex < heroCatalog.entries.size())
  {
    const LinuxHeroCatalogEntry& selectedHero = heroCatalog.entries[selectedHeroIndex];
    logFile << "  localHeroId=" << (selectedHero.persistentId.empty() ? selectedHero.id : selectedHero.persistentId) << "\n";
    logFile << "  localHeroTitle=" << selectedHero.title << "\n";
  }
  logFile << "  localMatchSelectedSlot=" << localMatchPreview.selectedSlotIndex << "\n";
  logFile << "  localMatchHumanTeam=" << localMatchPreview.humanTeam << "\n";
  logFile << "  localMatchRequestedTeamSize=" << localMatchPreview.requestedTeamSize << "\n";
  logFile << "  localMatchTeamSize=" << localMatchPreview.teamSize << "\n";
  logFile << "  localMatchManualOverrides=" << CountManualHeroOverrides(localMatchPreview) << "\n";
  logFile << "  localMatchGenerationCount=" << localMatchPreview.generationCount << "\n";
  logFile << "  engineStartReady=" << (engineMapStartPreview.ready ? "yes" : "no") << "\n";
  logFile << "  engineStartSource=" << (engineMapStartPreview.source.empty() ? "<none>" : engineMapStartPreview.source) << "\n";
  logFile << "  engineStartTotalSpawners=" << engineMapStartPreview.totalSpawners << "\n";
  logFile << "  engineStartAssignedSlots=" << engineMapStartPreview.assignedSlots << "\n";
  logFile << "  engineStartOverflowPlayers=" << engineMapStartPreview.overflowPlayers << "\n";
  logFile << "  engineStartHumanPlayers=" << engineMapStartPreview.humanPlayers << "\n";
  logFile << "  engineStartBotPlayers=" << engineMapStartPreview.botPlayers << "\n";
  for (size_t i = 0; i < localMatchPreview.lineup.size(); ++i)
  {
    logFile << "  localMatchSlot[" << i << "]="
            << "T" << localMatchPreview.lineup[i].team << " "
            << (localMatchPreview.lineup[i].human ? "Human" : "Bot")
            << (localMatchPreview.lineup[i].manualHero ? "/Manual " : " ")
            << " " << localMatchPreview.lineup[i].heroTitle << "\n";
  }
  for (size_t i = 0; i < engineMapStartPreview.slots.size(); ++i)
  {
    logFile << "  engineStartSlot[" << i << "]="
            << "T" << engineMapStartPreview.slots[i].team << " "
            << (engineMapStartPreview.slots[i].filled ? (engineMapStartPreview.slots[i].human ? "Human" : "Bot") : "Empty")
            << (engineMapStartPreview.slots[i].manualHero ? "/Manual " : " ")
            << " " << (engineMapStartPreview.slots[i].heroTitle.empty() ? "<none>" : engineMapStartPreview.slots[i].heroTitle)
            << " " << (engineMapStartPreview.slots[i].nickname.empty() ? "<none>" : engineMapStartPreview.slots[i].nickname)
            << " @ " << engineMapStartPreview.slots[i].translateX
            << "," << engineMapStartPreview.slots[i].translateY << "\n";
  }
}

const char* SelectWindowTitle(const LinuxClientEnvironment& environment)
{
  if (environment.engineReady)
  {
    return "Prime World Classic - Linux Client Shell - Engine Ready";
  }

  if (!environment.gameRoot.empty())
  {
    return "Prime World Classic - Linux Client Shell - Root Ready";
  }

  return "Prime World Classic - Linux Client Shell - Root Missing";
}
}

int main(int argc, char** argv)
{
  InitializeCmdLine(argc, argv);

  LinuxClientLaunchSettings settings;
  settings.runSeconds = ReadRunSeconds(argc, argv);
  settings.demoCycleSeconds = ReadDemoCycleSeconds(argc, argv);
  settings.width = ReadWindowSize(argc, argv, "--width", 1280);
  settings.height = ReadWindowSize(argc, argv, "--height", 720);
  settings.spectator = CmdLineLite::Instance().IsKeyDefined("spectator");
  settings.tutorial = CmdLineLite::Instance().IsKeyDefined("--launchTutorial");
  settings.localeOverride = ReadLocaleOverride(argc, argv);
  settings.mapSelector = ReadMapSelector(argc, argv);
  settings.heroSelector = ReadHeroSelector(argc, argv);
  if (const char* artworkArg = ReadStringArg(argc, argv, "--artwork"))
  {
    settings.artworkMode = ParseArtworkMode(artworkArg);
  }
  settings.replayFile = DetectReplayFile();
  TryAdoptParentWindowSize(&settings);

  LinuxClientEnvironment environment;
  environment.executablePath = ReadExecutablePath();
  environment.gameRoot = DetectGameRoot(argc, argv);
  InitializeEngineEnvironment(environment.gameRoot, &environment);
  const fs::path logsDir = ResolveLogsDir(environment);
  LinuxInputState inputState;
  InitializeInputState(environment, &inputState);
  LinuxConfigBootstrapPreview configPreview;
  BootstrapConfigState(environment, settings, &configPreview);
  FinalizeInputState(&inputState);
  LinuxLaunchPreview launchPreview;
  ProbeLaunchPreview(&settings, &launchPreview);
  LinuxSessionPreview sessionPreview;
  ProbeSessionPreview(&settings, &sessionPreview);
  LinuxContentProbe contentProbe;
  ProbeContentRoots(environment, settings, configPreview, &contentProbe);
  LinuxRootFileSystemPreview rootFileSystemPreview;
  ProbeRootFileSystem(environment, contentProbe, &rootFileSystemPreview);
  LinuxUiRootPreview uiRootPreview;
  ProbeUiRootPreview(rootFileSystemPreview, &uiRootPreview);
  LinuxSessionRootPreview sessionRootPreview;
  ProbeSessionRootPreview(rootFileSystemPreview, &sessionRootPreview);
  LinuxSoundRootPreview soundRootPreview;
  ProbeSoundRootPreview(rootFileSystemPreview, &soundRootPreview);
  LinuxResourceCatalogPreview resourceCatalogPreview;
  ProbeResourceCatalogPreview(environment, &resourceCatalogPreview);
  LinuxLoadingScreenPreview loadingPreview;
  ProbeLoadingScreenAssets(environment, contentProbe, &loadingPreview);
  LinuxLoadingUiPreview loadingUiPreview;
  ProbeLoadingUiPreview(rootFileSystemPreview, &loadingUiPreview);
  LinuxLoadingUiState loadingUiState;
  InitializeLoadingUiState(contentProbe, loadingUiPreview, &loadingUiState);
  LinuxLoadingRuntimeDriver loadingRuntimeDriver;
  InitializeLoadingRuntimeDriver(loadingUiPreview, &loadingRuntimeDriver, &loadingUiState);
  LinuxLoadingArtwork loadingArtwork;
  LoadLoadingScreenArtwork(contentProbe, &loadingPreview, &loadingArtwork);
  LinuxMapCatalog mapCatalog;
  ProbeMapCatalog(environment, &mapCatalog);
  LinuxMapBrowserState mapBrowserState;
  InitializeMapBrowserState(settings, mapCatalog, &mapBrowserState);
  if (launchPreview.mapIdProvided && settings.mapSelector == launchPreview.mapId)
  {
    if (mapBrowserState.selectionSource == "command-line")
    {
      mapBrowserState.selectionSource = "launch-mapId";
    }
    else if (mapBrowserState.selectionSource == "command-line-miss")
    {
      mapBrowserState.selectionSource = "launch-mapId-miss";
    }
  }
  LinuxArtworkSelectionState artworkState;
  LinuxHeroCatalog heroCatalog;
  ProbeHeroCatalog(environment, &heroCatalog);
  LinuxLocalMatchPreview localMatchPreview;
  InitializeLocalMatchPreview(heroCatalog, mapCatalog, mapBrowserState, &localMatchPreview);
  ApplyLaunchSelections(settings, heroCatalog, mapCatalog, mapBrowserState, &artworkState, &localMatchPreview);
  ApplySessionSelections(&sessionPreview, heroCatalog, mapCatalog, &mapBrowserState, &localMatchPreview);
  LinuxSelectedHeroDbPreview selectedHeroPreview;
  ProbeSelectedHeroDbPreview(environment, sessionRootPreview, heroCatalog, localMatchPreview, &selectedHeroPreview);
  LinuxSelectedMapPreview selectedMapPreview;
  ProbeSelectedMapPreview(environment, mapCatalog, mapBrowserState, &selectedMapPreview);
  LinuxEngineMapStartPreview engineMapStartPreview;
  ProbeEngineMapStartPreview(
    sessionPreview,
    heroCatalog,
    selectedMapPreview,
    mapCatalog,
    mapBrowserState,
    localMatchPreview,
    contentProbe.locale,
    &engineMapStartPreview
  );
  LinuxLoadingHeroesRuntimePreview loadingHeroesRuntimePreview;
  ProbeLoadingHeroesRuntimePreview(
    sessionPreview,
    selectedMapPreview,
    engineMapStartPreview,
    &loadingHeroesRuntimePreview
  );

  char appName[256] = {0};
  snprintf(
    appName,
    sizeof(appName),
    "%s - %s - %d.%d.%02d.%04d",
    PRODUCT_TITLE,
    VERSION_LINE,
    VERSION_MAJOR,
    VERSION_MINOR,
    VERSION_PATCH,
    VERSION_REVISION
  );

  const char* windowTitle = SelectWindowTitle(environment);
  if (!NMainFrame::InitApplication(0, appName, windowTitle, 0, false, settings.width, settings.height, 0))
  {
    fprintf(stderr, "Failed to initialize the native Linux client shell window.\n");
    return 1;
  }

  LinuxWindowOverlay overlay;
  InitializeWindowOverlay(&overlay);
  LinuxLoadingArtwork displayArtwork;
  std::string displayArtworkSource;
  const bool displayArtworkReady = BuildSelectedMapDisplayArtwork(
    environment,
    settings,
    loadingArtwork,
    loadingUiPreview,
    loadingUiState,
    selectedMapPreview,
    artworkState,
    heroCatalog,
    localMatchPreview,
    selectedHeroPreview,
    engineMapStartPreview,
    &displayArtwork,
    &displayArtworkSource
  );
  const bool artworkUploaded = displayArtworkReady && UploadArtworkPixmap(&overlay, displayArtwork);
  if (displayArtworkReady && !artworkUploaded)
  {
    loadingPreview.warnings.push_back("Linux artwork upload failed");
  }

  WriteStartupLog(
    environment,
    settings,
    overlay,
    launchPreview,
    sessionPreview,
    configPreview,
    contentProbe,
    loadingPreview,
    loadingUiPreview,
    loadingRuntimeDriver,
    loadingHeroesRuntimePreview,
    loadingUiState,
    mapCatalog,
    mapBrowserState,
    selectedMapPreview,
    artworkState,
    heroCatalog,
    localMatchPreview,
    selectedHeroPreview,
    engineMapStartPreview,
    rootFileSystemPreview,
    uiRootPreview,
    sessionRootPreview,
    soundRootPreview,
    resourceCatalogPreview,
    inputState
  );

  fprintf(stdout, "Prime World Linux client shell started.\n");
  fprintf(stdout, "Renderer backend is not ported yet. Input now uses a native Linux binds bootstrap.\n");
  fprintf(stdout, "Executable: %s\n", environment.executablePath.empty() ? "<unknown>" : environment.executablePath.string().c_str());
  fprintf(stdout, "Game root: %s\n", environment.gameRoot.empty() ? "<not found>" : environment.gameRoot.string().c_str());
  fprintf(stdout, "Base dir: %s\n", environment.baseDir.empty() ? "<not initialized>" : environment.baseDir.string().c_str());
  fprintf(stdout, "Bin dir: %s\n", environment.binDir.empty() ? "<not initialized>" : environment.binDir.string().c_str());
  fprintf(stdout, "User dir: %s\n", environment.userDir.empty() ? "<not initialized>" : environment.userDir.string().c_str());
  fprintf(stdout, "Logs: %s\n", logsDir.string().c_str());
  fprintf(stdout, "Window size source: %s\n",
    (settings.widthFromParent || settings.heightFromParent) ? "parent launch" : "default/command-line");
  fprintf(stdout, "Launch source: %s\n", launchPreview.source.empty() ? "<none>" : launchPreview.source.c_str());
  fprintf(stdout, "Launch protocol: %s\n",
    launchPreview.protocolPresent ?
      (launchPreview.protocolValid ? launchPreview.method.c_str() : "invalid") :
      "<none>");
  fprintf(stdout, "Launch version: %s (match=%s)\n",
    launchPreview.version.empty() ? "<none>" : launchPreview.version.c_str(),
    launchPreview.versionMatches ? "yes" : "no");
  fprintf(stdout, "Launch token: %s\n", MaskSensitiveValue(launchPreview.token).c_str());
  fprintf(stdout, "Launch mapId/server/uid: %s | %s | %s\n",
    launchPreview.mapId.empty() ? "<none>" : launchPreview.mapId.c_str(),
    launchPreview.serverName.empty() ? "<none>" : launchPreview.serverName.c_str(),
    launchPreview.uid.empty() ? "<none>" : launchPreview.uid.c_str());
  fprintf(stdout, "Launch session login: %s\n",
    launchPreview.sessionLogin.empty() ? "<none>" : launchPreview.sessionLogin.c_str());
  for (size_t i = 0; i < launchPreview.warnings.size(); ++i)
  {
    fprintf(stdout, "Launch warning: %s\n", launchPreview.warnings[i].c_str());
  }
  fprintf(stdout, "Session JSON: %s\n",
    sessionPreview.fileProvided ? sessionPreview.filePath.c_str() : "<none>");
  fprintf(stdout, "Session import: loaded=%s valid=%s method=%s players=%lu\n",
    sessionPreview.loaded ? "yes" : "no",
    sessionPreview.valid ? "yes" : "no",
    sessionPreview.method.empty() ? "<none>" : sessionPreview.method.c_str(),
    static_cast<unsigned long>(sessionPreview.players.size()));
  fprintf(stdout, "Session current: %s team=%d hero=%s\n",
    sessionPreview.currentNickname.empty() ? "<none>" : sessionPreview.currentNickname.c_str(),
    sessionPreview.currentTeamId,
    sessionPreview.currentHeroPersistentId.empty() ? "<none>" : sessionPreview.currentHeroPersistentId.c_str());
  fprintf(stdout, "Session teams: %lu vs %lu\n",
    static_cast<unsigned long>(CountSessionTeamPlayers(sessionPreview, 1)),
    static_cast<unsigned long>(CountSessionTeamPlayers(sessionPreview, 2)));
  for (size_t i = 0; i < sessionPreview.players.size(); ++i)
  {
    fprintf(stdout, "Session player: T%d %s%s hero=%s web=%d\n",
      sessionPreview.players[i].teamId,
      sessionPreview.players[i].nickname.empty() ? "<none>" : sessionPreview.players[i].nickname.c_str(),
      sessionPreview.players[i].currentPlayer ? " [current]" : "",
      sessionPreview.players[i].heroPersistentId.empty() ? "<none>" : sessionPreview.players[i].heroPersistentId.c_str(),
      sessionPreview.players[i].heroWebId);
  }
  for (size_t i = 0; i < sessionPreview.warnings.size(); ++i)
  {
    fprintf(stdout, "Session warning: %s\n", sessionPreview.warnings[i].c_str());
  }
  fprintf(stdout, "Config default/social/game: %s/%s/%s\n",
    configPreview.defaultLoaded ? "yes" : "no",
    configPreview.socialLoaded ? "yes" : "no",
    configPreview.gameLoaded ? "yes" : "no");
  fprintf(stdout, "Config language: %s\n", configPreview.language.empty() ? "<none>" : configPreview.language.c_str());
  fprintf(stdout, "Config local_game: %s\n", configPreview.localGame.empty() ? "<none>" : configPreview.localGame.c_str());
  fprintf(stdout, "Config login: %s\n", configPreview.loginAddress.empty() ? "<none>" : configPreview.loginAddress.c_str());
  fprintf(stdout, "Locale: %s\n", contentProbe.locale.empty() ? "<none>" : contentProbe.locale.c_str());
  fprintf(stdout, "Data mounted: %s\n", contentProbe.dataMounted ? "yes" : "no");
  fprintf(stdout, "Localization mounted: %s\n", contentProbe.localizationMounted ? "yes" : "no");
  fprintf(stdout, "Loading layout: %s\n", loadingPreview.layoutFound ? "yes" : "no");
  fprintf(stdout, "Loading flash: %s\n", loadingPreview.flashAsset.empty() ? "<none>" : loadingPreview.flashAsset.c_str());
  fprintf(stdout, "Loading artwork: %s\n", loadingPreview.artworkLoaded ? loadingPreview.artworkFile.c_str() : "<none>");
  fprintf(stdout, "Loading DB: %s statuses=%lu tips=%lu locales=%lu forceColors=%lu\n",
    loadingUiPreview.ready ? "ready" : "missing",
    static_cast<unsigned long>(loadingUiPreview.statusCount),
    static_cast<unsigned long>(loadingUiPreview.tipCount),
    static_cast<unsigned long>(loadingUiPreview.localeCount),
    static_cast<unsigned long>(loadingUiPreview.forceColorCount));
  fprintf(stdout, "Loading UI extras: minimap=%s icons=%lu smartChat=%s chat=%lu reports=%lu binds=%lu recent=%d\n",
    loadingUiPreview.minimapReady ? "yes" : "no",
    static_cast<unsigned long>(loadingUiPreview.minimapIconCount),
    loadingUiPreview.smartChatReady ? "yes" : "no",
    static_cast<unsigned long>(loadingUiPreview.chatChannelCount),
    static_cast<unsigned long>(loadingUiPreview.reportTypeCount),
    static_cast<unsigned long>(loadingUiPreview.bindCount),
    loadingUiPreview.recentPlayers);
  fprintf(stdout, "Loading modes: maneuvers=%s guard=%s guild=%s custom=%s\n",
    loadingUiPreview.maneuversModeReady ? "yes" : "no",
    loadingUiPreview.guardModeReady ? "yes" : "no",
    loadingUiPreview.guildModeReady ? "yes" : "no",
    loadingUiPreview.customModeReady ? "yes" : "no");
  if (!loadingUiPreview.statusSamples.empty())
  {
    fprintf(stdout, "Loading status samples: %s\n", JoinPreviewSamples(loadingUiPreview.statusSamples).c_str());
  }
  if (!loadingRuntimeDriver.samples.empty())
  {
    fprintf(stdout, "Loading runtime samples: %s\n", JoinPreviewSamples(loadingRuntimeDriver.samples).c_str());
  }
  if (!loadingHeroesRuntimePreview.samples.empty())
  {
    fprintf(stdout, "Loading hero samples: %s\n", JoinPreviewSamples(loadingHeroesRuntimePreview.samples).c_str());
  }
  if (!loadingHeroesRuntimePreview.metaSamples.empty())
  {
    fprintf(stdout, "Loading hero metadata: %s\n", JoinPreviewSamples(loadingHeroesRuntimePreview.metaSamples).c_str());
  }
  if (!loadingUiPreview.localeSamples.empty())
  {
    fprintf(stdout, "Loading locale samples: %s\n", JoinPreviewSamples(loadingUiPreview.localeSamples).c_str());
  }
  if (!loadingUiPreview.modeSamples.empty())
  {
    fprintf(stdout, "Loading mode samples: %s\n", JoinPreviewSamples(loadingUiPreview.modeSamples).c_str());
  }
  if (!loadingUiPreview.chatChannelSamples.empty())
  {
    fprintf(stdout, "Loading chat samples: %s\n", JoinPreviewSamples(loadingUiPreview.chatChannelSamples).c_str());
  }
  if (!loadingUiPreview.reportTypeSamples.empty())
  {
    fprintf(stdout, "Loading report samples: %s\n", JoinPreviewSamples(loadingUiPreview.reportTypeSamples).c_str());
  }
  if (!loadingUiPreview.smartChatSamples.empty())
  {
    fprintf(stdout, "Loading smart-chat samples: %s\n", JoinPreviewSamples(loadingUiPreview.smartChatSamples).c_str());
  }
  if (!loadingUiPreview.sampleTip.empty())
  {
    fprintf(stdout, "Loading tip sample: %s\n", loadingUiPreview.sampleTip.c_str());
  }
  if (!loadingUiPreview.statuses.empty() && loadingUiState.statusIndex < loadingUiPreview.statuses.size())
  {
    fprintf(stdout, "Loading status active: %s -> %s\n",
      loadingUiPreview.statuses[loadingUiState.statusIndex].key.c_str(),
      loadingUiPreview.statuses[loadingUiState.statusIndex].text.empty() ?
        "<empty>" : loadingUiPreview.statuses[loadingUiState.statusIndex].text.c_str());
  }
  if (!loadingUiState.runtimeEvent.empty())
  {
    fprintf(stdout, "Loading runtime active: %s -> %s\n",
      loadingUiState.runtimeEvent.c_str(),
      loadingUiState.runtimeStatusText.empty() ? "<empty>" : loadingUiState.runtimeStatusText.c_str());
  }
  if (loadingHeroesRuntimePreview.ready)
  {
    fprintf(stdout, "Loading runtime heroes: %lu total, %lu humans, %lu bots, %lu disconnected, %lu premium, %lu locales, %lu flagged\n",
      static_cast<unsigned long>(loadingHeroesRuntimePreview.heroes.size()),
      static_cast<unsigned long>(loadingHeroesRuntimePreview.humanCount),
      static_cast<unsigned long>(loadingHeroesRuntimePreview.botCount),
      static_cast<unsigned long>(loadingHeroesRuntimePreview.disconnectedCount),
      static_cast<unsigned long>(loadingHeroesRuntimePreview.premiumCount),
      static_cast<unsigned long>(loadingHeroesRuntimePreview.localeCount),
      static_cast<unsigned long>(loadingHeroesRuntimePreview.flaggedCount));
  }
  if (!loadingUiPreview.tips.empty() && loadingUiState.tipIndex < loadingUiPreview.tips.size())
  {
    fprintf(stdout, "Loading tip active: %s\n", loadingUiPreview.tips[loadingUiState.tipIndex].c_str());
  }
  if (!loadingUiPreview.locales.empty() && loadingUiState.currentLocaleIndex < loadingUiPreview.locales.size())
  {
    fprintf(stdout, "Loading locale current: %s (%s)\n",
      loadingUiPreview.locales[loadingUiState.currentLocaleIndex].locale.c_str(),
      loadingUiPreview.locales[loadingUiState.currentLocaleIndex].tooltip.empty() ?
        "<empty>" : loadingUiPreview.locales[loadingUiState.currentLocaleIndex].tooltip.c_str());
  }
  if (!loadingUiPreview.locales.empty() && loadingUiState.enemyLocaleIndex < loadingUiPreview.locales.size())
  {
    fprintf(stdout, "Loading locale enemy: %s (%s)\n",
      loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].locale.c_str(),
      loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].tooltip.empty() ?
        "<empty>" : loadingUiPreview.locales[loadingUiState.enemyLocaleIndex].tooltip.c_str());
  }
  if (!loadingUiPreview.modes.empty() && loadingUiState.modeIndex < loadingUiPreview.modes.size())
  {
    fprintf(stdout, "Loading mode active: %s -> %s\n",
      loadingUiPreview.modes[loadingUiState.modeIndex].id.c_str(),
      loadingUiPreview.modes[loadingUiState.modeIndex].tooltip.empty() ?
        "<empty>" : loadingUiPreview.modes[loadingUiState.modeIndex].tooltip.c_str());
  }
  if (!loadingUiPreview.premiumTooltip.empty())
  {
    fprintf(stdout, "Loading premium tooltip: %s\n", loadingUiPreview.premiumTooltip.c_str());
  }
  for (size_t i = 0; i < loadingRuntimeDriver.warnings.size(); ++i)
  {
    fprintf(stdout, "Loading runtime warning: %s\n", loadingRuntimeDriver.warnings[i].c_str());
  }
  for (size_t i = 0; i < loadingHeroesRuntimePreview.warnings.size(); ++i)
  {
    fprintf(stdout, "Loading heroes warning: %s\n", loadingHeroesRuntimePreview.warnings[i].c_str());
  }
  for (size_t i = 0; i < loadingUiPreview.warnings.size(); ++i)
  {
    fprintf(stdout, "Loading DB warning: %s\n", loadingUiPreview.warnings[i].c_str());
  }
  fprintf(stdout, "Displayed artwork source: %s\n", displayArtworkSource.c_str());
  fprintf(stdout, "Overlay backend: %s\n", overlay.openglReady ? "OpenGL" : "X11");
  fprintf(stdout, "Loading artwork uploaded: %s\n", artworkUploaded ? "yes" : "no");
  fprintf(stdout, "Artwork mode: %s\n", DescribeArtworkMode(artworkState.mode));
  fprintf(stdout, "Demo cycle: %s\n", settings.demoCycleSeconds > 0.0 ? NStr::StrFmt("%.1fs", settings.demoCycleSeconds) : "off");
  fprintf(stdout, "Map catalog: %lu maps (%lu PvP, %lu PvE, %lu tutorial)\n",
    static_cast<unsigned long>(mapCatalog.descriptorCount),
    static_cast<unsigned long>(mapCatalog.pvpCount),
    static_cast<unsigned long>(mapCatalog.pveCount),
    static_cast<unsigned long>(mapCatalog.tutorialCount));
  if (!mapCatalog.entries.empty() && mapBrowserState.selectedIndex < mapCatalog.entries.size())
  {
    const LinuxMapCatalogEntry& selectedEntry = mapCatalog.entries[mapBrowserState.selectedIndex];
    fprintf(stdout, "Selected map: %s (%s)\n", selectedEntry.title.c_str(), selectedEntry.descriptor.c_str());
  }
  fprintf(stdout, "Selected map objects/scripted: %lu/%lu\n",
    static_cast<unsigned long>(selectedMapPreview.objectCount),
    static_cast<unsigned long>(selectedMapPreview.scriptedObjectCount));
  fprintf(stdout, "Selected map settings: %s source=%s chain=%lu\n",
    selectedMapPreview.mapSettingsResolved ? "resolved" : "missing",
    selectedMapPreview.settings.source.empty() ? "<none>" : selectedMapPreview.settings.source.c_str(),
    static_cast<unsigned long>(selectedMapPreview.settings.chainFiles.size()));
  fprintf(stdout, "Selected map script: %s\n",
    selectedMapPreview.scriptFile.empty() ? "<none>" : selectedMapPreview.scriptFile.c_str());
  fprintf(stdout, "Selected map rules: delay=%d prime=%d force=%d portal=%s stats=%s\n",
    selectedMapPreview.settings.battleStartDelay,
    selectedMapPreview.settings.startPrimePerTeam,
    selectedMapPreview.settings.force,
    selectedMapPreview.settings.enablePortalTalent ? "yes" : "no",
    selectedMapPreview.settings.enableStatistics ? "yes" : "no");
  fprintf(stdout, "Selected map objectives: towers=%lu spawns=%lu lane=%lu neutral=%lu bosses=%lu shops=%lu glyphs=%lu\n",
    static_cast<unsigned long>(selectedMapPreview.tactical.towerCount),
    static_cast<unsigned long>(selectedMapPreview.tactical.heroSpawnCount),
    static_cast<unsigned long>(selectedMapPreview.tactical.laneSpawnerCount),
    static_cast<unsigned long>(selectedMapPreview.tactical.neutralSpawnerCount),
    static_cast<unsigned long>(selectedMapPreview.tactical.bossCount),
    static_cast<unsigned long>(selectedMapPreview.tactical.shopCount),
    static_cast<unsigned long>(selectedMapPreview.tactical.glyphCount));
  fprintf(stdout, "Selected map back art: %s\n",
    selectedMapPreview.loadingBack.sourceFile.empty() ? "<none>" : selectedMapPreview.loadingBack.sourceFile.c_str());
  fprintf(stdout, "Selected map logo art: %s\n",
    selectedMapPreview.loadingLogo.sourceFile.empty() ? "<none>" : selectedMapPreview.loadingLogo.sourceFile.c_str());
  fprintf(stdout, "Selected minimaps: %s | %s | %s\n",
    selectedMapPreview.minimapFirst.sourceFile.empty() ? "<none>" : selectedMapPreview.minimapFirst.sourceFile.c_str(),
    selectedMapPreview.minimapSecond.sourceFile.empty() ? "<none>" : selectedMapPreview.minimapSecond.sourceFile.c_str(),
    selectedMapPreview.minimapNeutral.sourceFile.empty() ? "<none>" : selectedMapPreview.minimapNeutral.sourceFile.c_str());
  fprintf(stdout, "Hero catalog: %lu legal heroes\n",
    static_cast<unsigned long>(heroCatalog.entries.size()));
  size_t selectedHeroIndex = localMatchPreview.selectedHeroIndex;
  if (localMatchPreview.ready && localMatchPreview.selectedSlotIndex < localMatchPreview.lineup.size())
  {
    selectedHeroIndex = localMatchPreview.lineup[localMatchPreview.selectedSlotIndex].heroIndex;
  }
  if (!heroCatalog.entries.empty() && selectedHeroIndex < heroCatalog.entries.size())
  {
    const LinuxHeroCatalogEntry& selectedHero = heroCatalog.entries[selectedHeroIndex];
    fprintf(stdout, "Selected lineup hero: %s (%s)\n",
      selectedHero.title.c_str(),
      selectedHero.persistentId.empty() ? selectedHero.id.c_str() : selectedHero.persistentId.c_str());
  }
  fprintf(stdout, "Selected hero DB: %s attack=%s abilities=%lu talents=%lu/%lu/%lu stats=%lu upgrades=%lu\n",
    selectedHeroPreview.found ? "ready" : "missing",
    selectedHeroPreview.attackReady ? "yes" : "no",
    static_cast<unsigned long>(selectedHeroPreview.abilityCount),
    static_cast<unsigned long>(selectedHeroPreview.defaultTalentSetCount),
    static_cast<unsigned long>(selectedHeroPreview.defaultTalentLevelCount),
    static_cast<unsigned long>(selectedHeroPreview.defaultTalentSlotCount),
    static_cast<unsigned long>(selectedHeroPreview.statsCount),
    static_cast<unsigned long>(selectedHeroPreview.levelUpgradeCount));
  fprintf(stdout, "Selected hero talent icons: %lu/%lu\n",
    static_cast<unsigned long>(selectedHeroPreview.defaultTalentIconCount),
    static_cast<unsigned long>(selectedHeroPreview.defaultTalentPreviews.size()));
  if (!selectedHeroPreview.featuredAbilities.empty())
  {
    std::vector<std::string> abilitySamples;
    for (size_t i = 0; i < selectedHeroPreview.featuredAbilities.size(); ++i)
    {
      const LinuxHeroAbilityPreview& abilityPreview = selectedHeroPreview.featuredAbilities[i];
      std::string sample = abilityPreview.isAttack ? "Attack" : abilityPreview.name;
      if (!abilityPreview.isAttack && !abilityPreview.type.empty())
      {
        sample += " [" + abilityPreview.type + "]";
      }
      AppendSampleValue(&abilitySamples, sample, 4);
    }

    if (!abilitySamples.empty())
    {
      fprintf(stdout, "Selected hero abilities: %s\n", JoinPreviewSamples(abilitySamples).c_str());
    }
  }
  if (!selectedHeroPreview.uniqueResourceName.empty())
  {
    fprintf(stdout, "Selected hero resource: %s\n", selectedHeroPreview.uniqueResourceName.c_str());
  }
  fprintf(stdout, "Selected lineup slot: %lu\n",
    static_cast<unsigned long>(localMatchPreview.selectedSlotIndex + 1));
  fprintf(stdout, "Local match lineup: teamSize=%lu slots=%lu team=%d manual=%lu\n",
    static_cast<unsigned long>(localMatchPreview.teamSize),
    static_cast<unsigned long>(localMatchPreview.lineup.size()),
    localMatchPreview.humanTeam,
    static_cast<unsigned long>(CountManualHeroOverrides(localMatchPreview)));
  fprintf(stdout, "Engine start slots: %lu total, %lu assigned, %lu overflow via %s\n",
    static_cast<unsigned long>(engineMapStartPreview.totalSpawners),
    static_cast<unsigned long>(engineMapStartPreview.assignedSlots),
    static_cast<unsigned long>(engineMapStartPreview.overflowPlayers),
    engineMapStartPreview.source.empty() ? "<none>" : engineMapStartPreview.source.c_str());
  fprintf(stdout, "Engine start players: humans=%lu bots=%lu maxTeam=%d seed=%d\n",
    static_cast<unsigned long>(engineMapStartPreview.humanPlayers),
    static_cast<unsigned long>(engineMapStartPreview.botPlayers),
    engineMapStartPreview.maxPlayersPerTeam,
    engineMapStartPreview.randomSeed);
  fprintf(stdout, "RootFS mounted: %s\n", rootFileSystemPreview.mounted ? "yes" : "no");
  fprintf(stdout, "RootFS DB cache: %s\n", rootFileSystemPreview.dbCacheReady ? "ready" : "missing");
  fprintf(stdout, "RootFS textref: %s\n", rootFileSystemPreview.textRefValue.empty() ? "<none>" : rootFileSystemPreview.textRefValue.c_str());
  fprintf(stdout, "UIRoot: %s screens=%lu cursors=%lu contents=%lu consts=%lu\n",
    uiRootPreview.ready ? "ready" : "missing",
    static_cast<unsigned long>(uiRootPreview.screenCount),
    static_cast<unsigned long>(uiRootPreview.cursorCount),
    static_cast<unsigned long>(uiRootPreview.contentGroupCount),
    static_cast<unsigned long>(uiRootPreview.constantCount));
  if (!uiRootPreview.screenSamples.empty())
  {
    fprintf(stdout, "UI screen samples: %s\n", JoinPreviewSamples(uiRootPreview.screenSamples).c_str());
  }
  fprintf(stdout, "SessionRoot: %s (%s)\n",
    sessionRootPreview.ready ? "ready" : "missing",
    sessionRootPreview.rootDbid.empty() ? "<none>" : sessionRootPreview.rootDbid.c_str());
  fprintf(stdout, "Session logic DB: heroes=%lu legal=%lu maps=%lu ai=%s bots=%s portal=%s\n",
    static_cast<unsigned long>(sessionRootPreview.logicHeroCount),
    static_cast<unsigned long>(sessionRootPreview.logicLegalHeroCount),
    static_cast<unsigned long>(sessionRootPreview.logicMapCount),
    sessionRootPreview.logicAiReady ? "yes" : "no",
    sessionRootPreview.logicBotsSettingsReady ? "yes" : "no",
    sessionRootPreview.logicPortalReady ? "yes" : "no");
  fprintf(stdout, "Session progression: unitCats=%lu/%lu teamNames=%lu ranks=%lu levels=%lu exp=%d..%d\n",
    static_cast<unsigned long>(sessionRootPreview.uiUnitCategoryCount),
    static_cast<unsigned long>(sessionRootPreview.uiUnitCategoryParamCount),
    static_cast<unsigned long>(sessionRootPreview.logicTeamNameCount),
    static_cast<unsigned long>(sessionRootPreview.logicHeroRankCount),
    static_cast<unsigned long>(sessionRootPreview.logicLevelCount),
    sessionRootPreview.logicLevelFirstExp,
    sessionRootPreview.logicLevelLastExp);
  fprintf(stdout, "Session gameplay DB: scoring=%s glyphs=%lu levelups=%lu formulas=%lu/%lu/%lu units=%lu guild=%lu\n",
    sessionRootPreview.logicScoringReady ? "yes" : "no",
    static_cast<unsigned long>(sessionRootPreview.logicGlyphCount),
    static_cast<unsigned long>(sessionRootPreview.logicLevelUpCount),
    static_cast<unsigned long>(sessionRootPreview.logicFloatFormulaCount),
    static_cast<unsigned long>(sessionRootPreview.logicBoolFormulaCount),
    static_cast<unsigned long>(sessionRootPreview.logicIntFormulaCount),
    static_cast<unsigned long>(sessionRootPreview.logicUnitParameterCount),
    static_cast<unsigned long>(sessionRootPreview.logicGuildBuffCount));
  fprintf(stdout, "Session scoring: achievements=%lu titles=%lu scoreDefs=%lu teleports=%lu rollMods=%lu/%lu\n",
    static_cast<unsigned long>(sessionRootPreview.logicScoringAchievementCount),
    static_cast<unsigned long>(sessionRootPreview.logicScoringHeroTitleCount),
    static_cast<unsigned long>(sessionRootPreview.logicScoringDescriptionCount),
    static_cast<unsigned long>(sessionRootPreview.logicScoringTeleporterCount),
    static_cast<unsigned long>(sessionRootPreview.rollRatingModifierCount),
    static_cast<unsigned long>(sessionRootPreview.rollFullPartyModifierCount));
  fprintf(stdout, "Session AI: waves=%d levelCap=%d emblem=%d botsAI=%s midOnly=%s go=%d tp=%d\n",
    sessionRootPreview.logicCreepsWavesDelay,
    sessionRootPreview.logicCreepLevelCap,
    sessionRootPreview.logicBaseEmblemHeroNeeds,
    sessionRootPreview.botsAiEnabled ? "yes" : "no",
    sessionRootPreview.botsMidOnly ? "yes" : "no",
    sessionRootPreview.logicBotsTimeToGo,
    sessionRootPreview.logicBotsTimeToTeleport);
  if (!sessionRootPreview.logicTeamNameSamples.empty())
  {
    fprintf(stdout, "Session team names: %s\n", JoinPreviewSamples(sessionRootPreview.logicTeamNameSamples).c_str());
  }
  if (!sessionRootPreview.logicGlyphSamples.empty())
  {
    fprintf(stdout, "Session glyph samples: %s\n", JoinPreviewSamples(sessionRootPreview.logicGlyphSamples).c_str());
  }
  if (!sessionRootPreview.logicScoreSamples.empty())
  {
    fprintf(stdout, "Session score samples: %s\n", JoinPreviewSamples(sessionRootPreview.logicScoreSamples).c_str());
  }
  if (!sessionRootPreview.logicGuildBuffSamples.empty())
  {
    fprintf(stdout, "Session guild buff samples: %s\n", JoinPreviewSamples(sessionRootPreview.logicGuildBuffSamples).c_str());
  }
  if (!sessionRootPreview.logicRankSamples.empty())
  {
    fprintf(stdout, "Session rank samples: %s\n", JoinPreviewSamples(sessionRootPreview.logicRankSamples).c_str());
  }
  fprintf(stdout, "Session visual DB: cameras=%lu animSets=%lu winLose=%lu auras=%lu/%lu uiEvents=%lu\n",
    static_cast<unsigned long>(sessionRootPreview.visualCameraCount),
    static_cast<unsigned long>(sessionRootPreview.visualAnimSetCount),
    static_cast<unsigned long>(sessionRootPreview.visualWinLoseCount),
    static_cast<unsigned long>(sessionRootPreview.visualSelfAuraCount),
    static_cast<unsigned long>(sessionRootPreview.visualAuraCount),
    static_cast<unsigned long>(sessionRootPreview.visualUiEventCount));
  if (!sessionRootPreview.visualCameraSamples.empty())
  {
    fprintf(stdout, "Session camera samples: %s\n", JoinPreviewSamples(sessionRootPreview.visualCameraSamples).c_str());
  }
  if (!sessionRootPreview.visualUiEventSamples.empty())
  {
    fprintf(stdout, "Session UI event samples: %s\n", JoinPreviewSamples(sessionRootPreview.visualUiEventSamples).c_str());
  }
  fprintf(stdout, "Session roll DB: pvp=%s mode=%s win=%d cap=%d premium=%lu guild=%lu excl=%d/%d\n",
    sessionRootPreview.rollPvpReady ? "yes" : "no",
    sessionRootPreview.rollPvpModeName.empty() ? "<none>" : sessionRootPreview.rollPvpModeName.c_str(),
    sessionRootPreview.rollPvpContainersOnWin,
    sessionRootPreview.rollPvpScoreCap,
    static_cast<unsigned long>(sessionRootPreview.rollPvpPremiumContainerCount),
    static_cast<unsigned long>(sessionRootPreview.rollGuildLevelCount),
    sessionRootPreview.rollRequiredLevelForExclusiveTalents,
    sessionRootPreview.rollRequiredRatingForExclusiveTalents);
  if (!sessionRootPreview.rollGuildLevelSamples.empty())
  {
    fprintf(stdout, "Session guild level samples: %s\n", JoinPreviewSamples(sessionRootPreview.rollGuildLevelSamples).c_str());
  }
  fprintf(stdout, "Session messages: %s\n",
    sessionRootPreview.dxErrorTitle.empty() ? "<none>" : sessionRootPreview.dxErrorTitle.c_str());
  fprintf(stdout, "SoundRoot: %s scenes=%lu groups=%lu ambience=%lu heartbeat=%s ambient=%s\n",
    soundRootPreview.ready ? "ready" : "missing",
    static_cast<unsigned long>(soundRootPreview.sceneCount),
    static_cast<unsigned long>(soundRootPreview.sceneGroupCount),
    static_cast<unsigned long>(soundRootPreview.ambienceGroupCount),
    soundRootPreview.heartbeatReady ? "yes" : "no",
    soundRootPreview.ambientReady ? "yes" : "no");
  if (!soundRootPreview.cueSamples.empty())
  {
    fprintf(stdout, "Sound cue samples: %s\n", JoinPreviewSamples(soundRootPreview.cueSamples).c_str());
  }
  fprintf(stdout, "Resource catalogs: talents=%lu consumables=%lu marketing=%lu\n",
    static_cast<unsigned long>(resourceCatalogPreview.talentCount),
    static_cast<unsigned long>(resourceCatalogPreview.consumableCount),
    static_cast<unsigned long>(resourceCatalogPreview.marketingItemCount));
  if (!resourceCatalogPreview.talentSamples.empty())
  {
    fprintf(stdout, "Resource talent samples: %s\n", JoinPreviewSamples(resourceCatalogPreview.talentSamples).c_str());
  }
  fprintf(stdout, "Input initialized: %s\n", inputState.initialized ? "yes" : "no");
  fprintf(stdout, "Input config: %s\n", inputState.inputConfigLoaded ? "loaded" : "missing");
  fprintf(stdout, "Input binds: %lu across %lu contexts\n",
    static_cast<unsigned long>(inputState.bindStringCount),
    static_cast<unsigned long>(inputState.bindContextCount));
  NHPTimer::STime start = 0;
  NHPTimer::GetTime(start);
  double lastDemoCycleTime = 0.0;

  while (!NMainFrame::IsExit())
  {
    NMainFrame::PumpMessages();
    UpdateInputState(&inputState);
    const size_t previousArtworkChangeCount = artworkState.changeCount;
    const size_t previousSelectedIndex = mapBrowserState.selectedIndex;
    const size_t previousLoadingUiChangeCount = loadingUiState.changeCount;
    UpdateArtworkSelectionState(inputState, &artworkState);
    UpdateMapBrowserState(inputState, mapCatalog, &mapBrowserState);
    UpdateLoadingUiState(inputState, loadingUiPreview, &loadingRuntimeDriver, &loadingUiState);
    bool localMatchChanged = UpdateLocalMatchPreviewState(
      inputState,
      heroCatalog,
      mapCatalog,
      mapBrowserState,
      &localMatchPreview
    );

    NHPTimer::STime now = 0;
    NHPTimer::GetTime(now);
    const double elapsedSeconds = NHPTimer::Time2Seconds(now - start);
    bool demoChanged = false;
    if (settings.demoCycleSeconds > 0.0 && elapsedSeconds - lastDemoCycleTime >= settings.demoCycleSeconds)
    {
      lastDemoCycleTime = elapsedSeconds;
      MoveMapSelection(mapCatalog, &mapBrowserState, 1, "demo-cycle");
      StepArtworkMode(&artworkState, 1, "demo-cycle");
      MoveSelectedHero(heroCatalog, &localMatchPreview, 1, "demo-cycle");
      ++localMatchPreview.shuffleOffset;
      localMatchPreview.generationSource = "demo-cycle";
      RegenerateLocalMatchPreview(heroCatalog, mapCatalog, mapBrowserState, &localMatchPreview, "demo-cycle");
      localMatchChanged = true;
      demoChanged = true;
    }

    if (previousSelectedIndex != mapBrowserState.selectedIndex)
    {
      ProbeSelectedMapPreview(environment, mapCatalog, mapBrowserState, &selectedMapPreview);
      if (!localMatchChanged)
      {
        RegenerateLocalMatchPreview(heroCatalog, mapCatalog, mapBrowserState, &localMatchPreview, "map-change");
      }
    }

    if (previousSelectedIndex != mapBrowserState.selectedIndex || localMatchChanged || demoChanged)
    {
      ProbeSelectedHeroDbPreview(environment, sessionRootPreview, heroCatalog, localMatchPreview, &selectedHeroPreview);
      ProbeEngineMapStartPreview(
        sessionPreview,
        heroCatalog,
        selectedMapPreview,
        mapCatalog,
        mapBrowserState,
        localMatchPreview,
        contentProbe.locale,
        &engineMapStartPreview
      );
      ProbeLoadingHeroesRuntimePreview(
        sessionPreview,
        selectedMapPreview,
        engineMapStartPreview,
        &loadingHeroesRuntimePreview
      );
    }

    if (previousSelectedIndex != mapBrowserState.selectedIndex ||
        localMatchChanged ||
        artworkState.changeCount != previousArtworkChangeCount ||
        loadingUiState.changeCount != previousLoadingUiChangeCount ||
        demoChanged)
    {
      LinuxLoadingArtwork refreshedArtwork;
      std::string refreshedArtworkSource;
      if (BuildSelectedMapDisplayArtwork(
            environment,
            settings,
            loadingArtwork,
            loadingUiPreview,
            loadingUiState,
            selectedMapPreview,
            artworkState,
            heroCatalog,
            localMatchPreview,
            selectedHeroPreview,
            engineMapStartPreview,
            &refreshedArtwork,
            &refreshedArtworkSource))
      {
        UploadArtworkPixmap(&overlay, refreshedArtwork);
        displayArtworkSource = refreshedArtworkSource;
      }
    }

    DrawWindowOverlay(
      &overlay,
      environment,
      settings,
      launchPreview,
      sessionPreview,
      configPreview,
      contentProbe,
      loadingPreview,
      loadingUiPreview,
      loadingRuntimeDriver,
      loadingHeroesRuntimePreview,
      loadingUiState,
      mapCatalog,
      mapBrowserState,
      selectedMapPreview,
      artworkState,
      heroCatalog,
      localMatchPreview,
      selectedHeroPreview,
      engineMapStartPreview,
      rootFileSystemPreview,
      uiRootPreview,
      sessionRootPreview,
      soundRootPreview,
      resourceCatalogPreview,
      inputState,
      elapsedSeconds
    );

    if (settings.runSeconds > 0.0)
    {
      if (elapsedSeconds >= settings.runSeconds)
      {
        break;
      }
    }

    usleep(16 * 1000);
  }

  AppendRuntimeInputLog(
    environment,
    inputState,
    launchPreview,
    sessionPreview,
    mapCatalog,
    mapBrowserState,
    selectedMapPreview,
    artworkState,
    heroCatalog,
    localMatchPreview,
    engineMapStartPreview
  );
  ShutdownWindowOverlay(&overlay);
  Input::BindsManager::Instance()->SetBinds(0);
  NDb::SoundRoot::InitRoot(0);
  NDb::SessionRoot::InitRoot(0);
  NDb::SetResourceCache(0);
  RootFileSystem::ClearFileSystems();
  NMainFrame::ShutdownApplication();
  fprintf(stdout, "Prime World Linux client shell finished.\n");
  return 0;
}

#else

#include "stdafx.h"

#pragma warning (disable : 4996)
#include "System/StrProc.h"
#include "System/SyncProcessorState.h"
#include "System/HPTimer.h"
#include "System/MainFrame.h"
#include "System/ProfileManager.h"
#include "Client/MainLoop.h"
#include "Client/MainTimer.h"
#include "Client/VSyncController.h"
#include "Client/ScreenCommands.h"
#include "System/MainFrame.h"
#include "System/FileSystem/FilePath.h"
#include "System/StackWalk.h"
#include "System/FileStreamDumper.h"
#include "System/AssertDumper.h"
#include "System/EventDumper.h"
#include "System/DebugTraceDumper.h"
#include "System/EditBoxDumper.h"
#include "System/CrashRptWrapper.h"
#include "system/BSUtil.h" //TODO: Remove this header (NUM_TASK)
#include "system/expreport.h"
#include "System/meminfo.h"
#include "Render/renderer.h"
#include "System/TextFileDumper.h"
#include "System/ConfigFiles.h"
#include "System/AppInstancesLimit.h"
#include "System/FileSystem/FileExtensionStatisticsMonitor.h"
#include "System/FileSystem/FileActivitySimpleMonitor.h"
#include "System/CmdLineLite.h"
#include "System/InlineProfiler.h"
#include "System/InlineProfiler3/Profiler3UI.h"
#include "System/InlineProfiler3/InlineProfiler3Control.h"
#include "System/PersistEvents.h"
#include "System/FileSystem/TinyFileWriteStream.h"

#include "Core/CoreFSM.h"
#include "Core/Transceiver.h"

#include "System/FileSystem/WinFileSystem.h"
#include "System/FileSystem/PileFileSystem.h"
#include "System/Stream.h"
#include "System/SyncProcessorState.h"
#include "Render/smartrenderer.h"
#include "Render/NullRenderSignal.h"

#include "Render/ParticleFX.h"

#include "PF_GameLogic/PFRenderInterface.h"

#include "UI/Root.h"
#include "UI/DebugVarsRender.h"
#include "UI/FrameTimeRender.h"
#include "UI/Resolution.h"
#include "UI/Scripts.h"

#include "Sound/SoundScene.h"
#include "Sound/EventScene.h"
#include "PF_GameLogic/DBSound.h"

#include "System/LogFileName.h"
#include "ApplicationResources.h"
#include "Version.h"
#include "commctrl.h"

#include "PF_GameLogic/PFGameLogicLog.h"

#include "PF_Core/ForceLink.h"
#include "PF_Minigames/ForceLink.h"

#include "TypesHash.h"
#include "PF_TypesHash.h"

#include "GameContext.h"
#include "LocalGameContext.h"
#include "System/Events.h"
#include "System/EventFileLogger.h"
#include "System/IniFiles.h"
#include "Tools/Censor/CensorDll.h"
#include "NivalInput/Binds.h"
#include "NivalInput/SystemInputEvents.h"

#include "CpuTopology.h"
#include "../System/SimpleSignalST.h"
#include "../System/HPTimer.h"
#include "../Client/MainTimer.h"

#include <Tlhelp32.h>
#include "System/StdOutDumper.h"
#include "LobbyConnection.h"

#include "PF_GameLogic/StringExecutor.h"
#include "PF_GameLogic/AdventureScreen.h"
#include "GameStatistics.h"

#include "PF_GameLogic/SocialConnection.h"
#include "PF_GameLogic/SocialBootstrap.h"
#include "PF_GameLogic/TutorialSplash.h"
#include "PF_GameLogic/GuildEmblem.h"
#include "PF_GameLogic/DBSessionMessages.h"

#include "steam/steam_api.h"
#include "steam/isteamuserstats.h"
#include "steam/isteamremotestorage.h"
#include "steam/isteammatchmaking.h"
#include "steam/steam_gameserver.h"

#include "RegistryToolbox.h"
#include "../PF_GameLogic/WebLauncher.h"
#include "../PW_Game/server_ip.h"
#include "../Shared/WebRequests.h"

static int    g_VideoFPS = 10;
static float  g_RecordingTime = 10.0f;
static bool   g_DebugDumpInfo = false;
static bool   g_NullRenderNoLogBox = false;
static bool   g_boostPriority = false;
static int    g_preferredProcessor = 0;
static int    g_inactiveSleep = -1;
static bool   s_localGame = false;
static bool   s_skipFrames = false;
static int    s_skipFramesBarrier = 25;
static int    s_NullRender = false;
static bool   s_bSteamInited = false;
static bool   s_bRegisterReplayExtention = true;


REGISTER_VAR( "debug_dump_info", g_DebugDumpInfo, STORAGE_GLOBAL );
REGISTER_VAR( "boost_main_thread_priority", g_boostPriority, STORAGE_NONE );
REGISTER_VAR( "boost_mtp_processor_number", g_preferredProcessor, STORAGE_NONE );
REGISTER_VAR( "inactive_sleep", g_inactiveSleep, STORAGE_NONE );
REGISTER_DEV_VAR( "video_FPS", g_VideoFPS, STORAGE_NONE);
REGISTER_DEV_VAR( "video_recording_time", g_RecordingTime, STORAGE_NONE);
REGISTER_DEV_VAR( "nullrender", s_NullRender, STORAGE_NONE);
REGISTER_DEV_VAR( "nullrender_no_log_box", g_NullRenderNoLogBox, STORAGE_NONE);
//REGISTER_DEV_VAR( "local_game", s_localGame, STORAGE_NONE);
REGISTER_DEV_VAR( "skipFramesEnable", s_skipFrames, STORAGE_NONE);
REGISTER_DEV_VAR( "skipFramesBarrier", s_skipFramesBarrier, STORAGE_NONE);

static int g_deleteLogFilesAfterDays = 30;
REGISTER_VAR( "delete_log_files_after_days", g_deleteLogFilesAfterDays, STORAGE_GLOBAL );

REGISTER_VAR( "register_replay_extention", s_bRegisterReplayExtention, STORAGE_USER );
static string g_CensorFolder = "..\\Censor";
REGISTER_VAR( "censor_folder", g_CensorFolder, STORAGE_GLOBAL );

static string g_language = "ru-RU";
REGISTER_VAR( "language", g_language, STORAGE_PLAYER);

static NDebug::PerformanceDebugVar mainPerf( "01_Total", "MainPerf", 10000, 150.0, false, 80 );
static NDebug::PerformanceDebugVar mainPerf_ContextStep( "03_Context", "MainPerf", 10000, 30.0, true, 80 );
static NDebug::PerformanceDebugVar mainPerf_Step( "04_Step", "MainPerf", 10000, 30.0, true, 80 );
static NDebug::PerformanceDebugVar mainPerf_Present( "05_Present", "MainPerf", 10000, 20.0, false, 80 );
static NDebug::PerformanceDebugVar mainPerf_Render( "06_Render", "MainPerf", 10000, 30.0, true, 80 );

static NDebug::DebugVar<int> heapAllocs( "HeapAllocs", "", true );
static NDebug::DebugVar<int> heapAllocsSize( "HeapAllocsSize", "", true );
static NDebug::DebugVar<int> unfreeHeapAllocs( "UnfreeHeapAllocs", "", true );

static NDebug::DebugVar<int> virtualAllocs( "VirtualAllocs", "", true );
static NDebug::DebugVar<int> virtualAllocsSize( "VirtualAllocsSize", "", true );
static NDebug::DebugVar<int> unfreeVirtualAllocs( "UnfreeVirtualAllocs", "", true );

static NDebug::DebugVar<int> totalAllocsSize( "TotalAllocsSize", "", true );

std::string g_protocolToken;
extern bool g_localGameRun;

//CRAP
extern "C" INTERMODULE_EXPORT void TooSmartLinker();


namespace
{
void ShowLocalizedErrorMB(LPCWSTR pszName, LPCWSTR pszDefault)
{
  // get the current language from the 'lang.cfg' file
  nstl::wstring lang = L"ru-RU";
  if (!NGlobal::GetVar("language").GetString().empty())
  {
    lang = NGlobal::GetVar("language").GetString() + L"/";
  }
  nstl::string filepath(NFile::Combine(NFile::GetBaseDir(), "Data/Localization/" + NStr::ToMBCS(lang)));
  
  wstring message = NIniFiles::GetINIString( NStr::ToUnicode(filepath).c_str(), L"pw_game_lang.ini", L"ErrMsgs", pszName );
  wstring title = NIniFiles::GetINIString( NStr::ToUnicode(filepath).c_str(), L"pw_game_lang.ini", L"ErrMsgs", L"ErrorTile" );
  if (message.empty()) message = pszDefault ? pszDefault : L"Unexpected error.";
  if (title.empty()) title = L"Error";
  MessageBoxW( 0, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST );
}

} // namespace


static NDebug::PerformanceDebugVar* FindPerfVar( const wchar_t* screen, const wchar_t* name )
{
  NDebug::IDebugVar* pFrameTime = NDebug::FindDebugVar( name );
  if( !pFrameTime )
    return 0;

  return static_cast<NDebug::PerformanceDebugVar*>( pFrameTime );
}



void DumpLoadedModules() 
{ 
  CObj<FileWriteStream> pFile;
  string logFileName = NDebug::GenerateDebugFileName( "modules", "dmp" );
  pFile = new FileWriteStream( logFileName, FILEACCESS_WRITE, FILEOPEN_OPEN_ALWAYS );

  DWORD dwPID = GetCurrentProcessId();
  HANDLE hModuleSnap = INVALID_HANDLE_VALUE; 
  MODULEENTRY32 me32; 

  //  Take a snapshot of all modules in the specified process. 
  hModuleSnap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, dwPID ); 
  if( hModuleSnap == INVALID_HANDLE_VALUE ) 
  { 
    //printError( "CreateToolhelp32Snapshot (of modules)" ); 
    return; 
  } 

  //  Set the size of the structure before using it. 
  me32.dwSize = sizeof( MODULEENTRY32 ); 

  //  Retrieve information about the first module, 
  //  and exit if unsuccessful 
  if( !Module32First( hModuleSnap, &me32 ) ) 
  { 
    //printError( "Module32First" );  // Show cause of failure 
    CloseHandle( hModuleSnap );     // Must clean up the snapshot object! 
    return; 
  } 

  //  Now walk the module list of the process, 
  //  and display information about each module 
  do 
  { 
    /*DebugTrace( NStr::StrFmt( "\n\n     MODULE NAME:     %s",             me32.szModule ) ); 
    DebugTrace( NStr::StrFmt( "\n     executable     = %s",             me32.szExePath ) ); 
    DebugTrace( NStr::StrFmt( "\n     process ID     = 0x%08X",         me32.th32ProcessID ) ); 
    DebugTrace( NStr::StrFmt( "\n     ref count (g)  =     0x%04X",     me32.GlblcntUsage ) ); 
    DebugTrace( NStr::StrFmt( "\n     ref count (p)  =     0x%04X",     me32.ProccntUsage ) ); 
    DebugTrace( NStr::StrFmt( "\n     base address   = 0x%08X", (DWORD) me32.modBaseAddr ) ); 
    DebugTrace( NStr::StrFmt( "\n     base size      = %d",             me32.modBaseSize ) ); */

    const char* line = NStr::StrFmt( "%08X,%08X,%s\x0D\x0A", (DWORD)me32.modBaseAddr, me32.modBaseSize, me32.szExePath );
    pFile->WriteString( line );

  } while( Module32Next( hModuleSnap, &me32 ) ); 

  //  Do not forget to clean up the snapshot object. 
  CloseHandle( hModuleSnap ); 
}



//#define DUMP_ALLOCS_FOR_MEMORY_MONITOR // uncomment this define to dump all allocations for memory monitor application

bool CheckHardwareCompatibility()
{
  bool supportSM30 = Render::GetRenderer()->GetCaps().bSupportSM30;
  bool hasEnoughMemory = true;

  MEMORYSTATUSEX globMemStatus;
  ZeroMemory( &globMemStatus, sizeof( globMemStatus ) );
  globMemStatus.dwLength = sizeof( globMemStatus );

  NDb::Ptr<NDb::SessionMessages> sessionMessages = NDb::SessionRoot::GetRoot()->sessionMessages;
  NI_DATA_ASSERT(sessionMessages, "Session.ROOT.sessionMessages is empty!");

  {
    const NDb::DXErrorMessages* const dxMessages = &sessionMessages->dxErrorMessages;
    Render::SetErrorMessage( 0, dxMessages->title.GetText() );
    Render::SetErrorMessage( D3DERR_INVALIDCALL, dxMessages->errorMessage.GetText() );
    Render::SetErrorMessage( E_OUTOFMEMORY, dxMessages->lowMemoryMessage.GetText() );
    Render::SetErrorMessage( D3DERR_OUTOFVIDEOMEMORY, dxMessages->lowVidMemMessage.GetText() );
  }

  const NDb::ClientHardwareErrorMessages* const heMessages = &sessionMessages->clientHardwareErrorMessages;

  if ( GlobalMemoryStatusEx( &globMemStatus ) )
    hasEnoughMemory = ( ( globMemStatus.ullTotalPhys / 1024ul )  > 900000 ); // ��-�� ������� �����

  if ( !supportSM30 || !hasEnoughMemory )
  {
    nstl::wstring errorMessage = heMessages->errorMessage.GetText();
    if ( !supportSM30 )
    {
      errorMessage += L"\n\n";
      errorMessage += heMessages->shader3compatibilityError.GetText();
    }
    if ( !hasEnoughMemory )
    {
      errorMessage += L"\n\n";
      errorMessage += heMessages->lowMemoryError.GetText();
    }

    MessageBoxW( 0, errorMessage.c_str(), L"Error", MB_OK | MB_ICONWARNING );

    return false;
  }

  return true;
}



struct SPluginSettings
{
  int   width;
  int   height;
  bool  fullscreen;
  const char * sessionLogin;
  const char* serverName;
  const char* uid;
  const char* serverKey;
  const char* serverSecret;

  SPluginSettings() : width( 0 ), height( 0 ), fullscreen( false ), sessionLogin( 0 ), serverName(0), uid(0), serverKey(0), serverSecret(0) {}
};



class AltTabChecker
{
  bool isActive;

public:
  AltTabChecker( bool isActive ) : isActive( isActive ) {}
  void Update()
  {
    const bool appFocused = NMainFrame::IsAppNotMinimized() && NMainFrame::IsAppActive(); //NMainFrame::IsCursorInsideWndClientRect()
    if ( isActive && !appFocused )
    {
      DebugTrace("AltTabChecker: lost focus");
      isActive = appFocused;
    }
    else if ( !isActive && appFocused )
    {
      DebugTrace("AltTabChecker: acquire focus");
      isActive = appFocused;
      NMainFrame::SetActualClipCursorRect();
    }
  }
  bool IsActive() const
  {
    return isActive;
  }
};


class IRenderableScene;

typedef SimpleSignalST<bool, ::IRenderableScene> SceneSkipFrameSignal;

static bool SkipFrameIfNeeded(int _numCommands)
{
  static bool lastFrameSkipped = false;

  if(!s_skipFrames)
    return false;

  static NHPTimer::STime lastTime = 0;

  NHPTimer::STime currTime = NMainLoop::GetHPTime();

  bool skipFrame = !lastFrameSkipped &&
    (_numCommands > s_skipFramesBarrier && NHPTimer::Time2Seconds(currTime - lastTime) < 0.2);
  if(!skipFrame)
    lastTime = currTime;

  SceneSkipFrameSignal::Signal(skipFrame);
  lastFrameSkipped = skipFrame;
  return skipFrame;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <class T, size_t N>
class MovingAverage
{
public:
  MovingAverage()
  {
    curIndex = 0; 

    for( size_t i = 0; i < ARRAY_SIZE(arr); ++i )
      arr[i] = T();
  }

  T NextValue( T val, T maxVal )
  {
    arr[curIndex] = val;

    T totalSum = T();     
    size_t itemsProcessed = 0;
    size_t i = curIndex;

    do
    { 
      totalSum += arr[i];   

      if( i != 0 )
        --i;
      else
        i = ARRAY_SIZE(arr) - 1;

      ++itemsProcessed;
    } 
    while( i != curIndex && totalSum < maxVal );


    if( curIndex != ARRAY_SIZE(arr) - 1 )
      ++curIndex;
    else
      curIndex = 0;

    NI_ASSERT( itemsProcessed <= ARRAY_SIZE(arr), "" );
    return totalSum / itemsProcessed;   
  }


private:
  size_t curIndex;
  T arr[N];
};

//� ��������� ������� 0.2(9) ������� ������
//���������� ����� 2 ����� ������
static float g_maxMovingAvgTime = 0.2999999f;

REGISTER_DEV_VAR( "max_smooth_time", g_maxMovingAvgTime, STORAGE_NONE);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static _invalid_parameter_handler g_oldInvalidParamHandler;

void DebugTraceInvalidParamsHandler(const wchar_t* expression,
                                    const wchar_t* function, 
                                    const wchar_t* file, 
                                    unsigned int line, 
                                    uintptr_t pReserved)
{
  DebugTrace( "Invalid parameter detected in function %s."
    " File: %s Line: %d\n", NStr::ToMBCS(function).c_str(), NStr::ToMBCS(file).c_str(), line );

  DebugTrace( "Expression: %s\n", NStr::ToMBCS(expression).c_str() );

  //��������� ��������� ������. ���������� ������ ������. 
  //����� ���������� ������������ �����������, ������� ������� ���������.
  (*g_oldInvalidParamHandler)( expression, function, file, line, pReserved );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool StartRemoteDebugger()
{
  LPTSTR Command = 
    "%SystemDrive%\\Program Files\\Microsoft Visual Studio 9.0\\Common7\\IDE\\Remote Debugger\\x86\\msvsmon.exe"
    " /noauth /nosecuritywarn /nofirewallwarn /nowowwarn /anyuser /timeout 10000000";
  const LPCTSTR CurrentDirectory = NULL;

  TCHAR ExpCommand[MAX_PATH];

  ExpandEnvironmentStrings( Command, ExpCommand, ARRAY_SIZE(ExpCommand) );

  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory( &si, sizeof(si) );
  si.cb = sizeof(si);
  ZeroMemory( &pi, sizeof(pi) );

  // Start the child process. 
  if( !CreateProcess( NULL, // No module name (use command line). 
    ExpCommand,          // Command line. 
    NULL,             // Process handle not inheritable. 
    NULL,             // Thread handle not inheritable. 
    FALSE,            // Set handle inheritance to FALSE. 
    0,                // No creation flags. 
    NULL,             // Use parent's environment block. 
    CurrentDirectory, // Starting directory. 
    &si,              // Pointer to STARTUPINFO structure.
    &pi )             // Pointer to PROCESS_INFORMATION structure.
    ) 
  {
    systemLog( NLogg::LEVEL_ASSERT ) <<
      "Error on executing command '" << ExpCommand <<
      "' with starting dir '" << (CurrentDirectory == NULL ? "NULL" : CurrentDirectory) << 
      "': " <<  GetLastError(); 

    return false;    
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void RaiseMainThreadPriority()
{ // given our process is allowed to run at multiple cores,
  // pin the main thread to preferred one and raise thread priority
  const DWORD numberOfAvailableCores = CpuTopology().NumberOfProcessCores();
  if(g_boostPriority && numberOfAvailableCores > 1)
  {
    const DWORD desiredCore = g_preferredProcessor >= 0 ? g_preferredProcessor
      : numberOfAvailableCores + g_preferredProcessor;
    const DWORD desiredCoreMask = CpuTopology().CoreAffinityMask(desiredCore);
    DWORD idx;
    if( _BitScanForward(&idx, desiredCoreMask) )
    {
      SetThreadIdealProcessor(GetCurrentThread(), idx);
      //SetThreadAffinityMask(GetCurrentThread(), 1 << idx);
      int oldPriority = GetThreadPriority( GetCurrentThread() );
      if(THREAD_PRIORITY_NORMAL == oldPriority)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static StrongMT<Game::IGameContext> context;
static bool timerUpdated = false;

void StepContext()
{
  if(!timerUpdated)
    NMainLoop::UpdateTime();

  if( !NMainFrame::IsExit() )
  {
    NI_PROFILE_BLOCK( "Context" );
    mainPerf_ContextStep.Start();
    context->Poll( NMainLoop::GetTimeDelta() );
    mainPerf_ContextStep.Stop();
  }
  timerUpdated = false;
}

struct GameFileReadContext : public FileReadCallbackContext
{
//  OBJECT_BASIC_METHODS( GameFileReadContext );
public:
  GameFileReadContext( ICastle* castleLink ) : FileReadCallbackContext(), pCastleLink(castleLink) {}
  Weak<ICastle> pCastleLink;
};


void OnPileFileReadError(FileReadResultCode code, FileReadCallbackContext* pContext)
{
  switch (code)
  {
  case FR_READ_ERROR:
  case FR_CRC_FAIL:
    {
      CObj<TinyFileWriteStream> repairFile = new TinyFileWriteStream( NFile::GetBaseDir() + "..\\.force_repair" );
      repairFile->Flush();

      HWND hWnd = (HWND)NMainFrame::GetWnd();
      if( hWnd != 0 )
        ShowWindow(hWnd, SW_MINIMIZE);

      ShowLocalizedErrorMB( L"GameDataCorrupted",
        L"Game Data has been corrupted.\nTo fix the problem please restart the launcher." );

      GameFileReadContext* pGameContext = dynamic_cast<GameFileReadContext*>(pContext);
      if (pGameContext && pGameContext->pCastleLink)
        pGameContext->pCastleLink->QuitGame();

      if ( persistentEvents::GetSingleton() )
        persistentEvents::GetSingleton()->Close();

      NSoundScene::TryTurnOffSound();

      //inputSystemMesages = 0;
      //input = 0;

      context = 0;

      NCore::ReleaseGlobalGameFSM();

      UI::Release();
      UI::ReleaseUIScript();

      //renderingInterface.Stop();

      NDb::SessionRoot::InitRoot( 0 );
      debugDisplay::Cleanup();
      profiler3ui::Shutdown();
#ifndef _SHIPPING
      DumpLoadedModules();
#endif //_SHIPPING

      // Shutdown the SteamAPI
      if (s_bSteamInited)
        SteamAPI_Shutdown();
      // �� �� ����� ������������� �� ����� ������, �� � ��� ����� �� ���������� �������������
      // � ��������� �������������������� �� �� ������
      // ������ ������ � exit(0) �� ��������������� �������� ���-�� �� �����
      // ������� ���������� Message Box ��� ������������� ������, � ����� ������ TerminateProcess
      // NUM_TASK
      TerminateProcess( GetCurrentProcess(), 0 );
      //exit(0);
      break;
    }
    
  }
}

struct MainVars
{
  bool initSound;
  bool initInput;
  bool initContext;
  bool initGameFSM;
  bool initUI;
  bool initRender;
  bool initDB;
  bool initDisplay;
  bool initProfiler;

  bool useCrashRptHandler;

  CObj<Input::Binds> input;
  CObj<Input::SystemEvents> inputSystemMesages;

  Render::DeviceLostWrapper<PF_Render::Interface>* renderingInterface;
  StrongMT<NLogg::EditBoxDumper> logBox;

  MainVars()
  {
    initSound = false; 
    initInput = false; 
    initContext = false; 
    initGameFSM = false; 
    initUI = false; 
    initRender = false; 
    initDB = false; 
    initDisplay = false; 
    initProfiler = false;
    useCrashRptHandler = false;

    input = 0;
    inputSystemMesages = 0;
    renderingInterface = 0;
  }

  void DeInit()
  {
    if ( initSound )
	    NSoundScene::TryTurnOffSound();

    if ( initInput )
    {
      inputSystemMesages = 0;
      input = 0;
    }

    if ( initContext )
      context = 0;

    if ( initGameFSM )
      NCore::ReleaseGlobalGameFSM();

    if ( initUI )
    {
      UI::Release();
      UI::ReleaseUIScript();
    }

    if ( initRender && renderingInterface )
    {
      renderingInterface->Stop();
      delete renderingInterface;
    }

    if ( initDB )
      NDb::SessionRoot::InitRoot( 0 );

    if ( initDisplay )
      debugDisplay::Cleanup();

    if ( initProfiler )
      profiler3ui::Shutdown();

    if ( logBox )
    {
      GetSystemLog().RemoveDumper( logBox );
      logBox = 0;
    }

    GetSystemLog().ClearAllDumpers();

    if ( useCrashRptHandler )
      CrashRptWrapper::UninstallFromProcess();

    // Shutdown the SteamAPI
    if (s_bSteamInited)
      SteamAPI_Shutdown();
  }
};

void LoadCensorDict( string filename, bool isWhiteList = false )
{
  string resPath = NFile::Combine(NFile::GetBaseDir(), g_CensorFolder);
  resPath = NFile::Combine(resPath, "Dicts");
  CensorFilter::LoadDictionary( NStr::ToUnicode(NFile::Combine(resPath, filename)).c_str(), isWhiteList );
}

void InitCensorDicts()
{
  LoadCensorDict("custom.txt");
  LoadCensorDict("custom_whitelist.txt", true);
}

extern string g_devLogin;
extern string g_sessionToken;
extern string g_playerToken;

extern string g_sessionName;
extern WebLauncherPostRequest::RegisterSessionRequest g_sessionStatus;
extern WebLauncherPostRequest::WebLoginResponse g_webLoginResponse;
extern int g_playerTeamId;
extern int g_playerHeroId;
extern int g_playerPartyId;

std::string GetDirectoryFromPath(const std::string& fullPath) {
    std::size_t found = fullPath.find_last_of("/\\");
    if (found != std::string::npos) {
        return fullPath.substr(0, found);
    }
    return "";
}

#include <windows.h>
#include <TlHelp32.h>

int NumProcessRunning(const char* processName)
{
int count = 0;

    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE)
    {
        CloseHandle(hProcessSnap);
        return 0;
    }

    if (Process32First(hProcessSnap, &pe32))
    {
        do
        {
            if (_stricmp(pe32.szExeFile, processName) == 0)
            {
                count++;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);
    return count;
}

static void RunLinuxLauncher() {
  char curDirBuff[260];
  GetCurrentDirectoryA(260,curDirBuff);

  STARTUPINFO startupInfo;
  PROCESS_INFORMATION processInfo;
  ZeroMemory(&startupInfo, sizeof(startupInfo));
  startupInfo.cb = sizeof(startupInfo);
  ZeroMemory(&processInfo, sizeof(processInfo));

  BOOL procRun = CreateProcessA("../../Launcher/PWClassic", "", NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);

  systemLog( NLogg::LEVEL_MESSAGE ) << "Linux proc run: \"" << procRun << "\"" << endl;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int __stdcall PseudoWinMain( HINSTANCE hInstance, HWND hWnd, LPTSTR lpCmdLine, SPluginSettings * pluginSett )
{
	 char buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);

    // Extract only the executable name from the full path
    std::string fullPath = buffer;
    size_t found = fullPath.find_last_of("\\");
    std::string executableName = fullPath.substr(found + 1);

  MainVars mainVars;

  g_oldInvalidParamHandler = _set_invalid_parameter_handler( DebugTraceInvalidParamsHandler );

  Compatibility::Init();

  CmdLineLite::Instance().Init( lpCmdLine );

  bool isSpectator = CmdLineLite::Instance().IsKeyDefined( "spectator" );
  bool isTutorial = CmdLineLite::Instance().IsKeyDefined( "--launchTutorial" );

  if (CmdLineLite::Instance().IsKeyDefined( EXIT_CODE_QUIT_CASTLE ))
    NMainFrame::SetExitCode( EXIT_CODE_QUIT_CASTLE );

  // Check if we deal with a replay
  bool isReplay = false;
  string replayFileName;
  if ( CmdLineLite::Instance().ArgsCount() > 1 )
  {
    replayFileName = CmdLineLite::Instance().Argument(1);
    if(!replayFileName.empty() && replayFileName.find('.') != string::npos && NFile::DoesFileExist(replayFileName))
    {
      isSpectator = true;
      isReplay = true;

      if (!IsDebuggerPresent())
        NFile::SetModuleCurrentDir();
    }
  }

#ifndef _SHIPPING
  if( CmdLineLite::Instance().IsKeyDefined( "RemoteDebugger" ) )
  {
    if( StartRemoteDebugger() )
      MessageBox( 0, "Remote Debugger has been started.\nTry to connect to the computer and press OK", "Waiting for connection", MB_OK | MB_ICONINFORMATION ); 
  }
#endif

  SyncProcessorState();

  threading::SetDebugThreadName( "Main" );
  
  InitCommonControls();
  NFile::InitBaseDir();
  NProfile::Init( PRODUCT_TITLE );
  
  AppInstancesLimit instancesLimit( PRODUCT_TITLE_SHORT );
#ifndef _SHIP_FALSE
  NDebug::SetProductNameAndVersion( NFile::GetBinDir(), PRODUCT_TITLE_SHORT, VERSION_LINE, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_REVISION );
#else //_SHIP_FALSE
  NDebug::SetProductNameAndVersion( NProfile::GetFullFolderPath(NProfile::FOLDER_PLAYER), PRODUCT_TITLE_SHORT, VERSION_LINE, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_REVISION );
#endif //_SHIP_FALSE

#ifdef _SHIPPING
  if ( !instancesLimit.Lock( 1 ) )
  {
    mainVars.DeInit();
    return 0;
  }
#endif

  string logsPath = GetFullFolderPath( NProfile::FOLDER_LOGS );
  if (!NFile::DoesFolderExist(logsPath))
    NFile::CreatePath(logsPath);
  NDebug::OverrideDebugLogDir( logsPath.c_str() );

  persistentEvents::CreateSingleton( NDebug::GetDebugLogDir() );
  persistentEvents::GetSingleton()->CheckUnfinishedSessions();
  persistentEvents::AutoClose persistentEventsAutoClose;

  // ������� ���������� ��������� ������ �� ��������� "��� ����" (������ severity)
  GetSystemLog().SetHeaderFormat( NLogg::EHeaderFormat::Default ); 
  GetGameLogicLog().SetHeaderFormat( 0 );

  StrongMT<NLogg::BasicTextFileDumper> textLogDumper( NLogg::BasicTextFileDumper::New( &GetSystemLog(), "output") );

#ifndef _SHIPPING
  StrongMT<NLogg::CFileStreamDumper> gameLogicStreamDumper = new NLogg::CFileStreamDumper( &GetGameLogicLog() );
  StrongMT<NLogg::CAssertDumper> assertDumper = new NLogg::CAssertDumper( &GetSystemLog() );
#endif //_SHIPPING

  StrongMT<NLogg::CDebugTraceDumper> debugTraceDumper = new NLogg::CDebugTraceDumper( &GetSystemLog() );
  //StrongMT<NLogg::CEventDumper> eventDumper = new NLogg::CEventDumper( &GetSystemLog() ); // it takes 80MB at game loading in deep of StackWalk64

  StrongMT<NLogg::CDumper> pAuxDumper;

  if( Compatibility::IsRunnedUnderWine() )
    pAuxDumper = new NLogg::CStdOutDumper( &GetSystemLog(), stderr, false );

  if ( isSpectator )
  {
    if ( !SteamAPI_Init() )
      DebugTrace( "SteamAPI_Init() failed\n" );
    else
      s_bSteamInited = true;
  }
/*
  mainVars.useCrashRptHandler = !Compatibility::IsRunnedUnderWine() &&
    !CmdLineLite::Instance().IsKeyDefined( "-crashrpt_disable" );
*/
  if ( mainVars.useCrashRptHandler )
  {
    const char * privacyPolicy = "http://updates.playpw.com/eula-ru.rtf";
#ifdef _SHIP_FALSE
    const char * uploadUrl = CmdLineLite::Instance().GetStringKey( "-crashrpt_url", "http://SITE/upload/" );
#else //_SHIP_FALSE
    const char * uploadUrl = CmdLineLite::Instance().GetStringKey( "-crashrpt_url", "http://SITE/upload/" );
#endif //_SHIP_FALSE
    CrashRptWrapper::InstallForProcess( uploadUrl, !CmdLineLite::Instance().IsKeyDefined( "-crashrpt_nomultipart" ), false, 0, privacyPolicy );
  }
  else
    NBSU::InitUnhandledExceptionHandler();

  SetMallocThreadMask( GetCurrentThreadId() );

#ifdef DUMP_ALLOCS_FOR_MEMORY_MONITOR
  //NDebug::EnableAllocFreeEvents( true );
#endif

  if ( mainVars.useCrashRptHandler )
    CrashRptWrapper::AddFileToReport( textLogDumper->FileName().c_str(), "Application Log" );

  if ( mainVars.useCrashRptHandler )
    CrashRptWrapper::AddFileToReport( NProfile::GetFullFilePath( "user.cfg", NProfile::FOLDER_USER ).c_str(), "Game Settings File" );

  profiler3::GetControl()->Setup( profiler3::SSettings() );

  NI_PROFILE_THREAD;

  profiler3ui::Init();
  mainVars.initProfiler = true;

  if( Compatibility::IsRunnedUnderWine() )
    DebugTrace( "System runned under Wine emulator" );
  else
    DebugTrace( "System runned under native Windows" );  

#ifndef _SHIP_FALSE
  DebugTrace( "Development version" );
#endif // _SHIP_FALSE

  if ( isTutorial )
  {
    MessageTrace("Starting in tutorial mode");
  }
  else if ( isReplay )
  {
    MessageTrace("Starting in replay mode");
  }
  else if ( isSpectator )
  {
    MessageTrace("Starting in spectator mode");
  }
  else
  {
    MessageTrace("Starting in normal mode");
  }

  NGlobal::ExecuteConfig( "default.cfg", NProfile::FOLDER_GLOBAL );
  NGlobal::ExecuteConfig( "user.cfg", NProfile::FOLDER_USER );
  NGlobal::ExecuteConfig( "lang.cfg", NProfile::FOLDER_PLAYER );

  if (!NGlobal::ExecuteConfig("social.cfg", NProfile::FOLDER_GLOBAL))
  {
    mainVars.DeInit();
    return 0xDEAD;
  }

#ifndef _SHIPPING
  if (CmdLineLite::Instance().IsKeyDefined("--splash"))
  {
    const NGameX::TutorialSplash splash;

    ::Sleep(5000);

    return 0xBEEF;
  }
#endif // _SHIPPING

  hWnd = (HWND)CmdLineLite::Instance().GetIntKey( "parentWindow" );
  SPluginSettings parentSettings;
  Strong<NGameX::ISocialConnection> socialServer;
  Strong<NGameX::GuildEmblem> guildEmblem = new NGameX::GuildEmblem;

  bool startFromCastle = false;

  NGameX::SocialBootstrap::LoginParams socialLoginParams;
  NGameX::SocialBootstrap::LaunchData socialLaunchData;

  typedef AutoPtr<NGameX::SocialBootstrap::Session> SocialBootstrapSessionPtr;
  typedef AutoPtr<NGameX::SocialBootstrap::MatchMaking> SocialBootstrapMatchMakingPtr;

  SocialBootstrapSessionPtr socialSession;

  if (isTutorial)
  {
    //--snid pw
    //--snuid 3602865
    //--server login0
    //--sntoken 10b9c1b6fd2bab21de2aa1daaf3d189c
    //--secret 085fa17457d1fba12e4eb596b83ba99a
    //--needqueue
    //ver 0.12.357.33707
    //--pwdserver http://ru.pwcastle.SITE.com:88/
    socialLoginParams.queueLogin = CmdLineLite::Instance().IsKeyDefined("--needqueue");
    socialLoginParams.serverName = CmdLineLite::Instance().GetStringKey("--server");
    socialLoginParams.serverSecret = CmdLineLite::Instance().GetStringKey("--secret");
    socialLoginParams.snid = CmdLineLite::Instance().GetStringKey("--snid");
    socialLoginParams.snuid = CmdLineLite::Instance().GetStringKey("--snuid");
    socialLoginParams.sntoken = CmdLineLite::Instance().GetStringKey("--sntoken");
    socialLoginParams.sntoken2 = CmdLineLite::Instance().GetStringKey("--sntoken2");
    socialLoginParams.version = CmdLineLite::Instance().GetStringKey("ver");

    const NGameX::TutorialSplash splash;

    socialSession = SocialBootstrapSessionPtr(new NGameX::SocialBootstrap::Session(socialLoginParams, false));

    if (!socialSession->Login())
    {
      ErrorTrace("Social login failed");

      ShowLocalizedErrorMB(L"SocialConnectionFailed", L"Failed to connect to server.");
      return 0xDEAD;
    }

    NGameX::SocialBootstrap::MatchMaking smm(*socialSession);

    if (!smm.Make())
    {
      ErrorTrace("Social matchmaking failed");

      ShowLocalizedErrorMB(L"SocialMatchMakingFailed", L"Failed to start game session.");
      return 0xDEAD;
    }

    socialLaunchData.Update(*socialSession);
    socialLaunchData.Update(smm);

    pluginSett = &parentSettings;

    pluginSett->width = (CmdLineLite::Instance().GetIntKey("parentWidth"));
    pluginSett->height = (CmdLineLite::Instance().GetIntKey("parentHeight"));
    pluginSett->fullscreen = (CmdLineLite::Instance().GetIntKey("parentFullscreen") != 0);
    pluginSett->sessionLogin = socialLaunchData.sessionId.c_str();

    pluginSett->uid = socialLaunchData.uid.c_str();
    pluginSett->serverKey = socialLaunchData.serverKey.c_str();
    pluginSett->serverName = socialLaunchData.serverLocation.c_str();
    pluginSett->serverSecret = socialLaunchData.serverSecret.c_str();

    socialServer = new NGameX::SocialConnection(*socialSession);
  }
  else if ( CmdLineLite::Instance().IsKeyDefined( "parentWidth" ) )
  {
    pluginSett = &parentSettings;

    pluginSett->width = CmdLineLite::Instance().GetIntKey( "parentWidth" );
    pluginSett->height = CmdLineLite::Instance().GetIntKey( "parentHeight" );
    pluginSett->fullscreen = ( CmdLineLite::Instance().GetIntKey( "parentFullscreen" ) != 0 );
    pluginSett->sessionLogin = CmdLineLite::Instance().GetStringKey( "parentSessionLogin" );

    pluginSett->uid = CmdLineLite::Instance().GetStringKey("uid");
    pluginSett->serverKey = CmdLineLite::Instance().GetStringKey("serverKey");
    pluginSett->serverName = CmdLineLite::Instance().GetStringKey("serverName");
    pluginSett->serverSecret = CmdLineLite::Instance().GetStringKey("serverSecret");

    socialServer = new NGameX::SocialConnection( pluginSett->serverName, pluginSett->uid, pluginSett->serverKey, pluginSett->serverSecret );

    startFromCastle = true;
  }
  else
  {
    socialServer = new NGameX::DummySocialConnection();
	 startFromCastle = true;
  }

  const char * sessLogin = pluginSett ? pluginSett->sessionLogin : 0;
  if ( !sessLogin )
    sessLogin = CmdLineLite::Instance().GetStringKey( "-session_login", 0 );

//#ifdef _SHIPPING
//  const bool launch = isReplay || ((isTutorial || startFromCastle) && !!sessLogin);
//
//  if (!launch)
//  {
//    ShowLocalizedErrorMB( L"StartViaLauncher", L"Please start the game via the launcher." );
//
//    mainVars.DeInit();
//    return 0xDEAD;
//  }
//#endif

  if ( pluginSett )
	{
//    NGlobal::SetVar( "gfx_resolution", NStr::StrFmt( "%dx%d", pluginSett->width, pluginSett->height ), STORAGE_DONT_CARE );
//		NGlobal::SetVar( "gfx_fullscreen", NStr::StrFmt( "%d", pluginSett->fullscreen ? 1 : 0 ), STORAGE_DONT_CARE );
	}

  //TODO: add SHIPPING ifdef
  bool fullscreen = NGlobal::GetVar( "gfx_fullscreen" ).GetInt64();

  const string szAppName( NStr::StrFmt( "%s - %s - %d.%d.%02d.%04d", PRODUCT_TITLE, VERSION_LINE, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_REVISION ) );
  string windowTitle( NStr::StrFmt( "%s - %s - %d.%d.%02d.%04d", PRODUCT_TITLE, VERSION_BRANCH, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_REVISION ) );
  if ( instancesLimit.Index() > 1 )
    windowTitle += NStr::StrFmt( " (%i)", instancesLimit.Index() );

  //if ( pluginSett )
  //{
  //  if ( !NMainFrame::InitApplication( hInstance, szAppName.c_str(), windowTitle.c_str(), MAKEINTRESOURCE( IDI_MAIN ), fullscreen, pluginSett->width, pluginSett->height, hWnd ) )
  //    return 0xDEAD;
  //}
  //else
  {
    if ( !NMainFrame::InitApplication( hInstance, szAppName.c_str(), windowTitle.c_str(), MAKEINTRESOURCEW( IDI_MAIN ), fullscreen, 800, 600, hWnd ) )
    {
      mainVars.DeInit();
      return 0xDEAD;
    }
  }

  Strong<ICastle> pCastleLink;

  if ( startFromCastle )
  {
    pCastleLink = CreateCastleLink( CmdLineLite::Instance().GetIntKey("connectionPort"),
                                    CmdLineLite::Instance().GetStringKey("castleCmdLn"),
                                    hInstance,
                                    NMainFrame::GetWnd());
  }
  else if (isTutorial)
  {
    nstl::string castleCmdLine(CmdLineLite::Instance().GetStringKey("--castleCmdLn", "Castle.exe"));

    {
      castleCmdLine = NStr::StrFmt("<%s> --snid %s --snuid %s --sntoken %s ver %s --server %s --secret %s --loginKey %s --uid %s",
        castleCmdLine.c_str(),
        socialLoginParams.snid,
        socialLoginParams.snuid,
        socialLaunchData.serverLoginToken.c_str(),
        socialLoginParams.version,
        socialLaunchData.serverName.c_str(),
        socialLaunchData.serverSecret.c_str(),
        socialLaunchData.serverKey.c_str(),
        socialLaunchData.uid.c_str());
    }

    pCastleLink = CreateCastleLink(0, castleCmdLine.c_str(), hInstance, NMainFrame::GetWnd());
  }
  else
  {
    pCastleLink = CreateDummyCastleLink();
  }

  mainVars.input = new Input::Binds( Input::CreateHwInput( NMainFrame::GetWnd(), hInstance, false, true ) );
  mainVars.inputSystemMesages = new Input::SystemEvents( mainVars.input );
  Input::BindsManager::Instance()->SetBinds( mainVars.input );
  mainVars.initInput = true;

  RootFileSystem::AddDebugMonitor( new FileExtensionStatisticsMonitor );
  IFileReadCallback* callback = CreateFileReadCallback( OnPileFileReadError, new GameFileReadContext( pCastleLink ) );

  RootFileSystem::RegisterFileSystem( new fileSystem::PileFileSystem(callback), callback );
  RootFileSystem::RegisterFileSystem( new WinFileSystem( NFile::GetBaseDir() + "Data", false ) );

  if (g_language.empty())
  {
    g_language = "ru-RU";
  }
  if (!TryLoadTextsCache( NFile::Combine( NFile::GetBaseDir(), "Data/Localization/" + g_language + "/text.text" ).c_str() ))
  {
    // fall back to the root text.text file
    TryLoadTextsCache( NFile::Combine( NFile::GetBaseDir(), "Packs/text.text" ).c_str() );
  }

  if ( const char * fsActLog = CmdLineLite::Instance().GetStringKey( "-log_fs_activity" ) )
    RootFileSystem::AddDebugMonitor( new FileActivitySimpleMonitor( fsActLog ) );

  Types::HashCheck::Check();
  PF_Types::HashCheck::Check();
  NDb::SetResourceCache( NDb::CreateGameResourceCache( RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor() ) );
  mainVars.initDB = true;

  if ( !NGlobal::ExecuteConfig( "game.cfg",	NProfile::FOLDER_GLOBAL ) )
  {
    mainVars.DeInit();
    return false;
  }

#ifdef _SHIPPING
  NGlobal::ExecuteConfig( "input_new.cfg", NProfile::FOLDER_USER, L"input" );
  NGlobal::ExecuteConfig( "smart_chat.cfg", NProfile::FOLDER_GLOBAL );
#endif

  if ( isSpectator )
  {
    NGlobal::ExecuteConfig( "spectator.cfg",	NProfile::FOLDER_GLOBAL );
  }

  NFile::DeleteOldFiles( NProfile::GetRootLogsFolder().c_str(), double(g_deleteLogFilesAfterDays) * 60 * 60 * 24 );
  NFile::DeleteOldFiles( NProfile::GetFullFolderPath(NProfile::FOLDER_REPLAYS).c_str(), double(g_deleteLogFilesAfterDays) * 60 * 60 * 24 );
  static std::string currentLogin = "";

  if ( s_localGame || isReplay )
  {
    context = new Game::LocalGameContext( isSpectator );
  }
  else if (isTutorial)
  {
    context = new Game::GameContext(socialLaunchData.sessionId.c_str(), NULL, socialLaunchData.mapId.c_str(), socialServer, guildEmblem, false, true);
  }

  std::string linuxRun = CmdLineLite::Instance().GetStringKey( "linux", "" );

  systemLog( NLogg::LEVEL_MESSAGE ) << "Linux run: \"" << linuxRun.c_str() << "\"" << endl;
  std::string protocolLineStr;
  if(!linuxRun.empty()) {
    WebPostRequest request(L"127.0.0.1", L"/getConnectionData", 34980, 0);
    std::string protocolResponse = request.SendPostRequest("getConnectionData");

    Json::Value parsedValue = ParseJson(protocolResponse.c_str());
    systemLog( NLogg::LEVEL_MESSAGE ) << "Protocol response: \"" << protocolResponse.c_str() << "\"" << endl;

    if (parsedValue.empty()) {
      systemLog( NLogg::LEVEL_MESSAGE ) << "Invalid protocol response: \"" << protocolResponse.c_str() << "\"" << endl;
      RunLinuxLauncher();
      //ShowLocalizedErrorMB( L"StartViaLauncher", L"Invalid arguments [invalid socket protocol]! Please start the game via the launcher." );
      return 0;
    }
    Json::Value protocolValue = parsedValue.get("protocol", "");
    if (protocolValue.asString().empty()) {
      systemLog( NLogg::LEVEL_MESSAGE ) << "Empty protocol response: \"" << protocolResponse.c_str() << "\"" << endl;
      ShowLocalizedErrorMB( L"StartViaLauncher", L"Invalid arguments [empty socket protocol]! Please start the game via the launcher." );
      return 0;
    }

    protocolLineStr = protocolValue.asString();

    if (protocolLineStr.empty()) {
      RunLinuxLauncher();
      return 0;
    }
  } else {
    protocolLineStr = CmdLineLite::Instance().GetStringKey( "protocol", "" );
  }

  if(protocolLineStr.empty()) {
    ShowLocalizedErrorMB( L"StartViaLauncher", L"Invalid arguments [empty protocol]! Please start the game via the launcher." );
    return 0;
  } else {
    const char* protocolLine = protocolLineStr.c_str();
    const char* delimiter = "/";

    char* token = strtok(const_cast<char*>(protocolLine), delimiter);
    std::vector<std::string> allTokens;
    allTokens.reserve(5);

    while (token != 0) {
      allTokens.push_back(token);
      token = strtok(0, delimiter);
    }

    if(allTokens.size() < 5) {
      ShowLocalizedErrorMB( L"StartViaLauncher", L"Invalid protocol" );
      return 0;
    }

    string protocolMethod = allTokens[1].c_str();
    const char* protocolToken = allTokens[2].c_str();
    g_protocolToken = protocolToken;
    const char* versionStr = allTokens[3].c_str();
    const char* goMirrorFirst = allTokens[4].c_str();
    usedServer = goMirrorFirst[0] - (int('0'));

    int versionMajor = VERSION_MAJOR;
    int versionMinor = VERSION_MINOR;
    int versionPatch = VERSION_PATCH;
    char versionStrBuff[64] = {};

    sprintf_s(versionStrBuff,"%d.%d.%d",versionMajor, versionMinor, versionPatch);

    if(strcmp(versionStrBuff, versionStr) != 0) {
      ShowLocalizedErrorMB( L"Update failed", L"Game update has failed! Try to run from web-launcher" );
      return 0;
    }

    WebLauncherPostRequest::WebLoginResponse response;
    if (protocolMethod == "runGame" || protocolMethod == "reconnect") {
      //WebLauncherPostRequest cprequest;
      //cprequest.CreateDebugSession();
      WebLauncherPostRequest rprequest;
      response = rprequest.GetSessionData(protocolToken);
      if (response.retCode == WebLauncherPostRequest::LoginResponse_WEB_FAILED_CONNECTION) {
       usedServer = (usedServer + 1) % _countof(SERVER_IP_ARRAY);
       WebLauncherPostRequest mirror_rprequest;
       response = mirror_rprequest.GetSessionData(protocolToken);
       if (response.retCode == WebLauncherPostRequest::LoginResponse_WEB_FAILED_CONNECTION) {
         usedServer = (usedServer + 1) % _countof(SERVER_IP_ARRAY);
         WebLauncherPostRequest proxy_rprequest;
         response = proxy_rprequest.GetSessionData(protocolToken);
       }
      }
    } else {
      ShowLocalizedErrorMB( L"StartViaLauncher", L"Invalid protocol syntax" );
      return 0;
    }

    if (response.retCode == WebLauncherPostRequest::LoginResponse_WEB_FAIL) {
      systemLog( NLogg::LEVEL_MESSAGE ).Trace("Failed connection with reason: %s", response.response.c_str());
      ShowLocalizedErrorMB( L"Connection failed", L"Game server response error!" );
      return 0;
    }


    if (response.retCode == WebLauncherPostRequest::LoginResponse_WEB_JOIN) {
        // Login success
        currentLogin = std::string(" ") + response.response;
        currentLogin[0] = 0x09;
        g_devLogin = currentLogin.c_str();

        const char * mapId = CmdLineLite::Instance().GetStringKey( "mapId", "" );
        if (g_localGameRun) {
          context = new Game::LocalGameContext( false );
          g_sessionStatus = WebLauncherPostRequest::RegisterInSessionRequest_WebCreate;
        } else {
          context = new Game::GameContext(g_sessionToken.c_str(), g_devLogin.c_str(), mapId, socialServer, guildEmblem, isSpectator, false );
        }
        context->Start();
    } else {
      ShowLocalizedErrorMB( L"Error", L"Unknown response" );
      return 0;
    }
  }

  mainVars.initContext = true;

  //This config is mainly needed to enable custom lobby console commands
  NGlobal::ExecuteConfig( "autoexec.cfg",	NProfile::FOLDER_GLOBAL );

  if(s_bRegisterReplayExtention)
   RegisterReplayFileExtentionAssociation();

  CoInitialize(NULL);

  if ( s_NullRender && !g_NullRenderNoLogBox )
  {
    mainVars.logBox = new NLogg::EditBoxDumper( &GetSystemLog(), NMainFrame::GetWnd() );
  }

  if ( g_DebugDumpInfo )
  {
    NBSU::SystemReport sysRep;
    sysRep.dumpSystemInfo(true);
  }

  Render::RenderMode renderMode;
  
  mainVars.renderingInterface = new Render::DeviceLostWrapper<PF_Render::Interface>;
  Render::GetRenderModeFromConfig(renderMode);
  Render::GetRenderer()->CorrectRenderMode(renderMode);
  PF_Render::Interface::CorrectRendermode( renderMode );
    
  NullRenderSignal::Signal(s_NullRender);
  if(!mainVars.renderingInterface->Start( renderMode ) && s_NullRender != RENDER_DISABLE_FLAG)
  {
    mainVars.DeInit();
    return 0xDEAD;
  }

  mainVars.initRender = true;
  mainVars.initDisplay = true;

  if(s_NullRender != RENDER_DISABLE_FLAG)
    if ( !CheckHardwareCompatibility() )
    {
      mainVars.DeInit();
      return 0xDEAD;
    }

  UI::Initialize( NDb::Get<NDb::UIRoot>( NDb::DBID( "UI/UIRoot" ) ) );
  mainVars.initUI = true;

  if(s_NullRender != RENDER_DISABLE_FLAG) {
    const Render::RenderMode& currentRenderMode = Render::GetRenderer()->GetCurrentRenderMode();
    UI::UpdateScreenResolution( currentRenderMode.width, currentRenderMode.height, false );
    NMainFrame::ResizeWindow( currentRenderMode.width, currentRenderMode.height, currentRenderMode.isFullScreen, currentRenderMode.isBorderless );
  }

  if ( mainVars.logBox )
    mainVars.logBox->ResizeLogWindow();

  /*if(s_NullRender != RENDER_DISABLE_FLAG)*/ 
  {
    NSoundScene::TryTurnOnSound();

    NDb::SoundRoot::InitRoot( NDb::DBID( "Audio/SoundRoot" ) );
    for ( int i = 0; i < NDb::SoundRoot::GetRoot()->sceneScenes.size(); i++ )
      NSoundScene::InitSoundScene( i, NDb::SoundRoot::GetRoot()->sceneScenes[i] );
    NSoundScene::ActivateSoundScene( NDb::SOUNDSCENE_LOADING, false );
    mainVars.initSound = true;
  }

  if (s_bSteamInited)
    SteamUtils()->SetOverlayNotificationPosition( k_EPositionBottomLeft );

  // force linking with PF_Core
  PF_Core::ForceLink();
  PF_Minigames::ForceLink();
  TooSmartLinker();

  RaiseMainThreadPriority();

  NMainLoop::CreateSystemScreens();

  AltTabChecker altTabChecker( NMainFrame::IsAppNotMinimized() && NMainFrame::IsAppActive() );
  bool isActive = true; // This ensures, that first priority change will always raise priority

  MovingAverage<float, 60> avgDeltaTime;
  bool isFirstLoopStep = true;

  static HANDLE hFileMapping = INVALID_HANDLE_VALUE;

  bool wasActive = true;
  bool wasInLostDevice = false;

  SetStepCallback(StepContext);

  if(isReplay)
  {
    nstl::wstring path;
    NStr::ToUnicode(&path, replayFileName);
    NGlobal::RunCommand(NStr::StrFmtW(L"replay \"%s\"", path.c_str()));
  }

  InitCensorDicts();
  

  mainVars.initGameFSM = true;
  while ( true )
  {
    NI_PROFILE_BLOCK_MEM( "Main Loop" );

#if defined( NI_PROFILER_USE_VERSION ) && ( NI_PROFILER_USE_VERSION == 3 )
    NI_PROFILE_BLOCK_MEM( "Frame" );
#endif
    //compulsory update
    //-----------------------------------------------------------------------
    NMainLoop::UpdateTime();
    timerUpdated = true;
    
    { //Check for alt+tab
      altTabChecker.Update();
      if(altTabChecker.IsActive() != isActive) {
        isActive = altTabChecker.IsActive();
        context->OnAltTab(isActive);
      }
    }

    if( fullscreen && altTabChecker.IsActive() )
    {
      if(INVALID_HANDLE_VALUE == hFileMapping) {
        // Turn off Outlook notifications. See http://support.microsoft.com/kb/913045
        const TCHAR strFileMapName[] = TEXT("Local\\FullScreenPresentationModeInfo");
        hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RECT), strFileMapName);
        if(hFileMapping) {          
          // Writing the application's rect into the shared memory
          RECT *pRect = static_cast<RECT *>(MapViewOfFile(hFileMapping, FILE_MAP_WRITE, 0, 0, 0));
          if(pRect) {
            GetWindowRect(NMainFrame::GetWnd(), pRect);
            UnmapViewOfFile(pRect);
          }    
        }
      }
    }
    else if(INVALID_HANDLE_VALUE != hFileMapping)
    {
      CloseHandle(hFileMapping);
      hFileMapping = INVALID_HANDLE_VALUE;
    }

    // Update socialConnection
    {
      socialServer->Step();
    }

    // Update sound
    {
      if ( altTabChecker.IsActive() )
	      NSoundScene::SystemResume();
	    else
	      NSoundScene::SystemStop();
	    NSoundScene::EventSystemUpdate( NMainLoop::GetTimeDelta() * 1000.0f );
	    NSoundScene::Update();
    }

    StrongMT<StatisticService::GameStatistics> gameStatistics = context->GameStatLogic();
    // Pump messages
    {
      NHPTimer::STime startTime, endTime;
      NHPTimer::GetTime( startTime );

      NMainFrame::PumpMessages();

      NHPTimer::GetTime( endTime );

      if ( NHPTimer::Time2Milliseconds( endTime - startTime ) > 10 )
      {
        if ( gameStatistics )
        {
          gameStatistics->AddStepTimeFlags( StatisticService::RPC::DragFlag );
          gameStatistics->RemoveStepTimeFlags( StatisticService::RPC::DragFlag );
        }

        DebugTrace("Pump messages lag");
      }

      if ( NMainFrame::IsExit() )
        context->Shutdown();
    }

    if ( gameStatistics )
    {
      if ( wasActive ^ NMainFrame::IsAppActive() )
      {
        if ( NMainFrame::IsAppActive() )
            gameStatistics->RemoveStepTimeFlags( StatisticService::RPC::InactiveFlag );
        else
            gameStatistics->AddStepTimeFlags( StatisticService::RPC::InactiveFlag );

        wasActive = NMainFrame::IsAppActive();
      }

      if ( wasInLostDevice ^ Render::GetRenderer()->DeviceIsLost() )
      {
        if ( Render::GetRenderer()->DeviceIsLost() )
          gameStatistics->AddStepTimeFlags( StatisticService::RPC::InactiveFlag );
        else
          gameStatistics->RemoveStepTimeFlags( StatisticService::RPC::InactiveFlag );

        wasInLostDevice = Render::GetRenderer()->DeviceIsLost();
      }
    }

    RootFileSystem::ProcessFileWatchers();
    // Step context
    int numCommands = -1;
    if( !NMainFrame::IsExit() )
    {
      NI_PROFILE_BLOCK( "Context" );
      mainPerf_ContextStep.Start();
      numCommands = context->Poll( NMainLoop::GetTimeDelta() );
      mainPerf_ContextStep.Stop();
    }
    timerUpdated = false;

    SkipFrameIfNeeded(numCommands);

    const float commonTimeDelta = NMainLoop::GetTimeDelta();
    
    //��������� ������������ �����, ��� ��� ����������� ����� ������� ��������, ��� FPS < 60
    const float smoothTimeDelta = avgDeltaTime.NextValue( NMainLoop::GetTimeDelta(), g_maxMovingAvgTime );    
    NMainLoop::SetTemporaryTimeDelta( smoothTimeDelta );
    
    { // Mainloop
      mainPerf_Step.Start();

      //Poll input
      mainVars.input->Update( NMainLoop::GetTimeDelta(), altTabChecker.IsActive() );
      mainVars.inputSystemMesages->Pump( mainVars.input->GetEvents() );

      //Step game screens and stuff #here call adv screen
      const bool canContinue = NMainLoop::Step( NMainFrame::IsAppActive(), mainVars.input->GetEvents() );
      mainPerf_Step.Stop();
      if ( !canContinue ) 
        break;
    }

    if ( !NMainFrame::IsAppActive() && 0 <= g_inactiveSleep && g_inactiveSleep < 1000 )
    {
      NI_PROFILE_BLOCK( "Sleep" );
      Sleep( g_inactiveSleep );
    }

    // Do present, unless skip everything
    if ( !s_NullRender )
    {
      Render::SharedVB::UnlockAll(); // Don't hold VB's locked for too long

      NI_PROFILE_BLOCK( "RenderPresent" );
      mainPerf_Present.Start(true);
      //NMainLoop::VSyncController::WaitBeforePresent();
      int presentCount = NMainLoop::VSyncController::CalculatePresentCount();
      for (int i = 0; i < presentCount; i++)
        mainVars.renderingInterface->Present();
      NMainLoop::VSyncController::MarkPresentFinished();
      mainPerf_Present.Stop();
    }
    
    NMainLoop::SetTemporaryTimeDelta( commonTimeDelta );

    // Process counters
    mainPerf.Stop();

    {
      NI_PROFILE_BLOCK( "NDebug" );

      // debug ui renderers can be processed only if we call some Render
      NDebug::DrawDebugVars();

      debugDisplay::Render();

      debugDisplay::AddText( "__pos0", debugDisplay::Align( 0, -1 ), NStr::StrFmt( "version: %s-%d.%d.%02d.%04d", VERSION_LINE, VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD, VERSION_REVISION ), debugDisplay::Color::White );

      NDebug::ShowFPS();

      NDebug::ShowTime();
      NDebug::DumpSystemStatistics();

      NDebug::CheckWatchedVars();
      NDebug::ResetDebugVarsOnFrame( NMainLoop::GetTimeDelta() );

      // get information about mallocs from newdelete.cpp
		  heapAllocs.SetValue( GetMallocsTotal() );
		  heapAllocsSize.SetValue( GetMallocsSize() );
		  unfreeHeapAllocs.SetValue( GetMallocsUnfree() );

      // get information about mallocs fron detour
		  //heapAllocs.SetValue( NDebug::GetHeapAllocCount() );
		  //heapAllocsSize.SetValue( NDebug::GetTotalHeapAllocSize() );
		  //unfreeHeapAllocs.SetValue( NDebug::GetUnfreeHeapAllocCount() );

#ifndef _SHIPPING
		  virtualAllocs.SetValue( NDebug::GetVirtualAllocCount() );
		  virtualAllocsSize.SetValue( NDebug::GetTotalVirtualAllocSize() );
		  unfreeVirtualAllocs.SetValue( NDebug::GetUnfreeVirtualAllocCount() );

		  totalAllocsSize.SetValue( ( NDebug::GetTotalVirtualAllocSize() + NDebug::GetTotalHeapAllocSize() ) / 1024 / 1024 );
#endif //_SHIPPING
    }
		
    mainPerf.Start( true );

    // Render. RenderUI at least
    if( !s_NullRender )
    {
      NI_PROFILE_BLOCK( "Render" );
      mainPerf_Render.Start();
      mainVars.renderingInterface->Render( false );
      mainPerf_Render.Stop();
    }
    else
      mainVars.renderingInterface->FlushUI();

    if ( isFirstLoopStep )
      pCastleLink->StartRender();
      
    pCastleLink->Update( NMainLoop::GetTimeDelta() );
    
    isFirstLoopStep = false;
  } //Main loop ends

  if (socialSession )
    socialSession->Logout();

  if ( NMainFrame::GetExitCode() == EXIT_CODE_QUIT_CASTLE )
  {
    //if (socialSession)
    //  socialSession->Logout();
    pCastleLink->QuitGame();
    

  }
  else
    pCastleLink->ReturnToCastle();

  if ( persistentEvents::GetSingleton() )
    persistentEvents::GetSingleton()->Close();

	if(!isReplay)
  {
#ifdef _SHIPPING
		Input::SaveInputConfig( NProfile::GetFullFilePath( "input_new.cfg", NProfile::FOLDER_USER ), L"input" );
#endif
    NGlobal::SaveConfig( NProfile::GetFullFilePath( "user.cfg", NProfile::FOLDER_USER ), STORAGE_USER );
  }

#ifndef _SHIPPING
  DumpLoadedModules();
#endif //_SHIPPING

  mainVars.DeInit();
  return 0;
}


//Entry point
extern "C"
{

INTERMODULE_EXPORT void WINAPIV StartPWApplication( HWND hWnd )
{
  PseudoWinMain( GetModuleHandle( NULL ), hWnd, GetCommandLine(), 0 );
}

INTERMODULE_EXPORT void WINAPIV StartPWPlugin( HWND hWnd, int width, int height, bool fullscreen, const char * sessionLogin )
{
  SPluginSettings sett;
  sett.width = width;
  sett.height = height;
  sett.fullscreen = fullscreen;
  sett.sessionLogin = sessionLogin;
  PseudoWinMain( GetModuleHandle( NULL ), hWnd, GetCommandLine(), &sett );

  TerminateProcess( GetCurrentProcess(), 0 );
}

} //extern "C"


#ifndef DO_NOT_USE_DLLMAIN
BOOL WINAPI DllMain( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved )
{
  switch ( fdwReason )
  {
    case DLL_PROCESS_ATTACH:
      return TRUE;
    case DLL_THREAD_ATTACH:
      return TRUE;
    case DLL_THREAD_DETACH:
      return TRUE;
    case DLL_PROCESS_DETACH:
      return TRUE;
  }
  return TRUE;
}
#endif



//Debug console command, used to debug exception handling
static int DoTheStackOverflow( int depth, char * ptr, int size )
{
  if ( !depth )
    return 0;

  int sum = 0;
  for( int i = 0; i < size; ++i )
    sum += ptr[i];
  char buf[256];
  memset( buf, 0, 256 );
  sum += DoTheStackOverflow( depth - 1, buf, 256 );
  return sum;
}



static bool DebugCrashNow( const char *name, const vector<wstring> &params )
{
  if ( params.size() == 1 )
  {
    if ( !_wcsicmp( params[0].c_str(),  L"gpf") )
    {
      int * nullPtr = 0;
      DebugTrace( "Writing address 0x%08x...", (int)nullPtr ); //Block compiler optimizations
      *nullPtr = 0;
      return true;
    }
    else if ( !_wcsicmp( params[0].c_str(),  L"zero") )
    {
      int zero = strtol( "0", 0, 10 ); //FIXME: Maharaja code!
      DebugTrace( "Dividing by %d...", zero ); //Block compiler optimizations
      DebugTrace( "Result: %d", (int)params.size() / zero );
      return true;
    }
    else if ( !_wcsicmp( params[0].c_str(),  L"stack") )
    {
      DebugTrace( "Flooding stack..." );
      DebugTrace( "Result: %d", DoTheStackOverflow( 1024 * 1024 * 1024, 0, 0 ) );
      return true;
    }
    else if ( !_wcsicmp( params[0].c_str(),  L"assert") )
    {
      NI_ALWAYS_ASSERT( "Test assert" );
      return true;
    }
    else if ( !_wcsicmp( params[0].c_str(),  L"heap") )
    {
      DebugTrace( "Allocating memory..." );
      for ( int i = 0; i < 1024 * 1024; ++i )
      {
        const int size = 64 * 1024;
        char * ptr = new char[size];
        if ( ptr )
        {
          //Fill just first kb to avoid extensive swap
          for ( int j = 0; ( j < size ) && ( j < 1024 ); ++j )
            ptr[j] = j & 0xff;
        }
      }
      return true;
    }
  }

  DebugTrace( "Usage: %s gpf | zero | stack | assert | heap", name );
  return true;
}

static bool SetMallocThreadMask( const char * name, const vector<wstring> & _paramsSet )
{
  if ( _paramsSet.size() > 0 )
    SetMallocThreadMask( NStr::ToULong( NStr::ToMBCS( _paramsSet[0] ) ) );

  return true;
}

REGISTER_CMD( debug_crash_now, DebugCrashNow );
REGISTER_CMD( malloc_mask, SetMallocThreadMask )

#endif
