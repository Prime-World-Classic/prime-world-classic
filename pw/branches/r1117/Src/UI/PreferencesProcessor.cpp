#include "stdafx.h"
#include "PreferencesProcessor.h"
#include "DBPreferences.h"
#include "Render/renderer.h"
#include "FlashContainer2.h"
#include "System/nalgoritm.h"
#include "System/niterator.h"
#include "libdb/DbResourceCache.h"
#include "System/Texts.h"

namespace
{
  struct DisableAssertionLoadingFilesGuard : NonCopyable
  {
    DisableAssertionLoadingFilesGuard()
      : initial(NDb::GetDbResourceCache().EnableAssertionLoadingFiles(false))
    {
    }
    ~DisableAssertionLoadingFilesGuard()
    {
      NDb::GetDbResourceCache().EnableAssertionLoadingFiles(initial);
    }
  private:
    const bool initial;
  };
}

namespace UI
{
using NGlobal::VariantValue;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
const VariantValue GetVar( const string &name )
{
  return NGlobal::GetVar( name );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<class T>
void SetVar( const string &name, const T &val )
{
  NGlobal::SetVar( name, val, STORAGE_DONT_CARE );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<class T, class Compare>
int findIntervalNum( const vector<T> &arr, T val, Compare cmp, const string &debugVarName )
{
  int i = 0;

  while( i < arr.size() && !cmp(val, arr[i]) ) 
    ++i;

  if( i <= 0 )
  {
    DebugTrace( "Pref value too low. Please, check config variable '%s'", debugVarName.c_str() );
    return 0;
  }

  if( i == arr.size() && arr.back() != val )
   DebugTrace( "Pref value too high. Please, check config variable '%s'",  debugVarName.c_str() );

  return i - 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class PrefVarBase
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class PrefVarBase: public IPrefVar
{
  NI_DECLARE_REFCOUNT_CLASS_1( PrefVarBase, BaseObjectST );
  
public:
  PrefVarBase( FlashInterface *pFlash, const string &varId ): 
    pFlash(pFlash), varId(varId)
  {}

protected:
  void SetControlValue( int value, bool isEnabled )
  {
    pFlash->CallMethod( "SetControlValue", varId, value, isEnabled );
  }

private:
  FlashInterface * const pFlash;
  const string varId;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class StdVarBase
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class StdVarBase: public PrefVarBase
{
public:
  StdVarBase( FlashInterface *pFlash, const string &varId, const string &varName ): 
    PrefVarBase(pFlash, varId), varName(varName), isEnabled(true)
  {}
  
  virtual bool GetEnabledStateForDependents() const 
  { 
    return GetVar(varName).Get<bool>();   
  }
    
  virtual void UpdateCurValue( int newVal )
  { 
    SetVar( varName, convertValueFromUI(newVal) );
    SetControlValue( newVal, isEnabled );
  }
  
  virtual void UpdateEnabledState( bool isEnabled )
  {      
    this->isEnabled = isEnabled;
    SetControlValue( GetCurrValue(), isEnabled );
  }
  
  virtual int GetCurrValue() const
  {
    return convertValueToUI( GetVar(varName) );
  }
  
protected:
  const string &GetVarNameForDebug() const { return varName; }
 
private:
  virtual int convertValueToUI( const VariantValue &val ) const = 0;
  virtual const VariantValue convertValueFromUI( int val ) const = 0;
 
private:
  const string varName;
  bool isEnabled;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class CheckBoxControl
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CheckBoxControl: public StdVarBase
{
public:
  CheckBoxControl( FlashInterface *pFlash, const string &varId, const NDb::PrefVariable &desc ):  
    StdVarBase( pFlash, varId, desc.commandName )
  {
    pFlash->CallMethod( "AddCheckBoxControl", varId );
  }
  
private:
  virtual int convertValueToUI( const VariantValue &val ) const
  {
    return val.GetInt64();
  }
  
  virtual const VariantValue convertValueFromUI( int val ) const
  {
    return VariantValue(val);
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class ComboBoxControl
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ComboBoxControl: public StdVarBase
{
public:
  ComboBoxControl( FlashInterface *pFlash, const string &varId, const NDb::PrefVariable &desc ):  
      StdVarBase( pFlash, varId, desc.commandName )
  {
    vector<wstring> labels;
  
    for( int i = 0; i < desc.comboBoxDescription.size(); ++i )
    {
      valueIntervals.push_back( desc.comboBoxDescription[i].value );
      labels.push_back( desc.comboBoxDescription[i].userName.GetText() );
    }
  
    pFlash->CallMethod( "AddComboBoxControl", varId, labels );
  }

private:
  virtual int convertValueToUI( const VariantValue &val ) const
  {
    NI_ASSERT( valueIntervals.size() >= 2, "Size is too small" );      

    if( valueIntervals[0] < valueIntervals[1])
      return findIntervalNum( valueIntervals, val.GetFloat(), less(), GetVarNameForDebug() );
    else
      return findIntervalNum( valueIntervals, val.GetFloat(), greater(), GetVarNameForDebug() );
  }

  virtual const VariantValue convertValueFromUI( int val ) const
  {
    NI_ASSERT( val >= 0 && val < valueIntervals.size(), "Incorrect pref value" );   
    return VariantValue(valueIntervals[val]);
  }

private:
  vector<float> valueIntervals;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class SliderControl
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SliderControl: public StdVarBase
{
public:
  SliderControl( FlashInterface *pFlash, const string &varId, const NDb::PrefVariable &desc ):  
    StdVarBase( pFlash, varId, desc.commandName )
  {
    pFlash->CallMethod( "AddSliderControl", varId, valuesCount );
  }

private:
  virtual int convertValueToUI( const VariantValue &val ) const
  {
    //Минимальное значение, представимое на интервале, используем для округления
    const float discrValue = 1.0f / (valuesCount - 1);
    return static_cast<int>( val.GetFloat() * (valuesCount - 1) + discrValue / 2 );
  }

  virtual const VariantValue convertValueFromUI( int val ) const
  {
    return VariantValue( static_cast<float>(val) / (valuesCount - 1) );
  }
  
  static const int valuesCount = 100;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// class DisplayModeComboBoxControl
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class DisplayModeComboBoxControl: public PrefVarBase
{
private:
  struct DisplayMode
  {
    unsigned int width;
    unsigned int height;
        
    wstring ToString() const
    {
      return NStr::StrFmtW( L"%u x %u", width, height );
    }
  };
  
  struct DisplayModeCmp
  {
    bool operator()( const DisplayMode &dm1, const DisplayMode &dm2 ) const
    {
      return (dm1.width > dm2.width) || (!(dm2.width > dm1.width) && dm1.height > dm2.height);
    }
  };

  typedef set<DisplayMode, DisplayModeCmp> DisplayModes;
  typedef vector<wstring>  DisplayModesDesc;

public:
  DisplayModeComboBoxControl( FlashInterface *pFlash, const string &varId, const NDb::PrefVariable &desc ):  
    PrefVarBase( pFlash, varId ), isEnabled(true)
  {  
    const int count = Render::GetRenderer()->GetModesCount();

    for( int i = 0; i < count; ++i )
    {
      DisplayMode dm;
      unsigned int refreshRate;
     
      Render::GetRenderer()->GetMode( i, dm.width, dm.height, refreshRate );
      
      if( !Render::GetRenderer()->NeedToFilterResolutionFromUser(dm.width, dm.height) )
        displayModes.insert(dm);
    }
        
    displayModes.insert( GetCurDisplayMode() );
    
    DisplayModesDesc displayModesDesc;
    
    for( DisplayModes::const_iterator it = displayModes.begin(); it != displayModes.end(); ++it )
      displayModesDesc.push_back( it->ToString() );
    
    pFlash->CallMethod( "AddComboBoxControl", varId, displayModesDesc );
  }

private:
  virtual bool GetEnabledStateForDependents() const 
  { 
    return true;   
  }

  virtual void UpdateCurValue( int newVal )
  { 
    if( newVal < 0 || newVal >= displayModes.size() )
    {
      NI_ALWAYS_ASSERT("Incorrect pref value" );  
      return;    
    }
    
    DisplayModes::const_iterator it = displayModes.begin();
    
    advance( it, newVal );
    SetCurDisplayMode( *it );
		SetControlValue(newVal, isEnabled);
  }

  virtual void UpdateEnabledState( bool isEnabled )
  {      
    this->isEnabled = isEnabled;
    SetControlValue( GetCurrValue(), isEnabled );
  }
  
  virtual int GetCurrValue() const
  {
     return FindDisplayModeIndex( GetCurDisplayMode() );
  }
  
  private:
    DisplayMode GetCurDisplayMode() const
    {
      DisplayMode dm;
      Render::GetResolution( dm.width, dm.height );

      return dm;
    }
    
    void SetCurDisplayMode( const DisplayMode &dm )
    {
      Render::SetResolution( dm.width, dm.height );
    }
    
    int FindDisplayModeIndex( const DisplayMode &dm ) const
    {
      DisplayModes::const_iterator it = displayModes.find( dm );

      if( it == displayModes.end() )
      {
        NI_ALWAYS_ASSERT( NStr::StrFmt("Display mode %d x %d is not valid", dm.width, dm.height) );
        return 0;
      }

      return distance( displayModes.begin(), it );   
    }
  
private:
  DisplayModes displayModes;
  bool isEnabled;
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Strong<IPrefVar> createControl( FlashInterface *pFlash, const string &varId, const NDb::PrefVariable &desc )
{
  switch( desc.controlVarType )
  {
    case NDb::CONTROLVARTYPE_CHECKBOX: return new CheckBoxControl( pFlash, varId, desc );
    case NDb::CONTROLVARTYPE_COMBOBOXINTERVAL: return new ComboBoxControl( pFlash, varId, desc );
    case NDb::CONTROLVARTYPE_SLIDER: return new SliderControl( pFlash, varId, desc );
    case NDb::CONTROLVARTYPE_DISPLAYMODECOMBOBOX: return new DisplayModeComboBoxControl( pFlash, varId, desc ); 
    
    default:
      NI_ALWAYS_ASSERT( NStr::StrFmt("Unknown NDb::ControlVarType %d", desc.controlVarType) );
      return NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// "Advanced" settings page (the "Advanced" button in the options window).
// Client-side settings that have no description in Preferences data: each row is a console variable
// (STORAGE_USER, saved to user.cfg), the value is applied immediately, no "Apply" button needed.
// Rows whose variable is not registered in this build are skipped.
// To add a setting: add one line to s_advancedVars.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum EAdvancedControl { ADV_CHECKBOX, ADV_SLIDER, ADV_HEADER, ADV_PAGE, ADV_KEY, ADV_SLOT, ADV_NOTE };
// ADV_KEY: varName = bind command, the key is set in Flash; ADV_SLOT: varName = a control of the main settings tab moved
// to this place by Flash (still driven by the data-defined preferences); ADV_NOTE: small text under the previous row

struct AdvancedVarDesc
{
  const char *     varId;        // control name in Flash (unique)
  const char *     varName;      // console variable
  EAdvancedControl type;
  float            minValue;     // slider: value at the left end
  float            maxValue;     // slider: value at the right end
  int              steps;        // slider: number of positions
  const wchar_t *  format;       // slider: value text, printf-like
  const char *     enabledFrom;  // varId of the checkbox that enables this row ("" = always enabled)
  const wchar_t *  label;
  const wchar_t *  labelEn;      // English label (client language "en"), ASCII
};

// Labels follow the client language like the _en.txt texts do (System/Texts.cpp).
static wstring AdvancedLabel( const AdvancedVarDesc & desc )
{
  if ( GetLanguageShortCode() == "en" && desc.labelEn[0] != 0 )
    return wstring( desc.labelEn );
  return wstring( desc.label );
}

// labels are written as \u escapes (independent of the source file codepage); C4428 just notes that
#pragma warning( push )
#pragma warning( disable : 4428 )
static const AdvancedVarDesc s_advancedVars[] =
{
  { "adv_page_advanced", "", ADV_PAGE, 0.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_hdr_move", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u0414\u0432\u0438\u0436\u0435\u043d\u0438\u0435 \u0438 \u0430\u0442\u0430\u043a\u0430", L"Movement and attack" },
  { "adv_slot_left_click", "leftClick_tb", ADV_SLOT, 0.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_auto_attack", "auto_attack", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u0410\u0432\u0442\u043e\u0430\u0442\u0430\u043a\u0430", L"Auto attack" },
  { "adv_move_on_hold", "move_on_hold", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u0411\u0435\u0433 \u043f\u0440\u0438 \u0437\u0430\u0436\u0430\u0442\u043e\u0439 \u043a\u043d\u043e\u043f\u043a\u0435 \u043f\u0435\u0440\u0435\u0434\u0432\u0438\u0436\u0435\u043d\u0438\u044f", L"Move while holding the move button" },
  { "adv_move_on_hold_marker", "move_on_hold_marker", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "adv_move_on_hold", L"\u041c\u0430\u0440\u043a\u0435\u0440 \u043f\u0440\u0438 \u0431\u0435\u0433\u0435 \u0441 \u0437\u0430\u0436\u0430\u0442\u043e\u0439 \u043a\u043d\u043e\u043f\u043a\u043e\u0439", L"Marker when moving with the button held" },
  { "adv_move_on_hold_interval", "move_on_hold_interval", ADV_SLIDER, 0.1f, 0.5f, 9, L"%.2f", "adv_move_on_hold", L"\u0418\u043d\u0442\u0435\u0440\u0432\u0430\u043b \u043f\u043e\u0432\u0442\u043e\u0440\u0430 \u043a\u043b\u0438\u043a\u043e\u0432, \u0441", L"Click repeat interval, s" },
  { "adv_hdr_abilities", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u0421\u043f\u043e\u0441\u043e\u0431\u043d\u043e\u0441\u0442\u0438", L"Abilities" },
  { "adv_smart_cast_toggles", "smart_cast_toggles", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u0421\u043c\u0430\u0440\u0442\u043a\u0430\u0441\u0442: \u0433\u0430\u043b\u043e\u0447\u043a\u0438 \u043d\u0430 \u043f\u0430\u043d\u0435\u043b\u0438", L"Smart cast: checkboxes on the panel" },
  { "adv_dbl_click_selfcast", "dbl_click_selfcast", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u0414\u0432\u043e\u0439\u043d\u043e\u0435 \u043d\u0430\u0436\u0430\u0442\u0438\u0435 - \u043d\u0430 \u0441\u0435\u0431\u044f", L"Double press - cast on self" },
  { "adv_dbl_click_selfcast_time", "dbl_click_selfcast_time", ADV_SLIDER, 0.15f, 0.5f, 8, L"%.2f", "adv_dbl_click_selfcast", L"\u0412\u0440\u0435\u043c\u044f \u0434\u0432\u043e\u0439\u043d\u043e\u0433\u043e \u043d\u0430\u0436\u0430\u0442\u0438\u044f, \u0441", L"Double press time, s" },
  { "adv_hdr_interface", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u0418\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441", L"Interface" },
  { "adv_team_portraits_scale", "team_portraits_scale", ADV_SLIDER, 50.0f, 200.0f, 16, L"%.0f%%", "", L"\u0420\u0430\u0437\u043c\u0435\u0440 \u043f\u043e\u0440\u0442\u0440\u0435\u0442\u043e\u0432 \u043a\u043e\u043c\u0430\u043d\u0434", L"Team portraits size" },
  { "adv_cursor_scale", "cursor_scale", ADV_SLIDER, 50.0f, 300.0f, 26, L"%.0f%%", "", L"\u0420\u0430\u0437\u043c\u0435\u0440 \u043a\u0443\u0440\u0441\u043e\u0440\u0430", L"Cursor size" },
  { "adv_page_comm", "", ADV_PAGE, 1.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_hdr_ping_wheel", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u0421\u0438\u0433\u043d\u0430\u043b\u044b", L"Signals" },
  { "adv_ping_middle_mouse", "ping_middle_mouse", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u0421\u0438\u0433\u043d\u0430\u043b\u044b \u043d\u0430 \u0441\u0440\u0435\u0434\u043d\u044e\u044e \u043a\u043d\u043e\u043f\u043a\u0443 \u043c\u044b\u0448\u0438", L"Signals on the middle mouse button" },
  { "adv_ping_ground_effect", "ping_ground_effect", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u041c\u0435\u0442\u043a\u0430 \u0441\u0438\u0433\u043d\u0430\u043b\u0430 \u043d\u0430 \u0437\u0435\u043c\u043b\u0435", L"Signal mark on the ground" },
  { "adv_ping_sound", "ping_sound", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u041d\u043e\u0432\u044b\u0435 \u0437\u0432\u0443\u043a\u0438 \u0441\u0438\u0433\u043d\u0430\u043b\u043e\u0432", L"New signal sounds" },
  { "adv_hdr_praise", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u041f\u043e\u0445\u0432\u0430\u043b\u0430", L"Praise" },
  { "adv_praise", "praise", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u041f\u043e\u0445\u0432\u0430\u043b\u0430 (Alt + \u043a\u043b\u0438\u043a \u043f\u043e \u043f\u043e\u0440\u0442\u0440\u0435\u0442\u0443)", L"Praise (Alt + click on a portrait)" },
  { "adv_hdr_chat", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u0427\u0430\u0442", L"Chat" },
  { "adv_show_ability_status", "show_ability_status_messages", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u0421\u043e\u043e\u0431\u0449\u0438\u0442\u044c \u043e \u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u0438 \u0441\u043f\u043e\u0441\u043e\u0431\u043d\u043e\u0441\u0442\u0438 \u0441\u043e\u044e\u0437\u043d\u0438\u043a\u0430\u043c", L"Tell allies about an ability's state" },
  { "adv_note_ability_status", "", ADV_NOTE, 0.0f, 0.0f, 0, L"", "", L"(Alt+\u041a\u043b\u0438\u043a \u043f\u043e \u0442\u0430\u043b\u0430\u043d\u0442\u0443)", L"(Alt + click on a talent)" },
  { "adv_slot_bad_words", "useBadwordsFilter", ADV_SLOT, 0.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_slot_chat_off", "smartchat_cb", ADV_SLOT, 0.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_hdr_voice_chat", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u0413\u043e\u043b\u043e\u0441\u043e\u0432\u043e\u0439 \u0447\u0430\u0442", L"Voice chat" },
  { "adv_key_voice_overlay", "voice_overlay_unlock_on", ADV_KEY, 0.0f, 0.0f, 0, L"", "", L"\u0423\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u0432\u043e\u0439\u0441-\u043e\u0432\u0435\u0440\u043b\u0435\u0435\u043c (\u0437\u0430\u0436\u0430\u0442\u044c)", L"Voice overlay control (hold)" },
  { "adv_page_chat_wheel", "", ADV_PAGE, 2.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_hdr_chat_wheel", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u041a\u043e\u043b\u0435\u0441\u043e \u0431\u044b\u0441\u0442\u0440\u044b\u0445 \u043a\u043e\u043c\u0430\u043d\u0434", L"Quick commands wheel" },
  { "adv_chat_wheel", "chat_wheel", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u041a\u043e\u043b\u0435\u0441\u043e \u0432\u043c\u0435\u0441\u0442\u043e \u043c\u0435\u043d\u044e \u0441 \u0446\u0438\u0444\u0440\u0430\u043c\u0438", L"Wheel instead of the numbered menu" },
  { "adv_chat_wheel_centered", "chat_wheel_centered", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "adv_chat_wheel", L"\u041a\u043e\u043b\u0435\u0441\u043e \u043f\u043e \u0446\u0435\u043d\u0442\u0440\u0443 \u044d\u043a\u0440\u0430\u043d\u0430", L"Wheel in the center of the screen" },
  { "adv_key_smart_chat", "cmd_smart_chat", ADV_KEY, 0.0f, 0.0f, 0, L"", "", L"\u041a\u043d\u043e\u043f\u043a\u0430 \u0431\u044b\u0441\u0442\u0440\u044b\u0445 \u043a\u043e\u043c\u0430\u043d\u0434", L"Quick commands key" },
  { "adv_chat_wheel_transparency", "chat_wheel_transparency", ADV_SLIDER, 0.0f, 90.0f, 10, L"%.0f%%", "adv_chat_wheel", L"\u041f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u043e\u0441\u0442\u044c \u0444\u043e\u043d\u0430 \u043a\u043e\u043b\u0435\u0441\u0430", L"Wheel background transparency" },
  { "adv_chat_wheel_scale", "chat_wheel_scale", ADV_SLIDER, 60.0f, 160.0f, 11, L"%.0f%%", "adv_chat_wheel", L"\u0420\u0430\u0437\u043c\u0435\u0440 \u043a\u043e\u043b\u0435\u0441\u0430", L"Wheel size" },
  { "adv_page_ping_wheel", "", ADV_PAGE, 3.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_hdr_ping_wheel_opts", "", ADV_HEADER, 0.0f, 0.0f, 0, L"", "", L"\u041a\u043e\u043b\u0435\u0441\u043e \u0441\u0438\u0433\u043d\u0430\u043b\u043e\u0432", L"Signal wheel" },
  { "adv_ping_wheel", "ping_wheel", ADV_CHECKBOX, 0.0f, 0.0f, 0, L"", "", L"\u041a\u043e\u043b\u0435\u0441\u043e \u0441\u0438\u0433\u043d\u0430\u043b\u043e\u0432 (\u0443\u0434\u0435\u0440\u0436\u0430\u043d\u0438\u0435)", L"Signal wheel (hold)" },
  { "adv_ping_wheel_delay", "ping_wheel_delay", ADV_SLIDER, 0.1f, 0.5f, 9, L"%.2f", "adv_ping_wheel", L"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430 \u043a\u043e\u043b\u0435\u0441\u0430 \u0441\u0438\u0433\u043d\u0430\u043b\u043e\u0432, \u0441", L"Signal wheel delay, s" },
  { "adv_ping_wheel_transparency", "ping_wheel_transparency", ADV_SLIDER, 0.0f, 90.0f, 10, L"%.0f%%", "adv_ping_wheel", L"\u041f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u043e\u0441\u0442\u044c \u0444\u043e\u043d\u0430 \u043a\u043e\u043b\u0435\u0441\u0430", L"Wheel background transparency" },
  { "adv_ping_wheel_scale", "ping_wheel_scale", ADV_SLIDER, 60.0f, 160.0f, 11, L"%.0f%%", "adv_ping_wheel", L"\u0420\u0430\u0437\u043c\u0435\u0440 \u043a\u043e\u043b\u0435\u0441\u0430", L"Wheel size" },
  { "adv_page_game_sound", "", ADV_PAGE, 4.0f, 0.0f, 0, L"", "", L"", L"" },
  { "adv_ping_sound_volume", "ping_sound_volume", ADV_SLIDER, 0.0f, 100.0f, 11, L"%.0f%%", "adv_ping_sound", L"\u0413\u0440\u043e\u043c\u043a\u043e\u0441\u0442\u044c \u0441\u0438\u0433\u043d\u0430\u043b\u043e\u0432", L"Signal volume" }, // Flash puts it into the sound section (no value text)
};
#pragma warning( pop )

static const int ADVANCED_VARS_COUNT = sizeof( s_advancedVars ) / sizeof( s_advancedVars[0] );

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AdvancedCheckBoxControl: public StdVarBase
{
public:
  AdvancedCheckBoxControl( FlashInterface *pFlash, const AdvancedVarDesc &desc ):
    StdVarBase( pFlash, desc.varId, desc.varName )
  {
    pFlash->CallMethod( "AddAdvancedCheckBox", desc.varId, AdvancedLabel( desc ) );
  }

private:
  virtual int convertValueToUI( const VariantValue &val ) const
  {
    return val.GetInt64() != 0 ? 1 : 0;
  }

  virtual const VariantValue convertValueFromUI( int val ) const
  {
    return VariantValue( val );
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AdvancedSliderControl: public StdVarBase
{
public:
  AdvancedSliderControl( FlashInterface *_pFlash, const AdvancedVarDesc &_desc ):
    StdVarBase( _pFlash, _desc.varId, _desc.varName ), pFlash( _pFlash ), desc( _desc )
  {
    pFlash->CallMethod( "AddAdvancedSlider", desc.varId, AdvancedLabel( desc ), desc.steps );
  }

  virtual void UpdateCurValue( int newVal )
  {
    StdVarBase::UpdateCurValue( newVal );
    UpdateValueText();
  }

  virtual void UpdateEnabledState( bool isEnabled )
  {
    StdVarBase::UpdateEnabledState( isEnabled );
    UpdateValueText();
  }

private:
  void UpdateValueText()
  {
    const float value = GetVar( desc.varName ).GetFloat();
    pFlash->CallMethod( "SetAdvancedValueText", desc.varId, wstring( NStr::StrFmtW( desc.format, value ) ) ); // StrFmtW returns a pointer: pass wstring, not wchar_t*
  }

  virtual int convertValueToUI( const VariantValue &val ) const
  {
    const float range = desc.maxValue - desc.minValue;
    if ( range <= 0.0f || desc.steps < 2 )
      return 0;
    const int pos = static_cast<int>( ( val.GetFloat() - desc.minValue ) / range * ( desc.steps - 1 ) + 0.5f );
    return pos < 0 ? 0 : ( pos > desc.steps - 1 ? desc.steps - 1 : pos );
  }

  virtual const VariantValue convertValueFromUI( int val ) const
  {
    if ( desc.steps < 2 )
      return VariantValue( desc.minValue );
    return VariantValue( desc.minValue + ( desc.maxValue - desc.minValue ) * val / static_cast<float>( desc.steps - 1 ) );
  }

  FlashInterface * const pFlash;
  const AdvancedVarDesc &desc;
};

// descriptions in the format of data-defined preferences (PreferencesProcessor keeps pointers to them)
static NDb::PrefVariable s_advancedPrefVars[ADVANCED_VARS_COUNT];

} //anonymous namespace
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static const char szUpdateFlashCommand[] = "PreferenceComponentSetValue";
static const char szApplyFlashCommand[] = "PreferenceComponentsApply";
static const char szCancelFlashCommand[] = "PreferenceComponentsCancel";
static const char szLoadPresetFlashCommand[] = "PreferenceComponentsLoadPreset";
static const char szShortcutRebindFlashCommand[] = "PreferenceShortcutRebind";
static const char szShortcutsApplyFlashCommand[] = "PreferenceShortcutsApply";
static const char szSetVarFlashCommand[] = "PreferenceSetVar"; // "name value": settings edited by Flash itself (wheel editors)

PreferencesProcessor::PreferencesProcessor( UI::FlashContainer2 * flashWnd, const char* className ):
  FlashInterface(flashWnd, className), pPresets(0), advancedPageEnabled(true)
{
  flashWnd->AddFSListner( szUpdateFlashCommand, this );
  flashWnd->AddFSListner( szApplyFlashCommand, this );
  flashWnd->AddFSListner( szCancelFlashCommand, this );
  flashWnd->AddFSListner( szLoadPresetFlashCommand, this );
  flashWnd->AddFSListner( szShortcutRebindFlashCommand, this );
  flashWnd->AddFSListner( szShortcutsApplyFlashCommand, this );
  flashWnd->AddFSListner( szSetVarFlashCommand, this );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::Init( const NDb::Preferences &desc )
{
  variables.clear();
  
  for( int i = 0; i < desc.variables.size(); ++i )
  {
    VarData val;

    val.pDesc = &desc.variables[i];
    val.pAbstract = createControl(this, val.pDesc->varId, desc.variables[i]);
    val.defaultValue = -1;
    
    variables.insert( Variables::value_type( val.pDesc->varId, val ) );  
  }

  // "Advanced" page rows (see s_advancedVars)
  for( int i = 0; advancedPageEnabled && i < ADVANCED_VARS_COUNT; ++i )
  {
    const AdvancedVarDesc &adv = s_advancedVars[i];

    if( adv.type == ADV_PAGE )
    {
      CallMethod( "SetAdvancedTargetPage", static_cast<int>( adv.minValue ) ); // following rows go to this page (0 = Advanced, 1 = Communication, 2 = Chat wheel, 3 = Ping wheel, 4 = Game: under the sound)
      continue;
    }
    if( adv.type == ADV_HEADER )
    {
      CallMethod( "AddAdvancedHeader", AdvancedLabel( adv ) ); // category title, no variable behind it
      continue;
    }
    if( adv.type == ADV_SLOT )
    {
      CallMethod( "AddAdvancedSlot", adv.varId, adv.varName );
      continue;
    }
    if( adv.type == ADV_NOTE )
    {
      CallMethod( "AddAdvancedNote", AdvancedLabel( adv ) );
      continue;
    }
    if( adv.type == ADV_KEY )
    {
      CallMethod( "AddAdvancedKey", adv.varId, AdvancedLabel( adv ), adv.varName ); // key of a bind command, rebound by Flash
      continue;
    }
    if( !NGlobal::IsCommandRegistred( adv.varName ) )
      continue;                                   // variable is not present in this build
    if( adv.enabledFrom[0] != '\0' && variables.find( adv.enabledFrom ) == variables.end() )
      continue;                                   // parent checkbox was skipped

    NDb::PrefVariable &pref = s_advancedPrefVars[i];
    pref.varId                 = adv.varId;
    pref.commandName           = adv.varName;
    pref.controlVarType        = ( adv.type == ADV_CHECKBOX ) ? NDb::CONTROLVARTYPE_CHECKBOX : NDb::CONTROLVARTYPE_SLIDER;
    pref.applyCommandName      = "";
    pref.enabledFromOtherVarId = adv.enabledFrom;

    VarData val;
    val.pDesc = &pref;
    if( adv.type == ADV_CHECKBOX )
      val.pAbstract = new AdvancedCheckBoxControl( this, adv );
    else
      val.pAbstract = new AdvancedSliderControl( this, adv );
    val.defaultValue = -1;

    variables.insert( Variables::value_type( pref.varId, val ) );
  }
  
  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
    it->second.pAbstract->UpdateEnabledState( CalcEnabledState(it->second.pDesc->enabledFromOtherVarId) );
    
  StoreAllValues();
  
  pPresets = &desc.presets;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::StoreAllValues()
{
  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
    it->second.defaultValue = it->second.pAbstract->GetCurrValue();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PreferencesProcessor::CanBeCanceled( const VarData &var )
{
  //Настройки, которые применяются мгновенно, нельзя отменить по диздоку
  return !var.pDesc->applyCommandName.empty();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PreferencesProcessor::NeedToApplyCommand( const VarData &var )
{
  return !var.pDesc->applyCommandName.empty() && 
         var.pAbstract->GetCurrValue() != var.defaultValue; 
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::RestoreAllValues()
{
  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
  {
    if( CanBeCanceled(it->second) )  
      UpdateValue( it->second, it->second.defaultValue );         
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::RestorePreset( const string &name )
{
  NI_ASSERT( pPresets != 0, "" );
  
  for( int i = 0; i < pPresets->size(); ++i )
    if( (*pPresets)[i].presetName == name )
    { 
      RestorePreset( (*pPresets)[i] );
      return;
    }
    
  NI_ALWAYS_ASSERT( NStr::StrFmt("Can't find preset name '%s'", name.c_str()) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::RestorePreset( int index )
{
  NI_ASSERT( pPresets != 0, "" );

  if( index < 0 || index >= pPresets->size() )
  {
    NI_ALWAYS_ASSERT( NStr::StrFmt("Can't find preset index %d", index) );
    return;
  }
  
  RestorePreset( (*pPresets)[index] );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::RestorePreset( const NDb::PreferencesPreset &preset )
{
  DebugTrace( "RestorePreset: %s", preset.presetName.c_str() );

  for( int j = 0; j < preset.data.size(); ++j )
    UpdateValue( preset.data[j].varId, preset.data[j].value );   
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PreferencesProcessor::CalcEnabledState( const string &varId ) const
{
  if( varId.empty() ) 
    return true;

  Variables::const_iterator it = variables.find( varId );
  
  if( it == variables.end() )
  {
    NI_ALWAYS_ASSERT( NStr::StrFmt("Can't find varName '%s'", varId.c_str()) )
    return true;
  }

  return it->second.pAbstract->GetEnabledStateForDependents();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::UpdateEnabledState( const string &enabledFromVarId, bool isEnabled )
{
  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
    if( it->second.pDesc->enabledFromOtherVarId == enabledFromVarId )
      it->second.pAbstract->UpdateEnabledState( isEnabled );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::UpdateValue( const VarData &var, int value )
{
  if( value == var.pAbstract->GetCurrValue() )
    return;

  var.pAbstract->UpdateCurValue( value );
  UpdateEnabledState( var.pDesc->varId, var.pAbstract->GetEnabledStateForDependents() );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::UpdateValue( const string &varId, int value )
{
  Variables::const_iterator it = variables.find(varId);

  if( it == variables.end() )
  {
    NI_ALWAYS_ASSERT( NStr::StrFmt("Invalid varId %s", varId.c_str()) );
    return;
  }
  
  UpdateValue( it->second, value );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PreferencesProcessor::NeedToApply() const
{
  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
    if( NeedToApplyCommand(it->second) )
      return true;
      
  return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::ApplyValuesOnly()
{
  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
    if( it->second.pAbstract->GetCurrValue() != it->second.defaultValue )
      it->second.pAbstract->UpdateEnabledState( true );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::Apply()
{
  const DisableAssertionLoadingFilesGuard dalf;

  typedef set<string> ApplyCommands;
  ApplyCommands applyCommands;

  for( Variables::const_iterator it = variables.begin(); it != variables.end(); ++it )
    if( NeedToApplyCommand(it->second) )
      applyCommands.insert( it->second.pDesc->applyCommandName );  

  for( ApplyCommands::iterator it = applyCommands.begin(); it != applyCommands.end(); ++it )
  {
    DebugTrace( "RunCommand: %s", it->c_str() );
    const bool result = NGlobal::RunCommand( NStr::ToUnicode(*it) );  
    NI_ASSERT( result, NStr::StrFmt("Command execution failed: %s", it->c_str()) );
  }
  
  StoreAllValues();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::Cancel()
{
  const DisableAssertionLoadingFilesGuard dalf;

  RestoreAllValues();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::ApplyShortcuts()
{
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::ShortcutRebind( const wchar_t* command )
{
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PreferencesProcessor::OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID,  const char* args, const wchar_t * argsW  )
{
  if( strcmp(listenerID, szApplyFlashCommand) == 0 )
  {
    Apply();
  }
  else if( strcmp(listenerID, szCancelFlashCommand) == 0 )
  {
    Cancel();
  }
  else if( strcmp(listenerID, szShortcutRebindFlashCommand) == 0 )
  {
    ShortcutRebind(argsW);
  }
  else if( strcmp(listenerID, szShortcutsApplyFlashCommand) == 0 )
  {
    ApplyShortcuts();
  }
  else if( strcmp(listenerID, szSetVarFlashCommand) == 0 )
  {
    string name;
    for( ; *args != '\0' && *args != ' '; ++args )
      name += *args;
    if( *args == ' ' )
      ++args;
    // only the variables of the wheel editors: Flash must not change anything else this way
    if( name == "chat_wheel_commands" || name == "ping_wheel_items" )
    {
      SetVar( name, VariantValue( string( args ) ) );
      SaveUserVars();
    }
  }
  else if( strcmp(listenerID, szUpdateFlashCommand) == 0 )
  {
    string varId; 
        
    for( ; *args != '\0' && *args != ' ';  ++args )
      varId += *args;     
    
    int value;
    
    sscanf_s( args, "%d", &value );
    UpdateValue( varId, value );
  }
  else if( strcmp(listenerID, szLoadPresetFlashCommand) == 0 )
  {  
    RestorePreset( args );
  }
  else
  {
    NI_ALWAYS_ASSERT( NStr::StrFmt("Unknown flash command: %s", listenerID) );
    return; 
  }
  
  CallMethod( "ActivateAbilityToChange", NeedToApply() ); 
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UI
