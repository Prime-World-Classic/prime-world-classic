package TalentBar
{
   import BaseClasses.BaseShortCut;
   import Components.FormatedTimer;
   import Controllers.SoundController;
   import DragAndDrop.DnDController;
   import DragAndDrop.TallentDragOBject;
   import Enums.ActionBarSlotState;
   import Enums.EErrorMessageType;
   import Enums.TalentUIState;
   import Events.ErrorEvent;
   import Events.MouseRightEvents;
   import Strokes.*;
   import fl.transitions.Tween;
   import fl.transitions.TweenEvent;
   import fl.transitions.easing.None;
   import fl.transitions.easing.Strong;
   import flash.display.Bitmap;
   import flash.display.DisplayObject;
   import flash.display.Loader;
   import flash.display.MovieClip;
   import flash.display.Shape;
   import flash.events.Event;
   import flash.events.MouseEvent;
   import flash.events.TimerEvent;
   import flash.filters.ColorMatrixFilter;
   import flash.geom.Point;
   import flash.net.URLRequest;
   import flash.utils.Timer;
   import flash.utils.getDefinitionByName;
   import src.Desaturate;
   import src.SeladonColor;
   
   [Embed(source="/_assets/assets.swf", symbol="symbol2856")]
   public class Talent extends MovieClip
   {
      
      private var TweenOverX:Tween;
      
      private var TweenOverY:Tween;
      
      private var TweenOutX:Tween;
      
      private var TweenOutY:Tween;
      
      private var TweenDownX:Tween;
      
      private var TweenDownY:Tween;
      
      private var TweenUpX:Tween;
      
      private var TweenUpY:Tween;
      
      public var path:String;
      
      public var altPath:String;
      
      public const type:String = "talent";
      
      private var _talantState:int = 0;
      
      public var _index:int;
      
      private const SIZE:Number = 70;
      
      private var configPath:String = "art/general/";
      
      private var onDown:Boolean;
      
      private var dragable:Boolean;
      
      public var icoState:int = 2;
      
      public var column:int = 0;
      
      public var raw:int = 0;
      
      private var _shortcut:BaseShortCut;
      
      private var cooldown:Number = 0;
      
      private var ico_ld:Loader;
      
      private var icoFilter:ColorMatrixFilter;
      
      private var color_arr:Array;
      
      public var brightness:Number;
      
      private var tweenBrightness:Tween;
      
      private var _isActive:Boolean = false;
      
      private var disabled:Boolean = false;
      
      private var canBuyGlow_mc:CanBuyGlowEffect = null;
      
      private var isAddEffect:Boolean = true;
      
      private var canBuyIcoTweenX:Tween;
      
      private var canBuyIcoTweenY:Tween;
      
      public var icoCont_mc:MovieClip;
      
      public var lock_bm:Shape;
      
      public var nafta_mc:MovieClip;
      
      public var clock_mc:FormatedTimer;
      
      public var IsClassTalent:Boolean;
      
      public var price:int;
      
      public var UpgradeLevel:int = 0;
      
      public var IsAlternativStateActivated:Boolean = false;
      
      private var nafta:DisplayObject;
      
      private var downPosition:Point = new Point(-10,-10);
      
      private var desiredIndex:int = -1;
      
      private var heroIsDead:Boolean;
      
      private var activeTalent_png:Bitmap;
      
      private var maxUpgradeLevel:int = 3;
      
      private var currentUpgradeLevelPos:Number = 35;
      
      private var upgradeLevels:Array = [];
      
      private var dragRadius:Number = 6;
      
      private var timerToDrug:Timer = new Timer(150,1);
      
      private var isTimerEnded:Boolean = false;
      
      private var tutorialHighLight:SquadStroke;
      
      public function Talent()
      {
         super();
         this.lock_bm = this.icoCont_mc.getChildAt(0) as Shape;
         this.lock_bm.visible = false;
         this.nafta_mc.visible = false;
         this.ico_ld = new Loader();
         this.icoCont_mc.addChild(this.ico_ld);
         this.icoCont_mc.setChildIndex(this.ico_ld,0);
         this.ico_ld.contentLoaderInfo.addEventListener(Event.INIT,this.icoInit);
         this.addEventListener(MouseEvent.MOUSE_OVER,this.onOver);
         this.addEventListener(MouseEvent.MOUSE_OUT,this.onOut);
         this.addEventListener(MouseEvent.MOUSE_DOWN,this.OnDown);
         this.addEventListener(MouseEvent.MOUSE_UP,this.onUp);
         this.addEventListener(MouseRightEvents.MOUSE_RIGHT_UP,this.onUp);
         this.addEventListener(MouseRightEvents.MOUSE_RIGHT_DOWN,this.OnDown);
         this.addEventListener(MouseEvent.DOUBLE_CLICK,this.onUp);
         this.addEventListener(MouseEvent.MOUSE_MOVE,this.MouseMoveOverThis);
         this.mouseChildren = false;
         this.buttonMode = true;
         this.doubleClickEnabled = true;
         this.TweenOverX = new Tween(this.icoCont_mc,"scaleX",Strong.easeOut,1,1.1,5,false);
         this.TweenOverY = new Tween(this.icoCont_mc,"scaleY",Strong.easeOut,1,1.1,5,false);
         this.TweenOutX = new Tween(this.icoCont_mc,"scaleX",Strong.easeOut,1.1,1,5,false);
         this.TweenOutY = new Tween(this.icoCont_mc,"scaleY",Strong.easeOut,1.1,1,5,false);
         this.TweenUpX = new Tween(this.icoCont_mc,"scaleX",Strong.easeOut,0.9,1,7,false);
         this.TweenUpY = new Tween(this.icoCont_mc,"scaleY",Strong.easeOut,0.9,1,7,false);
         this.TweenDownX = new Tween(this.icoCont_mc,"scaleX",Strong.easeOut,1,0.9,7,false);
         this.TweenDownY = new Tween(this.icoCont_mc,"scaleY",Strong.easeOut,1,0.9,7,false);
         var _loc1_:Point = new Point(this.nafta_mc.x,this.nafta_mc.y);
         this.nafta = this.ConvertFromMovieToBitmap(this.nafta_mc);
         this.nafta.x = _loc1_.x;
         this.nafta.y = _loc1_.y;
         this.clock_mc.TwoDigitalTimerFormat = true;
         this.clock_mc.visible = false;
         this.clock_mc.mouseEnabled = false;
         this.clock_mc.mouseChildren = false;
         this.nafta_mc = null;
         this.TweenDownX.stop();
         this.TweenDownY.stop();
         this.TweenUpX.stop();
         this.TweenUpY.stop();
         this.lock_bm.visible = false;
         this.dragable = true;
         this.nafta.visible = false;
         this.brightness = 0;
         this.tweenBrightness = new Tween(this,"brightness",None.easeNone,0,0.4,15);
         this.tweenBrightness.addEventListener(TweenEvent.MOTION_CHANGE,this.tweenBrightnessChange);
         this.tweenBrightness.addEventListener(TweenEvent.MOTION_FINISH,this.tweenBrightnessFinish);
         this.tweenBrightness.stop();
         this.color_arr = [1,0,0,0,0,this.brightness,1,0,0,0,0,0,1,0,0,0,0,0,1,0];
         this.icoFilter = new ColorMatrixFilter(this.color_arr);
         this.timerToDrug.stop();
         this.timerToDrug.addEventListener(TimerEvent.TIMER_COMPLETE,this.OnTimerToDrugEnd);
      }
      
      private function ConvertFromMovieToBitmap(param1:MovieClip) : DisplayObject
      {
         var _loc2_:DisplayObject = param1.getChildAt(0) as DisplayObject;
         this.addChildAt(_loc2_,this.getChildIndex(param1));
         this.removeChild(param1);
         return _loc2_;
      }
      
      public function set DesiredIndex(param1:int) : void
      {
         this.desiredIndex = param1;
      }
      
      public function get DesiredIndex() : int
      {
         return this.desiredIndex;
      }
      
      public function set IsActive(param1:Boolean) : void
      {
         this._isActive = param1;
         if(this._isActive && this.activeTalent_png == null)
         {
            this.activeTalent_png = new Bitmap(new ActiveTalent_png());
            this.activeTalent_png.x = (this.SIZE - this.activeTalent_png.width) / 2;
            this.activeTalent_png.y = (this.SIZE - this.activeTalent_png.height) / 2;
            addChild(this.activeTalent_png);
         }
         if(this.activeTalent_png != null)
         {
            this.activeTalent_png.visible = this._isActive;
         }
      }
      
      public function get IsActive() : *
      {
         return this._isActive;
      }
      
      public function set shortcut(param1:BaseShortCut) : void
      {
         this._shortcut = param1;
      }
      
      public function get shortcut() : BaseShortCut
      {
         return this._shortcut;
      }
      
      public function set TalantIndex(param1:int) : void
      {
         this._index = param1;
      }
      
      public function get TalantIndex() : int
      {
         return this._index;
      }
      
      public function set TalantState(param1:int) : void
      {
         this._talantState = param1;
      }
      
      public function get TalantState() : int
      {
         return this._talantState;
      }
      
      public function Press() : void
      {
         FSCommands.Send("TalentPress",this.column.toString() + " " + this.raw.toString());
         if(this.cooldown > 0)
         {
            dispatchEvent(new ErrorEvent(EErrorMessageType.ERRORMESSAGETYPE_ABILITYONCOOLDOWN));
            return;
         }
         if(this.icoState == TalentUIState.NOTENOUGH_PRIME)
         {
            dispatchEvent(new ErrorEvent(EErrorMessageType.ERRORMESSAGETYPE_NOTENOUGHPRIMETOBUY));
            return;
         }
         if(this.icoState == TalentUIState.BOUGHT)
         {
            if(this.TalantState == ActionBarSlotState.NOT_ENOUGHT_MANA)
            {
               dispatchEvent(new ErrorEvent(EErrorMessageType.ERRORMESSAGETYPE_NOTENOUGHMANAFORABILITY));
               return;
            }
            if(this.TalantState == ActionBarSlotState.NOT_ENOUGH_LIFE)
            {
               dispatchEvent(new ErrorEvent(EErrorMessageType.ERRORMESSAGETYPE_NOTENOUGHLIFEFORABILITY));
               return;
            }
         }
         if(this.icoState == TalentUIState.BOUGHT && this.TalantState == ActionBarSlotState.DISABLED)
         {
            return;
         }
         if(this.icoState == TalentUIState.CANBUY)
         {
            FSCommands.Send(FSCommands.TALENT_CLICK,this.column.toString() + " " + this.raw.toString());
            return;
         }
         if(this.icoState == TalentUIState.BOUGHT && !this.heroIsDead)
         {
            FSCommands.Send(FSCommands.TALENT_CLICK,this.column.toString() + " " + this.raw.toString());
            SoundController.PlaySound(SoundController.BUTTON_PRESS_SMALL);
         }
      }
      
      public function Cooldown(param1:Number, param2:Number) : void
      {
         if(this.icoState != TalentUIState.BOUGHT)
         {
            return;
         }
         this.cooldown = param1;
         this.clock_mc.Time = param1;
         this.clock_mc.visible = param1 > 0;
         if(this.shortcut != null)
         {
            this.shortcut.Cooldown(param1,param2);
         }
         if(!this.disabled && param1 > 0)
         {
            Desaturate.desaturate(this.ico_ld);
         }
         if(param1 <= 0)
         {
            Desaturate.saturate(this.ico_ld);
         }
      }
      
      private function onOver(param1:MouseEvent) : void
      {
         if(this.disabled)
         {
            return;
         }
         if(!this.onDown)
         {
            this.TweenOutX.stop();
            this.TweenOutY.stop();
            this.TweenOverX.start();
            this.TweenOverY.start();
         }
         else
         {
            this.TweenUpX.stop();
            this.TweenUpY.stop();
            this.TweenDownX.start();
            this.TweenDownY.start();
         }
         FSCommands.Send(FSCommands.TALENT_TOOLTIP,"1" + " " + this.column.toString() + " " + this.raw.toString());
      }
      
      private function onOut(param1:MouseEvent) : void
      {
         this.downPosition.x = -10;
         this.downPosition.y = -10;
         this.TweenUpX.stop();
         this.TweenUpY.stop();
         this.TweenDownX.stop();
         this.TweenDownY.stop();
         this.TweenOverX.stop();
         this.TweenOverY.stop();
         this.TweenOutX.start();
         this.TweenOutY.start();
         FSCommands.Send(FSCommands.TALENT_TOOLTIP,"0" + " " + this.column.toString() + " " + this.raw.toString());
         if(this.onDown && this.icoState == TalentUIState.BOUGHT && this._isActive && !DnDController.isDragging)
         {
            this.StartDragThis();
            this.timerToDrug.stop();
         }
         this.onDown = false;
      }
      
      private function OnDown(param1:MouseEvent) : void
      {
         if(this.disabled)
         {
            return;
         }
         this.timerToDrug.stop();
         this.timerToDrug.start();
         this.isTimerEnded = false;
         this.onDown = true;
         this.TweenUpX.stop();
         this.TweenUpY.stop();
         this.TweenDownX.start();
         this.TweenDownY.start();
         if(this.icoState == TalentUIState.BOUGHT && this._isActive)
         {
            this.downPosition.x = this.mouseX;
            this.downPosition.y = this.mouseY;
         }
      }
      
      private function onUp(param1:Event) : void
      {
         if(this.disabled)
         {
            return;
         }
         if(!this.onDown)
         {
            return;
         }
         this.onDown = false;
         this.timerToDrug.stop();
         this.TweenDownX.stop();
         this.TweenDownY.stop();
         this.TweenOverX.start();
         this.TweenOverY.start();
         if(!DnDController.isDragging)
         {
            this.Press();
         }
         this.downPosition.x = -10;
         this.downPosition.y = -10;
      }
      
      private function OnTimerToDrugEnd(param1:TimerEvent) : void
      {
         this.isTimerEnded = true;
      }
      
      private function MouseMoveOverThis(param1:MouseEvent) : void
      {
         if(DnDController.isDragging || this.downPosition.x == -10 || this.downPosition.y == -10)
         {
            return;
         }
         if(this.icoState != TalentUIState.BOUGHT || !this._isActive)
         {
            return;
         }
         var _loc2_:Number = Math.sqrt(Math.abs(this.mouseX - this.downPosition.x) * Math.abs(this.mouseX - this.downPosition.x) + Math.abs(this.mouseY - this.downPosition.y) * Math.abs(this.mouseY - this.downPosition.y));
         if(this.onDown && _loc2_ >= this.dragRadius && this.isTimerEnded)
         {
            this.StartDragThis();
         }
      }
      
      private function StartDragThis() : void
      {
         DnDController.StartDrag(new TallentDragOBject(this));
      }
      
      private function ResetState() : void
      {
         var _loc1_:Bitmap = null;
         SeladonColor.saturate(this.ico_ld);
         this.lock_bm.visible = false;
         this.dragable = false;
         this.nafta.visible = false;
         if(this.upgradeLevels.length == 0)
         {
            return;
         }
         for each(_loc1_ in this.upgradeLevels)
         {
            SeladonColor.saturate(_loc1_);
         }
      }
      
      public function setLock() : void
      {
         var _loc1_:Bitmap = null;
         this.ResetState();
         SeladonColor.seladon(this.ico_ld);
         this.lock_bm.visible = true;
         if(this.upgradeLevels.length == 0)
         {
            return;
         }
         for each(_loc1_ in this.upgradeLevels)
         {
            SeladonColor.seladon(_loc1_);
         }
      }
      
      public function setNoNafta() : void
      {
         var _loc1_:Bitmap = null;
         this.ResetState();
         SeladonColor.seladon(this.ico_ld);
         this.nafta.visible = true;
         if(this.upgradeLevels.length == 0)
         {
            return;
         }
         for each(_loc1_ in this.upgradeLevels)
         {
            SeladonColor.seladon(_loc1_);
         }
      }
      
      public function setNafta() : void
      {
         this.ResetState();
         this.dragable = true;
      }
      
      public function setCanBuy() : void
      {
         this.ResetState();
      }
      
      public function setEnable() : void
      {
         this.ResetState();
         this.dragable = true;
      }
      
      public function setOnTur() : void
      {
         var _loc1_:Bitmap = null;
         this.ResetState();
         SeladonColor.seladon(this.ico_ld);
         this.dragable = true;
         if(this.upgradeLevels.length == 0)
         {
            return;
         }
         for each(_loc1_ in this.upgradeLevels)
         {
            SeladonColor.seladon(_loc1_);
         }
      }
      
      public function set icon(param1:String) : void
      {
         this.path = param1;
         if(this.path.length == 0)
         {
            this.ico_ld.unload();
            return;
         }
         var _loc2_:URLRequest = new URLRequest(param1);
         this.ico_ld.unload();
         this.ico_ld.load(_loc2_);
      }
      
      public function get icon() : String
      {
         return this.path;
      }
      
      public function set altIcon(param1:String) : void
      {
         this.altPath = param1;
      }
      
      public function get altIcon() : String
      {
         return this.altPath;
      }
      
      private function icoInit(param1:Event) : void
      {
         this.ico_ld.x = -this.ico_ld.width / 2;
         this.ico_ld.y = -this.ico_ld.height / 2;
         param1.target.content.smoothing = true;
      }
      
      public function ApplyCanBuyEffect() : void
      {
         if(this.canBuyGlow_mc != null)
         {
            return;
         }
         this.canBuyGlow_mc = new CanBuyGlowEffect();
         this.canBuyGlow_mc.x = this.SIZE / 2;
         this.canBuyGlow_mc.y = this.SIZE / 2;
         this.canBuyGlow_mc.mouseEnabled = false;
         this.canBuyGlow_mc.mouseChildren = false;
         this.addChildAt(this.canBuyGlow_mc,0);
         this.tweenBrightness.start();
      }
      
      public function RemoveCanBuyEffect() : void
      {
         if(this.canBuyGlow_mc == null)
         {
            return;
         }
         this.removeChild(this.canBuyGlow_mc);
         this.canBuyGlow_mc = null;
         this.tweenBrightness.stop();
         this.tweenBrightness.rewind();
         this.icoCont_mc.filters = [];
      }
      
      public function HeroIsDead(param1:Boolean) : void
      {
         this.heroIsDead = param1;
      }
      
      public function SetUpgradeLevel(param1:int) : void
      {
         var _loc5_:Bitmap = null;
         this.UpgradeLevel = param1;
         var _loc2_:Class = getDefinitionByName("upgradeLevelIcon") as Class;
         if(param1 < 0 || param1 > this.maxUpgradeLevel)
         {
            return;
         }
         var _loc3_:int = 0;
         while(_loc3_ < this.upgradeLevels.length)
         {
            this.icoCont_mc.removeChild(this.upgradeLevels[_loc3_]);
            _loc3_++;
         }
         this.currentUpgradeLevelPos = this.SIZE / 2;
         this.upgradeLevels = new Array();
         var _loc4_:int = 0;
         while(_loc4_ < param1)
         {
            _loc5_ = new Bitmap(new _loc2_());
            this.icoCont_mc.addChild(_loc5_);
            _loc5_.x = this.currentUpgradeLevelPos - _loc5_.width;
            _loc5_.y = -this.SIZE / 2;
            this.upgradeLevels.push(_loc5_);
            this.currentUpgradeLevelPos = _loc5_.x;
            _loc4_++;
         }
      }
      
      internal function tweenBrightnessChange(param1:TweenEvent) : void
      {
         this.color_arr = [0.6,0.46,0.46,0,-71,0.38,0.34,0.36,0.21,-48,0.57,0.54,0.44,this.brightness,-73,0,0,0,1,0];
         this.icoFilter.matrix = this.color_arr;
         this.icoCont_mc.filters = [this.icoFilter];
      }
      
      internal function tweenBrightnessFinish(param1:TweenEvent) : void
      {
         this.tweenBrightness.yoyo();
      }
      
      public function ShowTutorialHighlight() : void
      {
         if(this.tutorialHighLight != null)
         {
            return;
         }
         this.tutorialHighLight = new SquadStroke();
         this.addChildAt(this.tutorialHighLight,this.numChildren - 1);
         this.tutorialHighLight.setSize(this.SIZE,this.SIZE);
         this.tutorialHighLight.x = (this.SIZE - this.tutorialHighLight.width) / 2;
         this.tutorialHighLight.y = (this.SIZE - this.tutorialHighLight.height) / 2;
      }
      
      public function HideTutorialHighlight() : void
      {
         if(this.tutorialHighLight == null)
         {
            return;
         }
         this.removeChild(this.tutorialHighLight);
         this.tutorialHighLight = null;
      }
   }
}

