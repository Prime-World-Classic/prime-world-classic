#include "stdafx.h"
#include "ClusterConfiguration.h"
#include "System/Commands.h"
#include "Network/FreePortsFinder.h"
#include "PW_Game/server_ip.h"

// Definition of `usedServer` (declared extern in server_ip.h and WebRequests.h).
// WebRequests.cpp isn't part of the server build on Linux, so we provide it here.
int usedServer = 0;

namespace 
{
  string coordinatorAddr = string(SERVER_IP) + ":" + SERVER_PORT;
  string loginAddr = string(SERVER_IP) + ":" + LOGIN_PORT + "@10";
  int firstServerPort = SERVER_CLUSTER_PORT_BACK;
  int firstServerPortFront = SERVER_CLUSTER_PORT_FRONT;

  string mirror_coordinatorAddr = string(MIRROR_SERVER_IP) + ":" + SERVER_PORT;
  string mirror_loginAddr = string(MIRROR_SERVER_IP) + ":" + LOGIN_PORT + "@10";

  string proxy_coordinatorAddr = string(SERVER_PROXY_IP) + ":" + SERVER_PORT;
  string proxy_loginAddr = string(SERVER_PROXY_IP) + ":" + LOGIN_PORT + "@10";

  string* serverAddrs_coordinator[] = {&coordinatorAddr, &mirror_coordinatorAddr, &proxy_coordinatorAddr};
  string* serverAddrs_login[] = {&loginAddr, &mirror_loginAddr, &proxy_loginAddr};

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
  return *(serverAddrs_coordinator[usedServer]);
}

const string & GetLoginServerAddress()
{
  return *(serverAddrs_login[usedServer]);
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
