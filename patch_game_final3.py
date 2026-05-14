path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/Game.cpp"
with open(path, "r", encoding="cp1251") as f:
    text = f.read()

# Fix NumProcessRunning issue
# Wait, I already ran patch_game_minimal.py, so it might be broken right now.
# Let's just fix it.
text = text.replace('''#else
int NumProcessRunning(const char* processName) { return 0; }
#endif
    }

    CloseHandle(hProcessSnap);
    return count;
}''', '''        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);
    return count;
}
#endif''')

text = text.replace('''int NumProcessRunning(const char* processName)
{''', '''#ifdef _WIN32
int NumProcessRunning(const char* processName)
{''')

# Fix NBSU SystemReport
text = text.replace('''  CoInitialize(NULL);

  NBSU::SystemReport sysRep;''', '''#ifdef _WIN32
  CoInitialize(NULL);

  NBSU::SystemReport sysRep;''')
text = text.replace('''  sysRep.dumpSystemInfo(true);
}''', '''  sysRep.dumpSystemInfo(true);
#endif
}''')

# Fix CreateFileMapping
text = text.replace('''HANDLE hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RECT), strFileMapName);''', '''#ifdef _WIN32\nHANDLE hFileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RECT), strFileMapName);''')
text = text.replace('''        UnmapViewOfFile(pRect);
      }
    }
  }''', '''        UnmapViewOfFile(pRect);
      }
    }
  }
#endif''')

# Fix DebugCrashNow cast
text = text.replace('''DebugTrace( "Writing address 0x%08x...", (int)nullPtr );''', '''DebugTrace( "Writing address 0x%08x...", (intptr_t)nullPtr );''')

# Fix DumpLoadedModules cast
text = text.replace('''(DWORD)me32.modBaseAddr, me32.modBaseSize, me32.szExePath );''', '''(uintptr_t)me32.modBaseAddr, me32.modBaseSize, me32.szExePath );''')

with open(path, "w", encoding="cp1251") as f:
    f.write(text)
print("Patched Game.cpp again")
