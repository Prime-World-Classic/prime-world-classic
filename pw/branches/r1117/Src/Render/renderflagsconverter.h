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

/// ��������������� ���� ���������� ������� � d3d9
D3DPRIMITIVETYPE ConvertPrimitiveType(ERenderPrimitiveType type);
/// ��������������� ���� �������� ������� � ��� ������
UINT GetVertexElementSize(EVertexElementType type);
/// ��������������� ���� ������������� �������� ������� � d3d9
BYTE ConvertVertexElementUsage(EVertexElementUsage usage);
/// ��������������� ���� �������� ������� � d3d9
BYTE ConvertVertexElementType(EVertexElementType type);

/// ��������������� ���� ������������� �������� ������� �� d3d9
EVertexElementUsage Convert2VertexElementUsage(BYTE usage);
/// ��������������� ���� �������� ������� �� d3d9
EVertexElementType Convert2VertexElementType(BYTE type);

DWORD ConvertRenderLockType(ERenderLockType type);

/// 
D3DFORMAT ConvertRenderFormat(ERenderFormat format);

/// get format name
const char* D3DFormat2Str(DWORD _format);

/// get pool name
const char* D3DPool2Str(D3DPOOL _pool);

// D3D9-словари -> нейтральные словари seam'а (renderbackend.h). Обратные
// переводы (EBackend* -> D3D*) живут в d3d9backend.cpp — там, где D3D9-реализация.
// bfUnknown / BACKEND_POOL_COUNT наружу не отдаются: вызывающий обязан
// отказать в создании ресурса, а не подменить формат молча.
EBackendFormat RenderFormatToBackend(ERenderFormat format);
EBackendFormat D3DFormatToBackend(D3DFORMAT fmt);
// Пул и usage переводятся парой: в D3D9 «динамичность» — это флаг usage при
// D3DPOOL_DEFAULT, в seam'е — отдельное значение EBackendPool.
EBackendPool   D3DPoolToBackend(D3DPOOL pool, DWORD usage);
// Только «назначение» (RENDERTARGET/DEPTHSTENCIL/AUTOGENMIPMAP); биты памяти
// (DYNAMIC/WRITEONLY) отсекаются — их выражает пул.
EBackendUsage  D3DUsageToBackend(DWORD usage);

// Обратные переводы (шаг 13.2): клиент восстанавливает D3DSURFACE_DESC из
// IRenderBackend::BackendTextureInfo, который вернул бэкенд. Возвращают false,
// если значение вне словаря — вызывающий обязан залогировать отказ, а не
// подменять формат/пул молча (молча подменённый пул = текстура не переживёт
// потерю устройства, и это проявляется только после alt-tab).
bool BackendFormatToD3D(EBackendFormat fmt, D3DFORMAT* pOut);
// Пул и usage восстанавливаются парой — см. D3DPoolToBackend (динамичность в
// D3D9 живёт в usage при D3DPOOL_DEFAULT).
bool BackendPoolUsageToD3D(EBackendPool pool, EBackendUsage usage,
                           D3DPOOL* pPool, DWORD* pUsage);
// Только биты «назначения» (без пуловых DYNAMIC/WRITEONLY).
DWORD D3DUsageFromBackendBits(EBackendUsage usage);

}; // namespace Render