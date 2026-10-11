/*
 * FormulaExecutor.h
 *
 *  Created on: 19.01.2009  Author: earnol
 *
 *  x64: <compiledString> is precompiled x86 machine code and cannot be executed
 *  in a 64-bit process, so on x64 the formula is compiled from <sString> into
 *  FormulaVM byte code instead (Src/System/FormulaVM.h). Bit exactness of the VM
 *  is gated by Tools/FormulaCheck/harness (reference = "the same C++ compiled
 *  for x64": MATCH=10824, DIFF=0), see its README.
 */

#ifndef FORMULAEXECUTOR_H_
#define FORMULAEXECUTOR_H_
#include "../../Data/GameLogic/FormulaPars.h"

#include "../System/DataExecutor.h"

#if defined(_M_X64) || defined(__x86_64__)
  #define FORMULA_USE_VM 1
  #include "../System/FormulaVM.h"
#endif

class FormulaExecutor: public DataExecutor, public CObjectBase
{
  OBJECT_METHODS(0x11621CF2, FormulaExecutor);
public:

  FormulaExecutor(unsigned char const *dataBuffer, unsigned int nBufferSize);
  FormulaExecutor(char const *cpBase64String);
#ifdef FORMULA_USE_VM
  // cpSource = sString: on x64 this is what actually gets compiled
  FormulaExecutor(char const *cpBase64String, char const *cpSource);
  // 3-argument DataExecutor ctor on purpose: DataExecutor(0, NULL) would bind to
  // the (unsigned char, char const *) overload and strlen(NULL) (see the .cpp)
  FormulaExecutor(): DataExecutor(0, (unsigned char const *)NULL, 0), pVM(NULL){;}
  ~FormulaExecutor();

  // hides DataExecutor::IsValid (it is not virtual): on x64 there is no blob
  bool IsValid() { return pVM != NULL && pVM->IsValid(); }
  char const * GetFormulaError() const { return pVM ? pVM->GetError() : NULL; }

  // load time statistics: shipping compiles NI_* asserts out, so formula
  // problems have to be visible in the log instead (see the .cpp)
  static int GetVmCompiled() { return s_nVmCompiled; }
  static int GetVmFailed()   { return s_nVmFailed; }
#else
  FormulaExecutor(): DataExecutor(0, NULL){;}
#endif

  template <typename T>
  T Execute(IUnitFormulaPars const *pFirst, IUnitFormulaPars const *pSecond, IMiscFormulaPars const *pMisc) const
  {
#ifdef FORMULA_USE_VM
    return ExecVM<T>((void const *)pFirst, (void const *)pSecond, (void const *)pMisc);
#else
    return DataExecutor::Execute<T>((void const *)pFirst, (void const *)pSecond, (void const *)pMisc);
#endif
  }

  template <typename T>
  T Execute(IUnitFormulaPars const *pFirst, ICustomFormulaPars const *pSecond, IMiscFormulaPars const *pMisc) const
  {
#ifdef FORMULA_USE_VM
    return ExecVM<T>((void const *)pFirst, (void const *)pSecond, (void const *)pMisc);
#else
    return DataExecutor::Execute<T>((void const *)pFirst, (void const *)pSecond, (void const *)pMisc);
#endif
  }

#ifdef FORMULA_USE_VM
private:
  FormulaVM * pVM;

  static int s_nVmCompiled;
  static int s_nVmFailed;

  template <typename T>
  T ExecVM(void const *pFirst, void const *pSecond, void const *pMisc) const
  {
    NI_ASSERT(false, "FormulaExecutor::ExecVM: unsupported return type");
    return T(0);
  }
  float ExecFloat(void const *pFirst, void const *pSecond, void const *pMisc) const;
  int   ExecInt  (void const *pFirst, void const *pSecond, void const *pMisc) const;
  bool  ExecBool (void const *pFirst, void const *pSecond, void const *pMisc) const;
#endif
};

#ifdef FORMULA_USE_VM
// explicit specializations, defined in FormulaExecutor.cpp
template <> float FormulaExecutor::ExecVM<float>(void const *, void const *, void const *) const;
template <> int   FormulaExecutor::ExecVM<int  >(void const *, void const *, void const *) const;
template <> bool  FormulaExecutor::ExecVM<bool >(void const *, void const *, void const *) const;
#endif

#endif /* FORMULAEXECUTOR_H_ */
