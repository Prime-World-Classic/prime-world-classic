package EscMenu
{
	public class Preferences
	{
		private var main:Main;
		public function Preferences(_main:Main)
		{
			main = _main;
		}
		
		public function AddCheckBoxControl(controlName:String):void
		{
			main.optionsWindow.AddCheckBoxControl(controlName);
		}
		
		public function AddSliderControl(controlName:String,maxCount:int):void
		{
			main.optionsWindow.AddSliderControl(controlName, maxCount);
		}
		
		public function AddComboBoxControl(controlName:String,comboboxValues:Array):void
		{
			main.optionsWindow.AddComboBoxControl(controlName,comboboxValues);
		}
		
		public function SetControlValue(controlName:String, value:int, active:Boolean):void
		{
			main.optionsWindow.SetControlValue(controlName, value, active);
		}
		
		// 'Advanced' / 'Communication' option pages: the rows are created from C++ (PreferencesProcessor.cpp, s_advancedVars)
		public function AddAdvancedCheckBox(name:String, caption:String):void
		{
			main.optionsWindow.AddAdvancedCheckBox(name, caption);
		}
		
		public function AddAdvancedSlider(name:String, caption:String, maxValue:int):void
		{
			main.optionsWindow.AddAdvancedSlider(name, caption, maxValue);
		}
		
		public function AddAdvancedKey(name:String, caption:String, command:String):void
		{
			main.optionsWindow.AddAdvancedKey(name, caption, command);
		}

		public function AddAdvancedSlot(name:String, controlName:String):void
		{
			main.optionsWindow.AddAdvancedSlot(name, controlName);
		}
		
		public function AddAdvancedNote(text:String):void
		{
			main.optionsWindow.AddAdvancedNote(text);
		}
		
		public function SetAdvancedTargetPage(page:int):void
		{
			main.optionsWindow.SetAdvancedTargetPage(page);
		}
		
		public function AddAdvancedHeader(caption:String):void
		{
			main.optionsWindow.AddAdvancedHeader(caption);
		}
		
		public function SetAdvancedValueText(name:String, text:String):void
		{
			main.optionsWindow.SetAdvancedValueText(name, text);
		}
		
		public function ActivateAbilityToChange(activate:Boolean):void
		{
			main.optionsWindow.ActivateAbilityToChange(activate);
		}
	}
}