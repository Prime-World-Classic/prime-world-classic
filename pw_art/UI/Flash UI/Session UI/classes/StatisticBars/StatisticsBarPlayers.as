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
	import flash.geom.ColorTransform;
	import flash.utils.getDefinitionByName;
	import flash.events.Event;
	import flash.text.TextField;
	import flash.text.TextFieldAutoSize;
	import Interfaces.IMate;
	import src.ButtonTooltipEvent;
	import flash.events.MouseEvent;
	import flash.geom.Point;
	import Events.PWComponentValueEvent;
	import EscMenu.ShortcutItem;
	import flash.display.Loader;
	import flash.display.Sprite;
	import flash.display.Shape;
	import flash.net.URLRequest;

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
		public var sumWardStat:TextField;   // wards placed: a copy of sumKillStat in main.fla, one column left of it

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

		// Иконка рейтинга аккаунта зеркальна иконке рейтинга героя относительно горизонтали
		// через центр портрета (как флаг слева сверху и уровень слева снизу). Создаётся кодом
		// из того же символа, что и elitHeroIcon; картинка ранга слегка тонируется в золото.
		// Размеры — из main.fla (statBarEliteHeroIcon 23x23, портрет sttisticsMate_mc 73x73):
		// width/height движка у таких клипов ненадёжны.
		private static const RANK_ICON_SIZE:Number = 23;
		private static const PORTRAIT_CENTER_Y:Number = 36.5;
		private static const ACC_GOLD_TINT:ColorTransform = new ColorTransform(1, 0.92, 0.55, 1, 70, 22, 0, 0);
		private var accRankIcon:HeroRankIconHolder;
		private var _muteTooltip:TooltipSender;

		// wards placed (lanterns of the shop): icon + count left of the stat columns
		private static var wardCounts:Object = new Object();
		private static const WARD_ICON:String = "/UI/Styles/Icons/Artifacts/EyeYellow.dds";
		private var wardIcon:Loader;
		private var wardHolder:MovieClip;      // значок + область наведения для подсказки
		private var wardTooltip:TooltipSender;
		private static const WARD_ICON_SIZE:Number = 22;
		private var wardCenterX:Number = NaN; // ось колонки фонарей: и значок, и число по её центру
		// фонари соперников видны только в итоговой статистике после боя
		private var finalStatistic:Boolean = false;


		// pings of the player off: button next to the chat mute (allies only, the pings of the enemies are not shown anyway)
		private static const PING_ICON:String = "/UI/Styles/Icons/PingWheel/ping_icon5.dds";
		private var pingMuteButton:Sprite;
		private var pingMuteIcon:Loader;
		private var pingMuted:Boolean = false;

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

			try
			{
				CreateWardsAndPingMute();
			}
			catch (e:Error)
			{
			}
		}

		private function CreateWardsAndPingMute():void
		{
			// the ward column goes before the kills one with the same step as between the stat columns
			var step:Number = scythe.x - might.x;
			var wardX:Number = might.x - step;

			// the names end before the ward column (long ones get "..." and a tooltip)
			playerNameHolder.RecalcTextWidth(Math.min(playerName.width, wardX - 4 - playerName.x));
			playerHeroClassHolder.RecalcTextWidth(Math.min(playerHero.width, wardX - 4 - playerHero.x));

			wardIcon = new Loader();
			wardIcon.mouseEnabled = false;
			wardIcon.load(new URLRequest(WARD_ICON));
			wardIcon.scaleX = WARD_ICON_SIZE / 64;
			wardIcon.scaleY = WARD_ICON_SIZE / 64;
			wardHolder = new MovieClip();
			var wardHit:Shape = new Shape(); // область наведения для подсказки «Поставлено фонарей»
			wardHit.graphics.beginFill(0x000000, 0.01);
			wardHit.graphics.drawRect(0, 0, WARD_ICON_SIZE, WARD_ICON_SIZE);
			wardHit.graphics.endFill();
			wardHolder.addChild(wardHit);
			wardHolder.addChild(wardIcon);
			wardHolder.x = wardX;
			wardHolder.y = might.y;
			wardTooltip = new TooltipSender(wardHolder);
			// the count on the line of the kills count; the icon is centred right above the count (UpdateWards)
			if (sumWardStat != null)
			{
				wardCenterX = sumKillStat.x - step + sumKillStat.width / 2;
				sumWardStat.y = sumKillStat.y;
				sumWardStat.autoSize = TextFieldAutoSize.NONE;
			}
			addChild(wardHolder);

			UpdateWards();

			pingMuteButton = new Sprite();
			pingMuteIcon = new Loader();
			pingMuteIcon.mouseEnabled = false;
			pingMuteIcon.load(new URLRequest(PING_ICON));
			pingMuteIcon.scaleX = 24 / 64;
			pingMuteIcon.scaleY = 24 / 64;
			pingMuteButton.addChild(pingMuteIcon);
			pingMuteButton.x = muteToggle_cmp.x - 28;
			pingMuteButton.y = muteToggle_cmp.y + 2;
			pingMuteButton.visible = false;
			pingMuteButton.addEventListener(MouseEvent.CLICK, OnPingMuteClick);
			pingMuteButton.addEventListener(MouseEvent.MOUSE_OVER, OnPingMuteOver);
			pingMuteButton.addEventListener(MouseEvent.MOUSE_OUT, onMuteToggleOut);
			addChild(pingMuteButton);
		}

		public static function SetWardCount(id:int, count:int):void
		{
			wardCounts[String(id)] = count;
		}

		public function UpdateWards():void
		{
			if (sumWardStat == null)
				return;
			var count:int = 0;
			if (_playerData != null && wardCounts[String(_playerData.HeroId)] != null)
				count = int(wardCounts[String(_playerData.HeroId)]);
			sumWardStat.text = count.toString();

			// соперникам во время боя фонари не показываем — только в итоговой статистике
			var shown:Boolean = finalStatistic || _playerData == null || !_playerData.IsEnemyTeam;
			sumWardStat.visible = shown;
			if (wardHolder != null)
			{
				wardHolder.visible = shown;
				wardTooltip.TooltipText = Localization.Tooltip_StatisticBarWardsIcon;
			}

			// движок не выравнивает текст по центру поля: поле по ширине числа, число и значок по оси колонки
			if (!isNaN(wardCenterX))
			{
				sumWardStat.width = Math.ceil(sumWardStat.textWidth) + 4;
				sumWardStat.x = Math.round(wardCenterX - sumWardStat.width / 2);
				if (wardHolder != null)
					wardHolder.x = Math.round(wardCenterX - WARD_ICON_SIZE / 2);
			}
		}

		private function UpdatePingMuteView():void
		{
			if (pingMuteIcon == null)
				return;
			pingMuteIcon.alpha = 1;
			if (pingMuted)
				pingMuteIcon.alpha = 0.3;
		}

		private function OnPingMuteClick(e:MouseEvent):void
		{
			try
			{
				if (_playerData == null)
					return;
				pingMuted = !pingMuted;
				UpdatePingMuteView();
				var value:String = "0";
				if (pingMuted)
					value = "1";
				FSCommands.Send("PingMuteUser", _playerData.HeroId.toString() + " " + value);
				OnPingMuteOver(null);
			}
			catch (err:Error)
			{
			}
		}

		private function OnPingMuteOver(e:MouseEvent):void
		{
			var tooltip:String = Localization.Statistics_PingMute;
			if (pingMuted)
				tooltip = Localization.Statistics_PingUnmute;
			var point:Point = pingMuteButton.localToGlobal(new Point());
			dispatchEvent(new ButtonTooltipEvent(ButtonTooltipEvent.ACTION_TYPE_IN, this, point.x, point.y, tooltip));
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
			finalStatistic = true;
			UpdateWards();
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

		// Подсказки: у иконки героя — ранг и рейтинг героя, у иконки аккаунта — аккаунта.
		private function SetRankTooltips(playerData:PlayerStatisticData):void
		{
			if (playerData.HeroRankIcon != null && playerData.HeroRankIcon.length != 0)
				elitHeroIcon.TooltipText = RankLine(Localization.StatBar_HeroRankTooltip, playerData.HeroRankTooltip, playerData.HeroRaiting);

			var hasAccount:Boolean = playerData.AccRankIcon != null && playerData.AccRankIcon.length != 0;
			if (!hasAccount)
			{
				if (accRankIcon != null)
					accRankIcon.visible = false;
				return;
			}

			if (!CreateAccRankIcon())
				return;

			accRankIcon.visible = true;
			accRankIcon.Icon = playerData.AccRankIcon;
			accRankIcon.TooltipText = RankLine(Localization.StatBar_AccountRankTooltip, playerData.AccRankName, playerData.AccRaiting);
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

			var centerY:Number = teamMateStat_mc.y + PORTRAIT_CENTER_Y;
			accRankIcon.x = elitHeroIcon.x;
			accRankIcon.y = 2 * centerY - (elitHeroIcon.y + RANK_ICON_SIZE);
			if (accRankIcon.eliteHero_mc != null)
				accRankIcon.eliteHero_mc.transform.colorTransform = ACC_GOLD_TINT;

			// В том же углу символа стоят значок премиума, отметка группы и мут чата —
			// иконка ниже их по слою, чтобы мут оставался кликабельным.
			addChildAt(accRankIcon, getChildIndex(elitHeroIcon) + 1);
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
			if (pingMuteButton != null)
			{
				pingMuteButton.visible = !playerData.IsOurHero && !playerData.IsEnemyTeam && playerData.HeroId != -1;
				UpdatePingMuteView();
			}
			UpdateWards();
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