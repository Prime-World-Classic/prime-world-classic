/*
 * FormulaVM.cpp — компилятор DSL формул в байткод + интерпретатор.
 * Зачем — FormulaVM.h; словарь аксессоров — FormulaAccessors.inc
 * (генерируется Tools/FormulaCheck/gen_accessors.py из FormulaPars.h).
 *
 * Правила разбора соответствуют Src/FormulaBuilder/FormulaBuilder.cpp::
 * PrepareCFile (там подстановки — регулярки по тексту, здесь — те же правила на
 * уровне токенов) и макросам/инлайнам Data/GameLogic/FormulaPars.h. Осознанные
 * отличия помечены «ОТЛИЧИЕ».
 *
 * Компилируется и под x86, и под x64 (аксессоры вызываются через vtable-слот,
 * как в предкомпилированном коде) — golden-дифф «VM vs нативный код» идёт в
 * одном процессе.
 */

#include "FormulaVM.h"
#include "../../Data/GameLogic/FormulaPars.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef NI_ASSERT
#define NI_ASSERT(cond, msg) \
  do { if (!(cond)) fprintf(stderr, "FormulaVM: assert: %s (%s:%d)\n", (msg), __FILE__, __LINE__); } while (0)
#endif

// типы значений; нумерация должна совпадать с kFormulaSigRet/kFormulaSigArgType в .inc
enum FmType { FT_F = 0, FT_D = 1, FT_I = 2, FT_B = 3, FT_V = 4, FT_P = 5, FT_U = 6 };

#include "FormulaAccessors.inc"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// встроенные функции (в отличие от макросов FormulaPars.h — это реальные вызовы)
enum EFmBuiltIn
{
  BI_min = 0, BI_max, BI_clamp, BI_lerp, BI_isinbounds, BI_round, BI_f2l,
  BI_f2l_sse3, BI_floor, BI_ceil, BI_fabs, BI_sqrt, BI_sin, BI_cos, BI_pow,
  BI_fmod, BI_d, BI_t, BI_SwitchByBool, BI_SwitchByAbilityRank, BI_COUNT
};

struct BuiltInEntry
{
  char const *  cpName;
  unsigned char nArgs;
  unsigned char cRet;
  unsigned char cArg[6];
};

static const BuiltInEntry kBuiltIn[BI_COUNT] =
{
  // inline-функции FormulaPars.h (float)
  {"min",       2, FT_F, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"max",       2, FT_F, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"clamp",     3, FT_F, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"lerp",      3, FT_F, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"isinbounds",3, FT_B, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"round",     1, FT_F, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},   // ni_round
  {"f2l",       1, FT_I, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},   // усечение к нулю
  {"f2l_sse3",  1, FT_I, {FT_F, FT_F, FT_F, FT_F, FT_F, FT_F}},   // ОТЛИЧИЕ: внутреннее
                                                                  // имя встречается в
                                                                  // контенте
  // link-таблица ExecutionMemoryManager: double-версии C-математики
  {"floor",     1, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"ceil",      1, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"fabs",      1, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"sqrt",      1, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"sin",       1, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"cos",       1, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"pow",       2, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"fmod",      2, FT_D, {FT_D, FT_D, FT_D, FT_D, FT_D, FT_D}},
  {"d",         2, FT_F, {FT_V, FT_V, FT_F, FT_F, FT_F, FT_F}},   // inline float
  {"t",         4, FT_F, {FT_I, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"SwitchByBool",        3, FT_F, {FT_B, FT_F, FT_F, FT_F, FT_F, FT_F}},
  {"SwitchByAbilityRank", 5, FT_F, {FT_I, FT_F, FT_F, FT_F, FT_F, FT_F}},
};

// интерфейс, к которому относится значение-указатель
enum FmRecv { FR_NONE = 0, FR_UNIT, FR_MISC, FR_CUSTOM };

// какой интерфейс у результата аксессора (для цепочек вида .oTarget->meth()):
// берётся из объявленного типа возврата и лежит в сгенерированной таблице
// (ручной список имён врал: GetObject/GetObjectOwner возвращают unit, а не misc).
static int KindOfPtrResult(int nAccIdx) { return kFormulaAccKind[nAccIdx]; }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// лексер
enum EFmTok { TK_EOF = 0, TK_ID, TK_DOL, TK_NUM, TK_STR, TK_PUNCT };

struct Tok
{
  int    nKind;
  char   id[96];
  char   str[512];
  char   ch[4];
  double num;
  int    numIsInt;
  int    numIsF;      // суффикс f/F -> float; без суффикса (с точкой/экспонентой) — double
};

class FmLexer
{
public:
  FmLexer(char const * cpSrc) : m_cpStart(cpSrc), m_cpP(cpSrc), m_bPeek(false)
  {
    memset(&m_cur, 0, sizeof(m_cur));
    memset(&m_peek, 0, sizeof(m_peek));
    Next();
  }

  void        Next() { if (m_bPeek) { m_cur = m_peek; m_bPeek = false; } else Raw(m_cur); }
  Tok const & Cur()  const { return m_cur; }
  Tok const & Peek()
  {
    if (!m_bPeek) { Raw(m_peek); m_bPeek = true; }
    return m_peek;
  }
  int  CurCh()  const { return m_cur.nKind == TK_PUNCT ? m_cur.ch[0] : -1; }
  int  CurCh2() const { return m_cur.nKind == TK_PUNCT ? m_cur.ch[1] : 0; }
  int  PeekCh()   { Tok const & t = Peek(); return t.nKind == TK_PUNCT ? t.ch[0] : -1; }
  int  PeekKind() { return Peek().nKind; }

private:
  static int IsIdCh(char c) { return isalnum((unsigned char)c) || c == '_'; }

  void SkipSpace()
  {
    for (;;)
    {
      while (*m_cpP && isspace((unsigned char)*m_cpP)) ++m_cpP;
      if (m_cpP[0] == '/' && m_cpP[1] == '/')
      {
        while (*m_cpP && *m_cpP != '\n') ++m_cpP;
        continue;
      }
      if (m_cpP[0] == '/' && m_cpP[1] == '*')
      {
        m_cpP += 2;
        while (*m_cpP && !(m_cpP[0] == '*' && m_cpP[1] == '/')) ++m_cpP;
        if (*m_cpP) m_cpP += 2;
        continue;
      }
      break;
    }
  }

  void Raw(Tok & t)
  {
    memset(&t, 0, sizeof(t));
    SkipSpace();
    char c = *m_cpP;
    if (c == 0) { t.nKind = TK_EOF; return; }

    if (c == '$')
    {
      ++m_cpP;
      int n = 0;
      while (IsIdCh(*m_cpP) && n < (int)sizeof(t.id) - 1) t.id[n++] = *m_cpP++;
      t.id[n] = 0;
      t.nKind = TK_DOL;
      return;
    }
    if (isalpha((unsigned char)c) || c == '_')
    {
      int n = 0;
      while (IsIdCh(*m_cpP) && n < (int)sizeof(t.id) - 1) t.id[n++] = *m_cpP++;
      t.id[n] = 0;
      t.nKind = TK_ID;
      return;
    }
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)m_cpP[1])))
    {
      char buf[64];
      int n = 0;
      while (n < (int)sizeof(buf) - 8 &&
             (isdigit((unsigned char)*m_cpP) || *m_cpP == '.' || *m_cpP == 'e' || *m_cpP == 'E' ||
              ((*m_cpP == '+' || *m_cpP == '-') && (m_cpP[-1] == 'e' || m_cpP[-1] == 'E'))))
        buf[n++] = *m_cpP++;
      buf[n] = 0;
      t.numIsInt = 1;
      t.numIsF = 0;
      if (*m_cpP == 'f' || *m_cpP == 'F') { t.numIsInt = 0; t.numIsF = 1; ++m_cpP; }
      else
      {
        while (*m_cpP == 'u' || *m_cpP == 'U' || *m_cpP == 'l' || *m_cpP == 'L') ++m_cpP;
        if (strchr(buf, '.') || strchr(buf, 'e') || strchr(buf, 'E')) t.numIsInt = 0;
      }
      t.num = atof(buf);
      t.nKind = TK_NUM;
      return;
    }
    if (c == '"')
    {
      ++m_cpP;
      int n = 0;
      while (*m_cpP && *m_cpP != '"' && n < (int)sizeof(t.str) - 1)
      {
        if (*m_cpP == '\\' && m_cpP[1]) ++m_cpP;
        t.str[n++] = *m_cpP++;
      }
      t.str[n] = 0;
      if (*m_cpP == '"') ++m_cpP;
      t.nKind = TK_STR;
      return;
    }
    static const char * const kTwo[] = {"==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "::", "%%", 0};
    for (int i = 0; kTwo[i]; ++i)
    {
      if (m_cpP[0] == kTwo[i][0] && m_cpP[1] == kTwo[i][1])
      {
        m_cpP += 2;
        if (!strcmp(kTwo[i], "%%")) t.ch[0] = '%';                 // ОТЛИЧИЕ: %% == %
        else { t.ch[0] = kTwo[i][0]; t.ch[1] = kTwo[i][1]; }
        t.nKind = TK_PUNCT;
        return;
      }
    }
    t.ch[0] = c;
    ++m_cpP;
    t.nKind = TK_PUNCT;
  }

  char const * m_cpStart;
  char const * m_cpP;
  Tok          m_cur;
  Tok          m_peek;
  bool         m_bPeek;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// компилятор: один проход, байткод сразу; типы считаются статически (как в C++)
class FormulaVMCompiler
{
public:
  FormulaVMCompiler(FormulaVM & vm, char const * cpSrc)
    : m_vm(vm), m_lex(cpSrc), m_bErr(false), m_lastType(FT_I), m_lastKind(FR_NONE)
  {
    m_err[0] = 0;
  }

  bool Compile()
  {
    ParseExpr();
    // оператор запятой C++ (в контенте есть опечатки вида «0,1» — скомпилированный
    // формулой blob даёт именно 1, т.е. левый операнд отбрасывается)
    while (m_lex.CurCh() == ',')
    {
      m_lex.Next();
      ParseExpr();
      E(FormulaVM::OP_COMMA);
    }
    while (m_lex.CurCh() == ';') m_lex.Next();      // в контенте бывают «3 ;// tooltip»
    if (m_lex.Cur().nKind != TK_EOF) Err("лишние символы после выражения");
    E(FormulaVM::OP_HALT);
    return !m_bErr;
  }

  char const * ErrMsg() const { return m_err; }

private:
  typedef int (FormulaVMCompiler::*ParseFn)();

  int  E(int nOp, int nA = 0, int nB = 0) { return m_vm.Emit(nOp, nA, nB); }
  int  Here() const { return m_vm.OpCount(); }
  void Patch(int nOpIdx, int nTarget) { m_vm.PatchJump(nOpIdx, nTarget); }
  void PatchC(int nOpIdx, int nType)  { m_vm.PatchConv(nOpIdx, nType); }

  void Err(char const * cpWhat)
  {
    if (m_bErr) return;
    m_bErr = true;
    sprintf(m_err, "%s (рядом с «%s»)", cpWhat,
            m_lex.Cur().nKind == TK_PUNCT ? m_lex.Cur().ch : m_lex.Cur().id);
  }

  static int CommonType(int a, int b)
  {
    if (a == b) return a;
    if (a == FT_D || b == FT_D) return FT_D;
    if (a == FT_F || b == FT_F) return FT_F;
    if (a == FT_P || b == FT_P) return FT_P;
    if (a == FT_V || b == FT_V) return FT_V;
    return FT_I;
  }

  void ConvTo(int nWant)
  {
    int have = m_lastType;
    if (have == nWant) return;
    bool ok = (nWant == FT_B && (have == FT_F || have == FT_D || have == FT_I || have == FT_P || have == FT_U))
           || (nWant == FT_D && (have == FT_F || have == FT_I || have == FT_B))
           || (nWant == FT_F && (have == FT_D || have == FT_I || have == FT_B))
           || (nWant == FT_I && (have == FT_D || have == FT_F || have == FT_B))
           || (nWant == FT_P && have == FT_U);
    if (!ok) { Err("несовместимый тип"); return; }
    E(FormulaVM::OP_CONV, have, nWant);
    m_lastType = nWant;
  }

  static int FindAcc(int nIface, char const * cpName)
  {
    for (int i = 0; i < kFormulaAccCount; ++i)
      if (kFormulaAcc[i].nIface == nIface && !strcmp(kFormulaAcc[i].cpName, cpName)) return i;
    return -1;
  }
  static int FindEnum(char const * cpName, int & nVal)
  {
    for (int i = 0; i < kFormulaEnumCount; ++i)
      if (!strcmp(kFormulaEnum[i].cpName, cpName)) { nVal = kFormulaEnum[i].nValue; return 1; }
    return 0;
  }
  static int FindBuiltIn(char const * cpName)
  {
    for (int i = 0; i < BI_COUNT; ++i)
      if (!strcmp(kBuiltIn[i].cpName, cpName)) return i;
    return -1;
  }

  // ВАЖНО: Cur().id — буфер лексера, который Next() перезаписывает; имя,
  // нужное после Next(), обязательно копируем.
  char const * TakeId()
  {
    strncpy(m_idBuf, m_lex.Cur().id, sizeof(m_idBuf) - 1);
    m_idBuf[sizeof(m_idBuf) - 1] = 0;
    return m_idBuf;
  }

  void PushSelf()   { E(FormulaVM::OP_PUSHSELF);   m_lastType = FT_P; m_lastKind = FR_UNIT; }
  void PushSecond() { E(FormulaVM::OP_PUSHSECOND); m_lastType = FT_P; m_lastKind = FR_UNIT; }
  void PushMisc()   { E(FormulaVM::OP_PUSHMISC);   m_lastType = FT_P; m_lastKind = FR_MISC; }

  // receiver на стеке, строковый аргумент кладётся здесь
  void EmitAccStr(int nAccIdx, char const * cpStr)
  {
    E(FormulaVM::OP_PUSHSTR, m_vm.AddString(cpStr));
    E(FormulaVM::OP_ACC, nAccIdx);
    m_lastType = kFormulaSigRet[kFormulaAcc[nAccIdx].nSig];
    m_lastKind = KindOfPtrResult(nAccIdx);
  }

  int  RecvIface() const
  {
    if (m_lastKind == FR_MISC)   return FI_MISC;
    if (m_lastKind == FR_CUSTOM) return FI_CUSTOM;
    return FI_UNIT;
  }

  // вызов метода без аргументов (receiver на стеке)
  int CallNoArg(char const * cpName)
  {
    int nIface = RecvIface();
    int idx = FindAcc(nIface, cpName);
    if (idx < 0) idx = FindAcc(FI_UNIT, cpName);
    if (idx < 0) { Err("нет такого метода у интерфейса формулы"); return FT_I; }
    if (kFormulaSigArgs[kFormulaAcc[idx].nSig] != 0) { Err("метод требует аргументы"); return FT_I; }
    E(FormulaVM::OP_ACC, idx);
    m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
    m_lastKind = KindOfPtrResult(idx);
    return m_lastType;
  }

  // вызов метода с аргументами: '(' уже съеден, аргументы парсятся здесь
  int CallWithArgs(char const * cpName)
  {
    int nIface = RecvIface();
    int idx = FindAcc(nIface, cpName);
    if (idx < 0) { Err("нет такого метода у интерфейса формулы"); return FT_I; }
    int nSig  = kFormulaAcc[idx].nSig;
    int nArgs = kFormulaSigArgs[nSig];
    for (int i = 0; i < nArgs; ++i)
    {
      if (i)
      {
        if (m_lex.CurCh() != ',') { Err("ожидалась ',' м/у аргументами"); break; }
        m_lex.Next();
      }
      if (kFormulaSigArgType[nSig * 6 + i] == FT_P)
      {
        // указательные аргументы в DSL пишутся как pFirst/pSecond (не выражение)
        // «deref» после имени (pMisc->GetObjectParent()) — это уже выражение,
        // быстрая ветка по имени неприменима
        int nPk = m_lex.PeekCh();
        bool bDeref = (nPk == '.' || nPk == '-');
        if (!bDeref && m_lex.Cur().nKind == TK_ID && !strcmp(m_lex.Cur().id, "pFirst")) { m_lex.Next(); PushSelf(); }
        else if (!bDeref && m_lex.Cur().nKind == TK_ID && !strcmp(m_lex.Cur().id, "pSecond")) { m_lex.Next(); PushSecond(); }
        else if (!bDeref && m_lex.Cur().nKind == TK_ID && !strcmp(m_lex.Cur().id, "pMisc")) { m_lex.Next(); PushMisc(); }
        else if (m_lex.CurCh() == '0') { m_lex.Next(); E(FormulaVM::OP_PUSHNULL); m_lastType = FT_U; }
        else { ParseExpr(); }
        ConvTo(FT_P);
      }
      else
      {
        ParseExpr();
        ConvTo(kFormulaSigArgType[nSig * 6 + i]);
      }
    }
    if (m_lex.CurCh() != ')') { Err("ожидалась ')'"); return FT_I; }
    m_lex.Next();
    E(FormulaVM::OP_ACC, idx);
    m_lastType = kFormulaSigRet[nSig];
    m_lastKind = KindOfPtrResult(idx);
    return m_lastType;
  }

  // ---------- уровни приоретета ----------
  int ParseExpr() { return ParseTernary(); }

  int ParseTernary()
  {
    int t = ParseLor();
    if (m_lex.CurCh() != '?') return t;
    m_lex.Next();
    ConvTo(FT_B);
    int jz  = E(FormulaVM::OP_JZ, 0);
    int tA  = ParseExpr();
    int cvA = E(FormulaVM::OP_CONV, tA, -1);
    if (m_lex.CurCh() != ':') { Err("в тернарном выражении нет ':'"); return tA; }
    m_lex.Next();
    int jend = E(FormulaVM::OP_JMP, 0);
    Patch(jz, Here());
    int tB  = ParseExpr();
    int cvB = E(FormulaVM::OP_CONV, tB, -1);
    Patch(jend, Here());
    int common = CommonType(tA, tB);
    PatchC(cvA, common);
    PatchC(cvB, common);
    m_lastType = common;
    return common;
  }

  int ParseLor()
  {
    int t = ParseLand();
    while (m_lex.CurCh() == '|' && m_lex.CurCh2() == '|')
    {
      m_lex.Next();                                  // токен '||' целиком
      ConvTo(FT_B);
      int jtrue = E(FormulaVM::OP_JNZ, 0);
      ParseLand();
      ConvTo(FT_B);
      int jend = E(FormulaVM::OP_JMP, 0);
      Patch(jtrue, Here());
      E(FormulaVM::OP_PUSHB, 1);
      Patch(jend, Here());
      m_lastType = FT_B;
      t = FT_B;
    }
    return t;
  }

  int ParseLand()
  {
    int t = ParseBitor();
    while (m_lex.CurCh() == '&' && m_lex.CurCh2() == '&')
    {
      m_lex.Next();                                  // токен '&&' целиком
      ConvTo(FT_B);
      int jfalse = E(FormulaVM::OP_JZ, 0);
      ParseBitor();
      ConvTo(FT_B);
      int jend = E(FormulaVM::OP_JMP, 0);
      Patch(jfalse, Here());
      E(FormulaVM::OP_PUSHB, 0);
      Patch(jend, Here());
      m_lastType = FT_B;
      t = FT_B;
    }
    return t;
  }

  int ParseBitor()
  {
    int t = ParseBitxor();
    while (m_lex.CurCh() == '|' && m_lex.CurCh2() == 0)
    {
      m_lex.Next();
      int t2 = ParseBitxor();
      if (t == FT_F || t2 == FT_F || t == FT_D || t2 == FT_D || t == FT_P || t2 == FT_P)
      { Err("| над нецелым"); break; }
      E(FormulaVM::OP_BOR, t, t2);
      m_lastType = FT_I; t = FT_I;
    }
    return t;
  }

  int ParseBitxor()
  {
    int t = ParseBitand();
    while (m_lex.CurCh() == '^')
    {
      m_lex.Next();
      int t2 = ParseBitand();
      E(FormulaVM::OP_BXOR, t, t2);
      m_lastType = FT_I; t = FT_I;
    }
    return t;
  }

  int ParseBitand()
  {
    int t = ParseEquality();
    while (m_lex.CurCh() == '&' && m_lex.CurCh2() == 0)
    {
      m_lex.Next();
      int t2 = ParseEquality();
      if (t == FT_F || t2 == FT_F || t == FT_D || t2 == FT_D || t == FT_P || t2 == FT_P)
      { Err("& над нецелым"); break; }
      E(FormulaVM::OP_BAND, t, t2);
      m_lastType = FT_I; t = FT_I;
    }
    return t;
  }

  int ParseEquality()
  {
    int t = ParseRelational();
    for (;;)
    {
      Tok const & c = m_lex.Cur();
      int nOp = -1;
      if (c.nKind == TK_PUNCT)
      {
        if      (!strcmp(c.ch, "==")) nOp = FormulaVM::OP_EQ;
        else if (!strcmp(c.ch, "!=")) nOp = FormulaVM::OP_NE;
      }
      if (nOp < 0) break;
      m_lex.Next();
      int t2 = ParseRelational();
      E(nOp, t, t2);
      m_lastType = FT_B; m_lastKind = FR_NONE;
      t = FT_B;
    }
    return t;
  }

  int ParseRelational()
  {
    int t = ParseAdditive();
    for (;;)
    {
      Tok const & c = m_lex.Cur();
      int nOp = -1;
      if (c.nKind == TK_PUNCT)
      {
        if      (!strcmp(c.ch, "<"))  nOp = FormulaVM::OP_LT;
        else if (!strcmp(c.ch, "<=")) nOp = FormulaVM::OP_LE;
        else if (!strcmp(c.ch, ">"))  nOp = FormulaVM::OP_GT;
        else if (!strcmp(c.ch, ">=")) nOp = FormulaVM::OP_GE;
      }
      if (nOp < 0) break;
      m_lex.Next();
      int t2 = ParseAdditive();
      if (t == FT_P || t2 == FT_P) { Err("указатели сравниваются только на ==/!="); break; }
      E(nOp, t, t2);
      m_lastType = FT_B; m_lastKind = FR_NONE;
      t = FT_B;
    }
    return t;
  }

  int ParseAdditive()
  {
    int t = ParseMul();
    for (;;)
    {
      int c = m_lex.CurCh();
      int nOp = (c == '+') ? FormulaVM::OP_ADD : ((c == '-') ? FormulaVM::OP_SUB : -1);
      if (nOp < 0) break;
      m_lex.Next();
      int t2 = ParseMul();
      if (t == FT_V || t2 == FT_V) { Err("векторы не складываются"); break; }
      if (t == FT_P || t2 == FT_P) { Err("указатели не складываются"); break; }
      E(nOp, t, t2);
      m_lastType = CommonType(t, t2);
      t = m_lastType;
    }
    return t;
  }

  int ParseMul()
  {
    int t = ParseUnary();
    for (;;)
    {
      int c = m_lex.CurCh();
      int nOp = (c == '*') ? FormulaVM::OP_MUL : ((c == '/') ? FormulaVM::OP_DIV : ((c == '%') ? FormulaVM::OP_MOD : -1));
      if (nOp < 0) break;
      m_lex.Next();
      int t2 = ParseUnary();
      if (t == FT_V || t2 == FT_V || t == FT_P || t2 == FT_P) { Err("нельзя умножать вектор/указатель"); break; }
      E(nOp, t, t2);
      m_lastType = CommonType(t, t2);
      t = m_lastType;
    }
    return t;
  }

  int ParseUnary()
  {
    int c = m_lex.CurCh();
    if (c == '!')
    {
      m_lex.Next();
      int t = ParseUnary();
      E(FormulaVM::OP_NOT, t);
      m_lastType = FT_B;
      return FT_B;
    }
    if (c == '-')
    {
      m_lex.Next();
      int t = ParseUnary();
      E(FormulaVM::OP_NEG, t);
      m_lastType = (t == FT_D) ? FT_D : ((t == FT_F) ? FT_F : FT_I);
      return m_lastType;
    }
    if (c == '+') { m_lex.Next(); return ParseUnary(); }
    return ParsePostfix();
  }

  int ParsePostfix()
  {
    int t = ParsePrimary();
    for (;;)
    {
      int c = m_lex.CurCh();
      bool bDot  = (c == '.');
      bool bArrow = (c == '-' && m_lex.PeekCh() == '>');
      if (!bDot && !bArrow) break;
      if (bArrow) m_lex.Next();
      m_lex.Next();
      if (m_lastType != FT_P) { Err("обращение к члену не у объекта"); break; }
      t = ParseMember();
    }
    return t;
  }

  // ---------- обращение через '.' / '->' (receiver уже на стеке) ----------
  int ParseMember()
  {
    if (m_lex.Cur().nKind != TK_ID) { Err("после '.' ожидается имя"); return FT_I; }
    char const * cpName = TakeId();
    int  nNext = m_lex.PeekCh();
    bool bCall = (nNext == '(');
    int  nIface = RecvIface();

    // .vName -> GetVariable("Name")
    if (!bCall && cpName[0] == 'v' && isupper((unsigned char)cpName[1]))
    {
      m_lex.Next();
      int idx = FindAcc(nIface, "GetVariable");
      if (idx < 0) { Err("нет GetVariable"); return FT_I; }
      EmitAccStr(idx, cpName + 1);
      return m_lastType;
    }
    // .cName -> GetConstant("Name", pFirst, pSecond)
    if (!bCall && cpName[0] == 'c' && isupper((unsigned char)cpName[1]))
    {
      m_lex.Next();
      int idx = FindAcc(nIface, "GetConstant");
      if (idx < 0) { Err("нет GetConstant"); return FT_I; }
      E(FormulaVM::OP_PUSHSTR, m_vm.AddString(cpName + 1));
      PushSelf();
      PushSecond();
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      m_lastKind = FR_NONE;
      return m_lastType;
    }
    // .oName -> GetObject("Name")
    if (cpName[0] == 'o' && isupper((unsigned char)cpName[1]))
    {
      m_lex.Next();
      int idx = FindAcc(nIface, "GetObject");
      if (idx < 0) { Err("нет GetObject"); return FT_I; }
      EmitAccStr(idx, cpName + 1);
      return m_lastType;
    }
    // .applName(TYPE) -> FindApplicator("Name", pMisc, TYPE)
    if (bCall && !strncmp(cpName, "appl", 4) && isupper((unsigned char)cpName[4]))
    {
      m_lex.Next();
      if (m_lex.CurCh() == '(') m_lex.Next();
      int e = 0;
      if (m_lex.Cur().nKind == TK_ID) e = ParseEnumValue();
      else Err("аргументом applX(...) должен быть ApplicatorSearchType");
      if (m_lex.CurCh() == ')') m_lex.Next();
      int idx = FindAcc(nIface, "FindApplicator");
      if (idx < 0) { Err("нет FindApplicator"); return FT_I; }
      E(FormulaVM::OP_PUSHSTR, m_vm.AddString(cpName + 4));
      PushMisc();
      E(FormulaVM::OP_PUSHI, e);
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      m_lastKind = FR_MISC;
      return m_lastType;
    }
    if (bCall)
    {
      m_lex.Next();                                   // имя метода
      if (m_lex.CurCh() == '(') m_lex.Next();         // '('
      return CallWithArgs(cpName);
    }
    // .lowercase. -> GetObject<Name>()  (master., target., parent., favorite.)
    if (islower((unsigned char)cpName[0]) && nNext == '.')
    {
      char mname[160];
      strcpy(mname, "GetObject");
      mname[9] = (char)toupper((unsigned char)cpName[0]);
      strcpy(mname + 10, cpName + 1);
      m_lex.Next();
      return CallNoArg(mname);
    }
    // .Name -> GetName()
    {
      char mname[160];
      if (isupper((unsigned char)cpName[0])) { strcpy(mname, "Get"); strcpy(mname + 3, cpName); }
      else strcpy(mname, cpName);
      m_lex.Next();
      return CallNoArg(mname);
    }
  }

  int ParseEnumValue()
  {
    char full[192];
    strncpy(full, m_lex.Cur().id, sizeof(full) - 1);
    full[sizeof(full) - 1] = 0;
    m_lex.Next();
    if (m_lex.Cur().nKind == TK_PUNCT && !strcmp(m_lex.Cur().ch, "::"))
    {
      m_lex.Next();
      strncat(full, "::", sizeof(full) - strlen(full) - 1);
      strncat(full, m_lex.Cur().id, sizeof(full) - strlen(full) - 1);
      m_lex.Next();
    }
    int v = 0;
    if (!FindEnum(full, v)) { Err("неизвестное имя enum"); return 0; }
    return v;
  }

  // ---------- primary ----------
  int ParsePrimary()
  {
    Tok const & tk = m_lex.Cur();

    if (tk.nKind == TK_NUM)
    {
      // «0.9» в C++ — double: `float * 0.9` считается в double, и только на
      // возврате приводится к float. Float-литерал даёт расхождение в последний
      // бит почти по всему контенту (поймано golden-диффом).
      if (tk.numIsInt)    { E(FormulaVM::OP_PUSHI, (int)tk.num); m_lastType = FT_I; }
      else if (tk.numIsF) { E(FormulaVM::OP_PUSHF, m_vm.AddFloat((float)tk.num)); m_lastType = FT_F; }
      else                { E(FormulaVM::OP_PUSHDBL, m_vm.AddDouble(tk.num)); m_lastType = FT_D; }
      m_lastKind = FR_NONE;
      m_lex.Next();
      return m_lastType;
    }
    if (tk.nKind == TK_STR)
    {
      E(FormulaVM::OP_PUSHSTR, m_vm.AddString(tk.str));
      m_lastType = FT_P; m_lastKind = FR_NONE;
      m_lex.Next();
      return FT_P;
    }
    if (tk.nKind == TK_PUNCT && tk.ch[0] == '(')
    {
      m_lex.Next();
      int t = ParseExpr();
      if (m_lex.CurCh() != ')') { Err("ожидалась ')'"); return t; }
      m_lex.Next();
      return t;
    }
    if (tk.nKind == TK_DOL) return ParseDollar();
    if (tk.nKind != TK_ID)  { Err("неожиданный символ"); m_lex.Next(); return FT_I; }
    return ParseIdentifier();
  }

  int ParseDollar()
  {
    char const * cpName = TakeId();
    m_lex.Next();
    if (!strcmp(cpName, "ApplTarget"))
    {
      PushMisc();
      int idx = FindAcc(FI_MISC, "GetObject");
      EmitAccStr(idx, "Target");
      return m_lastType;
    }
    if (!strcmp(cpName, "ParentAppl")) { PushMisc(); return CallNoArg("GetObjectParent"); }
    if (!strcmp(cpName, "ParentApplTarget"))
    {
      PushMisc();
      CallNoArg("GetObjectParent");
      int idx = FindAcc(FI_MISC, "GetObject");
      EmitAccStr(idx, "Target");
      return m_lastType;
    }
    if (!strcmp(cpName, "ParentApplVariable"))
    {
      PushMisc();
      CallNoArg("GetObjectParent");
      if (m_lex.CurCh() == '(') m_lex.Next();
      if (m_lex.Cur().nKind != TK_STR) { Err("$ParentApplVariable ожидает строку"); return FT_F; }
      int idx = FindAcc(FI_MISC, "GetVariable");
      EmitAccStr(idx, m_lex.Cur().str);
      m_lex.Next();
      if (m_lex.CurCh() == ')') m_lex.Next();
      return m_lastType;
    }
    if (!strcmp(cpName, "IsAppliedOnSelf"))
    {
      PushSelf();
      PushSecond();
      E(FormulaVM::OP_EQ, FT_P, FT_P);
      m_lastType = FT_B;
      return FT_B;
    }
    Err("неизвестная $-подстановка");
    return FT_I;
  }

  // ---------- макросы FormulaPars.h ----------
  void ParseBuiltInArgs(int bi, int nAlready)
  {
    for (int i = nAlready; i < (int)kBuiltIn[bi].nArgs; ++i)
    {
      if (i > nAlready && m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr();
      ConvTo(kBuiltIn[bi].cArg[i]);
    }
  }

  // хвост SwitchByBool(cond, trueVal, falseVal): cond уже на стеке
  int FinishSwitchByBool()
  {
    if (m_lex.CurCh() == '(') m_lex.Next();
    ParseBuiltInArgs(BI_SwitchByBool, 1);
    if (m_lex.CurCh() == ')') m_lex.Next();
    E(FormulaVM::OP_BUILTIN, BI_SwitchByBool);
    m_lastType = FT_F;
    return FT_F;
  }

  int ParseTalFamily(char const * cpName)
  {
    bool bMaster = (strncmp(cpName, "master", 6) == 0) || (strncmp(cpName, "smaster", 7) == 0);
    bool bCheck  = strcmp(cpName, "talconstNoCheck") != 0;
    bool bConst  = (strstr(cpName, "const") != 0);
    bool bRefine = (strcmp(cpName, "getTalentRefineRate") == 0);
    // getTalentRefineRate(talId, defVal) — второй аргумент тоже «значение иначе»
    bool bHasDef = (strcmp(cpName, "stalconst") == 0) || (strcmp(cpName, "smastertalconst") == 0) || bRefine;

    if (m_lex.CurCh() == '(') m_lex.Next();
    if (m_lex.Cur().nKind != TK_STR) { Err("ожидается имя таланта в кавычках"); return FT_F; }
    char talName[512];
    strncpy(talName, m_lex.Cur().str, sizeof(talName) - 1);
    talName[sizeof(talName) - 1] = 0;
    m_lex.Next();

    char cstName[512];
    cstName[0] = 0;
    if (bConst)
    {
      if (m_lex.CurCh() == ',') m_lex.Next();
      if (m_lex.Cur().nKind != TK_STR) { Err("ожидается имя константы в кавычках"); return FT_F; }
      strncpy(cstName, m_lex.Cur().str, sizeof(cstName) - 1);
      cstName[sizeof(cstName) - 1] = 0;
      m_lex.Next();
    }

    int iTal = FindAcc(FI_UNIT, "GetTalent");
    int iCst = FindAcc(FI_MISC, "GetConstant");
    int iBuy = FindAcc(FI_MISC, "IsTalentBought");
    int iRat = FindAcc(FI_MISC, "GetRefineRate");
    int iMst = FindAcc(FI_UNIT, "GetObjectMaster");

    // ветвление: все JZ патчатся на метку «иначе» (её адрес известен только
    // после истинной ветки)
    int ajz[4]; int njz = 0;
    if (bCheck)
    {
      if (bMaster)
      {
        // mastertal(a): pFirst->GetObjectMaster() != pFirst && ...GetTalent(x) != 0
        // (побочных эффектов нет — OP_BAND вместо короткого замыкания)
        PushSelf(); E(FormulaVM::OP_ACC, iMst);
        PushSelf();
        E(FormulaVM::OP_NE, FT_P, FT_P);
        PushSelf(); E(FormulaVM::OP_ACC, iMst);
        E(FormulaVM::OP_PUSHSTR, m_vm.AddString(talName));
        E(FormulaVM::OP_ACC, iTal);
        E(FormulaVM::OP_PUSHNULL);
        E(FormulaVM::OP_NE, FT_P, FT_P);
        E(FormulaVM::OP_BAND);
        ajz[njz++] = E(FormulaVM::OP_JZ, 0);
        if (bConst)
        {
          // talconst использует tal(a), а tal(a) = (GetTalent != 0) ? GetTalent->IsTalentBought()
          // — без IsTalentBought условие шире, чем у скомпилированного кода
          PushSelf(); E(FormulaVM::OP_ACC, iMst);
          E(FormulaVM::OP_PUSHSTR, m_vm.AddString(talName));
          E(FormulaVM::OP_ACC, iTal);
          E(FormulaVM::OP_ACC, iBuy);
          ajz[njz++] = E(FormulaVM::OP_JZ, 0);
        }
      }
      else
      {
        PushSelf();
        E(FormulaVM::OP_PUSHSTR, m_vm.AddString(talName));
        E(FormulaVM::OP_ACC, iTal);
        E(FormulaVM::OP_PUSHNULL);
        E(FormulaVM::OP_NE, FT_P, FT_P);
        ajz[njz++] = E(FormulaVM::OP_JZ, 0);
        if (bConst)
        {
          PushSelf();
          E(FormulaVM::OP_PUSHSTR, m_vm.AddString(talName));
          E(FormulaVM::OP_ACC, iTal);
          E(FormulaVM::OP_ACC, iBuy);
          ajz[njz++] = E(FormulaVM::OP_JZ, 0);
        }
      }
    }
    // при talconstNoCheck проверки нет — и ветвления тоже нет

    if (bMaster) { PushSelf(); E(FormulaVM::OP_ACC, iMst); }
    PushSelf();
    E(FormulaVM::OP_PUSHSTR, m_vm.AddString(talName));
    E(FormulaVM::OP_ACC, iTal);
    if (bConst)
    {
      E(FormulaVM::OP_PUSHSTR, m_vm.AddString(cstName));
      PushSelf();
      PushSecond();
      E(FormulaVM::OP_ACC, iCst);
    }
    else if (bRefine) E(FormulaVM::OP_ACC, iRat);
    else              E(FormulaVM::OP_ACC, iBuy);

    // обе ветки должны сойтись к одному типу (как в тернарнике): иначе на стеке
    // остаётся значение другого типа и последующие операции читают не то
    int nTrue  = bConst ? FT_F : (bRefine ? FT_I : FT_B);
    int cvTrue = bCheck ? E(FormulaVM::OP_CONV, nTrue, -1) : -1;
    int jmp = bCheck ? E(FormulaVM::OP_JMP, 0) : -1;
    for (int i = 0; i < njz; ++i) Patch(ajz[i], Here());
    int defType = FT_I;
    if (bCheck)                                  // у NoCheck ветки «иначе» нет
    {
      if (bHasDef)
      {
        if (m_lex.CurCh() == ',') m_lex.Next();
        ParseExpr();
        defType = m_lastType;
      }
      else if (bConst || bRefine) { E(FormulaVM::OP_PUSHI, 0); defType = FT_I; }
      else                        { E(FormulaVM::OP_PUSHB, 0); defType = FT_B; }
    }
    if (m_lex.CurCh() == ')') m_lex.Next();

    int common = bCheck ? CommonType(nTrue, defType) : nTrue;
    if (bCheck)
    {
      if (defType != common) E(FormulaVM::OP_CONV, defType, common);
      if (cvTrue >= 0) PatchC(cvTrue, common);
      Patch(jmp, Here());
    }
    m_lastType = common;
    return m_lastType;
  }

  int ParseScale(char const * cpName)
  {
    bool bDamage = (strncmp(cpName, "damage", 6) == 0);
    int  scaleMode = 0;
    char statMethod[64];
    statMethod[0] = 0;

    if      (!strcmp(cpName, "abilityScaleLife"))      { scaleMode = 1; strcpy(statMethod, "GetLife"); }
    else if (!strcmp(cpName, "abilityScaleEnergy"))    { scaleMode = 2; strcpy(statMethod, "GetEnergy"); }
    else if (!strcmp(cpName, "abilityScaleMaxLife"))   { scaleMode = 1; strcpy(statMethod, "GetMaxLife"); }
    else if (!strcmp(cpName, "abilityScaleMaxEnergy")) { scaleMode = 2; strcpy(statMethod, "GetMaxEnergy"); }
    else if (strstr(cpName, "Life"))                   scaleMode = 1;
    else if (strstr(cpName, "Energy"))                 scaleMode = 2;

    PushMisc();
    E(FormulaVM::OP_PUSHB, bDamage ? 1 : 0);
    if (m_lex.CurCh() == '(') m_lex.Next();
    if (statMethod[0])
    {
      PushSelf();
      CallNoArg(statMethod);
    }
    else
    {
      ParseExpr();
      ConvTo(FT_F);
    }
    E(FormulaVM::OP_PUSHI, scaleMode);
    if (m_lex.CurCh() == ',') m_lex.Next();
    ParseExpr(); ConvTo(FT_F);
    if (m_lex.CurCh() == ',') m_lex.Next();
    ParseExpr(); ConvTo(FT_F);
    if (m_lex.CurCh() == ',') { m_lex.Next(); ParseExpr(); ConvTo(FT_B); }
    else                      { E(FormulaVM::OP_PUSHB, 1); }   // bRound = true (default C++)
    if (m_lex.CurCh() == ')') m_lex.Next();

    int idx = FindAcc(FI_MISC, "GetAbilityScale");
    E(FormulaVM::OP_ACC, idx);
    m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
    return m_lastType;
  }

  int ParseMacro(char const * cpName)               // -1 = не макрос
  {
    if (!strcmp(cpName, "tal") || !strcmp(cpName, "talconst") ||
        !strcmp(cpName, "talconstNoCheck") || !strcmp(cpName, "stalconst") ||
        !strcmp(cpName, "mastertal") || !strcmp(cpName, "mastertalconst") ||
        !strcmp(cpName, "smastertalconst") || !strcmp(cpName, "getTalentRefineRate"))
      return ParseTalFamily(cpName);

    if (!strcmp(cpName, "abilityScale") || !strcmp(cpName, "damageScale") ||
        !strcmp(cpName, "abilityScaleLife") || !strcmp(cpName, "abilityScaleEnergy") ||
        !strcmp(cpName, "abilityScaleMaxLife") || !strcmp(cpName, "abilityScaleMaxEnergy") ||
        !strcmp(cpName, "abilityScaleCustomLife") || !strcmp(cpName, "abilityScaleCustomEnergy") ||
        !strcmp(cpName, "damageScaleCustomLife") || !strcmp(cpName, "damageScaleCustomEnergy"))
      return ParseScale(cpName);

    if (!strcmp(cpName, "nt") || !strcmp(cpName, "nativeTerrain"))
    {
      // (pFirst->GetTerrainType() == pFirst->GetFctn()) ? A : B
      PushSelf(); CallNoArg("GetTerrainType");
      PushSelf(); CallNoArg("GetFctn");
      E(FormulaVM::OP_EQ, FT_I, FT_I);
      int jz = E(FormulaVM::OP_JZ, 0);
      if (m_lex.CurCh() == '(') m_lex.Next();
      int t1 = ParseExpr();
      int cv1 = E(FormulaVM::OP_CONV, t1, -1);
      int jmp = E(FormulaVM::OP_JMP, 0);
      Patch(jz, Here());
      if (m_lex.CurCh() == ',') m_lex.Next();
      int t2 = ParseExpr();
      int cv2 = E(FormulaVM::OP_CONV, t2, -1);
      Patch(jmp, Here());
      if (m_lex.CurCh() == ')') m_lex.Next();
      int common = CommonType(t1, t2);
      PatchC(cv1, common);
      PatchC(cv2, common);
      m_lastType = common;
      return common;
    }
    if (!strcmp(cpName, "random"))
    {
      PushMisc();
      m_lex.Next();
      ParseExpr(); ConvTo(FT_I);
      if (m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr(); ConvTo(FT_I);
      if (m_lex.CurCh() == ')') m_lex.Next();
      int idx = FindAcc(FI_MISC, "GetRandom");
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      return m_lastType;
    }
    if (!strcmp(cpName, "refineScale"))
    {
      PushMisc();
      m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      if (m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      if (m_lex.CurCh() == ')') m_lex.Next();
      int idx = FindAcc(FI_MISC, "GetRefineAbilityScale");
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      return m_lastType;
    }
    if (!strcmp(cpName, "smartRandom") || !strcmp(cpName, "smartRoll"))
    {
      bool bRoll = (strcmp(cpName, "smartRoll") == 0);
      PushMisc();
      m_lex.Next();
      // smartRandom(outcomes:int, decr:float)  — ДВА аргумента
      // smartRoll(prob:float, maxFails:int, maxSuccs:int) — три
      ParseExpr(); ConvTo(bRoll ? FT_F : FT_I);
      if (m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr(); ConvTo(bRoll ? FT_I : FT_F);
      if (bRoll)
      {
        if (m_lex.CurCh() == ',') m_lex.Next();
        ParseExpr(); ConvTo(FT_I);
      }
      if (m_lex.CurCh() == ')') m_lex.Next();
      PushSelf();
      PushSecond();
      int idx = FindAcc(FI_MISC, bRoll ? "GetSmartRoll" : "GetSmartRandom");
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      return m_lastType;
    }
    if (!strcmp(cpName, "getStatusDispellPriority"))
    {
      PushMisc();
      m_lex.Next();
      PushSecond();
      ParseExpr(); ConvTo(FT_B);
      if (m_lex.CurCh() == ')') m_lex.Next();
      int idx = FindAcc(FI_MISC, "GetStatusDispellPriority");
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      return m_lastType;
    }
    if (!strcmp(cpName, "ut_hero") || !strcmp(cpName, "ut_bldg"))
    {
      PushSecond();
      E(FormulaVM::OP_PUSHI, strcmp(cpName, "ut_hero") ? 1 : 0);   // UNITCHECKID_*
      int idx = FindAcc(FI_UNIT, "UnitCheck");
      E(FormulaVM::OP_ACC, idx);
      return FinishSwitchByBool();
    }
    if (!strcmp(cpName, "r"))
    {
      PushMisc();
      CallNoArg("GetRank");
      if (m_lex.CurCh() == '(') m_lex.Next();
      ParseBuiltInArgs(BI_SwitchByAbilityRank, 1);
      if (m_lex.CurCh() == ')') m_lex.Next();
      E(FormulaVM::OP_BUILTIN, BI_SwitchByAbilityRank);
      m_lastType = FT_F;
      return FT_F;
    }
    if (!strcmp(cpName, "s"))
    {
      // s(cond, a, b) = SwitchByBool(cond, a, b): в отличие от ut_*/r здесь
      // условие НЕ разобрано заранее — парсим все три аргумента (nAlready=0).
      if (m_lex.CurCh() == '(') m_lex.Next();
      ParseBuiltInArgs(BI_SwitchByBool, 0);
      if (m_lex.CurCh() == ')') m_lex.Next();
      E(FormulaVM::OP_BUILTIN, BI_SwitchByBool);
      m_lastType = FT_F;
      return FT_F;
    }
    if (!strcmp(cpName, "roll"))
    {
      // roll(a,b,c): в скомпилированном коде это SwitchByBool(pMisc->Roll(a,b,c), b, c)
      // (лишние аргументы thiscall безвредны). Здесь — эквивалент: Roll(a), а b и c
      // идут в SwitchByBool.
      PushMisc();
      m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      int idx = FindAcc(FI_MISC, "Roll");
      E(FormulaVM::OP_ACC, idx);
      if (m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      if (m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      if (m_lex.CurCh() == ')') m_lex.Next();
      E(FormulaVM::OP_BUILTIN, BI_SwitchByBool);
      m_lastType = FT_F;
      return FT_F;
    }
    if (!strcmp(cpName, "sFlag") || !strcmp(cpName, "rFlag"))
    {
      if (cpName[0] == 'r') PushSecond(); else PushSelf();
      m_lex.Next();
      ParseExpr(); ConvTo(FT_I);
      if (m_lex.CurCh() == ')') m_lex.Next();
      int idx = FindAcc(FI_UNIT, "GetFlag");
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      return m_lastType;
    }
    return -1;
  }

  int ParseIdentifier()
  {
    char const * cpName = TakeId();
    bool bCall = (m_lex.PeekCh() == '(');

    if (!strcmp(cpName, "pFirst")   || !strcmp(cpName, "sender"))
      { m_lex.Next(); PushSelf();   return FT_P; }
    if (!strcmp(cpName, "pSecond")  || !strcmp(cpName, "receiver"))
      { m_lex.Next(); PushSecond(); return FT_P; }
    if (!strcmp(cpName, "pMisc")    || !strcmp(cpName, "appl"))
      { m_lex.Next(); PushMisc();   return FT_P; }
    if (!strcmp(cpName, "true"))  { m_lex.Next(); E(FormulaVM::OP_PUSHB, 1); m_lastType = FT_B; return FT_B; }
    if (!strcmp(cpName, "false")) { m_lex.Next(); E(FormulaVM::OP_PUSHB, 0); m_lastType = FT_B; return FT_B; }
    if (!strcmp(cpName, "CVec2") && bCall)
    {
      m_lex.Next(); m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      if (m_lex.CurCh() == ',') m_lex.Next();
      ParseExpr(); ConvTo(FT_F);
      if (m_lex.CurCh() == ')') m_lex.Next();
      E(FormulaVM::OP_VEC2);
      m_lastType = FT_V; m_lastKind = FR_NONE;
      return FT_V;
    }

    {
      int v = 0;
      if (FindEnum(cpName, v)) { m_lex.Next(); E(FormulaVM::OP_PUSHI, v); m_lastType = FT_I; return FT_I; }
    }
    if (bCall)
    {
      int bi = FindBuiltIn(cpName);
      if (bi >= 0)
      {
        m_lex.Next(); m_lex.Next();
        ParseBuiltInArgs(bi, 0);
        if (m_lex.CurCh() == ')') m_lex.Next();
        E(FormulaVM::OP_BUILTIN, bi);
        m_lastType = kBuiltIn[bi].cRet; m_lastKind = FR_NONE;
        return m_lastType;
      }
      m_lex.Next();                                  // имя макроса
      int r = ParseMacro(cpName);
      if (r >= 0) return r;
    }

    // префиксные правила: sFlag()/rFlag() уже разобраны в ParseMacro;
    // здесь sIsX/rIsX/mIsX, sX/rX/mX, cX
    {
      char mname[160];
      int  iface = -1;
      // ВАЖНО: sIsX/rIsX/mIsX проверяются ДО sX/rX/mX: «IsHero» тоже
      // начинается с заглавной, и общее правило съело бы его как GetIsHero.
      if (!strncmp(cpName, "sIs", 3) && isupper((unsigned char)cpName[3]))
        { iface = FI_UNIT;  strcpy(mname, "Is"); strcpy(mname + 2, cpName + 3); }
      else if (!strncmp(cpName, "rIs", 3) && isupper((unsigned char)cpName[3]))
        { iface = FI_UNIT;  strcpy(mname, "Is"); strcpy(mname + 2, cpName + 3); }
      else if (!strncmp(cpName, "mIs", 3) && isupper((unsigned char)cpName[3]))
        { iface = FI_MISC;  strcpy(mname, "Is"); strcpy(mname + 2, cpName + 3); }
      else if (cpName[0] == 's' && isupper((unsigned char)cpName[1]))
        { iface = FI_UNIT;  strcpy(mname, "Get"); strcpy(mname + 3, cpName + 1); }
      else if (cpName[0] == 'r' && isupper((unsigned char)cpName[1]))
        { iface = FI_UNIT;  strcpy(mname, "Get"); strcpy(mname + 3, cpName + 1); }
      else if (cpName[0] == 'm' && isupper((unsigned char)cpName[1]))
        { iface = FI_MISC;  strcpy(mname, "Get"); strcpy(mname + 3, cpName + 1); }
      else if (cpName[0] == 'c' && isupper((unsigned char)cpName[1]))
      {
        m_lex.Next();
        int idx = FindAcc(FI_MISC, "GetConstant");
        PushMisc();
        E(FormulaVM::OP_PUSHSTR, m_vm.AddString(cpName + 1));
        PushSelf();
        PushSecond();
        E(FormulaVM::OP_ACC, idx);
        m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
        m_lastKind = FR_NONE;
        return m_lastType;
      }
      else if (!strncmp(cpName, "sFlag", 5) || !strncmp(cpName, "rFlag", 5))
      {
        int r = ParseMacro(cpName);
        if (r >= 0) return r;
        return FT_I;
      }
      else
      {
        // имя без префикса: либо уже C++-форма (pFirst->... обрабатывается выше),
        // либо неизвестное имя
        Err("неизвестный идентификатор формулы");
        m_lex.Next();
        return FT_I;
      }

      // префикс sX/rX/mX/sIsX/... : receiver — соответствующий интерфейс
      m_lex.Next();
      int saveKind = m_lastKind;
      if (iface == FI_MISC) PushMisc(); else if (cpName[0] == 'r') PushSecond(); else PushSelf();
      (void)saveKind;
      int nSaveIface = RecvIface();
      (void)nSaveIface;
      // явный интерфейс из префикса
      int idx = FindAcc(iface, mname);
      if (idx < 0) { Err("нет аксессора для имени"); return FT_I; }
      if (kFormulaSigArgs[kFormulaAcc[idx].nSig] != 0) { Err("метод требует аргументы"); return FT_I; }
      E(FormulaVM::OP_ACC, idx);
      m_lastType = kFormulaSigRet[kFormulaAcc[idx].nSig];
      m_lastKind = KindOfPtrResult(idx);
      return m_lastType;
    }
  }

  FormulaVM &  m_vm;
  FmLexer      m_lex;
  bool         m_bErr;
  char         m_err[256];
  int          m_lastType;
  int          m_lastKind;
  char         m_idBuf[128];
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FormulaVM: хранение байткода и интерпретатор
#define FM_STACK_SIZE 64

FormulaVM::FormulaVM()
  : m_pOps(0), m_nOps(0), m_nOpsCap(0)
  , m_pFloats(0), m_nFloats(0), m_nFloatsCap(0)
  , m_pDoubles(0), m_nDoubles(0), m_nDoublesCap(0)
  , m_pStrPool(0), m_nStrPool(0), m_nStrPoolCap(0)
  , m_cpError(0), m_cpSource(0), m_cpReturnType(0), m_nRuntimeErr(0)
{
  m_errBuf[0] = 0;
}

FormulaVM::~FormulaVM()
{
  delete[] m_pOps;
  delete[] m_pFloats;
  delete[] m_pStrPool;
  delete[] m_cpSource;
}

int FormulaVM::Emit(int nOp, int nA, int nB)
{
  if (m_nOps >= m_nOpsCap)
  {
    int nNew = m_nOpsCap ? m_nOpsCap * 2 : 64;
    Op * pNew = new Op[nNew];
    if (m_pOps) memcpy(pNew, m_pOps, sizeof(Op) * m_nOps);
    delete[] m_pOps;
    m_pOps = pNew;
    m_nOpsCap = nNew;
  }
  m_pOps[m_nOps].nOp = (unsigned short)nOp;
  m_pOps[m_nOps].nA  = nA;
  m_pOps[m_nOps].nB  = nB;
  return m_nOps++;
}

void FormulaVM::PatchJump(int nOpIdx, int nTarget)
{
  m_pOps[nOpIdx].nA = nTarget;
}

void FormulaVM::PatchConv(int nOpIdx, int nType)
{
  m_pOps[nOpIdx].nB = nType;
}

int FormulaVM::AddDouble(double d)
{
  if (m_nDoubles >= m_nDoublesCap)
  {
    int nNew = m_nDoublesCap ? m_nDoublesCap * 2 : 32;
    double * pNew = new double[nNew];
    if (m_pDoubles) memcpy(pNew, m_pDoubles, sizeof(double) * m_nDoubles);
    delete[] m_pDoubles;
    m_pDoubles = pNew;
    m_nDoublesCap = nNew;
  }
  m_pDoubles[m_nDoubles] = d;
  return m_nDoubles++;
}

int FormulaVM::AddFloat(float f)
{
  if (m_nFloats >= m_nFloatsCap)
  {
    int nNew = m_nFloatsCap ? m_nFloatsCap * 2 : 32;
    float * pNew = new float[nNew];
    if (m_pFloats) memcpy(pNew, m_pFloats, sizeof(float) * m_nFloats);
    delete[] m_pFloats;
    m_pFloats = pNew;
    m_nFloatsCap = nNew;
  }
  m_pFloats[m_nFloats] = f;
  return m_nFloats++;
}

int FormulaVM::AddString(char const * cpS)
{
  int nLen = (int)strlen(cpS) + 1;
  if (m_nStrPool + nLen > m_nStrPoolCap)
  {
    int nNew = m_nStrPoolCap ? m_nStrPoolCap * 2 : 1024;
    while (nNew < m_nStrPool + nLen) nNew *= 2;
    char * pNew = new char[nNew];
    if (m_pStrPool) memcpy(pNew, m_pStrPool, m_nStrPool);
    delete[] m_pStrPool;
    m_pStrPool = pNew;
    m_nStrPoolCap = nNew;
  }
  int nAt = m_nStrPool;
  memcpy(m_pStrPool + nAt, cpS, nLen);
  m_nStrPool += nLen;
  return nAt;
}

FormulaVM * FormulaVM::Compile(char const * cpFormula, char const * cpReturnType)
{
  FormulaVM * pVM = new FormulaVM();
  int nLen = (int)strlen(cpFormula) + 1;
  pVM->m_cpSource = new char[nLen];
  memcpy(pVM->m_cpSource, cpFormula, nLen);
  pVM->m_cpReturnType = cpReturnType;

  FormulaVMCompiler comp(*pVM, cpFormula);
  if (!comp.Compile())
  {
    strcpy(pVM->m_errBuf, comp.ErrMsg());
    pVM->m_cpError = pVM->m_errBuf;
    pVM->m_nOps = 0;                       // IsValid() == false
  }
  return pVM;
}

float FormulaVM::V2F(Val const & v)
{
  switch (v.t)
  {
    case FT_F: return v.f;
    case FT_D: return (float)v.d;
    // bool хранится в i (не в p!): на x64 чтение v.p после записи в i дало бы
    // мусор в старших 32 битах юниона — поймаено golden-диффом по x64-эталону
    case FT_B: return v.i ? 1.0f : 0.0f;
    case FT_I: default: return (float)v.i;
  }
}

double FormulaVM::V2D(Val const & v)
{
  switch (v.t)
  {
    case FT_D: return v.d;
    case FT_F: return (double)v.f;
    case FT_I: return (double)v.i;
    case FT_B: return (double)(v.i ? 1 : 0);
    default:   return 0.0;
  }
}

int FormulaVM::V2I(Val const & v)
{
  switch (v.t)
  {
    case FT_I: return v.i;
    case FT_F: return (int)v.f;
    case FT_D: return (int)v.d;
    case FT_B: return v.i ? 1 : 0;
    case FT_P: return v.p ? 1 : 0;
    default:   return 0;
  }
}

bool FormulaVM::V2B(Val const & v)
{
  switch (v.t)
  {
    case FT_B: case FT_I: return v.i != 0;
    case FT_F: return v.f != 0.0f;
    case FT_D: return v.d != 0.0;
    case FT_P: case FT_U: return v.p != 0;
    default:   return false;
  }
}

static int CommonTypeOf(int a, int b)
{
  if (a == b) return a;
  if (a == FT_D || b == FT_D) return FT_D;
  if (a == FT_F || b == FT_F) return FT_F;
  return FT_I;
}

FormulaVM::Val const * FormulaVM::Run(void const * pFirst, void const * pSecond, void const * pMisc) const
{
  Val      st[FM_STACK_SIZE];
  int      sp = 0;
  Op const * pOp = m_pOps;
  int      nGuard = 0;

  m_nRuntimeErr = 0;  // ошибка относится к одному вызову, а не к формуле

  while (pOp)
  {
    if (++nGuard > 1000000) { m_nRuntimeErr = 10; break; }   // защита от петлевых ошибок компиляции
    switch (pOp->nOp)
    {
      case OP_HALT:
        pOp = 0; break;

      case OP_PUSHF: st[sp].t = FT_F; st[sp].f = m_pFloats[pOp->nA]; ++sp; break;
      case OP_PUSHDBL: st[sp].t = FT_D; st[sp].d = m_pDoubles[pOp->nA]; ++sp; break;
      case OP_PUSHI: st[sp].t = FT_I; st[sp].i = pOp->nA; ++sp; break;
      case OP_PUSHB: st[sp].t = FT_B; st[sp].i = pOp->nA; ++sp; break;
      case OP_PUSHSTR: st[sp].t = FT_P; st[sp].p = m_pStrPool + pOp->nA; ++sp; break;
      case OP_PUSHNULL: st[sp].t = FT_U; st[sp].p = 0; ++sp; break;
      case OP_PUSHSELF:   st[sp].t = FT_P; st[sp].p = pFirst;  ++sp; break;
      case OP_PUSHSECOND: st[sp].t = FT_P; st[sp].p = pSecond; ++sp; break;
      case OP_PUSHMISC:   st[sp].t = FT_P; st[sp].p = pMisc;   ++sp; break;

      case OP_VEC2:
      {
        if (sp < 2) { m_nRuntimeErr = 21; pOp = 0; break; }
        float y = V2F(st[sp - 1]);
        float x = V2F(st[sp - 2]);
        sp -= 2;
        st[sp].t = FT_V; st[sp].v2.x = x; st[sp].v2.y = y; ++sp;
        break;
      }

      case OP_CONV:
      {
        if (sp < 1) { m_nRuntimeErr = 17; pOp = 0; break; }
        Val & v = st[sp - 1];
        int from = v.t, to = pOp->nB;
        if (to < 0 || from == to) break;
        if (to == FT_B)      { int b = V2B(v) ? 1 : 0; v.t = FT_B; v.i = b; }
        else if (to == FT_D) { double d = V2D(v); v.t = FT_D; v.d = d; }
        else if (to == FT_F) { float f = V2F(v); v.t = FT_F; v.f = f; }
        else if (to == FT_I) { int i = V2I(v); v.t = FT_I; v.i = i; }
        else if (to == FT_P) { v.t = FT_P; }
        else { m_nRuntimeErr = 11; (void)from; }
        break;
      }

      case OP_NEG:
      {
        if (sp < 1) { m_nRuntimeErr = 17; pOp = 0; break; }
        Val & v = st[sp - 1];
        if (pOp->nA == FT_D)      v.d = -v.d;
        else if (pOp->nA == FT_F) v.f = -v.f;
        else                      v.i = -v.i;
        break;
      }

      case OP_NOT:
      {
        if (sp < 1) { m_nRuntimeErr = 17; pOp = 0; break; }
        Val & v = st[sp - 1];
        int b = V2B(v) ? 0 : 1;
        v.t = FT_B; v.i = b;
        break;
      }

      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
      case OP_BAND: case OP_BOR: case OP_BXOR:
      {
        if (sp < 2) { m_nRuntimeErr = 18; pOp = 0; break; }
        Val & a = st[sp - 2];
        Val const b = st[sp - 1];
        int ct = CommonTypeOf(pOp->nA, pOp->nB);
        if (pOp->nOp >= OP_BAND && pOp->nOp <= OP_BXOR) ct = FT_I;
        if (ct == FT_D)
        {
          double x = V2D(a), y = V2D(b), r = 0;
          switch (pOp->nOp)
          {
            case OP_ADD: r = x + y; break;
            case OP_SUB: r = x - y; break;
            case OP_MUL: r = x * y; break;
            case OP_DIV: if (y == 0.0) { m_nRuntimeErr = 1; } r = x / y; break;
            case OP_MOD: r = fmod(x, y); break;
            default: r = 0; break;
          }
          a.t = FT_D; a.d = r;
        }
        else if (ct == FT_F)
        {
          float x = V2F(a), y = V2F(b), r = 0;
          switch (pOp->nOp)
          {
            case OP_ADD: r = x + y; break;
            case OP_SUB: r = x - y; break;
            case OP_MUL: r = x * y; break;
            case OP_DIV: if (y == 0.0f) { m_nRuntimeErr = 1; } r = x / y; break;
            case OP_MOD: r = (float)fmod((double)x, (double)y); break;
            default: r = 0; break;
          }
          a.t = FT_F; a.f = r;
        }
        else
        {
          int x = V2I(a), y = V2I(b), r = 0;
          switch (pOp->nOp)
          {
            case OP_ADD:  r = x + y; break;
            case OP_SUB:  r = x - y; break;
            case OP_MUL:  r = x * y; break;
            case OP_DIV:  if (y == 0) { m_nRuntimeErr = 2; a.t = FT_I; a.i = 0; sp -= 1; pOp = 0; break; } r = x / y; break;
            case OP_MOD:  if (y == 0) { m_nRuntimeErr = 3; a.t = FT_I; a.i = 0; sp -= 1; pOp = 0; break; } r = x % y; break;
            case OP_BAND: r = x & y; break;
            case OP_BOR:  r = x | y; break;
            case OP_BXOR: r = x ^ y; break;
            default: r = 0; break;
          }
          a.t = FT_I; a.i = r;
        }
        sp -= 1;
        break;
      }

      case OP_LT: case OP_LE: case OP_GT: case OP_GE: case OP_EQ: case OP_NE:
      {
        if (sp < 2) { m_nRuntimeErr = 19; pOp = 0; break; }
        Val const & a = st[sp - 2];
        Val const & b = st[sp - 1];
        bool r = false;
        bool bPtr = (a.t == FT_P || a.t == FT_U || b.t == FT_P || b.t == FT_U) &&
                    (pOp->nOp == OP_EQ || pOp->nOp == OP_NE);
        if (bPtr)
        {
          r = (a.p == b.p);
          if (pOp->nOp == OP_NE) r = !r;
        }
        else
        {
          int ct = CommonTypeOf(a.t, b.t);
          if (ct == FT_D)
          {
            double x = V2D(a), y = V2D(b);
            switch (pOp->nOp)
            {
              case OP_LT: r = x <  y; break;  case OP_LE: r = x <= y; break;
              case OP_GT: r = x >  y; break;  case OP_GE: r = x >= y; break;
              case OP_EQ: r = x == y; break;  case OP_NE: r = x != y; break;
            }
          }
          else if (ct == FT_F)
          {
            float x = V2F(a), y = V2F(b);
            switch (pOp->nOp)
            {
              case OP_LT: r = x <  y; break;  case OP_LE: r = x <= y; break;
              case OP_GT: r = x >  y; break;  case OP_GE: r = x >= y; break;
              case OP_EQ: r = x == y; break;  case OP_NE: r = x != y; break;
            }
          }
          else
          {
            int x = V2I(a), y = V2I(b);
            switch (pOp->nOp)
            {
              case OP_LT: r = x <  y; break;  case OP_LE: r = x <= y; break;
              case OP_GT: r = x >  y; break;  case OP_GE: r = x >= y; break;
              case OP_EQ: r = x == y; break;  case OP_NE: r = x != y; break;
            }
          }
        }
        sp -= 2;
        st[sp].t = FT_B; st[sp].i = r ? 1 : 0; ++sp;
        break;
      }

      case OP_COMMA:
      {
        if (sp < 2) { m_nRuntimeErr = 23; pOp = 0; break; }
        Val r = st[sp - 1];
        sp -= 2; st[sp] = r; ++sp;
        break;
      }

      case OP_JZ: case OP_JNZ:
      {
        if (sp < 1) { m_nRuntimeErr = 22; pOp = 0; break; }
        bool b = V2B(st[sp - 1]);
        --sp;
        if ((pOp->nOp == OP_JZ && !b) || (pOp->nOp == OP_JNZ && b)) { pOp = m_pOps + pOp->nA; continue; }
        break;
      }
      case OP_JMP: pOp = m_pOps + pOp->nA; continue;

      case OP_ACC:
      {
        FormulaAccEntry const & e = kFormulaAcc[pOp->nA];
        int nArgs = kFormulaSigArgs[e.nSig];
        if (sp < nArgs + 1) { m_nRuntimeErr = 16; pOp = 0; break; }
        Val * pRecv = st + sp - 1 - nArgs;
        void const * self = pRecv->p;
        if (self == 0) { m_nRuntimeErr = 4; pOp = 0; break; }
        void const * ap[6];
        for (int i = 0; i < nArgs; ++i) ap[i] = &st[sp - nArgs + i];
        float rf = 0; int ri = 0; bool rb = false; CVec2 rv(0, 0); void const * rp = 0;
        if (kFormulaSigHasV[e.nSig])
        {
          // CVec2 в сигнатуре: MSVC возвращает/принимает его через скрытый
          // указатель (у типа нетривиальный ctor) — идти надо через member
          // pointer, а не через vtable-слот.
          FormulaAccCallVec(pOp->nA, self, ap, rf, ri, rb, rv, rp);
        }
        else
        {
          void const * const * pVtbl = *reinterpret_cast<void const * const * const *>(self);
          void * fp = const_cast<void *>(pVtbl[e.nSlot]);
          FormulaAccCall(e.nSig, fp, self, ap, rf, ri, rb, rv, rp);
        }
        sp = sp - 1 - nArgs;
        st[sp].t = kFormulaSigRet[e.nSig];
        switch (st[sp].t)
        {
          case FT_F: st[sp].f = rf; break;
          case FT_I: st[sp].i = ri; break;
          case FT_B: st[sp].i = rb ? 1 : 0; break;
          case FT_V: st[sp].v2.x = rv.x; st[sp].v2.y = rv.y; break;
          default:   st[sp].p = rp; break;
        }
        ++sp;
        break;
      }

      case OP_BUILTIN:
      {
        BuiltInEntry const & bi = kBuiltIn[pOp->nA];
        int n = bi.nArgs;
        if (sp < n) { m_nRuntimeErr = 20; pOp = 0; break; }
        Val * pA = st + sp - n;
        float rf = 0; double rd = 0; int ri = 0; bool rb = false;
        switch (pOp->nA)
        {
          case BI_min: rf = V2F(pA[0]) < V2F(pA[1]) ? V2F(pA[0]) : V2F(pA[1]); break;
          case BI_max: rf = V2F(pA[0]) > V2F(pA[1]) ? V2F(pA[0]) : V2F(pA[1]); break;
          case BI_clamp:
          {
            float a = V2F(pA[0]), b = V2F(pA[1]), c = V2F(pA[2]);
            float m = a > b ? a : b;
            rf = m < c ? m : c;
            break;
          }
          case BI_lerp: rf = V2F(pA[1]) * (1.0f - V2F(pA[0])) + V2F(pA[2]) * V2F(pA[0]); break;
          case BI_isinbounds: rb = (V2F(pA[1]) <= V2F(pA[0])) && (V2F(pA[0]) <= V2F(pA[2])); break;
          case BI_round:
          {
            float x = V2F(pA[0]);
            rf = (x >= 0.0f) ? floorf(x + 0.5f) : ceilf(x - 0.5f);   // ni_round
            break;
          }
          case BI_f2l: case BI_f2l_sse3: ri = (int)V2F(pA[0]); break;
          case BI_floor: rd = floor(V2D(pA[0])); break;
          case BI_ceil:  rd = ceil(V2D(pA[0]));  break;
          case BI_fabs:  rd = fabs(V2D(pA[0]));  break;
          case BI_sqrt:  rd = sqrt(V2D(pA[0]));  break;
          case BI_sin:   rd = sin(V2D(pA[0]));   break;
          case BI_cos:   rd = cos(V2D(pA[0]));   break;
          case BI_pow:   rd = pow(V2D(pA[0]), V2D(pA[1])); break;
          case BI_fmod:  rd = fmod(V2D(pA[0]), V2D(pA[1])); break;
          case BI_d:
          {
            float dx = pA[0].v2.x - pA[1].v2.x;
            float dy = pA[0].v2.y - pA[1].v2.y;
            rf = (float)sqrt((double)(dx * dx + dy * dy));
            break;
          }
          case BI_t:
          {
            int idx = V2I(pA[0]);
            rf = (idx == 0) ? V2F(pA[1]) : (idx == 1) ? V2F(pA[2]) : (idx == 2) ? V2F(pA[3]) : 0.0f;
            break;
          }
          case BI_SwitchByBool: rb = V2B(pA[0]); rf = rb ? V2F(pA[1]) : V2F(pA[2]); break;
          case BI_SwitchByAbilityRank:
          {
            int rank = V2I(pA[0]);
            rf = (rank == 1) ? V2F(pA[1]) : (rank == 2) ? V2F(pA[2]) :
                 (rank == 3) ? V2F(pA[3]) : (rank == 4) ? V2F(pA[4]) : 0.0f;
            break;
          }
          default: m_nRuntimeErr = 12; break;
        }
        sp -= n;
        st[sp].t = bi.cRet;
        if (bi.cRet == FT_F) st[sp].f = rf;
        else if (bi.cRet == FT_D) st[sp].d = rd;
        else if (bi.cRet == FT_I) st[sp].i = ri;
        else st[sp].i = rb ? 1 : 0;
        ++sp;
        break;
      }

      default:
        m_nRuntimeErr = 13;
        pOp = 0;
        break;
    }
    if (sp < 0) { m_nRuntimeErr = 14; break; }
    if (sp >= FM_STACK_SIZE) { m_nRuntimeErr = 15; break; }
    if (pOp) ++pOp;
  }

  return (sp > 0) ? &st[sp - 1] : 0;
}

float FormulaVM::ExecuteFloat(void const * pFirst, void const * pSecond, void const * pMisc) const
{
  Val const * v = Run(pFirst, pSecond, pMisc);
  // жёсткая ошибка (null-self, структурная) — у x86-blob'а её перехватывает
  // __except в DataExecutor::Execute и возвращает T(0); повторяем это и здесь
  if (m_nRuntimeErr >= FM_ERR_HARD) return 0.0f;
  return v ? V2F(*v) : 0.0f;
}

int FormulaVM::ExecuteInt(void const * pFirst, void const * pSecond, void const * pMisc) const
{
  Val const * v = Run(pFirst, pSecond, pMisc);
  if (m_nRuntimeErr >= FM_ERR_HARD) return 0;
  return v ? V2I(*v) : 0;
}

bool FormulaVM::ExecuteBool(void const * pFirst, void const * pSecond, void const * pMisc) const
{
  Val const * v = Run(pFirst, pSecond, pMisc);
  if (m_nRuntimeErr >= FM_ERR_HARD) return false;
  return v ? V2B(*v) : false;
}
