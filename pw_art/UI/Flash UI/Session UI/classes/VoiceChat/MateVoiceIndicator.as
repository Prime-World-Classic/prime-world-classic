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
	import flash.geom.ColorTransform;
	import flash.geom.Point;
	import flash.text.TextField;
	import flash.text.TextFieldAutoSize;
	import flash.text.TextFormat;
	import flash.utils.getTimer;

	// Войс на портрете союзника (панель teamInfoFrendly_mc) — как в тамбуре лаунчера
	// (pw-launcher main.css .mm-voice-btn, Voice.decorateLobbyPlayer):
	// - пока игрок говорит, вокруг портрета мягкое зелёное свечение;
	// - в правом верхнем углу портрета круглая кнопка: золотая рамка, бирюзовый фон, значок.
	//   У себя — микрофон, у союзника — звук (заглушён мной — красный), его выключенный или
	//   отсутствующий микрофон — серый микрофон; союзник заглушил меня — красная рамка;
	//   соединение ещё не установлено — кнопка мерцает; союзник вне войса — бледные наушники;
	//   вокруг кнопки союзника золотая дуга — его громкость (10..100%, заглушён — бледная);
	// - кнопка нажимается только с зажатой клавишей voice_overlay_unlock (иначе клики проходят
	//   к портрету): у союзника — заглушить/включить, у себя — микрофон. Пока клавиша зажата,
	//   под кнопкой союзника видна громкость, колесо над портретом её меняет.
	// Уровень и полоски важнее: свечение кладётся в слой под ними.
	//
	// Размеры текста не берём у движка: у созданных кодом TextField он держит
	// рамку по умолчанию и считает textWidth лениво; NaN в координатах фигуры
	// вешает его тесселятор — все числа для рисования проверяем.
	public class MateVoiceIndicator
	{
		private static const LABEL_COLOR:uint = 0x0E1F21;
		private static const LABEL_TEXT_COLOR:uint = 0xF3ECD9;
		private static const TEXT_SIZE:int = 11;
		// Движок рисует текст у верха поля: поправка ~0.35 кегля вниз (замер в игре).
		private static const TEXT_DROP:Number = 0.35;
		// Мерцание кнопки, пока соединение не установлено (voice-wait в лаунчере).
		private static const CONNECTING_PERIOD_MS:Number = 1600;

		// Элементы портрета, которые должны быть поверх свечения.
		// Свечение кладём выше слоёв портрета (roundFrame, свечения, полоска ульты закрывали его),
		// но под уровнем и ником; полоски — ниже портрета, свечение их не перекрывает.
		private static const PRIORITY_CHILDREN:Array = ["levelRoundFrame", "level_txt", "levelHighLight", "nameText"];

		// Текстуры, как у штатного интерфейса и колеса пингов: свою векторную графику движок
		// растеризует грубо (круги ломаными, без сглаживания, контуры и многоугольники не рисует
		// вовсе). Генератор: pw_art/UI/Voice/gen_voice_icons.py.
		private static const ICON_PATH:String = "/UI/Styles/Icons/Voice/";
		// Кнопка (plate_*) и значок (v_*) — 64x64, круг кнопки занимает 168/200 текстуры.
		private static const ICON_TEX:Number = 64;
		private static const PLATE_FRACTION:Number = 0.84;
		// Свечение — 128x128, радиус текстуры = GLOW_EXTENT радиусов портрета.
		private static const GLOW_TEX:Number = 128;
		private static const GLOW_EXTENT:Number = 1.9;
		// Кнопка — 34% портрета и выступает за его край на 6%, как в тамбуре.
		private static const BUTTON_FRACTION:Number = 0.34;
		private static const BUTTON_OUTSET:Number = 0.06;
		// Дуга громкости союзника (vol_010..vol_100, 64x64): её внешний радиус — 23.2/17 радиуса
		// кнопки и 31 px из 32 в текстуре.
		private static const ARC_FRACTION:Number = 23.2 / 17 * 32 / 31;

		public var mate:MateInfoBase;
		public var peerId:int;
		public var isMe:Boolean;

		private var glow:Sprite;
		private var button:Sprite;
		private var arcIcon:Sprite;
		private var plateIcon:Sprite;
		private var buttonIcon:Sprite;
		// Громкость под кнопкой. Движок не рисует graphics самого Sprite (видны только дети) —
		// подложка рисуется в дочерний Shape.
		private var label:Sprite;
		private var labelShape:Shape;
		private var labelText:TextField;
		private var labelTextSize:int = 0;

		private var speaking:Boolean = false;
		private var inCall:Boolean = false;
		private var notInVoice:Boolean = false;
		private var interactive:Boolean = false;
		private var hovered:Boolean = false;
		private var flags:int = 0;
		private var volume:int = 50;
		private var micState:int = 0;

		// Свечение плавно разгорается и гаснет; анимация крутится, пока оно видно или кнопка мерцает.
		private var glowLevel:Number = 0;
		private var lastFrameTime:int = 0;
		private var animating:Boolean = false;
		private var buttonAlpha:Number = 1;

		private var frameX:Number = 0;
		private var frameY:Number = 0;
		private var frameSize:Number = 60;
		private var buttonSize:Number = 20;
		private var buttonCenterX:Number = 0;
		private var buttonCenterY:Number = 0;

		public function MateVoiceIndicator(_mate:MateInfoBase, _isMe:Boolean)
		{
			mate = _mate;
			isMe = _isMe;
			Measure();

			glow = CreateIcon();
			glow.visible = false;
			SetIconImage(glow, "glow");
			mate.addChildAt(glow, RingIndex());
			// Полоски ХП и МП тоже над свечением (они под портретом, рамку портрета не перекрывают).
			RaiseAboveRing("manaBar_mc");
			RaiseAboveRing("healthBar_mc");

			button = new Sprite();
			button.visible = false;
			button.mouseChildren = false;
			arcIcon = CreateIcon();
			arcIcon.visible = false;
			button.addChild(arcIcon);
			plateIcon = CreateIcon();
			button.addChild(plateIcon);
			buttonIcon = CreateIcon();
			button.addChild(buttonIcon);
			mate.addChild(button);

			label = new Sprite();
			label.visible = false;
			label.mouseEnabled = false;
			label.mouseChildren = false;
			labelShape = new Shape();
			label.addChild(labelShape);
			labelText = CreateText(TEXT_SIZE, LABEL_TEXT_COLOR, 80);
			label.addChild(labelText);
			mate.addChild(label);

			SetButtonMouse();
			button.addEventListener(MouseEvent.MOUSE_DOWN, OnButtonDown);
			button.addEventListener(MouseEvent.MOUSE_UP, StopEvent);
			button.addEventListener(MouseEvent.CLICK, StopEvent);
			button.addEventListener(MouseEvent.DOUBLE_CLICK, StopEvent);
			button.addEventListener(MouseEvent.MOUSE_OVER, OnButtonOver);
			button.addEventListener(MouseEvent.MOUSE_OUT, OnButtonOut);

			// Колесо приходит самому верхнему объекту под курсором: у портрета это
			// mouseCatcher, если он есть, иначе сам портрет.
			mate.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			if (mate.mouseCatcher != null)
				mate.mouseCatcher.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			button.addEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
		}

		public function Dispose():void
		{
			StopAnimation();
			mate.removeEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			if (mate.mouseCatcher != null)
				mate.mouseCatcher.removeEventListener(MouseEvent.MOUSE_WHEEL, OnWheel);
			for each (var child:DisplayObject in [glow, button, label])
			{
				if (child.parent != null)
					child.parent.removeChild(child);
			}
		}

		// flags (союзник): 1 говорит, 2 заглушён мной, 4 заглушил меня, 8 нет соединения,
		// 16 микрофон выключен, 32 микрофона нет.
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
				Redraw();
		}

		// Союзник в бою, но не в нашем звонке.
		public function SetNotInVoice():void
		{
			if (notInVoice)
				return;
			notInVoice = true;
			inCall = false;
			flags = 0;
			SetSpeaking(false);
			Redraw();
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
				Redraw();
		}

		public function SetInteractive(value:Boolean):void
		{
			if (interactive == value)
				return;
			interactive = value;
			if (!interactive)
				hovered = false;
			Redraw();
		}

		private function Redraw():void
		{
			DrawButton();
			DrawLabel();
			SetButtonMouse();
			UpdateAnimation();
		}

		private function SetSpeaking(value:Boolean):void
		{
			if (speaking == value)
				return;
			speaking = value;
			if (speaking)
				Measure();
			UpdateAnimation();
		}

		private function UpdateAnimation():void
		{
			var need:Boolean = speaking || glowLevel > 0 || IsConnecting();
			if (need && !animating)
			{
				animating = true;
				lastFrameTime = getTimer();
				mate.addEventListener(Event.ENTER_FRAME, OnFrame);
				OnFrame(null);
			}
			else if (!need)
			{
				StopAnimation();
				button.alpha = buttonAlpha;
			}
		}

		private function StopAnimation():void
		{
			if (!animating)
				return;
			animating = false;
			mate.removeEventListener(Event.ENTER_FRAME, OnFrame);
		}

		private function IsConnecting():Boolean
		{
			return inCall && !isMe && (flags & 8) != 0;
		}

		private function OnFrame(e:Event):void
		{
			var now:int = getTimer();
			var dt:Number = Math.min(0.1, Math.max(0, (now - lastFrameTime) / 1000));
			lastFrameTime = now;

			// Уровня голоса клиент не шлёт — говорящему свечение «дышит», как от голоса:
			// разгорается за ~0.1 с, гаснет за ~0.25 с.
			var target:Number = 0;
			if (speaking)
				target = 0.78 + 0.22 * (0.5 * Math.sin(now * 0.017) + 0.3 * Math.sin(now * 0.041 + 1.3) + 0.2 * Math.sin(now * 0.007));
			var rate:Number = target > glowLevel ? 10 : 4;
			glowLevel += (target - glowLevel) * Math.min(1, rate * dt * 2);
			if (!speaking && glowLevel < 0.02)
				glowLevel = 0;
			DrawGlow();

			if (IsConnecting())
			{
				var phase:Number = (now % CONNECTING_PERIOD_MS) / CONNECTING_PERIOD_MS;
				button.alpha = buttonAlpha * (0.7 + 0.3 * Math.cos(phase * Math.PI * 2));
			}
			else
				button.alpha = buttonAlpha;

			if (!speaking && glowLevel == 0 && !IsConnecting())
				StopAnimation();
		}

		// Свечение растёт вместе с уровнем, как box-shadow в тамбуре.
		private function DrawGlow():void
		{
			glow.visible = glowLevel > 0;
			if (!glow.visible)
				return;
			var radius:Number = frameSize / 2 * (0.9 + 0.1 * glowLevel);
			var size:Number = radius * GLOW_EXTENT * 2;
			glow.alpha = Math.min(1, glowLevel);
			glow.scaleX = glow.scaleY = size / GLOW_TEX;
			glow.x = Math.round(frameX + frameSize / 2 - size / 2);
			glow.y = Math.round(frameY + frameSize / 2 - size / 2);
		}

		// Габариты портрета и кнопки — в координатах портрета.
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
			buttonSize = Math.round(Math.max(20, frameSize * BUTTON_FRACTION));
			buttonCenterX = frameX + frameSize * (1 + BUTTON_OUTSET) - buttonSize / 2;
			buttonCenterY = frameY - frameSize * BUTTON_OUTSET + buttonSize / 2;

			// Портреты стоят у верхнего края экрана — кнопка не уходит за него.
			try
			{
				var top:Point = mate.localToGlobal(new Point(buttonCenterX, buttonCenterY - buttonSize / 2));
				var origin:Point = mate.localToGlobal(new Point(0, 0));
				var unit:Point = mate.localToGlobal(new Point(0, 1));
				var scale:Number = Finite(unit.y - origin.y, 1);
				var topY:Number = Finite(top.y, 0);
				if (topY < 0 && scale > 0)
					buttonCenterY += -topY / scale;
			}
			catch (e:Error)
			{
			}
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
			var ringIndex:int = mate.getChildIndex(glow);
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

		private function DrawButton():void
		{
			var show:Boolean = inCall || notInVoice;
			button.visible = show;
			if (!show)
				return;

			Measure();

			var icon:String;
			buttonAlpha = 1;
			if (notInVoice)
			{
				icon = "v_headset_off";
				buttonAlpha = 0.6;
			}
			else if (isMe)
			{
				icon = micState == 1 ? "v_mic" : (micState == 2 ? "v_mic_off" : "v_mic_none");
				if (micState == 0)
					buttonAlpha = 0.75;
			}
			else if ((flags & 2) != 0)
				icon = "v_spk_off";
			else if ((flags & 16) != 0)
				icon = "v_peer_mic_off";
			else if ((flags & 32) != 0)
				icon = "v_peer_mic_none";
			else
				icon = "v_spk_on";

			SetIconImage(plateIcon, !isMe && inCall && (flags & 4) != 0 ? "plate_red" : "plate_gold");
			SetIconImage(buttonIcon, icon);

			var size:Number = buttonSize / PLATE_FRACTION;
			button.scaleX = button.scaleY = 1;
			for each (var layer:Sprite in [plateIcon, buttonIcon])
			{
				layer.scaleX = layer.scaleY = size / ICON_TEX;
				layer.x = 0;
				layer.y = 0;
			}
			// дуга вокруг кнопки союзника — его громкость (видно, что колёсиком её можно крутить)
			arcIcon.visible = inCall && !isMe;
			if (arcIcon.visible)
			{
				var percent:int = 5 * Math.max(2, Math.min(20, Math.round(volume / 5)));
				SetIconImage(arcIcon, "vol_" + (percent < 100 ? "0" : "") + percent);
				var arcSize:Number = buttonSize * ARC_FRACTION;
				arcIcon.scaleX = arcIcon.scaleY = arcSize / ICON_TEX;
				arcIcon.x = arcIcon.y = (size - arcSize) / 2;
				arcIcon.alpha = (flags & 2) != 0 ? 0.35 : 1;
			}

			button.x = Math.round(buttonCenterX - size / 2);
			button.y = Math.round(buttonCenterY - size / 2);
			button.alpha = buttonAlpha;
			ApplyHover();
		}

		private function DrawLabel():void
		{
			var show:Boolean = interactive && inCall && !isMe;
			label.visible = show;
			if (!show)
				return;

			var textSize:int = Math.max(TEXT_SIZE, Math.round(buttonSize * 0.45));
			var pad:Number = Math.round(textSize * 0.4);
			var text:String = volume + "%";
			var width:Number = EstimateTextWidth(text, textSize) + 2 * pad;
			var height:Number = textSize + Math.round(pad * 0.8) * 2;
			var corner:Number = Math.round(height * 0.5);

			label.x = Math.round(buttonCenterX - width / 2);
			// ниже дуги громкости
			label.y = Math.round(buttonCenterY + buttonSize / 2 * 23.2 / 17 + 2);

			var g:* = labelShape.graphics;
			g.clear();
			g.beginFill(LABEL_COLOR, 0.92);
			g.drawRoundRect(0, 0, width, height, corner, corner);
			g.endFill();

			if (labelTextSize != textSize)
			{
				labelTextSize = textSize;
				var format:TextFormat = labelText.defaultTextFormat;
				format.size = textSize;
				labelText.defaultTextFormat = format;
				labelText.width = Math.round(width + 40);
				labelText.height = textSize + 12;
			}
			labelText.text = text;
			labelText.textColor = (flags & 2) != 0 ? 0x949BA4 : LABEL_TEXT_COLOR;
			labelText.x = pad;
			labelText.y = Math.round((height - (textSize + 8)) / 2 + textSize * TEXT_DROP);
		}

		// Без клавиши кнопка прозрачна для мыши: клики и наведение — портрету.
		private function SetButtonMouse():void
		{
			var active:Boolean = interactive && inCall;
			button.mouseEnabled = active;
			button.buttonMode = active;
			button.useHandCursor = active;
		}

		private function ApplyHover():void
		{
			var k:Number = hovered && interactive && inCall ? 1.2 : 1;
			button.transform.colorTransform = new ColorTransform(k, k, k, 1, 0, 0, 0, 0);
		}

		private function OnButtonOver(e:MouseEvent):void
		{
			hovered = true;
			ApplyHover();
		}

		private function OnButtonOut(e:MouseEvent):void
		{
			hovered = false;
			ApplyHover();
		}

		// Контейнер текстуры: один Loader; имя текущей текстуры хранится в name.
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
			if (!interactive || !inCall)
				return;
			e.stopPropagation();
			if (isMe)
				FSCommands.Send(VoiceTeamOverlay.COMMAND, "mic");
			else
				FSCommands.Send(VoiceTeamOverlay.COMMAND, "mute " + peerId);
		}

		private function StopEvent(e:MouseEvent):void
		{
			if (interactive && inCall)
				e.stopPropagation();
		}

		private function OnWheel(e:MouseEvent):void
		{
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
