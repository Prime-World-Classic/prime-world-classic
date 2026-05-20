#pragma once

#include <stdlib.h>
#include <string.h>

#include "../../Data/GameLogic/FormulaPars.h"

class IBinSaver;
struct IXmlSaver;

namespace NScript
{
class VariantValue;
}

inline const char* SkipExecutableStringSpaces(const char* p)
{
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    ++p;
  return p;
}

inline bool ParseExecutableFloatLiteral(const string& text, float& value)
{
  const char* begin = SkipExecutableStringSpaces(text.c_str());
  if (!*begin)
    return false;

  char* end = 0;
  const double parsed = strtod(begin, &end);
  if (end == begin)
    return false;

  end = const_cast<char*>(SkipExecutableStringSpaces(end));
  if (*end == 'f' || *end == 'F')
    end = const_cast<char*>(SkipExecutableStringSpaces(end + 1));
  if (*end)
    return false;

  value = static_cast<float>(parsed);
  return true;
}

inline bool ParseExecutableIntLiteral(const string& text, int& value)
{
  const char* begin = SkipExecutableStringSpaces(text.c_str());
  if (!*begin)
    return false;

  char* end = 0;
  const long parsed = strtol(begin, &end, 10);
  if (end == begin)
    return false;

  end = const_cast<char*>(SkipExecutableStringSpaces(end));
  if (*end)
    return false;

  value = static_cast<int>(parsed);
  return true;
}

inline bool ParseExecutableBoolLiteral(const string& text, bool& value)
{
  const char* begin = SkipExecutableStringSpaces(text.c_str());
  if (!strcmp(begin, "true") || !strcmp(begin, "True") || !strcmp(begin, "TRUE"))
  {
    value = true;
    return true;
  }
  if (!strcmp(begin, "false") || !strcmp(begin, "False") || !strcmp(begin, "FALSE"))
  {
    value = false;
    return true;
  }

  int intValue = 0;
  if (ParseExecutableIntLiteral(text, intValue))
  {
    value = intValue != 0;
    return true;
  }
  return false;
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
    float parsed = 0.0f;
    return ParseExecutableFloatLiteral(sString, parsed) ? parsed : ExecutableString::operator()<float>(pFirst, pSecond, pMisc, defaultValue);
  }
};

struct ExecutableBoolString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  bool operator()(IUnitFormulaPars const* pFirst, IUnitFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, bool defaultValue = false) const
  {
    bool parsed = false;
    return ParseExecutableBoolLiteral(sString, parsed) ? parsed : ExecutableString::operator()<bool>(pFirst, pSecond, pMisc, defaultValue);
  }
};

struct ExecutableBooleanString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  bool operator()(IUnitFormulaPars const* pFirst, ICustomFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, bool defaultValue = false) const
  {
    bool parsed = false;
    return ParseExecutableBoolLiteral(sString, parsed) ? parsed : ExecutableString::operator()<bool>(pFirst, pSecond, pMisc, defaultValue);
  }
};

struct ExecutableIntString : public ExecutableString
{
  int operator&(IBinSaver& saver) { return ExecutableString::operator&(saver); }
  int operator&(IXmlSaver& saver) { return ExecutableString::operator&(saver); }
  int operator()(IUnitFormulaPars const* pFirst, IUnitFormulaPars const* pSecond, IMiscFormulaPars const* pMisc, int defaultValue = 0) const
  {
    int parsed = 0;
    return ParseExecutableIntLiteral(sString, parsed) ? parsed : ExecutableString::operator()<int>(pFirst, pSecond, pMisc, defaultValue);
  }
};
