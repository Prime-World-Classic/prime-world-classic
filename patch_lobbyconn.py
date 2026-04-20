import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/LobbyConnection.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap all includes after stdafx.h
match = re.search(r'(#include "stdafx\.h"\s*\n)([\s\S]*)', text)
if match:
    stub = """
#ifdef _WIN32
""" + match.group(2) + """
#else

namespace Game {
  LobbyConnection::LobbyConnection( const nstl::string& _address, int _port ) {}
  LobbyConnection::~LobbyConnection() {}
  bool LobbyConnection::Connect() { return false; }
  void LobbyConnection::Disconnect() {}
  bool LobbyConnection::IsConnected() const { return false; }
  void LobbyConnection::Step() {}
  void LobbyConnection::Send( const void* _pData, int _length ) {}
  int LobbyConnection::Receive( void* _pData, int _length ) { return 0; }
  void LobbyConnection::SetNonBlocking( bool _nonBlocking ) {}
  void LobbyConnection::LaunchSync(const char* sessionLogin) {}
}

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(match.group(1) + stub)
