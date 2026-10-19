/*
 * native_exec.cpp — нативная (x86) ветка golden-диффа: исполняет предкомпилированный
 * blob из compiledString тем же DataExecutor'ом, что и клиент.
 *
 * Компилируется только в x86-сборке обвязки (vs2008) и только с /DHARNESS_NATIVE.
 * Включает systemStdAfx так же, как системные .cpp клиента (иначе цепочка
 * Logger/Dumper заголовков не собирается).
 */
#include "stdafx.h"
#include "DataExecutor.h"
#include "FormulaPars.h"        // FORMULA_VERSION

// заглушки минимального набора системных модулей (полный клиент в обвязку не линкуем)
void TraceMsg(const char *) {}
namespace NObjectFactory
{
  void RegisterType(int, ObjectFactoryNewFunc, const std::type_info *) {}
  void RegisterType(int, ObjectFactoryNewFunc, const std::type_info *, const char *) {}
  void StartRegister() {}
}

extern "C" int HarnessNativeRun(char const * cpRetType, char const * cpBase64,
                                void const * pFirst, void const * pSecond, void const * pMisc,
                                double * pResOut)
{
  if (!cpBase64 || !*cpBase64) return -1;          // нет предкомпилированного кода
  DataExecutor exec(FORMULA_VERSION, cpBase64);
  if (!exec.IsValid()) return -2;                  // blob не распознан

  if (!strcmp(cpRetType, "int"))
    *pResOut = (double)exec.Execute<int>(pFirst, pSecond, pMisc);
  else if (!strcmp(cpRetType, "bool") || !strcmp(cpRetType, "boolean"))
    *pResOut = exec.Execute<bool>(pFirst, pSecond, pMisc) ? 1.0 : 0.0;
  else
    *pResOut = (double)exec.Execute<float>(pFirst, pSecond, pMisc);
  return 0;
}
