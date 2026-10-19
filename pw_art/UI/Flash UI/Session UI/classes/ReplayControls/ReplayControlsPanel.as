package ReplayControls
{
	import BaseClasses.TooltipSender;
	import flash.display.DisplayObjectContainer;
	import flash.display.Loader;
	import flash.display.MovieClip;
	import flash.display.Shape;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.events.MouseEvent;
	import flash.net.URLRequest;
	import flash.text.TextField;
	import flash.text.TextFieldAutoSize;
	import flash.text.TextFormat;
	import flash.text.TextFormatAlign;

	// Панель управления реплеем: время, шкала сессии (клик — перейти к моменту), назад /
	// пауза / вперёд и скорость. Стоит по центру чуть выше панели активных талантов.
	// Состояние шлёт клиент (PF_GameLogic/ReplayControlsController.cpp) через
	// MainInterface.SetReplayState; нажатия уходят обратно FSCommand'ом COMMAND и
	// выполняют те же консольные команды, что и клавиши replay.cfg (стрелки, пробел).
	//
	// Движок PW: Sprite.graphics не рисуется (только дочерние Shape), обводок нет,
	// мелкие фигуры размыты — значки кнопок текстурами (pw_art/UI/Replay/gen_replay_icons.py),
	// TextField только без масштаба, getBounds() не работает, MouseEvent.localX всегда 0
	// (координата — через mouseX), почти прозрачная заливка не ловит нажатие (клик уходит в игру).
	public class ReplayControlsPanel
	{
		public static const COMMAND:String = "ReplayControlCmd";

		private static const ICON_PATH:String = "/UI/Styles/Icons/Replay/";
		private static const ICON_TEX:Number = 64;

		private static const PANEL_W:Number = 440;
		private static const PANEL_H:Number = 62;
		// отрицательный: панель чуть заходит на зону баффов, чтобы сообщение «Скорость воспроизведения
		// записи» вставало прямо над панелью, а не поверх кнопок
		private static const GAP_ABOVE_BAR:Number = -16;
		private static const BUTTON:Number = 30;
		private static const ICON:Number = 22;
		private static const BUTTONS_Y:Number = 6;
		private static const BAR_X:Number = 62;
		private static const BAR_Y:Number = 46;
		private static const BAR_H:Number = 6;
		private static const TRACK_PAD:Number = 2; // насколько фон шкалы больше полосы с каждой стороны
		private static const HIT_H:Number = BAR_H + 16; // тёмная полоса клика по шкале
		private static const HIT_PAD:Number = HIT_H / 2 - BAR_H / 2; // выступ за концы шкалы: скругления концентричны
		private static const TEXT_SIZE:int = 13;

		private static const PANEL_COLOR:uint = 0x000000;
		private static const PANEL_ALPHA:Number = 0.6;
		private static const BUTTON_COLOR:uint = 0xFFFFFF;
		private static const BUTTON_ALPHA:Number = 0.2;
		private static const BUTTON_HOVER_ALPHA:Number = 0.4;
		private static const HOVER_PAD:Number = 8; // поля фона вокруг времени под курсором
		private static const PANEL_ROUND:Number = 16; // диаметр скругления углов панели
		private static const BUTTON_ROUND:Number = 10; // и кнопок (и фона времени под курсором)
		private static const HIT_ALPHA:Number = 0.35;
		private static const BAR_COLOR:uint = 0x5C5C5C;
		private static const PROGRESS_COLOR:uint = 0xD9B44A;
		private static const TARGET_COLOR:uint = 0xFFFFFF;

		private var parent:DisplayObjectContainer;
		private var anchorTop:Function; // верх панели талантов (с баффами) в координатах экрана
		private var anchorCenterX:Function; // середина панели талантов по горизонтали
		private var panel:Sprite;

		private var backButton:MovieClip;
		private var playButton:MovieClip;
		private var forwardButton:MovieClip;
		private var slowerButton:MovieClip;
		private var fasterButton:MovieClip;
		private var buttonTooltips:Array = [];

		private var speedText:TextField;
		private var timeText:TextField;
		private var totalText:TextField;
		private var hoverText:TextField;
		private var hoverBack:Shape;

		private var timeline:Sprite;
		private var barShape:Shape;
		private var timelineTooltip:TooltipSender;

		private var shown:Boolean = false;
		// Нажатие уже обработано по MOUSE_DOWN — CLICK того же нажатия пропускаем
		private var pressHandled:Boolean = false;
		private var lastAnchor:Number = NaN;
		private var lastCenterX:Number = NaN;

		// Последнее состояние: время в десятых секунды, скорость в процентах
		private var time:int = 0;
		private var total:int = 0;
		private var target:int = -1;
		private var speed:int = 100;
		private var paused:Boolean = false;
		private var ended:Boolean = false;

		public function ReplayControlsPanel(_parent:DisplayObjectContainer, _anchorTop:Function, _anchorCenterX:Function)
		{
			parent = _parent;
			anchorTop = _anchorTop;
			anchorCenterX = _anchorCenterX;
		}

		public function Show(show:Boolean):void
		{
			if (show == shown)
				return;
			shown = show;

			if (show)
			{
				if (panel == null)
					Create();
				UpdateTooltips();
				parent.addChild(panel);
				panel.addEventListener(Event.ENTER_FRAME, OnEnterFrame);
				Layout();
				Redraw();
			}
			else if (panel != null)
			{
				panel.removeEventListener(Event.ENTER_FRAME, OnEnterFrame);
				if (panel.parent != null)
					panel.parent.removeChild(panel);
			}
		}

		public function SetState(_time:int, _total:int, _target:int, _speed:int, _paused:Boolean, _ended:Boolean):void
		{
			time = _time;
			total = _total;
			target = _target;
			speed = _speed;
			paused = _paused;
			ended = _ended;

			if (shown)
				Redraw();
		}

		private function Create():void
		{
			panel = new Sprite();

			var back:Shape = new Shape();
			back.graphics.beginFill(PANEL_COLOR, PANEL_ALPHA);
			back.graphics.drawRoundRect(0, 0, PANEL_W, PANEL_H, PANEL_ROUND, PANEL_ROUND);
			back.graphics.endFill();
			panel.addChild(back);

			// Кнопки: назад / пауза / вперёд по центру, скорость справа
			var center:Number = PANEL_W / 2;
			backButton = CreateButton(center - BUTTON * 1.5 - 8, "back", "seek -10");
			playButton = CreateButton(center - BUTTON / 2, "pause", "pause");
			forwardButton = CreateButton(center + BUTTON / 2 + 8, "forward", "seek 10");
			slowerButton = CreateButton(PANEL_W - 12 - BUTTON * 2 - 52, "slower", "speed_dec");
			fasterButton = CreateButton(PANEL_W - 12 - BUTTON, "faster", "speed_inc");

			speedText = CreateText(TEXT_SIZE + 1, 0xFFFFFF, 52, TextFormatAlign.LEFT);
			speedText.y = BUTTONS_Y + (BUTTON - speedText.height) / 2;
			panel.addChild(speedText);

			// Шкала сессии
			timeText = CreateText(TEXT_SIZE, 0xFFFFFF, BAR_X - 10, TextFormatAlign.LEFT);
			timeText.y = BAR_Y + BAR_H / 2 - timeText.height / 2;
			panel.addChild(timeText);

			totalText = CreateText(TEXT_SIZE, 0xC8C8C8, BAR_X - 10, TextFormatAlign.LEFT);
			totalText.y = timeText.y;
			panel.addChild(totalText);

			timeline = new MovieClip();
			timeline.buttonMode = true;
			timeline.mouseChildren = false;
			timeline.x = BAR_X;
			timeline.y = BAR_Y - 8;
			var hit:Shape = new Shape(); // область клика выше и ниже тонкой полосы (тёмная полоса под шкалой)
			hit.graphics.beginFill(0x000000, HIT_ALPHA);
			// скруглена и выступает за концы шкалы так, что её скругления концентричны скруглениям фона шкалы:
			// тёмная кайма вокруг шкалы одинаковой ширины
			hit.graphics.drawRoundRect(-HIT_PAD, 0, BarWidth() + HIT_PAD * 2, HIT_H, HIT_H, HIT_H);
			hit.graphics.endFill();
			timeline.addChild(hit);
			barShape = new Shape();
			barShape.y = 8;
			timeline.addChild(barShape);
			timeline.addEventListener(MouseEvent.MOUSE_DOWN, OnTimelineDown);
			timeline.addEventListener(MouseEvent.MOUSE_MOVE, OnTimelineMove);
			timeline.addEventListener(MouseEvent.MOUSE_OUT, OnTimelineOut);
			timeline.addEventListener(MouseEvent.MOUSE_UP, StopEvent);
			timeline.addEventListener(MouseEvent.CLICK, OnTimelineClick);
			timeline.addEventListener(MouseEvent.DOUBLE_CLICK, StopEvent);
			panel.addChild(timeline);
			timelineTooltip = new TooltipSender(timeline as MovieClip);

			// Время под курсором над шкалой
			hoverBack = new Shape();
			hoverBack.visible = false;
			panel.addChild(hoverBack);
			hoverText = CreateText(TEXT_SIZE, 0xFFFFFF, 64, TextFormatAlign.LEFT);
			hoverText.visible = false;
			panel.addChild(hoverText);
		}

		private function CreateButton(x:Number, image:String, command:String):MovieClip
		{
			var button:MovieClip = new MovieClip();
			button.buttonMode = true;
			button.mouseChildren = false;
			button.x = Math.round(x);
			button.y = BUTTONS_Y;

			var back:Shape = new Shape();
			back.name = "back";
			DrawButtonBack(back, BUTTON_ALPHA);
			button.addChild(back);

			var icon:Sprite = new Sprite();
			icon.mouseEnabled = false;
			icon.mouseChildren = false;
			icon.x = icon.y = (BUTTON - ICON) / 2;
			icon.scaleX = icon.scaleY = ICON / ICON_TEX;
			icon.addChild(new Loader());
			icon.name = "icon";
			button.addChild(icon);
			SetIcon(button, image);

			var press:Function = function():void
			{
				SendCommand(button == playButton && ended ? "seek_to 0" : command);
			};
			button.addEventListener(MouseEvent.MOUSE_DOWN, function(e:MouseEvent):void
			{
				e.stopPropagation();
				pressHandled = true;
				SendCommand("dbg down " + image);
				press();
			});
			button.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void
			{
				e.stopPropagation();
				if (!pressHandled)
				{
					SendCommand("dbg click " + image);
					press();
				}
				pressHandled = false;
			});
			button.addEventListener(MouseEvent.MOUSE_OVER, function(e:MouseEvent):void { DrawButtonBack(button.getChildByName("back") as Shape, BUTTON_HOVER_ALPHA); });
			button.addEventListener(MouseEvent.MOUSE_OUT, function(e:MouseEvent):void { DrawButtonBack(button.getChildByName("back") as Shape, BUTTON_ALPHA); });
			button.addEventListener(MouseEvent.MOUSE_UP, StopEvent);
			button.addEventListener(MouseEvent.DOUBLE_CLICK, StopEvent);

			buttonTooltips.push(new TooltipSender(button));
			panel.addChild(button);
			return button;
		}

		private static function DrawButtonBack(back:Shape, alpha:Number):void
		{
			back.graphics.clear();
			back.graphics.beginFill(BUTTON_COLOR, alpha);
			back.graphics.drawRoundRect(0, 0, BUTTON, BUTTON, BUTTON_ROUND, BUTTON_ROUND);
			back.graphics.endFill();
		}

		private static function SetIcon(button:MovieClip, image:String):void
		{
			var icon:Sprite = button.getChildByName("icon") as Sprite;
			if (icon == null || icon.getChildAt(0).name == image)
				return;
			var loader:Loader = icon.getChildAt(0) as Loader;
			loader.name = image;
			loader.unload();
			loader.load(new URLRequest(ICON_PATH + image + ".dds"));
		}

		// Подсказки из Localization (Combat.xdb, ru + en) — к показу панели переводы уже загружены
		private function UpdateTooltips():void
		{
			var texts:Array = [Localization.ReplayControls_Back, Localization.ReplayControls_Pause, Localization.ReplayControls_Forward,
				Localization.ReplayControls_Slower, Localization.ReplayControls_Faster];
			for (var i:int = 0; i < buttonTooltips.length && i < texts.length; i++)
				TooltipSender(buttonTooltips[i]).TooltipText = texts[i];
			timelineTooltip.TooltipText = Localization.ReplayControls_Timeline;
		}

		private static function CreateText(size:int, color:uint, fieldWidth:Number, align:String):TextField
		{
			var format:TextFormat = new TextFormat();
			format.font = "DejaVu LGC Sans";
			format.size = size;
			format.color = color;
			format.align = align;

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

		// Движок не выравнивает текст по центру поля: поле по ширине текста и центр вручную
		private static function CenterText(text:TextField, centerX:Number):void
		{
			text.width = Math.ceil(text.textWidth) + 4;
			text.x = Math.round(centerX - text.width / 2);
		}

		private static function BarWidth():Number
		{
			return PANEL_W - BAR_X * 2;
		}

		// По центру экрана, над баффами и полосками панели талантов: следим каждый кадр,
		// потому что второй ряд слотов и смена разрешения двигают панель талантов
		private function OnEnterFrame(e:Event):void
		{
			Layout();
		}

		private function Layout():void
		{
			if (panel == null || panel.stage == null)
				return;

			var top:Number = anchorTop != null ? Number(anchorTop()) : NaN;
			if (isNaN(top))
				top = panel.stage.stageHeight - 160;
			// по центру панели талантов: центр сцены у движка не совпадает с центром экрана
			var centerX:Number = anchorCenterX != null ? Number(anchorCenterX()) : NaN;
			if (isNaN(centerX))
				centerX = panel.stage.stageWidth / 2;
			if (top == lastAnchor && centerX == lastCenterX)
				return;
			lastAnchor = top;
			lastCenterX = centerX;
			SendCommand("dbg layout top " + int(top) + " center " + int(centerX) + " stage " + int(panel.stage.stageWidth) + " parent " + int(parent.x));

			panel.x = Math.round(centerX - PANEL_W / 2);
			panel.y = Math.round(top - GAP_ABOVE_BAR - PANEL_H);
		}

		private function Redraw():void
		{
			SetIcon(playButton, paused || ended ? "play" : "pause");

			speedText.text = SpeedLabel(speed);
			// посередине между «−» и «+»: выравнивание по центру поля движок не делает
			speedText.width = Math.ceil(speedText.textWidth) + 4;
			speedText.x = Math.round(PANEL_W - 12 - BUTTON - 26 - speedText.width / 2);
			timeText.text = FormatTime(target >= 0 ? target : time);
			totalText.text = FormatTime(total);

			var width:Number = BarWidth();
			var progress:Number = total > 0 ? Math.min(1, Math.max(0, time / total)) : 0;

			// время — по центру полей слева и справа от шкалы
			CenterText(timeText, (BAR_X - HIT_PAD) / 2);
			CenterText(totalText, PANEL_W - (BAR_X - HIT_PAD) / 2);

			// Фон шкалы шире и выше полосы на TRACK_PAD: скруглённая полоса лежит в нём ровной каймой
			var g:* = barShape.graphics;
			g.clear();
			g.beginFill(BAR_COLOR, 0.9);
			g.drawRoundRect(-TRACK_PAD, -TRACK_PAD, width + TRACK_PAD * 2, BAR_H + TRACK_PAD * 2, BAR_H + TRACK_PAD * 2, BAR_H + TRACK_PAD * 2);
			g.endFill();
			if (progress > 0)
			{
				g.beginFill(PROGRESS_COLOR, 1);
				g.drawRoundRect(0, 0, Math.max(BAR_H, Math.round(width * progress)), BAR_H, BAR_H, BAR_H);
				g.endFill();
			}
			// Куда идёт перемотка
			if (target >= 0 && total > 0)
			{
				var targetX:Number = Math.round(width * Math.min(1, target / total));
				g.beginFill(TARGET_COLOR, 0.6);
				g.drawRect(Math.min(targetX, width - 2), -3, 2, BAR_H + 6);
				g.endFill();
			}
			// Ползунок
			var knobX:Number = Math.round(width * progress);
			g.beginFill(0xFFFFFF, 1);
			g.drawRoundRect(Math.max(0, Math.min(width - 4, knobX - 2)), -4, 4, BAR_H + 8, 4, 4);
			g.endFill();
		}

		private function OnTimelineDown(e:MouseEvent):void
		{
			e.stopPropagation();
			pressHandled = true;
			SeekToMouse("down");
		}

		private function OnTimelineClick(e:MouseEvent):void
		{
			e.stopPropagation();
			if (!pressHandled)
				SeekToMouse("click");
			pressHandled = false;
		}

		private function SeekToMouse(source:String):void
		{
			var fraction:Number = TimelineFraction();
			SendCommand("dbg timeline " + source + " x " + int(timeline.mouseX) + " of " + int(BarWidth()) + " total " + total);
			if (total <= 0)
				return;
			SendCommand("seek_to " + fraction.toFixed(4));
		}

		private function OnTimelineMove(e:MouseEvent):void
		{
			if (total <= 0)
				return;
			var fraction:Number = TimelineFraction();
			hoverText.text = FormatTime(int(total * fraction));
			// Движок не выравнивает текст по центру поля: поле по ширине текста (+2 px отступа TextField
			// с каждой стороны) и центр вручную, как в InlineTextLayout
			var center:Number = BAR_X + BarWidth() * fraction;
			var textW:Number = Math.ceil(hoverText.textWidth);
			hoverText.width = textW + 4;
			hoverText.x = Math.round(center - hoverText.width / 2);
			hoverText.y = -hoverText.height - 2;
			hoverBack.graphics.clear();
			hoverBack.graphics.beginFill(PANEL_COLOR, PANEL_ALPHA);
			hoverBack.graphics.drawRoundRect(Math.round(center - textW / 2 - HOVER_PAD), hoverText.y + 1, textW + HOVER_PAD * 2, hoverText.height, BUTTON_ROUND, BUTTON_ROUND);
			hoverBack.graphics.endFill();
			hoverText.visible = hoverBack.visible = true;
		}

		private function OnTimelineOut(e:MouseEvent):void
		{
			hoverText.visible = hoverBack.visible = false;
		}

		private function TimelineFraction():Number
		{
			// mouseX — координата в timeline, полоса начинается с 0 (localX события в движке всегда 0)
			return Math.min(1, Math.max(0, timeline.mouseX / BarWidth()));
		}

		private function StopEvent(e:MouseEvent):void
		{
			e.stopPropagation();
		}

		private static function SendCommand(command:String):void
		{
			FSCommands.Send(COMMAND, command);
		}

		private static function SpeedLabel(percent:int):String
		{
			if (percent % 100 == 0)
				return "x" + int(percent / 100);
			return "x" + (percent / 100).toString();
		}

		// Десятые доли секунды -> "м:сс" или "ч:мм:сс"
		private static function FormatTime(tenths:int):String
		{
			if (tenths < 0)
				tenths = 0;
			var seconds:int = int(tenths / 10);
			var hours:int = int(seconds / 3600);
			var minutes:int = int((seconds % 3600) / 60);
			seconds = seconds % 60;
			var mm:String = (hours > 0 && minutes < 10 ? "0" : "") + minutes;
			var ss:String = (seconds < 10 ? "0" : "") + seconds;
			return (hours > 0 ? hours + ":" : "") + mm + ":" + ss;
		}
	}
}
