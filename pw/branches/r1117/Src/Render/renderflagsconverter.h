#pragma once

#include "Vendor/DirectX/Include/d3d9types.h"
#include "renderprimitivetype.h"
#include "vertexelementtype.h"
#include "vertexelementusage.h"
#include "renderstates.h"
#include "renderformat.h"
#include "renderbackend.h"   // EBackendFormat/EBackendPool/EBackendUsage
#include "sampler.h"

namespace Render
{

/// Конвертирование типа примитивов рендера в d3d9
D3DPRIMITIVETYPE ConvertPrimitiveType(ERenderPrimitiveType type);
/// Конвертирование типа элемента вершины в его размер
UINT GetVertexElementSize(EVertexElementType type);
/// Конвертирование типа использования элемента вершины в d3d9
BYTE ConvertVertexElementUsage(EVertexElementUsage usage);
/// Конвертирование типа элемента вершины в d3d9
BYTE ConvertVertexElementType(EVertexElementType type);

/// Конвертирование типа использования элемента вершины из d3d9
EVertexElementUsage Convert2VertexElementUsage(BYTE usage);
/// Конвертирование типа элемента вершины из d3d9
EVertexElementType Convert2VertexElementType(BYTE type);

DWORD ConvertRenderLockType(ERenderLockType type);

/// 
D3DFORMAT ConvertRenderFormat(ERenderFormat format);

/// get format name
const char* D3DFormat2Str(DWORD _format);

/// get pool name
const char* D3DPool2Str(D3DPOOL _pool);

// D3D9-СЃР»РѕРІР°СЂРё -> РЅРµР№С‚СЂР°Р»СЊРЅС‹Рµ СЃР»РѕРІР°СЂРё seam'Р° (renderbackend.h). РћР±СЂР°С‚РЅС‹Рµ
// РїРµСЂРµРІРѕРґС‹ (EBackend* -> D3D*) Р¶РёРІСѓС‚ РІ d3d9backend.cpp вЂ” С‚Р°Рј, РіРґРµ D3D9-СЂРµР°Р»РёР·Р°С†РёСЏ.
// bfUnknown / BACKEND_POOL_COUNT РЅР°СЂСѓР¶Сѓ РЅРµ РѕС‚РґР°СЋС‚СЃСЏ: РІС‹Р·С‹РІР°СЋС‰РёР№ РѕР±СЏР·Р°РЅ
// РѕС‚РєР°Р·Р°С‚СЊ РІ СЃРѕР·РґР°РЅРёРё СЂРµСЃСѓСЂСЃР°, Р° РЅРµ РїРѕРґРјРµРЅРёС‚СЊ С„РѕСЂРјР°С‚ РјРѕР»С‡Р°.
EBackendFormat RenderFormatToBackend(ERenderFormat format);
EBackendFormat D3DFormatToBackend(D3DFORMAT fmt);
// РџСѓР» Рё usage РїРµСЂРµРІРѕРґСЏС‚СЃСЏ РїР°СЂРѕР№: РІ D3D9 В«РґРёРЅР°РјРёС‡РЅРѕСЃС‚СЊВ» вЂ” СЌС‚Рѕ С„Р»Р°Рі usage РїСЂРё
// D3DPOOL_DEFAULT, РІ seam'Рµ вЂ” РѕС‚РґРµР»СЊРЅРѕРµ Р·РЅР°С‡РµРЅРёРµ EBackendPool.
EBackendPool   D3DPoolToBackend(D3DPOOL pool, DWORD usage);
// РўРѕР»СЊРєРѕ В«РЅР°Р·РЅР°С‡РµРЅРёРµВ» (RENDERTARGET/DEPTHSTENCIL/AUTOGENMIPMAP); Р±РёС‚С‹ РїР°РјСЏС‚Рё
// (DYNAMIC/WRITEONLY) РѕС‚СЃРµРєР°СЋС‚СЃСЏ вЂ” РёС… РІС‹СЂР°Р¶Р°РµС‚ РїСѓР».
EBackendUsage  D3DUsageToBackend(DWORD usage);

}; // namespace Render