#include "stdafx.h"
#include "ClusterConfiguration.h"
#include "System/Commands.h"
#include "Network/FreePortsFinder.h"
#include "PW_Game/server_ip.h"
#include <Shared/ServerIps.h>


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

const string & GetCoordinatorAddress()
{
  coordinatorAddr = string(GetServerIpA(usedServer)) + ":" + SERVER_PORT;
  return coordinatorAddr;
}

const string & GetLoginServerAddress()
{
  loginAddr = string(GetServerIpA(usedServer)) + ":" + LOGIN_PORT + "@10";
  return loginAddr;
}

int GetFirstServerPortBack()
{
  return firstServerPort;
}

int GetFirstServerPortFront()
{
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
