path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/renderer.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Wrap DriverVersion usage
content = content.replace(b'    CrashRptWrapper::AddTagToReport( "DisplayDriverVersion"', b'#ifdef _WIN32\n    CrashRptWrapper::AddTagToReport( "DisplayDriverVersion"')
content = content.replace(b'(unsigned)LOWORD( adapId.DriverVersion.LowPart ) ) );', b'(unsigned)LOWORD( adapId.DriverVersion.LowPart ) ) );\n#endif')

# Wrap EnumDisplaySettings loop
content = content.replace(b'  if(pp.Windowed == FALSE)', b'#ifdef _WIN32\n  if(pp.Windowed == FALSE)')
content = content.replace(b'      if(devmode.dmBitsPerPel == 32 && devmode.dmPelsWidth == pp.BackBufferWidth && devmode.dmPelsHeight == pp.BackBufferHeight)\n      {\n        break;\n      }\n    }\n  }', b'      if(devmode.dmBitsPerPel == 32 && devmode.dmPelsWidth == pp.BackBufferWidth && devmode.dmPelsHeight == pp.BackBufferHeight)\n      {\n        break;\n      }\n    }\n  }\n#endif')

with open(path, 'wb') as f:
    f.write(content)
