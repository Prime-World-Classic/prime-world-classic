#pragma once

namespace Network
{

const string & GetCoordinatorAddress();
const string & GetLoginServerAddress();
int GetFirstServerPortBack();
int GetFirstServerPortFront();

// Client only: switch the cluster addressing to "derived from the launch
// protocol" (IP block + base port). Without this call the getters return the
// server_ip.h / Profiles-cfg values (production server behavior).
void SetClusterBasePort(int basePort);

string const & GetFrontendIPAddr();
string const & GetBackendIPAddr();

} //namespace Network
