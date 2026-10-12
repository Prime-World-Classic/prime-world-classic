#pragma once

namespace Render
{
  typedef pair<string, string> StringPair;

// Effect-side Config Manager interface
struct NOVTABLE IConfigManager
{
#include "Features.h"

  typedef vector<StringPair> ConfigParams;

  struct TechniqueDesc
  {
    DWORD flags;
  };

  enum ResourceType
  {
    Total = 0,
    Shaders,
    Textures,
    Geometry,
    NUM_RESOURCE_TYPES
  };

  struct SysInfo
  {
    DWORD MaxVertexShaderConst;
    bool  IsNullRef;
  };

  virtual const SysInfo& GetSysInfo() const = 0;
  virtual UINT GetPerfomanceLevel(ResourceType _type) const = 0;
  virtual UINT FindAcceptableTechnique(const TechniqueDesc **_pDesc, UINT numDesc) const = 0;
  virtual bool CheckFeatureSupport(DWORD _features) const = 0;      // return true if any of requested features supported
  virtual bool CheckFeatureSupportExact(DWORD _features) const = 0; // return true if all of requested features supported
  virtual bool ReadableDepthEnabled() const = 0;
  virtual bool StencilSupported(ERenderFormat _depthFormat) const = 0;
  virtual ERenderFormat GetDepthFormatPreffered() const = 0;
  virtual const ConfigParams& GetConfigParams() const = 0;

  // Wrappers
  bool HasD16T() const { return CheckFeatureSupport(hasD16T); } // Depth/Stencil textures (hardware shadow maps) - D3DFMT_D16
  bool HasD24X() const { return CheckFeatureSupport(hasD24X); } // Depth/Stencil textures (hardware shadow maps) - D3DFMT_D24X8
  bool HasD24S() const { return CheckFeatureSupport(hasD24S); } // Depth/Stencil textures (hardware shadow maps) - D3DFMT_D24S8

  // NV/ATI Depth/Stencil textures (hardware shadow maps)
  bool HasDSM() const { return CheckFeatureSupport(hasD16T|hasD24X|hasD24S); }

protected:
  IConfigManager(void)  {}
  ~IConfigManager(void) {}
};

const IConfigManager* GetIConfigManager();

}; // namespace Render