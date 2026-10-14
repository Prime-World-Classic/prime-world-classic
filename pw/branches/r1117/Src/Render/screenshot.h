#pragma once
//////////////////////////////////////////////////////////////////////////
// Запись снимка кадра (этап 2 плана NVRHI, docs/PLAN_NVRHI.md).
//
// Раньше это делал D3DX (D3DXSaveSurfaceToFile / ...ToFileInMemory /
// D3DXLoadSurfaceFromSurface для снятия альфы). Своё нужно по двум причинам:
//   * D3DX — импорт d3dx9_43.dll, а этап 4 убирает зависимость целиком;
//   * у D3DX не было способа прочитать пиксели без промежуточной D3D9-
//     поверхности, т.е. без прямого устройства (этап 2 как раз уводит
//     поверхность за IRenderBackend).
//
// Форматы: png, bmp, dds. jpg и hdr умел только D3DX; в клиенте jpg был лишь
// расширением по умолчанию (cvar screenshotext), для визуальной сверки кадров
// lossy-формат не годится — расширение по умолчанию переведено на png, а
// jpg/hdr в запросе молча сворачиваются в png (см. ScreenshotFormatFromName).
//
// Порядок байтов в буфере пикселей задаётся EBackendPixelLayout: у D3D9
// поверхность A8R8G8B8 лежит в памяти как B,G,R,A, у Vulkan-бэкенда этапа 3
// естественный ответ — R,G,B,A. Пишущий код перестановку делает сам.

#include "renderpixellayout.h"   // Render::EBackendPixelLayout

namespace Render {

enum EScreenshotFormat {
  sfPng = 0,
  sfBmp,
  sfDds,
};

// Формат по расширению файла. Неизвестное расширение (в т.ч. jpg/jpeg/hdr) —
// png. Имя файла при этом НЕ меняется: shot.jpg останется shot.jpg, но внутри
// будет png (переписывать имя вызывающему коду — имя приходит из cvar
// screenshotext и из имени, которое передаёт зовущий).
EScreenshotFormat ScreenshotFormatFromName(const char* fileName);

// Пишет кадр в файл. pPixels — строки сверху вниз, rowPitch байт в строке.
// keepAlpha=false — альфа отбрасывается (для png это RGB-цветовой тип, для
// bmp/dds — A=0xff).
bool WriteScreenshotFile(const char* fileName, EScreenshotFormat fmt,
                         const unsigned char* pPixels, unsigned width, unsigned height,
                         unsigned rowPitch, EBackendPixelLayout layout,
                         bool keepAlpha);

// Собирает DDS в память (128-байтный заголовок + пиксели A8R8G8B8). Размер —
// в *pOutSize. Память выделяется new[], освобождается delete[].
unsigned char* BuildDdsImage(const unsigned char* pPixels, unsigned width, unsigned height,
                             unsigned rowPitch, EBackendPixelLayout layout,
                             unsigned* pOutSize);

} // namespace Render
