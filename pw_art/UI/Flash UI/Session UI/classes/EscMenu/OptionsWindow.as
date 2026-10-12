package EscMenu 
{
	import BaseClasses.BaseDragObject;
	import BaseClasses.BaseResizableObject;
	import BaseClasses.TooltipSender;
	import Interfaces.IPWComponents;
	import Components.ButtonStandart;
	import Components.PWCheckBox;
	import Components.PWCombobox;
	import Components.PWSlider;
	import Components.WFrame;
	import Effects.AnimationBack;
	import Events.PWComponentValueEvent;
	import Events.WindowVisibleControllerEvent;
	import fl.controls.Slider;
	import flash.display.MovieClip;
	import flash.events.Event;
	import flash.events.MouseEvent;
	import flash.display.DisplayObject;
	import flash.display.Sprite;
	import flash.text.TextField;
	import flash.utils.Dictionary;
	
	public class OptionsWindow extends BaseDragObject
	{
		public var title:TextField;
		
		//public var downClose_btn:ButtonStandart;
		public var wFrame_mc:WFrame;
		
		public var tab1:ButtonStandart;
		
		public var shortcutsTab:ShortcutsTab;		
		public var settingsTab:OptionsSettingsTab;
		
		private var checkBoxes:Array = [];
		private var comboBoxes:Array = [];
		private var sliders:Array = [];
		
		private var binded:Dictionary = new Dictionary();
	//	private var _index:int = -1;
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
			_filterChatMessagesTooltip = new TooltipSender(settingsTab.smartchat_cb);
			
		//	this.addEventListener(MouseEvent.MOUSE_OUT, OnOut);
		//	this.addEventListener(MouseEvent.MOUSE_OVER, OnOver);
			
			checkBoxes.push(settingsTab.windowed_tb, settingsTab.leftClick_tb, settingsTab.useBadwordsFilter, settingsTab.smartchat_cb);
			comboBoxes.push(settingsTab.resolution, settingsTab.graphic);
			sliders.push(settingsTab.sfx_mc, settingsTab.music_mc, settingsTab.voice_mc);
			
		//	closeButton_mc.addEventListener("Up", CloseThis);
			addEventListener(PWComponentValueEvent.CHANGE, ComponentValueChanged);
			
			settingsTab.apply_btn.addEventListener("Up", ApplyPreferences);
			settingsTab.cansel_btn.addEventListener("Up", CanselPreferences);
			//downClose_btn.addEventListener("Up", closeThisFunction);
			
			settingsTab.warning.visible = false;
			settingsTab.apply_btn.EnableButton = false;
			settingsTab.cansel_btn.EnableButton = false;
			
			settingsTab.common.visible = false;
			settingsTab.common_mc.visible = false;
			
			tab1.addEventListener("Up", Tab1Click);
			
			settingsTab.visible = true;
			shortcutsTab.visible = false;
			shortcutsTab.apply_btn.addEventListener("Up", ApplyShortCutClick);
			shortcutsTab.cancel_btn.addEventListener("Up", CancelShortCutClick);
			
			if (Localization.CompleteEventDispatcher != null)
				Localization.CompleteEventDispatcher.addEventListener(Event.COMPLETE, FillLocalization);
			
			try
			{
				CreateAdvancedPage();
			}
			catch (e:Error)
			{
				advancedPanel = null;
				advancedBtn = null;
				pagePanels = null;
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
		
		private function ApplyShortCutClick(e:Event):void 
		{
			ResetAdvanced();
			if (shortcutsTab.shortcutContainer.IsChanged())
			{
				shortcutsTab.shortcutContainer.SendRebinds();
				FSCommands.Send(FSCommands.PREFERENCE_SHORTCUTS_APPLY);
			}
			
			settingsTab.visible = true;
			shortcutsTab.visible = false;
			tab1.visible = true;
			title.text = Localization.PreferenceMenu_Header;
			shortcutsTab.shortcutContainer.ClearFocus();
		}
		
		private function CancelShortCutClick(e:Event):void 
		{
			ResetAdvanced();
			shortcutsTab.shortcutContainer.ResetToDefault();
			
			settingsTab.visible = true;
			shortcutsTab.visible = false;
			tab1.visible = true;
			title.text = Localization.PreferenceMenu_Header;
			shortcutsTab.shortcutContainer.ClearFocus();
		}
		
		private function Tab1Click(e:Event):void 
		{
			if (pageButtons != null)
			{
				for (var i:int = 0; i < pageButtons.length; i++)
					ButtonStandart(pageButtons[i]).visible = false;
			}
			title.text = Localization.ShortcutsMenu_Header;
			settingsTab.visible = false;
			tab1.visible = false;
			shortcutsTab.visible = true;
			shortcutsTab.Init();
			shortcutsTab.shortcutContainer.ClearFocus();
		}
		
		override public function AddedToStage(e:Event):void 
		{
			super.AddedToStage(e);		
			
			settingsTab.AddedToStage();
		}
/*
		private function OnOver(e:MouseEvent):void 
		{
			myBack_mc.onOver();
		}
		
		private function OnOut(e:MouseEvent):void 
		{
			myBack_mc.onOut();
		} */
		
		private function FillLocalization(e:Event):void 
		{
			if (settingsTab.visible)
			{
				title.text = Localization.PreferenceMenu_Header;
			}
			else
			{
				title.text = Localization.ShortcutsMenu_Header;
			}
			//downClose_btn.LocalizeText = Localization.PreferenceMenu_CloseBtn;
			
			tab1.LocalizeText = Localization.ShortcutsMenu_Header;
			
			settingsTab.Localize();
			
			_filterChatMessagesTooltip.TooltipText = Localization.Tooltip_PreferenceMenu_ReceiveRegularChatMessages;
		}
		
		private function ApplyPreferences(e:Event):void 
		{
		//	if (warning.visible)
		//		isApplyPressed = true;
			
	
			FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_APPLY);
		}
		
		private function CanselPreferences(e:Event):void 
		{
			FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_CANCEL);
		}
		
	/*	private function CloseThis(e:Event):void 
		{
			e.stopPropagation();
			dispatchEvent(new WindowVisibleControllerEvent(WindowVisibleControllerEvent.OPEN_CLOSE,_index));
		}
	*/	
		private function ComponentValueChanged(e:PWComponentValueEvent):void 
		{
			FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_SET_VALUE, e.ControlName + " " + e.componentValue);
			
			if (e.ControlName == "smartchat_cb")
			{
				(this.parent as Main).chatBar.chatInput_mc.chatOutput_cb.Value = e.componentValue;
			}
		}
		
		public function AddCheckBoxControl(controlName:String):void 
		{
			for each (var item:PWCheckBox in checkBoxes)
			{
				if (item.name == controlName)
				{
					binded[controlName] = item;
					return;
				}
			}
		}
		
		public function AddSliderControl(controlName:String, maxCount:int):void 
		{
			for each (var item:PWSlider in sliders)
			{
				if (item.name == controlName)
				{
					binded[controlName] = item;
					item.SetSectionsCount(maxCount);
					return
				}
			}
		}
		
		public function AddComboBoxControl(controlName:String, comboboxValues:Array):void 
		{
			for each (var item:PWCombobox in comboBoxes)
			{
				if (item.name == controlName)
				{
					for (var i:int = 0; i < comboboxValues.length; i++) 
					{
						item.AddComboBoxRow(comboboxValues[i]);
					}
					
					binded[controlName] = item;
					return;
				}
			}
		}
		
		public function SetControlValue(controlName:String, value:int, active:Boolean):void 
		{
			if (binded[controlName] == null)
				return;
			
			var item:IPWComponents = binded[controlName];
			
			item.Value = value;
			item.Disable = !active;
						
			if (controlName == "smartchat_cb")
			{
				(this.parent as Main).chatBar.chatInput_mc.chatOutput_cb.Value = value;
			}
		}
		
		public function ActivateAbilityToChange(activate:Boolean):void 
		{
		//	if(!isApplyPressed)
				//warning.visible = activate;
				
			settingsTab.apply_btn.EnableButton = activate;
			settingsTab.cansel_btn.EnableButton = activate;
		}
	
		override protected function BaseBarObjectResize(e:Event = null):void 
		{
			super.BaseBarObjectResize(e);
			this.x = (stage.stageWidth - this.wFrame_mc.width) / 2;
			this.y = (stage.stageHeight - this.wFrame_mc.height) / 2 - 15; 
		}
		/*
		public function set WindowIndex(value:int):void
		{
			_index = value;
		}
		public function get WindowIndex():int
		{
			return _index;
		}
		*/
	/*	public function set CloseButtonTooltip(value:String):void
		{
			closeButton_mc.Tooltip = value;
		}
		*/
		
		private function OnDownCloseClick(e:Event):void 
		{
			e.stopPropagation();			
			
			FSCommands.Send(FSCommands.PREFERENCE_COMPONENT_CANCEL);
			dispatchEvent(new WindowVisibleControllerEvent(WindowVisibleControllerEvent.OPEN_CLOSE,WindowIndex));
		}
		
		override protected function closeThisFunction(e:Event):void
		{
			CancelShortCutClick(e);
			
			super.closeThisFunction(e);
		}
		
		public function OnCloseByEsc():void
		{
			CancelShortCutClick(null);
		}
		
		override public function SetOnStartPosition():void 
		{
			
		}
		
		override public function ShowCinematicDialog():void
		{
			// remains unaffected by dialog
		}
		
		override public function HideCinematicDialog():void
		{
			// remains unaffected by dialog
		}
	}

}