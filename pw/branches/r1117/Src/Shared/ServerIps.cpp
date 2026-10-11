// Dynamic server-address registry (see Shared/ServerIps.h).
//
// This used to live in Shared/WebRequests.cpp. That file is compiled only by
// the MSVC (client) build, while Network/ClusterConfiguration.cpp - which the
// Linux server build compiles too - calls GetServerIpA()/GetServerBasePort(),
// so the Linux link failed with undefined references. The registry is
// dependency-free (nstl + PW_Game/server_ip.h), so it can be compiled into
// every build.
//
// Platform detection uses compiler macros, not System/config.h: the Shared
// project is built without WIN32/NV_WIN_PLATFORM (see the pw-client skill), and
// config.h would then hit its "#error Unknown platform" branch.

#include "ServerIps.h"
#include "../PW_Game/server_ip.h"

#include <stdio.h>
#include <string.h>

#if defined( _WIN32 )
  #include <windows.h>
#endif

#define SERVERIPS_ARRAY_COUNT( a ) ( (int)( sizeof( a ) / sizeof( ( a )[ 0 ] ) ) )

int usedServer = 0;

static nstl::vector<nstl::string> g_dynamicServerIpsA;
static nstl::vector<nstl::wstring> g_dynamicServerIpsW;

static bool IsHexDigit(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int HexCharToInt(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

bool ParseServerIpsFromHex(const char* hexBlock, nstl::vector<nstl::string>& outIps)
{
  outIps.clear();

  if (!hexBlock)
    return false;

  size_t len = strlen(hexBlock);
  if (len == 0 || len % 8 != 0)
    return false;

  for (size_t i = 0; i < len; i += 8) {
    unsigned char octet[4];
    for (int j = 0; j < 4; ++j) {
      if (!IsHexDigit(hexBlock[i + j * 2]) || !IsHexDigit(hexBlock[i + j * 2 + 1]))
        return false;
      octet[j] = (unsigned char)((HexCharToInt(hexBlock[i + j * 2]) << 4) | HexCharToInt(hexBlock[i + j * 2 + 1]));
    }
    // "255.255.255.255" is 15 chars + NUL, so buf[16] cannot overflow.
    char buf[16];
    sprintf(buf, "%u.%u.%u.%u", (unsigned)octet[0], (unsigned)octet[1], (unsigned)octet[2], (unsigned)octet[3]);
    outIps.push_back(nstl::string(buf));
  }

  return !outIps.empty();
}

#if !defined( _WIN32 )
// UTF-8 -> wchar_t without <windows.h>. The addresses themselves are ASCII;
// the decoder only has to be correct, not exhaustive.
static void Utf8ToWide(const char * src, size_t len, nstl::wstring & dst)
{
  for (size_t i = 0; i < len; )
  {
    const unsigned char c = (unsigned char)src[i];
    unsigned int cp = c;
    size_t extra = 0;

    if (c >= 0xF0)      { cp &= 0x07; extra = 3; }
    else if (c >= 0xE0) { cp &= 0x0F; extra = 2; }
    else if (c >= 0xC0) { cp &= 0x1F; extra = 1; }

    if (extra && i + extra < len)
    {
      for (size_t k = 1; k <= extra; ++k)
        cp = (cp << 6) | ((unsigned char)src[i + k] & 0x3F);
    }
    i += extra + 1;
    dst.push_back((wchar_t)cp);
  }
}
#endif

void SetDynamicServerIps(const nstl::vector<nstl::string>& ips)
{
  g_dynamicServerIpsA = ips;
  g_dynamicServerIpsW.clear();
  g_dynamicServerIpsW.reserve(ips.size());

#if !defined( _WIN32 )
  for (int i = 0; i < ips.size(); ++i)
  {
    nstl::wstring wide;
    Utf8ToWide(ips[i].c_str(), ips[i].size(), wide);
    g_dynamicServerIpsW.push_back(wide);
  }
#else
  for (int i = 0; i < ips.size(); ++i)
  {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, ips[i].c_str(), (int)ips[i].size(), NULL, 0);
    if (wideLen <= 0)
      continue;
    wchar_t* wbuf = new wchar_t[wideLen + 1];
    MultiByteToWideChar(CP_UTF8, 0, ips[i].c_str(), (int)ips[i].size(), wbuf, wideLen);
    wbuf[wideLen] = 0;
    g_dynamicServerIpsW.push_back(nstl::wstring(wbuf, (size_t)wideLen));
    delete[] wbuf;
  }
#endif
}

int GetServerIpCount()
{
  if (g_dynamicServerIpsA.empty())
    return SERVERIPS_ARRAY_COUNT(SERVER_IP_ARRAY);
  return (int)g_dynamicServerIpsA.size();
}

const char* GetServerIpA(int index)
{
  if (g_dynamicServerIpsA.empty())
    return SERVER_IP_ARRAY[index % SERVERIPS_ARRAY_COUNT(SERVER_IP_ARRAY)];
  int count = (int)g_dynamicServerIpsA.size();
  index %= count;
  if (index < 0)
    index += count;
  return g_dynamicServerIpsA[index].c_str();
}

const wchar_t* GetServerIpW(int index)
{
  if (g_dynamicServerIpsW.empty())
    return SERVER_IP_W_ARRAY[index % SERVERIPS_ARRAY_COUNT(SERVER_IP_W_ARRAY)];
  int count = (int)g_dynamicServerIpsW.size();
  index %= count;
  if (index < 0)
    index += count;
  return g_dynamicServerIpsW[index].c_str();
}

static int g_serverBasePort = 27300;

void SetServerBasePort(int basePort)
{
  if (basePort > 0 && basePort <= 65535)
    g_serverBasePort = basePort;
}

int GetServerBasePort()
{
  return g_serverBasePort;
}
