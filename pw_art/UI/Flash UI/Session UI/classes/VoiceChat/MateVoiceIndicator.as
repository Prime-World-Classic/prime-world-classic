package VoiceChat
{
	import BaseClasses.MateInfoBase;
	import flash.display.DisplayObject;
	import flash.display.Loader;
	import flash.net.URLRequest;
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
		// Рамку кладём выше слоёв портрета (roundFrame, свечения, полоска ульты закрывали её),
		// но под уровнем и ником; полоски — ниже портрета, рамка их не перекрывает.
		private static const PRIORITY_CHILDREN:Array = ["levelRoundFrame", "level_txt", "levelHighLight", "nameText"];

		public var mate:MateInfoBase;
		public var peerId:int;
		public var isMe:Boolean;

		private var ring:Shape;
		private var badge:Shape;
		// Значок в углу: круг радиусом BADGE_BASE (в своих единицах), масштабируется под портрет.
		private static const BADGE_BASE:Number = 100;
		private static const ICON_BOX:Number = 200;
		private var button:Sprite;
		// Движок не рисует graphics самого Sprite (видны только дети) — фон и значок
		// кнопки рисуются в дочерний Shape.
		private var buttonShape:Shape;
		// Значки — текстуры, как у штатного интерфейса и колеса пингов: свою векторную графику
		// движок растеризует грубо (круги ломаными, без сглаживания, контуры и многоугольники
		// не рисует вовсе). Генератор: pw_art/UI/Voice/gen_voice_icons.py (64x64 DDS).
		private static const ICON_PATH:String = "/UI/Styles/Icons/Voice/";
		private static const ICON_TEX:Number = 64;
		// Кнопка под полосками крупнее базового размера. Увеличиваются сами размеры и кегль,
		// а не scaleX/scaleY слоя: масштабированный TextField движок обрезает по рамке.
		private static const BUTTON_SCALE:Number = 1.6;
		private var buttonTextSize:int = 0;
		private var buttonIcon:Sprite;
		private var badgeIcon:Sprite;
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
			// Полоски ХП и МП тоже над рамкой (они под портретом, рамку портрета не перекрывают).
			RaiseAboveRing("manaBar_mc");
			RaiseAboveRing("healthBar_mc");

			badge = new Shape();
			badge.visible = false;
			mate.addChild(badge);
			badgeIcon = CreateIcon();
			badgeIcon.visible = false;
			mate.addChild(badgeIcon);

			button = new Sprite();
			button.visible = false;
			button.buttonMode = true;
			button.mouseChildren = false;
			buttonShape = new Shape();
			button.addChild(buttonShape);
			buttonIcon = CreateIcon();
			button.addChild(buttonIcon);
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
			if (badgeIcon.parent != null)
				badgeIcon.parent.removeChild(badgeIcon);
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
			FSCommands.Send(VoiceTeamOverlay.COMMAND, "dbg speaking " + (isMe ? "me" : "peer " + peerId) + " " + speaking + " ringIndex " + (ring.parent != null ? mate.getChildIndex(ring) + "/" + mate.numChildren : "-"));
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

		private function RaiseAboveRing(name:String):void
		{
			var child:DisplayObject = Child(name);
			if (child == null || child.parent != mate)
				return;
			var ringIndex:int = mate.getChildIndex(ring);
			if (mate.getChildIndex(child) < ringIndex)
				mate.setChildIndex(child, ringIndex);
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
			ring.graphics.clear();
			ring.graphics.lineStyle(thickness, SPEAKING_COLOR, 1);
			ring.graphics.drawCircle(frameX + frameSize / 2, frameY + frameSize / 2, frameSize / 2 + thickness / 2 + 1);
		}

		private function DrawButton():void
		{
			var show:Boolean = interactive && inCall;
			button.visible = show;
			if (!show)
				return;

			Measure();

			var k:Number = BUTTON_SCALE;
			var textSize:int = Math.round(TEXT_SIZE * k);
			var height:Number = Math.round(Math.max(18, frameSize * 0.28) * k);
			var pad:Number = Math.round(2 * k);
			var iconSize:Number = height - 2 * pad;
			var iconWidth:Number = iconSize + pad;
			var label:String = isMe ? "" : volume + "%";
			var textWidth:Number = label.length > 0 ? EstimateTextWidth(label, textSize) + 2 * pad : 0;
			var width:Number = Math.max(height + 3 * pad, 3 * pad + iconWidth + textWidth + 2 * pad);
			var corner:Number = Math.round(8 * k);

			button.scaleX = button.scaleY = 1;
			button.x = Math.round(frameX + (frameSize - width) / 2);
			button.y = Math.round(statsBottom + 2);

			var g:* = buttonShape.graphics;
			g.clear();
			g.beginFill(0xFFFFFF, 0.25);
			g.drawRoundRect(-1, -1, width + 2, height + 2, corner + 1, corner + 1);
			g.endFill();
			g.beginFill(BUTTON_COLOR, 1);
			g.drawRoundRect(0, 0, width, height, corner, corner);
			g.endFill();

			if (isMe)
				SetIconImage(buttonIcon, micState == 1 ? "mic_on" : (micState == 0 ? "mic_none" : "mic_off"));
			else
				SetIconImage(buttonIcon, (flags & 2) != 0 ? "spk_muted" : ((flags & 8) != 0 ? "spk_off" : "spk_on"));
			buttonIcon.scaleX = buttonIcon.scaleY = iconSize / ICON_TEX;
			buttonIcon.x = label.length > 0 ? 2 * pad : (width - iconSize) / 2;
			buttonIcon.y = (height - iconSize) / 2;

			if (buttonTextSize != textSize)
			{
				buttonTextSize = textSize;
				var format:TextFormat = buttonText.defaultTextFormat;
				format.size = textSize;
				buttonText.defaultTextFormat = format;
				buttonText.width = Math.round(80 * k);
				buttonText.height = textSize + 12;
			}
			buttonText.visible = label.length > 0;
			buttonText.text = label;
			buttonText.textColor = (flags & 2) != 0 ? OFF_COLOR : 0xFFFFFF;
			buttonText.x = 2 * pad + iconWidth;
			buttonText.y = Math.round((height - (textSize + 8)) / 2 + textSize * TEXT_DROP);

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
			badgeIcon.visible = kind != 0;
			if (kind == 0)
				return;

			Measure();
			// круглая подложка — внутри текстуры (b_*), векторный круг не нужен
			badge.visible = false;
			SetIconImage(badgeIcon, kind == 1 ? "b_headset_off" : (kind == 2 ? "b_spk_muted" : (kind == 3 ? "b_mic_off" : "b_mic_none")));

			// радиус значка на экране: не меньше 11, растёт с портретом
			var radius:Number = Math.max(11, frameSize * 0.17);
			var centerX:Number = Math.round(frameX + frameSize - radius * 0.8);
			var centerY:Number = Math.round(frameY + radius * 0.8);
			badgeIcon.scaleX = badgeIcon.scaleY = radius * 2 / ICON_TEX;
			badgeIcon.x = centerX - radius;
			badgeIcon.y = centerY - radius;
		}


		// Контейнер значка: один Loader; имя текущей текстуры хранится в name.
		private static function CreateIcon():Sprite
		{
			var icon:Sprite = new Sprite();
			icon.mouseEnabled = false;
			icon.mouseChildren = false;
			icon.addChild(new Loader());
			return icon;
		}

		private static function SetIconImage(icon:Sprite, image:String):void
		{
			if (icon.name == image)
				return;
			icon.name = image;
			var loader:Loader = icon.getChildAt(0) as Loader;
			loader.unload();
			loader.load(new URLRequest(ICON_PATH + image + ".dds"));
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
