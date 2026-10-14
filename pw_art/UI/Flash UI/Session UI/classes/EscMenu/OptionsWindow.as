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
	import flash.events.KeyboardEvent;
	import flash.events.MouseEvent;
	import flash.display.DisplayObject;
	import flash.display.Shape;
	import flash.display.Sprite;
	import flash.display.Loader;
	import flash.net.URLRequest;
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

		// The window has a column of categories on the left (Graphics, Sound, Game, Keys, Communication, Chat wheel,
		// Ping wheel, Advanced) and shows one category at a time. The rows of the pages "Communication", "Chat wheel",
		// "Ping wheel" and "Advanced" are created at runtime by C++ (PreferencesProcessor.cpp, s_advancedVars).
		// State is STATIC on purpose: the instance layout must stay exactly as in the original class,
		// because the timeline (options window symbol) binds its children to instance slots.
		private static var advancedPanel:Sprite;

		private static var advancedRows:Dictionary;

		private static var advancedNextY:Number = 0;

		// pages created by C++: 0 = Advanced, 1 = Communication, 2 = Chat wheel, 3 = Ping wheel, 4 = Game (under the sound)
		private static const PAGE_COUNT:int = 5;
		private static const PAGE_GAME:int = 4;

		private static var pagePanels:Array;

		private static var pageNextY:Array;

		private static var buildPage:int = 0;

		// categories (buttons of the left column): Game (graphics + sound), Keys, Communication, Chat wheel, Ping wheel, Advanced
		private static const CAT_GAME:int = 0;
		private static const CAT_KEYS:int = 1;
		private static const CAT_COUNT:int = 6;

		// page shown by a category (-1 = not a page)
		private static function CatPage(category:int) : int
		{
			if (category == 0)
			{
				return PAGE_GAME;
			}
			if (category == 2)
			{
				return 1;
			}
			if (category == 3)
			{
				return 2;
			}
			if (category == 4)
			{
				return 3;
			}
			if (category == 5)
			{
				return 0;
			}
			return -1;
		}

		private static function get CAT_NAMES():Array
		{
			return [Localization.PreferenceMenu_GameLabel, Localization.ShortcutsMenu_Header, Localization.Options_CommunicationPage,
				Localization.Options_ChatWheelPage, Localization.Options_PingWheelPage, Localization.Options_AdvancedPage];
		}

		// titles of the sections of the "Game" category
		private static function get SECTION_NAMES():Array
		{
			return [Localization.PreferenceMenu_GraphicLable, Localization.PreferenceMenu_SoundLabel];
		}

		private static var categoryButtons:Array;

		private static var categorySelection:Shape;

		private static var shownCategory:int = 0;

		// controls of the main settings tab by category (graphics, sound, game) and the headers of these categories
		private static var settingsGroups:Array;

		private static var settingsHeaders:Array;

		// key rows (AddAdvancedKey): bind command -> ShortcutItem; last known key of every bind command
		private static var keyRows:Array;

		private static var keyCommands:Array;

		private static var bindTexts:Object;

		private static const SIDEBAR_WIDTH:Number = 165;
		private static const CONTENT_EXTRA_WIDTH:Number = 70; // the rows of the pages need more room than the old window had
		private static const SIDEBAR_TOP:Number = 40;
		private static const SIDEBAR_BUTTON_WIDTH:Number = 141;
		private static const SIDEBAR_BUTTON_STEP:Number = 40;
		private static const CONTENT_TOP:Number = 34;

		private static const LABEL_COLOR:uint = 16765321;
		private static const LABEL_SELECTED_COLOR:uint = 16777215;
		private static const SELECTION_COLOR:uint = 16765502;

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

			SafeCreateCategoryLayout();
		}

		// a separate method: FFDec puts wrong exception offsets into the long constructor (VerifyError #1021)
		private function SafeCreateCategoryLayout() : void
		{
			try
			{
				CreateCategoryLayout();
			}
			catch (e:Error)
			{
				advancedPanel = null;
				pagePanels = null;
				categoryButtons = null;
			}
		}

		// wider window: the column of categories on the left, the content moves to the right
		private function CreateCategoryLayout() : void
		{
			var i:int = 0;
			var panel:Sprite = null;
			var button:ButtonStandart = null;
			var back:Shape = null;
			var scale:Number = 1;
			var frameHeight:Number = this.wFrame_mc.height;

			OptionsWindow.advancedRows = new Dictionary();
			OptionsWindow.keyRows = [];
			OptionsWindow.keyCommands = [];
			OptionsWindow.pagePanels = [];
			OptionsWindow.pageNextY = [];
			OptionsWindow.buildPage = 0;
			OptionsWindow.shownCategory = CAT_GAME;

			this.wFrame_mc.setSize(this.wFrame_mc.width + SIDEBAR_WIDTH + CONTENT_EXTRA_WIDTH, frameHeight);
			if (this.myBack_mc != null)
			{
				this.myBack_mc.width = this.myBack_mc.width + SIDEBAR_WIDTH + CONTENT_EXTRA_WIDTH;
			}
			if (this.closeButton_mc != null)
			{
				this.closeButton_mc.x = this.closeButton_mc.x + SIDEBAR_WIDTH + CONTENT_EXTRA_WIDTH;
			}
			this.title.x = this.title.x + (SIDEBAR_WIDTH + CONTENT_EXTRA_WIDTH) / 2;
			this.settingsTab.x = this.settingsTab.x + SIDEBAR_WIDTH;
			this.shortcutsTab.x = this.shortcutsTab.x + SIDEBAR_WIDTH;
			this.tab1.visible = false;

			// column background
			back = new Shape();
			back.graphics.beginFill(667180, 0.45);
			back.graphics.drawRoundRect(10, SIDEBAR_TOP - 8, SIDEBAR_WIDTH - 14, frameHeight - SIDEBAR_TOP - 10, 10, 10);
			back.graphics.endFill();
			// the game's Graphics has no moveTo/lineTo: lines are thin rectangles
			back.graphics.beginFill(14266474, 0.35);
			back.graphics.drawRect(SIDEBAR_WIDTH + 2, SIDEBAR_TOP - 8, 1, frameHeight - SIDEBAR_TOP - 10);
			back.graphics.endFill();
			addChild(back);

			OptionsWindow.categorySelection = new Shape();
			addChild(OptionsWindow.categorySelection);

			OptionsWindow.categoryButtons = [];
			i = 0;
			while (i < OptionsWindow.CAT_COUNT)
			{
				// ButtonStandart is a base class without graphics: create the library button symbol (same as the "Keys" button)
				button = new myButtonStandart();
				scale = 1;
				if (button.width > 0)
				{
					scale = SIDEBAR_BUTTON_WIDTH / button.width;
				}
				button.scaleX = scale;
				button.scaleY = scale;
				button.x = Math.round((SIDEBAR_WIDTH + 6 - SIDEBAR_BUTTON_WIDTH) / 2);
				button.y = Math.round(SIDEBAR_TOP + i * SIDEBAR_BUTTON_STEP);
				button.addEventListener("Up", this.CategoryButtonClick);
				addChild(button);
				OptionsWindow.categoryButtons.push(button);
				i++;
			}

			// pages of the C++ rows: same place as the content of the main tab
			i = 0;
			while (i < PAGE_COUNT)
			{
				panel = new Sprite();
				panel.x = this.settingsTab.x + 2;
				panel.y = CONTENT_TOP;
				panel.visible = false;
				addChild(panel);
				OptionsWindow.pagePanels.push(panel);
				OptionsWindow.pageNextY.push(0);
				i++;
			}
			OptionsWindow.advancedPanel = OptionsWindow.pagePanels[0];
			OptionsWindow.advancedNextY = 0;

			this.SplitSettingsTab();

			if (Localization.CompleteEventDispatcher != null)
			{
				Localization.CompleteEventDispatcher.addEventListener(Event.COMPLETE, this.OnLocalizationComplete);
			}
			this.UpdateCategoryTexts();
			this.ShowCategory(CAT_GAME);
		}

		// the main tab is the category "Game": the graphics section stays at the top, the sound section goes under it
		// (two columns), the page of the C++ rows PAGE_GAME (signal sound volume) under the sound; the checkboxes of
		// the old "Game" section are moved to other pages by C++ (AddAdvancedSlot)
		private function SplitSettingsTab() : void
		{
			var i:int = 0;
			var header:ShortcutsHeader = null;
			var tab:OptionsSettingsTab = this.settingsTab;

			// background with the section bands (the first child without a name) and the section titles on the bands
			if (tab.numChildren > 0)
			{
				var bandBack:DisplayObject = tab.getChildAt(0);
				if (bandBack is Shape)
				{
					bandBack.visible = false;
				}
			}
			tab.video.visible = false;
			tab.sound.visible = false;
			tab.game.visible = false;

			OptionsWindow.settingsHeaders = [];
			i = 0;
			while (i < 2)
			{
				header = new ShortcutsHeader();
				header.mouseEnabled = false;
				header.mouseChildren = false;
				header.x = 2;
				header.y = CONTENT_TOP;
				if (i == 1)
				{
					header.y = SOUND_TOP;
				}
				tab.addChild(header);
				OptionsWindow.settingsHeaders.push(header);
				i++;
			}

			// graphics: the controls stay where they are (the section was at the top)
			var graphicsGroup:Array = [tab.resolutionText, tab.graphicText, tab.resolution, tab.graphic, tab.windowed_tb, tab.windowMode, tab.apply_btn, tab.cansel_btn];

			// sound: music | voice, effects under the music
			var soundGroup:Array = [tab.music, tab.music_mc, tab.voice, tab.voice_mc, tab.sfx, tab.sfx_mc];
			this.PlaceAt(tab.music, tab.music_mc, 29, SOUND_TOP + 40);
			this.PlaceAt(tab.voice, tab.voice_mc, 264, SOUND_TOP + 40);
			this.PlaceAt(tab.sfx, tab.sfx_mc, 29, SOUND_TOP + 100);

			OptionsWindow.settingsGroups = [graphicsGroup, soundGroup];
			tab.pingVolumeText.visible = false; // until C++ adds the signal volume (AddSoundSectionSlider)
			// the old "Game" section: shown only where C++ puts its checkboxes (AddAdvancedSlot)
			tab.leftClick_tb.visible = false;
			tab.useBadwordsFilter.visible = false;
			tab.smartchat_cb.visible = false;
			tab.leftClick.visible = false;
			tab.useBadwordsFilterText.visible = false;
			tab.smartchat_cbText.visible = false;
			Sprite(OptionsWindow.pagePanels[PAGE_GAME]).y = SOUND_TOP + 150;
		}

		private static const SOUND_TOP:Number = 236;

		// label above the slider, as in the original layout
		private function PlaceAt(label:TextField, slider:DisplayObject, x:Number, y:Number) : void
		{
			var dx:Number = slider.x - label.x;
			var dy:Number = slider.y - label.y;
			label.x = x;
			label.y = y;
			slider.x = x + dx;
			slider.y = y + dy;
		}

		// the checkbox captions become children of the checkboxes (OptionsSettingsTab.AddedToStage): those move with them
		private function MoveBy(item:DisplayObject, dy:Number) : void
		{
			if (item != null && item.parent == this.settingsTab)
			{
				item.y = item.y + dy;
			}
		}

		private function UpdateCategoryTexts() : void
		{
			var i:int = 0;
			var names:Array = OptionsWindow.CAT_NAMES;
			var color:uint = 0;
			var header:ShortcutsHeader = null;
			if (OptionsWindow.categoryButtons == null)
			{
				return;
			}
			i = 0;
			while (i < OptionsWindow.categoryButtons.length)
			{
				color = LABEL_COLOR;
				if (i == OptionsWindow.shownCategory)
				{
					color = LABEL_SELECTED_COLOR;
				}
				this.SetButtonText(ButtonStandart(OptionsWindow.categoryButtons[i]), String(names[i]), color);
				i++;
			}
			if (OptionsWindow.settingsHeaders != null)
			{
				i = 0;
				while (i < OptionsWindow.settingsHeaders.length)
				{
					header = OptionsWindow.settingsHeaders[i];
					if (header.title != null)
					{
						header.title.text = String(OptionsWindow.SECTION_NAMES[i]);
					}
					i++;
				}
			}
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

		// Окно создаётся до того, как движок подставит переводы в Localization (Combat.xdb):
		// без этого при первом открытии подписи были на языке по умолчанию.
		private function OnLocalizationComplete(e:Event) : void
		{
			this.UpdateCategoryTexts();
			this.UpdateSlotCaptions();

			this.title.text = Localization.PreferenceMenu_Header;
		}

		private function SetButtonText(button:ButtonStandart, text:String, color:uint) : void
		{
			try
			{
				button["componentInspectorSetting"] = true;
			}
			catch(e:Error)
			{
			}
			button.labelColor = color;
			button.labelText = text;
			try
			{
				button["componentInspectorSetting"] = false;
			}
			catch(e:Error)
			{
			}
		}

		private function CategoryButtonClick(param1:Event) : void
		{
			try
			{
				this.ShowCategoryOf(param1);
			}
			catch(e:Error)
			{
				// an exception here must not reach the native event dispatcher (it crashes the game)
			}
		}

		private function ShowCategoryOf(param1:Event) : void
		{
			var index:int = OptionsWindow.categoryButtons.indexOf(param1.currentTarget);
			if(index < 0)
			{
				index = OptionsWindow.categoryButtons.indexOf(param1.target);
			}
			if(index >= 0)
			{
				this.ShowCategory(index);
			}
		}

		private function ShowCategory(param1:int) : void
		{
			var category:int = param1;
			var i:int = 0;
			var j:int = 0;
			var group:Array = null;
			var visible:Boolean = false;
			var page:int = -1;
			var button:ButtonStandart = null;
			if (OptionsWindow.categoryButtons == null)
			{
				return;
			}
			if (category < 0 || category >= OptionsWindow.categoryButtons.length)
			{
				category = CAT_GAME;
			}
			OptionsWindow.shownCategory = category;
			page = OptionsWindow.CatPage(category);

			if (category != CAT_KEYS)
			{
				this.shortcutsTab.shortcutContainer.ClearFocus();
			}
			this.ClearKeyFocus();

			visible = false;
			if (category == CAT_GAME)
			{
				visible = true;
			}
			this.settingsTab.visible = visible;
			i = 0;
			while (i < OptionsWindow.settingsGroups.length)
			{
				group = OptionsWindow.settingsGroups[i];
				j = 0;
				while (j < group.length)
				{
					DisplayObject(group[j]).visible = visible;
					j++;
				}
				ShortcutsHeader(OptionsWindow.settingsHeaders[i]).visible = visible;
				i++;
			}

			visible = false;
			if (category == CAT_KEYS)
			{
				visible = true;
			}
			this.shortcutsTab.visible = visible;
			if (category == CAT_KEYS)
			{
				this.shortcutsTab.Init();
				this.shortcutsTab.shortcutContainer.ClearFocus();
			}

			i = 0;
			while (i < OptionsWindow.pagePanels.length)
			{
				visible = false;
				if (i == page)
				{
					visible = true;
				}
				Sprite(OptionsWindow.pagePanels[i]).visible = visible;
				i++;
			}

			this.tab1.visible = false;
			this.title.text = Localization.PreferenceMenu_Header;
			this.UpdateCategoryTexts();
			this.PlaceWheelEditors();

			button = OptionsWindow.categoryButtons[category];
			OptionsWindow.categorySelection.graphics.clear();
			// a gold plate under the selected button (the plate is below the buttons)
			OptionsWindow.categorySelection.graphics.beginFill(SELECTION_COLOR, 0.9);
			OptionsWindow.categorySelection.graphics.drawRoundRect(button.x - 3, button.y - 3, button.width + 6, button.height + 6, 12, 12);
			OptionsWindow.categorySelection.graphics.endFill();
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

		// slider: | label ............ | slider (140) | value (50) |
		// checkbox: | [x] label .......... |
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
			if(param4)
			{
				// the checkbox before its caption
				param3.x = _loc5_.text.x;
				_loc5_.text.x = _loc5_.text.x + param3.width + 6;
				_loc5_.value.visible = false;
			}
			else
			{
				param3.x = _loc7_;
				_loc5_.value.width = OptionsWindow.ADV_VALUE_WIDTH;
				_loc5_.value.x = _loc6_ - OptionsWindow.ADV_VALUE_WIDTH;
			}
			param3.y = _loc5_.y + _loc5_.valueFrame.y + (_loc5_.valueFrame.height - param3.height) / 2;
			OptionsWindow.advancedPanel.addChild(param3);
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
			if (param1 == PING_VOLUME_ID)
			{
				this.AddSoundSectionSlider(_loc4_, param2, param3);
				return;
			}
			_loc4_.SetSize(OptionsWindow.ADV_SLIDER_WIDTH,0);
			_loc4_.SetSectionsCount(param3);
			this.AddAdvancedRow(param1,param2,_loc4_,false);
			this.binded[param1] = _loc4_;
		}

		// signal volume (C++ row "adv_ping_sound_volume"): in the sound section of the "Game" category, right of the effects volume
		// and under the voice volume; the caption is pingVolumeText (a copy of the effects volume caption in main.fla)
		private static const PING_VOLUME_ID:String = "adv_ping_sound_volume";

		private static var pingVolumeCaption:String = "";

		private function AddSoundSectionSlider(slider:PWSlider, caption:String, steps:int) : void
		{
			var tab:OptionsSettingsTab = this.settingsTab;
			slider.SetSectionsCount(steps);
			slider.x = tab.voice_mc.x;
			slider.y = tab.sfx_mc.y;
			tab.addChild(slider);
			tab.pingVolumeText.x = tab.voice.x;
			tab.pingVolumeText.y = tab.sfx.y;
			tab.pingVolumeText.text = caption;
			tab.pingVolumeText.visible = true;
			OptionsWindow.pingVolumeCaption = caption;
			this.binded[PING_VOLUME_ID] = slider;
		}

		// a control of the main settings tab (still driven by the data-defined preferences) is moved to this row
		public function AddAdvancedSlot(param1:String, param2:String) : void
		{
			if (OptionsWindow.advancedPanel == null)
			{
				return;
			}
			var control:DisplayObject = this.settingsTab[param2] as DisplayObject;
			if (control == null)
			{
				return;
			}
			// the caption becomes a child of the checkbox in OptionsSettingsTab.AddedToStage; if that has not happened yet
			// the caption is moved too
			var caption:DisplayObject = null;
			if (param2 == "leftClick_tb")
			{
				caption = this.settingsTab.leftClick;
			}
			else if (param2 == "useBadwordsFilter")
			{
				caption = this.settingsTab.useBadwordsFilterText;
			}
			else if (param2 == "smartchat_cb")
			{
				caption = this.settingsTab.smartchat_cbText;
			}
			// the row looks like the other rows of the page: the caption becomes a label of the row
			var text:String = OptionsWindow.SlotCaption(param2);
			if (caption != null && caption.parent != null)
			{
				caption.parent.removeChild(caption);
			}
			control.visible = true;
			this.AddAdvancedRow(param1, text, control, true);
		}

		// captions of the controls moved by AddAdvancedSlot (the same texts as in OptionsSettingsTab.Localize)
		private static function SlotCaption(controlName:String) : String
		{
			if (controlName == "leftClick_tb")
			{
				return Localization.PreferenceMenu_LeftClickLabel;
			}
			if (controlName == "useBadwordsFilter")
			{
				return Localization.PreferenceMenu_UseBadWordsFilter;
			}
			if (controlName == "smartchat_cb")
			{
				return Localization.PreferenceMenu_ReceiveRegularChatMessages;
			}
			return "";
		}

		private static const SLOT_ROWS:Array = ["adv_slot_left_click", "leftClick_tb", "adv_slot_bad_words", "useBadwordsFilter", "adv_slot_chat_off", "smartchat_cb"];

		private function UpdateSlotCaptions() : void
		{
			var i:int = 0;
			var row:ShortcutItem = null;
			if (OptionsWindow.advancedRows == null)
			{
				return;
			}
			while (i < SLOT_ROWS.length)
			{
				row = OptionsWindow.advancedRows[SLOT_ROWS[i]] as ShortcutItem;
				if (row != null)
				{
					row.SetName(OptionsWindow.SlotCaption(String(SLOT_ROWS[i + 1])));
				}
				i += 2;
			}
		}

		// small text under the previous row, aligned with its caption
		public function AddAdvancedNote(param1:String) : void
		{
			if (OptionsWindow.advancedPanel == null)
			{
				return;
			}
			var note:ShortcutItem = new ShortcutItem();
			note.SetName(param1);
			note.SetBind("");
			note.valueFrame.visible = false;
			note.value.visible = false;
			note.mouseEnabled = false;
			note.mouseChildren = false;
			note.text.textColor = 10473664;
			note.x = 26;
			note.y = OptionsWindow.advancedNextY - 8;
			OptionsWindow.advancedPanel.addChild(note);
			OptionsWindow.advancedNextY = note.y + 22;
		}

		// key of a bind command: click the key field and press a new key, it is rebound and saved at once
		public function AddAdvancedKey(param1:String, param2:String, param3:String) : void
		{
			if(OptionsWindow.advancedPanel == null)
			{
				return;
			}
			var item:ShortcutItem = new ShortcutItem();
			var key:String = "";
			item.name = param1;
			item.SetName(param2);
			if (OptionsWindow.bindTexts != null && OptionsWindow.bindTexts[param3] != null)
			{
				key = String(OptionsWindow.bindTexts[param3]);
			}
			item.SetBind(key);
			item.x = 0;
			item.y = OptionsWindow.advancedNextY;
			OptionsWindow.advancedPanel.addChild(item);
			// after ShortcutItem's own handler: it has set the new key by then
			item.value.addEventListener(KeyboardEvent.KEY_UP, this.OnKeyRowKeyUp);
			if (OptionsWindow.keyRows.length == 0)
			{
				addEventListener(Event.ENTER_FRAME, this.OnKeyRowsFrame);
			}
			OptionsWindow.keyRows.push(item);
			OptionsWindow.keyCommands.push(param3);
			OptionsWindow.advancedRows[param1] = item;
			OptionsWindow.advancedNextY = item.y + item.height + 3;
		}

		// a changed key row is applied at once (checked every frame: the key event of the row does not always reach here)
		private function OnKeyRowsFrame(e:Event) : void
		{
			var i:int = 0;
			if (!this.visible || OptionsWindow.keyRows == null)
			{
				return;
			}
			try
			{
				while (i < OptionsWindow.keyRows.length)
				{
					if (ShortcutItem(OptionsWindow.keyRows[i]).IsChanged())
					{
						this.ApplyKeyRow(ShortcutItem(OptionsWindow.keyRows[i]).value);
					}
					i++;
				}
			}
			catch (err:Error)
			{
			}
		}

		private function OnKeyRowKeyUp(e:KeyboardEvent) : void
		{
			try
			{
				this.ApplyKeyRow(e.currentTarget);
			}
			catch (err:Error)
			{
			}
		}

		private function ApplyKeyRow(target:Object) : void
		{
			var index:int = -1;
			var i:int = 0;
			var item:ShortcutItem = null;
			var command:String = null;
			while (i < OptionsWindow.keyRows.length)
			{
				if (ShortcutItem(OptionsWindow.keyRows[i]).value == target)
				{
					index = i;
				}
				i++;
			}
			if (index < 0)
			{
				return;
			}
			item = OptionsWindow.keyRows[index];
			if (!item.IsChanged())
			{
				return;
			}
			command = String(OptionsWindow.keyCommands[index]);
			// smart chat commands are bound without a bind section, the others in adventure_screen (ShortcutsContainer.SendRebinds)
			var isSmartChat:Boolean = command.search("cmd_smart_chat") != -1;
			if (!isSmartChat)
			{
				FSCommands.Send(FSCommands.PREFERENCE_SHORTCUT_REBIND, "bindsection adventure_screen");
			}
			FSCommands.Send(FSCommands.PREFERENCE_SHORTCUT_REBIND, "rebind " + command + " " + item.GetRebindString());
			if (command == "voice_overlay_unlock_on")
			{
				FSCommands.Send(FSCommands.PREFERENCE_SHORTCUT_REBIND, "rebind !voice_overlay_unlock_off " + item.GetRebindString());
			}
			if (!isSmartChat)
			{
				FSCommands.Send(FSCommands.PREFERENCE_SHORTCUT_REBIND, "bindsection");
			}
			FSCommands.Send(FSCommands.PREFERENCE_SHORTCUTS_APPLY);
			if (OptionsWindow.bindTexts == null)
			{
				OptionsWindow.bindTexts = new Object();
			}
			OptionsWindow.bindTexts[command] = item.value.text;
			item.SetBind(item.value.text);
		}

		private function ClearKeyFocus() : void
		{
			var i:int = 0;
			if (OptionsWindow.keyRows == null)
			{
				return;
			}
			while (i < OptionsWindow.keyRows.length)
			{
				ShortcutItem(OptionsWindow.keyRows[i]).ClearFocus();
				i++;
			}
		}

		// every bind string the game sends (MainInterface.SetBindString), e.g. "bind cmd_smart_chat 'V'"
		public static function OnBindString(bindString:String) : void
		{
			var index:int = -1;
			var second:int = -1;
			var command:String = null;
			var rest:String = null;
			var q1:int = -1;
			var q2:int = -1;
			var q3:int = -1;
			var q4:int = -1;
			var key:String = null;
			var first:int = bindString.indexOf(" ");
			if (first == -1)
			{
				return;
			}
			second = bindString.indexOf(" ", first + 1);
			if (second == -1)
			{
				return;
			}
			command = bindString.slice(first + 1, second);
			rest = bindString.slice(second + 1);
			q1 = rest.indexOf("'");
			if (q1 == -1)
			{
				return;
			}
			q2 = rest.indexOf("'", q1 + 1);
			if (q2 == -1)
			{
				return;
			}
			key = rest.slice(q1 + 1, q2);
			rest = rest.slice(q2 + 1);
			if (rest.indexOf("+") != -1)
			{
				q3 = rest.indexOf("'");
				q4 = rest.indexOf("'", q3 + 1);
				if (q3 != -1 && q4 != -1)
				{
					key = key + "+" + rest.slice(q3 + 1, q4);
				}
			}
			if (OptionsWindow.bindTexts == null)
			{
				OptionsWindow.bindTexts = new Object();
			}
			OptionsWindow.bindTexts[command] = key;
			if (OptionsWindow.keyCommands == null)
			{
				return;
			}
			index = OptionsWindow.keyCommands.indexOf(command);
			if (index >= 0)
			{
				ShortcutItem(OptionsWindow.keyRows[index]).SetBind(key);
			}
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

		// ---------------------------------------------------------------- wheel editors ("Chat wheel" / "Ping wheel" pages)
		// C++ (AdventureScreen::SendWheelEditors) -> MainInterface.SetWheelEditor -> here. A slot gets a command by dragging
		// it from the list of commands onto a sector of the wheel preview or onto the list of the slots (or: click a slot,
		// then click a command). Changes go to C++ at once: PreferenceSetVar "<varName> a,b,c".
		private static var wheelEditors:Array = [null, null];

		private static var dragEditor:Object;

		private static var dragSource:Object;     // {catalog: id} or {slot: index}

		private static var dragProxy:ShortcutItem;

		private static var dragStartX:Number = 0;

		private static var dragStartY:Number = 0;

		private static var dragMoved:Boolean = false;

		private static const EDITOR_PAGES:Array = [2, 3];
		private static const EDITOR_PREVIEW_SIZE:Number = 160;
		private static const EDITOR_PREVIEW_X:Number = 90;     // center
		private static const EDITOR_PREVIEW_Y:Number = 132;
		private static const EDITOR_SLOTS_X:Number = 200;
		private static const EDITOR_SLOTS_Y:Number = 52;
		private static const EDITOR_SLOT_STEP:Number = 21;
		private static const EDITOR_CATALOG_Y:Number = 254;
		private static const EDITOR_CATALOG_STEP:Number = 20;
		private static const EDITOR_CATALOG_COLUMN:Number = 176;
		private static const EDITOR_CATALOG_COLUMNS:int = 3;
		private static const EDITOR_WHEEL_MIN:int = 4;
		private static const EDITOR_WHEEL_MAX:int = 8;
		private static const EDITOR_USED_ALPHA:Number = 0.45;

		public function SetWheelEditor(wheel:int, varName:String, ids:String, labels:String, icons:String, items:String, texts:String) : void
		{
			var ed:Object = null;
			var i:int = 0;
			var panel:Sprite = null;
			if (wheel < 0 || wheel > 1 || OptionsWindow.pagePanels == null)
			{
				return;
			}
			panel = OptionsWindow.pagePanels[EDITOR_PAGES[wheel]];
			ed = OptionsWindow.wheelEditors[wheel];
			if (ed != null && ed.sprite != null && ed.sprite.parent != null)
			{
				ed.sprite.parent.removeChild(ed.sprite);
			}
			ed = new Object();
			ed.wheel = wheel;
			ed.varName = varName;
			ed.ids = [];
			var idList:Array = ids.split("|");
			i = 0;
			while (i < idList.length)
			{
				ed.ids.push(int(idList[i]));
				i++;
			}
			ed.labels = labels.split("|");
			ed.icons = [];
			if (icons.length > 0)
			{
				ed.icons = icons.split("|");
			}
			ed.items = [];
			var itemList:Array = items.split(",");
			i = 0;
			while (i < itemList.length)
			{
				if (String(itemList[i]).length > 0)
				{
					ed.items.push(int(itemList[i]));
				}
				i++;
			}
			ed.texts = texts.split("|");
			ed.selected = 0;
			ed.hover = -1;
			OptionsWindow.wheelEditors[wheel] = ed;
			this.BuildWheelEditor(ed, panel);
			this.RefreshWheelEditor(ed);
		}

		private function EditorText(ed:Object, index:int) : String
		{
			if (index < ed.texts.length)
			{
				return String(ed.texts[index]);
			}
			return "";
		}

		private function CatalogIndex(ed:Object, id:int) : int
		{
			return ed.ids.indexOf(id);
		}

		private function MakeEditorLabel(text:String, color:uint) : ShortcutItem
		{
			var item:ShortcutItem = new ShortcutItem();
			item.SetName(text);
			item.SetBind("");
			item.valueFrame.visible = false;
			item.value.visible = false;
			item.text.textColor = color;
			item.mouseChildren = false;
			return item;
		}

		// small square button in the colors of the window (the game's Graphics: only filled rectangles)
		private function MakeEditorButton(text:String) : Sprite
		{
			var button:Sprite = new Sprite();
			// drawing works on a Shape (the game's Flash player does not draw Sprite.graphics)
			var back:Shape = new Shape();
			back.graphics.beginFill(14266474, 1);
			back.graphics.drawRoundRect(0, 0, 30, 22, 6, 6);
			back.graphics.endFill();
			back.graphics.beginFill(2059124, 1);
			back.graphics.drawRoundRect(1, 1, 28, 20, 5, 5);
			back.graphics.endFill();
			button.addChild(back);
			var label:ShortcutItem = this.MakeEditorLabel(text, 16777215);
			label.mouseEnabled = false;
			label.x = Math.floor(15 - label.text.x - label.text.textWidth / 2);
			label.y = Math.floor(11 - label.text.y - label.text.textHeight / 2);
			button.addChild(label);
			button.mouseChildren = false;
			return button;
		}

		private function LoadEditorImage(path:String) : Loader
		{
			var ld:Loader = new Loader();
			ld.mouseEnabled = false;
			ld.load(new URLRequest(path));
			return ld;
		}

		private function BuildWheelEditor(ed:Object, panel:Sprite) : void
		{
			var i:int = 0;
			var n:int = 0;
			var ld:Loader = null;
			var hls:Array = null;
			var offsets:Array = null;
			var row:ShortcutItem = null;
			var header:ShortcutsHeader = null;
			var isPing:Boolean = ed.wheel == 1;
			var texSize:Number = 512;
			var texPath:String = "/UI/Styles/Icons/ChatWheel/chat_wheel_";
			var allOffsets:Array = MainInterface.CHAT_HL_OFFSETS;
			if (isPing)
			{
				texSize = 256;
				texPath = "/UI/Styles/Icons/PingWheel/ping_wheel_";
				allOffsets = MainInterface.PING_HL_OFFSETS;
			}
			ed.texScale = EDITOR_PREVIEW_SIZE / texSize;
			// texture radii: wheel / center circle (gen_chatwheel.py, gen_pingwheel.py)
			ed.rOut = 244;
			ed.rIn = 58;
			ed.rLabel = 152;
			if (isPing)
			{
				ed.rOut = 124;
				ed.rIn = 42;
				ed.rLabel = 85;
			}

			var sprite:Sprite = new Sprite();
			panel.addChild(sprite);
			ed.sprite = sprite;

			header = new ShortcutsHeader();
			header.mouseEnabled = false;
			header.mouseChildren = false;
			if (header.title != null)
			{
				header.title.text = this.EditorText(ed, 0);
			}
			sprite.addChild(header);

			var hint:ShortcutItem = this.MakeEditorLabel(this.EditorText(ed, 1), 10473664);
			hint.mouseEnabled = false;
			hint.y = 30;
			sprite.addChild(hint);

			// wheel preview: all textures exist, the right ones are shown
			var preview:Sprite = new Sprite();
			preview.x = EDITOR_PREVIEW_X;
			preview.y = EDITOR_PREVIEW_Y;
			sprite.addChild(preview);
			ed.preview = preview;
			var gfx:Sprite = new Sprite();
			gfx.mouseEnabled = false;
			gfx.mouseChildren = false;
			gfx.scaleX = ed.texScale;
			gfx.scaleY = ed.texScale;
			preview.addChild(gfx);
			ed.bgs = [];
			ed.hls = [];
			n = 0;
			while (n <= EDITOR_WHEEL_MAX)
			{
				ed.bgs.push(null);
				ed.hls.push(null);
				if (n >= EDITOR_WHEEL_MIN)
				{
					ld = this.LoadEditorImage(texPath + "bg_" + n + ".dds");
					ld.x = -texSize / 2;
					ld.y = -texSize / 2;
					ld.visible = false;
					gfx.addChild(ld);
					ed.bgs[n] = ld;
					offsets = allOffsets[n];
					hls = [];
					i = 0;
					while (i < n)
					{
						ld = this.LoadEditorImage(texPath + "hl_" + n + "_" + i + ".dds");
						ld.x = int(offsets[i * 2]);
						ld.y = int(offsets[i * 2 + 1]);
						ld.visible = false;
						gfx.addChild(ld);
						hls.push(ld);
						i++;
					}
					ed.hls[n] = hls;
				}
				n++;
			}
			// slot marks on the preview: number (chat) or the icon of the ping type (ping)
			ed.previewMarks = [];
			i = 0;
			while (i < EDITOR_WHEEL_MAX)
			{
				var mark:Sprite = new Sprite();
				mark.mouseEnabled = false;
				mark.mouseChildren = false;
				preview.addChild(mark);
				ed.previewMarks.push(mark);
				i++;
			}
			ed.previewIcons = [];
			if (isPing)
			{
				i = 0;
				while (i < ed.icons.length)
				{
					ld = this.LoadEditorImage("/UI/Styles/Icons/PingWheel/" + String(ed.icons[i]));
					ld.scaleX = 0.5;
					ld.scaleY = 0.5;
					ld.visible = false;
					preview.addChild(ld);
					ed.previewIcons.push(ld);
					i++;
				}
			}
			// hit area of the preview (its pictures do not take the mouse): sectors can be dragged onto each other
			var hit:Shape = new Shape();
			hit.graphics.beginFill(0, 0.01);
			hit.graphics.drawRoundRect(-EDITOR_PREVIEW_SIZE / 2, -EDITOR_PREVIEW_SIZE / 2, EDITOR_PREVIEW_SIZE, EDITOR_PREVIEW_SIZE, EDITOR_PREVIEW_SIZE, EDITOR_PREVIEW_SIZE);
			hit.graphics.endFill();
			preview.addChildAt(hit, 0);
			preview.addEventListener(MouseEvent.MOUSE_DOWN, this.OnEditorPreviewDown);
			// slot numbers of the preview: created once (new labels on every change reached the engine sprite limit)
			ed.previewNumbers = [];
			i = 0;
			while (i < EDITOR_WHEEL_MAX)
			{
				var number:ShortcutItem = this.MakeEditorLabel(String(i + 1), 16777215);
				number.mouseEnabled = false;
				number.visible = false;
				preview.addChild(number);
				ed.previewNumbers.push(number);
				i++;
			}

			// slots: "1  name"
			ed.slotRows = [];
			i = 0;
			while (i < EDITOR_WHEEL_MAX)
			{
				row = new ShortcutItem();
				row.SetName(String(i + 1));
				row.SetBind("");
				row.mouseChildren = false;
				row.text.x = 0;
				row.valueFrame.x = 22;
				row.valueFrame.width = 196;
				row.value.x = 26;
				row.value.width = 188;
				row.x = EDITOR_SLOTS_X;
				row.y = EDITOR_SLOTS_Y + i * EDITOR_SLOT_STEP;
				row.addEventListener(MouseEvent.MOUSE_DOWN, this.OnEditorSlotDown);
				sprite.addChild(row);
				ed.slotRows.push(row);
				i++;
			}

			// slot count: [-] Slots: N [+]
			var minus:Sprite = this.MakeEditorButton("-");
			var plus:Sprite = this.MakeEditorButton("+");
			minus.x = EDITOR_SLOTS_X + 22;
			minus.y = EDITOR_SLOTS_Y + EDITOR_WHEEL_MAX * EDITOR_SLOT_STEP + 6;
			plus.x = EDITOR_SLOTS_X + 22 + 196 - 30;
			plus.y = minus.y;
			minus.addEventListener(MouseEvent.CLICK, this.OnEditorMinus);
			plus.addEventListener(MouseEvent.CLICK, this.OnEditorPlus);
			sprite.addChild(minus);
			sprite.addChild(plus);
			ed.minus = minus;
			ed.plus = plus;
			var count:ShortcutItem = this.MakeEditorLabel("", LABEL_COLOR);
			count.mouseEnabled = false;
			count.y = minus.y + 2;
			sprite.addChild(count);
			ed.countLabel = count;

			// commands
			ed.catalogRows = [];
			i = 0;
			while (i < ed.ids.length)
			{
				row = this.MakeEditorLabel(String(ed.labels[i]), 16765321);
				row.x = (i % EDITOR_CATALOG_COLUMNS) * EDITOR_CATALOG_COLUMN;
				row.y = EDITOR_CATALOG_Y + int(i / EDITOR_CATALOG_COLUMNS) * EDITOR_CATALOG_STEP;
				if (isPing && i < ed.icons.length)
				{
					ld = this.LoadEditorImage("/UI/Styles/Icons/PingWheel/" + String(ed.icons[i]));
					ld.scaleX = 0.3;
					ld.scaleY = 0.3;
					ld.x = row.text.x;
					ld.y = 0;
					row.addChild(ld);
					row.text.x = row.text.x + 22;
				}
				row.addEventListener(MouseEvent.MOUSE_DOWN, this.OnEditorCatalogDown);
				sprite.addChild(row);
				ed.catalogRows.push(row);
				i++;
			}
		}

		// everything that depends on the slots
		private function RefreshWheelEditor(ed:Object) : void
		{
			var i:int = 0;
			var n:int = ed.items.length;
			var shown:Boolean = false;
			var row:ShortcutItem = null;
			var ci:int = 0;
			var angle:Number = NaN;
			var radius:Number = NaN;
			var mark:Sprite = null;
			var label:ShortcutItem = null;
			var ld:Loader = null;
			if (ed.selected >= n)
			{
				ed.selected = n - 1;
			}
			i = EDITOR_WHEEL_MIN;
			while (i <= EDITOR_WHEEL_MAX)
			{
				Loader(ed.bgs[i]).visible = i == n;
				i++;
			}
			this.UpdateEditorHighlight(ed);
			// preview marks
			i = 0;
			while (i < ed.previewIcons.length)
			{
				Loader(ed.previewIcons[i]).visible = false;
				i++;
			}
			i = 0;
			while (i < EDITOR_WHEEL_MAX)
			{
				label = ed.previewNumbers[i];
				label.visible = false;
				if (i < n)
				{
					angle = (-90 + i * 360 / n) * Math.PI / 180;
					radius = ed.rLabel * ed.texScale;
					ci = this.CatalogIndex(ed, int(ed.items[i]));
					if (ed.wheel == 1 && ci >= 0 && ci < ed.previewIcons.length)
					{
						ld = ed.previewIcons[ci];
						ld.x = Math.floor(Math.cos(angle) * radius - 16);
						ld.y = Math.floor(Math.sin(angle) * radius - 16);
						ld.visible = true;
					}
					else
					{
						label.x = Math.floor(Math.cos(angle) * radius - label.text.x - label.text.textWidth / 2);
						label.y = Math.floor(Math.sin(angle) * radius - label.text.y - 8);
						label.visible = true;
					}
				}
				i++;
			}
			// slot rows
			i = 0;
			while (i < EDITOR_WHEEL_MAX)
			{
				row = ed.slotRows[i];
				shown = i < n;
				row.visible = shown;
				if (shown)
				{
					ci = this.CatalogIndex(ed, int(ed.items[i]));
					if (ci >= 0)
					{
						row.value.text = String(ed.labels[ci]);
					}
					else
					{
						row.value.text = "";
					}
					row.text.textColor = 16765321;
					if (i == ed.selected)
					{
						row.text.textColor = 16777215;
					}
				}
				i++;
			}
			ed.countLabel.SetName(this.EditorText(ed, 2) + ": " + n);
			ed.countLabel.x = EDITOR_SLOTS_X + 22 + 98 - ed.countLabel.text.x - ed.countLabel.text.textWidth / 2;
			ed.minus.alpha = 1;
			if (n <= EDITOR_WHEEL_MIN)
			{
				ed.minus.alpha = 0.4;
			}
			ed.plus.alpha = 1;
			if (n >= EDITOR_WHEEL_MAX)
			{
				ed.plus.alpha = 0.4;
			}
		}

		private function UpdateEditorHighlight(ed:Object) : void
		{
			var n:int = 0;
			var i:int = 0;
			var target:int = ed.hover;
			if (target < 0)
			{
				target = ed.selected;
			}
			n = EDITOR_WHEEL_MIN;
			while (n <= EDITOR_WHEEL_MAX)
			{
				i = 0;
				while (i < ed.hls[n].length)
				{
					Loader(ed.hls[n][i]).visible = n == ed.items.length && i == target;
					i++;
				}
				n++;
			}
		}

		private function SaveWheelEditor(ed:Object) : void
		{
			FSCommands.Send("PreferenceSetVar", String(ed.varName) + " " + ed.items.join(","));
		}

		private function FindEditor(target:Object) : Object
		{
			var w:int = 0;
			var ed:Object = null;
			var obj:DisplayObject = target as DisplayObject;
			while (obj != null)
			{
				w = 0;
				while (w < OptionsWindow.wheelEditors.length)
				{
					ed = OptionsWindow.wheelEditors[w];
					if (ed != null && ed.sprite == obj)
					{
						return ed;
					}
					w++;
				}
				obj = obj.parent;
			}
			return null;
		}

		// slot under the mouse: a sector of the preview or a row of the slot list; -1 = none
		private function EditorSlotAt(ed:Object) : int
		{
			var i:int = 0;
			var n:int = ed.items.length;
			var row:ShortcutItem = null;
			var mx:Number = ed.sprite.mouseX;
			var my:Number = ed.sprite.mouseY;
			var dx:Number = mx - EDITOR_PREVIEW_X;
			var dy:Number = my - EDITOR_PREVIEW_Y;
			var dist:Number = Math.sqrt(dx * dx + dy * dy);
			var angle:Number = NaN;
			var sector:Number = NaN;
			if (n > 0 && dist <= ed.rOut * ed.texScale && dist >= ed.rIn * ed.texScale * 0.6)
			{
				angle = Math.atan2(dy, dx) * 180 / Math.PI + 90;
				while (angle < 0)
				{
					angle += 360;
				}
				sector = 360 / n;
				return int((angle + sector / 2) / sector) % n;
			}
			i = 0;
			while (i < n)
			{
				row = ed.slotRows[i];
				if (mx >= row.x && mx <= row.x + 22 + 196 && my >= row.y && my < row.y + EDITOR_SLOT_STEP)
				{
					return i;
				}
				i++;
			}
			return -1;
		}

		private function OnEditorPreviewDown(e:MouseEvent) : void
		{
			try
			{
				var ed:Object = this.FindEditor(e.currentTarget);
				if (ed == null)
				{
					return;
				}
				var slot:int = this.EditorSlotAt(ed);
				if (slot >= 0)
				{
					var ci:int = this.CatalogIndex(ed, int(ed.items[slot]));
					var text:String = "";
					if (ci >= 0)
					{
						text = String(ed.labels[ci]);
					}
					this.BeginEditorDrag(ed, {slot: slot}, text);
				}
			}
			catch (err:Error)
			{
			}
		}

		private function OnEditorSlotDown(e:MouseEvent) : void
		{
			try
			{
				var ed:Object = this.FindEditor(e.currentTarget);
				if (ed == null)
				{
					return;
				}
				var slot:int = ed.slotRows.indexOf(e.currentTarget);
				if (slot >= 0 && slot < ed.items.length)
				{
					this.BeginEditorDrag(ed, {slot: slot}, ShortcutItem(e.currentTarget).value.text);
				}
			}
			catch (err:Error)
			{
			}
		}

		private function OnEditorCatalogDown(e:MouseEvent) : void
		{
			try
			{
				var ed:Object = this.FindEditor(e.currentTarget);
				if (ed == null)
				{
					return;
				}
				var index:int = ed.catalogRows.indexOf(e.currentTarget);
				if (index >= 0)
				{
					this.BeginEditorDrag(ed, {catalog: int(ed.ids[index])}, String(ed.labels[index]));
				}
			}
			catch (err:Error)
			{
			}
		}

		private function BeginEditorDrag(ed:Object, source:Object, text:String) : void
		{
			this.EndEditorDrag();
			OptionsWindow.dragEditor = ed;
			OptionsWindow.dragSource = source;
			OptionsWindow.dragMoved = false;
			OptionsWindow.dragStartX = this.mouseX;
			OptionsWindow.dragStartY = this.mouseY;
			if (OptionsWindow.dragProxy == null)
			{
				OptionsWindow.dragProxy = this.MakeEditorLabel("", 16777215);
				OptionsWindow.dragProxy.mouseEnabled = false;
			}
			OptionsWindow.dragProxy.SetName(text);
			OptionsWindow.dragProxy.visible = false;
			addChild(OptionsWindow.dragProxy);
			addEventListener(MouseEvent.MOUSE_MOVE, this.OnEditorDragMove);
			addEventListener(MouseEvent.MOUSE_UP, this.OnEditorDragUp);
			if (stage != null)
			{
				stage.addEventListener(MouseEvent.MOUSE_UP, this.OnEditorDragUp);
			}
		}

		private function OnEditorDragMove(e:MouseEvent) : void
		{
			try
			{
				var ed:Object = OptionsWindow.dragEditor;
				if (ed == null)
				{
					return;
				}
				var dx:Number = this.mouseX - OptionsWindow.dragStartX;
				var dy:Number = this.mouseY - OptionsWindow.dragStartY;
				if (!OptionsWindow.dragMoved && dx * dx + dy * dy > 16)
				{
					OptionsWindow.dragMoved = true;
				}
				if (OptionsWindow.dragProxy != null && OptionsWindow.dragMoved)
				{
					OptionsWindow.dragProxy.x = this.mouseX + 10 - OptionsWindow.dragProxy.text.x;
					OptionsWindow.dragProxy.y = this.mouseY + 4 - OptionsWindow.dragProxy.text.y;
					OptionsWindow.dragProxy.visible = true;
				}
				var hover:int = -1;
				if (OptionsWindow.dragMoved)
				{
					hover = this.EditorSlotAt(ed);
				}
				if (hover != ed.hover)
				{
					ed.hover = hover;
					this.UpdateEditorHighlight(ed);
				}
			}
			catch (err:Error)
			{
			}
		}

		private function OnEditorDragUp(e:MouseEvent) : void
		{
			try
			{
				var ed:Object = OptionsWindow.dragEditor;
				var source:Object = OptionsWindow.dragSource;
				var moved:Boolean = OptionsWindow.dragMoved;
				if (ed != null && source != null)
				{
					var target:int = this.EditorSlotAt(ed);
					if (!moved)
					{
						// a click: a slot becomes the selected one, a command goes to the selected slot
						if (source.slot != undefined)
						{
							ed.selected = int(source.slot);
						}
						else
						{
							this.PutIntoSlot(ed, int(source.catalog), ed.selected);
							if (ed.selected < ed.items.length - 1)
							{
								ed.selected = ed.selected + 1;
							}
						}
					}
					else if (target >= 0)
					{
						if (source.slot != undefined)
						{
							this.SwapSlots(ed, int(source.slot), target);
						}
						else
						{
							this.PutIntoSlot(ed, int(source.catalog), target);
						}
						ed.selected = target;
					}
					ed.hover = -1;
					this.RefreshWheelEditor(ed);
				}
			}
			catch (err:Error)
			{
			}
			this.EndEditorDrag();
		}

		private function EndEditorDrag() : void
		{
			removeEventListener(MouseEvent.MOUSE_MOVE, this.OnEditorDragMove);
			removeEventListener(MouseEvent.MOUSE_UP, this.OnEditorDragUp);
			if (stage != null)
			{
				stage.removeEventListener(MouseEvent.MOUSE_UP, this.OnEditorDragUp);
			}
			if (OptionsWindow.dragProxy != null && OptionsWindow.dragProxy.parent != null)
			{
				OptionsWindow.dragProxy.parent.removeChild(OptionsWindow.dragProxy);
			}
			OptionsWindow.dragEditor = null;
			OptionsWindow.dragSource = null;
		}

		// a command that is already in another slot changes places with the one in the target slot
		private function PutIntoSlot(ed:Object, id:int, slot:int) : void
		{
			if (slot < 0 || slot >= ed.items.length)
			{
				return;
			}
			var old:int = ed.items.indexOf(id);
			if (old == slot)
			{
				return;
			}
			if (old >= 0)
			{
				ed.items[old] = ed.items[slot];
			}
			ed.items[slot] = id;
			this.SaveWheelEditor(ed);
		}

		private function SwapSlots(ed:Object, a:int, b:int) : void
		{
			if (a == b || a < 0 || b < 0 || a >= ed.items.length || b >= ed.items.length)
			{
				return;
			}
			var t:int = int(ed.items[a]);
			ed.items[a] = ed.items[b];
			ed.items[b] = t;
			this.SaveWheelEditor(ed);
		}

		private function EditorOfButton(button:Object) : Object
		{
			var w:int = 0;
			var ed:Object = null;
			while (w < OptionsWindow.wheelEditors.length)
			{
				ed = OptionsWindow.wheelEditors[w];
				if (ed != null && (ed.minus == button || ed.plus == button))
				{
					return ed;
				}
				w++;
			}
			return null;
		}

		private function OnEditorMinus(e:Event) : void
		{
			try
			{
				var ed:Object = this.EditorOfButton(e.currentTarget);
				if (ed != null && ed.items.length > EDITOR_WHEEL_MIN)
				{
					ed.items.pop();
					this.SaveWheelEditor(ed);
					this.RefreshWheelEditor(ed);
				}
			}
			catch (err:Error)
			{
			}
		}

		private function OnEditorPlus(e:Event) : void
		{
			try
			{
				var ed:Object = this.EditorOfButton(e.currentTarget);
				if (ed == null || ed.items.length >= EDITOR_WHEEL_MAX)
				{
					return;
				}
				var i:int = 0;
				while (i < ed.ids.length)
				{
					if (ed.items.indexOf(int(ed.ids[i])) < 0)
					{
						ed.items.push(int(ed.ids[i]));
						ed.selected = ed.items.length - 1;
						this.SaveWheelEditor(ed);
						this.RefreshWheelEditor(ed);
						return;
					}
					i++;
				}
			}
			catch (err:Error)
			{
			}
		}

		// the editor goes under the rows of its page (the rows are created by C++ at any time before the page is shown)
		private function PlaceWheelEditors() : void
		{
			var w:int = 0;
			var ed:Object = null;
			var page:int = 0;
			var bottom:Number = 0;
			while (w < OptionsWindow.wheelEditors.length)
			{
				ed = OptionsWindow.wheelEditors[w];
				if (ed != null && ed.sprite != null)
				{
					page = EDITOR_PAGES[w];
					bottom = Number(OptionsWindow.pageNextY[page]);
					if (page == OptionsWindow.buildPage)
					{
						bottom = OptionsWindow.advancedNextY;
					}
					ed.sprite.y = bottom + 8;
				}
				w++;
			}
		}

		// apply / cancel of the key bindings: the window stays on the current category
		private function ApplyShortCutClick(e:Event):void
		{
			if (shortcutsTab.shortcutContainer.IsChanged())
			{
				shortcutsTab.shortcutContainer.SendRebinds();
				FSCommands.Send(FSCommands.PREFERENCE_SHORTCUTS_APPLY);
			}
			shortcutsTab.shortcutContainer.ClearFocus();
			RestoreCategory();
		}

		private function CancelShortCutClick(e:Event):void
		{
			shortcutsTab.shortcutContainer.ResetToDefault();
			shortcutsTab.shortcutContainer.ClearFocus();
			RestoreCategory();
		}

		private function RestoreCategory():void
		{
			if (categoryButtons == null)
			{
				// no categories (layout failed): the original behaviour
				settingsTab.visible = true;
				shortcutsTab.visible = false;
				tab1.visible = true;
				title.text = Localization.PreferenceMenu_Header;
			}
			else
			{
				ShowCategory(shownCategory);
			}
		}

		private function Tab1Click(e:Event):void
		{
			if (categoryButtons != null)
			{
				ShowCategory(CAT_KEYS);
				return;
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
			title.text = Localization.PreferenceMenu_Header;
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
			ClearKeyFocus();

			super.closeThisFunction(e);
		}

		public function OnCloseByEsc():void
		{
			CancelShortCutClick(null);
			ClearKeyFocus();
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
