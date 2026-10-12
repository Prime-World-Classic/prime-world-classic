package VoiceChat
{
	import flash.display.Loader;
	import flash.display.Shape;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.net.URLRequest;
	import flash.text.TextField;
	import flash.text.TextFieldAutoSize;
	import flash.text.TextFormat;

	// Одна строка оверлея войса в стиле Discord: круглый полупрозрачный портрет героя,
	// за которого играет этот игрок,
	// зелёное кольцо, пока человек говорит, тёмная плашка с ником и значки состояния.
	// Всё рисуется кодом — в библиотеке main.fla для оверлея нет символов.
	public class VoiceOverlayRow extends Sprite
	{
		public static const HEIGHT:int = 30;
		public static const AVATAR:int = 24;

		private static const SPEAKING_COLOR:uint = 0x43B581;
		private static const MUTED_COLOR:uint = 0xED4245;
		private static const PILL_COLOR:uint = 0x1E1F22;
		// Портрет героя полупрозрачный, чтобы не перекрывать поле боя.
		private static const AVATAR_ALPHA:Number = 0.6;
		private static const AVATAR_ALPHA_DIMMED:Number = 0.3;
		private static const PLACEHOLDER_COLORS:Array = [0x5865F2, 0x3BA55C, 0xFAA61A, 0xEB459E, 0x747F8D, 0xED4245];

		public var peerId:int;
		public var isMe:Boolean;

		private var avatarHolder:Sprite;
		private var avatarMask:Shape;
		private var placeholder:Shape;
		private var placeholderText:TextField;
		private var iconLoader:Loader;
		private var iconPath:String = null;
		private var ring:Shape;
		private var pill:Shape;
		private var nameText:TextField;
		private var volumeText:TextField;
		private var stateIcons:Shape;

		private var nick:String = "";
		private var speaking:Boolean = false;
		private var flags:int = 0;
		private var volume:int = 50;
		private var micState:int = 0;
		private var interactive:Boolean = false;

		public function VoiceOverlayRow(_peerId:int, _isMe:Boolean)
		{
			peerId = _peerId;
			isMe = _isMe;
			mouseChildren = false;
			buttonMode = false;

			avatarHolder = new Sprite();
			avatarHolder.x = 3;
			avatarHolder.y = (HEIGHT - AVATAR) / 2;
			addChild(avatarHolder);

			placeholder = new Shape();
			avatarHolder.addChild(placeholder);

			placeholderText = CreateText(12, 0xFFFFFF, true);
			avatarHolder.addChild(placeholderText);

			iconLoader = new Loader();
			iconLoader.contentLoaderInfo.addEventListener(Event.INIT, OnIconLoaded);
			iconLoader.visible = false;
			avatarHolder.addChild(iconLoader);

			avatarMask = new Shape();
			avatarMask.graphics.beginFill(0xFF0000);
			avatarMask.graphics.drawRoundRect(0, 0, AVATAR, AVATAR, AVATAR, AVATAR);
			avatarMask.graphics.endFill();
			avatarHolder.addChild(avatarMask);
			avatarHolder.mask = avatarMask;

			ring = new Shape();
			ring.x = avatarHolder.x;
			ring.y = avatarHolder.y;
			addChild(ring);

			pill = new Shape();
			addChild(pill);

			nameText = CreateText(12, 0xFFFFFF, false);
			nameText.x = avatarHolder.x + AVATAR + 9;
			addChild(nameText);

			volumeText = CreateText(11, 0xB5BAC1, false);
			volumeText.visible = false;
			addChild(volumeText);

			stateIcons = new Shape();
			addChild(stateIcons);
		}

		public static function CreateText(size:int, color:uint, bold:Boolean):TextField
		{
			var format:TextFormat = new TextFormat();
			format.font = "DejaVu LGC Sans";
			format.size = size;
			format.color = color;
			format.bold = bold;

			var text:TextField = new TextField();
			text.selectable = false;
			text.mouseEnabled = false;
			text.autoSize = TextFieldAutoSize.LEFT;
			text.defaultTextFormat = format;
			text.text = "";
			return text;
		}

		// flags: 1 говорит, 2 заглушён мной, 4 заглушил меня, 8 нет соединения.
		public function SetPeer(_nick:String, _flags:int, _volume:int):void
		{
			var changed:Boolean = _nick != nick || _flags != flags || _volume != volume;
			nick = _nick;
			flags = _flags;
			volume = _volume;
			speaking = (flags & 1) != 0;
			if (changed)
				Redraw();
			else
				DrawRing();
		}

		// micState: 0 микрофона нет, 1 включён, 2 выключен.
		public function SetMe(_nick:String, _micState:int, _speaking:Boolean):void
		{
			var changed:Boolean = _nick != nick || _micState != micState;
			nick = _nick;
			micState = _micState;
			speaking = _speaking;
			if (changed)
				Redraw();
			else
				DrawRing();
		}

		public function SetInteractive(value:Boolean):void
		{
			if (interactive == value)
				return;
			interactive = value;
			Redraw();
		}

		public function SetIcon(path:String):void
		{
			if (path == iconPath)
				return;
			iconPath = path;
			iconLoader.visible = false;
			try
			{
				iconLoader.unload();
				if (path != null && path.length > 0)
					iconLoader.load(new URLRequest(path));
			}
			catch (e:Error)
			{
				iconLoader.visible = false;
			}
		}

		private function OnIconLoaded(e:Event):void
		{
			iconLoader.width = AVATAR;
			iconLoader.height = AVATAR;
			iconLoader.visible = true;
			placeholder.visible = false;
			placeholderText.visible = false;
		}

		public function get IsMutedByMe():Boolean
		{
			return (flags & 2) != 0;
		}

		private function get IsOffline():Boolean
		{
			return !isMe && (flags & 8) != 0;
		}

		private function Redraw():void
		{
			DrawPlaceholder();

			nameText.text = nick;
			var dimmed:Boolean = IsOffline || (!isMe && IsMutedByMe);
			nameText.textColor = dimmed ? 0x949BA4 : 0xFFFFFF;
			avatarHolder.alpha = dimmed ? AVATAR_ALPHA_DIMMED : AVATAR_ALPHA;
			nameText.y = (HEIGHT - nameText.height) / 2;

			var right:Number = nameText.x + nameText.width + 4;

			stateIcons.graphics.clear();
			var iconsWidth:Number = DrawStateIcons(right);
			right += iconsWidth;

			volumeText.visible = interactive && !isMe;
			if (volumeText.visible)
			{
				volumeText.text = volume + "%";
				volumeText.x = right + 2;
				volumeText.y = (HEIGHT - volumeText.height) / 2;
				right = volumeText.x + volumeText.width + 2;
			}

			var pillX:Number = avatarHolder.x + AVATAR + 4;
			pill.graphics.clear();
			pill.graphics.beginFill(PILL_COLOR, 0.82);
			pill.graphics.drawRoundRect(pillX, 5, right + 4 - pillX, HEIGHT - 10, 8, 8);
			pill.graphics.endFill();

			// Прозрачная подложка во всю строку: попадание мышью (клик, колесо) без «дыр».
			graphics.clear();
			graphics.beginFill(0x000000, interactive ? 0.01 : 0);
			graphics.drawRect(0, 0, right + 6, HEIGHT);
			graphics.endFill();

			DrawRing();
		}

		private function DrawPlaceholder():void
		{
			var color:uint = PLACEHOLDER_COLORS[HashName(nick) % PLACEHOLDER_COLORS.length];
			placeholder.graphics.clear();
			placeholder.graphics.beginFill(color);
			placeholder.graphics.drawRect(0, 0, AVATAR, AVATAR);
			placeholder.graphics.endFill();

			placeholderText.text = nick.length > 0 ? nick.charAt(0).toUpperCase() : "?";
			placeholderText.x = (AVATAR - placeholderText.width) / 2;
			placeholderText.y = (AVATAR - placeholderText.height) / 2;

			var hasIcon:Boolean = iconLoader.visible;
			placeholder.visible = !hasIcon;
			placeholderText.visible = !hasIcon;
		}

		private function DrawRing():void
		{
			ring.graphics.clear();
			var active:Boolean = speaking && !(!isMe && IsMutedByMe);
			if (active)
			{
				ring.graphics.lineStyle(2, SPEAKING_COLOR, 1);
				ring.graphics.drawRoundRect(-1, -1, AVATAR + 2, AVATAR + 2, AVATAR + 2, AVATAR + 2);
			}
		}

		// Значки справа от ника. Возвращает занятую ширину.
		private function DrawStateIcons(left:Number):Number
		{
			var g:* = stateIcons.graphics;
			var x:Number = left;
			var cy:Number = HEIGHT / 2;
			var width:Number = 0;

			if (isMe)
			{
				if (micState != 1)
				{
					DrawMic(x + 2, cy, micState == 0 ? 0x949BA4 : MUTED_COLOR, true);
					width += 14;
				}
				return width;
			}

			if (IsMutedByMe)
			{
				DrawSpeaker(x + 2, cy, MUTED_COLOR, true);
				x += 16;
				width += 16;
			}

			if ((flags & 4) != 0)
			{
				// Этот игрок заглушил меня у себя.
				DrawMic(x + 2, cy, 0x949BA4, true);
				width += 14;
			}

			return width;
		}

		private function DrawMic(x:Number, cy:Number, color:uint, crossed:Boolean):void
		{
			var g:* = stateIcons.graphics;
			g.lineStyle(0, 0, 0);
			g.beginFill(color);
			g.drawRoundRect(x + 3, cy - 6, 5, 8, 5, 5);
			g.endFill();
			g.lineStyle(1.5, color, 1);
			g.moveTo(x + 1, cy - 1);
			g.curveTo(x + 1, cy + 4, x + 5.5, cy + 4);
			g.curveTo(x + 10, cy + 4, x + 10, cy - 1);
			g.moveTo(x + 5.5, cy + 4);
			g.lineTo(x + 5.5, cy + 6);
			if (crossed)
			{
				g.lineStyle(2, MUTED_COLOR, 1);
				g.moveTo(x, cy + 6);
				g.lineTo(x + 11, cy - 6);
			}
			g.lineStyle(0, 0, 0);
		}

		private function DrawSpeaker(x:Number, cy:Number, color:uint, crossed:Boolean):void
		{
			var g:* = stateIcons.graphics;
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

		private static function HashName(value:String):int
		{
			var hash:int = 0;
			for (var i:int = 0; i < value.length; i++)
				hash = (hash * 31 + value.charCodeAt(i)) & 0x7FFFFFFF;
			return hash;
		}
	}
}
