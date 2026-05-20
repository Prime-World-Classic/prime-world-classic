#ifndef PREFERENCESPROCESSOR_H_10FB1B03
#define PREFERENCESPROCESSOR_H_10FB1B03
#include "DBPreferences.h"
#include "System/Pointers/BaseObjectST.h"
#include "FSCommandListner.h"
#include <cstdio>

#if !defined(PW_LINUX_DB_BOOTSTRAP)
#include "FlashInterface.h"
#endif

#if defined(__linux__)
#define sscanf_s sscanf
#endif

namespace UI
{
struct IPrefVar: BaseObjectST
{
  virtual ~IPrefVar() {}    

  virtual int  GetCurrValue() const = 0;
  virtual void UpdateCurValue( int newVal ) = 0;
  virtual void UpdateEnabledState( bool isEnabled ) = 0;
  virtual bool GetEnabledStateForDependents() const = 0;
};


class PreferencesProcessor :
#if defined(PW_LINUX_DB_BOOTSTRAP)
  public IFSCommandListner, public BaseObjectST
#else
  public FlashInterface, public IFSCommandListner
#endif
{
   NI_DECLARE_REFCOUNT_CLASS_2( PreferencesProcessor, UI::IFSCommandListner, BaseObjectST );

public:
  PreferencesProcessor( UI::FlashContainer2 * flashWnd, const char* className );
  virtual void Init( const NDb::Preferences &desc );
  virtual void RestorePreset( const string &name );
  virtual void ShortcutRebind( const wchar_t* command );
  virtual void RestorePreset( int index );
  virtual void Apply();
  virtual void ApplyShortcuts();
  virtual void ApplyValuesOnly();
  virtual void Cancel();
  bool NeedToApply() const;
  
protected:
  typedef Strong<IPrefVar> PrefVarPtr;

  struct VarData
  {
    PrefVarPtr pAbstract;
    const NDb::PrefVariable *pDesc;
    int defaultValue;
  };

  typedef map<string, VarData> Variables;
  typedef vector<NDb::PreferencesPreset> TPresets;
  
private:
  bool CalcEnabledState( const string &varId ) const;
  void UpdateEnabledState( const string &enabledFromVarId, bool isEnabled );  
  void UpdateValue( const VarData &var, int value );
  void UpdateValue( const string &varId, int value );
  void RestorePreset( const NDb::PreferencesPreset &preset );
  void StoreAllValues();
  void RestoreAllValues();
  static bool CanBeCanceled( const VarData &var );
  static bool NeedToApplyCommand( const VarData &var );
  
  // from UI::IFSCommandListner
  virtual void OnFSCommand( UI::FlashContainer2* _wnd, const char* listenerID,  const char* args, const wchar_t * argsW  );
      
private:
  Variables variables;
  const TPresets *pPresets;
};

}



#endif //#define PREFERENCESPROCESSOR_H_10FB1B03
