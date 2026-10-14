// stdafx.h сюда НЕ кладут: модуль намеренно самодостаточен (только C-stdlib,
// std::vector и zlib), благодаря чему он компилируется и проверяется вне
// графа клиента — tools/screenshot_test (нативный g++, без Wine и без D3D9).
// В графе клиента TU оборачивается в pch-обёртку, которая сама включает
// stdafx.h (Tools/ClientCMake/pch_rules.py), поэтому своему TU он не нужен.
#include "screenshot.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "zlib.h"   // Vendor/zlib (в Render.vcproj уже есть include и zdll.lib)

namespace Render {

namespace {

inline void PutU32LE(unsigned char* p, unsigned v) {
  p[0] = (unsigned char)(v);       p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

inline void PutU32BE(unsigned char* p, unsigned v) {
  p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)(v);
}

// Пиксель источника (B,G,R,A или R,G,B,A — по layout) в канонические r,g,b,a.
inline void UnpackPixel(const unsigned char* p, EBackendPixelLayout layout,
                        unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a) {
  if (layout == plRGBA) { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
  else { b = p[0]; g = p[1]; r = p[2]; a = p[3]; }
}

//////////////////////////////////////////////////////////////////////////
// PNG: IHDR + один IDAT + IEND, без интерлейса, фильтр 0 на каждую строку.
// Сжатие — zlib (compress2), CRC32 — тоже из zlib.

void AppendPngChunk(std::vector<unsigned char>& out, const char* pType,
                    const unsigned char* pData, size_t size) {
  unsigned char len[4];
  PutU32BE(len, (unsigned)size);
  out.insert(out.end(), len, len + 4);
  // CRC чанка покрывает ТОЛЬКО тип и данные (поле длины в него не входит) —
  // начало считаем после вставки длины, иначе декодер бракует каждый чанк
  // (поймано tools/screenshot_test: «битый CRC у чанка IHDR»).
  const size_t crcStart = out.size();
  out.insert(out.end(), pType, pType + 4);
  if (size != 0)
    out.insert(out.end(), pData, pData + size);
  const unsigned long crc = crc32(crc32(0L, Z_NULL, 0), out.data() + crcStart, (uInt)(size + 4));
  unsigned char crcBuf[4];
  PutU32BE(crcBuf, (unsigned)crc);
  out.insert(out.end(), crcBuf, crcBuf + 4);
}

bool WritePng(FILE* f, const unsigned char* pPixels, unsigned width, unsigned height,
              unsigned rowPitch, EBackendPixelLayout layout, bool keepAlpha) {
  const size_t stride = (size_t)width * (keepAlpha ? 4 : 3);
  std::vector<unsigned char> raw;
  raw.resize((size_t)height * (stride + 1));
  unsigned char* pDst = &raw[0];
  for (unsigned y = 0; y < height; ++y) {
    *pDst++ = 0;   // фильтр None
    const unsigned char* pRow = pPixels + (size_t)y * rowPitch;
    for (unsigned x = 0; x < width; ++x) {
      unsigned char r, g, b, a;
      UnpackPixel(pRow + (size_t)x * 4, layout, r, g, b, a);
      if (!keepAlpha) a = 0xff;
      *pDst++ = r; *pDst++ = g; *pDst++ = b;
      if (keepAlpha) *pDst++ = a;
    }
  }

  uLongf compSz = compressBound((uLong)raw.size());
  std::vector<unsigned char> comp(compSz);
  if (compress2(&comp[0], &compSz, &raw[0], (uLong)raw.size(), 6) != Z_OK)
    return false;

  std::vector<unsigned char> png;
  png.reserve(compSz + 64);
  static const unsigned char kSig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  png.insert(png.end(), kSig, kSig + 8);

  unsigned char ihdr[13];
  PutU32BE(ihdr + 0, width);
  PutU32BE(ihdr + 4, height);
  ihdr[8]  = 8;                                        // бит на канал
  ihdr[9]  = (unsigned char)(keepAlpha ? 6 : 2);       // 6 = RGBA, 2 = RGB
  ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;            // deflate, без фильтра, без интерлейса
  AppendPngChunk(png, "IHDR", ihdr, sizeof(ihdr));
  AppendPngChunk(png, "IDAT", &comp[0], (size_t)compSz);
  static const unsigned char kEmpty[1] = { 0 };
  AppendPngChunk(png, "IEND", kEmpty, 0);

  return fwrite(&png[0], 1, png.size(), f) == png.size();
}

//////////////////////////////////////////////////////////////////////////
// BMP: BITMAPFILEHEADER + BITMAPINFOHEADER, 32 bpp BI_RGB, строки снизу вверх
// (отрицательная высота в DIB запрещена спецификацией, поэтому строки
// разворачиваются вручную). Альфа в 32-битном BI_RGB не используется -> 0xff.

bool WriteBmp(FILE* f, const unsigned char* pPixels, unsigned width, unsigned height,
              unsigned rowPitch, EBackendPixelLayout layout) {
  const unsigned kHeaders = 14 + 40;
  const unsigned imgSize = width * height * 4;
  unsigned char hdr[kHeaders];
  memset(hdr, 0, sizeof(hdr));

  hdr[0] = 'B'; hdr[1] = 'M';
  PutU32LE(hdr + 2, kHeaders + imgSize);
  PutU32LE(hdr + 10, kHeaders);

  unsigned char* dib = hdr + 14;
  PutU32LE(dib + 0, 40);
  PutU32LE(dib + 4, width);
  PutU32LE(dib + 8, height);
  dib[12] = 1;  dib[13] = 0;          // planes
  dib[14] = 32; dib[15] = 0;          // бит на пиксель
  PutU32LE(dib + 16, 0);              // BI_RGB
  PutU32LE(dib + 20, imgSize);
  PutU32LE(dib + 24, 2835);           // 72 dpi
  PutU32LE(dib + 28, 2835);

  if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
    return false;

  std::vector<unsigned char> row((size_t)width * 4);
  for (unsigned y = height; y-- > 0;) {
    const unsigned char* pSrc = pPixels + (size_t)y * rowPitch;
    unsigned char* pOut = &row[0];
    for (unsigned x = 0; x < width; ++x) {
      unsigned char r, g, b, a;
      UnpackPixel(pSrc + (size_t)x * 4, layout, r, g, b, a);
      *pOut++ = b; *pOut++ = g; *pOut++ = r; *pOut++ = 0xff;
    }
    if (fwrite(&row[0], 1, row.size(), f) != row.size())
      return false;
  }
  return true;
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// DDS: заголовок 128 байт (магия "DDS " + DDSURFACEDESC2 без DX10-блока) +
// пиксели A8R8G8B8 сверху вниз. Тот же размер заголовка, что давал D3DXIFF_DDS,
// поэтому смещение 128 у снимка в память сохраняется.

unsigned char* BuildDdsImage(const unsigned char* pPixels, unsigned width, unsigned height,
                             unsigned rowPitch, EBackendPixelLayout layout,
                             unsigned* pOutSize) {
  if (pOutSize) *pOutSize = 0;
  if (!pPixels || !width || !height)
    return 0;

  const unsigned kHeader = 128;
  const unsigned pitch = width * 4;
  const unsigned size = kHeader + pitch * height;

  unsigned char* pOut = new unsigned char[size];
  memset(pOut, 0, kHeader);

  // Раскладка .dds: 4 байта магии "DDS ", следом DDSURFACEDESC2 (124 байта),
  // итого 128. Поэтому ВСЕ поля структуры сдвинуты на +4 относительно её
  // начала в заголовке (ошибиться на 4 — файл читается, но с другими
  // размерами/масками; поймано tools/screenshot_test).
  pOut[0] = 'D'; pOut[1] = 'D'; pOut[2] = 'S'; pOut[3] = ' ';
  PutU32LE(pOut + 4, 124);              // DDSURFACEDESC2.dwSize
  // DDSD_CAPS(0x1)|HEIGHT(0x2)|WIDTH(0x4)|PITCH(0x8)|PIXELFORMAT(0x1000)|MIPMAPCOUNT(0x20000)
  PutU32LE(pOut + 8, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000);
  PutU32LE(pOut + 12, height);
  PutU32LE(pOut + 16, width);
  PutU32LE(pOut + 20, pitch);
  PutU32LE(pOut + 28, 1);              // dwMipMapCount
  PutU32LE(pOut + 76, 32);             // ddspf.dwSize
  PutU32LE(pOut + 80, 0x40 | 0x40000); // DDPF_RGB | DDPF_ALPHAPIXELS
  // ddspf.dwFourCC = 0 (RGB-формат), dwRGBBitCount = 32
  PutU32LE(pOut + 88, 32);
  PutU32LE(pOut + 92,  0x00ff0000);    // R
  PutU32LE(pOut + 96,  0x0000ff00);    // G
  PutU32LE(pOut + 100, 0x000000ff);    // B
  PutU32LE(pOut + 104, 0xff000000);    // A
  PutU32LE(pOut + 108, 0x1000);        // DDSCAPS_TEXTURE

  unsigned char* pDst = pOut + kHeader;
  for (unsigned y = 0; y < height; ++y) {
    const unsigned char* pSrc = pPixels + (size_t)y * rowPitch;
    for (unsigned x = 0; x < width; ++x) {
      unsigned char r, g, b, a;
      UnpackPixel(pSrc + (size_t)x * 4, layout, r, g, b, a);
      // память A8R8G8B8 — байты B,G,R,A
      *pDst++ = b; *pDst++ = g; *pDst++ = r; *pDst++ = a;
    }
  }

  if (pOutSize) *pOutSize = size;
  return pOut;
}

EScreenshotFormat ScreenshotFormatFromName(const char* fileName) {
  if (!fileName)
    return sfPng;
  const char* pDot = strrchr(fileName, '.');
  if (!pDot)
    return sfPng;
  char ext[8] = { 0 };
  unsigned n = 0;
  for (const char* p = pDot + 1; *p && n < sizeof(ext) - 1; ++p, ++n)
    ext[n] = (char)tolower(*p);
  if (strcmp(ext, "bmp") == 0) return sfBmp;
  if (strcmp(ext, "dds") == 0) return sfDds;
  // png, а также jpg/jpeg/hdr/всё остальное: jpg и hdr умел только D3DX,
  // lossy-формат для сверки кадров не годится — пишем png.
  return sfPng;
}

bool WriteScreenshotFile(const char* fileName, EScreenshotFormat fmt,
                         const unsigned char* pPixels, unsigned width, unsigned height,
                         unsigned rowPitch, EBackendPixelLayout layout,
                         bool keepAlpha) {
  if (!fileName || !pPixels || !width || !height)
    return false;

  FILE* f = fopen(fileName, "wb");
  if (!f)
    return false;

  bool ok = false;
  switch (fmt) {
  case sfBmp: ok = WriteBmp(f, pPixels, width, height, rowPitch, layout); break;
  case sfDds: {
    unsigned char* pDds = BuildDdsImage(pPixels, width, height, rowPitch, layout, 0);
    if (pDds) {
      const unsigned size = 128 + width * height * 4;
      ok = fwrite(pDds, 1, size, f) == size;
      delete[] pDds;
    }
    break;
  }
  case sfPng:
  default:
    ok = WritePng(f, pPixels, width, height, rowPitch, layout, keepAlpha);
    break;
  }

  fclose(f);
  return ok;
}

} // namespace Render
