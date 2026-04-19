#ifndef LOBBYCLIENTSTATUSCOMPAT_H_INCLUDED
#define LOBBYCLIENTSTATUSCOMPAT_H_INCLUDED

namespace lobby
{

namespace EClientStatus
{
  enum Enum
  {
    Initial,
    Error,
    Disconnected,
    WaitingEntrance,
    RequestingServerInstance,
    WaitingAccounting,
    Connected,
    InCustomLobby,
    ConnectingToGameSvc,
    InGameSession,
    GameFinished,
    LeavingServer
  };
}

namespace EClientError
{
  enum Enum
  {
    NoError,
    PrematureDisconnect,
    DataCorruption,
    ServiceTimeOut,
    ProtocolViolation,
    ServiceDenial
  };
}

} // namespace lobby

#endif // LOBBYCLIENTSTATUSCOMPAT_H_INCLUDED
