// dsm_probe.cpp — проверка, потянет ли адаптер аппаратные карты теней (depth-текстуры).
//
// Зачем: на E2E-стенде (tiny10, Microsoft Basic Render Driver = D3D9 WARP)
// маска caps в ConfigManager выходит 0x0 — HasD16T/HasD24X/HasDSM ложны, и
// путь аппаратных карт теней стендом вообще не исполняется (PLAN_NVRHI §2.5,
// §2.15 п.9). Прежде чем форсировать флаги dev-переменной, надо знать, что
// устройство СПОСОБНО создать depth-текстуру: иначе форс превратит «путь не
// исполняется» в «NI_DX_THROW на создании ресурса».
//
// Проверка идёт на том же железе/драйвере, что и клиент: D3D9Ex не используется,
// тракт такой же, как у ConfigManager::CheckFormats (CheckDeviceFormat c
// D3DUSAGE_DEPTHSTENCIL + D3DRTYPE_TEXTURE) плюс реальная попытка создания.
//
// Сборка на tiny10:
//   cl /W4 /EHsc /Fe:C:\pwbuild\dsm_probe.exe dsm_probe.cpp /link d3d9.lib
// Запуск: dsm_probe.exe [w]  (w = HAL, w = REF/WARP; см. вывод)

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#pragma comment(lib, "d3d9.lib")

static const char* FmtName(D3DFORMAT f) {
  switch (f) {
    case D3DFMT_D16:   return "D16";
    case D3DFMT_D24X8: return "D24X8";
    case D3DFMT_D24S8: return "D24S8";
    case D3DFMT_D24X4S4: return "D24X4S4";
    case D3DFMT_D32:   return "D32";
    case D3DFMT_D16_LOCKABLE: return "D16_LOCKABLE";
    default: return "?";
  }
}

static void ProbeFormats(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat) {
  static const D3DFORMAT fmts[] = { D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D24X4S4, D3DFMT_D32, D3DFMT_D16_LOCKABLE };
  for (unsigned i = 0; i < sizeof(fmts)/sizeof(fmts[0]); ++i) {
    const HRESULT okTex  = d3d->CheckDeviceFormat(adapter, type, adapterFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,  fmts[i]);
    const HRESULT okSurf = d3d->CheckDeviceFormat(adapter, type, adapterFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,   fmts[i]);
    printf("  %-13s texture=%s surface=%s\n", FmtName(fmts[i]),
           okTex == D3D_OK ? "OK" : "no", okSurf == D3D_OK ? "OK" : "no");
  }
}

static void RunForType(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, const char* tag) {
  D3DDISPLAYMODE dm = {};
  const HRESULT hdm = d3d->GetAdapterDisplayMode(adapter, &dm);
  printf("[%s] adapter=%u displayMode.Format=0x%x (%s) hr=0x%08x\n", tag, adapter, dm.Format, FmtName(dm.Format), hdm);

  D3DCAPS9 caps = {};
  if (FAILED(d3d->GetDeviceCaps(adapter, type, &caps))) { printf("  GetDeviceCaps FAILED\n"); return; }
  printf("  MaxVertexShaderConst=%u MaxTextureBlendStages=%u MaxSimultaneousTextures=%u DevType=%d\n",
         caps.MaxVertexShaderConst, caps.MaxTextureBlendStages, caps.MaxSimultaneousTextures, (int)caps.DeviceType);

  ProbeFormats(d3d, adapter, type, dm.Format);

  // Реальная попытка создать то, что просит путь карт теней: depth-текстура.
  IDirect3DDevice9* dev = nullptr;
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = GetDesktopWindow();
  pp.BackBufferWidth = 64; pp.BackBufferHeight = 64;
  pp.BackBufferFormat = dm.Format ? dm.Format : D3DFMT_X8R8G8B8;
  // AutoDepthStencil не включаем: он может уронить CreateDevice на адаптере без
  // D24S8, а нам нужен сам CreateTexture(DEPTHSTENCIL) — он от автостенсила не
  // зависит.
  HRESULT hr = d3d->CreateDevice(adapter, type, pp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
  printf("  CreateDevice hr=0x%08x\n", hr);
  if (FAILED(hr) || !dev) return;

  static const D3DFORMAT texfmts[] = { D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16 };
  for (unsigned i = 0; i < sizeof(texfmts)/sizeof(texfmts[0]); ++i) {
    IDirect3DTexture9* tex = nullptr;
    const HRESULT h = dev->CreateTexture(512, 512, 1, D3DUSAGE_DEPTHSTENCIL, texfmts[i], D3DPOOL_DEFAULT, &tex, nullptr);
    printf("  CreateTexture(512x512, DEPTHSTENCIL, %s) hr=0x%08x%s\n", FmtName(texfmts[i]), h,
           (h == D3D_OK && tex) ? " -> LockRect/DSV probe" : "");
    if (h == D3D_OK && tex) {
      D3DSURFACE_DESC sd = {};
      tex->GetLevelDesc(0, &sd);
      printf("    desc: format=0x%x usage=0x%x w=%u h=%u\n", sd.Format, sd.Usage, sd.Width, sd.Height);
      D3DLOCKED_RECT lr = {};
      const HRESULT hl = tex->LockRect(0, &lr, nullptr, 0);
      printf("    LockRect hr=0x%08x pitch=%d\n", hl, lr.Pitch);
      if (hl == D3D_OK) tex->UnlockRect(0);
      // depth-stencil surface из текстуры (то, что нужно шейдеру карт теней)
      IDirect3DSurface9* surf = nullptr;
      const HRESULT hdsv = tex->GetSurfaceLevel(0, &surf);
      printf("    GetSurfaceLevel hr=0x%08x\n", hdsv);
      if (hdsv == D3D_OK && surf) surf->Release();
      tex->Release();
    }
  }

  // Заодно: читается ли depth-текстура как текстура (D3DSAMP / sampleable depth)
  {
    IDirect3DTexture9* tex = nullptr;
    const HRESULT h = dev->CreateTexture(512, 512, 1, D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC, D3DFMT_D24S8, D3DPOOL_DEFAULT, &tex, nullptr);
    printf("  CreateTexture(DEPTHSTENCIL|DYNAMIC, D24S8) hr=0x%08x\n", h);
    if (h == D3D_OK && tex) tex->Release();
  }

  dev->Release();
}

int main() {
  IDirect3D9* p = Direct3DCreate9(D3D_SDK_VERSION);
  if (!p) { printf("Direct3DCreate9 FAILED\n"); return 1; }
  const UINT adapters = p->GetAdapterCount();
  printf("adapters=%u\n", adapters);
  for (UINT a = 0; a < adapters; ++a) {
    D3DADAPTER_IDENTIFIER9 id = {};
    if (SUCCEEDED(p->GetAdapterIdentifier(a, 0, &id)))
      printf("adapter %u: \"%s\" vendor=0x%04x device=0x%04x driver=%u.%u\n", a, id.Description,
             id.VendorId, id.DeviceId,
             (unsigned)(id.DriverVersion.QuadPart >> 32), (unsigned)(id.DriverVersion.QuadPart & 0xffffffffULL));
  }
  for (UINT a = 0; a < adapters; ++a) {
    RunForType(p, a, D3DDEVTYPE_HAL,  "HAL");
    RunForType(p, a, D3DDEVTYPE_REF,  "REF");
  }
  p->Release();
  printf("DONE\n");
  return 0;
}
