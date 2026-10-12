package EscMenu
{
   import BaseClasses.BaseDragObject;
   import BaseClasses.TooltipSender;
   import Components.ButtonStandart;
   import Components.PWCheckBox;
   import Components.PWCombobox;
   import Components.PWSlider;
   import Components.WFrame;
   import Events.PWComponentValueEvent;
   import Events.WindowVisibleControllerEvent;
   import Interfaces.IPWComponents;
   import flash.display.DisplayObject;
   import flash.display.Sprite;
   import flash.events.Event;
   import flash.text.TextField;
   import flash.utils.Dictionary;
   
   [Embed(source="/_assets/assets.swf", symbol="symbol3733")]
   public class OptionsWindow extends BaseDragObject
   {
      
      public var title:TextField;
      
      public var wFrame_mc:WFrame;
      
      public var tab1:ButtonStandart;
      
      public var shortcutsTab:ShortcutsTab;
      
      public var settingsTab:OptionsSettingsTab;
      
      private var checkBoxes:Array = [];
      
      private var comboBoxes:Array = [];
      
      private var sliders:Array = [];
      
      private var binded:Dictionary = new Dictionary();
      
      private var isApplyPressed:Boolean;
      
      private var _filterChatMessagesTooltip:TooltipSender;
      
      // "Advanced" settings page: rows are created at runtime by C++ (PreferencesProcessor).
      // State is STATIC on purpose: the instance layout must stay exactly as in the original class,
      // because the timeline (options window symbol) binds its children to instance slots.
      private static var advancedBtn:ButtonStandart;
      
      private static var advancedPanel:Sprite;
      
      private static var advancedRows:Dictionary;
      
      private static var advancedNextY:Number = 0;
      
      private static var isAdvancedShown:Boolean = false;
      
      // extra pages ("Advanced", "Communication"): panel, button, next row Y and title per page
      private static var pagePanels:Array;
      
      private static var pageButtons:Array;
      
      private static var pageNextY:Array;
      
      private static var buildPage:int = 0;
      
      private static var shownPage:int = -1;
      
      private static const PAGE_NAMES:Array = ["Дополнительно","Общение"];
      
      private static const PAGE_TITLES:Array = ["Дополнительные настройки","Общение"];
      
      private static const ADV_SLIDER_WIDTH:Number = 140;
      
      private static const ADV_VALUE_WIDTH:Number = 50;
      
      public function OptionsWindow()
      {
         super();
         this._filterChatMessagesTooltip = new TooltipSender(this.settingsTab.smartchat_cb);
         this.checkBoxes.push(this.settingsTab.windowed_tb,this.settingsTab.leftClick_tb,this.settingsTab.useBadwordsFilter,this.settingsTab.smartchat_cb);
         this.comboBoxes.push(this.settingsTab.resolution,this.settingsTab.graphic);
         this.sliders.push(this.settingsTab.sfx_mc,this.settingsTab.music_mc,this.settingsTab.voice_mc);
         addEventListener(PWComponentValueEvent.CHANGE,this.ComponentValueChanged);
         this.settingsTab.apply_btn.addEventListener("Up",this.ApplyPreferences);
         this.settingsTab.cansel_btn.addEventListener("Up",this.CanselPreferences);
         this.settingsTab.warning.visible = false;
         this.settingsTab.apply_btn.EnableButton = false;
         this.settingsTab.cansel_btn.EnableButton = false;
         this.settingsTab.common.visible = false;
         this.settingsTab.common_mc.visible = false;
         this.tab1.addEventListener("Up",this.Tab1Click);
         this.settingsTab.visible = true;
         this.shortcutsTab.visible = false;
         this.shortcutsTab.apply_btn.addEventListener("Up",this.ApplyShortCutClick);
         this.shortcutsTab.cancel_btn.addEventListener("Up",this.CancelShortCutClick);
         if(Localization.CompleteEventDispatcher != null)
         {
            Localization.CompleteEventDispatcher.addEventListener(Event.COMPLETE,this.FillLocalization);
         }
         this.__setProp_tab1_options_pnl_tabs_0();
         try
         {
            this.CreateAdvancedPage();
         }
         catch(e:Error)
         {
            OptionsWindow.advancedPanel = null;
            OptionsWindow.advancedBtn = null;
            OptionsWindow.pagePanels = null;
         }
      }
      
      private function CreateAdvancedPage() : void
      {
         var _loc1_:int = 0;
         var _loc2_:Sprite = null;
         var _loc3_:ButtonStandart = null;
         OptionsWindow.advancedRows = new Dictionary();
         OptionsWindow.isAdvancedShown = false;
         OptionsWindow.pagePanels = [];
         OptionsWindow.pageButtons = [];
         OptionsWindow.pageNextY = [];
         OptionsWindow.buildPage = 0;
         OptionsWindow.shownPage = -1;
         _loc1_ = 0;
         while(_loc1_ < OptionsWindow.PAGE_NAMES.length)
         {
            _loc2_ = new Sprite();
            _loc2_.x = this.shortcutsTab.x + this.shortcutsTab.shortcutContainer.x;
            _loc2_.y = this.shortcutsTab.y + this.shortcutsTab.shortcutContainer.y;
            _loc2_.visible = false;
            addChild(_loc2_);
            OptionsWindow.pagePanels.push(_loc2_);
            OptionsWindow.pageNextY.push(0);
            // ButtonStandart is a base class without graphics: create the library button symbol (same as "Keys" button)
            _loc3_ = new myButtonStandart();
            OptionsWindow.pageButtons.push(_loc3_);
            _loc1_++;
         }
         // the "Keys" button and the page buttons in one centered row
         _loc3_ = OptionsWindow.pageButtons[0];
         this.tab1.x -= (_loc3_.width + 8) * OptionsWindow.pageButtons.length / 2;
         var _loc4_:Number = this.tab1.x + this.tab1.width + 8;
         _loc1_ = 0;
         while(_loc1_ < OptionsWindow.pageButtons.length)
         {
            _loc3_ = OptionsWindow.pageButtons[_loc1_];
            _loc3_.x = _loc4_;
            _loc3_.y = this.tab1.y;
            _loc4_ += _loc3_.width + 8;
            this.SetPageButtonText(_loc1_,OptionsWindow.PAGE_NAMES[_loc1_]);
            _loc3_.addEventListener("Up",this.PageButtonClick);
            addChild(_loc3_);
            _loc1_++;
         }
         OptionsWindow.advancedPanel = OptionsWindow.pagePanels[0];
         OptionsWindow.advancedBtn = OptionsWindow.pageButtons[0];
         OptionsWindow.advancedNextY = 0;
      }
      
      // C++ (PreferencesProcessor): the following rows go to this page
      public function SetAdvancedTargetPage(param1:int) : void
      {
         if(OptionsWindow.pagePanels == null || param1 < 0 || param1 >= OptionsWindow.pagePanels.length)
         {
            return;
         }
         OptionsWindow.pageNextY[OptionsWindow.buildPage] = OptionsWindow.advancedNextY;
         OptionsWindow.buildPage = param1;
         OptionsWindow.advancedPanel = OptionsWindow.pagePanels[param1];
         OptionsWindow.advancedNextY = OptionsWindow.pageNextY[param1];
      }
      
      private function SetPageButtonText(param1:int, param2:String) : void
      {
         var _loc3_:ButtonStandart = OptionsWindow.pageButtons[param1];
         try
         {
            _loc3_["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         _loc3_.labelColor = 16765321;
         _loc3_.labelText = param2;
         try
         {
            _loc3_["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      private function PageButtonClick(param1:Event) : void
      {
         var _loc2_:int = OptionsWindow.pageButtons.indexOf(param1.currentTarget);
         if(_loc2_ < 0)
         {
            _loc2_ = OptionsWindow.pageButtons.indexOf(param1.target);
         }
         if(_loc2_ < 0)
         {
            return;
         }
         // NB: no '?:' mixing types here - the JPEXS compiler makes bytecode the verifier rejects (#1068)
         var _loc3_:int = _loc2_;
         if(OptionsWindow.shownPage == _loc2_)
         {
            _loc3_ = -1;
         }
         try
         {
            this.ShowPage(_loc3_);
         }
         catch(e:Error)
         {
            // an exception here must not reach the native event dispatcher (it crashes the game)
         }
      }
      
      // -1 = back to the main settings
      private function ShowPage(param1:int) : void
      {
         var _loc2_:int = 0;
         var _loc3_:String = null;
         var _loc4_:Boolean = false;
         OptionsWindow.shownPage = param1;
         OptionsWindow.isAdvancedShown = false;
         if(param1 >= 0)
         {
            OptionsWindow.isAdvancedShown = true;
         }
         _loc2_ = 0;
         while(_loc2_ < OptionsWindow.pagePanels.length)
         {
            _loc4_ = false;
            if(_loc2_ == param1)
            {
               _loc4_ = true;
            }
            Sprite(OptionsWindow.pagePanels[_loc2_]).visible = _loc4_;
            if(param1 < 0)
            {
               ButtonStandart(OptionsWindow.pageButtons[_loc2_]).visible = true;
            }
            else
            {
               ButtonStandart(OptionsWindow.pageButtons[_loc2_]).visible = _loc4_;
            }
            _loc3_ = String(OptionsWindow.PAGE_NAMES[_loc2_]);
            if(_loc4_)
            {
               _loc3_ = "Назад";
            }
            this.SetPageButtonText(_loc2_,_loc3_);
            _loc2_++;
         }
         this.settingsTab.visible = !OptionsWindow.isAdvancedShown;
         this.shortcutsTab.visible = false;
         this.tab1.visible = !OptionsWindow.isAdvancedShown;
         if(param1 >= 0)
         {
            this.title.text = String(OptionsWindow.PAGE_TITLES[param1]);
         }
         else
         {
            this.title.text = Localization.PreferenceMenu_Header;
         }
      }
      
      private function SetAdvancedButtonText(param1:String) : void
      {
         try
         {
            OptionsWindow.advancedBtn["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         OptionsWindow.advancedBtn.labelColor = 16765321;
         OptionsWindow.advancedBtn.labelText = param1;
         try
         {
            OptionsWindow.advancedBtn["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      private function ResetAdvanced() : void
      {
         var _loc1_:int = 0;
         if(OptionsWindow.pagePanels == null)
         {
            return;
         }
         OptionsWindow.shownPage = -1;
         OptionsWindow.isAdvancedShown = false;
         _loc1_ = 0;
         while(_loc1_ < OptionsWindow.pagePanels.length)
         {
            Sprite(OptionsWindow.pagePanels[_loc1_]).visible = false;
            ButtonStandart(OptionsWindow.pageButtons[_loc1_]).visible = true;
            this.SetPageButtonText(_loc1_,OptionsWindow.PAGE_NAMES[_loc1_]);
            _loc1_++;
         }
      }
      
      // right edge of the usable area inside the window frame, in advancedPanel coordinates
      private function GetAdvancedRightLimit(param1:ShortcutItem) : Number
      {
         if(this.wFrame_mc != null && this.wFrame_mc.width > 100)
         {
            return this.wFrame_mc.x + this.wFrame_mc.width - OptionsWindow.advancedPanel.x - 28;
         }
         return param1.valueFrame.x + param1.valueFrame.width;
      }
      
      // layout: | label ............ | [control column] slider (140) | value (50) |
      private function AddAdvancedRow(param1:String, param2:String, param3:DisplayObject, param4:Boolean) : void
      {
         var _loc5_:ShortcutItem = new ShortcutItem();
         _loc5_.SetName(param2);
         _loc5_.SetBind("");
         _loc5_.mouseChildren = false;
         _loc5_.mouseEnabled = false;
         _loc5_.x = 0;
         _loc5_.y = OptionsWindow.advancedNextY;
         OptionsWindow.advancedPanel.addChild(_loc5_);
         _loc5_.valueFrame.visible = false;
         var _loc6_:Number = this.GetAdvancedRightLimit(_loc5_);
         var _loc7_:Number = _loc6_ - OptionsWindow.ADV_VALUE_WIDTH - 10 - OptionsWindow.ADV_SLIDER_WIDTH;
         param3.x = _loc7_;
         param3.y = _loc5_.y + _loc5_.valueFrame.y + (_loc5_.valueFrame.height - param3.height) / 2;
         OptionsWindow.advancedPanel.addChild(param3);
         if(param4)
         {
            _loc5_.value.visible = false;
         }
         else
         {
            _loc5_.value.width = OptionsWindow.ADV_VALUE_WIDTH;
            _loc5_.value.x = _loc6_ - OptionsWindow.ADV_VALUE_WIDTH;
         }
         OptionsWindow.advancedRows[param1] = _loc5_;
         OptionsWindow.advancedNextY = _loc5_.y + _loc5_.height + 3;
      }
      
      public function AddAdvancedCheckBox(param1:String, param2:String) : void
      {
         if(OptionsWindow.advancedPanel == null)
         {
            return;
         }
         var _loc3_:PWCheckBox = new PWCheckBox();
         _loc3_.name = param1;
         this.AddAdvancedRow(param1,param2,_loc3_,true);
         this.binded[param1] = _loc3_;
      }
      
      public function AddAdvancedSlider(param1:String, param2:String, param3:int) : void
      {
         if(OptionsWindow.advancedPanel == null)
         {
            return;
         }
         var _loc4_:PWSlider = new PWSlider();
         _loc4_.name = param1;
         _loc4_.SetSize(OptionsWindow.ADV_SLIDER_WIDTH,0);
         _loc4_.SetSectionsCount(param3);
         this.AddAdvancedRow(param1,param2,_loc4_,false);
         this.binded[param1] = _loc4_;
      }
      
      // category title (same symbol as the headers of the key bindings tab)
      public function AddAdvancedHeader(param1:String) : void
      {
         if(OptionsWindow.advancedPanel == null)
         {
            return;
         }
         var _loc2_:ShortcutsHeader = new ShortcutsHeader();
         if(_loc2_.title != null)
         {
            _loc2_.title.text = param1;
         }
         _loc2_.mouseEnabled = false;
         _loc2_.mouseChildren = false;
         _loc2_.x = 0;
         if(OptionsWindow.advancedNextY > 0)
         {
            OptionsWindow.advancedNextY += 6;
         }
         _loc2_.y = OptionsWindow.advancedNextY;
         OptionsWindow.advancedPanel.addChild(_loc2_);
         OptionsWindow.advancedNextY = _loc2_.y + Math.max(_loc2_.height + 2,26);
      }
      
      public function SetAdvancedValueText(param1:String, param2:String) : void
      {
         var _loc3_:ShortcutItem = OptionsWindow.advancedRows[param1];
         if(_loc3_ != null)
         {
            _loc3_.value.text = param2;
         }
      }
      
      private function ApplyShortCutClick(param1:Event) : void
      {
         this.ResetAdvanced();
         if(this.shortcutsTab.shortcutContainer.IsChanged())
         {
            this.shortcutsTab.shortcutContainer.SendRebinds();
            FSCommands.Send(FSCommands.PREFERENCE_SHORTCUTS_APPLY);
         }
         this.settingsTab.visible = true;
         this.shortcutsTab.visible = false;
         this.tab1.visible = true;
         this.title.text = Localization.PreferenceMenu_Header;
         this.shortcutsTab.shortcutContainer.ClearFocus();
      }
      
      private function CancelShortCutClick(param1:Event) : void
      {
         this.ResetAdvanced();
         this.shortcutsTab.shortcutContainer.ResetToDefault();
         this.settingsTab.visible = true;
         this.shortcutsTab.visible = false;
         this.tab1.visible = true;
         this.title.text = Localization.PreferenceMenu_Header;
         this.shortcutsTab.shortcutContainer.ClearFocus();
      }
      
      private function Tab1Click(param1:Event) : void
      {
         var _loc2_:int = 0;
         if(OptionsWindow.pageButtons != null)
         {
            _loc2_ = 0;
            while(_loc2_ < OptionsWindow.pageButtons.length)
            {
               ButtonStandart(OptionsWindow.pageButtons[_loc2_]).visible = false;
               _loc2_++;
            }
         }
         this.title.text = Localization.ShortcutsMenu_Header;
         this.settingsTab.visible = false;
         this.tab1.visible = false;
         this.shortcutsTab.visible = true;
         this.shortcutsTab.Init();
         this.shortcutsTab.shortcutContainer.ClearFocus();
      }
      
      override public function AddedToStage(param1:Event) : void
      {
         super.AddedToStage(param1);
         this.settingsTab.AddedToStage();
      }
      
      private function FillLocalization(param1:Event) : void
      {
         if(this.settingsTab.visible)
         {
            this.title.text = Localization.PreferenceMenu_Header;
         }
         else
         {
            this.title.text = Localization.ShortcutsMenu_Header;
         }
         this.tab1.LocalizeText = Localization.ShortcutsMenu_Header;
         this.settingsTab.Localize();
         this._filterChatMessagesTooltip.TooltipText = Localization.Tooltip_PreferenceMenu_ReceiveRegularChatMessages;
      }
      
      private function ApplyPreferences(param1:Event) : void
      {
         FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_APPLY);
      }
      
      private function CanselPreferences(param1:Event) : void
      {
         FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_CANCEL);
      }
      
      private function ComponentValueChanged(param1:PWComponentValueEvent) : void
      {
         FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_SET_VALUE,param1.ControlName + " " + param1.componentValue);
         if(param1.ControlName == "smartchat_cb")
         {
            (this.parent as Main).chatBar.chatInput_mc.chatOutput_cb.Value = param1.componentValue;
         }
      }
      
      public function AddCheckBoxControl(param1:String) : void
      {
         var _loc2_:PWCheckBox = null;
         for each(_loc2_ in this.checkBoxes)
         {
            if(_loc2_.name == param1)
            {
               this.binded[param1] = _loc2_;
               return;
            }
         }
      }
      
      public function AddSliderControl(param1:String, param2:int) : void
      {
         var _loc3_:PWSlider = null;
         for each(_loc3_ in this.sliders)
         {
            if(_loc3_.name == param1)
            {
               this.binded[param1] = _loc3_;
               _loc3_.SetSectionsCount(param2);
               return;
            }
         }
      }
      
      public function AddComboBoxControl(param1:String, param2:Array) : void
      {
         var _loc3_:PWCombobox = null;
         var _loc4_:int = 0;
         for each(_loc3_ in this.comboBoxes)
         {
            if(_loc3_.name == param1)
            {
               _loc4_ = 0;
               while(_loc4_ < param2.length)
               {
                  _loc3_.AddComboBoxRow(param2[_loc4_]);
                  _loc4_++;
               }
               this.binded[param1] = _loc3_;
               return;
            }
         }
      }
      
      public function SetControlValue(param1:String, param2:int, param3:Boolean) : void
      {
         if(this.binded[param1] == null)
         {
            return;
         }
         var _loc4_:IPWComponents = this.binded[param1];
         _loc4_.Value = param2;
         _loc4_.Disable = !param3;
         if(param1 == "smartchat_cb")
         {
            (this.parent as Main).chatBar.chatInput_mc.chatOutput_cb.Value = param2;
         }
      }
      
      public function ActivateAbilityToChange(param1:Boolean) : void
      {
         this.settingsTab.apply_btn.EnableButton = param1;
         this.settingsTab.cansel_btn.EnableButton = param1;
      }
      
      override protected function BaseBarObjectResize(param1:Event = null) : void
      {
         super.BaseBarObjectResize(param1);
         this.x = (stage.stageWidth - this.wFrame_mc.width) / 2;
         this.y = (stage.stageHeight - this.wFrame_mc.height) / 2 - 15;
      }
      
      private function OnDownCloseClick(param1:Event) : void
      {
         param1.stopPropagation();
         FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_CANCEL);
         dispatchEvent(new WindowVisibleControllerEvent(WindowVisibleControllerEvent.OPEN_CLOSE,WindowIndex));
      }
      
      override protected function closeThisFunction(param1:Event) : void
      {
         this.CancelShortCutClick(param1);
         super.closeThisFunction(param1);
      }
      
      public function OnCloseByEsc() : void
      {
         this.CancelShortCutClick(null);
      }
      
      override public function SetOnStartPosition() : void
      {
      }
      
      override public function ShowCinematicDialog() : void
      {
      }
      
      override public function HideCinematicDialog() : void
      {
      }
      
      internal function __setProp_tab1_options_pnl_tabs_0() : *
      {
         try
         {
            this.tab1["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         this.tab1.labelColor = 16765321;
         this.tab1.labelText = "Кнопки";
         try
         {
            this.tab1["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
   }
}

