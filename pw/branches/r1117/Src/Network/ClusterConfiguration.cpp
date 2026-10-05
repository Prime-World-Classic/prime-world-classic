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

  // Set only by the CLIENT (Network::SetClusterBasePort, called when the
  // pwclassic:// launch protocol is parsed). The server never calls it, so on
  // the server these addresses stay exactly what server_ip.h / the Profiles
  // cfg say (`setvar login_address = 0.0.0.0:27301`) - deriving them from the
  // registry here used to bind login to 127.0.0.1 and made the server
  // unreachable from the LAN (E2E on tiny10: "Active handshake timed out").
  bool s_clusterFromProtocolBase = false;

  REGISTER_VAR( "coordinator_address", coordinatorAddr, STORAGE_GLOBAL );
  REGISTER_VAR( "login_address", loginAddr, STORAGE_GLOBAL );
  REGISTER_VAR( "first_server_port", firstServerPort, STORAGE_GLOBAL );
  REGISTER_VAR( "first_server_port_front", firstServerPortFront, STORAGE_GLOBAL );
  REGISTER_VAR( "frontend_ip_addr", frontendIPAddr, STORAGE_GLOBAL);
  REGISTER_VAR( "backend_ip_addr", backendIPAddr, STORAGE_GLOBAL);
}

namespace Network
{

// The client derives the whole cluster addressing from the launch protocol:
// IP block (5th token, hex) + base port (6th token, decimal).
// coordinator=base, login=base+1, front=base+10, back=base+40.
void SetClusterBasePort(int basePort)
{
  SetServerBasePort(basePort);
  s_clusterFromProtocolBase = true;
}

const string & GetCoordinatorAddress()
{
  if (!s_clusterFromProtocolBase)
    return coordinatorAddr;   // server_ip.h default, may be overridden by cfg

  char portBuf[16];
  sprintf(portBuf, "%d", GetServerBasePort());
  coordinatorAddr = string(GetServerIpA(usedServer)) + ":" + portBuf;
  return coordinatorAddr;
}

const string & GetLoginServerAddress()
{
  if (!s_clusterFromProtocolBase)
    return loginAddr;         // server_ip.h default, may be overridden by cfg

  char portBuf[16];
  sprintf(portBuf, "%d", GetServerBasePort() + 1);
  loginAddr = string(GetServerIpA(usedServer)) + ":" + portBuf + "@10";
  return loginAddr;
}

int GetFirstServerPortBack()
{
  if (!s_clusterFromProtocolBase)
    return firstServerPort;

  firstServerPort = GetServerBasePort() + 40;
  return firstServerPort;
}

int GetFirstServerPortFront()
{
  if (!s_clusterFromProtocolBase)
    return firstServerPortFront;

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
