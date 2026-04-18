#pragma once

#include "StaticMesh.h"
#if !defined(PW_LINUX_NULL_RENDER)
#include "MaterialSpec.h"
#endif

namespace Render
{

class InstancedMeshGeometry;
class InstancingMaterial;
#if defined(PW_LINUX_NULL_RENDER)
class BasicMaterial;
#endif

#if defined(NV_LINUX_PLATFORM)
  #define INSTANCED_MESH_ALIGN16 __attribute__((aligned(16)))
#else
  #define INSTANCED_MESH_ALIGN16 __declspec(align(16))
#endif

class INSTANCED_MESH_ALIGN16 InstancedMesh : public StaticMeshBase
{
	REPLACE_DEFAULT_NEW_DELETE(InstancedMesh);
#if defined(PW_LINUX_NULL_RENDER)
	BasicMaterial* pMaterial;
#else
	ScopedPtr<BasicMaterial>  pMaterial;
#endif
	InstancedMeshGeometry         *pGeometry;

public:	
	void Initialize( const NDb::DBStaticSceneComponent* pDBMeshResource );

	virtual void RenderToQueue( Render::BatchQueue &q );

  OcclusionQueries* GetQueries() const { return 0; }
	virtual void ForAllMaterials(Render::IMaterialProcessor &proc);

	virtual bool IsInstanced() const { return true; }
};

#undef INSTANCED_MESH_ALIGN16

}
