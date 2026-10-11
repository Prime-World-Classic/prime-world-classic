/*
 * vm_harness.cpp — прогон формул контента через FormulaVM (и, в x86-сборке,
 * через предкомпилированный x86-код) с mock-интерфейсами. Golden-дифф:
 * результаты «VM» и «нативный код» на одинаковых входах обязаны совпадать.
 *
 * Вход: TSV-строки: returnType \t sString \t compiledString(base64, может быть пуст)
 * Запуск: vm_harness <formulas.tsv> [--out results.txt] [--only-compiled]
 */

#include "../../Src/System/FormulaVM.h"
#include "MockFormulaPars.h"

#ifdef HARNESS_NATIVE
// заглушки для минимального набора системных модулей (полный клиент в обвязку
// не линкуем): лог + фабрика объектов (нужна только для REGISTER у MemoryStream)
// нативный прогон blob'а — в отдельном TU (native_exec.cpp), там же и все
// системные зависимости (systemStdAfx)
extern "C" int HarnessNativeRun(char const * cpRetType, char const * cpBase64,
                                void const * pFirst, void const * pSecond, void const * pMisc,
                                double * pResOut);
#endif

#ifdef HARNESS_REF
// эталон x64: тот же C++, что и в контенте, но собранный под x64 (SSE) —
// именно так будет считать будущий x64-клиент
#include "ref_table.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// mock-контекст: значения аксессоров детерминированы от (имя метода, seed формулы)
static unsigned g_seed = 1;
static char     g_strKey[1024] = "";

static MockUnitFormulaPars   g_unit;
static MockMiscFormulaPars   g_misc;
static MockCustomFormulaPars g_custom;

// Контрольная сумма аргументов всех вызванных аксессоров (mock копит её в MockDigest).
// Результат формулы сам по себе не отличает «аргумент передан неверно» от «значение
// такое» — mock-аксессоры возвращают детерминированные значения по имени/слоту и
// аргументы не используют. Без сравнения суммы golden-дифф не видел, что VM передаёт
// в аксессоры мусор (на клиенте это вылезло как AV в dynamic_cast у FindApplicator).
unsigned long long g_mockDigest = 0;
unsigned long long g_mockCallHash = 0x9E3779B97F4A7C15ull;
static unsigned char g_mockSeen[8][512];

// Контрольная сумма строится как СУММА хэшей отдельных вызовов (iface+slot+имя+
// аргументы): порядок вызовов не важен (C++ и VM считают выражения в разном порядке,
// для mock-значений это безразлично), а вот состав вызовов и аргументы — важны.
static unsigned long long MixU(unsigned long long h, unsigned long long v)
{
  h += v;
  h *= 1099511628211ull;
  h ^= h >> 23;
  return h;
}

static void MockFold(int nIface, int nSlot, char const * cpName)
{
  // покрытие: какие vtable-слоты аксессоров вообще проверены golden-диффом
  if (nIface >= 0 && nIface < 8 && nSlot >= 0 && nSlot < 512)
    g_mockSeen[nIface][nSlot] = 1;
  unsigned long long h = 1469598103934665603ull;
  h = MixU(h, (unsigned)(unsigned char)nIface);
  h = MixU(h, (unsigned)(unsigned char)nSlot);
  if (cpName)
    for (; *cpName; ++cpName) h = MixU(h, (unsigned char)*cpName);
  h = MixU(h, g_mockCallHash);
  g_mockDigest += h;
  g_mockCallHash = 0x9E3779B97F4A7C15ull;
}

// MOCK_TRACE=1 — печатает цепочку вызовов аксессоров (только строковые аргументы),
// раздельно для прогона VM и эталона: по расхождению видно, ГДЕ именно VM передаёт
// не то (имя константы/переменной, тип аргумента, лишний или пропущенный вызов).
static int g_mockTrace = -1;
static char const * g_mockPhase = "";

void MockTraceStr(char const * cpName, char const * cpArg)
{
  if (g_mockTrace < 0) g_mockTrace = getenv("MOCK_TRACE") ? 1 : 0;
  if (!g_mockTrace) return;
  char safe[128];
  if (!cpArg) snprintf(safe, sizeof(safe), "(null)");
  else
  {
    safe[0] = 0;
    __try { snprintf(safe, sizeof(safe), "%s", cpArg); }
    __except (EXCEPTION_EXECUTE_HANDLER) { snprintf(safe, sizeof(safe), "<<bad>>"); }
  }
  fprintf(stderr, "  %s %s(%s)\n", g_mockPhase, cpName, safe);
}

void MockDigestBytes(void const * p, unsigned n)
{
  unsigned char const * b = (unsigned char const *)p;
  unsigned long long h = g_mockCallHash * 1099511628211ull + n + 1ull;
  for (unsigned i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  g_mockCallHash = h;
}

static unsigned HashStr(char const * cpS)
{
  unsigned h = 2166136261u;
  while (*cpS) { h ^= (unsigned)(unsigned char)*cpS++; h *= 16777619u; }
  return h;
}

static unsigned Mix(unsigned a, unsigned b)
{
  unsigned h = a * 2654435761u + b * 40503u + 0x9E3779B9u;
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
  return h;
}

float MockF(int nIface, int nSlot, char const * cpName)
{
  MockFold(nIface, nSlot, cpName);
  unsigned h = Mix(HashStr(cpName) ^ HashStr(g_strKey), g_seed + (unsigned)(nIface * 100 + nSlot));
  return (float)((double)(h % 20000) / 100.0 - 100.0);
}

int MockI(int nIface, int nSlot, char const * cpName)
{
  MockFold(nIface, nSlot, cpName);
  unsigned h = Mix(HashStr(cpName) ^ HashStr(g_strKey), g_seed + (unsigned)(nIface * 100 + nSlot) + 7u);
  return (int)(h % 400) - 200;
}

bool MockB(int nIface, int nSlot, char const * cpName)
{
  MockFold(nIface, nSlot, cpName);
  unsigned h = Mix(HashStr(cpName) ^ HashStr(g_strKey), g_seed + (unsigned)(nIface * 100 + nSlot) + 13u);
  return (h >> 5) & 1u;
}

CVec2 MockV(int nIface, int nSlot, char const * cpName)
{
  CVec2 v;
  v.x = MockF(nIface, nSlot, cpName);
  v.y = MockF(nIface, nSlot + 1, cpName);
  return v;
}

void MockX(int nIface, int nSlot, char const * cpName) { MockFold(nIface, nSlot, cpName); }

void const * MockP(int nKind, int nSelfIface)
{
  MockFold(nKind, nSelfIface, 0);
  if (nKind == 0) return (void const *)&g_unit;
  if (nKind == 2) return (void const *)&g_custom;
  return (void const *)&g_misc;
}

// pSecond имеет два статических типа: Execute(..., IUnitFormulaPars const *pSecond, ...)
// и Execute(..., ICustomFormulaPars const *pSecond, ...) (FormulaExecutor.h:52/:62).
// Формулы вида rPos2D обращаются к ICustomFormulaPars, и им вторым аргументом надо
// отдавать mock кастомного интерфейса — один и тот же объект и для VM, и для
// эталонного C++ (иначе сверка бессмысленна, а каст чужого mock'а = AV).
static bool UsesCustomSecond(char const * pSrc)
{
  if (!pSrc) return false;
  for (unsigned i = 0; i < sizeof(kMockCustomNames) / sizeof(kMockCustomNames[0]); ++i)
    if (*kMockCustomNames[i] && strstr(pSrc, kMockCustomNames[i])) return true;
  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct Stats
{
  int nTotal, nOk, nCompileErr, nRunErr;
  int nMatch, nDiff, nNoNative, nDigestDiff;
};

static void PrintHeadline(char const * cpTag, Stats & s)
{
  printf("%s: всего=%d ок=%d ошибок_компиляции=%d ошибок_выполнения=%d",
         cpTag, s.nTotal, s.nOk, s.nCompileErr, s.nRunErr);
#if defined(HARNESS_NATIVE) || defined(HARNESS_REF)
  printf(" | MATCH=%d DIFF=%d NO_REF=%d", s.nMatch, s.nDiff, s.nNoNative);
  printf(" MATCH_NO_DIGEST=%d", s.nDigestDiff);
#endif
  printf("\n");
}

// сравнение результата VM с нативным blob'ом: для float — побитово (ловит
// и NaN/inf, и последний бит), для int/bool — по значению
static bool SameResult(char const * cpRet, double vmRes, double natRes)
{
  if (!strcmp(cpRet, "int"))  return (int)vmRes == (int)natRes;
  if (!strcmp(cpRet, "bool") || !strcmp(cpRet, "boolean"))
    return ((vmRes != 0.0) ? 1 : 0) == ((natRes != 0.0) ? 1 : 0);
  float a = (float)vmRes, b = (float)natRes;
  return memcmp(&a, &b, sizeof(float)) == 0;
}

int main(int argc, char ** argv)
{
  char const * cpIn = (argc > 1) ? argv[1] : 0;
  char const * cpOut = 0;
  int nMaxReport = 25;
  for (int i = 2; i < argc; ++i)
  {
    if (!strncmp(argv[i], "--out=", 6)) cpOut = argv[i] + 6;
    else if (!strncmp(argv[i], "--report=", 9)) nMaxReport = atoi(argv[i] + 9);
  }
  if (!cpIn) { fprintf(stderr, "использование: vm_harness <formulas.tsv> [--out=f] [--report=N]\n"); return 2; }

  FILE * f = fopen(cpIn, "rb");
  if (!f) { fprintf(stderr, "не открыт %s\n", cpIn); return 2; }
  FILE * fo = cpOut ? fopen(cpOut, "wb") : stdout;

  static char line[8192];
  Stats st;
  memset(&st, 0, sizeof(st));
  int nIdx = 0;

  while (fgets(line, sizeof(line), f))
  {
    int n = (int)strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!n) continue;
    char * pRet = line;
    char * pSrc = strchr(line, '\t');
    if (!pSrc) continue;
    *pSrc++ = 0;
    char * pComp = strchr(pSrc, '\t');
    if (pComp) *pComp++ = 0;

    ++st.nTotal;
    ++nIdx;
    g_seed = (unsigned)nIdx * 2654435761u + 12345u;
    strncpy(g_strKey, pSrc, sizeof(g_strKey) - 1);
    g_strKey[sizeof(g_strKey) - 1] = 0;

    if (getenv("VM_TRACE")) fprintf(stderr, "IDX %d [%s]\n", nIdx, pSrc);
    FormulaVM * pVM = FormulaVM::Compile(pSrc, pRet);
    if (!pVM->IsValid())
    {
      ++st.nCompileErr;
      if (st.nCompileErr <= nMaxReport)
        fprintf(stderr, "COMPILE[%d] %s | %s | %s\n", nIdx, pVM->GetError(), pRet, pSrc);
      delete pVM;
      continue;
    }

    if (getenv("VM_TRACE")) fprintf(stderr, "  compiled ok, ops=%d\n", pVM->GetOpCount());
    double res = 0;
    char   resTxt[64];
    void const * pSecond = UsesCustomSecond(pSrc) ? (void const *)&g_custom : (void const *)&g_unit;
    g_mockDigest = 0;
    g_mockCallHash = 0x9E3779B97F4A7C15ull;
    g_mockPhase = "VM ";
    if (!strcmp(pRet, "int"))       res = (double)pVM->ExecuteInt(&g_unit, pSecond, &g_misc);
    else if (!strcmp(pRet, "bool")) res = pVM->ExecuteBool(&g_unit, pSecond, &g_misc) ? 1 : 0;
    else                            res = (double)pVM->ExecuteFloat(&g_unit, pSecond, &g_misc);
    unsigned long long digVM = g_mockDigest;
    if (getenv("VM_TRACE")) fprintf(stderr, "  executed\n");

    int nErr = pVM->GetRuntimeError();
    if (nErr) ++st.nRunErr;
    else      ++st.nOk;

    if (strcmp(pRet, "int") && strcmp(pRet, "bool")) sprintf(resTxt, "%.9g", res);
    else if (!strcmp(pRet, "int"))                   sprintf(resTxt, "%d", (int)res);
    else                                             sprintf(resTxt, "%s", res > 0 ? "1" : "0");

    char cmpTxt[16] = "SKIP";
    double refRes = 0; int nRef = -1;
    unsigned long long digRef = 0;
#ifdef HARNESS_NATIVE
    nRef = HarnessNativeRun(pRet, pComp, &g_unit, pSecond, &g_misc, &refRes);
#endif
#ifdef HARNESS_REF
    if (kRefTableCount > 0)
    {
      int nOk = 0;
      int k = (nIdx - 1) / kRefBatchSize;
      g_mockDigest = 0;
      g_mockCallHash = 0x9E3779B97F4A7C15ull;
      g_mockPhase = "REF";
      if (k < kRefTableCount) refRes = kRefTable[k](nIdx, &g_unit, pSecond, &g_misc, &nOk);
      digRef = g_mockDigest;
      nRef = nOk ? 0 : -1;
    }
#endif
    char refTxt[64];
    if (nRef == 0)
    {
      if (!strcmp(pRet, "int"))                      sprintf(refTxt, "%d", (int)refRes);
      else if (!strcmp(pRet, "bool") || !strcmp(pRet, "boolean"))
                                                     sprintf(refTxt, "%s", refRes != 0.0 ? "1" : "0");
      else                                           sprintf(refTxt, "%.9g", refRes);
      if (SameResult(pRet, res, refRes))
      {
        if (digVM == digRef) { strcpy(cmpTxt, "MATCH"); ++st.nMatch; }
        else
        {
          // значение совпало, но набор вызовов аксессоров другой: для части форм
          // (talconstNoCheck/mastertal/тернарники) эталонный C++ делает лишние
          // вызовы либо идёт другим путём — на результат не влияет, но держим на
          // счету: рост этого числа = подозрение на передачу аргументов.
          strcpy(cmpTxt, "MATCH*"); ++st.nDigestDiff;
          if (st.nDigestDiff <= nMaxReport)
            fprintf(stderr, "MDIG[%d] vm=%s ref=%s dig=%llu/%llu | %s | %s\n", nIdx, resTxt, refTxt,
                    digVM, digRef, pRet, pSrc);
        }
      }
      else
      {
        strcpy(cmpTxt, "DIFF"); ++st.nDiff;
        if (st.nDiff <= nMaxReport)
          fprintf(stderr, "DIFF[%d] vm=%s ref=%s dig=%llu/%llu | %s | %s\n", nIdx, resTxt, refTxt,
                  digVM, digRef, pRet, pSrc);
      }
    }
    else { strcpy(cmpTxt, "NO_REF"); ++st.nNoNative; }
    fprintf(fo, "%d\t%s\t%s\t%s\terr=%d\n", nIdx, pRet, resTxt, cmpTxt, nErr);
    delete pVM;
  }

  // Покрытие таблицы аксессоров. Таблица (имя -> vtable-слот) генерируется из
  // FormulaPars.h по порядку virtual'ов; на x64 сдвиг слота = вызов чужого метода
  // с тем же ABI. Проверены golden-диффом только затронутые корпусом слоты —
  // остальные печатаем списком, чтобы знать слепую зону.
  {
    int nCov = 0, nMiss = 0;
    for (unsigned i = 0; i < kMockAllSlotsCount; ++i)
    {
      MockSlotInfo const & si = kMockAllSlots[i];
      if (si.iface < 8 && si.slot < 512 && g_mockSeen[si.iface][si.slot]) ++nCov;
      else if (nMiss < 40) { fprintf(stderr, "NOCOV[%d] iface=%u slot=%u %s\n", nMiss, si.iface, si.slot, si.name); ++nMiss; }
      else ++nMiss;
    }
    printf("COVER: аксессоров проверено=%d из %u\n", nCov, kMockAllSlotsCount);
  }

  fclose(f);
  if (cpOut && fo) fclose(fo);
  PrintHeadline("VM", st);
  return 0;
}
