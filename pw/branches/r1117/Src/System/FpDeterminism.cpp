/*
 * FpDeterminism.cpp — x64: FP-детерминизм клиентов.
 *
 * Почему это нужно. Авторитет состояния игры держится на КОНСЕНСУСЕ клиентов:
 * расхождение хотя бы в одном ULP неточной FP-операции (sin/cos/tan/asin/acos/
 * atan/atan2/pow/exp/log) превращает два клиента в два разных «мнения», и
 * рассинхрон неотличим от чита. У x64 CRT (ucrtbase) реализаций две — FMA3 и
 * не-FMA3, выбор идёт по CPUID при старте процесса. Значит «два игрока на
 * разных процессорах» = два разных набора бит. Замерено на i7-7700 (FMA3 есть):
 *
 *     cos(1.0) = 0x3FE14A280FB5068C   FMA3 (состояние CRT по умолчанию)
 *     cos(1.0) = 0x3FE14A280FB5068B   после _set_FMA3_enable(0)
 *
 * Что делаем:
 *   1) _set_FMA3_enable(0) — единая точка: ВСЕ вызовы трансцендентных (наши,
 *      вендорские .lib, CRT) идут в не-FMA3 реализацию. Это главный рычаг:
 *      он закрывает и заранее пересобранные вендорские библиотеки, у которых
 *      свои флаги компилятора.
 *   2) /fp:strict в графе (Tools/ClientCMake/gen_cmake.py) — компилятор не имеет
 *      права менять порядок FP-операций (реассоциация, x/x→1, x*0→0 и т.п.).
 *      Отдельно от (1): (1) про рантайм CRT, (2) про кодогенерацию cl.
 *   3) Побитовая самопроверка (FpDeterminismTable.h) — хэш в diagnostic-лог
 *      рядом с exe. По нему видно, что машина игрока считает иначе, чем золотой
 *      эталон Tools/ClientCMake/fp_smoke, ДО того как игрок дойдёт до рассинхрона.
 *
 * Чего этот файл НЕ решает:
 *   * вендорские .lib, собранные с /fp:fast (их внутренняя кодогенерация уже
 *     зафиксирована в объектах) — закрыто частично: их вызовы CRT под (1) попадают;
 *   * Linux-сервер (glibc, свои реализации) — при участии сервера в FP-споре
 *     нужен отдельный эталон, тут речь только о клиент↔клиент;
 *   * x86/прод-клиент 2.15.x (msvcr90, x87 FSIN) — файл компилируется только
 *     под _M_X64, x86-сборка его не видит.
 */
#ifdef _M_X64

#include <math.h>       // _set_FMA3_enable / _get_FMA3_enable (corecrt_math.h, _M_X64)
#include <intrin.h>     // __cpuid
#include <stdio.h>

#include "FpDeterminismTable.h"

static int s_fma3ForcedOff = 0;

// Возвращает прежнее состояние CRT (1 = FMA3-реализации были включены).
int PwFpDisableFMA3()
{
  const int was = _set_FMA3_enable(0);
  s_fma3ForcedOff = (_get_FMA3_enable() == 0);
  return was;
}

// 1 = CRT сейчас использует FMA3-реализации, 0 = нет, -1 = недоступно (не x64 CRT)
int PwFpFMA3Enabled()
{
  return _get_FMA3_enable();
}

// 1 = мы успешно выключили FMA3-диспетч (самопроверка: CRT принял значение)
int PwFpFMA3ForcedOff()
{
  return s_fma3ForcedOff;
}

// 1 = процессор поддерживает FMA3 (CPUID.1:ECX bit 12). Нужно чтобы отличать
// «FMA3 нет, расхождений быть не могло» от «FMA3 есть, но CRT его не использует».
int PwFpCpuHasFMA3()
{
  int regs[4];
  __cpuid(regs, 1);
  return (regs[2] & (1 << 12)) != 0;
}

static void __cdecl PwFpPreInit()
{
  PwFpDisableFMA3();
}

/*
 * Хук до WinMain. .CRT$XCT линкер сортирует РАНЬШЕ .CRT$XCU, куда кладутся
 * ctors C++, — выключение диспетча успевает до любых FP-вычислений в статических
 * инициализаторах (регистрации фабрик/DB-ресурсов).
 *
 * Проверено отдельным прогоном (MSVC 14.44, /MT, /fp:strict, линковка с /OPT:REF):
 * в main уже _get_FMA3_enable()==0 и cos(1.0)=0x3FE14A280FB5068B (не-FMA вариант),
 * т.е. запись не выбрасывается ни линкером, ни /OPT:REF.
 */
#pragma section(".CRT$XCT", read)
__declspec(allocate(".CRT$XCT")) void (__cdecl * volatile s_pwFpPreInit)(void) = &PwFpPreInit;

// Вызывается из WinMain (Src/PW_Game/PW_Game.cpp). Сам по себе не обязателен —
// хук выше ставится раньше, — но это страховка на случай, если линковка/оптимизация
// когда-нибудь потеряет секцию, и точка, из которой берётся строчка в лог.
extern "C" void __cdecl PwFpInstall()
{
  PwFpDisableFMA3();
  // Чтение записи .CRT$XCT: /OPT:REF выбрасывает данные без живых ссылок,
  // ссылка обязана остаться в коде (volatile не даёт свернуть чтение).
  if (s_pwFpPreInit != &PwFpPreInit)
    PwFpPreInit();
}

extern "C" void PwFpRunSelfTest(PwFpSelfTest * out)
{
  *out = PwFpSelfTestRun();
}

/*
 * Одна строка диагностики. hash сравнивается с золотым эталоном
 * Tools/ClientCMake/fp_smoke/golden_x64.txt; ctrl — с контрольным (точные
 * операции): разошёлся только ctrl ⇒ разные CRT/тулчейны, а не CPU;
 * разошёлся hash при одинаковом ctrl ⇒ разные реализации трансцендентных.
 */
extern "C" int PwFpReport(char * buf, int cap)
{
  const PwFpSelfTest st = PwFpSelfTestRun();
  return snprintf(buf, cap,
    "fp: cpuFMA3=%d crtFMA3=%d forcedOff=%d samples=%u "
    "hash=%016I64X ctrl=%016I64X diff=%016I64X\n",
    PwFpCpuHasFMA3(), PwFpFMA3Enabled(), PwFpFMA3ForcedOff(),
    st.count, st.hash, st.ctrl, st.diff);
}

#endif // _M_X64
