/*
 * FpDeterminismTable.h — общий набор побитовых проб трансцендентных для x64.
 *
 * Зачем. Авторитет состояния игры достигается КОНСЕНСУСОМ клиентов: если два
 * x64-клиента получают разные биты от sin/cos/pow/exp, консенсус расходится, и
 * рассинхрон неотличим от чита. x64 CRT (ucrtbase) выбирает реализацию
 * трансцендентных по CPUID: на CPU с FMA3 (Intel Haswell+ / AMD Excavator+)
 * выполняется FMA-вариант, отличающийся от не-FMA варианта на 1 ULP. Замерено
 * на этой машине (i7-7700, FMA3 есть, MSVC 14.44 + ucrtbase):
 *
 *     cos(1.0) = 0x3FE14A280FB5068C   при FMA3 (состояние CRT по умолчанию)
 *     cos(1.0) = 0x3FE14A280FB5068B   после _set_FMA3_enable(0)
 *
 * т.е. риск реален и выключается рантайм-переключателем CRT (см.
 * System/FpDeterminism.cpp).
 *
 * Таблица ОДНА на два места: самопроверка клиента (System/FpDeterminism.cpp,
 * строчка в логе рядом с exe) и автономный стенд
 * Tools/ClientCMake/fp_smoke/fp_smoke.cpp. Хэш обязан совпадать байт-в-байт,
 * иначе стенд не валидирует клиент.
 *
 * Аргументы читаются через volatile: константные аргументы cl сворачивает на
 * компиляции, и проверка переставала бы проверять рантайм CRT.
 *
 * Точные по IEEE операции (sqrt/fmod/fabs/floor/ceil) положены в таблицу как
 * КОНТРОЛЬ: они не зависят от CPU. Если расходится только контрольный хэш —
 * виноват не CPU, а разные CRT/тулчейны (или разная разрядность).
 */
#ifndef PW_FP_DETERMINISM_TABLE_H
#define PW_FP_DETERMINISM_TABLE_H

#include <math.h>
#include <stddef.h>
#include <stdio.h>   // snprintf (PwFpFormatSample)

// Побитовое представление FP-значения. Union, а не memcpy: заголовок подхватывают
// разные TU, таскать <string.h> ради двух преобразований не хочется.
union PwFpBitsD { double d; unsigned long long b; };
union PwFpBitsF { float f; unsigned b; };

enum PwFpFn
{
  // неточные: реализация CRT зависит от CPU (FMA3 dispatch) — это предмет проверки
  PWFP_SIN = 0, PWFP_COS, PWFP_TAN, PWFP_ASIN, PWFP_ACOS, PWFP_ATAN,
  PWFP_ATAN2, PWFP_POW, PWFP_EXP, PWFP_LOG, PWFP_LOG10,
  // точные по IEEE-754: одинаковы на любом x64 — контрольная база
  PWFP_SQRT, PWFP_FMOD, PWFP_FABS, PWFP_FLOOR, PWFP_CEIL,
  PWFP_NFN
};
#define PWFP_EXACT_FIRST PWFP_SQRT   // индексы >= PWFP_EXACT_FIRST — контрольные

struct PwFpSample
{
  unsigned char fn;
  double a, b;                       // b — только для бинарных (atan2/pow/fmod)
};

#define PWFP1(f, a)          { (unsigned char)(f), (double)(a), 0.0 }
#define PWFP2(f, a, b)       { (unsigned char)(f), (double)(a), (double)(b) }

/* Набор аргументов покрыл то, что реально зовёт игровая логика (см. grep по Src:
 * sin/cos/atan2/atan/acos/asin/sqrt/pow/exp/log/tan/fmod/fabs/floor/ceil), плюс
 * точки, где FMA- и не-FMA-реализации расходятся охотнее всего: аргументное
 * приведение больших углов (1e7, 1e15), окрестности полюсов и единиц,
 * денормальные/малые аргументы, дробные степени. */
static PwFpSample const s_fpSamples[] =
{
  // --- sin / cos / tan: аргументное приведение — главное место расхождения
  PWFP1(PWFP_SIN,  0.0),        PWFP1(PWFP_SIN,  1e-9),
  PWFP1(PWFP_SIN,  0.1),        PWFP1(PWFP_SIN,  0.5),
  PWFP1(PWFP_SIN,  1.0),        PWFP1(PWFP_SIN,  0.7853981633974483),
  PWFP1(PWFP_SIN,  1.5707963267948966), PWFP1(PWFP_SIN, 3.1415926535897931),
  PWFP1(PWFP_SIN,  6.2831853071795862), PWFP1(PWFP_SIN, -1.0),
  PWFP1(PWFP_SIN,  -3.5),       PWFP1(PWFP_SIN,  10.0),
  PWFP1(PWFP_SIN,  1000.0),     PWFP1(PWFP_SIN,  3.1415926535897931e7),
  PWFP1(PWFP_SIN,  1e15),

  PWFP1(PWFP_COS,  0.0),        PWFP1(PWFP_COS,  1e-9),
  PWFP1(PWFP_COS,  0.1),        PWFP1(PWFP_COS,  1.0),
  PWFP1(PWFP_COS,  1.5707963267948966), PWFP1(PWFP_COS, 3.1415926535897931),
  PWFP1(PWFP_COS,  6.2831853071795862), PWFP1(PWFP_COS, -0.25),
  PWFP1(PWFP_COS,  10.0),       PWFP1(PWFP_COS,  1000.0),
  PWFP1(PWFP_COS,  3.1415926535897931e7), PWFP1(PWFP_COS, 1e15),

  PWFP1(PWFP_TAN,  0.0),        PWFP1(PWFP_TAN,  0.1),
  PWFP1(PWFP_TAN,  0.7853981633974483), PWFP1(PWFP_TAN, 1.5707963267948966),
  PWFP1(PWFP_TAN,  1.25),       PWFP1(PWFP_TAN,  -2.0),
  PWFP1(PWFP_TAN,  1000.0),

  // --- asin / acos: окрестности +/-1 (там же и домен, и максимум погрешности)
  PWFP1(PWFP_ASIN, -1.0),       PWFP1(PWFP_ASIN, -0.9999999),
  PWFP1(PWFP_ASIN, -0.5),       PWFP1(PWFP_ASIN, -1e-9),
  PWFP1(PWFP_ASIN,  0.0),       PWFP1(PWFP_ASIN,  1e-9),
  PWFP1(PWFP_ASIN,  0.25),      PWFP1(PWFP_ASIN,  0.9999999),
  PWFP1(PWFP_ASIN,  1.0),

  PWFP1(PWFP_ACOS, -1.0),       PWFP1(PWFP_ACOS, -0.9999999),
  PWFP1(PWFP_ACOS, -0.5),       PWFP1(PWFP_ACOS,  0.0),
  PWFP1(PWFP_ACOS,  1e-9),      PWFP1(PWFP_ACOS,  0.5),
  PWFP1(PWFP_ACOS,  0.9999999), PWFP1(PWFP_ACOS,  1.0),

  // --- atan / atan2: все четверти + малые/большие отношения
  PWFP1(PWFP_ATAN, -1e9),       PWFP1(PWFP_ATAN, -1.0),
  PWFP1(PWFP_ATAN, -0.5),       PWFP1(PWFP_ATAN,  0.0),
  PWFP1(PWFP_ATAN,  0.5),       PWFP1(PWFP_ATAN,  1.0),
  PWFP1(PWFP_ATAN,  1e9),

  PWFP2(PWFP_ATAN2,  1.0,  1.0),      PWFP2(PWFP_ATAN2,  1.0, -1.0),
  PWFP2(PWFP_ATAN2, -1.0,  1.0),      PWFP2(PWFP_ATAN2, -1.0, -1.0),
  PWFP2(PWFP_ATAN2,  0.0,  1.0),      PWFP2(PWFP_ATAN2,  0.0, -1.0),
  PWFP2(PWFP_ATAN2,  1.0,  0.0),      PWFP2(PWFP_ATAN2, -1.0,  0.0),
  PWFP2(PWFP_ATAN2,  1e-9, 1.0),      PWFP2(PWFP_ATAN2,  3.75, -2.25),
  PWFP2(PWFP_ATAN2, -0.5,  0.25),     PWFP2(PWFP_ATAN2, 1e300, 1e-300),

  // --- pow: дробные показатели и окрестность 1 (экспонента/логарифм внутри)
  PWFP2(PWFP_POW,  2.0, 0.5),        PWFP2(PWFP_POW,  9.0, 0.5),
  PWFP2(PWFP_POW,  10.0, 3.7),       PWFP2(PWFP_POW,  1.5, 2.5),
  PWFP2(PWFP_POW,  0.5, -3.0),       PWFP2(PWFP_POW,  3.0, 0.0),
  PWFP2(PWFP_POW,  2.0, 10.0),       PWFP2(PWFP_POW,  2.718281828459045, 1.0),
  PWFP2(PWFP_POW,  1.0000001, 1e6),  PWFP2(PWFP_POW,  0.9999999, -1e6),
  PWFP2(PWFP_POW,  1e-8, 3.0),       PWFP2(PWFP_POW,  7.0, 0.3333333333333333),

  // --- exp / log / log10
  PWFP1(PWFP_EXP,  0.0),        PWFP1(PWFP_EXP,  0.5),
  PWFP1(PWFP_EXP,  1.0),        PWFP1(PWFP_EXP,  -1.0),
  PWFP1(PWFP_EXP,  10.0),       PWFP1(PWFP_EXP,  -10.0),
  PWFP1(PWFP_EXP,  -100.0),     PWFP1(PWFP_EXP,  0.6931471805599453),
  PWFP1(PWFP_EXP,  24.0),

  PWFP1(PWFP_LOG,  1.0),        PWFP1(PWFP_LOG,  2.0),
  PWFP1(PWFP_LOG,  0.5),        PWFP1(PWFP_LOG,  1e-8),
  PWFP1(PWFP_LOG,  1e8),        PWFP1(PWFP_LOG,  2.718281828459045),
  PWFP1(PWFP_LOG,  1.0000001),  PWFP1(PWFP_LOG,  0.9999999),

  PWFP1(PWFP_LOG10, 1.0),       PWFP1(PWFP_LOG10, 1000.0),
  PWFP1(PWFP_LOG10, 1e-3),      PWFP1(PWFP_LOG10, 2.0),

  // --- контрольные (точные): должны совпадать на ЛЮБОМ x64 и любом CRT
  PWFP1(PWFP_SQRT, 2.0),        PWFP1(PWFP_SQRT, 0.5),
  PWFP1(PWFP_SQRT, 1e300),      PWFP1(PWFP_SQRT, 1e-300),
  PWFP2(PWFP_FMOD, 7.0, 3.0),   PWFP2(PWFP_FMOD, -7.0, 3.0),
  PWFP2(PWFP_FMOD, 1e21, 10.0), PWFP2(PWFP_FMOD, 0.1, 0.03),
  PWFP1(PWFP_FABS, -2.5),       PWFP1(PWFP_FABS, 2.5),
  PWFP1(PWFP_FLOOR, -2.5),      PWFP1(PWFP_FLOOR, 2.5),
  PWFP1(PWFP_FLOOR, 1e15),      PWFP1(PWFP_CEIL, -2.5),
  PWFP1(PWFP_CEIL, 2.5),        PWFP1(PWFP_CEIL, -1e15),
};

#undef PWFP1
#undef PWFP2

#define PWFP_NSAMPLE ((unsigned)(sizeof(s_fpSamples) / sizeof(s_fpSamples[0])))

inline double PwFpEvalD(unsigned fn, double a, double b)
{
  switch (fn)
  {
  case PWFP_SIN:    return sin(a);
  case PWFP_COS:    return cos(a);
  case PWFP_TAN:    return tan(a);
  case PWFP_ASIN:   return asin(a);
  case PWFP_ACOS:   return acos(a);
  case PWFP_ATAN:   return atan(a);
  case PWFP_ATAN2:  return atan2(a, b);
  case PWFP_POW:    return pow(a, b);
  case PWFP_EXP:    return exp(a);
  case PWFP_LOG:    return log(a);
  case PWFP_LOG10:  return log10(a);
  case PWFP_SQRT:   return sqrt(a);
  case PWFP_FMOD:   return fmod(a, b);
  case PWFP_FABS:   return fabs(a);
  case PWFP_FLOOR:  return floor(a);
  case PWFP_CEIL:   return ceil(a);
  default:          return 0.0;
  }
}

inline float PwFpEvalF(unsigned fn, float a, float b)
{
  switch (fn)
  {
  case PWFP_SIN:    return sinf(a);
  case PWFP_COS:    return cosf(a);
  case PWFP_TAN:    return tanf(a);
  case PWFP_ASIN:   return asinf(a);
  case PWFP_ACOS:   return acosf(a);
  case PWFP_ATAN:   return atanf(a);
  case PWFP_ATAN2:  return atan2f(a, b);
  case PWFP_POW:    return powf(a, b);
  case PWFP_EXP:    return expf(a);
  case PWFP_LOG:    return logf(a);
  case PWFP_LOG10:  return log10f(a);
  case PWFP_SQRT:   return sqrtf(a);
  case PWFP_FMOD:   return fmodf(a, b);
  case PWFP_FABS:   return fabsf(a);
  case PWFP_FLOOR:  return floorf(a);
  case PWFP_CEIL:   return ceilf(a);
  default:          return 0.0f;
  }
}

// Имя функции в хэш тоже подмешивается: перестановка строк таблицы меняет хэш
// (иначе стенд не заметил бы, что набор проб «тихо» поменяли).
inline const char * PwFpFnName(unsigned fn)
{
  static const char * const names[PWFP_NFN] =
  {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "pow", "exp", "log",
    "log10", "sqrt", "fmod", "fabs", "floor", "ceil"
  };
  return fn < PWFP_NFN ? names[fn] : "?";
}

// FNV-1a 64: побайтно, порядок байт — нативный x64 (little-endian) с обеих
// сторон, поэтому хэш одинаков у клиента и у стенда.
inline unsigned long long PwFpHashFeed(unsigned long long h, const void * p, size_t n)
{
  const unsigned char * b = (const unsigned char *)p;
  for (size_t i = 0; i < n; ++i)
  {
    h ^= (unsigned long long)b[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

struct PwFpSelfTest
{
  unsigned long long hash;   // все пробы (double + float), включая контрольные
  unsigned long long ctrl;   // только точные операции — контроль harness'а
  unsigned long long diff;   // XOR double-результатов неточных проб: 0 = совпали
  unsigned count;
};

#define PWFP_FNV_OFFSET 0xcbf29ce484222325ULL

inline PwFpSelfTest PwFpSelfTestRun()
{
  PwFpSelfTest r;
  r.hash = PWFP_FNV_OFFSET;
  r.ctrl = PWFP_FNV_OFFSET;
  r.diff = 0;
  r.count = 0;
  for (unsigned i = 0; i < PWFP_NSAMPLE; ++i)
  {
    // volatile-чтение: иначе cl сворачивает вызовы с константными аргументами
    // в константу, и хэш перестает зависеть от рантайма CRT.
    volatile double ra = s_fpSamples[i].a;
    volatile double rb = s_fpSamples[i].b;
    const unsigned fn = s_fpSamples[i].fn;
    const double a = ra, b = rb;

    const double d = PwFpEvalD(fn, a, b);
    const float  f = PwFpEvalF(fn, (float)a, (float)b);

    r.hash = PwFpHashFeed(r.hash, &fn, sizeof(fn));
    r.hash = PwFpHashFeed(r.hash, &d, sizeof(d));
    r.hash = PwFpHashFeed(r.hash, &f, sizeof(f));

    PwFpBitsD bd;
    bd.d = d;
    if (fn >= PWFP_EXACT_FIRST)
      r.ctrl = PwFpHashFeed(r.ctrl, &bd.b, sizeof(bd.b));
    else
      r.diff ^= bd.b ^ (unsigned long long)i;    // индекс пробы — против перестановок

    ++r.count;
  }
  return r;
}

// Побитовый дамп одной пробы (для --bits в стенде и для диффа «кто именно
// расходится»). Формат строки зафиксирован: его сравнивает run_fp_smoke.sh.
inline int PwFpFormatSample(char * buf, int cap, unsigned i)
{
  volatile double ra = s_fpSamples[i].a;
  volatile double rb = s_fpSamples[i].b;
  const unsigned fn = s_fpSamples[i].fn;
  const double a = ra, b = rb;
  const double d = PwFpEvalD(fn, a, b);
  const float  f = PwFpEvalF(fn, (float)a, (float)b);
  PwFpBitsD bd; bd.d = d;
  PwFpBitsF bf; bf.f = f;
  return snprintf(buf, cap, "%3u %-6s a=%+.17g b=%+.17g d=%016I64X f=%08X\n",
                   i, PwFpFnName(fn), a, b, bd.b, bf.b);
}

#endif // PW_FP_DETERMINISM_TABLE_H
