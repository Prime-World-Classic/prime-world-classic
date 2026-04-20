import re

path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/LobbyConnection.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Wrap all content after stdafx.h
match = re.search(r'(#include "stdafx\.h"\s*\n)([\s\S]*)', text)
if match:
    stub = """
#ifdef _WIN32
""" + match.group(2) + """
#else
#include "LobbyConnection.h"

Strong<ICastle> CreateCastleLink( int port, const char* castleCmdLine, HINSTANCE _instance, HWND _sessionWnd ) { return NULL; }
Strong<ICastle> CreateDummyCastleLink() { return NULL; }

#endif
"""
    with open(path, "w", encoding="cp1251") as f:
        f.write(match.group(1) + stub)
