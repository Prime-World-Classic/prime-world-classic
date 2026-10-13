#pragma once

// Замер потребляемого множества ключей пайплайнов: сколько реально
// применяется материальных состояний (sortId = CRC материала: техника,
// пины, сэмплеры, рендер-состояния). Вместе с парой (VS,PS) это и есть полный
// ключ IGraphicsPipeline на этапе 3. Переключатель — USE_RENDER_SHADER_STATS
// (см. Render/stdafx.h).

#ifdef USE_RENDER_SHADER_STATS
  void NoteSortId( unsigned int sortId );
#else
  __forceinline void NoteSortId( unsigned int ) {}
#endif
