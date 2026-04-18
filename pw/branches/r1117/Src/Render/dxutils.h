#pragma once

#if defined(PW_LINUX_NULL_RENDER)

#include <climits>
#include <cstring>

#include "../System/config.h"
#include "../System/ported/types.h"
#include "../System/nstring.h"
#include "../System/nvector.h"
#include "../System/IntrusivePtr.h"
#include "../System/noncopyable.h"

#ifndef ZeroMemory
#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif

#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

typedef HANDLE HMONITOR;
#ifndef WCHAR
typedef wchar_t WCHAR;
#endif

struct IUnknown
{
  ULONG AddRef() { return 1; }
  ULONG Release() { return 1; }
};
struct D3DDEVICE_CREATION_PARAMETERS
{
  UINT AdapterOrdinal;
  int DeviceType;
  HWND hFocusWindow;
  DWORD BehaviorFlags;
};
struct IDirect3D9 : IUnknown
{
  HMONITOR GetAdapterMonitor(UINT) { return 0; }
};
struct IDirect3DDevice9 : IUnknown
{
  HRESULT GetDirect3D(IDirect3D9** outD3D)
  {
    if (outD3D)
      *outD3D = 0;
    return E_FAIL;
  }

  HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* params)
  {
    if (params)
      ZeroMemory(params, sizeof(*params));
    return E_FAIL;
  }
};
struct IDirect3DResource9 : IUnknown {};
struct IDirect3DBaseTexture9 : IUnknown {};
struct IDirect3DTexture9 : IDirect3DBaseTexture9 {};
struct IDirect3DCubeTexture9 : IDirect3DBaseTexture9 {};
typedef IDirect3DBaseTexture9* PDIRECT3DBASETEXTURE9;
struct D3DVERTEXBUFFER_DESC
{
  int Format;
  int Type;
  DWORD Usage;
  int Pool;
  UINT Size;
  DWORD FVF;
};
struct D3DINDEXBUFFER_DESC
{
  int Format;
  int Type;
  DWORD Usage;
  int Pool;
  UINT Size;
};
struct IDirect3DVertexBuffer9
{
  nstl::vector<unsigned char> storage;
  int format = 100;

  HRESULT Lock(UINT, UINT, void** data, DWORD)
  {
    if (data)
      *data = storage.empty() ? 0 : &storage[0];
    return 0;
  }

  HRESULT GetDesc(D3DVERTEXBUFFER_DESC* desc)
  {
    if (desc)
    {
      memset(desc, 0, sizeof(*desc));
      desc->Format = format;
      desc->Type = 1;
      desc->Pool = 2;
      desc->Size = static_cast<UINT>(storage.size());
    }
    return 0;
  }

  void Unlock() {}
};
struct IDirect3DIndexBuffer9
{
  nstl::vector<unsigned char> storage;
  int format = 102;

  HRESULT Lock(UINT, UINT, void** data, DWORD)
  {
    if (data)
      *data = storage.empty() ? 0 : &storage[0];
    return 0;
  }

  HRESULT GetDesc(D3DINDEXBUFFER_DESC* desc)
  {
    if (desc)
    {
      memset(desc, 0, sizeof(*desc));
      desc->Format = format;
      desc->Type = 1;
      desc->Pool = 2;
      desc->Size = static_cast<UINT>(storage.size());
    }
    return 0;
  }

  void Unlock() {}
};
struct IDirect3DVertexDeclaration9 : IUnknown {};
struct IDirect3DVertexShader9 : IUnknown {};
struct IDirect3DPixelShader9 : IUnknown {};

struct D3DADAPTER_IDENTIFIER9
{
  char Driver[MAX_PATH];
  char Description[512];
  char DeviceName[32];
  LARGE_INTEGER DriverVersion;
  DWORD VendorId;
  DWORD DeviceId;
  DWORD SubSysId;
  DWORD Revision;
  GUID DeviceIdentifier;
  DWORD WHQLLevel;
};

struct D3DCAPS9
{
  DWORD Caps;
  DWORD Caps2;
  DWORD Caps3;
  DWORD PresentationIntervals;
  DWORD CursorCaps;
  DWORD DevCaps;
  DWORD PrimitiveMiscCaps;
  DWORD RasterCaps;
  DWORD ZCmpCaps;
  DWORD SrcBlendCaps;
  DWORD DestBlendCaps;
  DWORD AlphaCmpCaps;
  DWORD ShadeCaps;
  DWORD TextureCaps;
  DWORD TextureFilterCaps;
  DWORD CubeTextureFilterCaps;
  DWORD VolumeTextureFilterCaps;
  DWORD TextureAddressCaps;
  DWORD VolumeTextureAddressCaps;
  DWORD LineCaps;
  DWORD MaxTextureWidth;
  DWORD MaxVolumeExtent;
  DWORD MaxTextureRepeat;
  DWORD MaxTextureAspectRatio;
  DWORD MaxAnisotropy;
  DWORD StencilCaps;
  DWORD FVFCaps;
  DWORD TextureOpCaps;
  DWORD MaxTextureBlendStages;
  DWORD MaxSimultaneousTextures;
  DWORD VertexProcessingCaps;
  DWORD MaxActiveLights;
  DWORD MaxUserClipPlanes;
  DWORD MaxVertexBlendMatrices;
  DWORD MaxVertexBlendMatrixIndex;
  DWORD MaxPrimitiveCount;
  DWORD MaxVertexIndex;
  DWORD MaxStreams;
  DWORD MaxStreamStride;
  DWORD VertexShaderVersion;
  DWORD MaxVertexShaderConst;
  DWORD PixelShaderVersion;
};

struct OSVERSIONINFO
{
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  TCHAR szCSDVersion[128];
};

struct VS_FIXEDFILEINFO
{
  DWORD dwSignature;
  DWORD dwStrucVersion;
  DWORD dwFileVersionMS;
  DWORD dwFileVersionLS;
  DWORD dwProductVersionMS;
  DWORD dwProductVersionLS;
  DWORD dwFileFlagsMask;
  DWORD dwFileFlags;
  DWORD dwFileOS;
  DWORD dwFileType;
  DWORD dwFileSubtype;
  DWORD dwFileDateMS;
  DWORD dwFileDateLS;
};

typedef int D3DFORMAT;
typedef int D3DRESOURCETYPE;
typedef int D3DMULTISAMPLE_TYPE;
typedef int D3DTEXTUREFILTERTYPE;
typedef DWORD* PDWORD;

enum D3DPOOL
{
  D3DPOOL_DEFAULT = 0,
  D3DPOOL_MANAGED = 1,
  D3DPOOL_SYSTEMMEM = 2,
  D3DPOOL_SCRATCH = 3
};

struct D3DLOCKED_RECT
{
  int Pitch;
  void* pBits;
};

struct D3DSURFACE_DESC
{
  D3DFORMAT Format;
  D3DRESOURCETYPE Type;
  DWORD Usage;
  D3DPOOL Pool;
  D3DMULTISAMPLE_TYPE MultiSampleType;
  DWORD MultiSampleQuality;
  UINT Width;
  UINT Height;
};

struct IDirect3DSurface9
{
  D3DSURFACE_DESC desc = {};

  HRESULT GetDesc(D3DSURFACE_DESC* outDesc)
  {
    if (outDesc)
      *outDesc = desc;
    return 0;
  }
};

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
  ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

enum
{
  VER_PLATFORM_WIN32_WINDOWS = 1,
  VER_PLATFORM_WIN32_NT = 2,
  GENERIC_READ = 0x80000000u,
  FILE_SHARE_READ = 0x00000001u,
  OPEN_EXISTING = 3,
  D3DLOCK_DISCARD = 0x00002000L,
  D3DLOCK_NOOVERWRITE = 0x00001000L,
  D3DLOCK_READONLY = 0x00000010L,
  D3DLOCK_NOSYSLOCK = 0x00000800L,
  D3DUSAGE_RENDERTARGET = 0x00000001L,
  D3DUSAGE_DEPTHSTENCIL = 0x00000002L,
  D3DUSAGE_WRITEONLY = 0x00000008L,
  D3DUSAGE_DYNAMIC = 0x00000200L,
  D3DUSAGE_AUTOGENMIPMAP = 0x00000400L,
  D3DUSAGE_DMAP = 0x00004000L,
  D3DRTYPE_SURFACE = 1,
  D3DRTYPE_TEXTURE = 3,
  D3DRTYPE_CUBETEXTURE = 5,
  D3DFMT_UNKNOWN = 0,
  D3DFMT_R8G8B8 = 20,
  D3DFMT_R5G6B5 = 23,
  D3DFMT_A8R8G8B8 = 21,
  D3DFMT_X8R8G8B8 = 22,
  D3DFMT_A8 = 28,
  D3DFMT_L8 = 50,
  D3DFMT_L16 = 81,
  D3DFMT_R16F = 111,
  D3DFMT_G32R32F = 115,
  D3DFMT_R32F = 114,
  D3DFMT_D24S8 = 75,
  D3DFMT_D24X8 = 77,
  D3DFMT_D16 = 80,
  D3DFMT_DXT1 = MAKEFOURCC('D', 'X', 'T', '1'),
  D3DFMT_VERTEXDATA = 100,
  D3DFMT_INDEX16 = 101,
  D3DFMT_INDEX32 = 102,
  D3DFMT_A16B16G16R16F = 113,
  D3DFMT_A32B32G32R32F = 116,
  D3DMULTISAMPLE_NONE = 0,
  D3DTEXF_POINT = 1,
  D3DTEXF_LINEAR = 2,
  D3DBLEND_ZERO = 1,
  D3DBLEND_INVSRCALPHA = 6,
  D3DRS_ZENABLE = 7,
  D3DRS_ZWRITEENABLE = 14,
  D3DRS_SRCBLENDALPHA = 207,
  D3DRS_DESTBLENDALPHA = 208,
  D3DRS_COLORWRITEENABLE1 = 190,
  D3DRS_SEPARATEALPHABLENDENABLE = 206
};

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

BOOL WINAPI GetVersionEx(OSVERSIONINFO* lpVersionInformation);
DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, DWORD* lpdwHandle);
BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
BOOL WINAPI VerQueryValueA(const LPVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, UINT* puLen);
HANDLE WINAPI CreateFile(LPCTSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPVOID lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
DWORD WINAPI GetCurrentDirectory(DWORD nBufferLength, LPTSTR lpBuffer);
DWORD WINAPI GetFileSize(HANDLE hFile, DWORD* lpFileSizeHigh);
BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, DWORD* lpNumberOfBytesRead, LPVOID lpOverlapped);
BOOL WINAPI CloseHandle(HANDLE hObject);
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);

template<typename T>
T* Get(T* &p)
{
  return p;
}

template<typename T>
T* Get(T* const& p)
{
  return p;
}

template<typename T>
class DXPtr
{
public:
  DXPtr()
    : ptr(0)
  {
  }

  DXPtr(T* value)
    : ptr(value)
  {
  }

  T* GetPtr() const
  {
    return ptr;
  }

  void Attach(T* value)
  {
    ptr = value;
  }

  T* Detach()
  {
    T* value = ptr;
    ptr = 0;
    return value;
  }

  DXPtr& operator=(T* value)
  {
    ptr = value;
    return *this;
  }

  T* operator->() const
  {
    return ptr;
  }

  operator T*() const
  {
    return ptr;
  }

  explicit operator bool() const
  {
    return ptr != 0;
  }

private:
  T* ptr;
};

template<typename T>
T* Get(const DXPtr<T>& p)
{
  return p.GetPtr();
}

namespace NDb
{
class DBID;
}

namespace Render
{

struct DeviceLostHandler : NonCopyable
{
  enum HandlerPriority
  {
    HANDLERPRIORITY_HIGH,
    HANDLERPRIORITY_NORMAL,
    HANDLERPRIORITY_LOW,
    HANDLERPRIORITY_SUPER_LOW,
    HANDLERPRIORITY_COUNT
  };

protected:
  explicit DeviceLostHandler(HandlerPriority p = HANDLERPRIORITY_NORMAL)
  {
    (void)p;
  }

public:
  virtual void OnDeviceLost() = 0;
  virtual void OnDeviceReset() = 0;
  virtual ~DeviceLostHandler() {}
};

template<class T>
struct DeviceLostWrapper : public T
{
  DeviceLostWrapper() {}

  template<class T1>
  DeviceLostWrapper(T1 *_arg1) : T(_arg1) {}

  template<class T1>
  DeviceLostWrapper(const T1& _arg1) : T(_arg1) {}

  template<class T1, class T2>
  DeviceLostWrapper(T1 *_arg1, const T2& _arg2) : T(_arg1, _arg2) {}

  template<class T1, class T2>
  DeviceLostWrapper(const T1& _arg1, const T2& _arg2) : T(_arg1, _arg2) {}

  template<class T1, class T2, class T3>
  DeviceLostWrapper(const T1& _arg1, const T2& _arg2, const T3& _arg3) : T(_arg1, _arg2, _arg3) {}
};

struct DeviceDeleteHandler : NonCopyable
{
  virtual ~DeviceDeleteHandler() {}
  virtual void OnDeviceDelete() = 0;
  virtual void OnDeviceCreate() {}
};

class Material;

typedef DXPtr<IDirect3DBaseTexture9> DXBaseTextureRef;
typedef DXPtr<IDirect3DTexture9> DXTextureRef;
typedef DXPtr<IDirect3DCubeTexture9> DXCubeTextureRef;
typedef DXPtr<IDirect3DSurface9> DXSurfaceRef;
typedef DXPtr<IDirect3DSurface9> DXSurfacePtr;
typedef DXPtr<IDirect3DVertexBuffer9> DXVertexBufferRef;
typedef DXPtr<IDirect3DIndexBuffer9> DXIndexBufferRef;
typedef DXPtr<IDirect3DVertexDeclaration9> DXVertexDeclarationRef;
typedef DXPtr<IDirect3DVertexShader9> DXVertexShaderRef;
typedef DXPtr<IDirect3DPixelShader9> DXPixelShaderRef;

Material* CreateRenderMaterial(const int typeId);

void SetErrorMessage(HRESULT hr, const nstl::wstring &msg);
void ShowErrorMessageAndTerminate(HRESULT hr);

class DXVertexBufferDynamicRef : public DeviceLostHandler
{
  DXVertexBufferRef pDXBuffer;
  UINT size;

protected:
  DXVertexBufferDynamicRef()
    : size(0)
  {
  }

  explicit DXVertexBufferDynamicRef(UINT size_)
    : size(size_)
  {
  }

public:
  typedef IDirect3DVertexBuffer9 Iface;
  typedef DeviceLostWrapper<DXVertexBufferDynamicRef> Wrapped;

  bool Resize(int size, bool nullOnLostDevice = false);
  UINT GetSize() const { return size; }

  void Reset()
  {
    pDXBuffer = 0;
    size = 0;
  }

  IDirect3DVertexBuffer9* operator->() const { return Get(pDXBuffer); }
  IDirect3DVertexBuffer9* __gEt__() const { return Get(pDXBuffer); }

  virtual void OnDeviceLost()
  {
    pDXBuffer = 0;
  }

  virtual void OnDeviceReset();
};

inline IDirect3DVertexBuffer9* Get(DXVertexBufferDynamicRef const& sp)
{
  return sp.__gEt__();
}

template<UINT elemSize>
class DXIndexBufferDynamicRef_ : public DeviceLostHandler
{
  DXIndexBufferRef pDXBuffer;
  UINT size;

protected:
  DXIndexBufferDynamicRef_()
    : size(0)
  {
  }

  explicit DXIndexBufferDynamicRef_(UINT size_)
    : size(size_)
  {
  }

public:
  typedef IDirect3DIndexBuffer9 Iface;
  typedef DeviceLostWrapper<DXIndexBufferDynamicRef_> Wrapped;

  bool Resize(int size);
  UINT GetSize() const { return size; }

  void Reset()
  {
    pDXBuffer = 0;
    size = 0;
  }

  IDirect3DIndexBuffer9* operator->() const { return Get(pDXBuffer); }
  IDirect3DIndexBuffer9* __gEt__() const { return Get(pDXBuffer); }

  virtual void OnDeviceLost()
  {
    pDXBuffer = 0;
  }

  virtual void OnDeviceReset();
};

typedef DXIndexBufferDynamicRef_<32> DXIndexBufferDynamicRef;
typedef DXIndexBufferDynamicRef_<16> DXIndexBufferDynamicRef16;

template<UINT elemSize>
inline IDirect3DIndexBuffer9* Get(DXIndexBufferDynamicRef_<elemSize> const& sp)
{
  return sp.__gEt__();
}

enum PoolType
{
  RENDER_POOL_DEFAULT,
  RENDER_POOL_MANAGED,
  RENDER_POOL_DYNAMIC,
  RENDER_POOL_TEX_DYNAMIC,
  RENDER_POOL_SYSMEM,
  RENDER_POOL_SYSMEM_DYNAMIC,
};

void GetD3DPoolAndUsagesParamaters(DWORD& usage, D3DPOOL& pool, PoolType poolType);

DXVertexBufferRef CreateVB(int size, PoolType type, void const *pData = 0);
void* LockVB(IDirect3DVertexBuffer9 *pBuff, unsigned int flags, int size = 0);
void FillVB(IDirect3DVertexBuffer9 *pBuff, int size, void const *pData, unsigned int lockFlags = 0);

template <typename T>
T* LockVB(IDirect3DVertexBuffer9 *pBuff, unsigned int flags, int size = 0)
{
  return reinterpret_cast<T*>( LockVB(pBuff, flags, size) );
}

DXIndexBufferRef CreateIB(int size, PoolType poolType, UINT const *pData = 0);
DXIndexBufferRef CreateIB16(int size, PoolType poolType, WORD const *pData = 0);
unsigned int* LockIB(IDirect3DIndexBuffer9 *pBuff, unsigned int flags, int size = 0);
void FillIB(IDirect3DIndexBuffer9 *pBuff, int size, void const *pData, unsigned int lockFlags);

int D3DFormatNumBits(D3DFORMAT format);

template<class _DynBuffer>
class SharedD3DBufferST : protected _DynBuffer
{
  DWORD position;
  DWORD sizeRequested;
  DWORD timeStamp;
  bool isLocked;

protected:
  SharedD3DBufferST()
    : position(0)
    , sizeRequested(0)
    , timeStamp(0)
    , isLocked(false)
  {
  }

  explicit SharedD3DBufferST(DWORD _size, float _minSizeScale = 0.0f)
    : _DynBuffer(_size)
    , position(0)
    , sizeRequested(0)
    , timeStamp(0)
    , isLocked(false)
  {
    (void)_minSizeScale;
  }

  ~SharedD3DBufferST()
  {
    Clear();
  }

public:
  typedef typename _DynBuffer::Iface Iface;

  void Clear()
  {
    Reset();
    _DynBuffer::Reset();
  }

  virtual void OnDeviceLost()
  {
    Reset();
    _DynBuffer::OnDeviceLost();
  }

  virtual void OnDeviceReset()
  {
    _DynBuffer::OnDeviceReset();
  }

  UINT GetSize() const
  {
    return _DynBuffer::GetSize();
  }

  UINT GetTimeStamp() const
  {
    return timeStamp;
  }

  void* GetPointer(UINT _pos) const
  {
    (void)_pos;
    return 0;
  }

  Iface* GetBuffer() const
  {
    return Get(static_cast<const _DynBuffer&>(*this));
  }

  bool IsLocked()
  {
    return isLocked;
  }

  UINT AcquireSpace(DWORD _size)
  {
    if (!isLocked)
      return UINT_MAX;

    sizeRequested += _size;
    if (position + _size > GetSize())
      return UINT_MAX;

    const UINT pos = position;
    position += _size;
    return pos;
  }

  void QuerySize(UINT _size)
  {
    if (_size == 0)
    {
      Reset();
      _DynBuffer::Reset();
      return;
    }

    _DynBuffer::Resize(_size);
  }

  bool Resize(int _size)
  {
    return _DynBuffer::Resize(_size);
  }

  bool Lock()
  {
    position = 0;
    sizeRequested = 0;
    isLocked = false;
    return false;
  }

  void Unlock()
  {
    isLocked = false;
  }

  static void UnlockAll()
  {
  }

  void Reset()
  {
    position = 0;
    sizeRequested = 0;
    isLocked = false;
  }

  static void ResetAll()
  {
  }
};

typedef DeviceLostWrapper< SharedD3DBufferST<DXVertexBufferDynamicRef> > SharedVB;
typedef DeviceLostWrapper< SharedD3DBufferST<DXIndexBufferDynamicRef> > SharedIB;

class SpaceHolder
{
  UINT position;
  UINT size;
  UINT timeStamp;

  typedef class NDb::DBID NDBID;

  template<class T> bool AcquireSpace_(DWORD _size, T& _buffer);
  template<class T> bool IsValid_(const T& _buffer, const NDBID& _id) const;

public:
  bool AcquireSpace(DWORD _size, SharedVB& _buffer) { return AcquireSpace_(_size, _buffer); }
  bool AcquireSpace(DWORD _size, SharedIB& _buffer) { return AcquireSpace_(_size, _buffer); }

  SpaceHolder()
    : position(UINT_MAX)
    , size(0)
    , timeStamp(0)
  {
  }

  bool IsValid(const SharedVB& _buffer, const NDBID& _id = *(NDBID*)0) const { return IsValid_(_buffer, _id); }
  bool IsValid(const SharedIB& _buffer, const NDBID& _id = *(NDBID*)0) const { return IsValid_(_buffer, _id); }

  UINT GetSize() const { return size; }
  UINT GetPosition() const { return position; }
};

template<class T>
bool SpaceHolder::AcquireSpace_(DWORD _size, T& _buffer)
{
  if (UINT_MAX == (position = _buffer.AcquireSpace(_size)))
    return false;

  timeStamp = _buffer.GetTimeStamp();
  size = _size;
  return true;
}

template<class T>
bool SpaceHolder::IsValid_(const T& _buffer, const NDBID& _id) const
{
  (void)_id;
  return timeStamp == _buffer.GetTimeStamp() && UINT_MAX > position;
}

namespace Shims
{

  template<typename T>
  struct PointedType
  {
  };

  template<typename T, class REF_POLICY>
  struct PointedType< IntrusivePtr<T, REF_POLICY> >
  {
    typedef typename IntrusivePtr<T, REF_POLICY>::Element type;
    typedef type* pointer;
  };

  template<typename T>
  struct PointedType<T*>
  {
    typedef T type;
    typedef type* pointer;
  };

  template<typename T>
  struct PointedType< DXPtr<T> >
  {
    typedef T type;
    typedef type* pointer;
  };

  template<class T>
  void Delete(T &p) { p = 0; }

  template<class T>
  void Delete(T* &p) { delete p; p = 0; }

  template<typename T, class REF_POLICY>
  void Attach(IntrusivePtr<T, REF_POLICY> &p, T* val ) { p.Attach(val); }

  template<typename T>
  void Attach(DXPtr<T> &p, T* val) { p.Attach(val); }

  template<typename T>
  void Attach(T* &p, T* val)
  {
    delete p;
    p = val;
  }

  template<typename T, class REF_POLICY>
  T* Detach(IntrusivePtr<T, REF_POLICY> &p) { return p.Detach(); }

  template<typename T>
  T* Detach(DXPtr<T> &p) { return p.Detach(); }

  template<typename T>
  T* Detach(T* val) { return val; }

}

class RefCountST
{
  long rc;

public:
  RefCountST()
    : rc(0)
  {
  }

  long AddRef()  { return ++rc; }
  long Release() { return --rc; }
};

class RefCountMT
{
  long rc;

public:
  RefCountMT()
    : rc(0)
  {
  }

  long AddRef()  { return ++rc; }
  long Release() { return --rc; }
};

template<class T, class RC = RefCountST>
class SharedBuffer
{
  T p;
  size_t size;
  RC refCount;

public:
  SharedBuffer()
    : p()
    , size(0)
    , refCount()
  {
  }

  long AddRef()
  {
    if (p)
      return refCount.AddRef();
    return INT_MIN;
  }

  long Release()
  {
    if (!p)
      return INT_MIN;
    const long rc = refCount.Release();
    if (!rc)
      Shims::Delete(p);

    return rc;
  }

  typedef T SharedBuffer::*unspecified_bool_type;

  operator unspecified_bool_type () const
  {
    return p ? &SharedBuffer::p : 0;
  }

  typename Shims::PointedType<T>::type& operator*() const { return *p; }
  typename Shims::PointedType<T>::type* operator->() const { return &*p; }

  size_t GetSize() const { return size; }
  const T& GetPtr() const { return p; }

  void SetPtr(const T& _src, size_t _size)
  {
    if (p)
      Shims::Delete(p);
    p = _src;
    size = _size;
  }
};

template<class Initializer>
struct ManagedResource : public Initializer, public DeviceDeleteHandler
{
  typedef typename Initializer::Type Type;
  typedef typename Shims::PointedType<Type>::pointer Pointer;

  ManagedResource()
    : p()
  {
  }

  ManagedResource(const Initializer& _initializer)
    : Initializer(_initializer)
    , p()
  {
  }

  ~ManagedResource()
  {
    Initializer::Delete(p);
  }

  virtual void OnDeviceDelete()
  {
    Initializer::Delete(p);
  }

  Pointer Get()
  {
    if (p)
      return ::Get(p);

    Initializer::Init(p);
    return ::Get(p);
  }

private:
  Type p;
};

template<class Initializer>
struct DefPoolResource : public Initializer, public DeviceLostHandler
{
  typedef typename Initializer::Type Type;
  typedef typename Shims::PointedType<Type>::pointer Pointer;

  DefPoolResource()
    : p()
  {
  }

  DefPoolResource(const Initializer& _initializer)
    : Initializer(_initializer)
    , p()
  {
  }

  ~DefPoolResource()
  {
    Initializer::Delete(p);
  }

  virtual void OnDeviceLost()
  {
    Initializer::Delete(p);
  }

  virtual void OnDeviceReset()
  {
  }

  Pointer Get()
  {
    if (p)
      return ::Get(p);

    Initializer::Init(p);
    return ::Get(p);
  }

private:
  Type p;
};

struct IntrusivePtrDeleter
{
  template<typename T, class REF_POLICY>
  static void Delete(IntrusivePtr<T, REF_POLICY> &p)
  {
    p = 0;
  }

  template<typename T>
  static void Delete(DXPtr<T>& p)
  {
    p = 0;
  }

  template<typename T>
  static void Delete(T*& p)
  {
    p = 0;
  }
};

template<class T>
struct SimpleInitializer
{
  typedef T* Type;

  static void Init(T*& _p)
  {
    _p = new T();
  }

  static void Delete(T*& _p)
  {
    delete _p;
    _p = 0;
  }
};

template<class T>
struct MaterialInit
{
  typedef T* Type;

  static void Init(T*& _p)
  {
    _p = static_cast<T*>( CreateRenderMaterial( T::typeId ) );
  }

  static void Delete(T*& _p)
  {
    delete _p;
    _p = 0;
  }
};

} // namespace Render

typedef Render::DXBaseTextureRef DXBaseTextureRef;
typedef Render::DXTextureRef DXTextureRef;
typedef Render::DXCubeTextureRef DXCubeTextureRef;
typedef Render::DXSurfaceRef DXSurfaceRef;
typedef Render::DXSurfacePtr DXSurfacePtr;
typedef Render::DXVertexBufferRef DXVertexBufferRef;
typedef Render::DXIndexBufferRef DXIndexBufferRef;
typedef Render::DXVertexDeclarationRef DXVertexDeclarationRef;
typedef Render::DXVertexShaderRef DXVertexShaderRef;
typedef Render::DXPixelShaderRef DXPixelShaderRef;

enum
{
  D3D_OK = 0
};

#else

#include "Vendor/DirectX/Include/d3d9.h"
#include "Vendor/DirectX/Include/d3dx9.h"

#include "DxIntrusivePtr.h"
#include "DeviceLost.h"

template<typename T>
T* Get(T* &p)
{
  return p;
}

class DXVertexBufferDynamicRef : public Render::DeviceLostHandler
{
  DXVertexBufferRef pDXBuffer;
  UINT size;

protected:
  DXVertexBufferDynamicRef() : size(0) {}
  explicit DXVertexBufferDynamicRef(UINT size_) : size(size_) {}

public:
  typedef IDirect3DVertexBuffer9 Iface;
  typedef Render::DeviceLostWrapper<DXVertexBufferDynamicRef> Wrapped;

  bool Resize(int size, bool nullOnLostDevice = false);
  UINT GetSize() const { return size; }

  void Reset() { pDXBuffer.Attach(0); size = 0; }

  IDirect3DVertexBuffer9* operator->() const { return Get(pDXBuffer); }
  IDirect3DVertexBuffer9* __gEt__() const { return Get(pDXBuffer); }

  // Handle lost device
  virtual void OnDeviceLost() 
	{ 
		NI_ASSERT( size > 0 || !pDXBuffer, "Invalid dynamic VB size"); 
		pDXBuffer = 0; 
	}
  virtual void OnDeviceReset();
};

inline IDirect3DVertexBuffer9* Get(DXVertexBufferDynamicRef const& sp)
{
  return sp.__gEt__();
}

template<UINT elemSize>
class DXIndexBufferDynamicRef_ : public Render::DeviceLostHandler
{
  DXIndexBufferRef pDXBuffer;
  UINT size;

protected:
  DXIndexBufferDynamicRef_() : size(0) {}
  explicit DXIndexBufferDynamicRef_(UINT size_) : size(size_) {}
  DXIndexBufferDynamicRef_(IDirect3DIndexBuffer9* pBuffer, UINT size_) : pDXBuffer(pBuffer, false), size(size_) {}

public:
  typedef IDirect3DIndexBuffer9 Iface;
  typedef Render::DeviceLostWrapper<DXIndexBufferDynamicRef_> Wrapped;

  bool Resize(int _size);
  UINT GetSize() const { return size; }

  void Reset() { pDXBuffer.Attach(0); size = 0; }

  IDirect3DIndexBuffer9* operator->() const { return Get(pDXBuffer); }
  IDirect3DIndexBuffer9* __gEt__() const { return Get(pDXBuffer); }

  // Handle lost device
  virtual void OnDeviceLost()
	{ 
		NI_ASSERT( size > 0 || !pDXBuffer, "Invalid dynamic IB size"); 
		pDXBuffer = 0; 
	}

  virtual void OnDeviceReset();
};

typedef DXIndexBufferDynamicRef_<32> DXIndexBufferDynamicRef;
typedef DXIndexBufferDynamicRef_<16> DXIndexBufferDynamicRef16;

template<UINT elemSize>
inline IDirect3DIndexBuffer9* Get(DXIndexBufferDynamicRef_<elemSize> const& sp)
{
  return sp.__gEt__();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Render
{

void SetErrorMessage(HRESULT hr, const wstring &msg); // Set localized messages for DXERR_*** cracker.
void ShowErrorMessageAndTerminate(HRESULT hr);     // Cracker for DXERR_*** codes. Terminates app.

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Single-threaded shared D3D buffer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef _SHIPPING
//#define CHECK_FOR_ST
#endif // _SHIPPING

template<class _DynBuffer>
class SharedD3DBufferST : protected _DynBuffer
{
  seDECLARE_RING_T(SharedD3DBufferST, m_part, Ring);

  static Ring& GetRing();

  void* ptr;
  DWORD position;
  DWORD positionAtLock;
  DWORD sizeRequested;
  DWORD timeStamp;
  const DWORD threadID;
  float minSizeScale;
  bool  isLocked;

  enum LOCK_FLAGS
  {
    LOCKFLAGS_FLUSH  = D3DLOCK_DISCARD,
    LOCKFLAGS_APPEND = D3DLOCK_NOOVERWRITE
  };

  void Init() // Call this ONCE in constructor
  {
    ptr = 0; position = 0; timeStamp = 0;
    isLocked = false;
    GetRing().addLast(this);
  }

  bool IsSameThread() const { return ::GetCurrentThreadId() == threadID; }
#ifdef CHECK_FOR_ST
  void CheckForST() const { NI_ASSERT(IsSameThread(), "SharedD3DBufferST should be used from single thread only!"); }
#else
  #define CheckForST()
#endif

  INTERMODULE_EXPORT void QuerySize(UINT _size);
  INTERMODULE_EXPORT bool Resize(int _size); // Returns true on success. If false, size may be null or not changed.

protected :
  SharedD3DBufferST() : minSizeScale(), threadID( ::GetCurrentThreadId() ) { Init(); }

  explicit SharedD3DBufferST(DWORD _size, float _minSizeScale = 0.0f)
    : _DynBuffer(_size), minSizeScale(_minSizeScale), threadID( ::GetCurrentThreadId() )
  { Init(); }

  ~SharedD3DBufferST()
  {
    Clear();
    Ring::remove(this);
  }

public :
  typedef typename _DynBuffer::Iface Iface;

  void Clear() { CheckForST(); Reset(); _DynBuffer::Reset(); }

  virtual void OnDeviceLost()  { CheckForST(); Reset(); _DynBuffer::OnDeviceLost(); }
  virtual void OnDeviceReset() { CheckForST(); _DynBuffer::OnDeviceReset(); }

  UINT GetSize()  const  { CheckForST(); return _DynBuffer::GetSize(); }
  UINT GetTimeStamp() const { CheckForST(); return timeStamp; }

  void* GetPointer(UINT _pos) const
  {
    CheckForST();
    NI_ASSERT(isLocked && _pos >= positionAtLock, "SharedD3DBufferST::GetPointer() - wrong position");
    return PBYTE(ptr) + _pos - positionAtLock;
  }
  Iface* GetBuffer() const { CheckForST(); return Get(*this); }

  bool IsLocked() { CheckForST(); return isLocked; }

  UINT AcquireSpace(DWORD _size)
  {
    CheckForST();
    NI_VERIFY(isLocked, NStr::StrFmt("SharedD3DBufferST::AcquireSpace called while buffer is unlocked"), return UINT_MAX);

    sizeRequested += _size;
    // Ensure there is enough space for this data
    if( position + _size > GetSize() )
      return UINT_MAX;

    UINT pos = position;
    position += _size;
    return pos;
  }

  INTERMODULE_EXPORT bool Lock(); // returns false if Lock didn't succeed
  void Unlock()
  {
    CheckForST();
    if(isLocked) {
      (*this)->Unlock();
      isLocked = false;
    }
  }

  static void UnlockAll()
  {
    for(ring::Range<Ring> it( GetRing() ); it; ++it)
      it->Unlock();
  }

  void Reset()
  {
    ptr = 0;
    position = 0;
    Unlock();
  }

  static void ResetAll()
  {
    for(ring::Range<Ring> it( GetRing() ); it; ++it)
      it->Reset();
  }
};  // class SharedD3DBufferST

// DeviceLostWrapper MUST be terminal class! Don't inherit from SharedVB, only from SharedD3DBufferST!
typedef DeviceLostWrapper< SharedD3DBufferST<DXVertexBufferDynamicRef> > SharedVB;
typedef DeviceLostWrapper< SharedD3DBufferST<DXIndexBufferDynamicRef> >  SharedIB;


//==== SharedD3DBufferST helper - holds info about acquired buffer space ====================================

class SpaceHolder
{
  UINT position;
  UINT size;
  UINT timeStamp; // there is no need to initialize this

  typedef class NDb::DBID NDBID;

  template<class T> bool AcquireSpace_(DWORD _size, T& _buffer);
  template<class T> bool IsValid_(const T& _buffer, const NDBID& _id) const;

public:
  bool AcquireSpace(DWORD _size, SharedVB& _buffer) { return AcquireSpace_(_size, _buffer); }
  bool AcquireSpace(DWORD _size, SharedIB& _buffer) { return AcquireSpace_(_size, _buffer); }

  SpaceHolder() : position(UINT_MAX), size() {}

  bool IsValid(const SharedVB& _buffer, const NDBID& _id = *(NDBID*)0) const { return IsValid_(_buffer, _id); }
  bool IsValid(const SharedIB& _buffer, const NDBID& _id = *(NDBID*)0) const { return IsValid_(_buffer, _id); }

  UINT GetSize() const { return size; }
  UINT GetPosition() const { return position; }

};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum PoolType
{
	RENDER_POOL_DEFAULT,
	RENDER_POOL_MANAGED,
	RENDER_POOL_DYNAMIC,
	RENDER_POOL_TEX_DYNAMIC,
	RENDER_POOL_SYSMEM,
  RENDER_POOL_SYSMEM_DYNAMIC,
};

void GetD3DPoolAndUsagesParamaters(DWORD& usage, D3DPOOL& pool, PoolType poolType);

DXVertexBufferRef CreateVB(int size, PoolType type, void const *pData = 0);
void* LockVB(IDirect3DVertexBuffer9 *pBuff, unsigned int flags, int size = 0);
void FillVB(IDirect3DVertexBuffer9 *pBuff, int size, void const *pData, unsigned int lockFlags = 0);

template <typename T>
T* LockVB(IDirect3DVertexBuffer9 *pBuff, unsigned int flags, int size = 0)
{
	return reinterpret_cast<T*>( LockVB(pBuff, flags, size) );
}

DXIndexBufferRef CreateIB(int size, PoolType poolType, UINT const *pData = 0);
DXIndexBufferRef CreateIB16(int size, PoolType poolType, WORD const *pData = 0);
unsigned int* LockIB(IDirect3DIndexBuffer9 *pBuff, unsigned int flags, int size = 0);
void FillIB(IDirect3DIndexBuffer9 *pBuff, int size, void const *pData, unsigned int lockFlags);

int D3DFormatNumBits(D3DFORMAT format);

namespace Shims
{

  template<typename T>
  struct PointedType
  {
  };

  template<typename T, class REF_POLICY>
  struct PointedType< IntrusivePtr<T, REF_POLICY> >
  {
    typedef typename IntrusivePtr<T, REF_POLICY>::Element type;
    typedef type* pointer;
  };

  template<typename T>
  struct PointedType<T*>
  {
    typedef T type;
    typedef type* pointer;
  };

//////////////////////////////////////////////////////////////////////////
  template<class T>
  void Delete(T &p) { p = 0; }

  template<class T>
  void Delete(T* &p) { delete p; p = 0; }

//////////////////////////////////////////////////////////////////////////
  template<typename T, class REF_POLICY>
  void Attach(IntrusivePtr<T, REF_POLICY> &p, T* val ) { p.Attach(val); }

  template<typename T>
  void Attach(T* &p, T* val)
  {
    delete p;
    p = val;
  }

//////////////////////////////////////////////////////////////////////////
  template<typename T, class REF_POLICY>
  T* Detach(IntrusivePtr<T, REF_POLICY> &p) { return p.Detach(); }

  template<typename T>
  T* Detach(T* val) { return val; }

};

class RefCountST
{
  long rc;

public:
  RefCountST() : rc() {}
  long AddRef()  { return ++rc; }
  long Release() { return --rc; }
};

class RefCountMT
{
  volatile long rc;

public:
  RefCountMT() : rc() {}
  long AddRef()  { return InterlockedIncrement(&rc); }
  long Release() { return InterlockedDecrement(&rc); }
};


template<class T, class RC = RefCountST>
class SharedBuffer
{
  T p;
  size_t size;
  RC refCount;

public:
  SharedBuffer() : p(), size(), refCount()  {}

  long AddRef()
  {
    if(p)
      return refCount.AddRef();
    else
      return INT_MIN;
  }
  long Release()
  {
    if(!p)
      return INT_MIN;
    const long rc = refCount.Release();
    if(!rc)
      Shims::Delete(p);

    return rc;
  }

  typedef T SharedBuffer::*unspecified_bool_type;

  operator unspecified_bool_type () const
  {
    return p ? &SharedBuffer::p : 0;
  }

  typename Shims::PointedType<T>::type& operator*()  const { return *p; }
  typename Shims::PointedType<T>::type* operator->() const { return &*p; }

  size_t  GetSize() const { return size; }
  const T& GetPtr() const { return p; }
  void     SetPtr(const T& _src, size_t _size)
  {
    if(p)
      Shims::Delete(p);
    p = _src;
    size = _size;
  }
};

// Single-threaded holder for resources listening for OnDeviceDelete
// Initializer class must provide Delete(Initializer::Type &p) and Init(Initializer::Type &p)
template<class Initializer>
struct ManagedResource : public Initializer, public DeviceDeleteHandler
{
  typedef typename Initializer::Type Type;
  typedef typename Shims::PointedType<Type>::pointer Pointer;

  ManagedResource() : p() {}
  ManagedResource(const Initializer& _initializer) : Initializer(_initializer), p() {}
  ~ManagedResource() { Initializer::Delete(p); }

  virtual void OnDeviceDelete()
  {
    Initializer::Delete(p);
  }

  Pointer Get()
  {
    if(p) // static branch prediction assumes that forward-pointing branches will not be taken
      return ::Get(p);

    Initializer::Init(p);

    return ::Get(p);
  }

private:
  Type p;
};

// holder for resources listening for OnDeviceLost
// Initializer class must provide Delete(Initializer::Type &p) and Init(Initializer::Type &p)
template<class Initializer>
struct DefPoolResource : public Initializer, public DeviceLostHandler
{
  typedef typename Initializer::Type Type;
  typedef typename Shims::PointedType<Type>::pointer Pointer;

  DefPoolResource() : p() {}
  DefPoolResource(const Initializer& _initializer) : Initializer(_initializer), p() {}
  ~DefPoolResource() { Initializer::Delete(p); }

  virtual void OnDeviceLost()
  {
    Initializer::Delete(p);
  }

  Pointer Get()
  {
    if(p) // static branch prediction assumes that forward-pointing branches will not be taken
      return ::Get(p);

    Initializer::Init(p);

    return ::Get(p);
  }

private:
  Type p;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
struct IntrusivePtrDeleter
{
  template<typename T, class REF_POLICY>
  static void Delete(IntrusivePtr<T, REF_POLICY> &p)
  {
    p = 0;
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
template<class T>
struct SimpleInitializer
{
  typedef T* Type;

  static void Init(T*& _p)
  {
    _p = new T();
  }

  static void Delete(T*& _p)
  {
    delete _p;
    _p = 0;
  }
}; // struct Initializer

///////////////////////////////////////////////////////////////////////////////////////////////////
template<class T>
struct MaterialInit
{
  typedef T* Type;

  static void Init(T*& _p)
  {
    _p = static_cast<T*>( CreateRenderMaterial( T::typeId ) );
  }

  static void Delete(T*& _p)
  {
    delete _p;
    _p = 0;
  }
};

}

#endif
