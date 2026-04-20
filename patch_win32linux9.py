path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

defs = """
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))

typedef struct _DEVMODE {
  char  dmDeviceName[32];
  WORD  dmSpecVersion;
  WORD  dmDriverVersion;
  WORD  dmSize;
  WORD  dmDriverExtra;
  DWORD dmFields;
  short dmOrientation;
  short dmPaperSize;
  short dmPaperLength;
  short dmPaperWidth;
  short dmScale;
  short dmCopies;
  short dmDefaultSource;
  short dmPrintQuality;
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  char  dmFormName[32];
  WORD  dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  DWORD dmDisplayFlags;
  DWORD dmDisplayFrequency;
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
} DEVMODE;

#define ENUM_REGISTRY_SETTINGS ((DWORD)-1)
inline BOOL EnumDisplaySettings(const char*, DWORD, DEVMODE*) { return FALSE; }
"""

text = text.replace('#define MAX_PATH 260', defs + '\n#define MAX_PATH 260')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
