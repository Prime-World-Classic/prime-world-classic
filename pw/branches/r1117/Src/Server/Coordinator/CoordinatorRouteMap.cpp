#include "stdafx.h"
#include "CoordinatorRouteMap.h"
#include "RCoordinatorClientIface.auto.h"

namespace Coordinator
{

void CoordinatorRouteMap::AddRoute( const Transport::TServiceId& service, SvcNetAddresses const & addrs )
{
  routeMap[ service ] = addrs;

  // routes �������� ����� ��������
  for ( int i = 0; i < slaves.size(); ++i )
    slaves[i]->AddRoute( service, addrs );
}



void CoordinatorRouteMap::RemoveRoute( const Transport::TServiceId& service )
{
  routeMap.erase( service );

  // routes �������� ����� ��������
  for ( int i = 0; i < slaves.size(); ++i )
    slaves[i]->RemoveRoute( service );
}



bool CoordinatorRouteMap::FindRoute( SvcNetAddresses & _result, const Transport::TServiceId & _serviceId ) const
{
  TServicesMap::const_iterator it = routeMap.find( _serviceId );
  if ( it != routeMap.end() )
  {
    _result = it->second;
    return true;
  }

  return false;
}



void CoordinatorRouteMap::AddSlave( ICoordinatorClientRemote * _cli )
{
  // For remote clients, check for duplicates by entity GUID
  const RICoordinatorClientRemote* remoteCli = dynamic_cast<const RICoordinatorClientRemote*>(_cli);
  if (remoteCli)
  {
    for ( CoordinatorClientsT::iterator it = slaves.begin(); it != slaves.end(); ++it )
    {
      const RICoordinatorClientRemote* existingRemote = dynamic_cast<const RICoordinatorClientRemote*>(&**it);
      if (existingRemote && remoteCli->GetInfo().entityGUID == existingRemote->GetInfo().entityGUID)
      {
        *it = _cli;
        return;
      }
    }
  }

  slaves.push_back( _cli );
}



void CoordinatorRouteMap::InitSlave( ICoordinatorClientRemote * _cli )
{
  // For remote clients, find by entity GUID; for local clients just apply routes directly
  const RICoordinatorClientRemote* remoteCli = dynamic_cast<const RICoordinatorClientRemote*>(_cli);
  if (remoteCli)
  {
    for ( CoordinatorClientsT::const_iterator cit = slaves.begin(); cit != slaves.end(); ++cit )
    {
      const RICoordinatorClientRemote* existingRemote = dynamic_cast<const RICoordinatorClientRemote*>(&**cit);
      if (existingRemote && remoteCli->GetInfo().entityGUID == existingRemote->GetInfo().entityGUID)
      {
        for( TServicesMap::iterator it = routeMap.begin(); it != routeMap.end(); ++it )
          _cli->AddRoute( it->first, it->second );
        return;
      }
    }
  }
  // For local clients, just apply all routes
  for( TServicesMap::iterator it = routeMap.begin(); it != routeMap.end(); ++it )
    _cli->AddRoute( it->first, it->second );
}



void CoordinatorRouteMap::RemoveSlaveCorpses()
{
  for ( CoordinatorClientsT::iterator it = slaves.begin(); it != slaves.end(); )
  {
    // Remote clients can disconnect; local clients are always "connected"
    RICoordinatorClientRemote* remote = dynamic_cast<RICoordinatorClientRemote*>(&**it);
    if (!remote || (remote->GetStatus() == rpc::Connected))
      ++it;
    else
      it = slaves.erase( it );
  }
}

} //namespace Coordinator
