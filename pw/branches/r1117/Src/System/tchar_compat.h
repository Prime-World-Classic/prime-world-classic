#pragma once

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifndef WCHAR
typedef wchar_t WCHAR;
#endif

#ifndef TEXT
#define TEXT(x) x
#endif

#ifndef _T
#define _T(x) TEXT(x)
#endif

inline errno_t _tcscat_s(TCHAR* destination, size_t destinationCount, const TCHAR* source)
{
  if (!destination || !source || destinationCount == 0)
    return EINVAL;

  const size_t destinationLength = strlen(destination);
  const size_t sourceLength = strlen(source);
  if (destinationLength + sourceLength + 1 > destinationCount)
  {
    destination[0] = '\0';
    return ERANGE;
  }

  memcpy(destination + destinationLength, source, sourceLength + 1);
  return 0;
}

inline int _stprintf_s(TCHAR* buffer, size_t bufferCount, const TCHAR* format, ...)
{
  if (!buffer || !format || bufferCount == 0)
    return -1;

  va_list args;
  va_start(args, format);
  const int result = vsnprintf(buffer, bufferCount, format, args);
  va_end(args);

  if (result < 0)
    buffer[0] = '\0';

  return result;
}

inline int sprintf_s(char* buffer, size_t bufferCount, const char* format, ...)
{
  if (!buffer || !format || bufferCount == 0)
    return -1;

  va_list args;
  va_start(args, format);
  const int result = vsnprintf(buffer, bufferCount, format, args);
  va_end(args);

  if (result < 0)
    buffer[0] = '\0';

  return result;
}
