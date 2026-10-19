package Effects
{
   import Controllers.SoundController;
   import fl.motion.easing.Linear;
   import fl.transitions.Tween;
   import flash.display.MovieClip;
   import flash.events.TimerEvent;
   import flash.geom.ColorTransform;
   import flash.utils.Timer;
   
   public class AttentionEffect extends MovieClip
   {
      
      private static const BIRTH_FRAMES:uint = 15;
      
      private static const PING_FRAMES:uint = 20;
      
      // false: the C++ side plays its own ping sounds (MainInterface.SetMinimapAlertSound)
      public static var soundEnabled:Boolean = true;
      
      private var _free:Boolean = true;
      
      private var _timer:Timer = null;
      
      private var _fader:Tween = null;
      
      public function AttentionEffect()
      {
         super();
         mouseEnabled = false;
         mouseChildren = false;
         scaleX = 1.5;
         scaleY = 1.5;
         visible = false;
         stop();
      }
      
      public function StartFade() : void
      {
      }
      
      private function CleanupLifeTimeController() : void
      {
         if(this._timer != null)
         {
            this._timer.stop();
         }
         if(this._fader != null)
         {
            this._fader.stop();
         }
         this._timer = null;
         this._fader = null;
      }
      
      private function StartLifeTimeController(param1:Number) : void
      {
         var _loc2_:Number = Number(BIRTH_FRAMES) / stage.frameRate;
         var _loc3_:Number = Number(PING_FRAMES) / stage.frameRate;
         var _loc4_:Number = Math.ceil((param1 - _loc2_) / _loc3_);
         if(_loc4_ < 1)
         {
            _loc4_ = 1;
         }
         var _loc5_:* = _loc3_ * _loc4_;
         var _loc6_:* = _loc2_ + _loc5_;
         this._timer = new Timer(_loc6_ * 1000,1);
         this._timer.addEventListener(TimerEvent.TIMER_COMPLETE,this.OnTimerComplete);
         this._timer.start();
         this._fader = new Tween(this,"alpha",Linear.easeNone,1,0,_loc5_,true);
         this._fader.start();
      }
      
      private function Stop() : void
      {
         this._free = true;
         visible = false;
         gotoAndStop(1);
         this.CleanupLifeTimeController();
      }
      
      public function Play(param1:Number = 5) : void
      {
         this._free = false;
         alpha = 1;
         visible = true;
         gotoAndPlay(1);
         if(AttentionEffect.soundEnabled && !SoundController.muteMinimapAlert)
         {
            SoundController.PlaySound(SoundController.MINIMAP_ALERT);
         }
         this.StartLifeTimeController(param1);
      }
      
      public function get Available() : Boolean
      {
         return this._free;
      }
      
      public function set Color(param1:uint) : void
      {
         var _loc2_:Number = param1 >> 16 & 0xFF;
         var _loc3_:Number = param1 >> 8 & 0xFF;
         var _loc4_:Number = param1 >> 0 & 0xFF;
         _loc2_ /= 255;
         _loc3_ /= 255;
         _loc4_ /= 255;
         var _loc5_:ColorTransform = transform.colorTransform;
         _loc5_.redMultiplier = _loc2_;
         _loc5_.greenMultiplier = _loc3_;
         _loc5_.blueMultiplier = _loc4_;
         transform.colorTransform = _loc5_;
      }
      
      private function OnTimerComplete(param1:TimerEvent) : void
      {
         this.Stop();
      }
   }
}

