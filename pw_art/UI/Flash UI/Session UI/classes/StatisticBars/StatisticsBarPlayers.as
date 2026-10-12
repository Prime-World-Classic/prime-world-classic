package StatisticBars
{
	import Components.PWCheckBox;
	import BaseClasses.MateInfoBase;
	import BaseClasses.TooltipSender;
	import Components.FlagFormater;
	import Components.HeroRankIconHolder;
	import Components.PremiumShields;
	import Controllers.TextFieldFormater;
	import Datas.PlayerStatisticData;
	import flash.display.MovieClip;
	import flash.display.Shape;
	import flash.utils.getDefinitionByName;
	import flash.events.Event;
	import flash.text.TextField;
	import Interfaces.IMate;
	import src.ButtonTooltipEvent;
	import flash.events.MouseEvent;
	import flash.geom.Point;
	import Events.PWComponentValueEvent;

	public class StatisticsBarPlayers extends MovieClip implements IMate
	{
		public var teamMateStat_mc:StatisticsMate;
		public var playerName:TextField;
		public var playerHero:TextField;
		public var sumKillStat:TextField;
		public var sumDeathStat:TextField;
		public var sumSupportStat:TextField;
		public var sumPointStat:TextField;
		public var sumPrimeStat:TextField;
		
		public var _kills:int;
		public var _prime:int;
		public var elitHeroIcon:HeroRankIconHolder;
		public var party_mc:MovieClip;
		
		public var might:CurrentStatisticIcons;
		public var scythe:CurrentStatisticIcons;
		public var fist:CurrentStatisticIcons;
		public var cup:CurrentStatisticIcons;
		public var prime:CurrentStatisticIcons;
		
		public var muteToggle_cmp:PWCheckBox;
		
		private var playerNameHolder:TextFieldFormater;
		private var playerHeroClassHolder:TextFieldFormater;
		private var _playerData:PlayerStatisticData;
		public var premiumIcon_mc:PremiumShields;
		public var flag:FlagFormater;
		public var mute:MovieClip;
		public var redBack:MovieClip;
		
		private var _partyMarkerTooltip:TooltipSender;

		// Иконка рейтинга аккаунта над иконкой рейтинга героя, на золотистой подложке как на
		// экране загрузки (PWFrames/raitingBackAcc: #F0FC31, 30%). Создаётся кодом из того же
		// символа, что и elitHeroIcon; подложка — отдельный Shape (graphics самого спрайта
		// движок не рисует).
		private static const RANK_ICON_SIZE:Number = 23;
		private static const ACC_BACK_COLOR:uint = 0xF0FC31;
		private static const ACC_BACK_ALPHA:Number = 0.3;
		private var accRankIcon:HeroRankIconHolder;
		private var accRankBack:Shape;
		private var _muteTooltip:TooltipSender;
		
		public function StatisticsBarPlayers()
		{
			_partyMarkerTooltip = new TooltipSender(party_mc);
			_muteTooltip = new TooltipSender(mute);
			
			addEventListener(Event.ADDED_TO_STAGE, OnAddedToStage);
			muteToggle_cmp.addEventListener(MouseEvent.MOUSE_OVER, onMuteToggleOver);
			muteToggle_cmp.addEventListener(MouseEvent.MOUSE_OUT, onMuteToggleOut);
			muteToggle_cmp.addEventListener(PWComponentValueEvent.CHANGE, ChangeToggleMute)
		}
		
		private function OnAddedToStage(e:Event):void 
		{
			removeEventListener(Event.ADDED_TO_STAGE, OnAddedToStage);
			this.visible = false;
			elitHeroIcon.visible = false;
			flag.visible = false;
			
			playerNameHolder = new TextFieldFormater(playerName);
			playerHeroClassHolder = new TextFieldFormater(playerHero);
			
			party_mc.visible = false;
			mute.visible = false;
			redBack.visible = false;
			muteToggle_cmp.visible = false;
		}
		
		public function ChangeToggleMute(e:PWComponentValueEvent):void 
		{
			if (e.ControlName == "muteToggle_cmp")
			{
				e.stopPropagation();
				
				if (e.componentValue == 1)
				{
					FSCommands.Send(FSCommands.IGNORE_USER, _playerData.HeroId.toString());
				}
				else
				{
					FSCommands.Send(FSCommands.REMOVE_IGNORE, _playerData.HeroId.toString());
				}
			}
		}
		
		public function set MightTooltip(value:String):void
		{
			might.Tooltip = value;
		}
		
		public function get MightTooltip():String
		{
			return might.Tooltip;
		}
		
		public function set ScytheTooltip(value:String):void
		{
			scythe.Tooltip = value
		}
		
		public function get ScytheTooltip():String
		{
			return scythe.Tooltip;
		}
		
		public function set FistTooltip(value:String):void
		{
			fist.Tooltip = value;
		}
		
		public function get FistTooltip():String
		{
			return fist.Tooltip;
		}
		
		public function set CupTooltip(value:String):void
		{
			cup.Tooltip = value;
		}
		
		public function get CupTooltip():String
		{
			return cup.Tooltip
		}
		
		public function get PrimeTooltip():String
		{
			return prime.Tooltip;
		}
		
		public function set PrimeTooltip(value:String):void
		{
			prime.Tooltip = value;
		}
		
		public function get PartyMarkerTooltip():String
		{
			return _partyMarkerTooltip.TooltipText;
		}
		
		public function set PartyMarkerTooltip(value:String):void
		{
			_partyMarkerTooltip.TooltipText = value;
		}
		
		public function get MuteTooltip():String
		{
			return _muteTooltip.TooltipText;
		}
		
		public function set MuteTooltip(value:String):void
		{
			_muteTooltip.TooltipText = value;
		}
		
		public function set TextColors(color:uint):void
		{
			sumKillStat.textColor = color;
			sumDeathStat.textColor = color;
			sumSupportStat.textColor = color;
			sumPointStat.textColor = color;
			sumPrimeStat.textColor = color;
		}
		
		public function get isBusy():Boolean
		{
			return teamMateStat_mc.isBusy;
		}
		
		public function set isBusy(busy:Boolean):void
		{
			teamMateStat_mc.isBusy = busy;
		}
		
		public function set id(id:int):void
		{
			teamMateStat_mc.id = id
		}
		
		public function get id():int
		{
			return teamMateStat_mc.id;
		}
		
		public function set level(value:int):void
		{
			teamMateStat_mc.level = value;
		}
		
		public function SetGreyMask(show:Boolean):void
		{
			teamMateStat_mc.SetGreyMask(show);
		}
		
		public function SetDeathTimeOut(timeToRessurect:int):void
		{
			teamMateStat_mc.SetDeathTimeOut(timeToRessurect);
		}
		
		public function SetHeroGameProgress(playerData:PlayerStatisticData):void
		{
			_kills = playerData.Kills;
			_prime = playerData.CurrentPrime;
			sumKillStat.text = playerData.Kills.toString();
			sumDeathStat.text = playerData.Deaths.toString()
			sumSupportStat.text = playerData.Assists.toString();
			sumPointStat.text = playerData.Score.toString();
			sumPrimeStat.text = playerData.CurrentPrime.toString();
		}
		
		public function SetMateSelected(isSelect:Boolean):void
		{
			teamMateStat_mc.SetSelected(isSelect);
		}
		
		public function OnHeroLeft(leaveType:int):void
		{
			teamMateStat_mc.OnPlayerLeft(leaveType);
		}
		
		public function SetSelected(isSelect:Boolean):void
		{
			teamMateStat_mc.SetSelected(isSelect);
		}
		
		public function SetFriendshipRelation(areFriends:Boolean,isPending:Boolean):void
		{
			teamMateStat_mc.SetFriendshipRelation(areFriends,isPending);
		}
		
		public function SwitchToFinalStatistic():void
		{
			teamMateStat_mc.SetViewInFinalStatistic();
			if (_playerData == null)
				return;
			
			level = _playerData.Level;
			_prime = _playerData.Prime;
			sumPrimeStat.text = _playerData.Prime.toString();
		}
		
		public function OnHeroReturn():void
		{
			teamMateStat_mc.OnPlayerReturn();
		}
		
		public function SetHeroRang(rankIcon:String, rankTooltip:String):void 
		{
			elitHeroIcon.visible = rankIcon.length != 0;
			elitHeroIcon.Icon = rankIcon;
			elitHeroIcon.TooltipText = rankTooltip;
		}

		private static function RankLine(template:String, rankName:String, rating:Number):String
		{
			return template.replace("{0}", rankName != null ? rankName : "").replace("{1}", int(rating).toString());
		}

		// Подсказки: у иконки героя — его ранг и рейтинг; у иконки аккаунта — ранг и рейтинг
		// аккаунта, ниже — ранг и рейтинг героя.
		private function SetRankTooltips(playerData:PlayerStatisticData):void
		{
			var heroLine:String = "";
			if (playerData.HeroRankIcon != null && playerData.HeroRankIcon.length != 0)
			{
				heroLine = RankLine(Localization.StatBar_HeroRankTooltip, playerData.HeroRankTooltip, playerData.HeroRaiting);
				elitHeroIcon.TooltipText = heroLine;
			}

			var hasAccount:Boolean = playerData.AccRankIcon != null && playerData.AccRankIcon.length != 0;
			if (!hasAccount)
			{
				if (accRankIcon != null)
				{
					accRankIcon.visible = false;
					accRankBack.visible = false;
				}
				return;
			}

			if (!CreateAccRankIcon())
				return;

			accRankIcon.visible = true;
			accRankBack.visible = true;
			accRankIcon.Icon = playerData.AccRankIcon;
			var text:String = RankLine(Localization.StatBar_AccountRankTooltip, playerData.AccRankName, playerData.AccRaiting);
			if (heroLine.length != 0)
				text += "<br>" + heroLine;
			accRankIcon.TooltipText = text;
		}

		private function CreateAccRankIcon():Boolean
		{
			if (accRankIcon != null)
				return true;
			try
			{
				var iconClass:Class = getDefinitionByName("statBarEliteHeroIcon") as Class;
				accRankIcon = new iconClass() as HeroRankIconHolder;
			}
			catch (e:Error)
			{
				accRankIcon = null;
			}
			if (accRankIcon == null)
				return false;

			accRankIcon.x = elitHeroIcon.x;
			accRankIcon.y = elitHeroIcon.y - RANK_ICON_SIZE - 3;

			accRankBack = new Shape();
			accRankBack.graphics.beginFill(ACC_BACK_COLOR, ACC_BACK_ALPHA);
			accRankBack.graphics.drawRoundRect(-2, -2, RANK_ICON_SIZE + 4, RANK_ICON_SIZE + 4, 8, 8);
			accRankBack.graphics.endFill();
			accRankBack.x = accRankIcon.x;
			accRankBack.y = accRankIcon.y;

			var index:int = getChildIndex(elitHeroIcon);
			addChildAt(accRankBack, index);
			addChildAt(accRankIcon, index + 1);
			return true;
		}
		
		public function SetPremium(isPremium:Boolean, originalFraction:int,playerName:String):void 
		{
			premiumIcon_mc.SetPremium(isPremium, originalFraction,playerName);
		}
		
		public function SetPlayerData(playerData:PlayerStatisticData):void 
		{
			_playerData = playerData;
			
			playerHeroClassHolder.Text = playerData.ClassName;
			playerName.textColor = playerData.HeroNameColor;
			
			SetHeroRang(playerData.HeroRankIcon, playerData.HeroRankTooltip);
			SetRankTooltips(playerData);
			SetPremium(playerData.IsPremium, playerData.OriginalFraction, playerData.PlayerName);
			SetHeroGameProgress(playerData);
			playerNameHolder.Text = playerData.PlayerName;
			flag.visible = _playerData.playerFlagPath.length != 0;
			flag.Icon = _playerData.playerFlagPath;
			flag.TooltipText = _playerData.playerFlagTooltip;
			teamMateStat_mc.SetHero(playerData);
			
			if (playerData.IsOurHero) SetSelected(true);			
			
			redBack.visible = _playerData.ShowRedBack;
			
			if (!playerData.IsOurHero && playerData.HeroId != -1 && playerData.PlayerName != "" && !playerData.IsBot)
			{
				muteToggle_cmp.visible = true;
				ChatMuted = playerData.ChatMuted;
			}
			if (playerData.IsOurHero && playerData.ChatMuted)
			{
				muteToggle_cmp.visible = true;
				muteToggle_cmp.Value = 1;
				muteToggle_cmp.Disable = true;
			}			
		}
		
		public function set ChatMuted(value:Boolean):void
		{
			//mute.visible = value;
			// Если замьючен на сервере не даём выбирать
			if (muteToggle_cmp.visible)
			{
				if (value)
				{
					muteToggle_cmp.Value = 1;
					muteToggle_cmp.Disable = true;
				}
				else
				{
					muteToggle_cmp.Value = _playerData.IsIgnoring ? 1 : 0;
					muteToggle_cmp.Disable = false;
				}
			}
		}
		
		public function set HeroIgnored(value:Boolean):void
		{
			teamMateStat_mc.isHeroIgnored = value;
			
			if (muteToggle_cmp.visible && muteToggle_cmp.togDisabled_mc.visible == false && muteToggle_cmp.disabled_mc.visible == false)
			{
				muteToggle_cmp.Value = value ? 1 : 0;
			}
		}
		
		private function onMuteToggleOver(e:MouseEvent):void
		{
			var tooltip:String = "";
			if (muteToggle_cmp.togDisabled_mc.visible == false && muteToggle_cmp.disabled_mc.visible == false)
			{
				if (muteToggle_cmp.Value == 1)
				{
					tooltip = Localization.ContextMenu_RemoveIgnore;
				}
				else
				{
					tooltip = Localization.ContextMenu_Ignore;
				}
			}
			else
			{
				tooltip = _muteTooltip.TooltipText;
			}
			
			var point:Point = this.muteToggle_cmp.localToGlobal(new Point());
			dispatchEvent(new ButtonTooltipEvent(ButtonTooltipEvent.ACTION_TYPE_IN, this, point.x, point.y, tooltip));			
		}
		
		private function onMuteToggleOut(e:MouseEvent):void
		{
			dispatchEvent(new ButtonTooltipEvent(ButtonTooltipEvent.ACTION_TYPE_OUT));	
		}
	}

}