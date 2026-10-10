#pragma once

#if defined(__linux__)
#include <Vendor/Tamarin/source/core/avmplus.h>
#include "System/StrProc.h"

namespace flash
{
/// Marshal native Linux wchar_t strings through UTF-8 instead of reinterpreting them as UTF-16.
inline avmplus::Stringp CreateAvmStringFromWide(avmplus::AvmCore* core, const wstring& text)
{
	string utf8;
	NStr::UnicodeToUTF8(&utf8, text);
	return core->newStringUTF8(utf8.data(), utf8.size());
}

/// Return native-width text owned by the caller, preserving embedded NULs and supplementary scalars.
inline wstring CreateWideStringFromAvm(avmplus::Stringp text)
{
	avmplus::StUTF8String utf8(text);
	wstring result;
	NStr::UTF8ToUnicode(&result, string(utf8.c_str(), utf8.length()));
	return result;
}
}
#endif
