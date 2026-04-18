#pragma once

#if defined(PW_LINUX_NULL_RENDER)
#include "dxutils.h"
#endif

namespace Render {
HRESULT CreateTextureFromDDSFileInMemory(const char *_data, UINT _dataSize, UINT _skipMipLevels, PDIRECT3DBASETEXTURE9* _ppTex);
} // namespace Render
