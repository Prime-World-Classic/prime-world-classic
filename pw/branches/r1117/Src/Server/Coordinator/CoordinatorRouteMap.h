#pragma once
#include "Network/TransportAddress.h"
#include "Network/Address.h"
#include "Coordinator/CoordinatorClientIface.h"


namespace Coordinator
{

class CoordinatorRouteMap
{
public:
  typedef map<Transport::TServiceId, SvcNetAddresses> TServicesMap;

private:
  typedef TServicesMap::iterator TIter;
  typedef map<Transport::TServiceId, vector<Transport::TServiceId> > TServicesByClass;
  typedef vector<StrongMT<ICoordinatorClientRemote> > CoordinatorClientsT;

  TServicesMap routeMap;
  CoordinatorClientsT slaves;

public:

  CoordinatorRouteMap(){}
  ~CoordinatorRouteMap(){};

  void AddRoute( const Transport::TServiceId& service, SvcNetAddresses const & addrs);
  void RemoveRoute( const Transport::TServiceId& service );

  bool FindRoute( SvcNetAddresses & _result, const Transport::TServiceId & _serviceId ) const;

  void AddSlave( ICoordinatorClientRemote * cli );
  void InitSlave( ICoordinatorClientRemote * cli );

  void RemoveSlaveCorpses();
};

} //namespace Coordinator
