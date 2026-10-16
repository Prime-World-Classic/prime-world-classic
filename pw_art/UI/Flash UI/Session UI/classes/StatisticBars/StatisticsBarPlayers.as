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
	import Controllers.SoundController;

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
		// x of might, scythe, fist and their counts in main.fla; with the ward column they move one step left
		private var wardBaseX:Array;
		private var wardShiftStep:Number = 0;


		// pings of the player off: button "!" left of the chat mute, drawn like it (pw_art/UI/PingMute; allies only:
		// the pings of the enemies are not shown anyway). C++ keeps the muted players in user.cfg by account id (SetPingMuted).
		private static const PING_MUTE_TEX:String = "/UI/Styles/Icons/PingMute/ping_mute_";
		private static const PING_MUTE_STATES:Array = ["default", "over", "down", "default_mute", "over_mute", "down_mute"];
		private static const PING_MUTE_SIZE:Number = 24.1; // as muteToggle_cmp (31 px picture in a 32 px texture)
		private static var pingMutedIds:Object = new Object();
		private var pingMuteButton:Sprite;
		private var pingMuteStates:Array;
		private var pingMuteOver:Boolean = false;
		private var pingMuteDown:Boolean = false;

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
			// the ward column goes between the assists and the score: kills, deaths and assists move one column
			// to the left (the step between the stat columns), the wards take the place of the assists
			var step:Number = scythe.x - might.x;
			// sizes from main.fla (fist 20x20, might 24x24): width/height of such clips are not reliable in the engine
			var wardIconCenterX:Number = fist.x + 10;
			var wardTextCenterX:Number = sumSupportStat.x + sumSupportStat.width / 2;
			var wardIconCenterY:Number = might.y + 12;
			wardShiftStep = step;
			wardBaseX = [might.x, scythe.x, fist.x, sumKillStat.x, sumDeathStat.x, sumSupportStat.x];
			var wardX:Number = Math.round(wardIconCenterX - WARD_ICON_SIZE / 2);

			// the names end before the first stat column of the shifted layout (long ones get "..." and a tooltip)
			playerNameHolder.RecalcTextWidth(Math.min(playerName.width, might.x - step - 4 - playerName.x));
			playerHeroClassHolder.RecalcTextWidth(Math.min(playerHero.width, might.x - step - 4 - playerHero.x));

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
			wardHolder.y = Math.round(wardIconCenterY - WARD_ICON_SIZE / 2);
			wardTooltip = new TooltipSender(wardHolder);
			// the count on the line of the other counts, centred under the icon (UpdateWards)
			if (sumWardStat != null)
			{
				wardCenterX = wardTextCenterX;
				sumWardStat.y = sumKillStat.y;
				sumWardStat.autoSize = TextFieldAutoSize.NONE;
			}
			addChild(wardHolder);

			UpdateWards();

			pingMuteButton = new Sprite();
			pingMuteButton.mouseChildren = false;
			// click area under the picture (an almost transparent fill does not catch clicks in the engine)
			var pingHit:Shape = new Shape();
			pingHit.graphics.beginFill(0x000000, 0.35);
			pingHit.graphics.drawCircle(PING_MUTE_SIZE / 2, PING_MUTE_SIZE / 2, PING_MUTE_SIZE / 2 - 2);
			pingHit.graphics.endFill();
			pingMuteButton.addChild(pingHit);
			pingMuteStates = new Array();
			for (var i:int = 0; i < PING_MUTE_STATES.length; i++)
			{
				var state:Loader = new Loader();
				state.mouseEnabled = false;
				state.load(new URLRequest(PING_MUTE_TEX + PING_MUTE_STATES[i] + ".dds"));
				state.scaleX = PING_MUTE_SIZE / 31;
				state.scaleY = PING_MUTE_SIZE / 31;
				state.visible = false;
				pingMuteButton.addChild(state);
				pingMuteStates.push(state);
			}
			// the row of the chat mute, one button to the left of it with a small gap
			pingMuteButton.x = Math.round(muteToggle_cmp.x - PING_MUTE_SIZE - 3);
			pingMuteButton.y = muteToggle_cmp.y;
			pingMuteButton.visible = false;
			pingMuteButton.addEventListener(MouseEvent.CLICK, OnPingMuteClick);
			pingMuteButton.addEventListener(MouseEvent.MOUSE_OVER, OnPingMuteOver);
			pingMuteButton.addEventListener(MouseEvent.MOUSE_OUT, OnPingMuteOut);
			pingMuteButton.addEventListener(MouseEvent.MOUSE_DOWN, OnPingMuteDown);
			pingMuteButton.addEventListener(MouseEvent.MOUSE_UP, OnPingMuteUp);
			addChild(pingMuteButton);
			UpdatePingMuteView();
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
			// no ward column: kills, deaths and assists stay in their old place (moved left only when the column is there)
			if (wardBaseX != null)
			{
				var shift:Number = shown ? wardShiftStep : 0;
				might.x = wardBaseX[0] - shift;
				scythe.x = wardBaseX[1] - shift;
				fist.x = wardBaseX[2] - shift;
				sumKillStat.x = wardBaseX[3] - shift;
				sumDeathStat.x = wardBaseX[4] - shift;
				sumSupportStat.x = wardBaseX[5] - shift;
			}
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

		// players whose pings are off (C++: on the battle start and after every click)
		public static function SetPingMuted(id:int, muted:Boolean):void
		{
			if (muted)
				pingMutedIds[String(id)] = true;
			else
				delete pingMutedIds[String(id)];
		}

		private function IsPingMuted():Boolean
		{
			return _playerData != null && pingMutedIds[String(_playerData.HeroId)] == true;
		}

		public function UpdatePingMuteView():void
		{
			if (pingMuteStates == null)
				return;
			var index:int = 0;
			if (pingMuteDown)
				index = 2;
			else if (pingMuteOver)
				index = 1;
			if (IsPingMuted())
				index += 3;
			for (var i:int = 0; i < pingMuteStates.length; i++)
				Loader(pingMuteStates[i]).visible = (i == index);
		}

		private function OnPingMuteClick(e:MouseEvent):void
		{
			try
			{
				if (_playerData == null)
					return;
				var muted:Boolean = !IsPingMuted();
				SetPingMuted(_playerData.HeroId, muted);
				pingMuteDown = false;
				UpdatePingMuteView();
				var value:String = "0";
				if (muted)
					value = "1";
				SoundController.PlaySound(SoundController.BUTTON_PRESS_SMALL);
				FSCommands.Send("PingMuteUser", _playerData.HeroId.toString() + " " + value);
				OnPingMuteOver(null);
			}
			catch (err:Error)
			{
			}
		}

		private function OnPingMuteOver(e:MouseEvent):void
		{
			try
			{
				pingMuteOver = true;
				UpdatePingMuteView();
				var tooltip:String = Localization.Statistics_PingMute;
				if (IsPingMuted())
					tooltip = Localization.Statistics_PingUnmute;
				var point:Point = pingMuteButton.localToGlobal(new Point());
				dispatchEvent(new ButtonTooltipEvent(ButtonTooltipEvent.ACTION_TYPE_IN, this, point.x, point.y, tooltip));
			}
			catch (err:Error)
			{
			}
		}

		private function OnPingMuteOut(e:MouseEvent):void
		{
			pingMuteOver = false;
			pingMuteDown = false;
			UpdatePingMuteView();
			onMuteToggleOut(e);
		}

		private function OnPingMuteDown(e:MouseEvent):void
		{
			pingMuteDown = true;
			UpdatePingMuteView();
		}

		private function OnPingMuteUp(e:MouseEvent):void
		{
			pingMuteDown = false;
			UpdatePingMuteView();
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