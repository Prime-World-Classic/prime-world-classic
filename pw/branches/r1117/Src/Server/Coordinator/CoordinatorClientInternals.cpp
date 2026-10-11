#include "stdafx.h"
#include "CoordinatorClientInternals.h"

namespace Coordinator
{

void ClassRoutes::AddClassRoute( const Transport::TServiceId & _svcId )
{
  Transport::TServiceId serviceClass;
  Transport::GetServiceClass( _svcId, &serviceClass );

  threading::MutexLock lock( mutex );

  Transport::TServiceId & slot = routes[serviceClass];
  if ( slot == _svcId )
    return;

  DebugTrace( "Updating fresh class routes. svc_class=%s, svc=%s, prev_svc=%s", serviceClass.c_str(), _svcId.c_str(), slot.c_str() );
  slot = _svcId;
}



Transport::TServiceId ClassRoutes::FindClassLastService( const Transport::TServiceId & _svcClass ) const
{
  threading::MutexLock lock( mutex );

  // The map is keyed by service class, but callers may pass a full instance
  // id ("lobby/1", "lobby_00") — extract the class before the lookup.
  // (Without this, lookups by instance id never hit, and clients requesting
  // "lobby/1" could not be mapped to the local "lobby_00" instance.)
  Transport::TServiceId classKey = _svcClass;
  Transport::TServiceId svcClass;
  if ( Transport::GetServiceClass( _svcClass, &svcClass ) )
    classKey = svcClass;

  TMap::const_iterator it = routes.find( classKey );
  if ( it != routes.end() )
    return it->second;
  return _svcClass;
}










AddressTranslator::AddressTranslator( const char * _tag, ClassRoutes * _classRoutes ) :
tag( _tag ),
classRoutes( _classRoutes )
{}



void AddressTranslator::AddRoute( const Transport::TServiceId & _svcId, Network::NetAddress const & _addr )
{
  DebugTrace( "Adding permanent route. tag=%s, svc=%s, addr=%s", tag, _svcId.c_str(), _addr );

  threading::MutexLock lock( mutex );
  permanentRoutes[_svcId] = _addr;
}



void AddressTranslator::RemoveRoute( const Transport::TServiceId & _svcId )
{
  DebugTrace( "Removing permanent route. tag=%s, svc=%s", tag, _svcId.c_str() );

  threading::MutexLock lock( mutex );
  permanentRoutes.erase( _svcId );
}



Network::NetAddress AddressTranslator::GetSvcAddress( const Transport::TServiceId & _serviceId )
{
  threading::MutexLock lock( mutex );

  // 1) Exact lookup: works when the route is defined for this very id
  //    (the original behavior; routes are keyed by instance id on Windows
  //    and by "class_NN" on the Linux single-process deployment).
  TRouteMap::const_iterator it = freshRoutes.find( _serviceId );
  if( it != freshRoutes.end() )
    return it->second;

  TRouteMap::const_iterator it2 = permanentRoutes.find( _serviceId );
  if ( it2 != permanentRoutes.end() )
    return it2->second;

  // 2) Resolve the service class ("lobby/1" -> class "lobby") to the last
  //    registered instance and look the route up by the resolved id.
  Transport::TServiceId serviceId = classRoutes->FindClassLastService( _serviceId );
  if ( serviceId != _serviceId )
  {
    TRouteMap::const_iterator it3 = freshRoutes.find( serviceId );
    if( it3 != freshRoutes.end() )
      return it3->second;

    TRouteMap::const_iterator it4 = permanentRoutes.find( serviceId );
    if ( it4 != permanentRoutes.end() )
      return it4->second;
  }

  return Network::NetAddress();
}


void AddressTranslator::DefineRoute( const Transport::TServiceId & _service, const Network::NetAddress & _naddr )
{
  DebugTrace( "Updating fresh routes. tag=%s, svc=%s, addr=%s", tag, _service.c_str(), _naddr );

  {
    threading::MutexLock lock( mutex );
    {
      Network::NetAddress & slot = freshRoutes[_service];
      NI_ASSERT( slot.empty(), NI_STRFMT( "Re-defining fresh route. tag=%s, svc=%s, new_addr=%s, prev_addr=%s", tag, _service.c_str(), _naddr, slot ) );
      slot = _naddr;
    }
  }

  classRoutes->AddClassRoute( _service );
}


void AddressTranslator::DefineAliasRoute( const Transport::TServiceId & _service, const Transport::TServiceId & _alias )
{
  threading::MutexLock lock( mutex );

  TRouteMap::iterator it = freshRoutes.find( _service );
  if ( it == freshRoutes.end() )
  {
    ErrorTrace( "Anknown service, fresh route alias not defined. tag=%s, svc=%s, alias=%s", tag, _service.c_str(), _alias.c_str() );
    return;
  }

  Network::NetAddress netaddr = it->second;

  DebugTrace( "Adding fresh route alias. tag=%s, svc=%s, alias=%s, addr=%s", tag, _service.c_str(), _alias.c_str(), netaddr );

  Network::NetAddress & slot = freshRoutes[_alias];
  NI_VERIFY( slot.empty(), NI_STRFMT( "Duplicate fresh alias route. tag=%s, svc=%s, new_addr=%s, prev_addr=%s", tag, _service.c_str(), netaddr, slot ), return );
  slot = netaddr;
}


Transport::TServiceId AddressTranslator::GetLastServiceInstance( const Transport::TServiceId & _svcClass )
{
  return classRoutes->FindClassLastService( _svcClass );
}


void AddressTranslator::PopFreshRoutes( TRouteMap & _result )
{
  threading::MutexLock lock( mutex );

  _result.clear();
  _result.swap( freshRoutes );
}

} //namespace Coordinator
