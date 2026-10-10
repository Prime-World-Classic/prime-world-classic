/*
 * x86 + MSVC v143: OpenSSL из Vendor/OpenSSL/lib/static (libeay32MT.lib) собран VC90 и зовёт
 * __iob_func() — функцию старого CRT, которой в UCRT нет (vfprintf закрывает
 * legacy_stdio_definitions.lib). Исходников OpenSSL в дереве нет, пересобрать её нельзя.
 *
 * В libeay32MT __iob_func используется только в аварийных/отладочных путях:
 * OPENSSL_showfatal (печать фатальной ошибки), pem_lib/ui_openssl (консольный ввод пароля),
 * rsa_sign (отладочная печать). Клиент туда не заходит.
 *
 * ВНИМАНИЕ: VC90-код берёт stdout/stderr как &__iob_func()[1]/[2] с sizeof(FILE) VC90 (32 байта),
 * а в UCRT FILE другого размера — корректен только stdin ([0]). Это временная заглушка до
 * пересборки OpenSSL новым компилятором (или перехода на OpenSSL 3.x).
 */
#include <stdio.h>

FILE * __cdecl __iob_func(void)
{
  return __acrt_iob_func(0);
}
