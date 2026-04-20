#include "stdafx.h"
#include "ClusterConfiguration.h"
#include "System/Commands.h"
#include "Network/FreePortsFinder.h"
#define SERVER_IP "127.0.0.1"
#define MIRROR_SERVER_IP "127.0.0.1"
#define SERVER_PROXY_IP "127.0.0.1"
#define SERVER_PORT "8000"
#define LOGIN_PORT "8001"
#define SERVER_CLUSTER_PORT_BACK 8002
#define SERVER_CLUSTER_PORT_FRONT 8003
#define usedServer 0


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
