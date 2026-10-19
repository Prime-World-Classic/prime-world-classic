#include "stdafx.h"

// Next function is needed only to check D3DXVECTOR3 and CVec3 sizes at compile time
#pragma warning(disable : 4505)
#include "Vendor/DirectX/Include/d3dx9math.h"  // D3DXVECTOR3: needed only for the sizeof() check below
static void CheckCVec3Size() { NI_STATIC_ASSERT(sizeof(D3DXVECTOR3) == sizeof(CVec3), wrong_CVec3_size) }
