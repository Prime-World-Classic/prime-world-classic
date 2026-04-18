#pragma once

#if defined(PW_LINUX_NULL_RENDER)

inline const char* DXGetErrorStringA(HRESULT hr)
{
  (void)hr;
  return "D3D stub";
}

inline const char* DXGetErrorDescriptionA(HRESULT hr)
{
  (void)hr;
  return "Direct3D error descriptions are unavailable in the Linux null-render bootstrap";
}

#else

#include "../../Vendor/DirectX/Include/DxErr.h"

#endif
