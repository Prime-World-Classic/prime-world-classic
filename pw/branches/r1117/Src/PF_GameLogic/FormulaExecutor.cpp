/*
* FormulaExecutor.cpp
*
*  Created on: 19.01.2009  Author: earnol
*/

#include "stdafx.h"

#include "FormulaExecutor.h"
#include "System/Base64.h"
#include "stdafx.h"

FormulaExecutor::FormulaExecutor(char const *cpBase64String): DataExecutor(FORMULA_VERSION, cpBase64String)
{
}

FormulaExecutor::FormulaExecutor(unsigned char const *dataBuffer, unsigned int nBufferSize): DataExecutor(FORMULA_VERSION, dataBuffer, nBufferSize)
{
}

#ifdef FORMULA_USE_VM

int FormulaExecutor::s_nVmCompiled = 0;
int FormulaExecutor::s_nVmFailed   = 0;
static int s_nVmNoSource = 0;

// Runtime errors of the VM are reported to the log a limited number of times:
// a battle calls formulas thousands of times per second. Division by zero is
// legitimate content behaviour (the x86 blob produced an inf as well), so only
// "hard" errors (FM_ERR_HARD and above: null self, structural/stack errors) are
// reported. See FormulaVM::Run for the codes.
static const int FM_ERR_HARD = 4;
static int s_nVmRuntimeReports = 0;

static void ReportVmRuntimeError(FormulaVM const * pVM)
{
  int nErr = pVM ? pVM->GetRuntimeError() : 0;
  if (nErr < FM_ERR_HARD)
    return;
  if (s_nVmRuntimeReports < 20)
  {
    ++s_nVmRuntimeReports;
    DebugTrace("FormulaVM: runtime error %d in \"%s\" (%d reported)",
               nErr, pVM->GetSource() ? pVM->GetSource() : "?", s_nVmRuntimeReports);
  }
}

FormulaExecutor::FormulaExecutor(char const * /*cpBase64String*/, char const *cpSource): DataExecutor(0, (unsigned char const *)NULL, 0), pVM(NULL)
{
  if (cpSource != NULL && *cpSource != '\0')
    pVM = FormulaVM::Compile(cpSource, NULL);
  else
  {
    // Content case found by the audit: 62 formulas have a compiled x86 blob but
    // an empty sString, i.e. there is nothing to compile on x64 - the formula
    // stays inert (x86 executes the blob). Reported, not fixed here: the fix
    // belongs to the content (restore sString), see PLAN_client_modern.md.
    if (s_nVmNoSource < 20)
      DebugTrace("FormulaVM: compiledString without sString, formula left inert (%d so far)", ++s_nVmNoSource);
    else
      ++s_nVmNoSource;
  }

  if (pVM != NULL && !pVM->IsValid())
  {
    // Content is data, not code: an unparsable formula must be visible in the log
    // (shipping compiles NI_* asserts out) and must not kill the client - the
    // loader reports it through IsValid()==false exactly like the x86 path does
    // for a broken blob.
    ++s_nVmFailed;
    NI_DATA_ALWAYS_ASSERT(NStr::StrFmt("Formula \"%s\" could not be compiled: %s",
                                       cpSource, pVM->GetError() ? pVM->GetError() : "?"));
    DebugTrace("FormulaVM: cannot compile \"%s\": %s (%d failed so far)",
               cpSource, pVM->GetError() ? pVM->GetError() : "?", s_nVmFailed);
    delete pVM;
    pVM = NULL;
  }
  else if (pVM != NULL)
  {
    // positive signal for E2E: formulas really are compiled into the VM
    if ((++s_nVmCompiled % 2000) == 0)
      DebugTrace("FormulaVM: %d formulas compiled", s_nVmCompiled);
  }
}

FormulaExecutor::~FormulaExecutor()
{
  delete pVM;
  pVM = NULL;
}

float FormulaExecutor::ExecFloat(void const *pFirst, void const *pSecond, void const *pMisc) const
{
  float fResult = pVM != NULL ? pVM->ExecuteFloat(pFirst, pSecond, pMisc) : 0.0f;
  if (pVM != NULL) ReportVmRuntimeError(pVM);
  return fResult;
}

int FormulaExecutor::ExecInt(void const *pFirst, void const *pSecond, void const *pMisc) const
{
  int nResult = pVM != NULL ? pVM->ExecuteInt(pFirst, pSecond, pMisc) : 0;
  if (pVM != NULL) ReportVmRuntimeError(pVM);
  return nResult;
}

bool FormulaExecutor::ExecBool(void const *pFirst, void const *pSecond, void const *pMisc) const
{
  bool bResult = pVM != NULL ? pVM->ExecuteBool(pFirst, pSecond, pMisc) : false;
  if (pVM != NULL) ReportVmRuntimeError(pVM);
  return bResult;
}

template <> float FormulaExecutor::ExecVM<float>(void const *pFirst, void const *pSecond, void const *pMisc) const
{ return ExecFloat(pFirst, pSecond, pMisc); }

template <> int FormulaExecutor::ExecVM<int>(void const *pFirst, void const *pSecond, void const *pMisc) const
{ return ExecInt(pFirst, pSecond, pMisc); }

template <> bool FormulaExecutor::ExecVM<bool>(void const *pFirst, void const *pSecond, void const *pMisc) const
{ return ExecBool(pFirst, pSecond, pMisc); }
#endif

//REGISTER_SAVELOAD_CLASS( FormulaExecutor )
