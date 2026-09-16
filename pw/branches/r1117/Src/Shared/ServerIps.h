#pragma once

// The game's own STL (nstl) is used for the dynamic IP registry: the MSVC
// std::vector<std::string> path in this build misbehaves under LTCG (release
// invariant check fires on the hex string pointer), while nstl is the STL the
// rest of the game is built on.
#include "../System/nstring.h"
#include "../System/nvector.h"

// Dynamic server address registry.
// IPv4 addresses can be delivered in the launch protocol as a hex-encoded block
// (8 hex chars per address, e.g. "7f000001" == "127.0.0.1").
// When the block is set, GetServerIpA/GetServerIpW/GetServerIpCount use it;
// otherwise they fall back to the static arrays from server_ip.h.
//
// Kept in a dependency-free header so projects without the JsonCpp include
// path (e.g. Network) can use it without pulling in WebRequests.h.
bool ParseServerIpsFromHex(const char* hexBlock, nstl::vector<nstl::string>& outIps);
void SetDynamicServerIps(const nstl::vector<nstl::string>& ips);
int GetServerIpCount();
const char* GetServerIpA(int index);
const wchar_t* GetServerIpW(int index);
