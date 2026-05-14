#ifndef CONNECTIONSCONTAINER_H_BAB40A8E
#define CONNECTIONSCONTAINER_H_BAB40A8E

#include "../System/Thread.h"

namespace Network
{
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  class Connection;

  class ConnectionsContainer
  {
  public:
    ConnectionsContainer();

    void AddConnection( StrongMT<Connection>& connection );
    void Step();
    void Clear();
    
    threading::Mutex& GetMutex() const { return mutex; }
  private:
    struct ConnSlot
    {
      StrongMT<Connection>   conn;
      timer::Time            expires;

      ConnSlot() : expires( 0 ) {}

      ConnSlot( Connection * _conn, timer::Time _expires ) :
      conn( _conn ),
      expires( _expires )
      {}
    };

    vector<ConnSlot> connections;
    vector< StrongMT< Connection > > toAddConns; // array for new connections
    LONG addedConn; // 0 = false, 1 = true (потокобезопасная замена volatile bool)
    mutable threading::Mutex mutex;

    timer::Time lastConnectionsLeakReport;

    void ProcessConnections( int first, int num );
    void CleanupErasedConnections( timer::Time _now );
  };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
#endif //#define CONNECTIONSCONTAINER_H_BAB40A8E