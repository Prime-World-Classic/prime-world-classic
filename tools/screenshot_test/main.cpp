// main.cpp — смоук писателей снимка (Render/screenshot.{h,cpp}).
//
// Пишет несколько файлов с известным узором (нечётные W/H — ловят ошибки
// шага строки и выравнивания), дальше check.py декодирует png/bmp/dds и
// сверяет пиксели. Собирается нативно (g++/clang + zlib): после выноса
// EBackendPixelLayout в renderpixellayout.h модуль не зависит ни от D3D9,
// ни от Windows, ни от stdafx клиента.
//
//   ./build_and_run.sh

#include "screenshot.h"

#include <stdio.h>
#include <vector>

namespace {

const unsigned kW = 37, kH = 13;

// Узор: B,G,R,A зависят от x,y по-разному, так что перестановка каналов,
// переворот строк и ошибка stride дают разные расхождения.
void MakePattern(std::vector<unsigned char>& px, bool bgraOrder) {
  px.assign((size_t)kW * kH * 4, 0);
  for (unsigned y = 0; y < kH; ++y)
    for (unsigned x = 0; x < kW; ++x) {
      unsigned char* p = &px[((size_t)y * kW + x) * 4];
      const unsigned char c0 = (unsigned char)((x * 7 + y) & 0xff);
      const unsigned char c1 = (unsigned char)((y * 19 + 3) & 0xff);
      const unsigned char c2 = (unsigned char)((x + y * 5 + 11) & 0xff);
      const unsigned char c3 = (unsigned char)((x * 5 + y * 2) & 0xff);
      if (bgraOrder) { p[0] = c0; p[1] = c1; p[2] = c2; p[3] = c3; }   // B,G,R,A
      else           { p[0] = c2; p[1] = c1; p[2] = c0; p[3] = c3; }   // R,G,B,A
    }
}

} // namespace

int main() {
  using namespace Render;

  std::vector<unsigned char> bgra, rgba;
  MakePattern(bgra, true);
  MakePattern(rgba, false);

  struct Case {
    const char* file;
    EScreenshotFormat fmt;
    bool keepAlpha;
    const std::vector<unsigned char>* px;
    EBackendPixelLayout layout;
  };
  const Case cases[] = {
    { "out.png",      sfPng, true,  &bgra, plBGRA },
    { "out_rgb.png",  sfPng, false, &bgra, plBGRA },
    { "out_rgba.png", sfPng, true,  &rgba, plRGBA },
    { "out.bmp",      sfBmp, true,  &bgra, plBGRA },
    { "out.dds",      sfDds, true,  &bgra, plBGRA },
    { "out_rgba.dds", sfDds, true,  &rgba, plRGBA },
  };

  int failed = 0;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    const Case& c = cases[i];
    const bool ok = WriteScreenshotFile(c.file, c.fmt, &(*c.px)[0], kW, kH, kW * 4, c.layout, c.keepAlpha);
    printf("%-14s %s\n", c.file, ok ? "written" : "FAILED");
    if (!ok) ++failed;
  }

  // DDS в память (путь DumpScreenshot'а в редакторе был таким же)
  unsigned size = 0;
  unsigned char* pDds = BuildDdsImage(&bgra[0], kW, kH, kW * 4, plBGRA, &size);
  printf("dds memory: %u байт (ожидалось %u)\n", size, 128 + kW * kH * 4);
  if (!pDds || size != 128 + kW * kH * 4) ++failed;
  else {
    FILE* f = fopen("out_mem.dds", "wb");
    if (f) { fwrite(pDds, 1, size, f); fclose(f); }
    delete[] pDds;
  }

  // Формат по расширению: jpg/hdr/неизвестное -> png
  printf("fmt jpg->%d hdr->%d png->%d bmp->%d dds->%d без-расш->%d\n",
         (int)ScreenshotFormatFromName("a.jpg"), (int)ScreenshotFormatFromName("a.HDR"),
         (int)ScreenshotFormatFromName("a.png"), (int)ScreenshotFormatFromName("a.bmp"),
         (int)ScreenshotFormatFromName("a.dds"), (int)ScreenshotFormatFromName("noext"));

  printf("итог: %s\n", failed == 0 ? "OK" : "FAIL");
  return failed == 0 ? 0 : 1;
}
