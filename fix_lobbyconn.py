import re
import os

path = "pw/branches/r1117/Src/PW_Client/LobbyConnection.cpp"
os.system("git checkout pw/branches/r1117/Src/PW_Client/LobbyConnection.cpp")

with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Remove the nested ifdefs that cause issues
text = text.replace('#include "stdafx.h"\n\n\n#ifdef _WIN32\n#ifdef _WIN32\n#include "LobbyConnection.h"', '#include "stdafx.h"\n#include "LobbyConnection.h"\n\n#ifdef _WIN32')

# Comprehensive rewrite of the bottom
new_code = r"""
class DummyCaslteLink : public ICastle, public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_2( DummyCaslteLink, ICastle, BaseObjectST );
public:
  DummyCaslteLink() {}
  virtual void Update( float seconds ) {}
  virtual void StartRender() {}
  virtual void ReturnToCastle() {}
  virtual void QuitGame() {}
};
NI_DEFINE_REFCOUNT(DummyCaslteLink);

Strong<ICastle> CreateCastleLink( int port, const char* castleCmdLine, HINSTANCE _instance, HWND _sessionWnd  )
{
#ifdef _WIN32
  return new RealCastleLink( port, castleCmdLine, _instance, _sessionWnd );
#else
  return new DummyCaslteLink();
#endif
}

Strong<ICastle> CreateDummyCastleLink()
{
  return new DummyCaslteLink();
}

#ifndef _WIN32
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

# Find where CreateCastleLink starts and replace until the end
text = re.sub(r'Strong<ICastle> CreateCastleLink[\s\S]*$', new_code, text)

# Close the open ifdefs from the beginning of the file
# The file starts with two #ifdef _WIN32 (due to my replacement above)
# Wait, my replacement above changed it to ONE #ifdef _WIN32.

# Let's check how many #ifdef _WIN32 are there.
count = text.count('#ifdef _WIN32')
print(f"Found {count} #ifdef _WIN32")

# We need to add corresponding #endif's BEFORE our new_code.
text = text.replace(new_code, "\n#endif\n" + new_code)

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
