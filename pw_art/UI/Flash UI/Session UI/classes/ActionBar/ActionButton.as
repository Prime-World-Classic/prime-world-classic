package ActionBar
{
	import Controllers.TextFieldFormater;
	import flash.display.Sprite;
	import flash.events.Event;
	import flash.events.MouseEvent;

	
	
	import flash.text.TextField;
	import flash.text.TextFieldAutoSize;
	import flash.display.MovieClip;
	
	import src.Desaturate;
	import BaseClasses.BaseBarButton;
	import BaseClasses.BaseShortCut;
	import DragAndDrop.DnDController;
	import DragAndDrop.ActionBarDragOBject;
	
	public class ActionButton extends BaseBarButton 
	{
		private var _actionBar:ActionBar;
		private var _index:int = -1;
		public var textNum:TextField;
		private var _isReserved:Boolean = false;
		private var _isActionBarLocked:Boolean;
		private var isBindSetted:Boolean;
	
		private var formatedBind:TextFieldFormater;
		
		// smart-cast checkboxes (MainInterface.RefreshSmartCastBoxes): action bar index -> checkbox sprite / "column raw" of its talent
		// (statics: this class is bound to a library symbol, instance fields would break the timeline slots)
		public static var smartBoxes:Object = new Object();
		public static var smartKeys:Object = new Object();
		private static var smartSwallowUp:Boolean = false;
		public static const SMART_BOX_X:Number = 38;
		public static const SMART_BOX_Y:Number = 38;
		public static const SMART_BOX_SIZE:Number = 14;
		
		public function ActionButton()
		{
			Shortcut = new BaseShortCut();
			Shortcut.actionButton = this;
			isFreeSlot = true;
			textNum.mouseEnabled = false;
			formatedBind = new TextFieldFormater(textNum);
		}
		
		public function set isReserved (value:Boolean):void
		{
			_isReserved = value;
		}
		
		public function get isReserved():Boolean
		{
			return _isReserved;
		}
		
		public function set ActionBarIndex(value:int):void
		{
			_index = value;
			
			value += 1;
			if (isBindSetted) return;

			if (value == 10)
				textNum.text = "0";
			else if (value >= 11 && value <= 24)
				textNum.text = "F" + (value - 10).toString();
			else
				textNum.text = value.toString();
		}
		
		public function get ActionBarIndex():int
		{
			return _index;
		}
		
		
		public function set actionBar(actBar:ActionBar):void
		{
			_actionBar = actBar;
		}
		// click on the smart-cast checkbox of this button: toggle it instead of using the ability
		private function SmartCastHit():Boolean
		{
			var box:Sprite = smartBoxes[String(ActionBarIndex)] as Sprite;
			if (box == null || !box.visible || box.parent != this)
				return false;
			if (mouseX < SMART_BOX_X - 3 || mouseX > SMART_BOX_X + SMART_BOX_SIZE + 3)
				return false;
			if (mouseY < SMART_BOX_Y - 3 || mouseY > SMART_BOX_Y + SMART_BOX_SIZE + 3)
				return false;
			FSCommands.Send("TalentSmartCast", String(smartKeys[String(ActionBarIndex)]));
			return true;
		}
		
		override protected function onDown(e:MouseEvent):void 
		{
			if (SmartCastHit())
			{
				smartSwallowUp = true;
				return;
			}
			smartSwallowUp = false;
			super.onDown(e);
		}
		
		override protected function onUp(e:MouseEvent):void 
		{	
			if (smartSwallowUp)
			{
				if (e == null || e.type != "doubleClick")
					smartSwallowUp = false;
				return;
			}
			// the second click of a double click already came as mouseDown+mouseUp; the extra 'doubleClick'
			// event must not press the ability a third time (breaks double-click self-cast timing)
			if (e != null && e.type == "doubleClick")
				return;
			super.onUp(e);
		
			if (DnDController.isDragging) 
			{
				DragEndedOverThis();
				return;
			}
			
			Shortcut.Press();
		} 
		
		override protected function onOver(e:MouseEvent):void 
		{	
			super.onOver(e);
			Shortcut.showTooltip();
		}
		
		override protected function onOut(e:MouseEvent):void 
		{	
			super.onOut(e);
			
			Shortcut.hideTooltip();
		}
		
		override protected function onRightClick(e:Event):void
		{
			e.stopPropagation();
			Shortcut.Press();
		}
			
		override protected function StartDragThis():void 
		{
			if ( _isActionBarLocked) return;
			
			DnDController.StartDrag(new ActionBarDragOBject(Shortcut));
		}
		
		override protected function DragEndedOverThis():void 
		{ 	
			if (DnDController.DraggingObject.CanDragIntoActionBar && !_isActionBarLocked)
			{	
				DnDController.DraggingObject.DragIntoActionBarSlot(ActionBarIndex, _actionBar);
			}
			
			DnDController.EndDrag();
		}
		
		override public function set Shortcut(value:BaseShortCut):void 
		{	
			_shortcut = value;
			_shortcut.actionButton = this;
			
			if (_shortcut) _shortcut.count = count;
			isFreeSlot = false;
			isReserved = false;
		}
		
		override public function GlobalCoolDown(value:Number,maxCooldownValue:Number):void 
		{
			if (isFreeSlot) return;
			super.GlobalCoolDown(value,maxCooldownValue);
		}
		
		public function BlockActionBar(lock:Boolean):void 
		{
			_isActionBarLocked = lock;
		}
			
		public function SetBindKey(bindKey:String):void
		{
			isBindSetted = true;
			formatedBind.Text = bindKey;
		}
	}
} 