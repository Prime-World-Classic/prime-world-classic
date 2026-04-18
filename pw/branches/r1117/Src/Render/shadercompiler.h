#pragma once

#if defined(PW_LINUX_NULL_RENDER)
#include "dxutils.h"
#else
#include "DxIntrusivePtr.h"
#endif
#include "shaderdefinestable.h"

namespace Render
{
	///
	DXPixelShaderRef CompilePixelShaderFromFile(const char* filename, const ShaderDefinesTable& definesTable = ShaderDefinesTable());
	///
	DXVertexShaderRef CompileVertexShaderFromFile(const char* filename, const ShaderDefinesTable& definesTable = ShaderDefinesTable());
}; // namespace Render
