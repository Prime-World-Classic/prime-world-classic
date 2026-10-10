/*
 * FormulaVM.h — переносимый бэкенд формул (этап 3, PLAN_client_modern.md).
 *
 * Зачем. Формулы контента хранятся как <sString> (исходник на DSL) и
 * <compiledString> (предкомпилированный x86-машинский код в формате
 * DataExecutor::FormulaHeader). На x64 тот код выполнить нельзя, поэтому вместо
 * него формула компилируется из sString в байткод этой VM. Язык формул —
 * C++-выражение над тремя интерфейсами (IUnitFormulaPars / IMiscFormulaPars /
 * ICustomFormulaPars) плюс слой подстановок (sLife, rIsHero, cFoo, .vVar,
 * tal(...), abilityScale(...) и т.д.); правила подстановок —
 * Src/FormulaBuilder/FormulaBuilder.cpp::PrepareCFile, словарь аксессоров —
 * Src/System/FormulaAccessors.inc (генерируется из FormulaPars.h).
 *
 * Точность: VM воспроизводит C++-семантику выражения, а не «примерно»: статический
 * тип каждого подвыражения считается при компиляции (float / double / int / bool /
 * CVec2 / указатель), целочисленное деление остаётся целочисленным, математика из
 * link-таблицы (floor/ceil/fabs/fmod/pow/sqrt/sin/cos) считается в double и
 * возвращает double — ровно как вызов `_floor` и т.п. из скомпилированной формулы.
 * Проверка точности — golden-дифф с реальным скомпилированным кодом
 * (Tools/FormulaCheck/harness).
 *
 * Аксессоры вызываются через vtable-слот (как в предкомпилированном коде:
 * `call [this + 4*slot]`), поэтому VM собирается и под x86, и под x64 — и
 * «VM vs нативный код» можно сравнивать в одном процессе.
 */

#ifndef FORMULAVM_H_
#define FORMULAVM_H_

#include <math.h>          // FormulaPars.h/Vec2_Base.h используют sqrt/floor/ceil
#include "../../Data/GameLogic/Vec2_Base.h"

class FormulaVM
{
public:
  // Ошибка компиляции — в GetError() (не assert: контент правят люди).
  static FormulaVM * Compile(char const * cpFormula, char const * cpReturnType = 0);

  ~FormulaVM();

  bool         IsValid()    const { return m_nOps > 0; }
  char const * GetError()   const { return m_cpError; }
  char const * GetSource()  const { return m_cpSource; }
  int          GetOpCount() const { return m_nOps; }
  // 0 = ок; иначе код ошибки времени выполнения (0-й деление, null-self и т.п.)
  int          GetRuntimeError() const { return m_nRuntimeErr; }

  // Тип результата задаёт вызывающая сторона (как ExecutableString::Execute<T>):
  // значение конвертируется по правилам C++ (float->int — усечение и т.д.).
  float ExecuteFloat(void const * pFirst, void const * pSecond, void const * pMisc) const;
  int   ExecuteInt  (void const * pFirst, void const * pSecond, void const * pMisc) const;
  bool  ExecuteBool (void const * pFirst, void const * pSecond, void const * pMisc) const;

private:
  FormulaVM();

  enum // операнды байткода
  {
    OP_HALT = 0,
    OP_PUSHF, OP_PUSHI, OP_PUSHB, OP_PUSHSTR, OP_PUSHNULL,
    OP_PUSHDBL,        // double-литерал (в C++ «0.9» — это double, не float)
    OP_PUSHSELF, OP_PUSHSECOND, OP_PUSHMISC,
    OP_ACC,            // nA = индекс в kFormulaAcc; receiver + аргументы на стеке
    OP_BUILTIN,        // nA = индекс в kBuiltIn
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_BAND, OP_BOR, OP_BXOR,
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE,
    OP_NOT, OP_JZ, OP_JNZ, OP_JMP,
    OP_VEC2,           // два float -> CVec2
    OP_COMMA,          // оператор запятой C++: левое значение отбрасывается
    OP_CONV,           // nA = тип значения, nB = целевой тип (-1 = ещё не патчен)
    OP_COUNT
  };

  struct Op
  {
    unsigned short nOp;
    int            nA;
    int            nB;
  };

  struct Val
  {
    unsigned char t;                          // FmType из FormulaVM.cpp
    union
    {
      float        f;
      double       d;
      int          i;                         // FT_I и FT_B (bool хранится как int)
      void const * p;
      struct { float x, y; } v2;              // FT_V (layout-совместим с CVec2)
    };
  };

  friend class FormulaVMCompiler;

  int  Emit(int nOp, int nA, int nB);
  int  OpCount() const { return m_nOps; }
  void PatchJump(int nOpIdx, int nTarget);
  void PatchConv(int nOpIdx, int nType);

  int  AddFloat(float f);
  int  AddDouble(double d);
  int  AddString(char const * cpS);

  Val const * Run(void const * pFirst, void const * pSecond, void const * pMisc) const;

  static float  V2F(Val const & v);
  static double V2D(Val const & v);
  static int    V2I(Val const & v);
  static bool   V2B(Val const & v);

  Op *              m_pOps;
  int               m_nOps;
  int               m_nOpsCap;
  float *           m_pFloats;
  double *          m_pDoubles;
  int               m_nDoubles;
  int               m_nDoublesCap;
  int               m_nFloats;
  int               m_nFloatsCap;
  char *            m_pStrPool;               // NUL-терминированные строки
  int               m_nStrPool;
  int               m_nStrPoolCap;
  char const *      m_cpError;
  char              m_errBuf[256];
  char *            m_cpSource;
  char const *      m_cpReturnType;
  mutable int       m_nRuntimeErr;
};

#endif // FORMULAVM_H_
