#include "stdafx.h"
#ifdef NV_LINUX_PLATFORM
#include <string>
#include <vector>



extern "C" {
  int FilterAsync(const wchar_t*) { return -1; }
  bool GetFilteredAsync(int, wchar_t*, int) { return false; }

  void* D3DXVec3TransformCoord(void* pOut, const void* pV, const void* pM) { return pOut; }
  void* D3DXVec3TransformNormal(void* pOut, const void* pV, const void* pM) { return pOut; }
  void* D3DXVec3TransformCoordArray(void* pOut, unsigned int OutStride, const void* pV, unsigned int VStride, const void* pM, unsigned int n) { return pOut; }
  void* D3DXVec2TransformNormalArray(void* pOut, unsigned int OutStride, const void* pV, unsigned int VStride, const void* pM, unsigned int n) { return pOut; }
  void* D3DXVec3Normalize(void* pOut, const void* pV) { return pOut; }
  void* D3DXMatrixTranspose(void* pOut, const void* pM) { return pOut; }
  void* D3DXMatrixInverse(void* pOut, float* pDeterminant, const void* pM) { return pOut; }
  void* D3DXMatrixMultiply(void* pOut, const void* pM1, const void* pM2) { return pOut; }
  void* D3DXMatrixTranslation(void* pOut, float x, float y, float z) { return pOut; }
  void* D3DXMatrixPerspectiveLH(void* pOut, float w, float h, float zn, float zf) { return pOut; }
  void* D3DXMatrixOrthoLH(void* pOut, float w, float h, float zn, float zf) { return pOut; }
  void* D3DXMatrixLookAtLH(void* pOut, const void* pEye, const void* pAt, const void* pUp) { return pOut; }
  void* D3DXMatrixOrthoOffCenterLH(void* pOut, float l, float r, float b, float t, float zn, float zf) { return pOut; }
  long D3DXSHProjectCubeMap(unsigned int Order, void* pCubeMap, float* pROut, float* pGOut, float* pBOut) { return 0; }
  long D3DXSHEvalDirectionalLight(unsigned int Order, const void* pDir, float RIntensity, float GIntensity, float BIntensity, float* pROut, float* pGOut, float* pBOut) { return 0; }
  float* D3DXSHEvalDirection(float* pOut, unsigned int Order, const void* pDir) { return 0; }
  const char* DXGetErrorDescriptionA(long hr) { return ""; }
  const char* DXGetErrorStringA(long hr) { return ""; }
  long D3DXCreateTextureFromFileInMemoryEx(void* pDevice, const void* pSrcData, unsigned int SrcDataSize, unsigned int Width, unsigned int Height, unsigned int MipLevels, unsigned long Usage, int Format, int Pool, unsigned long Filter, unsigned long MipFilter, unsigned long ColorKey, void* pSrcInfo, void* pPalette, void** ppTexture) { return 0; }
  long D3DXCreateCubeTextureFromFileInMemoryEx(void* pDevice, const void* pSrcData, unsigned int SrcDataSize, unsigned int Size, unsigned int MipLevels, unsigned long Usage, int Format, int Pool, unsigned long Filter, unsigned long MipFilter, unsigned long ColorKey, void* pSrcInfo, void* pPalette, void** ppCubeTexture) { return 0; }
  void D3DPERF_SetMarker(unsigned long col, const wchar_t* wszName) {}
  void* Direct3DCreate9(unsigned int SDKVersion) { return 0; }
  long D3DXCompileShader(const char* pSrcData, unsigned int SrcDataLen, const void* pDefines, void* pInclude, const char* pFunctionName, const char* pProfile, unsigned long Flags, void** ppShader, void** ppErrorMsgs, void** ppConstantTable) { return 0; }
  long D3DXSaveSurfaceToFileA(const char* pDestFile, int DestFormat, void* pSrcSurface, const void* pSrcPalette, const void* pSrcRect) { return 0; }
  long D3DXSaveSurfaceToFileInMemory(void** ppDestBuf, int DestFormat, void* pSrcSurface, const void* pSrcPalette, const void* pSrcRect) { return 0; }
  long D3DXLoadSurfaceFromSurface(void* pDestSurface, const void* pDestPalette, const void* pDestRect, void* pSrcSurface, const void* pSrcPalette, const void* pSrcRect, unsigned long Filter, unsigned long ColorKey) { return 0; }
}

namespace NFile {
  void EraseFile(const nstl::string&) {}
  void ExecuteFile(const nstl::string&) {}
  nstl::string GetTempFileName() { return ""; }
}

namespace PF_Minigames {
  class EaselEventSounds { public: EaselEventSounds(); };
  EaselEventSounds::EaselEventSounds() {}
  class EaselEventNullSounds { public: EaselEventNullSounds(); };
  EaselEventNullSounds::EaselEventNullSounds() {}
}

namespace NWorld {
  namespace PFResourcesCollection { void CollectConsumables() {} }
}

namespace NSoundScene {
  struct SoundDescription { SoundDescription(); };
  SoundDescription::SoundDescription() {}
  void PlaySound(const SoundDescription&, float*) {}
  void DeleteSound(int) {}
  class ISimpleSound {};
}

template<> CObjectBase* CastToObjectBaseImpl<NSoundScene::ISimpleSound>(NSoundScene::ISimpleSound*, void*) { return 0; }

template<unsigned int N, typename T> class FixedString;

namespace Transport {
  struct Address {};
  nstl::string AddressToString(const Address&) { return ""; }
  int GetServerIndex(const FixedString<64u, char>&) { return 0; }
  int GetServiceClass(const FixedString<64u, char>&, FixedString<64u, char>&) { return 0; }
  namespace Defaults {
    int GetOpenChannelTimeout() { return 0; }
    int GetPingPeriod() { return 0; }
  }
}

namespace nvl {
  namespace bds { class CBinaryBlock; class CTextBlock; class CDataFlowProxy; class CTerminatorBase { public: class CRegisterDataFlowEvent; }; }
  template<typename T> class CPoolableMTEntity { public: static void* m_Pool; };
  template<> void* CPoolableMTEntity<bds::CBinaryBlock>::m_Pool = 0;
  template<> void* CPoolableMTEntity<bds::CTextBlock>::m_Pool = 0;
  template<> void* CPoolableMTEntity<bds::CTerminatorBase::CRegisterDataFlowEvent>::m_Pool = 0;
  template<> void* CPoolableMTEntity<bds::CDataFlowProxy>::m_Pool = 0;
}

struct SCallStackEntry {};
void CollectCallStack(nstl::vector<SCallStackEntry>*) {}

namespace NBSU {
  void WriteAssertLogFile(const tm&, const char*, const nstl::vector<SCallStackEntry>&, bool) {}
  void ShowAssertionDlg(const char*, int, const char*, const nstl::vector<SCallStackEntry>&, bool) {}
}

#endif
