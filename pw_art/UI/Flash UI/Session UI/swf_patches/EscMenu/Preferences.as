package EscMenu
{
   public class Preferences
   {
      
      private var main:Main;
      
      public function Preferences(param1:Main)
      {
         super();
         this.main = param1;
      }
      
      public function AddCheckBoxControl(param1:String) : void
      {
         this.main.optionsWindow.AddCheckBoxControl(param1);
      }
      
      public function AddSliderControl(param1:String, param2:int) : void
      {
         this.main.optionsWindow.AddSliderControl(param1,param2);
      }
      
      public function AddComboBoxControl(param1:String, param2:Array) : void
      {
         this.main.optionsWindow.AddComboBoxControl(param1,param2);
      }
      
      public function SetControlValue(param1:String, param2:int, param3:Boolean) : void
      {
         this.main.optionsWindow.SetControlValue(param1,param2,param3);
      }
      
      public function AddAdvancedCheckBox(param1:String, param2:String) : void
      {
         this.main.optionsWindow.AddAdvancedCheckBox(param1,param2);
      }
      
      public function AddAdvancedSlider(param1:String, param2:String, param3:int) : void
      {
         this.main.optionsWindow.AddAdvancedSlider(param1,param2,param3);
      }
      
      public function SetAdvancedTargetPage(param1:int) : void
      {
         this.main.optionsWindow.SetAdvancedTargetPage(param1);
      }
      
      public function AddAdvancedHeader(param1:String) : void
      {
         this.main.optionsWindow.AddAdvancedHeader(param1);
      }
      
      public function SetAdvancedValueText(param1:String, param2:String) : void
      {
         this.main.optionsWindow.SetAdvancedValueText(param1,param2);
      }
      
      public function ActivateAbilityToChange(param1:Boolean) : void
      {
         this.main.optionsWindow.ActivateAbilityToChange(param1);
      }
   }
}

