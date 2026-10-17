#include "stdafx.h"
#include "renderflagsconverter.h"
#include "renderer.h"
#include "renderbackend.h"   // seam —ç—Ç–∞–ø–∞ 2: GetBackend()/HTexture/EBackend*
#include "TextureManager.h"
#include "DxResourcesControl.h"
#include "ReadDDS.h"

#include "smartrenderer.h"
#include "../System/nalgoritm.h"
#include "libdb/DbResourceCache.h"

static NDebug::DebugVar<unsigned int> render_TexturesCount( "TexturesCount", "Statistics" );
static NDebug::PerformanceDebugVar render_TexturesLoadTime( "LoadTimeTextures", "RenderOffline", 100, 100, false );

static bool g_disableTextureLoads = false;
REGISTER_DEV_VAR("no_texture_loads", g_disableTextureLoads, STORAGE_NONE);


namespace Render
{

namespace RenderResourceManager
{
  bool IsSecondaryThread();
}

DECLARE_NULL_RENDER_FLAG

// Internal functions
namespace
{

struct PoolTextureWrapper
{
public :
  PoolTextureWrapper(  ) : texPtr(NULL), sizeInFile(0) {}
  PoolTextureWrapper( TextureRef texRef_, SWin32Time modificationTime_, UINT skipMipLevels, bool canBeVisualDegrade, void * firstPoolId, int sizeInFile_ ) :
    texPtr( texRef_ ),
    modificationTime( modificationTime_ ),
    skipMipLevels( skipMipLevels ),
    canBeVisualDegrade( canBeVisualDegrade ),
    sizeInFile( sizeInFile_ )
  {
    usedInPools.reserve( 4 );
    usedInPools.push_back( firstPoolId );
  }

public :
  TextureRef texPtr;
  SWin32Time modificationTime;
  UINT skipMipLevels;
  bool canBeVisualDegrade;
  //ﬂ ËÒÔÓÎ¸ÁÛ˛ vector<> ÒÓÁÌ‡ÚÂÎ¸ÌÓ - ÓÔÂ‡ˆËË ‚ÒÚ‡‚ÍË/ÔÓËÒÍ‡/Û‰‡ÎÂÌËˇ ÔÓËÒıÓ‰ˇÚ Â‰ÍÓ, ‡ ‡ÁÏÂ ‚ÂÍÚÓ‡ ·Û‰ÂÚ ÌÂ·ÓÎ¸¯ËÏ
  vector<void *> usedInPools;
  int sizeInFile;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//============================================================================================================
class PoolMT
{
public:
  ~PoolMT()
  {
    Clear();
  }

protected:
  typedef nstl::hash_map<nstl::string, PoolTextureWrapper> TexturesPool;
  TexturesPool texturesPool;
  TexturesPool loadingPool;

  threading::Mutex loadMutex;

  bool FindMain(const nstl::string& _filename, PoolTextureWrapper* _result)
  {
    TexturesPool::iterator it = texturesPool.find(_filename);
    if(texturesPool.end() != it) {
      *_result = it->second;
      return true;
    }

    threading::MutexLock lock(loadMutex);

    it = loadingPool.find(_filename);
    if(loadingPool.end() == it)
      return false;
    // Unfortunately, we MUST copy it->second (i.e.PoolTextureWrapper) here, because loadingPool iterators are not stable
    *_result = it->second;
    return true;
  }

  bool FindSecondary(const nstl::string& _filename, PoolTextureWrapper* _result)
  {
    threading::MutexLock lock(loadMutex);

    TexturesPool::iterator it = texturesPool.find(_filename);
    if(texturesPool.end() != it) {
      *_result = it->second;
      return true;
    }
    it = loadingPool.find(_filename);
    if(loadingPool.end() == it)
      return false;

    *_result = it->second;
    return true;
  }

public:
  bool Find(const nstl::string& filename, PoolTextureWrapper* _result)
  {
    return RenderResourceManager::IsSecondaryThread() ? FindSecondary(filename, _result)
                                                      : FindMain(filename, _result);
  }

  void MarkAsLoaded(const PoolTextureWrapper& _wrapper, const nstl::string& _filename)
  {
    threading::MutexLock lock(loadMutex);
    loadingPool.insert( make_pair(_filename, _wrapper) );
  }

  void MoveMarkedInPool()
  {
    threading::MutexLock lock(loadMutex);
    texturesPool.insert( loadingPool.begin(), loadingPool.end() );
    loadingPool.clear();
  }

  void Clear()
  {
    threading::MutexLock lock(loadMutex);
    loadingPool.clear();
    texturesPool.clear();
  }

  void ForAllTextures(TextureProc &p);
  void ReloadTextures(bool bModifiedOnly);
  void UnloadTexturePool(void * poolId);
  bool Dump(const char *name, const vector<wstring>& params);
};


PoolMT s_texturesPool;
bool g_showMipMap = false;
static int g_skipMipLevels = 0;


REGISTER_DEV_VAR( "show_mipmap", g_showMipMap, STORAGE_NONE );
REGISTER_VAR_PRED( "gfx_skip_mip_levels", g_skipMipLevels, STORAGE_USER, NGlobal::MakeMinMaxVerifyPred(0, 5) );


HRESULT ProcessMipMaps(IDirect3DTexture9 **ppD3DTexture, UINT skipMipLevels, Stream *pStream)
{
  static   int mipMapColors[8][4] = {{0xFF, 0xFF, 0xFF, 0xFF} // ARGB
  , {0xFF, 0xFF, 0, 0}
  , {0xFF, 0, 0xFF, 0}
  , {0xFF, 0, 0, 0xFF}
  , {0xFF, 0xFF, 0xFF, 0}
  , {0xFF, 0xFF, 0, 0xFF}
  , {0xFF, 0, 0xFF, 0xFF}
  , {0x80, 0x80, 0x80, 0x80}
  };

  if(RENDER_DISABLED)
    return S_FALSE;

  // show_mipmap ‚Äî –æ—Ç–ª–∞–¥–æ—á–Ω–∞—è –ø–æ–¥—Å–≤–µ—Ç–∫–∞ —É—Ä–æ–≤–Ω–µ–π. –í–µ—Å—å –∫–æ–Ω—Ç–µ–Ω—Ç ‚Äî DDS
  // (–∑–∞–º–µ—Ä tools/dds_inventory: 8031 .dds –≤ Data/, –ø–æ—Å—Ç–æ—Ä–æ–Ω–Ω–∏—Ö —Ñ–æ—Ä–º–∞—Ç–æ–≤ –Ω–µ—Ç),
  // –ø–æ—ç—Ç–æ–º—É —Å–≤–æ–π –∑–∞–≥—Ä—É–∑—á–∏–∫, –∞ –Ω–µ D3DX. –§–æ—Ä–º–∞—Ç –±–µ—Ä—ë—Ç—Å—è –∏–∑ —Ñ–∞–π–ª–∞: –ø–æ–¥—Å–≤–µ—Ç–∫–∞ –∏ —Ç–∞–∫
  // —Ä–∞–±–æ—Ç–∞–µ—Ç —Ç–æ–ª—å–∫–æ –¥–ª—è A8R8G8B8 (–Ω–∏–∂–µ –ø—Ä–æ–≤–µ—Ä–∫–∞ levelDesc.Format).
  IDirect3DBaseTexture9* pBaseTexture = 0;
  HRESULT hResult = CreateTextureFromDDSFileInMemory( pStream->GetBuffer(), pStream->GetSize(), skipMipLevels, &pBaseTexture );
  IDirect3DTexture9* pD3DTexture = 0;
  // “ËÔ Ë ÓÔËÒ‡ÌËÂ ·Â∏Ï ËÁ ·˝ÍÂÌ‰‡: ÔÓ‰ NvrhiBackend ‚ pBaseTexture ÎÂÊËÚ
  // Û˜Í‡ ·˝ÍÂÌ‰‡, ‡ ÌÂ COM-Ó·˙ÂÍÚ, ÔÓ˝ÚÓÏÛ GetType()/GetLevelCount()/LockRect()
  // Ì‡ÔˇÏÛ˛ ÔÓ ÌÂÈ = ‚˚ÁÓ‚ ÔÓ ˜ÛÊÓÈ vtable (Á‡ÏÂ 2026-10-16 ó Á‡‚ËÒ‡ÌËÂ ÔÓÚÓÍ‡
  // ÂÌ‰Â‡ Ò‡ÁÛ ÔÓÒÎÂ DDS). Release() ÓÒÚ‡∏ÚÒˇ: Ó·∏ÚÍ‡ ·˝ÍÂÌ‰‡ ‰ÂÎ‡ÂÚ ÚÂ ÊÂ
  // AddRef/Release, ˜ÚÓ Ë COM-ÔÓÎËÚËÍ‡ ÛÏÌ˚ı ÛÍ‡Á‡ÚÂÎÂÈ ÍÎËÂÌÚ‡.
  IRenderBackend* const pBackend = GetBackend();
  IRenderBackend::BackendTextureInfo texInfo;
  const bool texIs2D = pBackend &&
                       SUCCEEDED(pBackend->GetTextureInfo(MakeBackendHandle<HTexture>(pBaseTexture), &texInfo)) &&
                       !texInfo.cube;
  if( SUCCEEDED( hResult ) && pBaseTexture && texIs2D )
    pD3DTexture = static_cast<IDirect3DTexture9*>( pBaseTexture );
  else if( pBaseTexture )
  {
    pBaseTexture->Release();
    hResult = E_INVALIDARG;
  }

  if (hResult == D3D_OK)
  {
    D3DLOCKED_RECT rect;
    for (DWORD level = 0, maxlevel = texInfo.levels; level < maxlevel; level++)
    {
      int const* pColors = mipMapColors[Min(level, (DWORD)(ARRAY_SIZE(mipMapColors) - 1))];
      void* lockBits = 0;
      unsigned lockPitch = 0;
      if (GetBackend()->LockTexture2D(MakeBackendHandle<HTexture>(pD3DTexture), level, 0u, 0,
                                      LOCK_DEFAULT, &lockBits, &lockPitch) == D3D_OK)
      {
        rect.pBits = lockBits;
        rect.Pitch = (LONG)lockPitch;
        // ŒÔËÒ‡ÌËÂ ÛÓ‚Ìˇ ÒÚÓËÏ ËÁ texInfo: COM-ÓÔËÒ‡ÌËˇ Û Û˜ÍË ·˝ÍÂÌ‰‡ ÌÂÚ.
        // –‡ÁÏÂ˚ ÛÓ‚Ìˇ ó ÔÓÎÓ‚ËÌÍË ÔÂ‰˚‰Û˘Â„Ó (DDS-ˆÂÔÓ˜Í‡), ÙÓÏ‡Ú Ó·˘ËÈ.
        D3DSURFACE_DESC levelDesc;
        ZeroMemory(&levelDesc, sizeof(levelDesc));
        levelDesc.Width  = Max(texInfo.width  >> level, 1u);
        levelDesc.Height = Max(texInfo.height >> level, 1u);
        levelDesc.Format = (texInfo.format == bfA8R8G8B8) ? D3DFMT_A8R8G8B8 : D3DFMT_UNKNOWN;
        {
          if (levelDesc.Format == D3DFMT_A8R8G8B8)
          {
            for (unsigned int y = 0; y < levelDesc.Height; y++)
            {
              uint* pixel = (uint *)((byte*)rect.pBits + y * rect.Pitch);
              for (unsigned int x = 0; x < levelDesc.Width; x++)
              {
                byte b = *pixel & 0xFF, g = (*pixel >> 8) & 0xFF, r = (*pixel >> 16) & 0xFF, a = (*pixel >> 24) & 0xFF;
                *pixel = ((b * pColors[3]) >> 8) | (((g * pColors[2]) >> 8) << 8) | (((r * pColors[1]) >> 8) << 16) | (((a * pColors[0]) >> 8) << 24);
                *pixel++;
              }
            }
          }
        }
        GetBackend()->UnlockTexture2D(MakeBackendHandle<HTexture>(pD3DTexture), level, 0u);
      }
    }
  }

  *ppD3DTexture = pD3DTexture;
  return hResult;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static HRESULT CreateTextureFromFileInMemoryImpl(IDirect3DTexture9** _pResult, const char * _pBuffer, UINT _dataSize,
                                               UINT _skipMipLevels = 0, const char* _fileName = 0)
{
  IDirect3DBaseTexture9* pBaseTex = 0;
  const HRESULT hr = CreateTextureFromDDSFileInMemory(_pBuffer, _dataSize, _skipMipLevels, &pBaseTex);

  if( SUCCEEDED(hr) ) {
    // “ËÔ ó ˜ÂÂÁ seam: ÔÓ‰ NvrhiBackend ‚ pBaseTex Û˜Í‡ ·˝ÍÂÌ‰‡, GetType() ÔÓ
    // ÌÂÈ = ‚˚ÁÓ‚ ÔÓ ˜ÛÊÓÈ vtable (Á‡‚ËÒ‡ÌËÂ, Á‡ÏÂ 2026-10-16).
    IRenderBackend::BackendTextureInfo info2D;
    const bool ok2D = GetBackend() &&
                      SUCCEEDED(GetBackend()->GetTextureInfo(MakeBackendHandle<HTexture>(pBaseTex), &info2D)) &&
                      !info2D.cube;
    NI_VERIFY(ok2D,
              NStr::StrFmt("Inconsistent texture %s: 2D texture requested, yet it isn't", _fileName),
              pBaseTex->Release(); return E_INVALIDARG);
    *_pResult = static_cast<IDirect3DTexture9*>(pBaseTex);
    return hr;
  }
  // –§–æ–ª–±—ç–∫–∞ –Ω–∞ D3DX –±–æ–ª—å—à–µ –Ω–µ—Ç. –ó–∞–º–µ—Ä –∫–æ–Ω—Ç–µ–Ω—Ç–∞ (tools/dds_inventory):
  // 8031 .dds –≤ Data/ ‚Äî 3715 A8R8G8B8, 3001 DXT5, 1294 DXT1, 5 L8, 16 –∫—É–±–æ–≤;
  // –≤ –º–∞–Ω–∏—Ñ–µ—Å—Ç–µ –æ–±–Ω–æ–≤–ª–µ–Ω–∏–π (files.list) —Ç–æ–∂–µ —Ç–æ–ª—å–∫–æ .dds, 4318 —à—Ç—É–∫. –°–≤–æ–π
  // –∑–∞–≥—Ä—É–∑—á–∏–∫ (Render/ReadDDS.cpp) –ø–æ–∫—Ä—ã–≤–∞–µ—Ç –≤—Å—ë —ç—Ç–æ, –≤–∫–ª—é—á–∞—è –∫—É–±—ã –∏ –ø—Ä–æ–ø—É—Å–∫
  // mipmap-—É—Ä–æ–≤–Ω–µ–π. –ü–æ–ø–∞–ª—Å—è –Ω–µ-DDS ‚Äî –∑–Ω–∞—á–∏—Ç –æ—à–∏–±–∫–∞ –∫–æ–Ω—Ç–µ–Ω—Ç–∞, –∞ –Ω–µ –ø–æ–≤–æ–¥
  // –≤–æ–∑–≤—Ä–∞—â–∞—Ç—å d3dx9_43.dll.
  NI_VERIFY( false, NStr::StrFmt( "Texture %s is not a loadable DDS (hr=0x%08X)",
                               _fileName ? _fileName : "<memory>", hr ), return hr );
  return hr;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool LoadTextureFromFileImpl( const char* fileName, UINT skipMipLevels, Texture2DRef &pTexture, int& sizeInFile )
{
  if( g_disableTextureLoads )
    if( pTexture = GetDefaultTextures().pTexture )
      return true;

  NI_ASSERT( !NDb::GetDbResourceCache().IsAssertionLoadingFiles(), 
    NStr::StrFmt( "Not recommended loading resource \"%s\" after game starting", fileName ) 
  );

	CObj<Stream> pStream = RootFileSystem::OpenFile( fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
	NI_VERIFY( pStream && pStream->IsOk(), NStr::StrFmt( "Cannot open file to load texture from: \"%s\"", fileName ), return false; );

  sizeInFile = pStream->GetSize();

	IDirect3DTexture9* pD3DTexture = 0;
  HRESULT hResult = E_FAIL;
  if(!g_showMipMap) 
    hResult = CreateTextureFromFileInMemoryImpl(&pD3DTexture, pStream->GetBuffer(), sizeInFile, skipMipLevels, fileName);
  else
    hResult = ProcessMipMaps(&pD3DTexture, skipMipLevels, pStream);

  pStream = 0;

  NI_DX_WARN(hResult, "LoadTextureFromFileImpl()");
	NI_VERIFY( SUCCEEDED( hResult ), NStr::StrFmt( "2D texture %s loading failed", fileName ), return false; );

  pTexture = Create<Texture2D>(pD3DTexture);
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool LoadTextureFromFileImpl(const char* fileName, UINT skipMipLevels, TextureCubeRef &pTexture, int& sizeInFile )
{
  NI_ASSERT( !NDb::GetDbResourceCache().IsAssertionLoadingFiles(), 
    NStr::StrFmt( "Not recommended loading resource \"%s\" after game starting", fileName ) 
  );

	CObj<Stream> pStream = RootFileSystem::OpenFile( fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
	NI_VERIFY( pStream && pStream->IsOk(), NStr::StrFmt( "Cannot open file to load texture from: \"%s\"", fileName ), return false; );

  sizeInFile = pStream->GetSize();

	IDirect3DCubeTexture9* d3dtexture = 0;

  HRESULT hResult = E_FAIL;
  IDirect3DBaseTexture9* pBaseTex = 0;
  //if( Compatibility::IsRunnedUnderWine() )  
    hResult = CreateTextureFromDDSFileInMemory(pStream->GetBuffer(), sizeInFile, skipMipLevels, &pBaseTex);

  if( SUCCEEDED(hResult) ) {
    NI_VERIFY(pBaseTex->GetType() == D3DRTYPE_CUBETEXTURE,
              NStr::StrFmt("Inconsistent texture %s: cubemap texture requested, yet it isn't", fileName),
              pBaseTex->Release(); return false);
    d3dtexture = static_cast<IDirect3DCubeTexture9*>(pBaseTex);
  }
  else
    // –∫—É–±–æ–≤-–Ω–µ-DDS –≤ –∫–æ–Ω—Ç–µ–Ω—Ç–µ –Ω–µ—Ç (–∑–∞–º–µ—Ä: 16 –∫—É–±–∏—á–µ—Å–∫–∏—Ö .dds ‚Äî 13 DXT1 +
    // 3 A8R8G8B8), –∞ –∫—É–±—ã —É–º–µ–µ—Ç –∏ —Å–≤–æ–π –∑–∞–≥—Ä—É–∑—á–∏–∫ (CubeTextureHolder –≤ ReadDDS.cpp)
    NI_ASSERT( false, NStr::StrFmt( "Cube texture %s is not a loadable DDS (hr=0x%08X)", fileName, hResult ) );
	pStream = 0;

	NI_VERIFY(SUCCEEDED(hResult), NStr::StrFmt("Cube texture %s loading failed", fileName), return false);

  pTexture = Create<TextureCube>(d3dtexture);
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum LoadTextureFlag
{
  LTF_NONE                  = 0,
  LTF_FORCE_RELOAD          = 1 << 0,
  LTF_CAN_BE_VISUAL_DEGRADE = 1 << 1
};

UINT CalcSkipMipLevels( const bool canBeVisualDegrade )
{
  return canBeVisualDegrade ? static_cast<UINT>(g_skipMipLevels) : 0;
}

template <class T>
CObj<T> LoadTextureFromFile( const nstl::string& _filename, void * poolId, unsigned int flags )
{
  if(RENDER_DISABLED)
    return CObj<T>();

	NDebug::PerformanceDebugVarGuard render_TexturesLoadTimeGuard( render_TexturesLoadTime, false );

  nstl::string correctFileName = ( _filename[0] == '/' || _filename[0] == '\\' ) ? _filename : ( "/" + _filename );

  NI_ASSERT( g_skipMipLevels >= 0 && g_skipMipLevels <= 20, NStr::StrFmt("Incorrect value of gfx_skip_mip_levels = %d", g_skipMipLevels) );
  const bool canBeVisualDegrade = flags & LTF_CAN_BE_VISUAL_DEGRADE;
  const UINT skipMipLevels = CalcSkipMipLevels( canBeVisualDegrade );

	CObj<T> pTexture;
  PoolTextureWrapper wrapper;

	if( s_texturesPool.Find(correctFileName, &wrapper) )
  {
    if(flags & LTF_FORCE_RELOAD) {
      bool bResult = LoadTextureFromFileImpl( correctFileName.c_str(), skipMipLevels, pTexture, wrapper.sizeInFile );
      NI_VERIFY( bResult, NStr::StrFmt( "cant load texture: %s", correctFileName.c_str() ), GetDefaultTexture(pTexture); );
      
      dynamic_cast<T*>( wrapper.texPtr.GetPtr() )->SetTexture(*pTexture);
    }

    //Update texturePoolId
    int poolIdx = 0;
    for( ; poolIdx < wrapper.usedInPools.size(); ++poolIdx )
      if ( wrapper.usedInPools[poolIdx] == poolId )
        break;
    if ( poolIdx == wrapper.usedInPools.size() )
      wrapper.usedInPools.push_back( poolId );

    pTexture = dynamic_cast<T*>( wrapper.texPtr.GetPtr() );
    NI_VERIFY( pTexture, NStr::StrFmt( "requested texture doesn't match type: %s", correctFileName.c_str() ), GetDefaultTexture(pTexture); );
	}
	else
	{
    int sizeInFile = 0;
		bool bResult = LoadTextureFromFileImpl( correctFileName.c_str(), skipMipLevels, pTexture, sizeInFile );
		NI_VERIFY( bResult, NStr::StrFmt( "cant load texture: %s", correctFileName.c_str() ), GetDefaultTexture(pTexture); );

    //FIXME: Accessing filesystem twice!!! Maybe even reading file twice
    SFileInfo fileInfo;
    RootFileSystem::GetFileInfo( &fileInfo, correctFileName );
    s_texturesPool.MarkAsLoaded(PoolTextureWrapper(pTexture.GetPtr(), fileInfo.time, skipMipLevels, canBeVisualDegrade, poolId, sizeInFile),
                                correctFileName);
		render_TexturesCount.AddValue(1);
	}

	return pTexture;
}

void GetDefaultTexture(Texture2DRef &pTex) { pTex = GetDefaultTexture2D(); }
void GetDefaultTexture(TextureCubeRef &pTex) { pTex = TextureCubeRef(); }

struct CReloadTextures
{
  bool bModifiedOnly;

public :
  typedef pair< const nstl::string, PoolTextureWrapper > tex_info_type;

public :

 CReloadTextures(bool bModifiedOnly) : bModifiedOnly(bModifiedOnly) {}

 void operator()( tex_info_type &texInfo )
 {
   const bool needReload = UpdateTexInfo( texInfo );
   
   if ( bModifiedOnly && !needReload )
     return;
   
   if(RENDER_DISABLED)
     return;

   Texture2DRef   curTex2dRef;
   TextureCubeRef curTexCubeRef;
   if( curTex2dRef = dynamic_cast<Texture2D*>( texInfo.second.texPtr.GetPtr() ) )
   {
     Texture2DRef newTexRef;
     LoadTextureFromFileImpl( texInfo.first.c_str(), texInfo.second.skipMipLevels, newTexRef, texInfo.second.sizeInFile );
     
     curTex2dRef->SetTexture(*newTexRef);
   }
   else if ( curTexCubeRef = dynamic_cast<TextureCube*>( texInfo.second.texPtr.GetPtr() ) )
   {
     TextureCubeRef newTexRef;
     LoadTextureFromFileImpl( texInfo.first.c_str(), texInfo.second.skipMipLevels, newTexRef, texInfo.second.sizeInFile );
     
     curTexCubeRef->SetTexture(*newTexRef);
   }
	 
   systemLog( NLogg::LEVEL_MESSAGE ) << "Texture from " << texInfo.first.c_str() << " was reloaded " << endl;
 }

private :
  bool UpdateTexInfo( tex_info_type &texInfo )
  { 
    SFileInfo fileInfo;
    RootFileSystem::GetFileInfo( &fileInfo, texInfo.first );
    
    const UINT skipMipLevels = CalcSkipMipLevels( texInfo.second.canBeVisualDegrade );

    const bool needReload = 
      fileInfo.time.GetFullTime() != texInfo.second.modificationTime.GetFullTime() ||
      skipMipLevels != texInfo.second.skipMipLevels;
      
    texInfo.second.modificationTime = fileInfo.time;
    texInfo.second.skipMipLevels = skipMipLevels;
      
    return needReload;
  }
};

bool ReloadTexturesCmd( const char *name, const vector<wstring>& params )
{
  bool bModifiedOnly = true;
  if (params.size() == 1 && params[0] == L"all")
    bModifiedOnly = false;
  ReloadTextures( bModifiedOnly );
  return true;
}

bool DumpTexturePool( const char *name, const vector<wstring>& params )
{
  return s_texturesPool.Dump(name, params);
}

} // End of module internals

// Used in RenderResourceManager::FrameTick()
void TickTextureManager() { s_texturesPool.MoveMarkedInPool(); }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DefaultTextures defTextures;

DefaultTextures&    GetDefaultTextures()  { return defTextures; }
const Texture2DRef& GetDefaultTexture2D() { return defTextures.pTexture; }
const Texture2DRef& GetEmptyTexture2D() { return defTextures.pEmptyTexture; }
const Texture2DRef& GetWhiteTexture2D() { return defTextures.pWhiteTexture; }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// –ï–¥–∏–Ω—Å—Ç–≤–µ–Ω–Ω–æ–µ –º–µ—Å—Ç–æ, –≥–¥–µ –¥–ª—è –∑–∞–≥—Ä—É–∑–∫–∏ —Ç–µ–∫—Å—Ç—É—Ä –µ—â—ë –Ω—É–∂–µ–Ω D3DX: —ç–º–±–ª–µ–º—ã –≥–∏–ª—å–¥–∏–π
// –ø—Ä–∏—Ö–æ–¥—è—Ç –±–∞–π—Ç–∞–º–∏ –ø–æ URL (GuildEmblem.cpp) ‚Äî —ç—Ç–æ –Ω–µ DDS, –∞ –≥–æ—Ç–æ–≤–∞—è –∫–∞—Ä—Ç–∏–Ω–∫–∞
// –≤–µ–±-—Ñ–æ—Ä–º–∞—Ç–∞. –ß—Ç–æ–±—ã —Å–Ω—è—Ç—å d3dx9_43.dll –ø–æ–ª–Ω–æ—Å—Ç—å—é, —Å—é–¥–∞ –Ω—É–∂–µ–Ω —Å–≤–æ–π PNG-–¥–µ–∫–æ–¥–µ—Ä
// (zlib —É–∂–µ –ø–æ–¥–∫–ª—é—á—ë–Ω) –ª–∏–±–æ –ø–µ—Ä–µ–≤–æ–¥ —ç–º–±–ª–µ–º –≤ DDS –Ω–∞ —Å—Ç–æ—Ä–æ–Ω–µ –æ—Ç–¥–∞—á–∏. –î–ª—è —ç—Ç–∞–ø–∞ 2
// –≤–∞–∂–Ω–æ –¥—Ä—É–≥–æ–µ: —ç—Ç–æ –µ–¥–∏–Ω—Å—Ç–≤–µ–Ω–Ω—ã–π –æ—Å—Ç–∞–≤—à–∏–π—Å—è –∑–∞–≥—Ä—É–∑—á–∏–∫, –∑–∞–≤–∏—Å—è—â–∏–π –æ—Ç —É—Å—Ç—Ä–æ–π—Å—Ç–≤–∞.
Texture2DRef CreateARGBTextureFromFileInMemory(const char* pData, int sizeInFile, UINT skipMipLevels)
{
  if( g_disableTextureLoads )
    return GetDefaultTextures().pTexture;

  IDirect3DTexture9* pD3DTexture = 0;
  HRESULT hResult = D3DXCreateTextureFromFileInMemoryEx(GetDevice(), pData, sizeInFile,
                                                        D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 0, 0, 
                                                        D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_TRIANGLE, 
                                                        D3DX_SKIP_DDS_MIP_LEVELS(skipMipLevels, D3DX_DEFAULT), 
                                                        0, 0, 0, &pD3DTexture);
  NI_DX_WARN(hResult, "CreateARGBTextureFromFileInMemory()");
  if( FAILED(hResult) )
    return Texture2DRef();

  return Create<Texture2D>(pD3DTexture);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DRef CreateTextureFromFileInMemory(const char* pData, int sizeInFile, UINT skipMipLevels)
{
  if( g_disableTextureLoads )
    return GetDefaultTextures().pTexture;

  IDirect3DTexture9* pD3DTexture = 0;
  HRESULT hResult = CreateTextureFromFileInMemoryImpl(&pD3DTexture, pData, sizeInFile, skipMipLevels);
  NI_DX_WARN(hResult, "CreateTextureFromFileInMemory()");
  if( FAILED(hResult) )
    return Texture2DRef();

  return Create<Texture2D>(pD3DTexture);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DRef CreateTexture2D(unsigned int width, unsigned int height, unsigned int level, PoolType poolType, ERenderFormat formatType)
{
  // –ü—É–ª –∏ usage –≤—ã–±–∏—Ä–∞–µ—Ç –±—ç–∫–µ–Ω–¥ (EBackendPool -> D3DPOOL + D3DUSAGE_*,
  // GetD3DPoolAndUsagesParamaters –≤—ã–∑—ã–≤–∞–µ—Ç—Å—è –≤–Ω—É—Ç—Ä–∏ –Ω–µ–≥–æ). PoolType –∏ EBackendPool ‚Äî
  // —Å–ª–æ–≤–∞—Ä–∏ –æ–¥–∏–Ω–∞–∫–æ–≤–æ–≥–æ —Å–æ—Å—Ç–∞–≤–∞, –ø–∞—Ä–∏—Ç–µ—Ç –ø—Ä–æ–≤–µ—Ä–µ–Ω static_assert'–æ–º –≤ –±—ç–∫–µ–Ω–¥–µ.
  HTexture hTex;
  if(!RENDER_DISABLED) {
    HRESULT hr = GetBackend()->CreateTexture2D(width, height, level, usageNone,
                                               RenderFormatToBackend(formatType),
                                               static_cast<EBackendPool>(poolType), &hTex);
    NI_DX_THROW_CRITICAL( hr, "Couldn't create texture" );
  }
	return Create<Texture2D>( static_cast<IDirect3DTexture9*>(hTex.p) );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DRef CreateRenderTexture2D(unsigned int width, unsigned int height, ERenderFormat formatType, bool autoMipmaps /* = FALSE */)
{
  // this call may occur during lost device, it will be created ASAP
  D3DSURFACE_DESC desc = {ConvertRenderFormat(formatType), D3DRTYPE_SURFACE, D3DUSAGE_RENDERTARGET | (autoMipmaps ? D3DUSAGE_AUTOGENMIPMAP : 0),
    D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0, width, height};
  Texture2DRef tex = Create<Texture2D>( desc );
  return tex;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TextureCubeRef CreateRenderCubeTexture(unsigned int edgeLen, ERenderFormat formatType)
{
  HTexture hTex;
  HRESULT res = GetBackend()->CreateTextureCube(edgeLen, 1, usageRenderTarget,
                                                RenderFormatToBackend(formatType),
                                                BACKEND_POOL_DEFAULT, &hTex);
  NI_VERIFY( res == D3D_OK, "cant create rendertexture!", return TextureCubeRef(); )

  return new TextureCube( static_cast<IDirect3DCubeTexture9*>(hTex.p) ); // no safety in case of lost device [11/23/2009 smirnov]
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
RenderSurfaceRef CreateDepthStencilSurface( unsigned int width, unsigned int height )
{
  // this call may occur during lost device, it will be created ASAP
  D3DSURFACE_DESC desc = {D3DFMT_D24S8, D3DRTYPE_SURFACE, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0, width, height};
  return Create<RenderSurface>(desc);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DRef Create2DTextureFromArray2D( const CArray2D<Render::Color> & src )
{
  if(RENDER_DISABLED)
    return Texture2DRef();

	const int w = src.GetSizeX();
	const int h = src.GetSizeY();
	NI_VERIFY(w > 0 && h > 0, "Invalid CArray2D given for texture creation", return GetDefaultTexture2D(); );

	IDirect3DTexture9* pD3DTexture = 0;
  HTexture hTex;
  HRESULT hResult = GetBackend()->CreateTexture2D(w, h, 0, usageNone, bfA8R8G8B8, BACKEND_POOL_MANAGED, &hTex);
  pD3DTexture = static_cast<IDirect3DTexture9*>(hTex.p);

	NI_VERIFY( SUCCEEDED( hResult ), NStr::StrFmt( "D3DXCreateTexture failed with result 0x%08X", hResult ), return GetDefaultTexture2D(); );

	D3DLOCKED_RECT rect;
	// ¡ÎÓÍËÓ‚Í‡ ˜ÂÂÁ seam: pD3DTexture Á‰ÂÒ¸ = Û˜Í‡, ‚ÓÁ‚‡˘∏ÌÌ‡ˇ ·˝ÍÂÌ‰ÓÏ.
	// COM-‚˚ÁÓ‚ ÔÓ ÌÂÈ ÔÓ‰ ‰Û„ËÏ ·˝ÍÂÌ‰ÓÏ = ‚˚ÁÓ‚ ÔÓ ˜ÛÊÓÈ vtable (¯‡„ 13.4c).
	void* lockBits = 0;
	unsigned lockPitch = 0;
	if ( SUCCEEDED( GetBackend()->LockTexture2D( hTex, 0, 0, NULL, LOCK_DEFAULT, &lockBits, &lockPitch ) ) )
	{
		rect.pBits = lockBits;
		rect.Pitch = ( LONG ) lockPitch;
		char *p = (char *)rect.pBits;
		for ( int y = 0; y < h; ++y )
		{
			memcpy( p, &src[y][0], 4*w );
			p += rect.Pitch;
		}
		GetBackend()->UnlockTexture2D( hTex, 0, 0 );
	}
	return Create<Texture2D>( pD3DTexture );
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void OnTextureDestruction(Texture *tex)
{
  //// remove from textures pool
  //TexturesPool::iterator it;
  //for (it = s_texturesPool.begin(); it != s_texturesPool.end(); ++it)
  //{
  //  if (it->second.texPtr == tex)
  //  {
  //    s_texturesPool.erase(it);
  //    render_TexturesCount.DecValue(1);
  //    break;
  //  }
  //}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Reset2DTextureFromFile( const nstl::string& filename )
{
  Load2DTextureFromFile( filename, true );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DRef Load2DTextureFromFile( const nstl::string& filename, bool bForceReload )
{
  return LoadTextureFromFile<Texture2D>( filename, 0, (bForceReload ? LTF_FORCE_RELOAD : LTF_NONE) );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Render::TextureCubeRef LoadCubeTextureFromFileIntoPoolRef( const nstl::string& filename, bool canBeVisualDegrade, void * poolId )
{
  return LoadTextureFromFile<TextureCube>( filename, poolId, (canBeVisualDegrade ? LTF_CAN_BE_VISUAL_DEGRADE : LTF_NONE) );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TextureCubeRef LoadCubeTextureFromFileRef( const nstl::string& filename )
{
  return LoadCubeTextureFromFileIntoPoolRef( filename, false, 0 );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Render::Texture2DRef LoadTexture2DIntoPool( const NDb::Texture &txt, bool canBeVisualDegrade, void * poolId )
{
	if ( txt.textureFileName.size() > 0 )
    return LoadTextureFromFile<Texture2D>( txt.textureFileName, poolId, 
               (canBeVisualDegrade ? LTF_CAN_BE_VISUAL_DEGRADE : LTF_NONE) );
	else
		return GetWhiteTexture2D();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DRef LoadTexture2D( const NDb::Texture &txt )
{
  return LoadTexture2DIntoPool( txt, false, 0 );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UnloadAllTextures()
{
  s_texturesPool.Clear();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReloadTextures( bool bModifiedOnly )
{
  s_texturesPool.ReloadTextures(bModifiedOnly);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void UnloadTexturePool( void * poolId )
{
  s_texturesPool.UnloadTexturePool(poolId);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ShowMipmapsWithReload(bool show)
{
  if (g_showMipMap != show)
  {
    g_showMipMap = show;
    ReloadTextures(false);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ForAllTextures(TextureProc &p)
{
  s_texturesPool.ForAllTextures(p);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void DefaultTextures::Init()
{
  if(RENDER_DISABLED)
    return;

	pEmptyTexture = Create<Texture2D>( (IDirect3DTexture9*)0 );

	// Load default texture
	pTexture = Load2DTextureFromFile( "Tech/Default/DefaultTexture.dds" );

	// Create white texture
	pWhiteTexture = CreateTexture2D( 1, 1, 1, Render::RENDER_POOL_MANAGED, FORMAT_A8R8G8B8 );
	LockedRect rect = pWhiteTexture->LockRect( 0,0,1,0,1, Render::LOCK_DEFAULT);
	*(uint*)rect.data = 0xFFFFFFFF;
	pWhiteTexture->UnlockRect(0);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void DefaultTextures::Term()
{
  pWhiteTexture = 0;
  pTexture = 0;
  pEmptyTexture = 0;
}

//======================================================================================
// PoolMT implementation
//======================================================================================
void PoolMT::ForAllTextures(TextureProc &p)
{
  TexturesPool::iterator it;
  for (it = texturesPool.begin(); it != texturesPool.end(); ++it)
  {
    p(it->first, TextureRef(it->second.texPtr));
  }
}

bool PoolMT::Dump(const char *name, const vector<wstring>& params)
{
  typedef nstl::list<nstl::string> FileNames;
  typedef nstl::map<int, FileNames> SortMap;
  SortMap sortMap;

  {
    TexturesPool::const_iterator it = texturesPool.begin();
    TexturesPool::const_iterator last = texturesPool.end();

    for ( ; it != last; ++it )
    {
      sortMap[-it->second.sizeInFile].push_back( it->first );
    }
  }

  DebugTrace( "TEXTURE MANAGER DUMP");

  int countSize = 0;
  int countNumber = 0;

  {
    SortMap::iterator it = sortMap.begin();
    SortMap::iterator last = sortMap.end();

    for ( ; it != last; ++it )
    {
      FileNames::iterator itF = it->second.begin();
      FileNames::iterator lastF = it->second.end();

      for ( ; itF != lastF; ++itF )
      {
        countNumber++;
        countSize += abs(it->first);
        DebugTrace( "%8d %s", abs(it->first), *itF );
      }
    }
  }

  DebugTrace( "Total: %d bytes in %d textures", countSize, countNumber );

  return true;
}


void PoolMT::ReloadTextures( bool bModifiedOnly )
{
  SmartRenderer::NullThePointers();
  for_each( texturesPool.begin(), texturesPool.end(), CReloadTextures(bModifiedOnly) );
}

void PoolMT::UnloadTexturePool( void * poolId )
{
  for ( TexturesPool::iterator it = texturesPool.begin(); it != texturesPool.end(); )
  {
    TexturesPool::iterator curr = it++;
    curr->second.usedInPools.remove( poolId );
    if ( curr->second.usedInPools.empty() )
      texturesPool.erase( curr );
  }
}

} //namespace Render

//======================================================================================

REGISTER_DEV_CMD( reloadtextures, Render::ReloadTexturesCmd );
REGISTER_DEV_CMD( dump_textures, Render::DumpTexturePool );
