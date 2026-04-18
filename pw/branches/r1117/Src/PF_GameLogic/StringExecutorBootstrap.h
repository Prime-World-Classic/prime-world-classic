#pragma once

#include "../../Data/GameLogic/FormulaPars.h"

class IBinSaver;
struct IXmlSaver;

namespace NScript
{
class VariantValue;
}

struct ExecutableString
{
private:
  static void* formulaCache;
  void* pExecutor;

protected:
  template <typename T>
  T operator()(IUnitFormulaPars const*, IUnitFormulaPars const*, IMiscFormulaPars const*, T defaultValue) const
  {
    return defaultValue;
  }

  template <typename T>
  T operator()(IUnitFormulaPars const*, ICustomFormulaPars const*, IMiscFormulaPars const*, T defaultValue) const
  {
    return defaultValue;
  }

  template <typename T>
  T Execute(T defaultValue) const
  {
    return defaultValue;
  }

public:
  string sString;
  string compiledString;
  string returnType;

  ExecutableString();

  int operator&(IBinSaver& saver);
  int operator&(IXmlSaver& saver);

  DWORD CalcCheckSum() const { return 0; }

  virtual bool GetVariantValue(NScript::VariantValue& value, const char* key) const;
  ExecutableString& operator=(const ExecutableString& other);
};

struct ExecutableFloatString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  float operator()(IUnitFormulaPars const* pFirst, IUnitFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, float defaultValue = 0.0f) const
  {
    return ExecutableString::operator()<float>(pFirst, pSecond, pMisc, defaultValue);
  }
};

struct ExecutableBoolString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  bool operator()(IUnitFormulaPars const* pFirst, IUnitFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, bool defaultValue = false) const
  {
    return ExecutableString::operator()<bool>(pFirst, pSecond, pMisc, defaultValue);
  }
};

struct ExecutableBooleanString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  bool operator()(IUnitFormulaPars const* pFirst, ICustomFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, bool defaultValue = false) const
  {
    return ExecutableString::operator()<bool>(pFirst, pSecond, pMisc, defaultValue);
  }
};

struct ExecutableIntString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  int operator()(IUnitFormulaPars const* pFirst, IUnitFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, int defaultValue = 0) const
  {
    return ExecutableString::operator()<int>(pFirst, pSecond, pMisc, defaultValue);
  }
};
