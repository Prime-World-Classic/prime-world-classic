#include "stdafx.h"
#include "ClusterConfiguration.h"
#include "System/Commands.h"
#include "Network/FreePortsFinder.h"
#include "PW_Game/server_ip.h"
#include <Shared/ServerIps.h>

// `usedServer` is defined once, in Shared/ServerIps.cpp (declared extern
// in server_ip.h / WebRequests.h).

namespace 
{
  // Initialized with the static server_ip.h values (static initialization order is
  // not safe across modules). Network::GetCoordinatorAddress()/GetLoginServerAddress()
  // re-resolve the address from the dynamic server IP registry on every call, so the
  // values below are always up to date by the time the network stack uses them.
  string coordinatorAddr = string(SERVER_IP) + ":" + SERVER_PORT;
  string loginAddr = string(SERVER_IP) + ":" + LOGIN_PORT + "@10";
  int firstServerPort = SERVER_CLUSTER_PORT_BACK;
  int firstServerPortFront = SERVER_CLUSTER_PORT_FRONT;

  string frontendIPAddr = "localhost";
  string backendIPAddr = "localhost";

  REGISTER_VAR( "coordinator_address", coordinatorAddr, STORAGE_GLOBAL );
  REGISTER_VAR( "login_address", loginAddr, STORAGE_GLOBAL );
  REGISTER_VAR( "first_server_port", firstServerPort, STORAGE_GLOBAL );
  REGISTER_VAR( "first_server_port_front", firstServerPortFront, STORAGE_GLOBAL );
  REGISTER_VAR( "frontend_ip_addr", frontendIPAddr, STORAGE_GLOBAL);
  REGISTER_VAR( "backend_ip_addr", backendIPAddr, STORAGE_GLOBAL);
}

namespace Network
{

// Ports are derived from the base port of the target server (6th token of
// the launch protocol; default 27300 — the legacy server_ip.h offsets):
// coordinator=base, login=base+1, front=base+10, back=base+40.
const string & GetCoordinatorAddress()
{
  char portBuf[16];
  sprintf(portBuf, "%d", GetServerBasePort());
  coordinatorAddr = string(GetServerIpA(usedServer)) + ":" + portBuf;
  return coordinatorAddr;
}

const string & GetLoginServerAddress()
{
  char portBuf[16];
  sprintf(portBuf, "%d", GetServerBasePort() + 1);
  loginAddr = string(GetServerIpA(usedServer)) + ":" + portBuf + "@10";
  return loginAddr;
}

int GetFirstServerPortBack()
{
  firstServerPort = GetServerBasePort() + 40;
  return firstServerPort;
}

int GetFirstServerPortFront()
{
  firstServerPortFront = GetServerBasePort() + 10;
  return firstServerPortFront;
}

const string & GetFrontendIPAddr()
{
  return frontendIPAddr;
}

const string & GetBackendIPAddr()
{
  return backendIPAddr;
}

}
