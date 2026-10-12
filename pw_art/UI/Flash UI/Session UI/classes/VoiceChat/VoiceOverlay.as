package VoiceChat
{
	import Datas.PlayerStatisticData;
	import flash.display.DisplayObject;
	import flash.display.Shape;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.events.MouseEvent;
	import flash.text.TextField;

	// Оверлей голосового чата в бою (как в Discord). Данные шлёт клиент
	// (PF_GameLogic/VoiceOverlayController.cpp) из лаунчера, где живёт сам войс.
	// Пока не зажата клавиша voice_overlay_unlock, оверлей не ловит мышь и клики
	// уходят в игру. С зажатой клавишей: клик по игроку — заглушить/включить,
	// клик по себе — микрофон, колесо — громкость игрока, перетаскивание — позиция.
	public class VoiceOverlay extends Sprite
	{
		public static const COMMAND:String = "VoiceOverlayCmd";

		// Пока игрок не перетащил оверлей, он стоит под панелями героев слева и
		// масштабируется вместе с ними (панели подгоняются под разрешение).
		private static const SCALE:Number = 1.5;
		private static const ANCHOR_GAP:Number = 8;
		private static const DRAG_THRESHOLD:Number = 4;
		private static const VOLUME_STEP:int = 5;

		private var playersSource:Function;
		private var anchorsSource:Function;
		private var anchored:Boolean = true;
		private var rows:Array = [];
		private var meRow:VoiceOverlayRow;
		private var frame:Shape;
		private var hint:TextField;

		private var interactive:Boolean = false;
		private var posX:Number = 0;
		private var posY:Number = 0;

		private var pressRow:VoiceOverlayRow = null;
		private var pressStageX:Number = 0;
		private var pressStageY:Number = 0;
		private var pressSelfX:Number = 0;
		private var pressSelfY:Number = 0;
		private var dragging:Boolean = false;

		// playersSource(): Array of PlayerStatisticData — для портретов героев.
		// anchorsSource(): Array of DisplayObject (панели героев, соседи по родителю) —
		// оверлей встаёт под ними и берёт их масштаб.
		public function VoiceOverlay(_playersSource:Function, _anchorsSource:Function)
		{
			playersSource = _playersSource;
			anchorsSource = _anchorsSource;
			visible = false;
			mouseEnabled = false;
			mouseChildren = false;

			frame = new Shape();
			addChild(frame);

			hint = VoiceOverlayRow.CreateText(10, 0xDBDEE1, false);
			hint.visible = false;
			addChild(hint);

			meRow = new VoiceOverlayRow(0, true);
			AddRowListeners(meRow);
			addChild(meRow);

			addEventListener(Event.ADDED_TO_STAGE, OnAddedToStage);
		}

		private function OnAddedToStage(e:Event):void
		{
			removeEventListener(Event.ADDED_TO_STAGE, OnAddedToStage);
			stage.addEventListener(MouseEvent.MOUSE_UP, OnStageMouseUp);
			stage.addEventListener(Event.RESIZE, OnStageResize);
		}

		public function Show(show:Boolean):void
		{
			visible = show;
			if (!show)
				CancelPress();
			else
				ApplyPosition();
		}

		public function SetPosition(x:Number, y:Number):void
		{
			if (x >= 0 && x <= 1 && y >= 0 && y <= 1)
			{
				posX = x;
				posY = y;
				anchored = false;
			}
			ApplyPosition();
		}

		public function SetInteractive(value:Boolean):void
		{
			interactive = value;
			mouseChildren = value;
			if (!value)
				CancelPress();
			meRow.SetInteractive(value);
			for each (var row:VoiceOverlayRow in rows)
				row.SetInteractive(value);
			Layout();
		}

		public function SetState(micState:int, meSpeaking:Boolean, myName:String, ids:Array, flags:Array, volumes:Array, names:Array):void
		{
			meRow.SetMe(myName, micState, meSpeaking);
			meRow.SetIcon(FindHeroIcon(myName));

			var keep:Array = [];
			for (var i:int = 0; i < ids.length; i++)
			{
				var row:VoiceOverlayRow = FindRow(int(ids[i]));
				if (row == null)
				{
					row = new VoiceOverlayRow(int(ids[i]), false);
					row.SetInteractive(interactive);
					AddRowListeners(row);
					addChild(row);
				}
				row.SetPeer(String(names[i]), int(flags[i]), int(volumes[i]));
				row.SetIcon(FindHeroIcon(String(names[i])));
				keep.push(row);
			}

			for each (var old:VoiceOverlayRow in rows)
			{
				if (keep.indexOf(old) < 0)
				{
					RemoveRowListeners(old);
					removeChild(old);
					if (pressRow == old)
						CancelPress();
				}
			}
			rows = keep;

			Layout();
			if (!dragging)
				ApplyPosition();
		}

		private function FindRow(id:int):VoiceOverlayRow
		{
			for each (var row:VoiceOverlayRow in rows)
				if (row.peerId == id)
					return row;
			return null;
		}

		// Ник из лаунчера сравниваем с ником в бою без тега гильдии «[TAG]».
		private function FindHeroIcon(nick:String):String
		{
			var players:Array = playersSource != null ? playersSource() : null;
			if (players == null || nick == null)
				return "";
			var wanted:String = NormalizeNick(nick);
			for each (var player:PlayerStatisticData in players)
			{
				if (player != null && player.PlayerName != null && NormalizeNick(player.PlayerName) == wanted)
					return player.HeroIcon;
			}
			return "";
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

		private function Layout():void
		{
			var y:Number = 0;
			var width:Number = 0;

			meRow.y = y;
			y += VoiceOverlayRow.HEIGHT;
			width = Math.max(width, meRow.width);

			for each (var row:VoiceOverlayRow in rows)
			{
				row.y = y;
				y += VoiceOverlayRow.HEIGHT;
				width = Math.max(width, row.width);
			}

			hint.visible = interactive;
			if (interactive)
			{
				hint.text = Localization.VoiceOverlay_Hint;
				hint.x = 4;
				hint.y = y + 2;
				y = hint.y + hint.height + 2;
				width = Math.max(width, hint.x + hint.width + 4);
			}

			frame.graphics.clear();
			if (interactive)
			{
				frame.graphics.lineStyle(1, 0xFFFFFF, 0.35);
				frame.graphics.beginFill(0x000000, 0.25);
				frame.graphics.drawRoundRect(-3, -3, width + 6, y + 6, 10, 10);
				frame.graphics.endFill();
			}
		}

		private function ApplyPosition():void
		{
			if (stage == null)
				return;

			// getBounds в движке не реализован — низ панелей считаем по y + height.
			var uiScale:Number = 0;
			var left:Number = 4;
			var bottom:Number = 0;
			var anchors:Array = anchorsSource != null ? anchorsSource() : null;
			for each (var anchor:DisplayObject in anchors)
			{
				if (anchor == null || !anchor.visible || anchor.parent != parent)
					continue;
				if (uiScale == 0)
				{
					uiScale = anchor.scaleY;
					left = anchor.x;
				}
				bottom = Math.max(bottom, anchor.y + anchor.height);
			}
			if (uiScale <= 0)
				uiScale = 1;
			scaleX = scaleY = SCALE * uiScale;

			var targetX:Number = anchored ? left + 4 : posX * stage.stageWidth;
			var targetY:Number = anchored ? bottom + ANCHOR_GAP * uiScale : posY * stage.stageHeight;
			var maxX:Number = Math.max(0, stage.stageWidth - width);
			var maxY:Number = Math.max(0, stage.stageHeight - height);
			x = Math.round(Math.min(maxX, Math.max(0, targetX)));
			y = Math.round(Math.min(maxY, Math.max(0, targetY)));
		}

		private function OnStageResize(e:Event):void
		{
			if (!dragging)
				ApplyPosition();
		}

		private function AddRowListeners(row:VoiceOverlayRow):void
		{
			row.addEventListener(MouseEvent.MOUSE_DOWN, OnRowMouseDown);
			row.addEventListener(MouseEvent.MOUSE_WHEEL, OnRowMouseWheel);
		}

		private function RemoveRowListeners(row:VoiceOverlayRow):void
		{
			row.removeEventListener(MouseEvent.MOUSE_DOWN, OnRowMouseDown);
			row.removeEventListener(MouseEvent.MOUSE_WHEEL, OnRowMouseWheel);
		}

		private function OnRowMouseDown(e:MouseEvent):void
		{
			if (!interactive || stage == null)
				return;
			e.stopPropagation();
			pressRow = e.currentTarget as VoiceOverlayRow;
			pressStageX = stage.mouseX;
			pressStageY = stage.mouseY;
			pressSelfX = x;
			pressSelfY = y;
			dragging = false;
			addEventListener(Event.ENTER_FRAME, OnDragFrame);
		}

		// Тащим по ENTER_FRAME: MOUSE_MOVE вне оверлея в движке приходит не всегда.
		private function OnDragFrame(e:Event):void
		{
			if (pressRow == null || stage == null)
				return;
			var dx:Number = stage.mouseX - pressStageX;
			var dy:Number = stage.mouseY - pressStageY;
			if (!dragging && Math.abs(dx) + Math.abs(dy) < DRAG_THRESHOLD)
				return;
			dragging = true;
			var maxX:Number = Math.max(0, stage.stageWidth - width);
			var maxY:Number = Math.max(0, stage.stageHeight - height);
			x = Math.round(Math.min(maxX, Math.max(0, pressSelfX + dx)));
			y = Math.round(Math.min(maxY, Math.max(0, pressSelfY + dy)));
		}

		private function OnStageMouseUp(e:MouseEvent):void
		{
			if (pressRow == null)
				return;
			var row:VoiceOverlayRow = pressRow;
			var wasDragging:Boolean = dragging;
			CancelPress();

			if (wasDragging)
			{
				if (stage != null && stage.stageWidth > 0 && stage.stageHeight > 0)
				{
					anchored = false;
					posX = x / stage.stageWidth;
					posY = y / stage.stageHeight;
					FSCommands.Send(COMMAND, "pos " + posX.toFixed(4) + " " + posY.toFixed(4));
				}
				return;
			}

			if (row.isMe)
				FSCommands.Send(COMMAND, "mic");
			else
				FSCommands.Send(COMMAND, "mute " + row.peerId);
		}

		private function CancelPress():void
		{
			removeEventListener(Event.ENTER_FRAME, OnDragFrame);
			pressRow = null;
			dragging = false;
		}

		private function OnRowMouseWheel(e:MouseEvent):void
		{
			if (!interactive)
				return;
			var row:VoiceOverlayRow = e.currentTarget as VoiceOverlayRow;
			if (row == null || row.isMe || e.delta == 0)
				return;
			e.stopPropagation();
			FSCommands.Send(COMMAND, "vol " + row.peerId + " " + (e.delta > 0 ? VOLUME_STEP : -VOLUME_STEP));
		}
	}
}
