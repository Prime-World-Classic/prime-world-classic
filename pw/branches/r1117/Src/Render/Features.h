// Аппаратные фичи, влияющие на выбор путей рендера.
//
// Из набора удалены vendor-специфичные расширения D3D9-эпохи:
//   hasDF16/hasDF24/hasRAWZ/hasINTZ/hasRESZ — форматы глубины ATI/NV,
//     читаемые как текстура. Под Vulkan/DX12 ту же роль играет depth SRV
//     (D24S8/D32F), отдельного флага не требуется.
//   hasR2VB / hasVT16 / hasVT32 — Render-to-VB и vertex textures.
//     Заменяются structured/typed buffer'ами.
//   hasNLRT — null render target. Заменяется color-write маской.
// hasD16T/hasD24X/hasD24S оставлены как единый гейт «аппаратные карты
// теней читаются как depth-текстура» (см. HasDSM()).
enum Features {
  hasD16T = 1,            // Depth/Stencil textures (hardware shadow maps) - D3DFMT_D16
  hasD24X = hasD16T << 1, // Depth/Stencil textures (hardware shadow maps) - D3DFMT_D24X8
  hasD24S = hasD24X << 1, // Depth/Stencil textures (hardware shadow maps) - D3DFMT_D24S8
};

// Меняется при несовместимом изменении набора флагов (в отчёты уходит
// только маска caps, см. renderer.cpp).
static const int featuresVersion = 2;
