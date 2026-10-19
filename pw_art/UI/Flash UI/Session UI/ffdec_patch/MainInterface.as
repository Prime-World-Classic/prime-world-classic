package
{
   import BaseClasses.BaseResizableObject;
   import BaseClasses.MateInfoBase;
   import Components.Bar;
   import Controllers.MateInfoController;
   import Controllers.SoundController;
   import Controllers.TutorialShower;
   import Datas.ForceColors;
   import Datas.PlayerStatisticData;
   import Enums.EFraction;
   import Enums.ETutorialBlocks;
   import Enums.ETutorialHighlitedObjects;
   import Enums.EWindows;
   import Enums.TalentUIState;
   import InventoryBar.*;
   import MainScreen.*;
   import TalentBar.*;
   import Utils.GameTimeGlobalUtils;
   import EscMenu.ShortcutItem;
   import Effects.AttentionEffect;
   import ActionBar.ActionButton;
   import flash.display.DisplayObject;
   import flash.display.Graphics;
   import flash.display.Loader;
   import flash.display.MovieClip;
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
      
      private var selectionCharstatUnitID:int = -1;
      
      private var forceColors:Array = [];
      
      private var spectatorMode:Boolean = false;
      
      private var showAlwaysHeroesLevels:Boolean = false;
      
      private var _shortheroname:SetPlayerShortName;
      
      public function MainInterface(param1:Main)
      {
         super();
         this.mainWindow = param1;
         this._shortheroname = new SetPlayerShortName();
         this.friendlyTeamController = new MateInfoController(this.mainWindow.teamInfoFrendly_mc);
         this.enemyTeamController = new MateInfoController(this.mainWindow.teamInfoEnemy_mc);
      }
      
      public function SetClientVersion(param1:String) : void
      {
         this.mainWindow.version_mc.SetVersion(param1);
      }
      
      public function ShowAlwaysHeroesLevels(param1:Boolean) : void
      {
         this.showAlwaysHeroesLevels = param1;
      }
      
      public function SetOurHeroIdententity(param1:int, param2:int, param3:int, param4:Array, param5:int = 0, param6:int = 0, param7:String = "", param8:String = "", param9:Boolean = false) : void
      {
         var _loc10_:int = this.ourID;
         this.ourID = param1;
         if(!this.spectatorMode)
         {
            this.mainWindow.statisticsBar_mc.allStats_mc.ResetMates();
         }
         this.mainWindow.statisticsBar_mc.allStats_mc.SetOurHero(param1,param2);
         this.mainWindow.finalStat.SetOurHero(param1,param2);
         var _loc11_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc11_ == null)
         {
            _loc11_ = new PlayerStatisticData();
            this.mainWindow.Players.push(_loc11_);
         }
         _loc11_.HeroId = param1;
         _loc11_.Fraction = param2;
         _loc11_.DamageType = param3;
         _loc11_.playerFlagPath = param7;
         _loc11_.playerFlagTooltip = param8;
         _loc11_.LeagueIndex = param5;
         _loc11_.ownLeaguePlace = param6;
         _loc11_.leaguePlaces.splice(0);
         var _loc12_:int = 0;
         while(_loc12_ < param4.length)
         {
            _loc11_.leaguePlaces.push(param4[_loc12_]);
            _loc12_++;
         }
         if(!this.spectatorMode)
         {
            this.friendlyTeamController.ResetToStartState();
            this.enemyTeamController.ResetToStartState();
            this.friendlyTeamController.OurHeroId = param1;
         }
         this.mainWindow.chatBar.SetOurPlayerId(param1);
         this.mainWindow.ResetData();
      }
      
      private function GetPlayerIndex(param1:int) : int
      {
         var _loc2_:int = 0;
         while(_loc2_ < this.mainWindow.Players.length)
         {
            if(this.mainWindow.Players[_loc2_].HeroId == param1)
            {
               return _loc2_;
            }
            _loc2_++;
         }
         return -1;
      }
      
      private function GetPlayer(param1:int) : PlayerStatisticData
      {
         var _loc2_:int = this.GetPlayerIndex(param1);
         if(_loc2_ == -1)
         {
            return null;
         }
         return this.mainWindow.Players[_loc2_];
      }
      
      private function SetHeroIdentity(param1:int, param2:String, param3:String, param4:String, param5:String, param6:String, param7:Boolean, param8:Boolean, param9:int, param10:int, param11:int, param12:Number, param13:String, param14:String, param15:String, param16:String, param17:Boolean, param18:Boolean, param19:int, param20:Boolean, param21:int, param22:int, param23:Array) : void
      {
         this.FillPlayerData(param1,param2,param3,param4,param5,param6,param10,param11,param7,param8,param9,param12,param13,param14,param15,param16,param17,param18,param19,param20,param21,param22,param23);
      }
      
      public function ShowHeroPortrait(param1:int, param2:Boolean = false) : void
      {
         var _loc5_:MateInfoController = null;
         var _loc6_:HeroMateInfo = null;
         var _loc3_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc3_ != null)
         {
            _loc3_.IsNeedToShowInStatistics = true;
            this.mainWindow.statisticsBar_mc.allStats_mc.SetHeroIdentity(_loc3_);
         }
         var _loc4_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc4_ == null)
         {
            _loc5_ = null;
            if(this.spectatorMode)
            {
               switch(_loc3_.Fraction)
               {
                  case EFraction.HUMAN_FACTION:
                     _loc5_ = this.friendlyTeamController;
                     break;
                  case EFraction.ELF_FACTION:
                     _loc5_ = this.enemyTeamController;
               }
            }
            else if(_loc3_.IsEnemyTeam)
            {
               _loc5_ = this.enemyTeamController;
            }
            else
            {
               _loc5_ = this.friendlyTeamController;
            }
            if(_loc5_ == null)
            {
               return;
            }
            _loc4_ = _loc5_.GenerateMateInfo(param1,_loc3_.PartyID);
         }
         if(_loc4_ == null)
         {
            return;
         }
         _loc4_.SetHero(_loc3_,false);
         if(_loc3_.IsOurHero)
         {
            _loc6_ = this.mainWindow.heroInfo_mc.heroInfoAnim_mc;
            _loc6_.SetHero(_loc3_);
            this.mainWindow.chatBar.chatOutput_mc.OurName = _loc3_.PlayerName;
            this.mainWindow.chatBar.chatOutput_mc.OurHeroId = _loc3_.HeroId;
         }
      }
      
      public function SetFriendlyHeroIdentity(param1:int, param2:String, param3:String, param4:String, param5:Boolean, param6:Boolean, param7:int, param8:int, param9:int, param10:Number, param11:Array, param12:String, param13:String, param14:Boolean = false, param15:int = 0, param16:String = "", param17:String = "", param18:Boolean = false, param19:int = 0, param20:int = 0) : void
      {
         var _loc21_:String = "";
         var _loc22_:String = "";
         var _loc23_:String = param2;
         if(param2.substr(0,1) != "[")
         {
            _loc21_ = "";
         }
         else
         {
            _loc21_ = param2.substr(0,param2.indexOf("]") + 1);
         }
         _loc22_ = this._shortheroname.SetPlayerName(param2);
         this.SetHeroIdentity(param1,_loc23_,_loc22_,_loc21_,param3,param4,param5,param6,param7,param8,param9,param10,param12,param13,param16,param17,param14,false,param15,param18,param19,param20,param11);
      }
      
      public function SetEnemyHeroIdentity(param1:int, param2:String, param3:String, param4:String, param5:Boolean, param6:Boolean, param7:int, param8:int, param9:int, param10:Number, param11:Array, param12:String, param13:String, param14:Boolean = false, param15:int = 0, param16:String = "", param17:String = "", param18:Boolean = false, param19:int = 0, param20:int = 0) : void
      {
         var _loc21_:String = "";
         var _loc22_:String = "";
         var _loc23_:String = param2;
         if(param2.substr(0,1) != "[")
         {
            _loc21_ = "";
         }
         else
         {
            _loc21_ = param2.substr(0,param2.indexOf("]") + 1);
         }
         _loc22_ = this._shortheroname.SetPlayerName(param2);
         this.SetHeroIdentity(param1,_loc23_,_loc22_,_loc21_,param3,param4,param5,param6,param7,param8,param9,param10,param12,param13,param16,param17,param14,true,param15,param18,param19,param20,param11);
      }
      
      private function FillPlayerData(param1:int, param2:String, param3:String, param4:String, param5:String, param6:String, param7:int, param8:int, param9:Boolean, param10:Boolean, param11:int, param12:Number, param13:String, param14:String, param15:String, param16:String, param17:Boolean, param18:Boolean, param19:int, param20:Boolean, param21:int, param22:int, param23:Array) : void
      {
         var _loc24_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc24_ == null)
         {
            _loc24_ = new PlayerStatisticData();
            this.mainWindow.Players.push(_loc24_);
         }
         _loc24_.IsOurHero = this.ourID == param1;
         _loc24_.HeroId = param1;
         _loc24_.PlayerName = param2;
         _loc24_.GuildName = param4;
         _loc24_.PlayerShortName = param3;
         _loc24_.ClassName = param5;
         _loc24_.HeroIcon = param6;
         _loc24_.IsAnimatedAvatar = param20;
         _loc24_.LeagueIndex = param21;
         _loc24_.Fraction = param7;
         _loc24_.Gender = param9;
         _loc24_.IsBot = param10;
         _loc24_.Force = param11;
         _loc24_.ShowRedBack = param11 >= this.forceColors[this.forceColors.length - 1].Force;
         _loc24_.HeroNameColor = this.GetColorByForce(param11);
         _loc24_.OriginalFraction = param8;
         _loc24_.HeroRankIcon = param13;
         _loc24_.HeroRankTooltip = param14;
         _loc24_.HeroRaiting = param12;
         _loc24_.IsPremium = param17;
         _loc24_.IsEnemyTeam = param18;
         _loc24_.PartyID = param19;
         _loc24_.playerFlagPath = param15;
         _loc24_.playerFlagTooltip = param16;
         _loc24_.ownLeaguePlace = param22;
         _loc24_.leaguePlaces.splice(0);
         var _loc25_:int = 0;
         while(_loc25_ < param23.length)
         {
            _loc24_.leaguePlaces.push(param23[_loc25_]);
            _loc25_++;
         }
      }
      
      public function SetHeroChatMuted(param1:int, param2:Boolean) : void
      {
         var _loc3_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc3_ == null)
         {
            trace("SetHeroChatMuted() - player must exist!");
            return;
         }
         _loc3_.ChatMuted = param2;
      }
      
      private function GetColorByForce(param1:int) : uint
      {
         var _loc2_:uint = 16777215;
         var _loc3_:int = 0;
         while(_loc3_ < this.forceColors.length)
         {
            if(param1 < this.forceColors[_loc3_].Force)
            {
               break;
            }
            _loc2_ = uint(this.forceColors[_loc3_].Color);
            _loc3_++;
         }
         return _loc2_;
      }
      
      public function SetFriendship(param1:int, param2:Boolean, param3:Boolean = false) : void
      {
         var _loc4_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc4_ == null)
         {
            return;
         }
         _loc4_.IsFriend = param2;
         _loc4_.IsPending = param3;
         this.mainWindow.statisticsBar_mc.allStats_mc.SetFriendshipRelation(param1,param2,param3);
         var _loc5_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc5_ == null)
         {
            return;
         }
         _loc5_.SetFriendshipRelation(param2,param3);
      }
      
      public function SetGameProgress(param1:Number, param2:Number, param3:int, param4:int) : void
      {
         this.mainWindow.statisticsBar_mc.allStats_mc.SetGameProgress(param1,param2,param3);
         GameTimeGlobalUtils.CurrentTime = param3;
         if(this.mainWindow.dayNight_mc.visible)
         {
            this.mainWindow.dayNight_mc.GameTime = param3;
         }
         else
         {
            this.mainWindow.miniMap_mc.GameTime = param3;
         }
         this.mainWindow.announcement_mc.TimeToCreepSpawn = param4;
         this.mainWindow.voting_mc.SetTime(param3);
      }
      
      public function SetHeroGameProgress(param1:int, param2:int, param3:int, param4:int, param5:int, param6:int = 0) : void
      {
         var _loc7_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc7_ == null)
         {
            return;
         }
         _loc7_.Assists = param4;
         _loc7_.Deaths = param3;
         _loc7_.Kills = param2;
         _loc7_.Score = param5;
         _loc7_.Prime = param6;
         if(_loc7_.isVisible)
         {
            _loc7_.CurrentPrime = _loc7_.Prime;
         }
         this.mainWindow.statisticsBar_mc.allStats_mc.SetHeroGameProgress(_loc7_);
      }
      
      private function GetMateInfo(param1:int) : MateInfoBase
      {
         if(this.friendlyTeamController.DoesHeroExist(param1))
         {
            return this.friendlyTeamController.GetMateInfo(param1);
         }
         if(this.enemyTeamController.DoesHeroExist(param1))
         {
            return this.enemyTeamController.GetMateInfo(param1);
         }
         return null;
      }
      
      public function SetHeroParams(param1:int, param2:int, param3:int, param4:int, param5:int, param6:int, param7:Boolean, param8:Boolean, param9:int, param10:Number, param11:Number = 0, param12:Number = 0, param13:Boolean = false, param14:Number = -1) : void
      {
         var _loc15_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc15_ == null)
         {
            return;
         }
         var _loc16_:Boolean = _loc15_.isVisible != param7;
         _loc15_.Level = param2;
         _loc15_.CurrentHealth = param3;
         _loc15_.MaximumHealth = param4;
         _loc15_.CurrentMana = param5;
         _loc15_.MaximumMana = param6;
         _loc15_.Channeling = param10;
         _loc15_.HealthRegen = param11;
         _loc15_.ManaRegen = param12;
         _loc15_.UltimateCooldown = param14;
         _loc15_.isVisible = param7;
         _loc15_.TimeToRessurect = param9;
         if(_loc16_)
         {
            this.SetHeroGameProgress(_loc15_.HeroId,_loc15_.Kills,_loc15_.Deaths,_loc15_.Assists,_loc15_.Score,_loc15_.Prime);
         }
         if(param7 || this.showAlwaysHeroesLevels || param1 == this.ourID)
         {
            _loc15_.LastVisibleLevel = param2;
         }
         this.mainWindow.statisticsBar_mc.allStats_mc.SetHeroStatus(param1,_loc15_.LastVisibleLevel,param7,false,param9);
         if(this.ourID == param1)
         {
            this.mainWindow.SetOurHeroParams(param2,param3,param4,param5,param6,param9,param10,param11,param12,param13);
            if(this.spectatorMode)
            {
               this.mainWindow.actionBarContainer.actionBar_mc.EnemyHealthbarPresentation = _loc15_.Fraction == EFraction.ELF_FACTION;
            }
            if(_loc15_.CustomEnergy)
            {
               this.mainWindow.actionBarContainer.actionBar_mc.ManaBarColor = _loc15_.CustomEnergyColor;
               this.mainWindow.actionBarContainer.actionBar_mc.ManaBarShowRegeneration = Math.abs(param12) > 0;
            }
            else
            {
               this.mainWindow.actionBarContainer.actionBar_mc.ManaBarColor = Bar.MANABAR_COLOR_DEFAULT;
               this.mainWindow.actionBarContainer.actionBar_mc.ManaBarShowRegeneration = true;
            }
         }
         var _loc17_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc17_ == null)
         {
            return;
         }
         var _loc18_:Boolean = param9 > 0;
         _loc17_.SetGreyMask(_loc18_ || !param7 || !param8);
         _loc17_.SetDeathTimeOut(param9);
         if(_loc18_ || !param7)
         {
            _loc17_.SetHeroParams(0,param4,0,param6,param11,param12,false);
         }
         else
         {
            _loc17_.SetHeroParams(param3,param4,param5,param6,param11,param12);
         }
         _loc17_.level = _loc15_.LastVisibleLevel;
         _loc17_.ultcd = _loc15_.UltimateCooldown;
         if(_loc15_.CustomEnergy)
         {
            _loc17_.ManaBarColor = _loc15_.CustomEnergyColor;
            _loc17_.ManaBarShowRegeneration = Math.abs(param12) > 0;
         }
         else
         {
            _loc17_.ManaBarColor = Bar.MANABAR_COLOR_DEFAULT;
            _loc17_.ManaBarShowRegeneration = true;
         }
         if(this.selectedTalentBarHeroId == param1)
         {
            this.mainWindow.talentsBarContext_mc.Level = _loc15_.LastVisibleLevel;
         }
         if(this.selectionCharstatUnitID == param1)
         {
            this.mainWindow.selectionCharStats_mc.SetDeathTimeOut(param9);
         }
      }
      
      public function SetHeroCustomEnergy(param1:int, param2:String, param3:uint) : void
      {
         var _loc4_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc4_ == null)
         {
            return;
         }
         _loc4_.CustomEnergy = true;
         _loc4_.CustomEnergyName = param2;
         _loc4_.CustomEnergyColor = param3;
      }
      
      public function ShowStatistics(param1:Boolean) : void
      {
         this.mainWindow.ShowStatisticsBar(param1);
      }
      
      public function SetSelectionParams(param1:String, param2:int, param3:int, param4:int, param5:int, param6:int, param7:Boolean, param8:Number, param9:Number = 0, param10:Number = 0, param11:int = -1) : void
      {
         var _loc12_:PlayerStatisticData = param11 < 0 ? null : this.GetPlayer(param11);
         var _loc13_:Boolean = _loc12_ == null ? false : _loc12_.CustomEnergy;
         var _loc14_:int = _loc12_ == null ? 0 : _loc12_.LeagueIndex;
         if(_loc13_)
         {
            this.mainWindow.selectionInfo_mc.manaBar_mc.barColor = _loc12_.CustomEnergyColor;
            this.mainWindow.selectionInfo_mc.manaBar_mc.ShowRegenerationText = Math.abs(param10) > 0;
         }
         else
         {
            this.mainWindow.selectionInfo_mc.manaBar_mc.barColor = Bar.MANABAR_COLOR_DEFAULT;
            this.mainWindow.selectionInfo_mc.manaBar_mc.ShowRegenerationText = true;
         }
         this.mainWindow.selectionInfo_mc.setParams(param1,param2,param3,param4,param5,param6,param7,param8,param9,param10,_loc14_);
      }
      
      public function setSelectionInfoIcon(param1:String, param2:int, param3:Boolean, param4:Boolean = false) : void
      {
         var _loc5_:PlayerStatisticData = param2 < 0 ? null : this.GetPlayer(param2);
         var _loc6_:MateInfoBase = param2 < 0 ? null : this.GetMateInfo(param2);
         if(_loc5_ == null)
         {
            this.mainWindow.selectionInfo_mc.HeroId = -1;
            this.mainWindow.selectionInfo_mc.IsOurHeroSelected = false;
            this.mainWindow.selectionInfo_mc.IsBot(false);
            this.mainWindow.selectionInfo_mc.ResetHeroNameColor();
            this.mainWindow.selectionInfo_mc.selectionInfoHero_mc.isHeroIgnored = false;
         }
         else
         {
            this.mainWindow.selectionInfo_mc.HeroId = param2;
            this.mainWindow.selectionInfo_mc.IsOurHeroSelected = param2 == this.ourID;
            this.mainWindow.selectionInfo_mc.IsBot(_loc5_.IsBot);
            this.mainWindow.selectionInfo_mc.SetHeroNameColor(_loc5_.HeroNameColor);
            this.mainWindow.selectionInfo_mc.SetFriendShipRelation(_loc5_.IsFriend,_loc5_.IsPending);
            this.mainWindow.selectionInfo_mc.SetRedBackVisibility(_loc5_.ShowRedBack);
            this.mainWindow.selectionInfo_mc.selectionInfoHero_mc.isHeroIgnored = _loc5_.IsIgnoring;
         }
         this.mainWindow.selectionInfo_mc.SetIcon(param1,param4);
         this.enemyTeamController.ResetMateSelection();
         this.friendlyTeamController.ResetMateSelection();
         if(_loc6_ == null)
         {
            this.mainWindow.selectionInfo_mc.ResetGender();
         }
         else
         {
            _loc6_.SetSelected(true);
            this.mainWindow.selectionInfo_mc.SetGender(param3);
         }
      }
      
      public function SetTalentIcon(param1:int, param2:int, param3:String, param4:String, param5:Boolean, param6:int, param7:int, param8:Boolean, param9:int) : void
      {
         this.mainWindow.talentsBar_mc.SetIcon(param1,param2,param3,param4,param5,param6,param7,param8,param9);
         if(param5)
         {
            this.mainWindow.actionBarContainer.actionBar_mc.SetReservedPosition(param6);
         }
      }
      
      public function SetTalentState(param1:int, param2:int, param3:int) : *
      {
         var _loc4_:Talent = this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
         if(_loc4_ == null)
         {
            return;
         }
         if((_loc4_.icoState == TalentUIState.CANBUY || _loc4_.icoState == TalentUIState.NOTENOUGH_DEV_POINTS) && param3 == TalentUIState.BOUGHT && Boolean(_loc4_.IsActive))
         {
            this.AddTalantToActBar(param1,param2);
         }
         this.mainWindow.talentsBar_mc.setState(param1,param2,param3);
      }
      
      public function OnTalentsStateChanged() : *
      {
         this.mainWindow.talentsBar_mc.onStateChanged();
      }
      
      private function AddTalantToActBar(param1:int, param2:int) : *
      {
         var _loc3_:Talent = this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
         if(_loc3_ == null)
         {
            return;
         }
         var _loc4_:TalentShortCut = new TalentShortCut(_loc3_,this.mainWindow.actionBarContainer.actionBar_mc);
         this.mainWindow.actionBarContainer.actionBar_mc.AddShortcut(_loc4_);
      }
      
      public function SetTalentStatus(param1:int, param2:int, param3:int, param4:Number, param5:Number, param6:Boolean = false) : *
      {
         var _loc7_:Talent = this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
         if(_loc7_ == null)
         {
            return;
         }
         _loc7_.TalantState = param3;
         _loc7_.IsAlternativStateActivated = param6;
         _loc7_.Cooldown(param4,param5);
         if(_loc7_.shortcut == null)
         {
            return;
         }
         _loc7_.shortcut.State = param3;
      }
      
      public function SetSelectionTalentBarUnitID(param1:int) : void
      {
         this.selectedTalentBarHeroId = param1;
         var _loc2_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc2_ == null)
         {
            return;
         }
         this.mainWindow.talentsBarContext_mc.HeaderText(_loc2_.PlayerName);
         this.mainWindow.talentsBarContext_mc.Level = _loc2_.LastVisibleLevel;
      }
      
      public function SetSelectionTalentIcon(param1:int, param2:int, param3:String, param4:Boolean, param5:int) : void
      {
         this.mainWindow.talentsBarContext_mc.SetIcon(param1,param2,param3,param4,param5);
      }
      
      public function SetSelectionTalentState(param1:int, param2:int, param3:int) : void
      {
         this.mainWindow.talentsBarContext_mc.setState(param1,param2,param3);
      }
      
      public function SetGlobalCooldown(param1:Number, param2:Number) : *
      {
         this.mainWindow.actionBarContainer.actionBar_mc.setGlobalCooldown(param1,param2);
         this.mainWindow.inventoriesBar_mc.setGlobalCooldown(param1,param2);
      }
      
      public function UseSlot(param1:int) : *
      {
         this.mainWindow.UseSlot(param1);
      }
      
      public function SetHeroDevelopmentParams(param1:int, param2:int) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.Prime = param1;
         this.mainWindow.actionBarContainer.actionBar_mc.Gold = param2;
         this.mainWindow.inventoriesBar_mc.Prime = param1;
      }
      
      // param5 (silent): no old minimap alert sound - the ping has its own sound (C++ AdventureScreen)
      public function AddSignal(param1:Number, param2:Number, param3:uint = 16776386, param4:Number = 0, param5:Boolean = false) : void
      {
         SoundController.muteMinimapAlert = param5;
         try
         {
            this.mainWindow.miniMap_mc.AddSignal(param1,param2,param3,param4);
         }
         catch(e:Error)
         {
         }
         SoundController.muteMinimapAlert = false;
      }
      
      public function SetMinimapTargetZoneColor(param1:uint) : void
      {
         this.mainWindow.miniMap_mc.SetTargetZoneColor(param1);
      }
      
      public function SetMinimapTargetZoneThickness(param1:uint) : void
      {
         this.mainWindow.miniMap_mc.SetTargetZoneThickness(param1);
      }
      
      public function ShowMinimapTargetZone(param1:Number, param2:Number, param3:Number) : void
      {
         this.mainWindow.miniMap_mc.ShowTargetZone(param1,param2,param3);
      }
      
      public function HideMinimapTargetZone() : void
      {
         this.mainWindow.miniMap_mc.HideTargetZone();
      }
      
      public function SetShopItemsCount(param1:int) : void
      {
         this.mainWindow.shopBar_mc.SetShopItemsCount(param1);
      }
      
      public function SetShopItemIcon(param1:int, param2:String) : void
      {
         this.mainWindow.shopBar_mc.SetShopItemIcon(param1,param2);
      }
      
      public function SetShopItemState(param1:int, param2:int) : void
      {
         this.mainWindow.shopBar_mc.SetShopItemState(param1,param2);
      }
      
      public function ShowShop(param1:Boolean) : void
      {
         this.mainWindow.ShowShop(param1);
      }
      
      public function AddInventoryItemToActionBar(param1:int) : void
      {
         var _loc2_:InventoryButton = this.mainWindow.inventoriesBar_mc.GetInventoryItem(param1);
         if(_loc2_ == null)
         {
            return;
         }
         this.mainWindow.actionBarContainer.actionBar_mc.AddShortcut(new InventoryItemShortcut(_loc2_,this.mainWindow.actionBarContainer.actionBar_mc));
      }
      
      public function SetInventoryItemIcon(param1:int, param2:String) : void
      {
         this.mainWindow.inventoriesBar_mc.SetInventoryItemIcon(param1,param2);
      }
      
      public function SetInventoryItemState(param1:int, param2:int, param3:int, param4:Number, param5:Number) : *
      {
         var _loc6_:int = this.mainWindow.inventoriesBar_mc.GetInventoryItem(param1).count;
         this.mainWindow.inventoriesBar_mc.SetInventoryItemState(param1,param2,param3,param4,param5);
         var _loc7_:InventoryButton = this.mainWindow.inventoriesBar_mc.GetInventoryItem(param1);
         if(_loc6_ == param2 || param2 < 1)
         {
            return;
         }
         this.mainWindow.inventoriesBar_mc.ResetBuyToIndex();
         if(_loc7_.Shortcut == null && this.mainWindow.actionBarContainer.actionBar_mc.BuyToIndex != -1)
         {
            this.mainWindow.actionBarContainer.actionBar_mc.AddShortcut(new InventoryItemShortcut(_loc7_,this.mainWindow.actionBarContainer.actionBar_mc));
         }
         if(_loc7_.Shortcut != null && this.mainWindow.actionBarContainer.actionBar_mc.BuyToIndex != -1)
         {
            this.mainWindow.actionBarContainer.actionBar_mc.placeShortCutIntoIndex(this.mainWindow.actionBarContainer.actionBar_mc.BuyToIndex,_loc7_.Shortcut);
         }
      }
      
      public function ShowImpulseBuff(param1:String) : void
      {
         this.mainWindow.ShowImpulseBuff(param1);
      }
      
      public function ResetImpulsBuff(param1:String) : void
      {
         this.mainWindow.impulsShop_mc.ResetImpulsBuffIcon(param1);
      }
      
      public function HideImpulseBuff(param1:Boolean, param2:Boolean) : void
      {
         this.mainWindow.impulsShop_mc.HideImpulseBuff(param1,param2);
      }
      
      public function HideAllWindows() : void
      {
      }
      
      public function OnEscape() : Boolean
      {
         return this.mainWindow.OnEscapeBtnClick();
      }
      
      public function OpenEscMenu() : void
      {
         this.mainWindow.OpenEscMenu();
      }
      
      public function EnterMinigame() : void
      {
         this.mainWindow.MiniGameOpen(true);
      }
      
      public function LeaveMinigame() : void
      {
         this.mainWindow.MiniGameOpen(false);
      }
      
      public function GameEnd(param1:Boolean, param2:String, param3:Boolean, param4:Boolean) : void
      {
         var _loc5_:PlayerStatisticData = this.GetPlayer(this.ourID);
         var _loc6_:int = 0;
         if(_loc5_ != null)
         {
            _loc6_ = _loc5_.OriginalFraction;
         }
         this.mainWindow.GameEnd(_loc6_,param1,param2,param3,param4);
      }
      
      public function ConfigurePlaySameTeamFeature(param1:Boolean, param2:int) : void
      {
         this.mainWindow.ConfigurePlaySameTeamFeature(param1,param2);
      }
      
      public function HandlePlaySameTeamDecision(param1:int, param2:Boolean) : void
      {
         this.mainWindow.HandlePlaySameTeamDecision(param1,param2);
      }
      
      public function SetBaseStat(param1:Boolean, param2:int, param3:int, param4:int, param5:Boolean) : void
      {
         this.mainWindow.SetBaseStat(param1,param2,param3,param4,param5);
      }
      
      public function SetDerivativeStat(param1:Boolean, param2:int, param3:int) : void
      {
         this.mainWindow.SetDerivativeStat(param1,param2,param3);
      }
      
      public function SetCharStatUnit(param1:Boolean, param2:int, param3:String, param4:String, param5:int, param6:int) : void
      {
         this.mainWindow.SetCharStatUnit(param1,param2,param3,param4,param5,param6);
         if(param1)
         {
            this.selectionCharstatUnitID = param2;
         }
         var _loc7_:PlayerStatisticData = this.GetPlayer(param2);
         if(_loc7_ != null)
         {
            this.mainWindow.selectionCharStats_mc.SetDeathTimeOut(_loc7_.TimeToRessurect);
         }
         else
         {
            this.mainWindow.selectionCharStats_mc.SetDeathTimeOut(0);
         }
      }
      
      public function SetCharStatUnitParams(param1:Boolean, param2:int, param3:int, param4:int, param5:int, param6:int, param7:int, param8:Boolean) : void
      {
         var _loc9_:PlayerStatisticData = this.GetPlayer(param2);
         var _loc10_:int = _loc9_ == null || _loc9_.isVisible || param2 == this.ourID ? param3 : _loc9_.LastVisibleLevel;
         if(this.selectedTalentBarHeroId == param2)
         {
            this.mainWindow.talentsBarContext_mc.Level = _loc10_;
         }
         this.mainWindow.SetCharStatUnitParams(param1,_loc10_,param4,param5,param6,param7);
         this.mainWindow.selectionCharStats_mc.SetGreyMask(!param8);
      }
      
      public function ShowHideWindow(param1:int) : void
      {
         this.mainWindow.ShowHideWindow(param1);
      }
      
      public function SetBuffIcon(param1:Boolean, param2:String, param3:int, param4:Boolean) : void
      {
         this.mainWindow.SetBuffIcon(param1,param2,param3,param4);
      }
      
      public function SetBuffStatus(param1:Boolean, param2:int, param3:Number, param4:int) : void
      {
         this.mainWindow.SetBuffStatus(param1,param2,param3,param4);
      }
      
      public function ShowFPS(param1:Boolean) : void
      {
         this.mainWindow.ShowFPS(param1);
      }
      
      public function SetFinalHeroTitle(param1:int, param2:String, param3:String, param4:int) : void
      {
         var _loc5_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc5_ == null)
         {
            return;
         }
         _loc5_.TitleIcon = param3;
         _loc5_.TitleName = param2;
         _loc5_.TitleTooltipID = param4;
      }
      
      public function AddOurHeroAward(param1:int, param2:String, param3:int, param4:Boolean = false, param5:Boolean = false) : void
      {
         var _loc6_:PlayerStatisticData = this.GetPlayer(this.ourID);
         if(_loc6_ == null)
         {
            return;
         }
         if(param5)
         {
            _loc6_.AddPremiumAwards(param1,param2,param3);
         }
         else
         {
            _loc6_.AddAwards(param1,param2,param3,param4);
         }
      }
      
      public function StartAwarding() : void
      {
         var _loc1_:PlayerStatisticData = this.GetPlayer(this.ourID);
         if(_loc1_ == null)
         {
            return;
         }
         _loc1_.Awards.splice(0,_loc1_.Awards.length);
         _loc1_.PremiumAwards.splice(0,_loc1_.PremiumAwards.length);
         this.mainWindow.finalStat.RemoveAwardsIcons();
      }
      
      public function FinishAwarding() : void
      {
         var _loc1_:int = this.GetPlayerIndex(this.ourID);
         if(_loc1_ == -1)
         {
            return;
         }
         this.mainWindow.finalStat.FinishAwarding(_loc1_);
      }
      
      public function AddHeroExploit(param1:int, param2:String, param3:int) : void
      {
         var _loc4_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc4_ == null)
         {
            return;
         }
         _loc4_.AddHeroExploit(param2);
         _loc4_.AddHeroExploitTooltipID(param3);
      }
      
      public function AddHeroResults(param1:int, param2:String, param3:int, param4:String) : void
      {
         var _loc5_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc5_ == null)
         {
            return;
         }
         _loc5_.AddHeroResult(param4,param3,param2);
      }
      
      public function SetErrorMessage(param1:int, param2:String, param3:Boolean, param4:Number) : void
      {
         this.mainWindow.SetErrorMessage(param1,param2,param3,param4);
      }
      
      public function ShowErrorMessage(param1:int, param2:Boolean) : void
      {
         this.mainWindow.ShowErrorMessage(param1,param2);
      }
      
      public function ShowErrorEvent(param1:String, param2:Boolean, param3:Number) : void
      {
         this.mainWindow.ShowErrorEvent(param1,param2,param3);
      }
      
      public function ShowAnnouncement(param1:String, param2:int, param3:int) : void
      {
         this.mainWindow.ShowAnnouncement(param1,param2,param3);
      }
      
      public function SetAnnouncementPart(param1:Boolean, param2:String, param3:int, param4:String, param5:int, param6:int = -1) : void
      {
         var _loc7_:PlayerStatisticData = this.GetPlayer(param6);
         if(_loc7_ != null)
         {
            this.mainWindow.SetAnnouncementPart(param1,param2,_loc7_.LastVisibleLevel,param4,param5);
            return;
         }
         this.mainWindow.SetAnnouncementPart(param1,param2,param3,param4,param5);
      }
      
      public function PlayerLeft(param1:int, param2:int = 0) : *
      {
         var _loc3_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc3_ == null)
         {
            _loc3_ = new PlayerStatisticData();
            this.mainWindow.Players.push(_loc3_);
         }
         _loc3_.IsHeroLeft = true;
         _loc3_.LeaveType = param2;
         var _loc4_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc4_ == null)
         {
            return;
         }
         _loc4_.OnPlayerLeft(param2);
         this.mainWindow.statisticsBar_mc.allStats_mc.OnHeroLeft(param1,param2);
      }
      
      public function PlayerRecconected(param1:int) : void
      {
         var _loc2_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc2_ == null)
         {
            _loc2_ = new PlayerStatisticData();
            this.mainWindow.Players.push(_loc2_);
         }
         _loc2_.IsHeroLeft = false;
         var _loc3_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc3_ == null)
         {
            return;
         }
         _loc3_.OnPlayerReturn();
         this.mainWindow.statisticsBar_mc.allStats_mc.OnHeroReturn(param1);
      }
      
      public function ShowSpawnCreepsAnnouncement(param1:String, param2:String, param3:String) : void
      {
         this.mainWindow.announcement_mc.ShowSpawnCreepsAnnouncement(param1,param2,param3);
      }
      
      public function AddChannel(param1:int, param2:String, param3:uint, param4:Boolean, param5:Boolean, param6:Boolean, param7:Boolean = false, param8:int = 0) : void
      {
         this.mainWindow.chatBar.AddChannel(param1,param2,param3,param4,param5,param6,param8);
      }
      
      public function AddMessage(param1:int, param2:String, param3:String) : void
      {
         this.mainWindow.chatBar.AddMessage(param1,param2,param3);
      }
      
      public function AddMessageEx(param1:int, param2:String, param3:String, param4:int) : void
      {
         this.mainWindow.chatBar.AddMessage(param1,param2,param3,param4);
      }
      
      public function SetDefaultChannel(param1:int) : void
      {
         this.mainWindow.chatBar.SetDefaultChannel(param1);
      }
      
      public function SetPlayerIcon(param1:int, param2:String) : void
      {
         this.mainWindow.chatBar.SetPlayerIcon(param1,param2);
      }
      
      public function SetPlayerHeroId(param1:int, param2:int, param3:int) : void
      {
         this.mainWindow.chatBar.SetPlayerHeroId(param1,param2,param3);
      }
      
      public function EnterPressed() : void
      {
         if(this.mainWindow.reportWindow.IsInFocus())
         {
            this.mainWindow.reportWindow.OnEnterPressed();
            return;
         }
         this.mainWindow.chatBar.EnterPressed();
      }
      
      public function OpenChanel(param1:int) : void
      {
         this.mainWindow.chatBar.OpenChannel(param1);
      }
      
      public function AddChannelShortCut(param1:int, param2:String) : void
      {
         this.mainWindow.chatBar.AddChannelShortCut(param1,param2);
      }
      
      public function ShowVoteWindow(param1:int, param2:Boolean) : void
      {
         this.mainWindow.voting_mc.ShowVoteWindow(param1,param2);
      }
      
      public function HideVoteWindow() : void
      {
         this.mainWindow.voting_mc.HideVoteWindow();
      }
      
      public function SetSurrenderVoteStatus(param1:int, param2:int, param3:int) : void
      {
         this.mainWindow.voting_mc.SetSurrenderVoteStatus(param1,param2,param3);
      }
      
      public function ShowTryExit() : void
      {
         var _loc1_:Boolean = false;
         var _loc2_:PlayerStatisticData = null;
         for each(_loc2_ in this.mainWindow.Players)
         {
            if(_loc2_.IsBot)
            {
               _loc1_ = true;
               break;
            }
         }
         this.mainWindow.ShowExitGameDialog(_loc1_,this.spectatorMode);
      }
      
      public function SetStatisticExperienceBar(param1:int, param2:int, param3:Array, param4:int = 0) : void
      {
         this.mainWindow.finalStat.personalStat.SetStatisticExperienceBar(param1,param2,param4,param3);
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
      
      public function SetChatVisible(param1:Boolean) : void
      {
         this.mainWindow.chatBar.DisableInput(!param1);
         this.mainWindow.chatBar.visible = param1;
      }
      
      public function SetCameraMode(param1:Boolean) : void
      {
         this.mainWindow.miniMap_mc.miniMapAnim_mc.SetCameraMode(param1);
      }
      
      public function AddFriend(param1:String) : void
      {
         this.mainWindow.chatBar.AddFriend(param1);
      }
      
      public function SetMinimapEffect(param1:int, param2:int) : void
      {
         this.mainWindow.miniMap_mc.miniMapAnim_mc.SetMinimapEffect(param1,param2);
      }
      
      public function UpdateMinimapEffect(param1:int, param2:Number, param3:Number) : void
      {
         this.mainWindow.miniMap_mc.miniMapAnim_mc.UpdateMinimapEffect(param1,param2,param3);
      }
      
      public function IgnoreUser(param1:int) : void
      {
         this.SetIgnoring(param1,true);
         var _loc2_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc2_ != null)
         {
            this.mainWindow.chatBar.AddIgnoringUser(param1,_loc2_.PlayerName);
         }
         else
         {
            this.mainWindow.chatBar.AddIgnoringUser(param1);
         }
      }
      
      public function RemoveIgnore(param1:int) : void
      {
         this.SetIgnoring(param1,false);
         var _loc2_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc2_ != null)
         {
            this.mainWindow.chatBar.RemoveIgnoringUser(param1,_loc2_.PlayerName);
         }
         else
         {
            this.mainWindow.chatBar.RemoveIgnoringUser(param1);
         }
      }
      
      private function SetIgnoring(param1:int, param2:Boolean) : void
      {
         if(param1 < 0)
         {
            return;
         }
         var _loc3_:PlayerStatisticData = this.GetPlayer(param1);
         if(_loc3_ == null)
         {
            return;
         }
         _loc3_.IsIgnoring = param2;
         var _loc4_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc4_ != null)
         {
            _loc4_.isHeroIgnored = param2;
            if(_loc4_.IsSelected())
            {
               this.mainWindow.selectionInfo_mc.selectionInfoHero_mc.isHeroIgnored = param2;
            }
         }
         this.mainWindow.statisticsBar_mc.allStats_mc.SetIgnoreStatus(param1,param2);
      }
      
      public function ShowUIBlock(param1:String, param2:Boolean) : void
      {
         if(this.mainWindow.ProcessSpecialUiBlock(param1,param2))
         {
            return;
         }
         if(this.mainWindow.TutorialBlocks[param1] == null)
         {
            return;
         }
         this.mainWindow.TutorialBlocks[param1].ShowElements(param2);
      }
      
      public function ShowAllUIBlocks(param1:Boolean) : void
      {
         var _loc2_:TutorialShower = null;
         for each(_loc2_ in this.mainWindow.TutorialBlocks)
         {
            _loc2_.ShowElements(param1);
         }
         this.mainWindow.ProcessSpecialUiBlock(ETutorialBlocks.TalentsSetBlock,param1);
         this.mainWindow.ProcessSpecialUiBlock(ETutorialBlocks.Buffs,param1);
      }
      
      public function ShowNPC(param1:String, param2:String) : void
      {
         var _loc3_:HeroMateInfo = this.mainWindow.heroInfo_mc.heroInfoAnim_mc;
         _loc3_.ShowNPC(param1,param2);
      }
      
      public function HideNPC() : void
      {
         this.mainWindow.heroInfo_mc.heroInfoAnim_mc.HideNPC();
      }
      
      public function StartCinematic() : void
      {
         var _loc2_:BaseResizableObject = null;
         var _loc1_:int = 0;
         while(_loc1_ < this.mainWindow.numChildren)
         {
            _loc2_ = this.mainWindow.getChildAt(_loc1_) as BaseResizableObject;
            if(_loc2_ != null)
            {
               _loc2_.ShowCinematicDialog();
            }
            _loc1_++;
         }
      }
      
      public function StopCinematic() : void
      {
         var _loc2_:BaseResizableObject = null;
         var _loc1_:int = 0;
         while(_loc1_ < this.mainWindow.numChildren)
         {
            _loc2_ = this.mainWindow.getChildAt(_loc1_) as BaseResizableObject;
            if(_loc2_ != null)
            {
               _loc2_.HideCinematicDialog();
            }
            _loc1_++;
         }
      }
      
      public function ShowCinematicNPC(param1:String, param2:String) : void
      {
         this.mainWindow.NPCLineDown_mc.ShowCinematicNPC(param1,param2);
      }
      
      public function ShowCinematicMessage(param1:String, param2:uint = 16373882) : void
      {
         this.mainWindow.NPCLineDown_mc.ShowCinematicMessage(param1,param2);
      }
      
      public function ShowMessageBox(param1:String, param2:String, param3:Boolean = false) : void
      {
         this.mainWindow.ShowMessageBox(param1,param2,param3);
      }
      
      public function ShowTutorialTalentHighlight(param1:int, param2:int, param3:*) : void
      {
         var _loc4_:Talent = this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
         if(_loc4_ == null)
         {
            return;
         }
         if(param3)
         {
            _loc4_.ShowTutorialHighlight();
         }
         else
         {
            _loc4_.HideTutorialHighlight();
         }
      }
      
      public function ShowTutorialInventoryItemHighlight(param1:int, param2:Boolean) : void
      {
         this.mainWindow.inventoriesBar_mc.ShowTutorialShopBarItemHighlight(param1,param2);
      }
      
      public function ShowTutorialShopItemHighlight(param1:int, param2:Boolean) : void
      {
         this.mainWindow.shopBar_mc.ShowTutorialShopBarItemHighlight(param1,param2);
      }
      
      public function ShowTutorialActionBarItemHighlight(param1:int, param2:Boolean) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.ShowTutorialActionBarItemHighlight(param1,param2);
      }
      
      public function ShowTutorialElementHighLight(param1:String, param2:*) : void
      {
         if(param1 == ETutorialHighlitedObjects.SelectionLevel)
         {
            this.mainWindow.selectionInfo_mc.selectionInfoHero_mc.ShowTutorialLevelHighlight(param2);
            return;
         }
         if(param2)
         {
            this.mainWindow.TutorialHighlighters[param1].ShowTutorialHighlight();
         }
         else
         {
            this.mainWindow.TutorialHighlighters[param1].HideTutorialHighlight();
         }
      }
      
      public function ShowTutorialHeroHighlight(param1:int, param2:Boolean) : *
      {
         var _loc3_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc3_ == null)
         {
            return;
         }
         _loc3_.ShowTutorialHighlight(param2);
      }
      
      public function ShowTutorialHeroLevelHighlight(param1:int, param2:Boolean) : *
      {
         var _loc3_:MateInfoBase = this.GetMateInfo(param1);
         if(_loc3_ == null)
         {
            return;
         }
         _loc3_.ShowTutorialLevelHighlight(param2);
      }
      
      public function ShowTutorialOvertipLevelHighlight(param1:Boolean, param2:Number, param3:Number) : *
      {
         this.mainWindow.ShowTutorialOvertipLevelHighlight(param1,param2,param3);
      }
      
      public function AddQuest(param1:int, param2:Boolean, param3:String, param4:int) : *
      {
         this.mainWindow.questTracker_mc.AddQuest(param1,param2,param3,param4);
      }
      
      public function UpdateQuest(param1:int, param2:int) : *
      {
         this.mainWindow.questTracker_mc.UpdateQuest(param1,param2);
      }
      
      public function UpdateQuestText(param1:int, param2:String) : *
      {
         this.mainWindow.questTracker_mc.UpdateQuestText(param1,param2);
      }
      
      public function RemoveQuest(param1:int) : *
      {
         this.mainWindow.questTracker_mc.RemoveQuest(param1);
      }
      
      public function SetQuestHintLine(param1:String, param2:String = "None") : void
      {
         this.mainWindow.actionBarContainer.SetHintline(param1,param2);
      }
      
      public function SetForceColors(param1:Array, param2:Array) : void
      {
         var _loc4_:ForceColors = null;
         if(param1.length != param2.length)
         {
            return;
         }
         var _loc3_:int = 0;
         while(_loc3_ < param1.length)
         {
            _loc4_ = new ForceColors();
            _loc4_.Force = param1[_loc3_];
            _loc4_.Color = param2[_loc3_];
            this.forceColors.push(_loc4_);
            _loc3_++;
         }
      }
      
      public function ShowBubble(param1:String, param2:Boolean, param3:String, param4:String = "None", param5:int = 0) : void
      {
         var _loc6_:Number = 0;
         var _loc7_:Number = 0;
         this.mainWindow.bubbleVisabilityController.ShowBuble(this.mainWindow.BubleControls[param1],param2,param3,param4,param5,_loc6_,_loc7_);
      }
      
      public function ShowTalentBubble(param1:int, param2:int, param3:Boolean, param4:String, param5:String = "None", param6:int = 0) : void
      {
         var _loc7_:Talent = this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
         if(_loc7_ == null)
         {
            return;
         }
         this.mainWindow.bubbleVisabilityController.ShowBuble(_loc7_,param3,param4,param5,param6,10);
      }
      
      public function ShowActionBarButtonBubble(param1:int, param2:Boolean, param3:String, param4:String = "None", param5:int = 0) : void
      {
         var _loc6_:MovieClip = this.mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(param1);
         if(_loc6_ == null)
         {
            return;
         }
         this.mainWindow.bubbleVisabilityController.ShowBuble(_loc6_,param2,param3,param4,param5);
      }
      
      public function ShowInventoryBarButtonBubble(param1:int, param2:Boolean, param3:String, param4:String = "None", param5:int = 0) : void
      {
         var _loc6_:InventoryButton = this.mainWindow.inventoriesBar_mc.GetInventoryItem(param1);
         if(_loc6_ == null)
         {
            return;
         }
         this.mainWindow.bubbleVisabilityController.ShowBuble(_loc6_,param2,param3,param4,param5);
      }
      
      public function ShowShopBarButtonBubble(param1:int, param2:Boolean, param3:String, param4:String = "None", param5:int = 0) : void
      {
         var _loc6_:MovieClip = this.mainWindow.shopBar_mc.GetShopBarItem(param1);
         if(_loc6_ == null)
         {
            return;
         }
         this.mainWindow.bubbleVisabilityController.ShowBuble(_loc6_,param2,param3,param4,param5);
      }
      
      public function ShowHeroBubble(param1:int, param2:Boolean, param3:String, param4:String = "None", param5:int = 0) : void
      {
         var _loc6_:MovieClip = this.GetMateInfo(param1);
         if(_loc6_ == null)
         {
            return;
         }
         this.mainWindow.bubbleVisabilityController.ShowBuble(_loc6_,param2,param3,param4,param5,20);
      }
      
      public function GetHeroDisplayObject(param1:int, param2:Boolean = false) : DisplayObject
      {
         var _loc3_:MovieClip = this.GetMateInfo(param1);
         if(param2)
         {
            return this.mainWindow.bubbleVisabilityController.GetBubble(_loc3_);
         }
         return _loc3_;
      }
      
      public function GetNamedDisplayObject(param1:String, param2:Boolean = false) : DisplayObject
      {
         if(param2)
         {
            return this.mainWindow.bubbleVisabilityController.GetBubble(this.mainWindow.BubleControls[param1]);
         }
         return this.mainWindow.ObjectsForGreyMask[param1];
      }
      
      public function GetTalentBarItemDisplayObject(param1:int, param2:int, param3:Boolean = false) : DisplayObject
      {
         if(param3)
         {
            return this.mainWindow.bubbleVisabilityController.GetBubble(this.mainWindow.talentsBar_mc.GetTalent(param1,param2));
         }
         return this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
      }
      
      public function GetActionBarItemDisplayObject(param1:int, param2:Boolean = false) : DisplayObject
      {
         if(param2)
         {
            return this.mainWindow.bubbleVisabilityController.GetBubble(this.mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(param1));
         }
         return this.mainWindow.actionBarContainer.actionBar_mc.GetActionBarItem(param1);
      }
      
      public function GetShopBarItemDisplayObject(param1:int, param2:Boolean = false) : DisplayObject
      {
         if(param2)
         {
            return this.mainWindow.bubbleVisabilityController.GetBubble(this.mainWindow.shopBar_mc.GetShopBarItem(param1));
         }
         return this.mainWindow.shopBar_mc.GetShopBarItem(param1);
      }
      
      public function GetInventoryBarItemDisplayObject(param1:int, param2:Boolean = false) : DisplayObject
      {
         if(param2)
         {
            return this.mainWindow.bubbleVisabilityController.GetBubble(this.mainWindow.inventoriesBar_mc.GetInventoryItem(param1));
         }
         return this.mainWindow.inventoriesBar_mc.GetInventoryItem(param1);
      }
      
      public function IsWindowVisible(param1:int) : Boolean
      {
         return this.mainWindow.IsWindowVisible(param1);
      }
      
      public function ShowCinematicNextButton(param1:Boolean) : void
      {
         this.mainWindow.NPCLineDown_mc.next_btn.visible = param1;
      }
      
      public function EnableCinematicNextButton(param1:Boolean) : void
      {
         if(param1)
         {
            this.mainWindow.NPCLineDown_mc.next_btn.enable();
         }
         else
         {
            this.mainWindow.NPCLineDown_mc.next_btn.disable();
         }
      }
      
      public function ShowCinematicNextButtonAnim(param1:Boolean) : void
      {
         if(param1)
         {
            this.mainWindow.NPCLineDown_mc.next_btn.startWaitingAnim();
         }
         else
         {
            this.mainWindow.NPCLineDown_mc.next_btn.stopWaitingAnim();
         }
      }
      
      public function SetPortalStatus(param1:int, param2:Number, param3:Number) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.portalBtn.SetPortalStatus(param1,param2,param3);
      }
      
      public function ShowCTEUi(param1:Boolean) : void
      {
         this.mainWindow.miniMap_mc.miniMapAnim_mc.ShowCTEUi(param1);
      }
      
      public function SetCTEAltarStatus(param1:int, param2:int, param3:int, param4:int) : void
      {
         this.mainWindow.miniMap_mc.miniMapAnim_mc.SetCTEAltarStatus(param1,param2,param3,param4);
      }
      
      public function LockActionBar(param1:Boolean) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.LockActionBar(param1);
      }
      
      public function ShowPanel(param1:Boolean) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.ShowPanel(param1);
      }
      
      public function TempUnlockActionBar(param1:Boolean) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.TempUnlockActionBar(param1);
      }
      
      public function BlockActionBar(param1:Boolean) : void
      {
         this.mainWindow.actionBarContainer.actionBar_mc.BlockActionBar(param1);
      }
      
      public function SetAwardDeltaRaiting(param1:Number, param2:String, param3:String) : void
      {
         var _loc7_:int = 0;
         var _loc8_:int = 0;
         var _loc9_:* = 0;
         var _loc4_:PlayerStatisticData = this.GetPlayer(this.ourID);
         if(_loc4_ == null)
         {
            return;
         }
         var _loc5_:Number = int(_loc4_.HeroRaiting + param1);
         var _loc6_:* = _loc4_.leaguePlaces.length;
         trace("newPlayerRating =",_loc5_);
         if(param1 != 0 && _loc4_.ownLeaguePlace != 0)
         {
            if(_loc4_.ownLeaguePlace <= 10)
            {
               if(_loc5_ >= _loc4_.leaguePlaces[0])
               {
                  _loc4_.newLeaguePlace = 1;
               }
               else if(_loc5_ <= _loc4_.leaguePlaces[_loc4_.leaguePlaces.length - 1])
               {
                  _loc4_.newLeaguePlace = 21;
               }
               else
               {
                  _loc7_ = 0;
                  while(_loc7_ < _loc6_)
                  {
                     if(_loc4_.leaguePlaces[_loc7_] < _loc5_)
                     {
                        _loc4_.newLeaguePlace = _loc7_ + 1;
                        break;
                     }
                     _loc7_++;
                  }
               }
            }
            else if(_loc5_ >= _loc4_.leaguePlaces[0])
            {
               _loc4_.newLeaguePlace = _loc4_.ownLeaguePlace - 11;
            }
            else if(_loc5_ <= _loc4_.leaguePlaces[_loc4_.leaguePlaces.length - 1])
            {
               _loc4_.newLeaguePlace = _loc4_.ownLeaguePlace + _loc4_.leaguePlaces.length - 11;
            }
            else if(param1 < 0)
            {
               _loc8_ = 11;
               while(_loc8_ < _loc6_)
               {
                  if(_loc4_.leaguePlaces[_loc8_] < _loc5_)
                  {
                     _loc4_.newLeaguePlace = _loc4_.ownLeaguePlace + _loc8_ + 1 - 11;
                     break;
                  }
                  _loc8_++;
               }
            }
            else
            {
               _loc9_ = 9;
               while(_loc9_ >= 0)
               {
                  if(_loc4_.leaguePlaces[_loc9_] > _loc5_)
                  {
                     _loc4_.newLeaguePlace = _loc4_.ownLeaguePlace - (9 - _loc9_);
                     break;
                  }
                  _loc9_--;
               }
            }
         }
         _loc4_.leaguePlaceChange = _loc4_.ownLeaguePlace - _loc4_.newLeaguePlace;
         trace("player.LeagueIndex =",_loc4_.LeagueIndex);
         trace("player.LeaguePlace =",_loc4_.ownLeaguePlace);
         trace("player.newLeaguePlace =",_loc4_.newLeaguePlace);
         trace("player.leaguePlaces =",_loc4_.leaguePlaces);
         _loc4_.HeroRaiting += param1;
         _loc4_.RaitingDelta = param1;
         _loc4_.HeroRankIcon = param2;
         _loc4_.HeroRankTooltip = param3;
         this.mainWindow.finalStat.personalStat.SetHeroRank(_loc4_.HeroRaiting,param1,param2,param3,_loc4_.LeagueIndex,_loc4_.newLeaguePlace,_loc4_.leaguePlaceChange);
         if(_loc4_.HeroId != this.ourID)
         {
            return;
         }
         if(this.mainWindow.finalStat.batleStatistic != null)
         {
            this.mainWindow.finalStat.batleStatistic.SetHeroIdentity(_loc4_);
         }
         else
         {
            this.mainWindow.statisticsBar_mc.allStats_mc.SetHeroIdentity(_loc4_);
         }
      }
      
      public function SetFirstWin(param1:String, param2:String) : void
      {
         this.mainWindow.finalStat.personalStat.SetFirstWin(param1,param2);
      }
      
      public function GetTalentActionBarIndex(param1:int, param2:int) : int
      {
         var _loc3_:Talent = this.mainWindow.talentsBar_mc.GetTalent(param1,param2);
         if(_loc3_ == null)
         {
            return -1;
         }
         return this.mainWindow.actionBarContainer.actionBar_mc.GetActionButtonIndex(_loc3_.shortcut);
      }
      
      public function GetInventoryItemActionBarIndex(param1:int) : int
      {
         var _loc2_:InventoryButton = this.mainWindow.inventoriesBar_mc.GetInventoryItemBySlot(param1);
         if(_loc2_ == null)
         {
            return -1;
         }
         return this.mainWindow.actionBarContainer.actionBar_mc.GetActionButtonIndex(_loc2_.Shortcut);
      }
      
      public function HideSurrenderButtonForEscMenu() : void
      {
         this.mainWindow.escMenu_mc.surrender_btn.disable();
      }
      
      public function SwitchToTutorialMode() : void
      {
         this.mainWindow.escMenu_mc.tutorialMode = true;
      }
      
      public function SwitchToSpectatorMode() : void
      {
         this.spectatorMode = true;
         this.mainWindow.escMenu_mc.IsSpectatorModeOn = true;
         this.HideSurrenderButtonForEscMenu();
         this.mainWindow.miniMap_mc.DisablePingButton();
         this.mainWindow.chatBar.DisableInput(true);
         this.friendlyTeamController.SpectatorMode = true;
         this.enemyTeamController.SpectatorMode = true;
         this.mainWindow.finalStat.SetSpectatorMode();
         this.mainWindow.statisticsBar_mc.allStats_mc.SwitchToSpectatorMode();
      }
      
      public function SetPveBacks(param1:String, param2:String, param3:String) : void
      {
         this.mainWindow.statisticsBar_mc.SetPveBacks(param1,param2,param3);
      }
      
      public function RaitingPenaltyEnabled(param1:Boolean) : void
      {
         this.mainWindow.escMenu_mc.IsCurrentGameIsTraining = !param1;
      }
      
      public function SetReportCounters(param1:int, param2:int) : void
      {
         this.mainWindow.reportWindow.SetReportCounters(param1,param2);
      }
      
      public function AddReportItem(param1:int, param2:String) : void
      {
         this.mainWindow.reportWindow.AddReportComboboxItem(param1,param2);
      }
      
      public function SetBindString(param1:String) : void
      {
         var _loc2_:Array = new Array();
         _loc2_.push(param1);
         this.mainWindow.optionsWindow.shortcutsTab.FillBindSettings(param1);
         if(param1.indexOf(" ") == -1)
         {
            return;
         }
         var _loc3_:String = "cmd_action_bar_slot";
         if(param1.indexOf(_loc3_) == -1)
         {
            return;
         }
         param1 = param1.slice(param1.indexOf(_loc3_) + _loc3_.length,param1.length);
         var _loc4_:int = int(param1.slice(0,param1.indexOf(" ")));
         if(param1.indexOf("\'") == -1)
         {
            return;
         }
         var _loc5_:String = param1.slice(param1.indexOf("\'") + 1,param1.length);
         _loc5_ = _loc5_.slice(0,_loc5_.indexOf("\'"));
         if(param1.indexOf("+",0) != -1)
         {
            param1 = param1.slice(param1.indexOf("+"),param1.length);
            param1 = param1.slice(param1.indexOf("\'") + 1,param1.length);
            _loc5_ = _loc5_ + " + " + param1.slice(0,param1.indexOf("\'"));
         }
         this.mainWindow.actionBarContainer.actionBar_mc.SetBindToSlot(_loc4_,_loc5_);
      }
      
      public function ShowQuestHint(param1:String, param2:String, param3:String, param4:String) : void
      {
         this.mainWindow.questHint.SetQuestHint(param1,param2,param3,param4);
         this.mainWindow.OpenWindow(EWindows.WINDOW_QUESTHINT);
      }
      
      public function GetPlayerIdxByHeroIdx(param1:int) : int
      {
         var _loc2_:int = -1;
         var _loc3_:int = int(this.enemyTeamController.GetMatesCount());
         var _loc4_:int = int(this.friendlyTeamController.GetMatesCount());
         var _loc5_:int = _loc3_ - (param1 - _loc4_ + 1);
         if(param1 >= 0 && param1 < _loc4_)
         {
            _loc2_ = this.friendlyTeamController.GetMateInfoByIdx(param1);
         }
         else if(param1 >= _loc4_ && _loc5_ >= 0 && _loc5_ < _loc3_)
         {
            _loc2_ = this.enemyTeamController.GetMateInfoByIdx(_loc5_);
         }
         return _loc2_;
      }
      
      public function SmartChatActivate() : void
      {
         this.mainWindow.smartChat.Show();
      }
      
      public function SmartChatActivateCategory(param1:uint) : void
      {
         this.mainWindow.smartChat.ShowCategory(param1);
      }
      
      public function SmartChatDeactivate() : void
      {
         this.mainWindow.smartChat.Hide();
      }
      
      public function SmartChatBeginUpdate() : void
      {
         this.mainWindow.smartChat.BeginUpdate();
      }
      
      public function SmartChatEndUpdate() : void
      {
         this.mainWindow.smartChat.EndUpdate();
      }
      
      public function SmartChatBeginCategory(param1:uint, param2:String, param3:String) : void
      {
         this.mainWindow.smartChat.BeginCategory(param1,param2,param3);
      }
      
      public function SmartChatEndCategory() : void
      {
         this.mainWindow.smartChat.EndCategory();
      }
      
      public function SmartChatAddMessage(param1:uint, param2:String, param3:String) : void
      {
         this.mainWindow.smartChat.AddMessage(param1,param2,param3);
      }
      
      public function SmartChatAddCancel(param1:uint, param2:String) : void
      {
         this.mainWindow.smartChat.AddCancel(param1,param2);
      }
      
      public function DayNightUpdateState(param1:Boolean, param2:Number) : void
      {
         this.mainWindow.dayNight_mc.dayNightAnim_mc.Night = param1;
         this.mainWindow.dayNight_mc.dayNightAnim_mc.StateFraction = param2;
      }
      
      public function DayNightHideUI(param1:Boolean) : void
      {
         this.mainWindow.dayNight_mc.visible = !param1;
         this.mainWindow.miniMap_mc.GameTimerVisible = param1;
      }
      
      public function SetChatOff(param1:Boolean) : void
      {
         this.mainWindow.chatBar.chatInput_mc.chatOutput_cb.Value = param1 ? 1 : 0;
         this.mainWindow.optionsWindow.settingsTab.smartchat_cb.Value = param1 ? 1 : 0;
      }
   }
}

