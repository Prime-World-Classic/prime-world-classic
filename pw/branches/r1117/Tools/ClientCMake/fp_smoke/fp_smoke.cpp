/*
 * fp_smoke.cpp — побитовый стенд трансцендентных x64: проверка, что одинаковый
 * код даёт одинаковые биты на РАЗНЫХ процессорах.
 *
 * Зачем. Авторитет состояния игры держится на консенсусе клиентов. x64 CRT
 * (ucrtbase) выбирает реализацию sin/cos/pow/exp/... по CPUID: на CPU с FMA3
 * работает FMA-вариант, отличающийся от не-FMA варианта на 1 ULP. Стенд печатает
 * побитовые результаты фиксированного набора проб и их хэш — два прогона на
 * разных машинах обязаны сойтись хэшем.
 *
 * Хэш считается по ТОЙ ЖЕ таблице, что и стартовая самопроверка клиента
 * (Src/System/FpDeterminism.cpp → строка "fp: ..." в fastfail.log рядом с exe).
 * Значит один прогон стенда валидирует и золотой эталон, и клиентский бинарь:
 * хэш из лога игрока сравнивается с golden_x64.txt напрямую.
 *
 * Запуск:
 *   fp_smoke.exe                 как в клиенте (FMA3-диспетч CRT выключен)
 *   fp_smoke.exe --fma-default   как в клиенте БЕЗ защиты — A/B: на FMA3-машине
 *                                хэш обязан отличаться (это и есть доказательство
 *                                того, что _set_FMA3_enable(0) необходим)
 *   fp_smoke.exe --bits          дамп каждой пробы (diff между двумя машинами)
 *   fp_smoke.exe --expect <hex>  код возврата 0, если FP-HASH == <hex>
 *
 * Границы применимости (важно, не выдумывать «зелёный» результат):
 *   * под Wine ucrtbase — реализация Wine, а не Microsoft: она честно соблюдает
 *     _set_FMA3_enable (проверено: cos(1) меняется на 1 ULP), но её биты НЕ
 *     обязаны совпадать с настоящим Windows. Золотой эталон, собранный под Wine,
 *     — регрессионная база нашей сборки, а не оракул для игроков; финальная
 *     сверка делается на реальной Windows-машине (tiny10 и любая вторая машина).
 *   * VM может не показывать FMA3 гостю (CPUID фильтруется hypervisor'ом) —
 *     тогда на ней расхождений между FMA/не-FMA путями не увидеть вовсе;
 *     поле cpuFMA3 в выводе отличает «FMA3 нет» от «FMA3 есть, но выключен».
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <intrin.h>

#include "../../../Src/System/FpDeterminismTable.h"

static int CpuBit(int leaf, int sub, int reg, int bit)
{
  int r[4];
  __cpuidex(r, leaf, sub);
  return (r[reg] >> bit) & 1;
}

static const char * FpModel()
{
#if defined(_M_FP_FAST)
  return "fast";
#elif defined(_M_FP_STRICT)
  return "strict";
#elif defined(_M_FP_EXCEPT)
  return "except";
#else
  return "precise(default)";
#endif
}

// Версия ucrtbase.dll: расхождение хэша при одинаковом CPU обычно объясняется
// именно обновлением CRT, поэтому её печатаем рядом с хэшем.
// При /MT (наш клиент) UCRT статический и ucrtbase.dll не загружен — тогда CRT
// взят из тулчейна, и это тоже надо сказать явно, иначе «нет версии» читается
// как поломка.
static void PrintCrt()
{
  HMODULE m = GetModuleHandleA("ucrtbase.dll");
  char path[MAX_PATH];
  if (!m || !GetModuleFileNameA(m, path, sizeof(path)))
  {
#ifdef _DLL
    printf("crt: ucrtbase.dll не найден (ожидался динамический CRT)\n");
#else
    printf("crt: статический UCRT (/MT), тулчейн _MSC_VER=%d\n", _MSC_VER);
#endif
    return;
  }
  DWORD ignore = 0;
  DWORD sz = GetFileVersionInfoSizeA(path, &ignore);
  if (!sz)
  {
    printf("crt: %s (версия не читается)\n", path);
    return;
  }
  char * blob = (char *)malloc(sz);
  if (!blob || !GetFileVersionInfoA(path, 0, sz, blob))
  {
    printf("crt: %s (GetFileVersionInfo не удалась)\n", path);
    free(blob);
    return;
  }
  VS_FIXEDFILEINFO * ffi = NULL;
  UINT len = 0;
  if (VerQueryValueA(blob, "\\", (LPVOID *)&ffi, &len) && ffi)
    printf("crt: ucrtbase %u.%u.%u.%u\n",
           HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
           HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
  else
    printf("crt: %s\n", path);
  free(blob);
}

int main(int argc, char ** argv)
{
  int fmaDefault = 0, wantBits = 0;
  const char * expect = NULL;
  for (int i = 1; i < argc; ++i)
  {
    if (!strcmp(argv[i], "--fma-default"))       fmaDefault = 1;
    else if (!strcmp(argv[i], "--bits"))         wantBits = 1;
    else if (!strcmp(argv[i], "--expect") && i + 1 < argc) expect = argv[++i];
    else
    {
      printf("неизвестный аргумент: %s "
             "(--fma-default | --bits | --expect <hex>)\n", argv[i]);
      return 2;
    }
  }

  printf("fp_smoke: cl _MSC_VER=%d /fp=%s режим=%s\n", _MSC_VER, FpModel(),
         fmaDefault ? "CRT-по-умолчанию (без защиты)" : "FMA3-диспетч выключен");
  printf("cpuFMA3=%d cpuAVX=%d cpuAVX2=%d cpuAVX512F=%d\n",
         CpuBit(1, 0, 2, 12), CpuBit(1, 0, 2, 28), CpuBit(7, 0, 1, 5), CpuBit(7, 0, 1, 16));
  PrintCrt();

  const int before = _get_FMA3_enable();
  if (!fmaDefault)
    _set_FMA3_enable(0);
  const int after = _get_FMA3_enable();
  printf("crtFMA3: before=%d after=%d\n", before, after);

  const PwFpSelfTest st = PwFpSelfTestRun();

  if (wantBits)
  {
    char line[192];
    for (unsigned i = 0; i < PWFP_NSAMPLE; ++i)
    {
      int n = PwFpFormatSample(line, sizeof(line) - 1, i);
      if (n > 0)
      {
        if (n > (int)sizeof(line) - 1)
          n = (int)sizeof(line) - 1;
        line[n] = 0;
        fputs(line, stdout);
      }
    }
  }

  printf("SAMPLES=%u\n", st.count);
  printf("FP-HASH=%016I64X\n", st.hash);
  printf("FP-CTRL=%016I64X\n", st.ctrl);
  printf("FP-DIFF=%016I64X\n", st.diff);

  if (expect)
  {
    char want[32];
    snprintf(want, sizeof(want), "%016I64X", st.hash);
    int ok = !_stricmp(want, expect);
    printf("EXPECT=%s %s\n", expect, ok ? "OK" : "RASSINKRON");
    return ok ? 0 : 1;
  }
  return 0;
}
