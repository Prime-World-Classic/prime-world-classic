package TalentBar
{
   import ActionBar.ActionBar;
   import ActionBar.ActionBarShortcut;
   import Interfaces.IActionButton;
   
   public class TalentShortCut extends ActionBarShortcut
   {
      
      private var talent:Talent;
      
      public function TalentShortCut(param1:Talent, param2:ActionBar)
      {
         super(param2);
         this.talent = param1;
         this.State = param1.TalantState;
      }
      
      override public function set State(param1:int) : void
      {
         if(Boolean(_actionButton) && this.talent.altPath != "")
         {
            if(this.talent.IsAlternativStateActivated)
            {
               _actionButton.iconPath = this.talent.altPath;
            }
            else
            {
               _actionButton.iconPath = this.talent.path;
            }
         }
         super.State = param1;
      }
      
      override public function Press() : void
      {
         super.Press();
         this.talent.Press();
      }
      
      override public function showTooltip() : void
      {
         FSCommands.Send(FSCommands.TALENT_ACTIONTOOLTIP,FSCommands.TOOLTIP_SHOW + this.talent.column.toString() + " " + this.talent.raw.toString());
      }
      
      override public function hideTooltip() : void
      {
         FSCommands.Send(FSCommands.TALENT_ACTIONTOOLTIP,FSCommands.TOOLTIP_HIDE + this.talent.column.toString() + " " + this.talent.raw.toString());
      }
      
      override public function set actionButton(param1:IActionButton) : *
      {
         super.actionButton = param1;
         this.talent.shortcut = this;
         if(_actionButton == null)
         {
            return;
         }
         _actionButton.iconPath = this.talent.icon;
      }
      
      override public function get icon() : String
      {
         return this.talent.icon;
      }
      
      override public function RemoveFromActionbar() : void
      {
         actionBar.RemoveShortcut(this);
         this.talent.shortcut = null;
      }
      
      // the talent of this shortcut (smart-cast checkboxes of the action bar)
      public function get TalentObj() : Talent
      {
         return this.talent;
      }
      
      override public function get DesirableIndex() : int
      {
         return this.talent.DesiredIndex;
      }
   }
}

