import os

path = "pw/branches/r1117/Src/PW_Client/LobbyConnection.cpp"
os.system("git checkout " + path)

with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap the whole Windows section
text = text.replace('#ifdef _WIN32', '#if defined(_WIN32) && !defined(NV_LINUX_PLATFORM)')

# Add missing stubs for Linux at the end
stubs = r"""
#ifndef _WIN32
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
  return new DummyCaslteLink();
}

Strong<ICastle> CreateDummyCastleLink()
{
  return new DummyCaslteLink();
}

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

# The file already has #else blocks. Let's just append our stubs if they are missing.
if "DummyCaslteLink" not in text:
    text += stubs

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
