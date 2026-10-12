package VoiceChat
{
	import BaseClasses.MateInfoBase;
	import Datas.PlayerStatisticData;

	// Голосовой чат в бою на портретах союзников. Данные шлёт клиент
	// (PF_GameLogic/VoiceOverlayController.cpp) из лаунчера, где живёт сам войс;
	// MainInterface.SetVoiceOverlay* передаёт их сюда. Участник звонка находится по
	// нику (без тега гильдии) среди союзников; кто не в бою — не показывается.
	// Если звонок идёт хотя бы с одним человеком, союзники (не боты) вне звонка
	// получают значок «нет в войсе».
	//
	// Клиент шлёт состояние только при изменениях, причём первое — ещё до Show(true),
	// а портреты в начале боя могут появиться позже. Поэтому последнее состояние
	// запоминается и применяется заново при показе и при зажатии/отпускании клавиши.
	public class VoiceTeamOverlay
	{
		public static const COMMAND:String = "VoiceOverlayCmd";

		private var playersSource:Function;
		private var mateSource:Function;
		private var ourIdSource:Function;

		// heroId -> MateVoiceIndicator
		private var indicators:Object = {};
		private var shown:Boolean = false;
		private var interactive:Boolean = false;
		// Аргументы последнего SetState или null.
		private var lastState:Array = null;

		// playersSource(): Array of PlayerStatisticData; mateSource(heroId): MateInfoBase
		// союзника или null; ourIdSource(): int — наш heroId.
		public function VoiceTeamOverlay(_playersSource:Function, _mateSource:Function, _ourIdSource:Function)
		{
			playersSource = _playersSource;
			mateSource = _mateSource;
			ourIdSource = _ourIdSource;
		}

		public function Show(show:Boolean):void
		{
			shown = show;
			if (!show)
				Clear();
			else
				Reapply();
		}

		public function SetInteractive(value:Boolean):void
		{
			interactive = value;
			for each (var indicator:MateVoiceIndicator in indicators)
				indicator.SetInteractive(value);
			Reapply();
		}

		public function SetState(micState:int, meSpeaking:Boolean, myName:String, ids:Array, flags:Array, volumes:Array, names:Array):void
		{
			lastState = [micState, meSpeaking, myName, ids || [], flags || [], volumes || [], names || []];
			Reapply();
		}

		private function Reapply():void
		{
			if (!shown || lastState == null)
				return;
			Apply(lastState[0], lastState[1], lastState[3], lastState[4], lastState[5], lastState[6]);
		}

		private function Apply(micState:int, meSpeaking:Boolean, ids:Array, flags:Array, volumes:Array, names:Array):void
		{

			var keep:Object = {};

			var ourId:int = ourIdSource != null ? int(ourIdSource()) : -1;
			var me:MateVoiceIndicator = GetIndicator(ourId, true);
			if (me != null)
			{
				me.SetMe(micState, meSpeaking);
				keep[ourId] = true;
			}

			for (var i:int = 0; i < ids.length; i++)
			{
				var heroId:int = FindAllyHeroId(String(names[i]), ourId);
				if (heroId < 0 || keep[heroId])
					continue;
				var indicator:MateVoiceIndicator = GetIndicator(heroId, false);
				if (indicator == null)
					continue;
				indicator.SetPeer(int(ids[i]), int(flags[i]), int(volumes[i]));
				keep[heroId] = true;
			}

			if (ids.length > 0)
			{
				var players:Array = playersSource != null ? playersSource() : null;
				for each (var player:PlayerStatisticData in players)
				{
					if (player == null || player.IsEnemyTeam || player.IsBot || player.HeroId == ourId || keep[player.HeroId])
						continue;
					var absent:MateVoiceIndicator = GetIndicator(player.HeroId, false);
					if (absent == null)
						continue;
					absent.SetNotInVoice();
					keep[player.HeroId] = true;
				}
			}

			for (var key:String in indicators)
			{
				if (!keep[int(key)])
				{
					MateVoiceIndicator(indicators[key]).Dispose();
					delete indicators[key];
				}
			}
		}

		private function GetIndicator(heroId:int, isMe:Boolean):MateVoiceIndicator
		{
			if (heroId < 0)
				return null;
			var mate:MateInfoBase = mateSource != null ? mateSource(heroId) as MateInfoBase : null;
			var indicator:MateVoiceIndicator = indicators[heroId] as MateVoiceIndicator;
			if (indicator != null && (indicator.mate != mate || indicator.isMe != isMe))
			{
				indicator.Dispose();
				delete indicators[heroId];
				indicator = null;
			}
			if (indicator == null && mate != null)
			{
				indicator = new MateVoiceIndicator(mate, isMe);
				indicator.SetInteractive(interactive);
				indicators[heroId] = indicator;
			}
			return indicator;
		}

		private function Clear():void
		{
			for (var key:String in indicators)
				MateVoiceIndicator(indicators[key]).Dispose();
			indicators = {};
		}

		// Ник из лаунчера сравниваем с ником в бою без тега гильдии «[TAG]».
		private function FindAllyHeroId(nick:String, ourId:int):int
		{
			var players:Array = playersSource != null ? playersSource() : null;
			if (players == null || nick == null)
				return -1;
			var wanted:String = NormalizeNick(nick);
			for each (var player:PlayerStatisticData in players)
			{
				if (player == null || player.IsEnemyTeam || player.HeroId == ourId || player.PlayerName == null)
					continue;
				if (NormalizeNick(player.PlayerName) == wanted)
					return player.HeroId;
			}
			return -1;
		}

		private static function NormalizeNick(value:String):String
		{
			var result:String = value;
			if (result.charAt(0) == "[")
			{
				var close:int = result.indexOf("]");
				if (close >= 0)
					result = result.substr(close + 1);
			}
			return result.replace(/^\s+|\s+$/g, "").toLowerCase();
		}
	}
}
