#include "System/stdafx.h"

#include "PF_GameLogic/PFAdvMap.h"

#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "PF_GameLogic/DBAdvMap.h"
#include "PF_GameLogic/DBHeroesList.h"
#include "PF_GameLogic/DBUnit.h"
#include "PF_GameLogic/GameMaps.h"
#include "System/Crc32Checksum.h"
#include "System/RandomGenerator.h"

extern bool G_IsRandomBotSkinsEnabled();

namespace NWorld
{
string GetRandomHeroSkin(uint heroId, const NDb::AdvMapDescription* advMapDesc, NRandom::RandomGenerator& randGen, NCore::ETeam::Enum teamId);
}

namespace
{
void AddBots(vector<NCore::PlayerStartInfo>& players, const vector<NDb::AdvMapPlayerData>& bots, NCore::ETeam::Enum team)
{
  for (int i = 0; i < bots.size(); ++i)
  {
    if (!IsValid(bots[i].hero))
    {
      continue;
    }

    NCore::PlayerStartInfo& player = players.push_back();
    player.playerType = NCore::EPlayerType::Computer;
    player.userID = -1;
    player.zzimaSex = NCore::ESex::Male;
    player.teamID = team;
    player.originalTeamID = team;
    player.playerInfo.heroId = Crc32Checksum().AddString(bots[i].hero->id.c_str()).Get();
    player.usePlayerInfoTalentSet = false;
    player.nickname = bots[i].nickname.GetText();
    player.playerInfo.leagueIndex = 0;
  }
}

void ShuffleHeroes(vector<NCore::PlayerStartInfo>& players, int randomSeed)
{
  NRandom::RandomGenerator randGen(randomSeed);

  vector<NCore::PlayerInfo*> teamPlayerInfos[2];
  for (int pl = 0, total = players.size(); pl < total; ++pl)
  {
    NCore::PlayerStartInfo& playerStartInfo = players[pl];
    if (playerStartInfo.playerType != NCore::EPlayerType::Human)
    {
      continue;
    }

    NI_VERIFY(0 <= playerStartInfo.teamID && playerStartInfo.teamID < 2, "Invalid teamID", continue);
    teamPlayerInfos[playerStartInfo.teamID].push_back(&playerStartInfo.playerInfo);
  }

  for (int team = 0; team < 2; ++team)
  {
    vector<NCore::PlayerInfo*>& playerInfos = teamPlayerInfos[team];
    const int teamSize = playerInfos.size();
    if (teamSize < 2)
    {
      continue;
    }

    vector<int> shufflePos;
    shufflePos.resize(teamSize);
    for (int i = 0; i < teamSize; ++i)
    {
      shufflePos[i] = i;
    }

    const bool odd = teamSize % 2;
    for (int count = 0, total = teamSize / 2 + odd; count < total; ++count)
    {
      const int pos = shufflePos[count];
      const int max = shufflePos.size() - odd;
      const int tmp = randGen.Next(max);
      const int pos2 = shufflePos[tmp];

      NCore::PlayerInfo& leftPI = *playerInfos[pos];
      NCore::PlayerInfo& rightPI = *playerInfos[pos2];

      nstl::swap(leftPI.auid, rightPI.auid);
      nstl::swap(leftPI.heroId, rightPI.heroId);
      nstl::swap(leftPI.heroEnergy, rightPI.heroEnergy);
      nstl::swap(leftPI.avatarLevel, rightPI.avatarLevel);
      nstl::swap(leftPI.heroLevel, rightPI.heroLevel);
      nstl::swap(leftPI.heroExp, rightPI.heroExp);
      nstl::swap(leftPI.heroRating, rightPI.heroRating);
      nstl::swap(leftPI.hsHealth, rightPI.hsHealth);
      nstl::swap(leftPI.hsMana, rightPI.hsMana);
      nstl::swap(leftPI.hsStrength, rightPI.hsStrength);
      nstl::swap(leftPI.hsIntellect, rightPI.hsIntellect);
      nstl::swap(leftPI.hsAgility, rightPI.hsAgility);
      nstl::swap(leftPI.hsCunning, rightPI.hsCunning);
      nstl::swap(leftPI.hsFortitude, rightPI.hsFortitude);
      nstl::swap(leftPI.hsWill, rightPI.hsWill);
      nstl::swap(leftPI.hsLifeRegen, rightPI.hsLifeRegen);
      nstl::swap(leftPI.hsManaRegen, rightPI.hsManaRegen);

      if (!odd || count > 0)
      {
        shufflePos.eraseByIndex(tmp);
      }
    }
  }
}
}

namespace NWorld
{
static bool GetSpawnersImpl(NCore::TPlayerSpawnInfo& result, const NDb::AdvMap* dbMap, int& team1Size, int& team2Size)
{
  vector<NDb::AdvMapObject> const& objects = dbMap->objects;
  for (vector<NDb::AdvMapObject>::const_iterator it = objects.begin(), end = objects.end(); it != end; ++it)
  {
    NDb::Ptr<NDb::GameObject> gameObject = it->gameObject;
    if (!gameObject || gameObject->GetObjectTypeID() != NDb::HeroPlaceHolder::typeId)
    {
      continue;
    }

    const NDb::HeroPlaceHolder* spawner = dynamic_cast<const NDb::HeroPlaceHolder*>(gameObject.GetPtr());
    NI_VERIFY(spawner, "Invalid gameobject!", continue);

    NCore::ETeam::Enum team = static_cast<NCore::ETeam::Enum>(spawner->teamId);
    if (team == NCore::ETeam::Team1)
    {
      ++team1Size;
    }
    else if (team == NCore::ETeam::Team2)
    {
      ++team2Size;
    }
    else
    {
      NI_ALWAYS_ASSERT("Wrong team id");
      continue;
    }

    result.push_back(team);
  }

  NI_VERIFY((0 < team1Size) && (0 < team2Size), "Invalid map: no placeholders found for one or both team(s)", return false);
  DebugTrace("Loaded info for map '%s', max players per team: %d / %d", dbMap->GetDBID().GetFileName().c_str(), team1Size, team2Size);
  return true;
}

class PWFillMapStartInfo : public IMapLoader, public BaseObjectMT
{
  NI_DECLARE_REFCOUNT_CLASS_2(PWFillMapStartInfo, IMapLoader, BaseObjectMT);

public:
  explicit PWFillMapStartInfo(const NDb::AdvMap* dbMapValue)
    : dbMap(dbMapValue),
      dbMapDescription(0)
  {
  }

  explicit PWFillMapStartInfo(const NDb::AdvMapDescription* dbMapDescriptionValue)
    : dbMap(dbMapDescriptionValue->map),
      dbMapDescription(dbMapDescriptionValue)
  {
  }

  virtual const char* GetMapDbId() const
  {
    return dbMap->GetDBID().GetFileName().c_str();
  }

  virtual const char* GetMapDescName()
  {
    return IsValid(dbMapDescription) ? dbMapDescription->GetDBID().GetFileName().c_str() : 0;
  }

  virtual bool FillMapStartInfo(NCore::MapStartInfo& mapStartInfo, const lobby::TGameLineUp& gameLineup, const lobby::SGameParameters& gameParams)
  {
    NCore::TPlayerSpawnInfo spawners;
    int team1Size = 0;
    int team2Size = 0;
    if (!NWorld::GetSpawnersImpl(spawners, dbMap, team1Size, team2Size))
    {
      return false;
    }

    if (!IsValid(dbMapDescription))
    {
      return false;
    }

    mapStartInfo = NCore::MapStartInfo();
    mapStartInfo.randomSeed = gameParams.randomSeed;
    mapStartInfo.mapDescName = dbMapDescription->GetDBID().GetFileName();
    mapStartInfo.playersInfo.resize(spawners.size());
    mapStartInfo.isCustomGame = gameParams.customGame;

    vector<NCore::PlayerStartInfo> players;
    players.reserve(gameLineup.size());
    for (int i = 0; i < gameLineup.size(); ++i)
    {
      const lobby::SGameMember& lobbyPlayer = gameLineup[i];
      NCore::PlayerStartInfo& player = players.push_back();
      player.playerType = NCore::EPlayerType::Enum(lobbyPlayer.context.playerType);
      player.userID = lobbyPlayer.user.userId;
      player.zzimaSex = static_cast<NCore::ESex::Enum>(lobbyPlayer.user.zzimaSex);
      player.teamID = static_cast<NCore::ETeam::Enum>(lobbyPlayer.context.team);
      player.originalTeamID = static_cast<NCore::ETeam::Enum>(lobbyPlayer.context.original_team);
      player.nickname = lobbyPlayer.user.nickname;
      player.playerInfo.heroId = Crc32Checksum().AddString(lobbyPlayer.context.hero.c_str()).Get();
      player.usePlayerInfoTalentSet = false;

      if (player.playerType == NCore::EPlayerType::Computer)
      {
        player.playerInfo.heroSkin = lobbyPlayer.context.botSkin;
      }
    }

    const NDb::AdvMapHeroesOverride* heroesOverride = dbMapDescription->heroesOverride;
    if (heroesOverride)
    {
      if (!players.empty() && dbMapDescription->mapType == NDb::MAPTYPE_COOPERATIVE)
      {
        OverrideCooperative(players);
      }
      else if (players.size() == 1)
      {
        OverrideSinglePlayer(players);
      }
    }

    for (int i = 0; i < players.size(); ++i)
    {
      NCore::PlayerStartInfo* firstFreeSlot = 0;
      NCore::PlayerStartInfo* firstCorrectSlot = 0;
      for (int j = 0; j < spawners.size(); ++j)
      {
        NCore::PlayerStartInfo* player = &mapStartInfo.playersInfo[j];
        if (player->playerType != NCore::EPlayerType::Invalid)
        {
          continue;
        }

        if (!firstFreeSlot)
        {
          firstFreeSlot = player;
        }

        if (!firstCorrectSlot && spawners[j] == players[i].teamID)
        {
          firstCorrectSlot = player;
          break;
        }
      }

      NI_DATA_VERIFY(firstFreeSlot, "Not enough spawners at all", return false);
      NI_DATA_VERIFY(firstCorrectSlot, "Not enough spawners for one of the teams", firstCorrectSlot = firstFreeSlot);
      *firstCorrectSlot = players[i];
    }

    for (int i = 0; i < mapStartInfo.playersInfo.size(); ++i)
    {
      mapStartInfo.playersInfo[i].playerID = i;
    }

    return true;
  }

  virtual bool FillPlayersInfo(NCore::MapStartInfo& mapStartInfo, const vector<Peered::ClientInfo>&, bool = false)
  {
    NRandom::RandomGenerator randGen;
    randGen.SetSeed(mapStartInfo.randomSeed);

    for (int i = 0; i < mapStartInfo.playersInfo.size(); ++i)
    {
      NCore::PlayerStartInfo& slot = mapStartInfo.playersInfo[i];
      if (slot.playerType == NCore::EPlayerType::Computer && G_IsRandomBotSkinsEnabled())
      {
        NCore::ETeam::Enum teamId = slot.originalTeamID;
        if (teamId != NCore::ETeam::Team1 && teamId != NCore::ETeam::Team2)
        {
          teamId = slot.teamID;
        }

        slot.playerInfo.heroSkin = GetRandomHeroSkin(slot.playerInfo.heroId, dbMapDescription.GetPtr(), randGen, teamId);
      }

    }

    if (dbMapDescription->heroesOverride && dbMapDescription->heroesOverride->shuffleHeroesInTeam)
    {
      NI_ASSERT(!dbMapDescription->heroesOverride->singlePlayerMale && !dbMapDescription->heroesOverride->singlePlayerFemale, "Incompatible heroesOverride settings");
      ShuffleHeroes(mapStartInfo.playersInfo, mapStartInfo.randomSeed);
    }

    return true;
  }

  virtual int GetMaxPlayersPerTeam()
  {
    int team1Size = 0;
    int team2Size = 0;
    NCore::TPlayerSpawnInfo spawns;
    NWorld::GetSpawnersImpl(spawns, dbMap, team1Size, team2Size);
    return team1Size < team2Size ? team1Size : team2Size;
  }

private:
  void OverrideSinglePlayer(vector<NCore::PlayerStartInfo>& players)
  {
    NDb::Ptr<NDb::AdvMapHeroesOverrideData> heroesOverride;
    if (players[0].zzimaSex == NCore::ESex::Male)
    {
      heroesOverride = dbMapDescription->heroesOverride->singlePlayerMale;
    }
    else if (players[0].zzimaSex == NCore::ESex::Female)
    {
      heroesOverride = dbMapDescription->heroesOverride->singlePlayerFemale;
    }

    if (!IsValid(heroesOverride))
    {
      return;
    }

    players.reserve(1 + heroesOverride->allies.size() + heroesOverride->enemies.size());
    if (IsValid(heroesOverride->ownHero.hero))
    {
      players[0].playerInfo.heroId = Crc32Checksum().AddString(heroesOverride->ownHero.hero->id.c_str()).Get();
    }

    const NCore::ETeam::Enum localTeam = players[0].teamID;
    const NCore::ETeam::Enum enemyTeam = localTeam == NCore::ETeam::Team1 ? NCore::ETeam::Team2 : NCore::ETeam::Team1;
    AddBots(players, heroesOverride->allies, localTeam);
    AddBots(players, heroesOverride->enemies, enemyTeam);
  }

  void OverrideCooperative(vector<NCore::PlayerStartInfo>& players)
  {
    NDb::Ptr<NDb::AdvMapHeroesOverrideData> heroesOverride = dbMapDescription->heroesOverride->singlePlayerMale;
    if (!IsValid(heroesOverride))
    {
      return;
    }

    players.reserve(players.size() + heroesOverride->enemies.size());
    const NCore::ETeam::Enum localTeam = players[0].teamID;
    const NCore::ETeam::Enum enemyTeam = localTeam == NCore::ETeam::Team1 ? NCore::ETeam::Team2 : NCore::ETeam::Team1;
    AddBots(players, heroesOverride->enemies, enemyTeam);
  }

  NDb::Ptr<NDb::AdvMap> dbMap;
  NDb::Ptr<NDb::AdvMapDescription> dbMapDescription;
};

IMapLoader* CreatePWFillMapStartInfo(const NDb::AdvMapDescription* dbMapDescription)
{
  return new PWFillMapStartInfo(dbMapDescription);
}

IMapLoader* CreatePWFillMapStartInfo(const NDb::AdvMap* dbMap)
{
  return new PWFillMapStartInfo(dbMap);
}
}

NI_DEFINE_REFCOUNT(NWorld::IMapLoader)
