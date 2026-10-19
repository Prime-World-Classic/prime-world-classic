package ActionBar
{
   import BaseClasses.BaseBarButton;
   import BaseClasses.BaseShortCut;
   import Controllers.TextFieldFormater;
   import DragAndDrop.ActionBarDragOBject;
   import DragAndDrop.DnDController;
   import flash.display.Sprite;
   import flash.events.Event;
   import flash.events.MouseEvent;
   import flash.text.TextField;
   
   [Embed(source="/_assets/assets.swf", symbol="symbol3323")]
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
      // (statics: this class is bound to a library symbol)
      public static var smartBoxes:Object = new Object();
      
      public static var smartKeys:Object = new Object();
      
      private static var smartSwallowUp:Boolean = false;
      
      public static const SMART_BOX_X:Number = 38;
      
      public static const SMART_BOX_Y:Number = 38;
      
      public static const SMART_BOX_SIZE:Number = 14;
      
      public function ActionButton()
      {
         super();
         this.Shortcut = new BaseShortCut();
         Shortcut.actionButton = this;
         isFreeSlot = true;
         this.textNum.mouseEnabled = false;
         this.formatedBind = new TextFieldFormater(this.textNum);
      }
      
      public function set isReserved(param1:Boolean) : void
      {
         this._isReserved = param1;
      }
      
      public function get isReserved() : Boolean
      {
         return this._isReserved;
      }
      
      public function set ActionBarIndex(param1:int) : void
      {
         this._index = param1;
         param1 += 1;
         if(this.isBindSetted)
         {
            return;
         }
         if(param1 == 10)
         {
            this.textNum.text = "0";
         }
         else if(param1 >= 11 && param1 <= 24)
         {
            this.textNum.text = "F" + (param1 - 10).toString();
         }
         else
         {
            this.textNum.text = param1.toString();
         }
      }
      
      public function get ActionBarIndex() : int
      {
         return this._index;
      }
      
      public function set actionBar(param1:ActionBar) : void
      {
         this._actionBar = param1;
      }
      
      // click on the smart-cast checkbox of this button: toggle it instead of using the ability
      private function SmartCastHit() : Boolean
      {
         var _loc1_:Sprite = ActionButton.smartBoxes[String(this.ActionBarIndex)] as Sprite;
         if(_loc1_ == null || !_loc1_.visible || _loc1_.parent != this)
         {
            return false;
         }
         if(this.mouseX < ActionButton.SMART_BOX_X - 3 || this.mouseX > ActionButton.SMART_BOX_X + ActionButton.SMART_BOX_SIZE + 3)
         {
            return false;
         }
         if(this.mouseY < ActionButton.SMART_BOX_Y - 3 || this.mouseY > ActionButton.SMART_BOX_Y + ActionButton.SMART_BOX_SIZE + 3)
         {
            return false;
         }
         FSCommands.Send("TalentSmartCast",String(ActionButton.smartKeys[String(this.ActionBarIndex)]));
         return true;
      }
      
      override protected function onDown(param1:MouseEvent) : void
      {
         if(this.SmartCastHit())
         {
            ActionButton.smartSwallowUp = true;
            return;
         }
         ActionButton.smartSwallowUp = false;
         super.onDown(param1);
      }
      
      override protected function onUp(param1:MouseEvent) : void
      {
         if(ActionButton.smartSwallowUp)
         {
            if(param1 == null || param1.type != "doubleClick")
            {
               ActionButton.smartSwallowUp = false;
            }
            return;
         }
         // the second click of a double click already came as mouseDown+mouseUp; the extra 'doubleClick'
         // event must not press the ability a third time (breaks double-click self-cast timing)
         if(param1 != null && param1.type == "doubleClick")
         {
            return;
         }
         super.onUp(param1);
         if(DnDController.isDragging)
         {
            this.DragEndedOverThis();
            return;
         }
         Shortcut.Press();
      }
      
      override protected function onOver(param1:MouseEvent) : void
      {
         super.onOver(param1);
         Shortcut.showTooltip();
      }
      
      override protected function onOut(param1:MouseEvent) : void
      {
         super.onOut(param1);
         Shortcut.hideTooltip();
      }
      
      override protected function onRightClick(param1:Event) : void
      {
         param1.stopPropagation();
         Shortcut.Press();
      }
      
      override protected function StartDragThis() : void
      {
         if(this._isActionBarLocked)
         {
            return;
         }
         DnDController.StartDrag(new ActionBarDragOBject(Shortcut));
      }
      
      override protected function DragEndedOverThis() : void
      {
         if(DnDController.DraggingObject.CanDragIntoActionBar && !this._isActionBarLocked)
         {
            DnDController.DraggingObject.DragIntoActionBarSlot(this.ActionBarIndex,this._actionBar);
         }
         DnDController.EndDrag();
      }
      
      override public function set Shortcut(param1:BaseShortCut) : void
      {
         _shortcut = param1;
         _shortcut.actionButton = this;
         if(_shortcut)
         {
            _shortcut.count = count;
         }
         isFreeSlot = false;
         this.isReserved = false;
      }
      
      override public function GlobalCoolDown(param1:Number, param2:Number) : void
      {
         if(isFreeSlot)
         {
            return;
         }
         super.GlobalCoolDown(param1,param2);
      }
      
      public function BlockActionBar(param1:Boolean) : void
      {
         this._isActionBarLocked = param1;
      }
      
      public function SetBindKey(param1:String) : void
      {
         this.isBindSetted = true;
         this.formatedBind.Text = param1;
      }
   }
}

