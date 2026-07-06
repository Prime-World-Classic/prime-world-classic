#include "stdafx.h"
#include "StaticMesh.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "batch.h"
#include "MaterialSpec.h"
#include "renderer.h"
#include "smartrenderer.h"

#include <algorithm>

namespace Render
{

DECLARE_INSTANCE_COUNTER(StaticMesh);

namespace
{

enum LinuxPreviewSamplerFillMode
{
  LINUX_PREVIEW_SAMPLER_METADATA,
  LINUX_PREVIEW_SAMPLER_TEXTURE
};

class NullStaticMeshMaterial : public BaseMaterial
{
public:
  NullStaticMeshMaterial()
    : BaseMaterial(NDb::MATERIALPRIORITY_MESHESOPAQUE, 0, -1)
  {
  }

  virtual void PrepareRenderer()
  {
  }
};

bool CanLoadPreviewDiffuseSamplerTexture(const NDb::Ptr<NDb::TextureBase>& texture)
{
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  return IsValid(texture);
#else
  return IsValid(texture) && dynamic_cast<const NDb::Texture*>(texture.GetPtr());
#endif
}

bool FillPreviewDiffuseSampler(
  const NDb::Sampler& dbSampler,
  Render::Sampler* sampler,
  bool canBeVisualDegrade,
  LinuxPreviewSamplerFillMode fillMode)
{
  if (!sampler || !IsValid(dbSampler.texture))
    return false;

  if (fillMode == LINUX_PREVIEW_SAMPLER_TEXTURE)
  {
    if (!CanLoadPreviewDiffuseSamplerTexture(dbSampler.texture))
      return false;

    Render::FillSampler(dbSampler, sampler, canBeVisualDegrade, 0);
    return sampler->GetTexture();
  }

  sampler->SetSamplerState(dbSampler.samplerState);
  return true;
}

bool FillPreviewDiffuseSampler(
  const NDb::SamplerEx& dbSampler,
  Render::Sampler* sampler,
  bool canBeVisualDegrade,
  LinuxPreviewSamplerFillMode fillMode)
{
  if (!sampler || !IsValid(dbSampler.texture))
    return false;

  if (fillMode == LINUX_PREVIEW_SAMPLER_TEXTURE)
  {
    if (!CanLoadPreviewDiffuseSamplerTexture(dbSampler.texture))
      return false;

    Render::FillSampler(dbSampler, sampler, canBeVisualDegrade, 0);
    return sampler->GetTexture();
  }

  sampler->SetSamplerState(dbSampler.samplerState);
  sampler->SetMultiplierAndAdd(dbSampler.Multiplier, dbSampler.Add);
  return true;
}

bool FillPreviewDiffuseSampler(
  const NDb::Material* dbMaterial,
  Render::Sampler* sampler,
  LinuxPreviewSamplerFillMode fillMode)
{
  if (!dbMaterial || !sampler)
    return false;

  switch (dbMaterial->GetObjectTypeID())
  {
    case NDb::BasicMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::BasicMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    case NDb::BasicMaskMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::BasicMaskMaterial*>(dbMaterial)->Diffuse,
        sampler,
        false,
        fillMode);

    case NDb::BasicFXMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::BasicFXMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    case NDb::DropMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::DropMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    case NDb::DecalMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::DecalMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    case NDb::DecalTerrainMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::DecalTerrainMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    case NDb::TerrainMaterial::typeId:
    {
      const NDb::TerrainMaterial* terrainMaterial =
        static_cast<const NDb::TerrainMaterial*>(dbMaterial);
      if (FillPreviewDiffuseSampler(terrainMaterial->N_DiffuseMap, sampler, true, fillMode))
        return true;
      if (FillPreviewDiffuseSampler(terrainMaterial->A_DiffuseMap, sampler, true, fillMode))
        return true;
      return FillPreviewDiffuseSampler(terrainMaterial->B_DiffuseMap, sampler, true, fillMode);
    }

    case NDb::ParticleFXMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::ParticleFXMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    case NDb::TestTownMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::TestTownMaterial*>(dbMaterial)->DiffuseMap,
        sampler,
        true,
        fillMode);

    default:
      return false;
  }
}

class LinuxStaticMeshPreviewMaterial : public NullStaticMeshMaterial
{
public:
  LinuxStaticMeshPreviewMaterial(const NDb::Material* dbMaterial)
    : NullStaticMeshMaterial()
    , diffuseSamplerReady(false)
    , diffuseTextureReady(false)
    , diffuseTextureAttempted(false)
  {
    if (!dbMaterial)
      return;

    pDbMaterial = dbMaterial;
    Render::Material::FillMaterial(dbMaterial, 0, false);
    diffuseSamplerReady =
      FillPreviewDiffuseSampler(dbMaterial, &diffuseMap, LINUX_PREVIEW_SAMPLER_METADATA);
  }

  virtual const NDb::Material* GetDBMaterial() const
  {
    return pDbMaterial.GetPtr();
  }

  virtual Render::Sampler* GetDiffuseMap()
  {
    return diffuseSamplerReady ? &diffuseMap : 0;
  }

  virtual const Render::Sampler* GetDiffuseMap() const
  {
    return diffuseSamplerReady ? &diffuseMap : 0;
  }

  virtual void PrepareRenderer()
  {
    if (diffuseSamplerReady && !diffuseTextureAttempted)
    {
      diffuseTextureAttempted = true;
      diffuseTextureReady =
        FillPreviewDiffuseSampler(pDbMaterial.GetPtr(), &diffuseMap, LINUX_PREVIEW_SAMPLER_TEXTURE);
    }

    if (diffuseTextureReady && diffuseMap.Enabled())
    {
      if (Render::GetStatesManager())
      {
        Render::BindSampler(0, diffuseMap);
      }
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
      else
      {
        Render::SmartRenderer::BindTexture(0, diffuseMap.GetTexture().GetPtr());
      }
#endif
    }
  }

private:
  NDb::Ptr<NDb::Material> pDbMaterial;
  Render::Sampler diffuseMap;
  bool diffuseSamplerReady;
  bool diffuseTextureReady;
  bool diffuseTextureAttempted;
};

void ClearStaticMeshMaterials(StaticArray<ScopedPtr<BaseMaterial>, 16>& materials, int* materialsCount)
{
  const int count = materialsCount ? *materialsCount : 0;
  for (int i = 0; i < count && i < static_cast<int>(materials.capacity()); ++i)
  {
    Reset(materials[i], 0);
  }
  if (materialsCount)
  {
    *materialsCount = 0;
  }
}

void InitializeStaticMeshNullMaterials(
  StaticArray<ScopedPtr<BaseMaterial>, 16>& materials,
  int* materialsCount,
  const MeshGeometry* meshGeometry,
  vector<Render::BaseMaterial*> sourceMaterials = vector<Render::BaseMaterial*>())
{
  ClearStaticMeshMaterials(materials, materialsCount);
  if (!materialsCount || !meshGeometry)
  {
    return;
  }

  const int primitiveCount = meshGeometry->primitives.size();
  const int materialCapacity = static_cast<int>(materials.capacity());
  *materialsCount = std::min(primitiveCount, materialCapacity);
  for (int i = 0; i < *materialsCount; ++i)
  {
    BaseMaterial* material =
      i < sourceMaterials.size() && sourceMaterials[i] ?
      sourceMaterials[i] :
      new NullStaticMeshMaterial();
    Reset(materials[i], material);
  }
}

} // namespace

BaseMaterial* CreateLinuxStaticMeshPreviewMaterial(const NDb::Material* dbMaterial)
{
  return dbMaterial ? new LinuxStaticMeshPreviewMaterial(dbMaterial) : 0;
}

StaticMesh::StaticMesh()
  : pMeshGeom(0)
  , materialsCount(0)
  , lightsFlags(0)
{
  Identity(&worldMatrix);
}

StaticMesh::~StaticMesh()
{
  ClearStaticMeshMaterials(materials, &materialsCount);
}

void StaticMesh::Initialize(const Matrix43& _worldMatrix, const NDb::StaticMesh* pDBMeshResource)
{
  worldMatrix = _worldMatrix;
  ClearStaticMeshMaterials(materials, &materialsCount);
  pMeshGeom = 0;
  if (pDBMeshResource)
  {
    localAABB.Set(pDBMeshResource->aabb);
    pMeshGeom = Render::RenderResourceManager::LoadStaticMeshGeometry(pDBMeshResource->geometryFileName, false);
    InitializeStaticMeshNullMaterials(materials, &materialsCount, pMeshGeom);
  }
}

bool StaticMesh::Initialize(const Matrix43& _worldMatrix, const NDb::DBStaticSceneComponent* pDBMeshResource, bool appendColorStream)
{
  worldMatrix = _worldMatrix;
  ClearStaticMeshMaterials(materials, &materialsCount);
  pMeshGeom = 0;
  if (pDBMeshResource)
  {
    localAABB.Set(pDBMeshResource->aabb);
    pMeshGeom = Render::RenderResourceManager::LoadStaticMeshGeometry(pDBMeshResource->geometryFileName, appendColorStream);
    InitializeStaticMeshNullMaterials(materials, &materialsCount, pMeshGeom);
  }
  return pMeshGeom != 0 && materialsCount > 0;
}

void StaticMesh::Initialize(const Matrix43& _worldMatrix, const MeshGeometry* geom, vector<Render::BaseMaterial*> materials)
{
  worldMatrix = _worldMatrix;
  ClearStaticMeshMaterials(this->materials, &materialsCount);
  pMeshGeom = geom;
  localAABB.center = CVec3(0.0f, 0.0f, 0.0f);
  localAABB.halfSize = CVec3(1.0f, 1.0f, 1.0f);
  InitializeStaticMeshNullMaterials(this->materials, &materialsCount, pMeshGeom, materials);
}

void StaticMesh::PrepareRendererAfterMaterial(unsigned int elementNumber) const
{
  (void)elementNumber;
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  SmartRenderer::SetOpenGLImmediateObjectMatrix(&worldMatrix);
  SmartRenderer::SetOpenGLImmediateSkeletalMatrices(0, 0);
#endif
}

void StaticMeshBase::SetWorldMatrix(const Matrix43& transform)
{
  worldMatrix = transform;
}

void StaticMeshBase::SetWorldMatrix(const SHMatrix& transform)
{
  worldMatrix.Set(transform);
}

void StaticMesh::RenderToQueue(BatchQueue& queue)
{
  if (!pMeshGeom || materialsCount <= 0)
  {
    return;
  }

  const int primitiveCount = std::min(pMeshGeom->primitives.size(), materialsCount);
  for (int i = 0; i < primitiveCount; ++i)
  {
    BaseMaterial* const pMaterial = Get(materials[i]);
    Primitive const* const pPrimitive = pMeshGeom->primitives[i];
    if (!pMaterial || !pPrimitive)
    {
      continue;
    }

    queue.Push(pMaterial->GetPriority(), this, pPrimitive, i, pMaterial);
  }
}

OcclusionQueries* StaticMesh::GetQueries() const
{
  return 0;
}

bool StaticMesh::FillOBB(CVec3 (&_vertices)[8]) const
{
  if (localAABB.IsEmpty())
  {
    return false;
  }

  RenderComponent::FillOBB(localAABB, worldMatrix, &_vertices[0]);
  return true;
}

void StaticMesh::SetQueryTriBound(UINT _bound)
{
  (void)_bound;
}

void StaticMesh::SetMaterial(int nElementIdx, BaseMaterial* _pMaterial)
{
  if (nElementIdx < 0 || nElementIdx >= materialsCount)
  {
    delete _pMaterial;
    return;
  }

  Reset(materials[nElementIdx], _pMaterial);
}

void StaticMesh::ForAllMaterials(Render::IMaterialProcessor& proc)
{
  for (int i = 0; i < materialsCount; ++i)
  {
    if (materials[i])
    {
      proc(*materials[i]);
    }
  }
}

void StaticMesh::SetVertexColors(AutoPtr<MeshVertexColors> pColors, bool fake)
{
  (void)pColors;
  (void)fake;
}

void StaticMesh::CalculateLighting(SceneConstants const& sceneConst)
{
  (void)sceneConst;
}

void StaticMesh::CalculateLightingEx(SceneConstants const& sceneConst, NDb::ELightEnvironment const selector)
{
  (void)sceneConst;
  (void)selector;
}

void StaticMesh::AddGeometryCRC(Crc32Checksum& crc)
{
  (void)crc;
}

} // namespace Render

#else

#include "light.h"
#include "batch.h"
#include "ConvexVolume.h"
#include "GlobalMasks.h"
#include "NullRenderSignal.h"
#include "SHCoeffs.h"
#include "sceneconstants.h"

static NDebug::DebugVar<int> render_SM_Render( "Stat_Render", "PerfCnt", true );

namespace Render
{

namespace
{

DECLARE_NULL_RENDER_FLAG

inline bool VertexHasColors(DXVertexDeclarationRef decl)
{
  if(RENDER_DISABLED)
    return false;

  D3DVERTEXELEMENT9  elements[MAXD3DDECLLENGTH];
  unsigned int n;
  decl->GetDeclaration(elements, &n);

  for (unsigned int i = 0; i < n; ++i)
  {
    if (elements[i].Usage == D3DDECLUSAGE_COLOR)
    {
      return true;
    }
  }

  return false;
}

}

DECLARE_INSTANCE_COUNTER(StaticMesh);

static UINT s_QueryTriBound = 1000;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
StaticMesh::StaticMesh()
: pMeshGeom( 0 )
, materialsCount( 0 )
, lightsFlags( 0 )
{
	Identity(&worldMatrix); 
#ifdef _DEBUG
	triangleCount = 0; 
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
StaticMesh::~StaticMesh()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::CreateStubMesh()
{
	NDb::Ptr<NDb::DBStaticSceneComponent> pDefaultStaticMesh = NDb::Get<NDb::DBStaticSceneComponent>( NDb::DBID( "/Tech/Default/Default.STSC" ) );
  NI_ASSERT( !pMeshGeom, "" );
	pMeshGeom = Render::RenderResourceManager::LoadStaticMeshGeometry( pDefaultStaticMesh->geometryFileName, false );

	Reset(materials[0], static_cast<BaseMaterial*>( Render::CreateRenderMaterial( pDefaultStaticMesh->materialsReferences[0] ) ) );
	materialsCount = 1;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::InitializeDynamicLighting()
{
  unsigned numMaterials = materials.capacity();
  NI_ASSERT(numMaterials <= 8 * sizeof(lightsFlags), "There's not enough bits to store all flags");
  
  lightsFlags = 0;
  for (unsigned i = 0; i < numMaterials; i++)
  {
    MaterialPtr& pMaterial = materials[i];
    if (pMaterial)
    {
      BasicMaterial* pBasicMaterial = dynamic_cast<BasicMaterial*>(Get(pMaterial));
      if (pBasicMaterial && pBasicMaterial->GetLightingPin() == NDb::LIGHTINGPIN_LIGHTINGDYNAMIC)
        lightsFlags |= (1 << i);
    }
  }

  if (lightsFlags != 0)
    lightsData.Initialize();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// used in simple StaticSceneComponent from ... [2/11/2010 smirnov]
void StaticMesh::Initialize( const Matrix43& _worldMatrix, const NDb::StaticMesh* pDBMeshResource )
{
	worldMatrix = _worldMatrix;

	localAABB.Set( pDBMeshResource->aabb );

  NI_ASSERT( !pMeshGeom, "" );
	pMeshGeom = Render::RenderResourceManager::LoadStaticMeshGeometry( pDBMeshResource->geometryFileName, false );
	if(pMeshGeom == 0)
	{
		CreateStubMesh();
		return;
	}

	// Allocate elements
	const int primitiveCount = pMeshGeom->primitives.size();

	materialsCount = pDBMeshResource->materialsReferences.size();
	NI_VERIFY( primitiveCount == materialsCount, "wrong number of materials", CreateStubMesh(); return; );
	for ( int i = 0; i < primitiveCount; ++i )
	{
		const NDb::Material* pMatType = pDBMeshResource->materialsReferences[i];
		NI_VERIFY(pMatType, "no material set for fragment #", continue; );
		BaseMaterial* pMatInstance = static_cast<BaseMaterial*>( Render::CreateRenderMaterial( pMatType ) );
		ASSERT(pMatInstance);
		pMatInstance->SetSkeletalMeshPin( NDb::BOOLEANPIN_NONE );
    
    pMatInstance->SetMultiplyVertexColorPin( VertexHasColors(pMeshGeom->primitives[i]->GetVertexDeclaration()) ?
                                             NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE );
    
    Reset(materials[i], pMatInstance);
	}
  InitializeDynamicLighting();
#ifdef _DEBUG
	triangleCount = pMeshGeom->triangleCount;
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// used in EaselPaintLayerSceneComponent [2/11/2010 smirnov]
void StaticMesh::Initialize( const Matrix43& _worldMatrix, const MeshGeometry* geom, vector<Render::BaseMaterial*> materials_ )
{
  NI_ASSERT( !pMeshGeom, "" );

	worldMatrix = _worldMatrix;
	pMeshGeom   = geom;

	localAABB.center   = CVec3( 0.0f, 0.0f, 0.0f );
	localAABB.halfSize = CVec3( 1.0f, 1.0f, 1.0f );

	// Allocate elements
	const int primitiveCount = pMeshGeom->primitives.size();

	materialsCount = materials_.size();
	NI_ASSERT( primitiveCount == materialsCount , "wrong number of materials" );
	for ( int i = 0; i < primitiveCount; ++i )
		Reset(materials[i], materials_[i]);
  InitializeDynamicLighting();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// used in normal StaticSceneComponent from ... [2/11/2010 smirnov]
// returns true if mesh was successfully loaded (otherwise stub mesh was created)
bool StaticMesh::Initialize( const Matrix43& _worldMatrix, const NDb::DBStaticSceneComponent* pDBMeshResource, bool appendColorStream )
{
	worldMatrix = _worldMatrix;

	localAABB.Set( pDBMeshResource->aabb );

  NI_ASSERT( !pMeshGeom, "" );
	pMeshGeom = Render::RenderResourceManager::LoadStaticMeshGeometry( pDBMeshResource->geometryFileName, appendColorStream );
	if (pMeshGeom == 0)
	{
		CreateStubMesh();
		return false;
	}

  NI_DATA_VERIFY( pMeshGeom->colorStreamAppended == appendColorStream, 
    NStr::StrFmt("Couldn't append vertex colors to '%s'. See '%s'", pDBMeshResource->geometryFileName, GetFormattedDbId(pDBMeshResource->GetDBID()).c_str()), 
    return false; );

	// Allocate elements
	const int primitiveCount = pMeshGeom->primitives.size();
	materialsCount = pDBMeshResource->materialsReferences.size();
  NI_DATA_VERIFY( primitiveCount == materialsCount,
    NStr::StrFmt( "Number of primitives (%d) and materials (%d) mismatch in '%s'", primitiveCount, materialsCount, NDb::GetFormattedDbId( pDBMeshResource->GetDBID() ).c_str() ),
    CreateStubMesh(); return false; );

  for ( int i = 0; i < materialsCount; ++i )
	{
    Reset(materials[i], 0);
		const NDb::Material* pMatType = pDBMeshResource->materialsReferences[i];
		NI_VERIFY(pMatType, NStr::StrFmt("No material in slot %i in '%s'", i, NDb::GetFormattedDbId(pDBMeshResource->GetDBID()).c_str()), continue);
    BaseMaterial* pMatInstance = static_cast<BaseMaterial*>( Render::CreateRenderMaterial( pMatType ) );
		NI_VERIFY(pMatInstance, NStr::StrFmt("Couldn't create material %i in '%s'", i, NDb::GetFormattedDbId(pDBMeshResource->GetDBID()).c_str()), continue);
		pMatInstance->SetSkeletalMeshPin( NDb::BOOLEANPIN_NONE ); // since assigned to static mesh
    pMatInstance->SetMultiplyVertexColorPin( VertexHasColors(pMeshGeom->primitives[i]->GetVertexDeclaration()) ? NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE );
		Reset(materials[i], pMatInstance);
	}
  InitializeDynamicLighting();
#ifdef _DEBUG
	triangleCount = pMeshGeom->triangleCount;
#endif

  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::SetVertexColors(AutoPtr<MeshVertexColors> pColors, bool fake)
{
  pVertexColors = pColors;
  if ( fake )
    pVertexColors = AutoPtr<MeshVertexColors>( new MeshVertexColors );
  ASSERT(pMeshGeom);
  pVertexColors->InitializeBuffer(*pMeshGeom, fake);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::SetMaterial( int nElementIdx, BaseMaterial* _pMaterial )
{
	NI_VERIFY( nElementIdx < (int)materialsCount && nElementIdx >= 0, "", return );
  Reset(materials[nElementIdx], _pMaterial);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::RenderToQueue( BatchQueue& queue )
{
  const int primitiveCount = pMeshGeom->primitives.size();
  NI_VERIFY( primitiveCount == materialsCount, "wrong number of materials", return );

  UINT numPointLights = 0;
  if (lightsFlags != 0)
  {
    AABB aabb;
    aabb.Transform(worldMatrix, localAABB);
    numPointLights = lightsData.Fill(aabb);
  }

  for(int i = 0, mask = 1; i < primitiveCount; ++i, mask <<= 1)
  {
    BaseMaterial* const pMaterial = Get(materials[i]);
    if( lightsFlags & mask )
      pMaterial->SetLightingPin( NDb::LightingPin(NDb::LIGHTINGPIN_LIGHTINGDYNAMIC + numPointLights) );

	  queue.Push( pMaterial->GetPriority(), this, pMeshGeom->primitives[i], i, pMaterial );
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
OcclusionQueries* StaticMesh::GetQueries() const
{
  return GetTriangleCount() > s_QueryTriBound ? &queries : 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool StaticMesh::FillOBB(CVec3 (&_vertices)[8]) const
{
  if(GetTriangleCount() <= s_QueryTriBound)
    return false;

  RenderComponent::FillOBB(localAABB, worldMatrix, &_vertices[0]);
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::SetQueryTriBound(UINT _bound) { s_QueryTriBound = _bound; }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::PrepareRendererAfterMaterial( unsigned int elementNumber ) const
{
	RenderComponent::PrepareRendererAfterMaterial( elementNumber );

	// Bind vertex colors
	if (pVertexColors)
		pVertexColors->Bind(elementNumber);

  if(GetRuntimePins().InstancingValue == NDb::BOOLEANPIN_NONE)
  {
    SHMatrix world;
    Copy( &world, worldMatrix );
    GetRenderer()->SetVertexShaderConstantsMatrix( WORLD, world );
  }

  // set lighting parameters
  if(GetRuntimePins().RenderModeValue == NDb::RENDERMODEPIN_RENDERNORMAL)
    if (lightsFlags & (1 << elementNumber))
      lightsData.Setup();

	render_SM_Render.AddValue(1);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMeshBase::SetWorldMatrix( const Matrix43& transform )
{
	worldMatrix = transform; 
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMeshBase::SetWorldMatrix( const SHMatrix& transform )
{
	worldMatrix.Set( transform ); 
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::ForAllMaterials(Render::IMaterialProcessor &proc)
{
  for (int i = 0; i < materialsCount; ++i)
    proc( *materials[i] );
}

namespace LightingCalculator
{
  struct Vertex
  {
    CVec3 position;
    CVec3 normal;
  };

  struct Context : public NonCopyable
  {
    const Render::SceneConstants& sceneConst;

    unsigned stride;
    unsigned numVertices;
    unsigned materialCount;

    void* geometry;
    Render::Color* colors;

    Render::AABB aabb;

    const Render::HDRColor* matDiffuseColor[16];
    float matLightIntensity[16];

    explicit Context(const Render::SceneConstants& _sceneConst)
      : sceneConst(_sceneConst)
      , stride(0U)
      , numVertices(0U)
      , materialCount(0U)
      , geometry(NULL)
      , colors(NULL)
      , aabb()
    {

    }
  private:
    Context();
  };

  void Calculate(const Context& ctx)
  {

  }
}

void StaticMesh::DoCalculateLighting(const SceneConstants& sceneConst, const DXVertexBufferRef& vb, const NDb::ELightEnvironment selector) const
{
  struct Vertex
  {
    CVec3 position;
    CVec3 normal;
  };

  Render::HDRColor const &ambientColor = sceneConst.ambientColor;

  Render::HDRColor defaultColor(1.f, 1.f, 1.f);
  Render::HDRColor const *matDiffuseColor[16];
  float matLightIntensity[16];

  // extract material light response
  {
    for (int i = 0; i < materialsCount; ++i)
    {
      Material *pMaterial = Get(materials[i]);

      const NDb::Material *pDBMat = pMaterial->GetDBMaterial();
      if (pDBMat->GetObjectTypeID() != NDb::BasicMaterial::typeId)
      {
        matDiffuseColor[i] = &defaultColor;
        matLightIntensity[i] = 1.f;
      }
      else
      {
        const NDb::BasicMaterial *pDBBaseMat = static_cast<const NDb::BasicMaterial*>(pDBMat);
        matDiffuseColor[i] = &pDBBaseMat->diffuseColor;
        matLightIntensity[i] = pDBBaseMat->lightingIntencity;
      }
    }
  }

  // collect lights affecting this object
  AABB aabb;
  aabb.Transform(worldMatrix, localAABB);

  Render::PointLightsCollector clf(selector);

  GetLightsManager()->ForLightsAffectingBBox( aabb, clf );

  NI_ASSERT(pMeshGeom->primitives[0], "No primitives");
  NI_ASSERT(Get(pMeshGeom->primitives[0]->GetVertexBuffer()), "No vertex buffer");

  // calculate total number of vertices
  unsigned int numVertices = 0;
  for (int i = 0; i < pMeshGeom->fragmentCount; ++i)
  {
    numVertices += pMeshGeom->primitives[i]->GetDipDescriptor().numVertices;
  }

  // calculate vertex stride
  D3DVERTEXBUFFER_DESC desc;
  pMeshGeom->primitives[0]->GetVertexBuffer()->GetDesc(&desc);
  unsigned int stride = desc.Size / numVertices;

  NI_ASSERT(stride * numVertices == desc.Size, "Invalid number of vertices");
  NI_ASSERT(stride >= sizeof(Vertex), "Vertex too small to hold required data");

  unsigned char *geovb = LockVB<unsigned char>(Get(pMeshGeom->primitives[0]->GetVertexBuffer()), 0);
  Render::Color *colors = LockVB<Render::Color>(Get(vb), 0);

  for (int i = 0; i < pMeshGeom->fragmentCount; ++i)
  {
    DipDescriptor &dip = pMeshGeom->primitives[i]->GetDipDescriptor();
    int matIdx = pMeshGeom->materialID[i];

    for (unsigned int j = 0; j < dip.numVertices; ++j)
    {
      int idx = dip.baseVertexIndex + dip.minIndex + j;
      Vertex *pV = (Vertex*)(geovb + idx * stride);

      CVec3 pos = Transform(pV->position, worldMatrix);
      CVec3 n = Rotate(pV->normal, worldMatrix);
      Normalize(&n);

      // calculate environment lighting
      HDRColor sceneEnvLight;
      EvaluateLightingBySHShaderConstants(sceneConst.envLighting, n, sceneEnvLight);

      // calculate lighting by point lights
      Render::HDRColor c = Render::HDRColor(0.f, 0.f, 0.f, 1.f);
      for (int j = 0; j < clf.lights.size(); ++j)
      {
        Render::PointLight const &pl = *clf.lights[j];

        CVec3 d = pl.GetLocation() - pos;
        float t = 1.f;

        float l = d.Length();
        if (l > 0.f)
        {
          t = n.Dot(d) * pl.GetAttenuation(l) / l;
          t = Clamp(t, 0.f, 1.f);
        }

        t *= pl.m_diffuseIntensity;

        c.Mad(pl.m_diffuseColor, Render::HDRColor(t,t,t,1.f), c);
      }

      c = ( ambientColor + (sceneEnvLight + c) * matLightIntensity[matIdx] ) * (*matDiffuseColor[matIdx]);

      Render::HDRColor const &minBakedColor = sceneConst.minBakedColor;
      Render::HDRColor const &maxBakedColor = sceneConst.maxBakedColor;
      c.R = Clamp(c.R, minBakedColor.R, Min(255.f, maxBakedColor.R));
      c.G = Clamp(c.G, minBakedColor.G, Min(255.f, maxBakedColor.G));
      c.B = Clamp(c.B, minBakedColor.B, Min(255.f, maxBakedColor.B));

      // adaptive scaling to allow HDR
      float max = Max(c.R, c.G);
      max = ceil(Max(max, c.B));
      float scale = 255.f / max;

      colors[ idx ] = Color(c.R * scale, c.G * scale, c.B * scale, max);
    }
  }

  vb->Unlock();
  pMeshGeom->primitives[0]->GetVertexBuffer()->Unlock();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::CalculateLighting(SceneConstants const &sceneConst)
{
  CalculateLightingEx(sceneConst, NDb::LIGHTENVIRONMENT_DAY);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::CalculateLightingEx(SceneConstants const &sceneConst, NDb::ELightEnvironment const selector)
{
  if (!pVertexColors)
    return;

  DXVertexBufferRef vb;

  switch (selector)
  {
  case NDb::LIGHTENVIRONMENT_DAY:
    vb = pVertexColors->pVB1;
    break;
  case NDb::LIGHTENVIRONMENT_NIGHT:
    vb = pVertexColors->pVB2;
    break;
  }

  if (!Get(vb))
    return;

  DoCalculateLighting(sceneConst, vb, selector);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void StaticMesh::AddGeometryCRC(Crc32Checksum &crc)
{
  if(RENDER_DISABLED)
    return;

  D3DVERTEXBUFFER_DESC desc;
  pMeshGeom->primitives[0]->GetVertexBuffer()->GetDesc(&desc);
  unsigned char *geovb = LockVB<unsigned char>(Get(pMeshGeom->primitives[0]->GetVertexBuffer()), 0);
  crc.Add(geovb, desc.Size);
  pMeshGeom->primitives[0]->GetVertexBuffer()->Unlock();
}

}; // namespace Render

#endif
