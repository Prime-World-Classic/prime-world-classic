package Chat
{
   import BaseClasses.BaseResizableObject;
   import Components.ButtonSquare;
   import Controllers.SoundController;
   import Datas.Channel;
   import Datas.ChatMessage;
   import Enums.EChatChannel;
   import Events.ChatBarChannelSelectEvent;
   import Events.ChatBarPlayerSelectEvent;
   import Events.PWComponentValueEvent;
   import LoaderSources.LoaderLocalization;
   import MainScreen.SetPlayerShortName;
   import flash.display.MovieClip;
   import flash.events.Event;
   import flash.events.MouseEvent;
   
   [Embed(source="/_assets/assets.swf", symbol="symbol3720")]
   public class ChatBar extends BaseResizableObject
   {
      
      public var chatOutput_mc:ChatOutputTextBox;
      
      public var chatInput_mc:ChatInputTextBox;
      
      public var up_btn:ButtonSquare;
      
      public var down_btn:ButtonSquare;
      
      public var end_btn:ButtonSquare;
      
      private var _index:int;
      
      private var blink_mc:MovieClip;
      
      private var channels:Array = [];
      
      private var isBlinking:Boolean = false;
      
      private var friendsList:Array = new Array();
      
      private var privateChannelId:int = -1;
      
      private var ignoringPlayers:Array = new Array();
      
      private var disabledInput:Boolean = false;
      
      public var knownPlayers:Array = new Array();
      
      public var CantFindPlayer:String = "";
      
      private var _shortheroname:SetPlayerShortName;
      
      private var _playerIcons:Object = new Object();
      
      private var _useLoadingLocalisation:Boolean;
      
      private var ourPlayerId:int;
      
      private var ourHeroId:int;
      
      private var playerTeam:Object = new Object();
      
      private var heroTeam:Object = new Object();
      
      private var ourIdSetted:Boolean;
      
      private var delayedMessages:Array = new Array();
      
      public function ChatBar()
      {
         super();
         this._shortheroname = new SetPlayerShortName();
         this.blink_mc = new Blink();
         this.up_btn.addEventListener(MouseEvent.MOUSE_UP,this.ScrollOutputTextUp);
         this.down_btn.addEventListener(MouseEvent.MOUSE_UP,this.ScrollOutputTextDown);
         this.end_btn.addEventListener(MouseEvent.MOUSE_UP,this.ScrollOutputTextToTheEnd);
         if(this.chatInput_mc != null)
         {
            this.chatInput_mc.knownPlayers = this.knownPlayers;
            this.chatInput_mc.SendErrorMessageToChat = this.SendErrorMessageToChat;
         }
         this.end_btn.button_mc.addChild(this.blink_mc);
         this.blink_mc.stop();
         this.chatOutput_mc.addEventListener(ChatBarChannelSelectEvent.EVENT_TYPE,this.ChangeChannel);
         this.chatOutput_mc.addEventListener(ChatBarPlayerSelectEvent.EVENT_TYPE,this.ChangePlayer);
         this.chatInput_mc.addEventListener(ChatBarChannelSelectEvent.EVENT_TYPE,this.ChangeChannel);
         this.chatInput_mc.addEventListener(PWComponentValueEvent.CHANGE,this.InputChannelChangedFromCombo);
         this.chatOutput_mc.IgnoringPlayers = this.ignoringPlayers;
         this.ourIdSetted = false;
         this.__setProp_end_btn_chat_mc_buttons_0();
         this.__setProp_down_btn_chat_mc_buttons_0();
         this.__setProp_up_btn_chat_mc_buttons_0();
      }
      
      private static function ShouldDisplayMessageIcon(param1:int, param2:int) : Boolean
      {
         if(param2 < 0)
         {
            return false;
         }
         switch(param1)
         {
            case EChatChannel.MyTeam:
            case EChatChannel.BothTeams:
            case EChatChannel.SmartChat:
               return true;
            default:
               return false;
         }
      }
      
      public function SetUseLoadingLocalisation() : void
      {
         this.chatOutput_mc.SetUseLoadingLocalisation();
         this._useLoadingLocalisation = true;
      }
      
      public function SetPlayerIcon(param1:int, param2:String) : void
      {
         if(param1 < 0)
         {
            return;
         }
         this._playerIcons[param1] = param2;
      }
      
      public function AddChannel(param1:int, param2:String, param3:uint, param4:Boolean, param5:Boolean, param6:Boolean, param7:int) : void
      {
         var _loc8_:Channel = this.GetChannelByID(param1);
         if(param7 == Channel.PrivateChannel)
         {
            this.privateChannelId = param1;
         }
         if(_loc8_ != null)
         {
            _loc8_.ChannelName = param2;
            _loc8_.ChannelColor = param3;
            _loc8_.ShowChannelName = param4;
            _loc8_.ShowPlayerName = param5;
            _loc8_.PlayerCanWriteInTo = param6;
            _loc8_.ChannelType = param7;
            this.chatInput_mc.AddChannel(_loc8_);
            return;
         }
         var _loc9_:Channel = new Channel();
         _loc9_.ChannelId = param1;
         _loc9_.ChannelName = param2;
         _loc9_.ChannelColor = param3;
         _loc9_.ShowChannelName = param4;
         _loc9_.ShowPlayerName = param5;
         _loc9_.PlayerCanWriteInTo = param6;
         _loc9_.ChannelType = param7;
         this.channels.push(_loc9_);
         this.chatInput_mc.AddChannel(_loc9_);
      }
      
      private function IsPlayerIgnored(param1:int) : Boolean
      {
         if(param1 < 0)
         {
            return false;
         }
         if(this.ignoringPlayers.indexOf(param1) < 0)
         {
            return false;
         }
         return true;
      }
      
      public function AddMessage(param1:int, param2:String, param3:String, param4:int = -1) : void
      {
         var _loc6_:Boolean = false;
         var _loc7_:Boolean = false;
         var _loc8_:ChatMessage = null;
         if(param1 != EChatChannel.SmartChat)
         {
            if(this.IsPlayerIgnored(param4))
            {
               return;
            }
         }
         var _loc5_:Channel = this.GetChannelByID(param1);
         if(!this.IsWeKnowPlayer(param2) && param2.length != 0)
         {
            this.knownPlayers.push(param2);
         }
         if(_loc5_ == null)
         {
            return;
         }
         if(ShouldDisplayMessageIcon(param1,param4))
         {
            if(this.ourIdSetted)
            {
               _loc7_ = false;
               if(this._useLoadingLocalisation)
               {
                  if(this.playerTeam.hasOwnProperty(param4) && this.heroTeam.hasOwnProperty(this.ourHeroId))
                  {
                     _loc6_ = this.playerTeam[param4] != this.heroTeam[this.ourHeroId];
                     _loc7_ = true;
                  }
               }
               else if(this.playerTeam.hasOwnProperty(param4) && this.playerTeam.hasOwnProperty(this.ourPlayerId))
               {
                  _loc6_ = this.playerTeam[param4] != this.playerTeam[this.ourPlayerId];
                  _loc7_ = true;
               }
               if(_loc7_)
               {
                  this.chatOutput_mc.AddMessage(_loc5_,param4,this._shortheroname.SetPlayerName(param2),param3,this._playerIcons[param4],_loc6_);
               }
               else
               {
                  this.chatOutput_mc.AddMessage(_loc5_,param4,this._shortheroname.SetPlayerName(param2),param3,this._playerIcons[param4]);
               }
            }
            else
            {
               _loc8_ = new ChatMessage();
               _loc8_.ChannelData = _loc5_;
               _loc8_.PlayerId = param4;
               _loc8_.PlayerName = this._shortheroname.SetPlayerName(param2);
               _loc8_.Message = param3;
               _loc8_.Icon = this._playerIcons[param4];
               this.delayedMessages.push(_loc8_);
            }
         }
         else
         {
            this.chatOutput_mc.AddMessage(_loc5_,param4,this._shortheroname.SetPlayerName(param2),param3);
         }
         if(_loc5_.ChannelType == Channel.PrivateChannel && param2.length != 0)
         {
            this.chatInput_mc.AddPlayerToInputCombo(param2);
         }
         if(!this.isBlinking && this.chatOutput_mc.CurrentTextPosition != 0)
         {
            this.blink_mc.visible = true;
            this.blink_mc.play();
            this.isBlinking = true;
         }
      }
      
      private function IsWeKnowPlayer(param1:String) : Boolean
      {
         var _loc2_:String = null;
         for each(_loc2_ in this.knownPlayers)
         {
            if(_loc2_ == param1)
            {
               return true;
            }
         }
         return false;
      }
      
      private function ChangePlayer(param1:ChatBarPlayerSelectEvent) : void
      {
         this.chatInput_mc.SelectInputComboPlayer(param1.Name);
         if(this.privateChannelId == -1)
         {
            return;
         }
         this.chatInput_mc.SetInputChannel(this.GetChannelByID(this.privateChannelId));
      }
      
      private function GetChannelByID(param1:int) : Channel
      {
         var _loc2_:int = 0;
         while(_loc2_ < this.channels.length)
         {
            if(this.channels[_loc2_].ChannelId == param1)
            {
               return this.channels[_loc2_];
            }
            _loc2_++;
         }
         return null;
      }
      
      private function ScrollOutputTextUp(param1:MouseEvent) : void
      {
         this.chatOutput_mc.ScrollOneStringUp();
         this.ShowOutput();
      }
      
      private function ScrollOutputTextDown(param1:MouseEvent) : void
      {
         this.chatOutput_mc.ScrollOneStringDown();
         this.ShowOutput();
         if(this.isBlinking && this.chatOutput_mc.CurrentTextPosition == 0)
         {
            this.blink_mc.visible = false;
            this.blink_mc.stop();
            this.isBlinking = false;
         }
      }
      
      private function ScrollOutputTextToTheEnd(param1:MouseEvent) : void
      {
         this.chatOutput_mc.ScrollToTheEnd();
         this.ShowOutput();
         if(this.isBlinking && this.chatOutput_mc.CurrentTextPosition == 0)
         {
            this.blink_mc.visible = false;
            this.blink_mc.stop();
            this.isBlinking = false;
         }
      }
      
      private function ShowOutput() : void
      {
         if(this.chatInput_mc.visible)
         {
            return;
         }
         this.chatOutput_mc.ShowOutputBox();
         this.chatOutput_mc.StartOutputFaidOut();
      }
      
      public function SetDefaultChannel(param1:int) : void
      {
         this.chatInput_mc.SetInputChannel(this.GetChannelByID(param1));
      }
      
      public function DisableInput(param1:Boolean) : void
      {
         this.disabledInput = param1;
      }
      
      public function EnterPressed() : void
      {
         if(this.disabledInput)
         {
            return;
         }
         if(this.chatInput_mc.visible && this.chatInput_mc.combo_player.AutoFillSelectin())
         {
            this.chatInput_mc.combo_player.EnterPressed();
            return;
         }
         FSCommands.Send(FSCommands.ACTIVATE_CHAT_INPUT,int(!this.chatInput_mc.visible).toString());
         SoundController.PlaySound(SoundController.CHAT_SOUND);
         if(this.chatInput_mc.visible)
         {
            this.chatInput_mc.EnterPressed();
            this.chatOutput_mc.StartOutputFaidOut();
            return;
         }
         this.chatInput_mc.StartInputText();
         this.chatOutput_mc.ShowOutputBox();
      }
      
      public function OpenChannel(param1:int) : void
      {
         if(this.disabledInput)
         {
            return;
         }
         var _loc2_:Channel = this.GetChannelByID(param1);
         if(_loc2_ == null)
         {
            return;
         }
         if(!this.chatInput_mc.visible)
         {
            FSCommands.Send(FSCommands.ACTIVATE_CHAT_INPUT,int(!this.chatInput_mc.visible).toString());
            SoundController.PlaySound(SoundController.CHAT_SOUND);
            this.chatInput_mc.StartInputText();
            this.chatOutput_mc.ShowOutputBox();
         }
         this.chatInput_mc.SetInputChannel(_loc2_);
      }
      
      public function SendErrorMessageToChat() : void
      {
         var _loc1_:Channel = null;
         for each(_loc1_ in this.channels)
         {
            if(_loc1_.ChannelType == Channel.SystemChannel)
            {
               this.AddMessage(_loc1_.ChannelId,"",this.CantFindPlayer);
               return;
            }
         }
      }
      
      public function EscPressed() : void
      {
         FSCommands.Send(FSCommands.ACTIVATE_CHAT_INPUT,int(!this.chatInput_mc.visible).toString());
         this.chatInput_mc.EscPressed();
         this.chatOutput_mc.StartOutputFaidOut();
      }
      
      public function AddChannelShortCut(param1:int, param2:String) : void
      {
         this.chatInput_mc.AddChannelShortCut(param1,param2);
      }
      
      public function AddFriend(param1:String) : void
      {
         var _loc2_:String = null;
         for each(_loc2_ in this.friendsList)
         {
            if(_loc2_ == param1)
            {
               return;
            }
         }
         if(!this.IsWeKnowPlayer(param1))
         {
            this.knownPlayers.push(param1);
         }
         this.friendsList.push(param1);
         this.chatInput_mc.AddFriend(param1);
      }
      
      public function AddIgnoringUser(param1:int, param2:String = "") : void
      {
         var _loc4_:* = undefined;
         var _loc3_:int = this.ignoringPlayers.indexOf(param1);
         if(_loc3_ < 0)
         {
            this.ignoringPlayers.push(param1);
            trace("Ignore " + param1.toString());
            if(param2 != "")
            {
               _loc4_ = this._useLoadingLocalisation ? LoaderLocalization.ChatMessage_Ignore : Localization.ChatMessage_Ignore;
               _loc4_ = "<style:TT_Chat>" + _loc4_.replace("{0}","<style:TT_Players_Name>" + param2 + "<style:TT_Chat>") + "<style:TT_Chat>";
               this.AddMessage(EChatChannel.Achievments,"",_loc4_);
            }
         }
      }
      
      public function RemoveIgnoringUser(param1:int, param2:String = "") : void
      {
         var _loc4_:* = undefined;
         var _loc3_:int = this.ignoringPlayers.indexOf(param1);
         if(_loc3_ < 0)
         {
            return;
         }
         this.ignoringPlayers.splice(_loc3_,1);
         trace("Forgive " + param1.toString());
         if(param2 != "")
         {
            _loc4_ = this._useLoadingLocalisation ? LoaderLocalization.ChatMessage_RemoveIgnore : Localization.ChatMessage_RemoveIgnore;
            _loc4_ = "<style:TT_Chat>" + _loc4_.replace("{0}","<style:TT_Players_Name>" + param2 + "<style:TT_Chat>") + "<style:TT_Chat>";
            this.AddMessage(EChatChannel.Achievments,"",_loc4_);
         }
      }
      
      public function ForcedHide() : void
      {
         if(this.chatInput_mc.visible)
         {
            FSCommands.Send(FSCommands.ACTIVATE_CHAT_INPUT,int(!this.chatInput_mc.visible).toString());
         }
         this.chatInput_mc.EscPressed();
         this.chatOutput_mc.ForceHide();
      }
      
      private function InputChannelChangedFromCombo(param1:PWComponentValueEvent) : void
      {
         var _loc2_:Channel = null;
         param1.stopPropagation();
         for each(_loc2_ in this.channels)
         {
            if(_loc2_.ComboBoxIndex == param1.componentValue)
            {
               this.chatInput_mc.SetInputChannel(_loc2_);
               return;
            }
         }
      }
      
      private function ChangeChannel(param1:ChatBarChannelSelectEvent) : void
      {
         param1.stopPropagation();
         this.chatInput_mc.SetInputChannel(this.GetChannelByID(param1.id));
      }
      
      // Dota-like centered chat (option 'chat_centered'); static: the instance layout must stay as on the timeline
      private static var centered:Boolean = false;
      
      private static var centeredRaise:Number = 0.2;
      
      public function SetCentered(param1:Boolean, param2:Number) : void
      {
         ChatBar.centered = param1;
         ChatBar.centeredRaise = param2;
         if(stage != null)
         {
            this.BaseBarObjectResize();
         }
      }
      
      override protected function BaseBarObjectResize(param1:Event = null) : void
      {
         super.BaseBarObjectResize(param1);
         this.x = 0;
         this.y = stage.stageHeight - (initialStage.y - startPozition.y) * scale;
         if(ChatBar.centered)
         {
            this.x = Math.floor((stage.stageWidth - this.width) / 2);
            this.y = Math.floor(this.y - stage.stageHeight * ChatBar.centeredRaise);
         }
      }
      
      public function SetPlayerHeroId(param1:int, param2:int, param3:int) : void
      {
         this.playerTeam[param1] = param3;
         this.heroTeam[param2] = param3;
      }
      
      public function SetOurHeroId(param1:int) : void
      {
         this.ourHeroId = param1;
         this.ourIdSetted = true;
      }
      
      public function SetOurPlayerId(param1:int) : void
      {
         var _loc2_:ChatMessage = null;
         var _loc3_:Boolean = false;
         var _loc4_:Boolean = false;
         this.ourPlayerId = param1;
         this.ourIdSetted = true;
         if(this.delayedMessages.length > 0)
         {
            for each(_loc2_ in this.delayedMessages)
            {
               _loc4_ = false;
               if(this._useLoadingLocalisation)
               {
                  if(this.playerTeam.hasOwnProperty(_loc2_.PlayerId) && this.heroTeam.hasOwnProperty(this.ourHeroId))
                  {
                     _loc3_ = this.playerTeam[_loc2_.PlayerId] != this.heroTeam[this.ourHeroId];
                     _loc4_ = true;
                  }
               }
               else if(this.playerTeam.hasOwnProperty(_loc2_.PlayerId) && this.playerTeam.hasOwnProperty(this.ourPlayerId))
               {
                  _loc3_ = this.playerTeam[_loc2_.PlayerId] != this.playerTeam[this.ourPlayerId];
                  _loc4_ = true;
               }
               if(_loc4_)
               {
                  this.chatOutput_mc.AddMessage(_loc2_.ChannelData,_loc2_.PlayerId,_loc2_.PlayerName,_loc2_.Message,_loc2_.Icon,_loc3_);
               }
               else
               {
                  this.chatOutput_mc.AddMessage(_loc2_.ChannelData,_loc2_.PlayerId,_loc2_.PlayerName,_loc2_.Message,_loc2_.Icon);
               }
            }
            this.delayedMessages.splice(0);
         }
      }
      
      internal function __setProp_end_btn_chat_mc_buttons_0() : *
      {
         try
         {
            this.end_btn["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         this.end_btn.Ico = "allDown_png";
         this.end_btn.Toggle = false;
         try
         {
            this.end_btn["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_down_btn_chat_mc_buttons_0() : *
      {
         try
         {
            this.down_btn["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         this.down_btn.Ico = "down_png";
         this.down_btn.Toggle = false;
         try
         {
            this.down_btn["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_up_btn_chat_mc_buttons_0() : *
      {
         try
         {
            this.up_btn["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         this.up_btn.Ico = "up_png";
         this.up_btn.Toggle = false;
         try
         {
            this.up_btn["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
   }
}

