package 
{
	import adobe.utils.ProductManager;
	import BaseClasses.BaseResizableObject;
	import Components.Bar;
	import Controllers.TutorialShower;
	import Datas.ForceColors;
	import Datas.LeavedPlayer;
	import Datas.LeaverIconData;
	import Datas.PlayerStatisticData;
	import Enums.EBubbledElements;
	import Enums.EBubbleViewType;
	import Enums.EFraction;
	import Enums.ETutorialBlocks;
	import Enums.ETutorialHighlitedObjects;
	import Enums.EWindows;
	import flash.display.DisplayObject;
	import flash.display.MovieClip;
	import flash.events.MouseEvent;
	import flash.utils.setInterval;
	import fl.motion.easing.Back;

	import Controllers.MateInfoController;
	import BaseClasses.MateInfoBase;
	import Enums.TalentUIState;
	import InventoryBar.*;
	import TalentBar.*;
	import MainScreen.*;
	import StatisticBars.CurrentGameStatistics;
	import flashx.textLayout.formats.Float;
	import Utils.GameTimeGlobalUtils;
	import VoiceChat.VoiceTeamOverlay;
	import Controllers.SoundController;
	import EscMenu.ShortcutItem;
	import Effects.AttentionEffect;
	import ActionBar.ActionButton;
	import flash.display.Graphics;
	import flash.display.Loader;
	import flash.display.Sprite;
	import flash.net.URLRequest;

	public class MainInterface
	{
		private var mainWindow:Main;
		
		// ping wheel + floating ping texts (created at runtime, driven by C++ AdventureScreen)
		private var pingLayer:Sprite;
		
		private var pingWheel:Sprite;
		
		private var pingWheelItems:Array;
		
		private var pingWheelHighlights:Array;
		
		private var pingWheelLabels:Array;
		
		private var pingWheelColors:Array;
		
		private var pingWheelText:ShortcutItem;
		
		private var pingMarkers:Object;
		
		private var pingWheelBg:Loader;
		
		private var pingPreloaded:Array;
		
		private var pingMarkerHeroes:Object;
		
		private var praisePanels:Object;
		
		private var smartCastModes:Object;
		

		private var friendlyTeamController:MateInfoController;
		private var enemyTeamController:MateInfoController;

		private var myInt:Number;

		private var ourID:int;
		
		private var selectedTalentBarHeroId:int = -1;
		private var selectionCharstatUnitID:int=-1;
		
		private var forceColors:Array = [];
		private var spectatorMode: Boolean = false;
		
		private var showAlwaysHeroesLevels:Boolean = false;
		private var _shortheroname: SetPlayerShortName;
		private var voiceTeamOverlay:VoiceTeamOverlay;
	
		public function MainInterface(_main:Main)
		{	
			mainWindow = _main
			_shortheroname = new SetPlayerShortName();
			friendlyTeamController = new MateInfoController(mainWindow.teamInfoFrendly_mc);
			enemyTeamController = new MateInfoController(mainWindow.teamInfoEnemy_mc);
			voiceTeamOverlay = new VoiceTeamOverlay(
				function():Array { return mainWindow.Players; },
				function(heroId:int):MateInfoBase { return friendlyTeamController.DoesHeroExist(heroId) ? friendlyTeamController.GetMateInfo(heroId) : null; },
				function():int { return ourID; });
		//	var Test:MainInterfaceTest = new MainInterfaceTest(this, mainWindow);
		//	var Test2:MyFullTest = new MyFullTest(this, mainWindow);
		}
		
		public function SetClientVersion(text:String):void
		{
			mainWindow.version_mc.SetVersion(text);
		}
		
		public function ShowAlwaysHeroesLevels(show:Boolean):void
		{
			showAlwaysHeroesLevels = show;
		}
		
		public function SetOurHeroIdententity(heroID:int, fraction:int,damageType:int, leaguePlaces:Array, leagueIndex:int = 0, ownLeaguePlace:int = 0, flagIcon:String = "", flagTooltip:String = "", isMuted:Boolean = false):void
		{
			var oldId:int = ourID;
			ourID = heroID;
			
			if (!spectatorMode) 
				mainWindow.statisticsBar_mc.allStats_mc.ResetMates();
				
			mainWindow.statisticsBar_mc.allStats_mc.SetOurHero(heroID, fraction);
			mainWindow.finalStat.SetOurHero(heroID, fraction);
			
			var player:PlayerStatisticData = GetPlayer(heroID);
			
			if (player == null)
			{
				player = new PlayerStatisticData();	
				mainWindow.Players.push(player);
			}
	
			player.HeroId = heroID;
			player.Fraction = fraction;
			player.DamageType = damageType;
			player.playerFlagPath = flagIcon;
			player.playerFlagTooltip = flagTooltip;
			player.LeagueIndex = leagueIndex;
			player.ownLeaguePlace = ownLeaguePlace;
			
			player.leaguePlaces.splice(0);
			for (var k:int = 0; k < leaguePlaces.length; k++)
			{
				player.leaguePlaces.push(leaguePlaces[k]);
			}
			
			if (!spectatorMode)
			{
				friendlyTeamController.ResetToStartState();
				enemyTeamController.ResetToStartState();
				
				friendlyTeamController.OurHeroId = heroID; // если не спектатор, то мы забиваем нашего игрока первым
			}
			
			mainWindow.chatBar.SetOurPlayerId(heroID);
			
			mainWindow.ResetData();
		}
		
		private function GetPlayerIndex(heroID:int):int
		{
			for (var i:int = 0; i < mainWindow.Players.length; i++)
			{
				if (mainWindow.Players[i].HeroId == heroID)
				{
					return i;
				}
			}
			return -1;
		}
		
		private function GetPlayer(heroId:int):PlayerStatisticData 
		{
			var index:int = GetPlayerIndex(heroId);
			
			if (index == -1) return null;
			
			return mainWindow.Players[index];
		}
		
		private function SetHeroIdentity(heroID:int, heroName:String, heroShortName:String, guildName:String, heroClass:String, path:String, isMale:Boolean, isBot:Boolean, force:int, fraction:int,
		originalFraction:int, heroRaiting:Number, heroRangIcon:String, heroRangTooltip:String,  flagIcon:String, flagTooltip:String, isPremiumAcount:Boolean, isEnemy:Boolean, partyId:int, isAnimatedAvatar:Boolean, leagueIndex:int, ownLeaguePlace:int, leaguePlaces:Array):void
		{
			FillPlayerData(heroID, heroName, heroShortName, guildName, heroClass, path, fraction, originalFraction, isMale, isBot, force, heroRaiting, heroRangIcon, heroRangTooltip, flagIcon, flagTooltip, isPremiumAcount, isEnemy, partyId, isAnimatedAvatar, leagueIndex, ownLeaguePlace, leaguePlaces);
		}
		public function ShowHeroPortrait( heroID:int, isEnemy : Boolean = false ) : void
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			
			if (player != null) 
			{
				//player.IsNPC = false;
				player.IsNeedToShowInStatistics = true;
				mainWindow.statisticsBar_mc.allStats_mc.SetHeroIdentity(player);
			}
			
			var mateInfo:MateInfoBase = GetMateInfo(heroID);

			if (mateInfo == null)
			{
				var mateInfos:MateInfoController = null;
				
				if (spectatorMode)
				{
					switch (player.Fraction)
					{
					case EFraction.HUMAN_FACTION:
						mateInfos = friendlyTeamController;
						break;
					case EFraction.ELF_FACTION:
						mateInfos = enemyTeamController;
						break;
					default:
						break;
					}
				}
				else
				{
					if (player.IsEnemyTeam)
						mateInfos = enemyTeamController;
					else
						mateInfos = friendlyTeamController;
				}
				
				if (mateInfos == null)
					return;
				
				mateInfo = mateInfos.GenerateMateInfo(heroID, player.PartyID);
			}
						
			if (mateInfo == null)
				return;
	
			mateInfo.SetHero(player, false);
			
			if (player.IsOurHero)
			{				
				var bottomMate:HeroMateInfo = mainWindow.heroInfo_mc.heroInfoAnim_mc;
				bottomMate.SetHero(player);
				
				mainWindow.chatBar.chatOutput_mc.OurName = player.PlayerName;
				mainWindow.chatBar.chatOutput_mc.OurHeroId = player.HeroId;
			}						
		}
		
		public function SetFriendlyHeroIdentity(heroID:int, heroName:String, heroClass:String, path:String, isMale:Boolean, isBot:Boolean, force:int, fraction:int, originalFraction:int, heroRaiting:Number, leaguePlaces:Array, 
		heroRangIcon:String, heroRangTooltip:String, isPremiumAcount:Boolean = false, partyId:int=0, flagIcon:String = "", flagTooltip:String = "", isAnimatedAvatar:Boolean = false, leagueIndex:int = 0, ownLeaguePlace:int = 0):void
		{	
			var guildName:String = "";
			var heroShortName:String = "";
			var nick:String = heroName;
			
			if (heroName.substr(0, 1) != "[") guildName = "";
			else guildName = heroName.substr(0, heroName.indexOf("]") + 1);
			heroShortName = _shortheroname.SetPlayerName(heroName);
			
			SetHeroIdentity(heroID, nick, heroShortName, guildName, heroClass, path, isMale, isBot, force, fraction, originalFraction, heroRaiting, heroRangIcon, heroRangTooltip, flagIcon, flagTooltip, isPremiumAcount, false, partyId, isAnimatedAvatar, leagueIndex, ownLeaguePlace, leaguePlaces);
		}
		
		public function SetEnemyHeroIdentity(heroID:int, heroName:String, heroClass:String, path:String, isMale:Boolean, isBot:Boolean, force:int, fraction:int, originalFraction:int, heroRaiting:Number, leaguePlaces:Array,
		heroRangIcon:String, heroRangTooltip:String, isPremiumAcount:Boolean = false, partyId:int=0, flagIcon:String = "", flagTooltip:String = "", isAnimatedAvatar:Boolean = false, leagueIndex:int = 0, ownLeaguePlace:int = 0):void
		{
			var guildName:String = "";
			var heroShortName:String = "";
			var nick:String = heroName;
			
			if (heroName.substr(0, 1) != "[") guildName = "";
			else guildName = heroName.substr(0, heroName.indexOf("]") + 1);
			heroShortName = _shortheroname.SetPlayerName(heroName);
			
			SetHeroIdentity(heroID, nick, heroShortName,  guildName, heroClass, path, isMale, isBot, force, fraction, originalFraction, heroRaiting, heroRangIcon, heroRangTooltip, flagIcon, flagTooltip, isPremiumAcount, true, partyId, isAnimatedAvatar, leagueIndex, ownLeaguePlace, leaguePlaces);
		}
		
		private function FillPlayerData(heroID:int, heroName:String, heroShortName:String, guildName:String, heroClass:String, path:String, fraction:int,
		originalFraction:int, isMale:Boolean, isBot:Boolean, force:int, heroRaiting:Number, heroRangIcon:String, heroRangTooltip:String, flagIcon:String, flagTooltip:String, isPremiumAcount:Boolean, isEnemy:Boolean, partyId:int, isAnimatedAvatar:Boolean, leagueIndex:int, ownLeaguePlace:int, leaguePlaces:Array):void
		{
			var player:PlayerStatisticData = GetPlayer(heroID); 
			
			if (player == null)
			{
				player = new PlayerStatisticData();
				mainWindow.Players.push(player);
			}
			
			player.IsOurHero = ourID == heroID;
			player.HeroId = heroID;
			player.PlayerName = heroName;
			player.GuildName = guildName;
			player.PlayerShortName = heroShortName;
			player.ClassName = heroClass;
			player.HeroIcon = path;
			player.IsAnimatedAvatar = isAnimatedAvatar;
			player.LeagueIndex = leagueIndex;
			player.Fraction = fraction;
			player.Gender = isMale;
			player.IsBot = isBot;
			player.Force = force;
			player.ShowRedBack = (force >= forceColors[forceColors.length - 1].Force);
			player.HeroNameColor = GetColorByForce(force);
			player.OriginalFraction = originalFraction;
			player.HeroRankIcon  = heroRangIcon;
			player.HeroRankTooltip  = heroRangTooltip;
			player.HeroRaiting  = heroRaiting;
			player.IsPremium = isPremiumAcount;
			player.IsEnemyTeam = isEnemy;
			player.PartyID = partyId;
			player.playerFlagPath = flagIcon;
			player.playerFlagTooltip = flagTooltip;
			player.ownLeaguePlace = ownLeaguePlace;
			
			player.leaguePlaces.splice(0);
			for (var k:int = 0; k < leaguePlaces.length; k++)
			{
				player.leaguePlaces.push(leaguePlaces[k]);
			}
		}
		
		public function SetHeroChatMuted(heroId:int, chatMuted:Boolean):void
		{
			var player:PlayerStatisticData = GetPlayer(heroId);
			
			if (player == null)
			{
				trace("SetHeroChatMuted() - player must exist!")
				return
			}
			
			player.ChatMuted = chatMuted;
		}
		
		private function GetColorByForce(force:int):uint
		{
			var color:uint = 0xffffff;
				
			for (var i:int = 0; i < forceColors.length; i++) 
			{
				if (force >= forceColors[i].Force)
					color = forceColors[i].Color;
				else
					break;
			}	
				
			return color;
		}
		
		public function SetFriendship(heroID:int, areFriends:Boolean,isPending:Boolean = false):void
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			
			if (player == null) return;
			
			player.IsFriend = areFriends;
			player.IsPending = isPending;
			
			mainWindow.statisticsBar_mc.allStats_mc.SetFriendshipRelation(heroID, areFriends,isPending);	
			
			var mateInfo:MateInfoBase = GetMateInfo(heroID);
			
			if (mateInfo == null) return;
			
			mateInfo.SetFriendshipRelation(areFriends,isPending);
		}
		
		public function SetGameProgress(humanTerrain:Number, elfTerrain:Number, gameTime:int,timeToCreepSpawn:int):void
		{
			mainWindow.statisticsBar_mc.allStats_mc.SetGameProgress(humanTerrain, elfTerrain, gameTime);
			GameTimeGlobalUtils.CurrentTime = gameTime;
			if (mainWindow.dayNight_mc.visible)
				mainWindow.dayNight_mc.GameTime = gameTime;
			else
				mainWindow.miniMap_mc.GameTime = gameTime;
			mainWindow.announcement_mc.TimeToCreepSpawn = timeToCreepSpawn;
			mainWindow.voting_mc.SetTime(gameTime);
		}
		
		public function SetHeroGameProgress(heroID:int, kills:int, deaths:int, assists:int, score:int,prime:int = 0 ):void
		{
			var playerData:PlayerStatisticData = GetPlayer(heroID);
			
			if (playerData == null) return
			
			playerData.Assists = assists;
			playerData.Deaths = deaths;
			playerData.Kills = kills;
			playerData.Score = score;
			
			playerData.Prime = prime;
			if (playerData.isVisible)
				playerData.CurrentPrime = playerData.Prime;
			
			mainWindow.statisticsBar_mc.allStats_mc.SetHeroGameProgress(playerData);
		}

		private function GetMateInfo(id:int):MateInfoBase
		{
			if (friendlyTeamController.DoesHeroExist(id))
				return friendlyTeamController.GetMateInfo(id);
			
			if (enemyTeamController.DoesHeroExist(id))
				return enemyTeamController.GetMateInfo(id);
			
			return null;
		}
		
		public function SetHeroParams(heroID:int,level:int,curHealth:int,maxHealth:int,curMana:int,maxMana:int, isVisible:Boolean, isPickable:Boolean, timeToRessurect:int,channelingProgress:Number,healthRegen:Number=0,manaRegen:Number=0,isCameraLocked:Boolean=false, ultimateCoolDown:Number=-1.0):void
		{
			var playerData:PlayerStatisticData = GetPlayer(heroID);
			
			if (playerData == null)
				return;
		
			var visibleChanged:Boolean = playerData.isVisible != isVisible;
			
			playerData.Level = level;
			playerData.CurrentHealth = curHealth;
			playerData.MaximumHealth = maxHealth;
			playerData.CurrentMana = curMana;
			playerData.MaximumMana = maxMana;
			playerData.Channeling = channelingProgress;
			playerData.HealthRegen = healthRegen;
			playerData.ManaRegen = manaRegen;
			playerData.UltimateCooldown = ultimateCoolDown;
			
			playerData.isVisible = isVisible;
			playerData.TimeToRessurect = timeToRessurect;
			
			if (visibleChanged)
				SetHeroGameProgress(playerData.HeroId, playerData.Kills, playerData.Deaths, playerData.Assists, playerData.Score, playerData.Prime);
			
			if (isVisible || showAlwaysHeroesLevels || heroID==ourID) 
				playerData.LastVisibleLevel = level;
			
			mainWindow.statisticsBar_mc.allStats_mc.SetHeroStatus(heroID, playerData.LastVisibleLevel, isVisible, false, timeToRessurect);
			
			if (ourID == heroID)
			{
				mainWindow.SetOurHeroParams(level, curHealth, maxHealth, curMana, maxMana, timeToRessurect, channelingProgress, healthRegen, manaRegen, isCameraLocked);
				
				if (spectatorMode)
					mainWindow.actionBarContainer.actionBar_mc.EnemyHealthbarPresentation = (playerData.Fraction == EFraction.ELF_FACTION);
					
				if (playerData.CustomEnergy)
				{
					mainWindow.actionBarContainer.actionBar_mc.ManaBarColor = playerData.CustomEnergyColor;
					mainWindow.actionBarContainer.actionBar_mc.ManaBarShowRegeneration = (Math.abs(manaRegen) > 0);
				}
				else
				{
					mainWindow.actionBarContainer.actionBar_mc.ManaBarColor = Bar.MANABAR_COLOR_DEFAULT;
					mainWindow.actionBarContainer.actionBar_mc.ManaBarShowRegeneration = true;
				}
			}
			
			var mateInfo:MateInfoBase = GetMateInfo(heroID);

			if (mateInfo == null)
				return;

			var isRessurecting:Boolean = timeToRessurect > 0;
			
			mateInfo.SetGreyMask(isRessurecting||!isVisible||!isPickable);
			mateInfo.SetDeathTimeOut(timeToRessurect);
									
			if (isRessurecting || !isVisible)
				mateInfo.SetHeroParams(0, maxHealth, 0, maxMana, healthRegen, manaRegen, false);
			else
				mateInfo.SetHeroParams(curHealth, maxHealth, curMana, maxMana, healthRegen, manaRegen);
			
			mateInfo.level = playerData.LastVisibleLevel;
			mateInfo.ultcd = playerData.UltimateCooldown;
			
			if (playerData.CustomEnergy)
			{
				mateInfo.ManaBarColor = playerData.CustomEnergyColor;
				mateInfo.ManaBarShowRegeneration = (Math.abs(manaRegen) > 0);
			}
			else
			{
				mateInfo.ManaBarColor = Bar.MANABAR_COLOR_DEFAULT;
				mateInfo.ManaBarShowRegeneration = true;
			}
			
			if (selectedTalentBarHeroId == heroID)
			{
				mainWindow.talentsBarContext_mc.Level = playerData.LastVisibleLevel;
			}
			
			if (selectionCharstatUnitID == heroID)
			{
				//SetCharStatUnitParams(true, heroID, level,curHealth,maxHealth,curMana,maxMana,isVisible)
				mainWindow.selectionCharStats_mc.SetDeathTimeOut(timeToRessurect);
			}	
		}
		
		public function SetHeroCustomEnergy(heroID:int, name:String, color:uint):void
		{
			var playerData:PlayerStatisticData = GetPlayer(heroID);
			
			if (playerData == null)
				return;
			
			playerData.CustomEnergy = true;
			playerData.CustomEnergyName = name;
			playerData.CustomEnergyColor = color;
		}
		
		public function ShowStatistics(show:Boolean):void 
		{	
			mainWindow.ShowStatisticsBar(show);
		}
		
		public function SetSelectionParams(heroName:String,level:int,curHealth:int,maxHealth:int,curMana:int,maxMana:int,isEnemy:Boolean,channelingProgress:Number,healthRegen:Number=0,manaRegen:Number =0, heroID:int = -1):void
		{
			var playerData:PlayerStatisticData = (heroID < 0) ? null : GetPlayer(heroID);
			var customEnergy:Boolean = (playerData == null) ? false : playerData.CustomEnergy;
			var leagueIndex:int = (playerData == null) ? 0 : playerData.LeagueIndex;
			
			if (customEnergy)
			{
				mainWindow.selectionInfo_mc.manaBar_mc.barColor = playerData.CustomEnergyColor;
				mainWindow.selectionInfo_mc.manaBar_mc.ShowRegenerationText = (Math.abs(manaRegen) > 0);
			}	
			else
			{
				mainWindow.selectionInfo_mc.manaBar_mc.barColor = Bar.MANABAR_COLOR_DEFAULT;
				mainWindow.selectionInfo_mc.manaBar_mc.ShowRegenerationText = true;
			}
			
			mainWindow.selectionInfo_mc.setParams(heroName, level, curHealth, maxHealth, curMana, maxMana, isEnemy,channelingProgress,healthRegen,manaRegen,leagueIndex);
		}
		
		public function setSelectionInfoIcon(path:String, heroID:int, isMale:Boolean, isAnimatedAvatar:Boolean = false):void
		{
			var playerData:PlayerStatisticData = (heroID < 0) ? null : GetPlayer(heroID);
			var mateInfo:MateInfoBase = (heroID < 0) ? null : GetMateInfo(heroID);
			
			if (playerData == null)
			{
				mainWindow.selectionInfo_mc.HeroId = -1;
				mainWindow.selectionInfo_mc.IsOurHeroSelected = false;
				mainWindow.selectionInfo_mc.IsBot(false);
				mainWindow.selectionInfo_mc.ResetHeroNameColor();
				
				mainWindow.selectionInfo_mc.selectionInfoHero_mc.isHeroIgnored = false;
			}
			else
			{
				mainWindow.selectionInfo_mc.HeroId = heroID;
				mainWindow.selectionInfo_mc.IsOurHeroSelected = (heroID == ourID);
				mainWindow.selectionInfo_mc.IsBot(playerData.IsBot);
				mainWindow.selectionInfo_mc.SetHeroNameColor(playerData.HeroNameColor);
				mainWindow.selectionInfo_mc.SetFriendShipRelation(playerData.IsFriend, playerData.IsPending);	
				mainWindow.selectionInfo_mc.SetRedBackVisibility(playerData.ShowRedBack);
				
				mainWindow.selectionInfo_mc.selectionInfoHero_mc.isHeroIgnored = playerData.IsIgnoring;
			}
			
			mainWindow.selectionInfo_mc.SetIcon(path, isAnimatedAvatar);
			
			enemyTeamController.ResetMateSelection();
			friendlyTeamController.ResetMateSelection();
			
			if (mateInfo == null) 
			{
				mainWindow.selectionInfo_mc.ResetGender();
			}
			else
			{
				mateInfo.SetSelected(true);
				
				mainWindow.selectionInfo_mc.SetGender(isMale);
			}
		}
		public function SetTalentIcon(column:int, raw:int, path:String, altPath:String, isActive:Boolean,desiredIndex:int, upgradeLevel:int, isClassTalent:Boolean,price:int):void
		{
			mainWindow.talentsBar_mc.SetIcon(column, raw, path, altPath, isActive, desiredIndex, upgradeLevel, isClassTalent, price);
			
			if (isActive)
				mainWindow.actionBarContainer.actionBar_mc.SetReservedPosition(desiredIndex);
		}

		public function SetTalentState(column:int, raw:int, state:int)   //куплено/ не куплено
		{
			var talent:Talent = mainWindow.talentsBar_mc.GetTalent(column, raw);
			
			if (talent == null) return;
			
			if ((talent.icoState == TalentUIState.CANBUY || talent.icoState == TalentUIState.NOTENOUGH_DEV_POINTS) && state == TalentUIState.BOUGHT&&talent.IsActive) 
			{
				AddTalantToActBar(column, raw);
			}
			
			mainWindow.talentsBar_mc.setState(column, raw, state );
		}

		public function OnTalentsStateChanged()
		{
			mainWindow.talentsBar_mc.onStateChanged();
		}
		
		private function AddTalantToActBar(column:int, raw:int)
		{
			var talent:Talent = mainWindow.talentsBar_mc.GetTalent(column, raw);
			if (talent == null) return;
			
			var talentShortcut:TalentShortCut = new TalentShortCut(talent, mainWindow.actionBarContainer.actionBar_mc)
			
			mainWindow.actionBarContainer.actionBar_mc.AddShortcut(talentShortcut);
		}
		
			
		public function SetTalentStatus(column:int, raw:int, status:int, cooldown:Number, maxCooldownValue:Number, isAlternativStateActivated:Boolean = false)  //активно не активно
		{
			var talent:Talent = mainWindow.talentsBar_mc.GetTalent(column, raw);
			if (talent == null) return;
			
			talent.TalantState = status;
			talent.IsAlternativStateActivated = isAlternativStateActivated
			talent.Cooldown(cooldown,maxCooldownValue);	
			
			if (talent.shortcut==null) return;
				talent.shortcut.State = status;
					
		}
		
		public function SetSelectionTalentBarUnitID(heroID:int):void
		{
			selectedTalentBarHeroId = heroID;
			var playerData:PlayerStatisticData = GetPlayer(heroID);
			if (playerData == null) return;
			
			mainWindow.talentsBarContext_mc.HeaderText(playerData.PlayerName);
			mainWindow.talentsBarContext_mc.Level = playerData.LastVisibleLevel;
		}
		
		public function SetSelectionTalentIcon(column:int, raw:int, path:String, isActive:Boolean,upgradeLevel:int):void
		{
			mainWindow.talentsBarContext_mc.SetIcon(column, raw, path, isActive,upgradeLevel);
		}
		
		public function SetSelectionTalentState(column:int, raw:int, state:int):void
		{
			mainWindow.talentsBarContext_mc.setState(column, raw, state);
		}
		
		public function SetGlobalCooldown(cooldown:Number,maxCooldownValue:Number)
		{
			mainWindow.actionBarContainer.actionBar_mc.setGlobalCooldown(cooldown,maxCooldownValue);
			mainWindow.inventoriesBar_mc.setGlobalCooldown(cooldown,maxCooldownValue);
		}
		
		public function UseSlot(slot:int)
		{
			mainWindow.UseSlot(slot);
		}
		
		public function SetHeroDevelopmentParams(prime:int,zzGold:int):void
		{
			mainWindow.actionBarContainer.actionBar_mc.Prime = prime;
			mainWindow.actionBarContainer.actionBar_mc.Gold = zzGold;
			
			mainWindow.inventoriesBar_mc.Prime = prime;
		}
		// silent: no old minimap alert sound - the ping has its own sound (C++ AdventureScreen)
		public function AddSignal(X:Number, Y:Number, color:uint = 0xFFFCC2, lifeTime:Number = 0, silent:Boolean = false):void
		{
			SoundController.muteMinimapAlert = silent;
			try
			{
				mainWindow.miniMap_mc.AddSignal(X, Y, color, lifeTime);
			}
			catch (e:Error)
			{
			}
			SoundController.muteMinimapAlert = false;
		}
		
		public function SetMinimapTargetZoneColor(value:uint):void
		{
			mainWindow.miniMap_mc.SetTargetZoneColor(value);
		}
		
		public function SetMinimapTargetZoneThickness(value:uint):void
		{
			mainWindow.miniMap_mc.SetTargetZoneThickness(value);
		}
		
		public function ShowMinimapTargetZone(X:Number, Y:Number, radius:Number):void
		{
			mainWindow.miniMap_mc.ShowTargetZone(X, Y, radius);
		}
		
		public function HideMinimapTargetZone():void
		{
			mainWindow.miniMap_mc.HideTargetZone();
		}
		
		public function SetShopItemsCount(count:int):void
		{
			mainWindow.shopBar_mc.SetShopItemsCount(count);
		}
		public function SetShopItemIcon(index:int, path:String):void
		{
			mainWindow.shopBar_mc.SetShopItemIcon(index, path);
		}
		public function SetShopItemState(index:int, ShopItemState:int):void
		{
			mainWindow.shopBar_mc.SetShopItemState(index, ShopItemState);
		}
		
		public function ShowShop(show:Boolean ):void
		{
			mainWindow.ShowShop(show);
		}
		
		public function AddInventoryItemToActionBar(index:int):void
		{
			var item:InventoryButton = mainWindow.inventoriesBar_mc.GetInventoryItem(index);
			if (item==null) return;
			mainWindow.actionBarContainer.actionBar_mc.AddShortcut(new InventoryItemShortcut(item, mainWindow.actionBarContainer.actionBar_mc));
		}
		
		public function SetInventoryItemIcon(index:int, path:String):void
		{
			mainWindow.inventoriesBar_mc.SetInventoryItemIcon(index, path);
		}

		public function SetInventoryItemState(index:int, count:int, state:int, cooldown:Number,maxCooldownValue:Number)
		{
			var itemCount:int = mainWindow.inventoriesBar_mc.GetInventoryItem(index).count;
			
			mainWindow.inventoriesBar_mc.SetInventoryItemState(index, count, state, cooldown,maxCooldownValue);
			
			var item:InventoryButton = mainWindow.inventoriesBar_mc.GetInventoryItem(index)
			
			if (itemCount == count || count < 1) return;
			
			mainWindow.inventoriesBar_mc.ResetBuyToIndex();
			
			if (item.Shortcut == null&&mainWindow.actionBarContainer.actionBar_mc.BuyToIndex!=-1)
			{
				mainWindow.actionBarContainer.actionBar_mc.AddShortcut(new InventoryItemShortcut(item, mainWindow.actionBarContainer.actionBar_mc));
			}
			
			if (item.Shortcut != null&&mainWindow.actionBarContainer.actionBar_mc.BuyToIndex!=-1)
			{
				mainWindow.actionBarContainer.actionBar_mc.placeShortCutIntoIndex(mainWindow.actionBarContainer.actionBar_mc.BuyToIndex, item.Shortcut);
			}
		}
		
		public function ShowImpulseBuff(path:String):void
		{
			mainWindow.ShowImpulseBuff(path);
			
		}
		
		public function ResetImpulsBuff(path:String):void
		{
			mainWindow.impulsShop_mc.ResetImpulsBuffIcon(path);
		}
		
		public function HideImpulseBuff(wasBought:Boolean, fromKeyboard:Boolean):void
		{
			mainWindow.impulsShop_mc.HideImpulseBuff(wasBought, fromKeyboard);
		}
		
		public function HideAllWindows():void 
		{
		//	main.HideAllWindows();
		}
		
		public function OnEscape():Boolean
		{
			return mainWindow.OnEscapeBtnClick();
		}
		
		public function OpenEscMenu():void 
		{
			mainWindow.OpenEscMenu();
		}
		
		public function EnterMinigame():void 
		{
			mainWindow.MiniGameOpen(true);
		}
		
		public function LeaveMinigame():void
		{
			mainWindow.MiniGameOpen(false);
		}
		public function GameEnd(isVictory:Boolean, screen:String, dontShowStatistics:Boolean, dontShowDialog:Boolean):void
		{
			var player:PlayerStatisticData = GetPlayer(ourID);
			
			var gameEndFraction:int = 0
			
			if (player != null)
				gameEndFraction = player.OriginalFraction;
			
			mainWindow.GameEnd(gameEndFraction, isVictory, screen, dontShowStatistics, dontShowDialog);
		}
		
		public function ConfigurePlaySameTeamFeature(available:Boolean, timeOut:int):void
		{
			mainWindow.ConfigurePlaySameTeamFeature(available, timeOut);
		}
		
		public function HandlePlaySameTeamDecision(id:int, agreed:Boolean):void
		{
			mainWindow.HandlePlaySameTeamDecision(id, agreed);
		}
		
		public function SetBaseStat(isSelection:Boolean,valueType:int, value:int,addition:int,isImportant:Boolean):void
		{
			mainWindow.SetBaseStat(isSelection, valueType, value, addition,isImportant);
		}
		
		public function SetDerivativeStat(isSelection:Boolean,valueType:int, value:int):void
		{
			mainWindow.SetDerivativeStat(isSelection,valueType, value)
		}
				
		public function SetCharStatUnit(isSelection:Boolean, heroID:int,iconPath:String, heroName:String, fraction:int, damageType:int):void
		{
			mainWindow.SetCharStatUnit(isSelection, heroID, iconPath, heroName, fraction, damageType);
			
			if (isSelection)
				selectionCharstatUnitID = heroID;
			
			var playerData:PlayerStatisticData = GetPlayer(heroID);
			
			if (playerData != null)
				mainWindow.selectionCharStats_mc.SetDeathTimeOut(playerData.TimeToRessurect);
			else
				mainWindow.selectionCharStats_mc.SetDeathTimeOut(0);
		}
		
		public function SetCharStatUnitParams(isSelection:Boolean, heroId:int, level:int,curHealth:int,maxHealth:int,curMana:int,maxMana:int,isVisible:Boolean):void
		{
			var player:PlayerStatisticData = GetPlayer(heroId);
			var levelToShow:int = (player == null||player.isVisible||heroId==ourID)? level:player.LastVisibleLevel;
			
			if ( selectedTalentBarHeroId == heroId )
			{
				mainWindow.talentsBarContext_mc.Level = levelToShow;
			}
			mainWindow.SetCharStatUnitParams(isSelection, levelToShow, curHealth, maxHealth, curMana, maxMana);
			mainWindow.selectionCharStats_mc.SetGreyMask(!isVisible);
		}
		
		
		public function ShowHideWindow(winNum:int):void
		{
			mainWindow.ShowHideWindow(winNum);
		}
		
		public function SetBuffIcon(isSelection:Boolean, path:String, index:int, isPositive:Boolean):void
		{
			mainWindow.SetBuffIcon(isSelection, path, index, isPositive);
		}
		
		public function SetBuffStatus(isSelection:Boolean, index:int, cooldown:Number, count:int ):void
		{
			mainWindow.SetBuffStatus(isSelection, index, cooldown, count )
		}

		public function ShowFPS(show:Boolean):void
		{
			mainWindow.ShowFPS(show);
		}
		
		public function SetFinalHeroTitle(heroID:int, heroTitle:String, heroTitleIcon:String,tooltipID:int):void
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			if (player == null) return; 
			
			player.TitleIcon = heroTitleIcon;
			player.TitleName = heroTitle;
			player.TitleTooltipID = tooltipID;
		}
		
		public function AddOurHeroAward(awardID:int,awardIcon:String,awardCount:int, isFirstWin:Boolean = false,buyPremium:Boolean=false):void
		{
			var player:PlayerStatisticData = GetPlayer(ourID);
			
			if (player == null) return;
			
			if (buyPremium)
				player.AddPremiumAwards(awardID, awardIcon, awardCount);
			else
				player.AddAwards(awardID, awardIcon, awardCount, isFirstWin);
			
		}
		
		public function StartAwarding():void
		{
			var player:PlayerStatisticData = GetPlayer(ourID);
			
			if (player == null) return;
			
			//clear array
			player.Awards.splice(0, player.Awards.length);	
			player.PremiumAwards.splice(0, player.PremiumAwards.length);
			
			mainWindow.finalStat.RemoveAwardsIcons();
		}
		
		public function FinishAwarding():void
		{
			var index:int = GetPlayerIndex(ourID);
			
			if (index == -1) return;
		
			mainWindow.finalStat.FinishAwarding(index);
		}
		
		
		public function AddHeroExploit(heroID:int,exploitIcon:String, tooltipID:int):void
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			if (player == null) return;
			player.AddHeroExploit(exploitIcon);
			player.AddHeroExploitTooltipID(tooltipID)
		}
		
		public function AddHeroResults(heroID:int,resultIcon:String,resultCount:int,resultName:String):void
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			if (player == null) return;
			player.AddHeroResult(resultName, resultCount, resultIcon);
		}
		
		public function SetErrorMessage(errorID:int, msg:String, playSound:Boolean, lifeTime:Number):void
		{
			mainWindow.SetErrorMessage(errorID, msg, playSound, lifeTime)		
		}
		
		public function ShowErrorMessage(errorID:int, show:Boolean):void
		{
			mainWindow.ShowErrorMessage(errorID, show);
		} 
		
		public function ShowErrorEvent(mes:String, playSound:Boolean, lifeTime:Number):void
		{
			mainWindow.ShowErrorEvent(mes, playSound, lifeTime);
		}
		
		public function ShowAnnouncement(announcementText:String, lifeTime:int,priority:int):void
		{
			mainWindow.ShowAnnouncement(announcementText,lifeTime,priority);
		}
		
		public function SetAnnouncementPart(right:Boolean, portraitIcon:String, level:int, name:String,fraction:int,heroID:int = -1):void
		{
			var playerData:PlayerStatisticData = GetPlayer(heroID);
			
			if (playerData!=null)
			{
				mainWindow.SetAnnouncementPart(right, portraitIcon, playerData.LastVisibleLevel, name, fraction);
				return;
			}			
			
			mainWindow.SetAnnouncementPart(right, portraitIcon, level, name, fraction);
		}
		
		public function PlayerLeft(heroID:int, leaveType:int = 0)
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			
			if (player == null) 
			{
				player = new PlayerStatisticData();
				mainWindow.Players.push(player);
			}
			
			player.IsHeroLeft = true;
			player.LeaveType = leaveType;
			
			var mateInfo:MateInfoBase = GetMateInfo(heroID);
			
			if (mateInfo == null)	return;
			
			mateInfo.OnPlayerLeft(leaveType);
			
			mainWindow.statisticsBar_mc.allStats_mc.OnHeroLeft(heroID,leaveType);
		}
		
		public function PlayerRecconected(id:int):void
		{
			var player:PlayerStatisticData = GetPlayer(id);
			
			if (player == null)
			{
				player = new PlayerStatisticData();
				mainWindow.Players.push(player);
			}
			
			player.IsHeroLeft = false;
			
			var mateInfo:MateInfoBase = GetMateInfo(id);
			
			if (mateInfo == null)	return;
			
			mateInfo.OnPlayerReturn();
			
			mainWindow.statisticsBar_mc.allStats_mc.OnHeroReturn(id);
		}
		
		public function ShowSpawnCreepsAnnouncement(leftIcon:String, rightIcon:String, announcementText:String):void
		{
			mainWindow.announcement_mc.ShowSpawnCreepsAnnouncement(leftIcon, rightIcon, announcementText);
		}
		
		public function AddChannel(channel:int, channelName:String, channelColor:uint, showChannelName:Boolean,showPlayerName:Boolean,playerCanWriteToChannel:Boolean, canSendPrivateMessage:Boolean = false, channelType:int=0):void
		{
			mainWindow.chatBar.AddChannel(channel, channelName, channelColor, showChannelName, showPlayerName,playerCanWriteToChannel,channelType);
		}
		
		public function AddMessage(channel:int, playerName:String, message:String):void
		{
			mainWindow.chatBar.AddMessage(channel, playerName, message);
		}
		
		public function AddMessageEx(channel:int, playerName:String, message:String, playerId:int):void
		{
			mainWindow.chatBar.AddMessage(channel, playerName, message, playerId);
		}
		
		public function SetDefaultChannel(channelID:int):void
		{
			mainWindow.chatBar.SetDefaultChannel(channelID);
		}
		
		public function SetPlayerIcon(playerId:int, path:String):void
		{
			mainWindow.chatBar.SetPlayerIcon(playerId, path);
		}
		
		public function SetPlayerHeroId(playerId:int, heroId:int, teamId:int):void
		{
			mainWindow.chatBar.SetPlayerHeroId(playerId, heroId, teamId);
		}
		
		public function EnterPressed():void
		{
			
			if (mainWindow.reportWindow.IsInFocus())
			{
				mainWindow.reportWindow.OnEnterPressed();
				return;
			}
			
			mainWindow.chatBar.EnterPressed();
		}
		
		public function OpenChanel(channelID:int):void 
		{
			mainWindow.chatBar.OpenChannel(channelID);
		}
		
		public function AddChannelShortCut(channel:int, shortcut:String):void
		{
			mainWindow.chatBar.AddChannelShortCut(channel, shortcut);
		}
		
		public function ShowVoteWindow(timeout:int, isAlreadyConceded:Boolean):void
		{
			mainWindow.voting_mc.ShowVoteWindow(timeout, isAlreadyConceded);
		}
		
		public function HideVoteWindow():void
		{
			mainWindow.voting_mc.HideVoteWindow();
		}
		
		public function SetSurrenderVoteStatus(concededCount:int, declinedCount:int, totalCount:int):void
		{
			mainWindow.voting_mc.SetSurrenderVoteStatus(concededCount, declinedCount, totalCount);
		}
		
		public function ShowTryExit():void
		{
			var isGameWithBots:Boolean;
			
			for each (var item:PlayerStatisticData in mainWindow.Players) 
			{
				if (item.IsBot)
				{
					isGameWithBots = true;
					break;
				}
			}
			
			mainWindow.ShowExitGameDialog(isGameWithBots,spectatorMode);
		}
		
		public function SetStatisticExperienceBar(curExp:int,earnedExp:int,levelsTabel:Array,premiumExp:int = 0):void
		{
			mainWindow.finalStat.personalStat.SetStatisticExperienceBar(curExp, earnedExp,premiumExp, levelsTabel);
		
		}
		
		private function GetPingLayer() : Sprite
		{
			if(this.pingLayer == null)
			{
				this.pingLayer = new Sprite();
				this.pingLayer.mouseEnabled = false;
				this.pingLayer.mouseChildren = false;
				this.pingMarkers = new Object();
				this.mainWindow.addChild(this.pingLayer);
			}
			this.mainWindow.setChildIndex(this.pingLayer,this.mainWindow.numChildren - 1);
			return this.pingLayer;
		}
		
		private function CreatePingLabel(param1:String, param2:uint) : ShortcutItem
		{
			var _loc3_:ShortcutItem = new ShortcutItem();
			_loc3_.SetName(param1);
			_loc3_.SetBind("");
			_loc3_.mouseEnabled = false;
			_loc3_.mouseChildren = false;
			_loc3_.valueFrame.visible = false;
			_loc3_.value.visible = false;
			_loc3_.text.textColor = param2;
			return _loc3_;
		}
		
		// textures: Data/UI/Styles/Icons/PingWheel (wheel 256x256, icons 64x64)
		private static const PING_TEX:String = "/UI/Styles/Icons/PingWheel/";
		
		private static const PING_ICON_SCALE:Number = 0.72;
		
		private static const PING_SLOT_RADIUS:Number = 84;
		
		private function LoadPingImage(param1:String) : Loader
		{
			var _loc2_:Loader = new Loader();
			_loc2_.mouseEnabled = false;
			_loc2_.load(new URLRequest(MainInterface.PING_TEX + param1));
			return _loc2_;
		}
		
		private function PlacePingIcon(param1:Loader, param2:int, param3:Number) : void
		{
			var _loc4_:Number = (-90 + param2 * 72) * Math.PI / 180;
			param1.scaleX = param3;
			param1.scaleY = param3;
			param1.x = Math.floor(Math.cos(_loc4_) * MainInterface.PING_SLOT_RADIUS - 32 * param3);
			param1.y = Math.floor(Math.sin(_loc4_) * MainInterface.PING_SLOT_RADIUS - 32 * param3);
		}
		
		private function CreatePingWheel() : void
		{
			var _loc1_:int = 0;
			var _loc2_:Loader = null;
			this.pingWheel = new Sprite();
			this.pingWheel.mouseEnabled = false;
			this.pingWheel.mouseChildren = false;
			_loc2_ = this.LoadPingImage("wheel_bg.dds");
			_loc2_.x = -128;
			_loc2_.y = -128;
			this.pingWheel.addChild(_loc2_);
			this.pingWheelBg = _loc2_;
			this.pingWheelHighlights = [];
			this.pingWheelItems = [];
			_loc1_ = 0;
			while(_loc1_ < 5)
			{
				_loc2_ = this.LoadPingImage("wheel_hl" + _loc1_ + ".dds");
				_loc2_.x = -128;
				_loc2_.y = -128;
				_loc2_.visible = false;
				this.pingWheel.addChild(_loc2_);
				this.pingWheelHighlights.push(_loc2_);
				_loc1_++;
			}
			_loc1_ = 0;
			while(_loc1_ < 5)
			{
				_loc2_ = this.LoadPingImage("ping_icon" + _loc1_ + ".dds");
				this.PlacePingIcon(_loc2_,_loc1_,MainInterface.PING_ICON_SCALE);
				this.pingWheel.addChild(_loc2_);
				this.pingWheelItems.push(_loc2_);
				_loc1_++;
			}
			this.pingWheelText = this.CreatePingLabel("",16777215);
			this.pingWheel.addChild(this.pingWheelText);
			this.pingWheel.visible = false;
			this.GetPingLayer().addChild(this.pingWheel);
		}
		
		// called by C++ while the map is loading: textures must not be loaded after the game start
		public function PreloadPingWheel() : void
		{
			var _loc1_:int = 0;
			var _loc2_:Loader = null;
			var _loc3_:Sprite = this.GetPingLayer();
			if(this.pingWheel == null)
			{
				this.CreatePingWheel();
			}
			if(this.pingPreloaded == null)
			{
				// marker textures: beams of all ping types + the icon of the ordinary ping (kept hidden, only to load them now)
				this.pingPreloaded = [];
				_loc1_ = 0;
				while(_loc1_ < 6)
				{
					_loc2_ = this.LoadPingImage("ping_beam" + _loc1_ + ".dds");
					_loc2_.visible = false;
					_loc3_.addChild(_loc2_);
					this.pingPreloaded.push(_loc2_);
					_loc1_++;
				}
				_loc2_ = this.LoadPingImage("ping_icon5.dds");
				_loc2_.visible = false;
				_loc3_.addChild(_loc2_);
				this.pingPreloaded.push(_loc2_);
				this.PreloadPingImage(MainInterface.SMARTCAST_TEX + "smartcast_off.dds");
				this.PreloadPingImage(MainInterface.SMARTCAST_TEX + "smartcast_on.dds");
				this.PreloadPingImage(MainInterface.PRAISE_TEX + "praise_bg.dds");
				this.PreloadPingImage(MainInterface.PRAISE_TEX + "prime_drop.dds");
			}
		}
		
		// nx, ny: 0..1 of the screen; labels and colors (hex RRGGBB) separated by '|', item 0 at the top, clockwise;
		// bgAlpha: background opacity 0..1, scale: wheel size (1 = 256 px)
		public function ShowPingWheel(param1:Number, param2:Number, param3:String, param4:String, param5:Number, param6:Number) : void
		{
			var _loc5_:int = 0;
			var _loc6_:Array = null;
			this.GetPingLayer();
			if(this.pingWheel == null)
			{
				this.CreatePingWheel();
			}
			this.pingWheelLabels = param3.split("|");
			_loc6_ = param4.split("|");
			this.pingWheelColors = [];
			_loc5_ = 0;
			while(_loc5_ < _loc6_.length)
			{
				this.pingWheelColors.push(uint(parseInt(_loc6_[_loc5_],16)));
				_loc5_++;
			}
			this.pingWheel.x = Math.floor(param1 * this.mainWindow.stage.stageWidth);
			this.pingWheel.y = Math.floor(param2 * this.mainWindow.stage.stageHeight);
			if(this.pingWheelBg != null)
			{
				this.pingWheelBg.alpha = param5;
			}
			this.pingWheel.scaleX = param6;
			this.pingWheel.scaleY = param6;
			this.HighlightPingWheel(-1);
			this.pingWheel.visible = true;
		}
		
		// highlighted sector + its name under the wheel; -1 = nothing
		public function HighlightPingWheel(param1:int) : void
		{
			var _loc2_:int = 0;
			var _loc3_:Number = NaN;
			var _loc4_:Boolean = false;
			if(this.pingWheel == null)
			{
				return;
			}
			_loc2_ = 0;
			while(_loc2_ < this.pingWheelItems.length)
			{
				_loc4_ = false;
				if(_loc2_ == param1)
				{
					_loc4_ = true;
				}
				Loader(this.pingWheelHighlights[_loc2_]).visible = _loc4_;
				_loc3_ = MainInterface.PING_ICON_SCALE;
				if(_loc4_)
				{
					_loc3_ = MainInterface.PING_ICON_SCALE * 1.18;
				}
				this.PlacePingIcon(Loader(this.pingWheelItems[_loc2_]),_loc2_,_loc3_);
				_loc2_++;
			}
			if(param1 < 0 || this.pingWheelLabels == null || param1 >= this.pingWheelLabels.length)
			{
				this.pingWheelText.visible = false;
				return;
			}
			this.pingWheelText.SetName(String(this.pingWheelLabels[param1]));
			this.pingWheelText.text.textColor = uint(this.pingWheelColors[param1]);
			this.pingWheelText.x = Math.floor(0 - this.pingWheelText.text.x - this.pingWheelText.text.textWidth / 2);
			this.pingWheelText.y = Math.floor(136 - this.pingWheelText.text.y);
			this.pingWheelText.visible = true;
		}
		
		public function HidePingWheel() : void
		{
			if(this.pingWheel != null)
			{
				this.pingWheel.visible = false;
			}
		}
		
		// loads a texture now (absolute path, e.g. a hero minimap icon): files must not be loaded after the game start
		public function PreloadPingImage(param1:String) : void
		{
			var _loc2_:Sprite = this.GetPingLayer();
			var _loc3_:Loader = null;
			if(param1 == null || param1.length == 0)
			{
				return;
			}
			if(this.pingPreloaded == null)
			{
				this.pingPreloaded = [];
			}
			_loc3_ = new Loader();
			_loc3_.mouseEnabled = false;
			_loc3_.visible = false;
			_loc3_.load(new URLRequest(param1));
			_loc2_.addChild(_loc3_);
			this.pingPreloaded.push(_loc3_);
		}
		
		// hero icon (path) shown above the marker id; called before the first SetPingMarker of that id
		public function SetPingMarkerHero(param1:int, param2:String) : void
		{
			if(this.pingMarkerHeroes == null)
			{
				this.pingMarkerHeroes = new Object();
			}
			this.pingMarkerHeroes[String(param1)] = param2;
		}
		
		// ping marker: light beam standing on the ground point (gx, gy) + icon (+ text) at its top (nx, ny); all 0..1 of the screen;
		// alpha <= 0 removes it
		public function SetPingMarker(param1:int, param2:int, param3:String, param4:uint, param5:Number, param6:Number, param7:Number, param8:Number, param9:Number) : void
		{
			var _loc10_:Sprite = this.GetPingLayer();
			var _loc11_:String = String(param1);
			var _loc12_:Sprite = this.pingMarkers[_loc11_] as Sprite;
			var _loc13_:Loader = null;
			var _loc14_:Loader = null;
			var _loc15_:ShortcutItem = null;
			var _loc16_:Number = 0;
			var _loc17_:Number = 0;
			var _loc18_:Number = 0;
			var _loc19_:Number = 0;
			var _loc20_:Number = 0;
			var _loc21_:Loader = null;
			var _loc22_:String = "";
			var _loc23_:Number = 0;
			var _loc24_:Number = 0;
			if(param9 <= 0)
			{
				if(_loc12_ != null)
				{
					if(_loc12_.parent != null)
					{
						_loc12_.parent.removeChild(_loc12_);
					}
					delete this.pingMarkers[_loc11_];
				}
				if(this.pingMarkerHeroes != null)
				{
					delete this.pingMarkerHeroes[_loc11_];
				}
				return;
			}
			if(_loc12_ == null)
			{
				_loc12_ = new Sprite();
				_loc12_.mouseEnabled = false;
				_loc12_.mouseChildren = false;
				_loc13_ = this.LoadPingImage("ping_beam" + param2 + ".dds");
				_loc12_.addChild(_loc13_);
				_loc14_ = this.LoadPingImage("ping_icon" + param2 + ".dds");
				_loc14_.scaleX = 0.75;
				_loc14_.scaleY = 0.75;
				_loc12_.addChild(_loc14_);
				if(this.pingMarkerHeroes != null && this.pingMarkerHeroes[_loc11_] != null)
				{
					_loc22_ = String(this.pingMarkerHeroes[_loc11_]);
				}
				_loc21_ = new Loader();
				_loc21_.mouseEnabled = false;
				if(_loc22_.length > 0)
				{
					_loc21_.load(new URLRequest(_loc22_));
				}
				else
				{
					_loc21_.visible = false;
				}
				_loc12_.addChild(_loc21_);
				if(param3.length > 0)
				{
					_loc15_ = this.CreatePingLabel(param3,param4);
					_loc12_.addChild(_loc15_);
				}
				_loc10_.addChild(_loc12_);
				this.pingMarkers[_loc11_] = _loc12_;
			}
			_loc13_ = Loader(_loc12_.getChildAt(0));
			_loc14_ = Loader(_loc12_.getChildAt(1));
			_loc21_ = Loader(_loc12_.getChildAt(2));
			_loc16_ = Math.floor(param7 * this.mainWindow.stage.stageWidth);
			_loc17_ = Math.floor(param8 * this.mainWindow.stage.stageHeight);
			_loc18_ = Math.floor(param6 * this.mainWindow.stage.stageHeight);
			_loc19_ = _loc17_ - _loc18_;
			_loc12_.x = _loc16_;
			_loc12_.y = _loc17_;
			if(_loc19_ > 6)
			{
				_loc13_.visible = true;
				_loc13_.x = -16;
				_loc13_.y = 0 - _loc19_;
				_loc13_.scaleY = _loc19_ / 128;
			}
			else
			{
				_loc13_.visible = false;
			}
			_loc20_ = 0 - _loc19_ - 40;
			_loc14_.x = -24;
			_loc14_.y = _loc20_;
			// hero icon above the ping icon, ~28 px whatever the texture size is
			_loc24_ = _loc20_;
			if(_loc21_.visible)
			{
				_loc23_ = 0;
				if(_loc21_.scaleX > 0)
				{
					_loc23_ = _loc21_.width / _loc21_.scaleX;
				}
				if(!(_loc23_ > 1))
				{
					_loc23_ = 20;
				}
				_loc21_.scaleX = 28 / _loc23_;
				_loc21_.scaleY = 28 / _loc23_;
				_loc21_.x = -14;
				_loc21_.y = _loc20_ - 30;
				_loc24_ = _loc20_ - 30;
			}
			if(_loc12_.numChildren > 3)
			{
				_loc15_ = ShortcutItem(_loc12_.getChildAt(3));
				_loc15_.x = Math.floor(0 - _loc15_.text.x - _loc15_.text.textWidth / 2);
				_loc15_.y = Math.floor(_loc24_ - 20 - _loc15_.text.y);
			}
			_loc12_.alpha = param9;
		}
		
		// the old sharp minimap alert sound of pings (off while the new ping sounds are on)
		public function SetMinimapAlertSound(param1:Boolean) : void
		{
			AttentionEffect.soundEnabled = param1;
		}
		
		// ---------------------------------------------------------------- smart-cast checkboxes on the action bar buttons
		private static const SMARTCAST_TEX:String = "/UI/Styles/Icons/SmartCast/";
		
		// mode of the talent (column = slot, raw = level): 0 no checkbox, 1 off, 2 on
		public function SetTalentSmartCast(param1:int, param2:int, param3:int) : void
		{
			if(this.smartCastModes == null)
			{
				this.smartCastModes = new Object();
			}
			this.smartCastModes[param1.toString() + " " + param2.toString()] = param3;
		}
		
		private function CreateSmartCastBox() : Sprite
		{
			var _loc1_:Sprite = new Sprite();
			var _loc2_:Loader = null;
			_loc1_.mouseEnabled = false;
			_loc1_.mouseChildren = false;
			_loc2_ = new Loader();
			_loc2_.mouseEnabled = false;
			_loc2_.load(new URLRequest(MainInterface.SMARTCAST_TEX + "smartcast_off.dds"));
			_loc2_.scaleX = ActionButton.SMART_BOX_SIZE / 32;
			_loc2_.scaleY = ActionButton.SMART_BOX_SIZE / 32;
			_loc1_.addChild(_loc2_);
			_loc2_ = new Loader();
			_loc2_.mouseEnabled = false;
			_loc2_.load(new URLRequest(MainInterface.SMARTCAST_TEX + "smartcast_on.dds"));
			_loc2_.scaleX = ActionButton.SMART_BOX_SIZE / 32;
			_loc2_.scaleY = ActionButton.SMART_BOX_SIZE / 32;
			_loc1_.addChild(_loc2_);
			_loc1_.x = ActionButton.SMART_BOX_X;
			_loc1_.y = ActionButton.SMART_BOX_Y;
			return _loc1_;
		}
		
		// puts the checkboxes on the buttons that hold talents now (talents can be dragged between the buttons)
		public function RefreshSmartCastBoxes() : void
		{
			var _loc1_:int = 0;
			var _loc2_:ActionButton = null;
			var _loc3_:String = null;
			var _loc4_:int = 0;
			var _loc5_:Sprite = null;
			var _loc6_:TalentShortCut = null;
			var _loc7_:Talent = null;
			_loc1_ = 0;
			while(_loc1_ < 24)
			{
				_loc2_ = this.mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(_loc1_) as ActionButton;
				if(_loc2_ != null)
				{
					_loc3_ = null;
					_loc4_ = 0;
					_loc6_ = _loc2_.Shortcut as TalentShortCut;
					if(_loc6_ != null)
					{
						_loc7_ = _loc6_.TalentObj;
						if(_loc7_ != null)
						{
							_loc3_ = _loc7_.column.toString() + " " + _loc7_.raw.toString();
							if(this.smartCastModes != null && this.smartCastModes[_loc3_] != null)
							{
								_loc4_ = int(this.smartCastModes[_loc3_]);
							}
						}
					}
					_loc5_ = ActionButton.smartBoxes[String(_loc1_)] as Sprite;
					if(_loc4_ <= 0)
					{
						if(_loc5_ != null)
						{
							_loc5_.visible = false;
						}
					}
					else
					{
						if(_loc5_ == null)
						{
							_loc5_ = this.CreateSmartCastBox();
							_loc2_.addChild(_loc5_);
							ActionButton.smartBoxes[String(_loc1_)] = _loc5_;
						}
						_loc2_.setChildIndex(_loc5_,_loc2_.numChildren - 1);
						ActionButton.smartKeys[String(_loc1_)] = _loc3_;
						_loc5_.getChildAt(0).visible = _loc4_ == 1;
						_loc5_.getChildAt(1).visible = _loc4_ == 2;
						_loc5_.visible = true;
					}
				}
				_loc1_++;
			}
		}
		
		// ---------------------------------------------------------------- praise notifications (right edge of the screen)
		private static const PRAISE_TEX:String = "/UI/Styles/Icons/Praise/";
		
		private static const PRAISE_W:Number = 320;
		
		private static const PRAISE_H:Number = 100;
		
		private static const PRAISE_PORTRAIT:Number = 60;
		
		private function CreatePraisePortrait(param1:String, param2:Number) : Loader
		{
			var _loc3_:Loader = new Loader();
			_loc3_.mouseEnabled = false;
			_loc3_.x = param2;
			_loc3_.y = 10;
			if(param1 != null && param1.length > 0)
			{
				_loc3_.load(new URLRequest(param1));
			}
			else
			{
				_loc3_.visible = false;
			}
			return _loc3_;
		}
		
		// portrait textures may have any size: fit them into the 60x60 slot
		private function FitPraisePortrait(param1:Loader) : void
		{
			var _loc2_:Number = 0;
			if(!param1.visible)
			{
				return;
			}
			if(param1.scaleX > 0)
			{
				_loc2_ = param1.width / param1.scaleX;
			}
			if(!(_loc2_ > 1))
			{
				_loc2_ = 128;
			}
			param1.scaleX = MainInterface.PRAISE_PORTRAIT / _loc2_;
			param1.scaleY = MainInterface.PRAISE_PORTRAIT / _loc2_;
		}
		
		private function PlacePraiseLabel(param1:ShortcutItem, param2:Number, param3:Number) : void
		{
			param1.x = Math.floor(param2 - param1.text.x - param1.text.textWidth / 2);
			param1.y = Math.floor(param3 - param1.text.y);
		}
		
		// children: 0 bg, 1 sender portrait, 2 target portrait, 3 title, 4 prime drop, 5 amount, 6 sender name, 7 target name
		public function ShowPraise(param1:int, param2:String, param3:String, param4:uint, param5:String, param6:String, param7:uint, param8:int) : void
		{
			var _loc9_:Sprite = this.GetPingLayer();
			var _loc10_:Sprite = null;
			var _loc11_:Loader = null;
			var _loc12_:ShortcutItem = null;
			var _loc13_:ShortcutItem = null;
			var _loc14_:Number = 0;
			var _loc15_:Number = 0;
			if(this.praisePanels == null)
			{
				this.praisePanels = new Object();
			}
			if(this.praisePanels[String(param1)] != null)
			{
				return;
			}
			_loc10_ = new Sprite();
			_loc10_.mouseEnabled = false;
			_loc10_.mouseChildren = false;
			_loc11_ = new Loader();
			_loc11_.mouseEnabled = false;
			_loc11_.load(new URLRequest(MainInterface.PRAISE_TEX + "praise_bg.dds"));
			_loc11_.scaleX = 0.625;
			_loc11_.scaleY = 0.625;
			_loc10_.addChild(_loc11_);
			_loc10_.addChild(this.CreatePraisePortrait(param2,12));
			_loc10_.addChild(this.CreatePraisePortrait(param5,248));
			_loc12_ = this.CreatePingLabel("ХВАЛИТ",15784080);
			_loc10_.addChild(_loc12_);
			this.PlacePraiseLabel(_loc12_,160,14);
			_loc11_ = new Loader();
			_loc11_.mouseEnabled = false;
			_loc11_.load(new URLRequest(MainInterface.PRAISE_TEX + "prime_drop.dds"));
			_loc11_.scaleX = 0.42;
			_loc11_.scaleY = 0.42;
			_loc10_.addChild(_loc11_);
			_loc13_ = this.CreatePingLabel("+" + String(param8),8382664);
			_loc10_.addChild(_loc13_);
			_loc14_ = 27 + 3 + _loc13_.text.textWidth;
			_loc15_ = Math.floor(160 - _loc14_ / 2);
			_loc11_.x = _loc15_;
			_loc11_.y = 34;
			_loc13_.x = Math.floor(_loc15_ + 30 - _loc13_.text.x);
			_loc13_.y = Math.floor(39 - _loc13_.text.y);
			_loc12_ = this.CreatePingLabel(param3,param4);
			_loc10_.addChild(_loc12_);
			this.PlacePraiseLabel(_loc12_,42,74);
			_loc12_ = this.CreatePingLabel(param6,param7);
			_loc10_.addChild(_loc12_);
			this.PlacePraiseLabel(_loc12_,278,74);
			_loc10_.visible = false;
			_loc9_.addChild(_loc10_);
			this.praisePanels[String(param1)] = _loc10_;
		}
		
		// slide: 0 = in place, 1 = hidden behind the right edge; row: 0 = top panel; top: part of the stage height; alpha <= 0 removes
		public function SetPraisePanel(param1:int, param2:Number, param3:Number, param4:Number, param5:Number) : void
		{
			var _loc6_:String = String(param1);
			var _loc7_:Sprite = null;
			if(this.praisePanels == null)
			{
				return;
			}
			_loc7_ = this.praisePanels[_loc6_] as Sprite;
			if(_loc7_ == null)
			{
				return;
			}
			if(param4 <= 0)
			{
				if(_loc7_.parent != null)
				{
					_loc7_.parent.removeChild(_loc7_);
				}
				delete this.praisePanels[_loc6_];
				return;
			}
			this.FitPraisePortrait(Loader(_loc7_.getChildAt(1)));
			this.FitPraisePortrait(Loader(_loc7_.getChildAt(2)));
			_loc7_.x = Math.floor(this.mainWindow.stage.stageWidth - MainInterface.PRAISE_W - 4 + param2 * (MainInterface.PRAISE_W + 12));
			_loc7_.y = Math.floor(this.mainWindow.stage.stageHeight * param5 + param3 * (MainInterface.PRAISE_H + 6));
			_loc7_.alpha = param4;
			_loc7_.visible = true;
		}
		
		public function SetChatCentered(param1:Boolean, param2:Number) : void
		{
			this.mainWindow.chatBar.SetCentered(param1,param2);
		}
		
		// Размер панелей портретов команд (настройки «Дополнительно», team_portraits_scale), 1 = исходный.
		public function SetTeamPortraitsScale(scale:Number):void
		{
			mainWindow.teamInfoFrendly_mc.SetSizeBoost(scale);
			mainWindow.teamInfoEnemy_mc.SetSizeBoost(scale);
		}

		public function SetChatVisible(value:Boolean):void
		{
			mainWindow.chatBar.DisableInput(!value);
			mainWindow.chatBar.visible = value;
		}
		
		public function SetCameraMode(cameraOn:Boolean):void
		{
			mainWindow.miniMap_mc.miniMapAnim_mc.SetCameraMode(cameraOn);
		}
		
		public function AddFriend (friendName:String):void
		{
			mainWindow.chatBar.AddFriend(friendName);
		}
		
		public function SetMinimapEffect(index:int, effectNum:int):void
		{
			mainWindow.miniMap_mc.miniMapAnim_mc.SetMinimapEffect(index, effectNum);
		}
		
		public function UpdateMinimapEffect(index:int, posX:Number, posY:Number):void
		{
			mainWindow.miniMap_mc.miniMapAnim_mc.UpdateMinimapEffect(index, posX, posY)
		}
		public function IgnoreUser(playerId:int):void
		{
			SetIgnoring(playerId, true);
				
			var player:PlayerStatisticData = GetPlayer(playerId);
			
			if (player != null)
			{
				mainWindow.chatBar.AddIgnoringUser(playerId, player.PlayerName);
			}
			else
			{
				mainWindow.chatBar.AddIgnoringUser(playerId);
			}
		}
		
		public function RemoveIgnore(playerId:int):void
		{
			SetIgnoring(playerId, false);
			
			var player:PlayerStatisticData = GetPlayer(playerId);
			
			if (player != null)
			{
				mainWindow.chatBar.RemoveIgnoringUser(playerId, player.PlayerName);
			}
			else
			{
				mainWindow.chatBar.RemoveIgnoringUser(playerId);
			}
		}
		
		private function SetIgnoring(id:int,ignore:Boolean):void
		{
			if (id < 0)
				return;
				
			var player:PlayerStatisticData = GetPlayer(id);
			
			if (player == null)
				return;
			
			player.IsIgnoring = ignore;
				
			var mateInfo:MateInfoBase = GetMateInfo(id);
			if (mateInfo != null) 
			{
				mateInfo.isHeroIgnored = ignore;
				if (mateInfo.IsSelected())
					mainWindow.selectionInfo_mc.selectionInfoHero_mc.isHeroIgnored = ignore;
			}
			
			mainWindow.statisticsBar_mc.allStats_mc.SetIgnoreStatus(id, ignore);
		}
		
		public function ShowUIBlock(id:String, show:Boolean):void 
		{
			if (mainWindow.ProcessSpecialUiBlock(id, show)) 
				return;
				
			if (mainWindow.TutorialBlocks[id] == null) return;
				
			mainWindow.TutorialBlocks[id].ShowElements(show);
		}
		
		public function ShowAllUIBlocks(show:Boolean):void
		{
			for each (var item:TutorialShower in mainWindow.TutorialBlocks) 
			{
				item.ShowElements(show);
			}			
			
			mainWindow.ProcessSpecialUiBlock(ETutorialBlocks.TalentsSetBlock, show)
			mainWindow.ProcessSpecialUiBlock(ETutorialBlocks.Buffs, show)
		}
		
		public function ShowNPC(iconPath:String, name:String):void
		{
			var bottomMate:HeroMateInfo = mainWindow.heroInfo_mc.heroInfoAnim_mc;
			
			bottomMate.ShowNPC(iconPath, name);
		}
		
		public function HideNPC():void
		{
			mainWindow.heroInfo_mc.heroInfoAnim_mc.HideNPC();
		}
		
		public function StartCinematic():void // по диз доку. убираем весь интерфейс. Выдвигаем каше
		{
			for (var i:int = 0; i < mainWindow.numChildren; i++ )
			{
				var showDialogObject:BaseResizableObject = mainWindow.getChildAt(i) as BaseResizableObject;
				
				if (showDialogObject == null)
					continue;
					
				showDialogObject.ShowCinematicDialog();
			}
			
		} 
		
		public function StopCinematic():void // по диз доку. показываем весь интерфейс. Задвигаем каше
		{
			for (var i:int = 0; i < mainWindow.numChildren; i++ )
			{
				var showDialogObject:BaseResizableObject = mainWindow.getChildAt(i) as BaseResizableObject;
				
				if (showDialogObject == null)
					continue;
					
				showDialogObject.HideCinematicDialog();
			}
		}
		
		public function ShowCinematicNPC( icon: String, name: String ):void // показывает портрет NPC
		{
			mainWindow.NPCLineDown_mc.ShowCinematicNPC(icon, name);
		} 
		
		public function ShowCinematicMessage( text: String, color:uint = 0xF9D87A ):void
		{
			mainWindow.NPCLineDown_mc.ShowCinematicMessage(text,color);
		}
		
		public function ShowMessageBox( header : String, text : String, isOk:Boolean = false ) :void
		{
			mainWindow.ShowMessageBox(header,text, isOk);
		}		
		
		public function ShowTutorialTalentHighlight( column:int,raw:int,show ):void
		{
			var talent:Talent = mainWindow.talentsBar_mc.GetTalent(column, raw);
			if (talent == null) return;
			
			if(show)
				talent.ShowTutorialHighlight();
			else
				talent.HideTutorialHighlight();
		}
		
		public function ShowTutorialInventoryItemHighlight( slot:int,show:Boolean):void
		{
			mainWindow.inventoriesBar_mc.ShowTutorialShopBarItemHighlight(slot, show);
		}
		
		public function ShowTutorialShopItemHighlight(slot:int,show:Boolean):void
		{
			mainWindow.shopBar_mc.ShowTutorialShopBarItemHighlight(slot, show);
		}
		
		public function ShowTutorialActionBarItemHighlight(slot:int,show:Boolean):void
		{
			mainWindow.actionBarContainer.actionBar_mc.ShowTutorialActionBarItemHighlight(slot, show);
		}
		
		public function ShowTutorialElementHighLight(elementName:String, show):void
		{
			if (elementName == ETutorialHighlitedObjects.SelectionLevel) 
			{
				mainWindow.selectionInfo_mc.selectionInfoHero_mc.ShowTutorialLevelHighlight(show);
				return;
			}
			
			
			if (show)
				mainWindow.TutorialHighlighters[elementName].ShowTutorialHighlight();
			else
				mainWindow.TutorialHighlighters[elementName].HideTutorialHighlight();
		}
		
		public function ShowTutorialHeroHighlight (heroID:int, show:Boolean) 
		{
			var mate:MateInfoBase = GetMateInfo(heroID);
			
			if (mate == null) return;
			
			mate.ShowTutorialHighlight(show);
		}
		
		
		public function ShowTutorialHeroLevelHighlight (heroID:int, show:Boolean) 
		{
			var mate:MateInfoBase = GetMateInfo(heroID);
			
			if (mate == null) return;
			
			mate.ShowTutorialLevelHighlight(show);
		}
		
		public function ShowTutorialOvertipLevelHighlight(show:Boolean, x:Number, y:Number) 
		{
			mainWindow.ShowTutorialOvertipLevelHighlight(show, x, y);
		}
		
		public function AddQuest( questId:int, primary:Boolean, text:String, totalGoals:int )
		{
			mainWindow.questTracker_mc.AddQuest(questId, primary, text, totalGoals);
		}
		
		public function UpdateQuest( questId:int, complitedGoals:int )
		{
			mainWindow.questTracker_mc.UpdateQuest(questId,complitedGoals);
		}
		public function UpdateQuestText( questId:int, text:String )
		{
			mainWindow.questTracker_mc.UpdateQuestText(questId,text);
		}
		
		
		public function RemoveQuest( questId: int )
		{
			mainWindow.questTracker_mc.RemoveQuest(questId);
		}
		
		public function SetQuestHintLine(text:String, type:String = "None"):void
		{
			mainWindow.actionBarContainer.SetHintline(text, type);
		}
		
		public function SetForceColors(forces:Array,colors:Array):void
		{ 
			if (forces.length != colors.length)
				return;
				
			for (var i:int = 0; i < forces.length; i++) 
			{
				var forceColor:ForceColors = new ForceColors();
				forceColor.Force = forces[i];
				forceColor.Color = colors[i];
				forceColors.push(forceColor);
			}
		}
		
		public function ShowBubble(element:String, show:Boolean,text:String, bubbleViewType:String="None",bubbleIndex:int=0):void
		{
			var xOffset:Number = 0; //element == EBubbledElements.BUBBLED_IMPULSETALENT? -10:0;
			var yOffset:Number = 0;// element == EBubbledElements.BUBBLED_IMPULSETALENT? -20:0;
			
			mainWindow.bubbleVisabilityController.ShowBuble(mainWindow.BubleControls[element], show, text,bubbleViewType,bubbleIndex,xOffset,yOffset);
		}
		
		public function ShowTalentBubble(column:int, raw:int, show:Boolean, text:String,bubbleViewType:String="None",bubbleIndex:int=0):void
		{
			var talent:Talent = mainWindow.talentsBar_mc.GetTalent(column, raw);
			
			if (talent == null) return;
			
			mainWindow.bubbleVisabilityController.ShowBuble(talent, show, text,bubbleViewType,bubbleIndex,10);
		}
		
		public function ShowActionBarButtonBubble(index:int, show:Boolean, text:String,bubbleViewType:String="None",bubbleIndex:int=0):void
		{
			var item:MovieClip = mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(index);
			if (item == null) return;
			
			mainWindow.bubbleVisabilityController.ShowBuble(item, show, text,bubbleViewType,bubbleIndex);
		}
		
		public function ShowInventoryBarButtonBubble(index:int, show:Boolean, text:String,bubbleViewType:String="None",bubbleIndex:int=0):void
		{
			var item:InventoryButton = mainWindow.inventoriesBar_mc.GetInventoryItem(index);
			if (item==null) return;
			
			mainWindow.bubbleVisabilityController.ShowBuble(item, show, text,bubbleViewType,bubbleIndex);
		}
		
		public function ShowShopBarButtonBubble(index:int, show:Boolean, text:String,bubbleViewType:String ="None",bubbleIndex:int=0):void
		{
			var item:MovieClip = mainWindow.shopBar_mc.GetShopBarItem(index);
			if (item==null) return;
			
			mainWindow.bubbleVisabilityController.ShowBuble(item, show, text,bubbleViewType,bubbleIndex);
		}
		
		public	function ShowHeroBubble (heroID:int, show:Boolean, text:String, bubbleViewType:String = "None", bubbleIndex:int = 0):void
		{
			var item:MovieClip = GetMateInfo(heroID);
			
			if (item == null)
				return;
			
			mainWindow.bubbleVisabilityController.ShowBuble(item, show, text, bubbleViewType, bubbleIndex, 20);
		}
		
		public function GetHeroDisplayObject(heroID:int, bubble:Boolean=false):DisplayObject
		{
			var item:MovieClip = GetMateInfo(heroID);
			
			if (bubble)
				return mainWindow.bubbleVisabilityController.GetBubble(item);
			
			return item;
		}
		
			
		public function GetNamedDisplayObject(elementName:String, bubble:Boolean=false):DisplayObject
		{
			if (bubble) 
			{
				return mainWindow.bubbleVisabilityController.GetBubble(mainWindow.BubleControls[elementName]);
			}
			
			return mainWindow.ObjectsForGreyMask[elementName];
		}
		
		public function GetTalentBarItemDisplayObject(column:int, raw:int, bubble:Boolean=false):DisplayObject
		{
			if (bubble)
			 	return mainWindow.bubbleVisabilityController.GetBubble(mainWindow.talentsBar_mc.GetTalent(column, raw));
			
			return mainWindow.talentsBar_mc.GetTalent(column, raw);
		}
		
		public function GetActionBarItemDisplayObject(slot:int, bubble:Boolean=false):DisplayObject
		{
			if (bubble)
				return mainWindow.bubbleVisabilityController.GetBubble(mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(slot));
			
			return mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(slot);
		}
		
		public function GetShopBarItemDisplayObject(index:int, bubble:Boolean=false):DisplayObject
		{
			if (bubble)
				return mainWindow.bubbleVisabilityController.GetBubble(mainWindow.shopBar_mc.GetShopBarItem(index));
			
			return mainWindow.shopBar_mc.GetShopBarItem(index);
		}
		
		public function GetInventoryBarItemDisplayObject(index:int, bubble:Boolean=false):DisplayObject
		{
			if (bubble)
				return mainWindow.bubbleVisabilityController.GetBubble(mainWindow.inventoriesBar_mc.GetInventoryItem(index));
			
			return mainWindow.inventoriesBar_mc.GetInventoryItem(index);
		}
		
		public function IsWindowVisible(winNum:int):Boolean
		{
			return mainWindow.IsWindowVisible(winNum);
		}
		
		public function ShowCinematicNextButton(show:Boolean):void
		{
			mainWindow.NPCLineDown_mc.next_btn.visible = show;
		}
		
		public function EnableCinematicNextButton(enable:Boolean):void
		{
			if ( enable )
				mainWindow.NPCLineDown_mc.next_btn.enable();
			else
				mainWindow.NPCLineDown_mc.next_btn.disable();
		}
		
		public function ShowCinematicNextButtonAnim(show:Boolean):void
		{
			if ( show )
				mainWindow.NPCLineDown_mc.next_btn.startWaitingAnim();
			else
				mainWindow.NPCLineDown_mc.next_btn.stopWaitingAnim();
		}
		
		public function SetPortalStatus(status:int, cooldown:Number, maxCooldownValue:Number):void
		{
			mainWindow.actionBarContainer.actionBar_mc.portalBtn.SetPortalStatus(status, cooldown, maxCooldownValue);
		}
		
		public function ShowCTEUi(show:Boolean):void
		{
			mainWindow.miniMap_mc.miniMapAnim_mc.ShowCTEUi(show);
		}
		
		public function SetCTEAltarStatus(fraction:int, curHealth:int, maxHealth:int, damageType:int):void
		{
			mainWindow.miniMap_mc.miniMapAnim_mc.SetCTEAltarStatus(fraction, curHealth, maxHealth, damageType);
		}
		
		public function LockActionBar(lock:Boolean):void
		{
			mainWindow.actionBarContainer.actionBar_mc.LockActionBar(lock);
		}

		public function ShowPanel(show:Boolean):void
		{
			mainWindow.actionBarContainer.actionBar_mc.ShowPanel(show);
		}

		// Войс на портретах союзников (PF_GameLogic/VoiceOverlayController.cpp).
		public function ShowVoiceOverlay(show:Boolean):void
		{
			voiceTeamOverlay.Show(show);
		}
		
		// Позиция была у отдельного оверлея; на портретах она не нужна.
		public function SetVoiceOverlayPosition(x:Number, y:Number):void
		{
		}
		
		public function SetVoiceOverlayInteractive(interactive:Boolean):void
		{
			voiceTeamOverlay.SetInteractive(interactive);
		}
		
		public function SetVoiceOverlayState(micState:int, meSpeaking:Boolean, myName:String, ids:Array, flags:Array, volumes:Array, names:Array):void
		{
			voiceTeamOverlay.SetState(micState, meSpeaking, myName, ids, flags, volumes, names);
		}
		
		public function TempUnlockActionBar(unlock:Boolean):void
		{
			mainWindow.actionBarContainer.actionBar_mc.TempUnlockActionBar(unlock);
		}
		
		public function BlockActionBar(lock:Boolean):void
		{
			mainWindow.actionBarContainer.actionBar_mc.BlockActionBar(lock);
		}
				
		public function SetAwardDeltaRaiting(delta:Number, rankPath:String, rankTooltip:String):void
		{
			var player:PlayerStatisticData = GetPlayer(ourID);
			
			if (player == null) return;
			
			var newPlayerRating:Number = int(player.HeroRaiting+delta);
			var ratingsLen = player.leaguePlaces.length;
			
			trace("newPlayerRating =", newPlayerRating);
			
			if (delta != 0 && player.ownLeaguePlace != 0) {
				if (player.ownLeaguePlace <= 10 ) {
					if (newPlayerRating >= player.leaguePlaces[0])
						player.newLeaguePlace = 1;
					else {
						if (newPlayerRating <= player.leaguePlaces[player.leaguePlaces.length - 1])
							player.newLeaguePlace = 21;
						else {							
							for (var k:int = 0; k < ratingsLen; k++) {
								if (player.leaguePlaces[k] < newPlayerRating) {
									player.newLeaguePlace = k + 1;
									break;
								}
							}
						}
					}
				}
				else {
					if (newPlayerRating >= player.leaguePlaces[0])
						player.newLeaguePlace = player.ownLeaguePlace - 11;
					else {
						if (newPlayerRating <= player.leaguePlaces[player.leaguePlaces.length - 1])
							player.newLeaguePlace = player.ownLeaguePlace + player.leaguePlaces.length - 11;
						else {
							if (delta < 0) {
								for (var i:int = 11; i < ratingsLen; i++) {
									if (player.leaguePlaces[i] < newPlayerRating){
										player.newLeaguePlace = player.ownLeaguePlace + i + 1 - 11;
										break;
									}
								}								
							}
							else {
								for (var j:int = 9; j >= 0; j--) {
									if (player.leaguePlaces[j] > newPlayerRating){
										player.newLeaguePlace = player.ownLeaguePlace - ( 9 - j );
										break;
									}
								}		
							}
						}
					}
				}
			}
			
					
			player.leaguePlaceChange = player.ownLeaguePlace - player.newLeaguePlace;
			
			trace("player.LeagueIndex =", player.LeagueIndex);
			trace("player.LeaguePlace =", player.ownLeaguePlace);
			trace("player.newLeaguePlace =", player.newLeaguePlace);
			trace("player.leaguePlaces =", player.leaguePlaces);
			
			player.HeroRaiting = player.HeroRaiting+delta;
			player.RaitingDelta = delta;
			player.HeroRankIcon = rankPath;
			player.HeroRankTooltip = rankTooltip;			
		
			mainWindow.finalStat.personalStat.SetHeroRank(player.HeroRaiting, delta, rankPath, rankTooltip, player.LeagueIndex, player.newLeaguePlace, player.leaguePlaceChange);
			
			if (player.HeroId != ourID)
				return;
			
			if (mainWindow.finalStat.batleStatistic != null)
				mainWindow.finalStat.batleStatistic.SetHeroIdentity(player);
			else
				mainWindow.statisticsBar_mc.allStats_mc.SetHeroIdentity(player);
		}
		
		// Рейтинг аккаунта игрока (PF_GameLogic/UnitInfoNew.cpp), как на экране загрузки.
		public function SetHeroAccountRank(heroID:int, rating:Number, rankIcon:String, rankName:String):void
		{
			var player:PlayerStatisticData = GetPlayer(heroID);
			if (player == null)
				return;

			player.AccRaiting = rating;
			player.AccRankIcon = rankIcon != null ? rankIcon : "";
			player.AccRankName = rankName != null ? rankName : "";

			if (mainWindow.finalStat.batleStatistic != null)
				mainWindow.finalStat.batleStatistic.SetHeroIdentity(player);
			else
				mainWindow.statisticsBar_mc.allStats_mc.SetHeroIdentity(player);
		}

		public function SetFirstWin(iconPath:String, tooltip:String):void
		{
			mainWindow.finalStat.personalStat.SetFirstWin(iconPath, tooltip);
		}
		
		public function GetTalentActionBarIndex( column:int, row:int ):int
		{
			var talent:Talent = mainWindow.talentsBar_mc.GetTalent( column, row );
			if (talent == null) return -1;
			
			return mainWindow.actionBarContainer.actionBar_mc.GetActionButtonIndex( talent.shortcut );
		}
		
		public function GetInventoryItemActionBarIndex( index:int ):int
		{
			var invItem:InventoryButton = mainWindow.inventoriesBar_mc.GetInventoryItemBySlot( index );
			if (invItem == null) return -1;
			
			return mainWindow.actionBarContainer.actionBar_mc.GetActionButtonIndex( invItem.Shortcut );
		}
		
		public function HideSurrenderButtonForEscMenu():void
		{
			mainWindow.escMenu_mc.surrender_btn.disable();
		}
		
		public function SwitchToTutorialMode():void
		{
			mainWindow.escMenu_mc.tutorialMode = true;
		}
		
		public function SwitchToSpectatorMode():void
		{
			spectatorMode = true;
			mainWindow.escMenu_mc.IsSpectatorModeOn = true;
			HideSurrenderButtonForEscMenu();
			mainWindow.miniMap_mc.DisablePingButton();
			mainWindow.chatBar.DisableInput(true);
			
			friendlyTeamController.SpectatorMode = true;
			enemyTeamController.SpectatorMode = true;
			mainWindow.finalStat.SetSpectatorMode();
			mainWindow.statisticsBar_mc.allStats_mc.SwitchToSpectatorMode();
		}
		
		public function SetPveBacks(winImage:String, normal:String, looseImage:String):void
		{
			mainWindow.statisticsBar_mc.SetPveBacks(winImage, normal, looseImage);
		}
		
		public function RaitingPenaltyEnabled(enabled:Boolean):void
		{
			mainWindow.escMenu_mc.IsCurrentGameIsTraining = !enabled;
		}
		
		public function SetReportCounters(currentReportCount:int, maxReportCount:int):void 
		{
			mainWindow.reportWindow.SetReportCounters(currentReportCount, maxReportCount);
		}
		
		public function AddReportItem(reportItemID:int, reportItemString:String):void 
		{
			mainWindow.reportWindow.AddReportComboboxItem(reportItemID, reportItemString);
		}
		
		public function SetBindString(bindString:String):void 
		{
			var binds:Array = new Array();
			
			binds.push(bindString);
			
			mainWindow.optionsWindow.shortcutsTab.FillBindSettings(bindString);
			
			if (bindString.indexOf(" ") == -1)
				return;
			
			var actionBarSlot:String = "cmd_action_bar_slot";
				
			if (bindString.indexOf(actionBarSlot)==-1) 
				return;
			
			bindString = bindString.slice(bindString.indexOf(actionBarSlot)+actionBarSlot.length, bindString.length);
			
			var index:int = int(bindString.slice(0, bindString.indexOf(" ")));

			if (bindString.indexOf("'") == -1)
				return;
			
			var bind:String = bindString.slice(bindString.indexOf("'")+1,bindString.length);
			
			bind  = bind.slice(0,bind.indexOf("'"))
			
			if (bindString.indexOf("+", 0) != -1 )
			{
				bindString = bindString.slice(bindString.indexOf("+"), bindString.length);
				bindString = bindString.slice(bindString.indexOf("'") + 1, bindString.length);
				bind = bind + " + " + bindString.slice(0, bindString.indexOf("'"));
			}
			
			mainWindow.actionBarContainer.actionBar_mc.SetBindToSlot(index, bind);
		}
					
		public function ShowQuestHint(id:String, title:String, hintText:String, picture:String) : void
		{
			mainWindow.questHint.SetQuestHint(id, title, hintText, picture);
			mainWindow.OpenWindow(EWindows.WINDOW_QUESTHINT);
		}
		
		public function GetPlayerIdxByHeroIdx(heroIdx:int):int
		{
			var playerIdx:int = -1;
			var enemyCount:int = enemyTeamController.GetMatesCount();
			var allyCount:int = friendlyTeamController.GetMatesCount();
			
			var enemyHeroIdx:int = enemyCount - (heroIdx - allyCount + 1);
			
			if ( heroIdx >= 0 && heroIdx < allyCount )
			{
				playerIdx = friendlyTeamController.GetMateInfoByIdx(heroIdx);
			}
			else if(heroIdx >= allyCount && enemyHeroIdx >= 0 && enemyHeroIdx < enemyCount)
			{
				playerIdx = enemyTeamController.GetMateInfoByIdx(enemyHeroIdx);			
			}
			
			return playerIdx;
		}
		
		// --- SmartChat

		public function SmartChatActivate():void
		{
			mainWindow.smartChat.Show();
		}
		
		public function SmartChatActivateCategory(id:uint):void
		{
			mainWindow.smartChat.ShowCategory(id);
		}
		
		public function SmartChatDeactivate():void
		{
			mainWindow.smartChat.Hide();
		}

		public function SmartChatBeginUpdate():void
		{
			mainWindow.smartChat.BeginUpdate();
		}

		public function SmartChatEndUpdate():void
		{
			mainWindow.smartChat.EndUpdate();
		}

		public function SmartChatBeginCategory(id:uint, key:String, name:String):void
		{
			mainWindow.smartChat.BeginCategory(id, key, name);
		}

		public function SmartChatEndCategory():void
		{
			mainWindow.smartChat.EndCategory();
		}

		public function SmartChatAddMessage(id:uint, key:String, text:String):void
		{
			mainWindow.smartChat.AddMessage(id, key, text);
		}

		public function SmartChatAddCancel(id:uint, key:String):void
		{
			mainWindow.smartChat.AddCancel(id, key);
		}
		
		public function DayNightUpdateState(night:Boolean, stateFraction:Number):void
		{
			mainWindow.dayNight_mc.dayNightAnim_mc.Night = night;
			mainWindow.dayNight_mc.dayNightAnim_mc.StateFraction = stateFraction;
		}
		
		public function DayNightHideUI(hide:Boolean):void
		{
			mainWindow.dayNight_mc.visible = !hide;
			mainWindow.miniMap_mc.GameTimerVisible = hide;
		}
		
		public function SetChatOff(isChatOff:Boolean):void
		{
			mainWindow.chatBar.chatInput_mc.chatOutput_cb.Value = isChatOff ? 1 : 0;
			mainWindow.optionsWindow.settingsTab.smartchat_cb.Value = isChatOff ? 1 : 0;
		}
	}
}