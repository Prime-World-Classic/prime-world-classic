package VoiceChat
{
	import BaseClasses.MateInfoBase;
	import flash.display.DisplayObject;
	import flash.display.Shape;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.events.MouseEvent;
	import flash.text.TextField;
	import flash.text.TextFieldAutoSize;
	import flash.text.TextFormat;
	import flash.utils.getTimer;

	// Войс на портрете союзника (панель teamInfoFrendly_mc), как в Discord:
	// - пока игрок говорит, вокруг портрета моргает зелёное кольцо;
	// - значок в правом верхнем углу портрета (слева сверху — уровень): игрока нет
	//   в войсе, у него нет микрофона или он выключен, он заглушён мной;
	// - с зажатой клавишей voice_overlay_unlock под полосками здоровья/энергии
	//   появляется кнопка: у союзника — заглушить/включить (и громкость в %),
	//   у себя — микрофон; колесо над портретом меняет громкость союзника.
	// Уровень и полоски важнее: кольцо кладётся в слой под ними.
	//
	// Размеры текста не берём у движка: у созданных кодом TextField он держит
	// рамку по умолчанию и считает textWidth лениво; NaN в координатах фигуры
	// вешает его тесселятор — все числа для рисования проверяем.
	public class MateVoiceIndicator
	{
		private static const SPEAKING_COLOR:uint = 0x43B581;
		private static const MUTED_COLOR:uint = 0xED4245;
		private static const BUTTON_COLOR:uint = 0x1E1F22;
		private static const OFF_COLOR:uint = 0x949BA4;
		private static const MIC_COLOR:uint = 0xC9CDD4;
		private static const TEXT_SIZE:int = 11;
		// Движок рисует текст у верха поля: поправка ~0.35 кегля вниз (замер в игре).
		private static const TEXT_DROP:Number = 0.35;
		private static const BLINK_PERIOD_MS:Number = 700;

		// Элементы портрета, которые должны быть поверх кольца.
		private static const PRIORITY_CHILDREN:Array = ["levelRoundFrame", "level_txt", "levelHighLight", "healthBar_mc", "manaBar_mc", "nameText"];

		public var mate:MateInfoBase;
		public var peerId:int;
		public var isMe:Boolean;

		private var ring:Shape;
		private var badge:Shape;
		// Значок рисуется под радиус BADGE_BASE и масштабируется под портрет.
		private static const BADGE_BASE:Number = 9;
		private var button:Sprite;
		// Движок не рисует graphics самого Sprite (видны только дети) — фон и значок
		// кнопки рисуются в дочерний Shape.
		private var buttonShape:Shape;
		private var buttonText:TextField;

		private var speaking:Boolean = false;
		private var inCall:Boolean = false;
		private var notInVoice:Boolean = false;
		private var interactive:Boolean = false;
		private var flags:int = 0;
		private var volume:int = 50;
		private var micState:int = 0;

		private var frameX:Number = 0;
		private var frameY:Number = 0;
		private var frameSize:Number = 60;
		private var statsBottom:Number = 80;

		public function MateVoiceIndicator(_mate:MateInfoBase, _isMe:Boolean)
		{
			mate = _mate;
			isMe = _isMe;
			Measure();

			ring = new Shape();
			ring.visible = false;
			mate.addChildAt(ring, RingIndex());

			badge = new Shape();
			badge.visible = false;
			mate.addChild(badge);

			button = new Sprite();
			button.visible = false;
			button.buttonMode = true;
			button.mouseChildren = false;
			buttonShape = new Shape();
			button.addChild(buttonShape);
			buttonText = CreateText(TEXT_SIZE, 0xFFFFFF, 80);
			button.addChild(buttonText);
			mate.addChild(button);

			button.addEventListener(MouseEvent.MOUSE_DOWN, OnButtonDown);
			button.addEventListener(MouseEvent.MOUSE_UP, StopEvent);
			button.addEventListener(MouseEvent.CLICK, StopEvent);
			button.addEventListener(MouseEvent.DOUBLE_CLICK, StopEvent);

			// Колесо приходит самому верхнему объекту под курсором: у портрета это
			// mouseCatcher, если он есть, иначе сам портрет.
			mate.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			if (mate.mouseCatcher != null)
				mate.mouseCatcher.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			button.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
		}

		public function Dispose():void
		{
			mate.removeEventListener(Event.ENTER_FRAME, OnBlink);
			mate.removeEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			if (mate.mouseCatcher != null)
				mate.mouseCatcher.removeEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			if (ring.parent != null)
				ring.parent.removeChild(ring);
			if (badge.parent != null)
				badge.parent.removeChild(badge);
			if (button.parent != null)
				button.parent.removeChild(button);
		}

		// flags (союзник): 1 говорит, 2 заглушён мной, 4 заглушил меня, 8 нет соединения.
		public function SetPeer(_peerId:int, _flags:int, _volume:int):void
		{
			var changed:Boolean = !inCall || _flags != flags || _volume != volume || _peerId != peerId;
			peerId = _peerId;
			flags = _flags;
			volume = _volume;
			inCall = true;
			notInVoice = false;
			SetSpeaking((flags & 1) != 0 && (flags & 2) == 0);
			if (changed)
			{
				DrawButton();
				DrawBadge();
			}
		}

		// Союзник в бою, но не в нашем звонке.
		public function SetNotInVoice():void
		{
			if (notInVoice)
				return;
			notInVoice = true;
			inCall = false;
			SetSpeaking(false);
			DrawButton();
			DrawBadge();
		}

		// micState: 0 микрофона нет, 1 включён, 2 выключен.
		public function SetMe(_micState:int, _speaking:Boolean):void
		{
			var changed:Boolean = !inCall || _micState != micState;
			micState = _micState;
			inCall = true;
			notInVoice = false;
			SetSpeaking(_speaking);
			if (changed)
			{
				DrawButton();
				DrawBadge();
			}
		}

		public function SetInteractive(value:Boolean):void
		{
			if (interactive == value)
				return;
			interactive = value;
			DrawButton();
		}

		private function SetSpeaking(value:Boolean):void
		{
			if (speaking == value)
				return;
			speaking = value;
			ring.visible = speaking;
			if (speaking)
			{
				Measure();
				DrawRing();
				mate.addEventListener(Event.ENTER_FRAME, OnBlink);
			}
			else
			{
				mate.removeEventListener(Event.ENTER_FRAME, OnBlink);
			}
		}

		private function OnBlink(e:Event):void
		{
			var phase:Number = (getTimer() % BLINK_PERIOD_MS) / BLINK_PERIOD_MS;
			ring.alpha = 0.45 + 0.55 * (0.5 + 0.5 * Math.cos(phase * Math.PI * 2));
		}

		// Габариты портрета и низ полосок — в координатах портрета.
		private function Measure():void
		{
			var frame:DisplayObject = mate.roundFrame;
			if (frame != null)
			{
				frameX = Finite(frame.x, 0);
				frameY = Finite(frame.y, 0);
				frameSize = Finite(Math.min(frame.width, frame.height), 60);
				if (frameSize < 10)
					frameSize = 60;
			}
			statsBottom = frameY + frameSize;
			for each (var name:String in ["healthBar_mc", "manaBar_mc"])
			{
				var bar:DisplayObject = Child(name);
				if (bar != null && bar.visible)
					statsBottom = Math.max(statsBottom, Finite(bar.y + BarHeight(bar), statsBottom));
			}
		}

		// Высота полоски без её текстов: у Components.Bar внутри TextField'ы, и движок
		// раздувает bar.height до их рамки по умолчанию (~100) — кнопка уезжала вниз.
		private function BarHeight(bar:DisplayObject):Number
		{
			var limit:Number = Math.max(4, frameSize * 0.25);
			var value:Number = NaN;
			try
			{
				value = Number(Object(bar).BarHeight) * Finite(bar.scaleY, 1);
			}
			catch (e:Error)
			{
			}
			value = Finite(value, limit);
			return value > 0 && value < limit ? value : limit;
		}

		private function RingIndex():int
		{
			var index:int = mate.numChildren;
			for each (var name:String in PRIORITY_CHILDREN)
			{
				var child:DisplayObject = Child(name);
				if (child != null && child.parent == mate)
					index = Math.min(index, mate.getChildIndex(child));
			}
			return index;
		}

		private function Child(name:String):DisplayObject
		{
			try
			{
				return Object(mate)[name] as DisplayObject;
			}
			catch (e:Error)
			{
			}
			return null;
		}

		private function DrawRing():void
		{
			var thickness:Number = Math.max(2, frameSize * 0.05);
			var size:Number = frameSize + thickness + 2;
			var left:Number = frameX + (frameSize - size) / 2;
			var top:Number = frameY + (frameSize - size) / 2;
			ring.graphics.clear();
			ring.graphics.lineStyle(thickness, SPEAKING_COLOR, 1);
			ring.graphics.drawRoundRect(left, top, size, size, size, size);
		}

		private function DrawButton():void
		{
			var show:Boolean = interactive && inCall;
			button.visible = show;
			if (!show)
				return;

			Measure();

			var height:Number = Math.max(14, Math.round(frameSize * 0.24));
			var iconWidth:Number = 14;
			var label:String = isMe ? "" : volume + "%";
			var textWidth:Number = label.length > 0 ? EstimateTextWidth(label, TEXT_SIZE) + 4 : 0;
			var width:Number = Math.max(height + 6, 6 + iconWidth + textWidth + 4);

			button.x = Math.round(frameX + (frameSize - width) / 2);
			button.y = Math.round(statsBottom + 2);

			var g:* = buttonShape.graphics;
			g.clear();
			g.lineStyle(1, 0xFFFFFF, 0.25);
			g.beginFill(BUTTON_COLOR, 0.88);
			g.drawRoundRect(0, 0, width, height, 8, 8);
			g.endFill();
			g.lineStyle(0, 0, 0);

			var cy:Number = height / 2;
			var iconX:Number = label.length > 0 ? 5 : (width - 11) / 2;
			if (isMe)
				DrawMic(g, iconX, cy, micState == 0 ? OFF_COLOR : MIC_COLOR, micState != 1, micState == 0 ? OFF_COLOR : MUTED_COLOR);
			else
				DrawSpeaker(g, iconX, cy, (flags & 2) != 0 ? MUTED_COLOR : ((flags & 8) != 0 ? OFF_COLOR : 0xFFFFFF), (flags & 2) != 0);

			buttonText.visible = label.length > 0;
			buttonText.text = label;
			buttonText.textColor = (flags & 2) != 0 ? OFF_COLOR : 0xFFFFFF;
			buttonText.x = 5 + iconWidth;
			buttonText.y = (height - (TEXT_SIZE + 8)) / 2 + TEXT_SIZE * TEXT_DROP;

			FSCommands.Send(VoiceTeamOverlay.COMMAND, "dbg button " + (isMe ? "me" : "peer " + peerId) + " at " + button.x + "," + button.y + " size " + width + "x" + height
				+ " frame " + frameX + "," + frameY + "/" + frameSize + " stats " + statsBottom + " parent " + (button.parent != null) + " mateVisible " + mate.visible);
		}

		private function DrawBadge():void
		{
			var g:* = badge.graphics;
			g.clear();

			// 0 — значка нет; дальше по важности.
			var kind:int = 0;
			if (notInVoice)
				kind = 1;
			else if (isMe)
				kind = micState == 2 ? 3 : (micState == 0 ? 4 : 0);
			else if ((flags & 2) != 0)
				kind = 2;
			else if ((flags & 16) != 0)
				kind = 3;
			else if ((flags & 32) != 0)
				kind = 4;

			badge.visible = kind != 0;
			if (kind == 0)
				return;

			Measure();
			var r:Number = BADGE_BASE;
			g.lineStyle(1, 0xFFFFFF, 0.3);
			g.beginFill(BUTTON_COLOR, 0.9);
			g.drawRoundRect(-r, -r, r * 2, r * 2, r * 2, r * 2);
			g.endFill();
			g.lineStyle(0, 0, 0);

			if (kind == 1)
				DrawHeadset(g, -6, 1, OFF_COLOR);
			else if (kind == 2)
				DrawSpeaker(g, -6.5, 0, MUTED_COLOR, true);
			else
				DrawMic(g, -5.5, 0, kind == 3 ? MIC_COLOR : OFF_COLOR, true, kind == 3 ? MUTED_COLOR : OFF_COLOR);

			var size:Number = Math.max(BADGE_BASE, frameSize * 0.15);
			badge.scaleX = badge.scaleY = size / BADGE_BASE;
			badge.x = Math.round(frameX + frameSize - size * 0.8);
			badge.y = Math.round(frameY + size * 0.8);
		}

		// Перечёркнутая гарнитура — «нет в войсе».
		private static function DrawHeadset(g:*, x:Number, cy:Number, color:uint):void
		{
			g.lineStyle(1.6, color, 1);
			g.moveTo(x + 1, cy + 2);
			g.curveTo(x + 1, cy - 6, x + 6, cy - 6);
			g.curveTo(x + 11, cy - 6, x + 11, cy + 2);
			g.lineStyle(0, 0, 0);
			g.beginFill(color);
			g.drawRoundRect(x, cy, 3, 5, 2, 2);
			g.drawRoundRect(x + 9, cy, 3, 5, 2, 2);
			g.endFill();
			g.lineStyle(2, color, 1);
			g.moveTo(x - 1, cy + 6);
			g.lineTo(x + 13, cy - 7);
			g.lineStyle(0, 0, 0);
		}

		private function OnButtonDown(e:MouseEvent):void
		{
			e.stopPropagation();
			FSCommands.Send(VoiceTeamOverlay.COMMAND, "dbg click " + (isMe ? "me" : "peer " + peerId) + " interactive " + interactive + " inCall " + inCall);
			if (!interactive || !inCall)
				return;
			if (isMe)
				FSCommands.Send(VoiceTeamOverlay.COMMAND, "mic");
			else
				FSCommands.Send(VoiceTeamOverlay.COMMAND, "mute " + peerId);
		}

		private function StopEvent(e:MouseEvent):void
		{
			e.stopPropagation();
		}

		private function OnWheel(e:MouseEvent):void
		{
			FSCommands.Send(VoiceTeamOverlay.COMMAND, "dbg wheel " + (isMe ? "me" : "peer " + peerId) + " delta " + e.delta + " interactive " + interactive);
			if (!interactive || !inCall || isMe || e.delta == 0)
				return;
			e.stopPropagation();
			FSCommands.Send(VoiceTeamOverlay.COMMAND, "vol " + peerId + " " + (e.delta > 0 ? 5 : -5));
		}

		// Микрофон в рамке 11x13 (x — левый край, cy — центр) по образцу 200x200:
		// капсула 50x105, держатель — полукруг радиусом 70, ножка 30; выключен — диагональ
		// с зазором цвета фона.
		private static function DrawMic(g:*, x:Number, cy:Number, color:uint, crossed:Boolean, slashColor:uint = 0xED4245):void
		{
			var s:Number = 13 / 200;
			var ox:Number = x + 5.5 - 100 * s;
			var oy:Number = cy - 100 * s;
			var stroke:Number = Math.max(1.2, 16 * s);

			g.lineStyle(0, 0, 0);
			g.beginFill(color);
			g.drawRoundRect(ox + 75 * s, oy + 8 * s, 50 * s, 105 * s, 50 * s, 50 * s);
			g.endFill();

			g.lineStyle(stroke, color, 1);
			g.moveTo(ox + 32 * s, oy + 92 * s);
			g.curveTo(ox + 32 * s, oy + 162 * s, ox + 100 * s, oy + 162 * s);
			g.curveTo(ox + 168 * s, oy + 162 * s, ox + 168 * s, oy + 92 * s);
			g.moveTo(ox + 100 * s, oy + 162 * s);
			g.lineTo(ox + 100 * s, oy + 196 * s);

			if (crossed)
			{
				g.lineStyle(Math.max(1.5, 14 * s) + 2, BUTTON_COLOR, 1);
				g.moveTo(ox + 24 * s, oy + 180 * s);
				g.lineTo(ox + 180 * s, oy + 24 * s);
				g.lineStyle(Math.max(1.5, 14 * s), slashColor, 1);
				g.moveTo(ox + 24 * s, oy + 180 * s);
				g.lineTo(ox + 180 * s, oy + 24 * s);
			}
			g.lineStyle(0, 0, 0);
		}

		private static function DrawSpeaker(g:*, x:Number, cy:Number, color:uint, crossed:Boolean):void
		{
			g.lineStyle(0, 0, 0);
			g.beginFill(color);
			g.moveTo(x, cy - 2.5);
			g.lineTo(x + 3, cy - 2.5);
			g.lineTo(x + 7, cy - 6);
			g.lineTo(x + 7, cy + 6);
			g.lineTo(x + 3, cy + 2.5);
			g.lineTo(x, cy + 2.5);
			g.lineTo(x, cy - 2.5);
			g.endFill();
			if (crossed)
			{
				g.lineStyle(2, color, 1);
				g.moveTo(x + 9, cy - 3);
				g.lineTo(x + 13, cy + 3);
				g.moveTo(x + 13, cy - 3);
				g.lineTo(x + 9, cy + 3);
			}
			g.lineStyle(0, 0, 0);
		}

		private static function CreateText(size:int, color:uint, fieldWidth:Number):TextField
		{
			var format:TextFormat = new TextFormat();
			format.font = "DejaVu LGC Sans";
			format.size = size;
			format.color = color;

			var text:TextField = new TextField();
			text.selectable = false;
			text.mouseEnabled = false;
			text.multiline = false;
			text.wordWrap = false;
			text.autoSize = TextFieldAutoSize.NONE;
			text.defaultTextFormat = format;
			text.width = fieldWidth;
			text.height = size + 8;
			text.text = "";
			return text;
		}

		// Ширина строки DejaVu Sans по символам: textWidth движка ненадёжен.
		private static function EstimateTextWidth(value:String, size:Number):Number
		{
			var width:Number = 0;
			for (var i:int = 0; i < value.length; i++)
			{
				var c:String = value.charAt(i);
				if ("ilIj.,:;!|'".indexOf(c) >= 0)
					width += 0.32;
				else if (c == "%")
					width += 0.9;
				else
					width += 0.62;
			}
			return Math.ceil(width * size) + 2;
		}

		private static function Finite(value:Number, fallback:Number):Number
		{
			return isNaN(value) || !isFinite(value) ? fallback : value;
		}
	}
}
