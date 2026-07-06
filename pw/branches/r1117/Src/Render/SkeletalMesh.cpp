#include "stdafx.h"
#include "SkeletalMesh.h"

#if defined(PW_LINUX_NULL_RENDER)

#include "DBRenderResources.h"
#include "batch.h"
#include "MaterialSpec.h"
#include "renderresourcemanager.h"
#include "SkeletalAnimationBlender.h"
#include "SkeletonWrapper.h"
#include "smartrenderer.h"

namespace Render
{

DECLARE_INSTANCE_COUNTER(SkeletalMesh);

namespace
{

class NullSkeletalMeshMaterial : public BaseMaterial
{
public:
  NullSkeletalMeshMaterial()
    : BaseMaterial(NDb::MATERIALPRIORITY_MESHESOPAQUE, 0, -1)
  {
  }

  virtual void PrepareRenderer()
  {
  }
};

bool FillPreviewDiffuseSampler(const NDb::Sampler& dbSampler, Render::Sampler* sampler)
{
  if (!sampler)
    return false;

  sampler->SetSamplerState(dbSampler.samplerState);
  return IsValid(dbSampler.texture);
}

bool FillPreviewDiffuseSampler(const NDb::SamplerEx& dbSampler, Render::Sampler* sampler)
{
  if (!sampler)
    return false;

  sampler->SetSamplerState(dbSampler.samplerState);
  sampler->SetMultiplierAndAdd(dbSampler.Multiplier, dbSampler.Add);
  return IsValid(dbSampler.texture);
}

bool FillPreviewDiffuseSampler(const NDb::Material* dbMaterial, Render::Sampler* sampler)
{
  if (!dbMaterial || !sampler)
    return false;

  switch (dbMaterial->GetObjectTypeID())
  {
    case NDb::BasicMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::BasicMaterial*>(dbMaterial)->DiffuseMap,
        sampler);

    case NDb::BasicMaskMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::BasicMaskMaterial*>(dbMaterial)->Diffuse,
        sampler);

    case NDb::BasicFXMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::BasicFXMaterial*>(dbMaterial)->DiffuseMap,
        sampler);

    case NDb::DropMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::DropMaterial*>(dbMaterial)->DiffuseMap,
        sampler);

    case NDb::ParticleFXMaterial::typeId:
      return FillPreviewDiffuseSampler(
        static_cast<const NDb::ParticleFXMaterial*>(dbMaterial)->DiffuseMap,
        sampler);

    default:
      return false;
  }
}

class LinuxSkeletalMeshPreviewMaterial : public NullSkeletalMeshMaterial
{
public:
  LinuxSkeletalMeshPreviewMaterial(const NDb::Material* dbMaterial)
    : NullSkeletalMeshMaterial()
    , diffuseSamplerReady(false)
  {
    if (!dbMaterial)
      return;

    pDbMaterial = dbMaterial;
    Render::Material::FillMaterial(dbMaterial, 0, false);
    diffuseSamplerReady = FillPreviewDiffuseSampler(dbMaterial, &diffuseMap);
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
  }

private:
  NDb::Ptr<NDb::Material> pDbMaterial;
  Render::Sampler diffuseMap;
  bool diffuseSamplerReady;
};

BaseMaterial* CreateLinuxSkeletalMeshPreviewMaterial(
  const NDb::SkinPartBase* skinPart,
  const MeshGeometry* meshGeometry,
  int primitiveIndex)
{
  if (skinPart && meshGeometry && primitiveIndex >= 0 &&
      static_cast<size_t>(primitiveIndex) <
        sizeof(meshGeometry->materialID) / sizeof(meshGeometry->materialID[0]))
  {
    const int materialIndex = meshGeometry->materialID[primitiveIndex];
    if (materialIndex >= 0 &&
        static_cast<size_t>(materialIndex) < skinPart->materialsReferences.size())
    {
      const NDb::Material* dbMaterial = skinPart->materialsReferences[materialIndex].GetPtr();
      if (dbMaterial)
        return new LinuxSkeletalMeshPreviewMaterial(dbMaterial);
    }
  }

  return new NullSkeletalMeshMaterial();
}

} // namespace

void SkeletalMesh::Update(bool bNeedBlenderUpdate)
{
  if (bNeedBlenderUpdate && pSkeletalAnimationBlender && pSkeletonWrapper)
  {
    pSkeletalAnimationBlender->Sample(pSkeletonWrapper->GetSampledMatrix());
  }

  if (pSkeletonWrapper)
  {
    pSkeletonWrapper->Update(worldMatrix);
  }
}

void SkeletalMesh::RenderToQueue(BatchQueue& queue)
{
  for (int slotNumber = 0; slotNumber < (int)maxSlotsCount; ++slotNumber)
  {
    SkeletalMeshElement& slot = elementSlots[slotNumber];
    if (slot.IsEmpty() || !slot.IsEnabled())
      continue;

    BaseMaterial* const pMaterial = slot.GetMaterial();
    Primitive const* const pPrimitive = slot.GetPrimitive();
    if (!pMaterial || !pPrimitive)
      continue;

    queue.Push(pMaterial->GetPriority(), this, pPrimitive, slotNumber, pMaterial);
  }
}

void SkeletalMesh::PrepareRendererAfterMaterial(unsigned int slotNumber) const
{
#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
  SmartRenderer::SetOpenGLImmediateObjectMatrix(0);
  if (!pSkeletonWrapper || slotNumber >= maxSlotsCount || elementSlots[slotNumber].IsEmpty())
  {
    SmartRenderer::SetOpenGLImmediateSkeletalMatrices(0, 0);
    return;
  }

  StaticVector<unsigned short> const* fragmentReindex = elementSlots[slotNumber].GetFragmentReindex();
  if (!fragmentReindex)
  {
    SmartRenderer::SetOpenGLImmediateSkeletalMatrices(0, 0);
    return;
  }

  const Matrix43* skinWorldMatrices = pSkeletonWrapper->GetSkinWorldMatrices();
  if (!skinWorldMatrices)
  {
    SmartRenderer::SetOpenGLImmediateSkeletalMatrices(0, 0);
    return;
  }

  const unsigned int currentBoneCount = fragmentReindex->size();
  const Matrix43* matrixArray = skinWorldMatrices + elementSlots[slotNumber].GetMatrixIndex();
  SmartRenderer::SetOpenGLImmediateSkeletalMatrices(matrixArray, currentBoneCount);
#else
  (void)slotNumber;
#endif
}

SkeletalMesh::SkeletalMesh()
  : pSkeletonWrapper(0)
  , pSkeletalAnimationBlender(0)
  , lightsFlags(0)
  , needLightingSH(false)
{
  Identity(&worldMatrix);
  localAABB.Init2Empty();
  worldAABB.center.Set(0.0f, 0.0f, 0.0f);
  worldAABB.halfSize.Set(0.5f, 0.5f, 0.5f);
}

void SkeletalMesh::Initialize(const Matrix43& _worldMatrix, const nstl::string& skeletonFileName)
{
  worldMatrix = _worldMatrix;
  delete pSkeletonWrapper;
  pSkeletonWrapper = 0;

  if (SkeletonDataWrapper* pSkeletonData = RenderResourceManager::LoadSkeletonData(skeletonFileName))
  {
    pSkeletonWrapper = new SkeletonWrapper(pSkeletonData);
  }
}

void SkeletalMesh::AddSkinPart(const NDb::SkinPartBase* pDBSkinPartResource, unsigned int* pPartIndexes, unsigned int* pPartsCount)
{
  if (pPartsCount)
  {
    *pPartsCount = 0;
  }

  if (!pDBSkinPartResource || !pPartIndexes || !pPartsCount)
  {
    return;
  }

  const MeshGeometry* pMeshGeometry = Render::RenderResourceManager::LoadSkeletalMeshGeometry(pDBSkinPartResource->geometryFileName);
  if (!pMeshGeometry)
  {
    return;
  }

  const int primitiveCount = pMeshGeometry->primitives.size();
  int counter = 0;
  for (int i = 0; i < (int)maxSlotsCount && counter < primitiveCount; ++i)
  {
    if (!elementSlots[i].IsEmpty())
      continue;

    BaseMaterial* pMaterial =
      CreateLinuxSkeletalMeshPreviewMaterial(pDBSkinPartResource, pMeshGeometry, counter);
    elementSlots[i].Initialize(pMaterial, pMeshGeometry, counter);
    pPartIndexes[counter] = i;
    ++counter;
  }

  *pPartsCount = counter;
  UpdateReindexMap();
}

void SkeletalMesh::RemoveSkinPart(unsigned int partsCount, const unsigned int* pPartIndexes)
{
  if (!pPartIndexes)
    return;

  for (unsigned int i = 0; i < partsCount; ++i)
  {
    const unsigned int slotIndex = pPartIndexes[i];
    if (slotIndex < maxSlotsCount)
      elementSlots[slotIndex].Destroy();
  }

  UpdateReindexMap();
}

void SkeletalMesh::SetEnableSkinPart(unsigned int partsCount, const unsigned int* pPartIndexes, bool val)
{
  if (!pPartIndexes)
    return;

  for (unsigned int i = 0; i < partsCount; ++i)
  {
    const unsigned int slotIndex = pPartIndexes[i];
    if (slotIndex < maxSlotsCount)
      elementSlots[slotIndex].SetEnabled(val);
  }
}

void SkeletalMesh::UpdateReindexMap()
{
  if (!pSkeletonWrapper)
  {
    return;
  }

  int startIndex = 0;

  for (unsigned int slotNumber = 0; slotNumber < maxSlotsCount; ++slotNumber)
  {
    if (elementSlots[slotNumber].IsEmpty())
      continue;

    StaticVector<unsigned short> const* fragmentReindex = elementSlots[slotNumber].GetFragmentReindex();
    if (fragmentReindex)
      startIndex += fragmentReindex->size();
  }

  pSkeletonWrapper->SetActiveBones(startIndex);

  startIndex = 0;

  for (unsigned int slotNumber = 0; slotNumber < maxSlotsCount; ++slotNumber)
  {
    if (elementSlots[slotNumber].IsEmpty())
      continue;

    StaticVector<unsigned short> const* fragmentReindex = elementSlots[slotNumber].GetFragmentReindex();
    if (!fragmentReindex)
      continue;

    elementSlots[slotNumber].SetMatrixIndex(startIndex);
    const unsigned int currentFragmentBoneCount = fragmentReindex->size();
    for (unsigned int boneNumber = 0; boneNumber < currentFragmentBoneCount; ++boneNumber)
    {
      pSkeletonWrapper->SetReindex(*(fragmentReindex->at(boneNumber)), startIndex + boneNumber);
    }

    startIndex += currentFragmentBoneCount;
  }
}

bool SkeletalMesh::FillOBB(CVec3 (&_vertices)[8]) const
{
  if (localAABB.IsEmpty())
  {
    return false;
  }

  RenderComponent::FillOBB(localAABB, worldMatrix, &_vertices[0]);
  return true;
}

void SkeletalMesh::SetAnimationBlender(SkeletalAnimationBlender* pSampler)
{
  pSkeletalAnimationBlender = pSampler;
}

void SkeletalMesh::SetWorldMatrix(const Matrix43& transform)
{
  worldMatrix = transform;
}

void SkeletalMesh::SetMaterial(int slotNumber, BaseMaterial* _pMaterial)
{
  if (slotNumber < 0 || slotNumber >= (int)maxSlotsCount)
  {
    return;
  }

  elementSlots[slotNumber].SetMaterial(_pMaterial);
}

BaseMaterial* SkeletalMesh::GetMaterial(int slotNumber)
{
  if (slotNumber < 0 || slotNumber >= (int)maxSlotsCount)
  {
    return 0;
  }

  return elementSlots[slotNumber].GetMaterial();
}

SkeletalMesh::~SkeletalMesh()
{
  delete pSkeletonWrapper;
  pSkeletonWrapper = 0;
  pSkeletalAnimationBlender = 0;
}

void SkeletalMesh::ForAllMaterials(Render::IMaterialProcessor& proc)
{
  for (int i = 0; i < (int)maxSlotsCount; ++i)
  {
    BaseMaterial* const pMaterial = GetMaterial(i);
    if (pMaterial)
      proc(*pMaterial);
  }
}

void SkeletalMeshElement::Initialize(BaseMaterial* pMaterial, MeshGeometry const* meshGeom, int index)
{
  pMaterialInstance = pMaterial;
  pMeshGeom = meshGeom;
  primitiveIndex = index;
  matrixIndex = 0;
  isEnabled = true;
}

SkeletalMeshElement::~SkeletalMeshElement()
{
  Destroy();
}

void SkeletalMeshElement::Destroy()
{
  delete pMaterialInstance;
  pMaterialInstance = 0;
  pMeshGeom = 0;
  primitiveIndex = 0;
  matrixIndex = 0;
}

void SkeletalMeshElement::SetMaterial(BaseMaterial* _pMaterial)
{
  if (pMaterialInstance == _pMaterial)
    return;

  delete pMaterialInstance;
  pMaterialInstance = _pMaterial;
}

Primitive const* SkeletalMeshElement::GetPrimitive() const
{
  if (!pMeshGeom ||
      primitiveIndex < 0 ||
      primitiveIndex >= pMeshGeom->primitives.size())
  {
    return 0;
  }

  return pMeshGeom->primitives[primitiveIndex];
}

StaticVector<unsigned short> const* SkeletalMeshElement::GetFragmentReindex() const
{
  if (!pMeshGeom ||
      !pMeshGeom->pReindex ||
      primitiveIndex < 0 ||
      primitiveIndex >= pMeshGeom->pReindex->reindex.size())
  {
    return 0;
  }

  return pMeshGeom->pReindex->reindex.at(primitiveIndex);
}

} // namespace Render

#else

#include "batch.h"
#include "ConvexVolume.h"
#include "../MeshConverter/SkeletalAnimationHeader.h"
#include "SkeletalAnimationBlender.h"
#include "SkeletonReindexResource.h"
#include "renderresourcemanager.h"
#include "SkeletonWrapper.h"
#include "GlobalMasks.h"
#include "NullRenderSignal.h"

static NDebug::DebugVar<int> render_Skel_Update( "Skel_Update", "PerfCnt", true );
static NDebug::DebugVar<int> render_Skel_Render( "Skel_Render", "PerfCnt", true );

namespace
{
  DECLARE_NULL_RENDER_FLAG

#ifndef _SHIPPING
  bool s_NoAnimation = false;
  REGISTER_DEV_VAR("animDisable", s_NoAnimation, STORAGE_NONE);

  bool s_drawSkeletal = true;
  REGISTER_DEV_VAR("draw_skeletons", s_drawSkeletal, STORAGE_NONE);
#else
  enum {
    s_NoAnimation = false,
    s_drawSkeletal = true
  };
#endif

inline bool VertexHasColors(DXVertexDeclarationRef decl)
{
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

namespace Render
{
	DECLARE_INSTANCE_COUNTER(SkeletalMesh);
	void SkeletalMesh::Update(bool bNeedBlenderUpdate)
	{
    if(bNeedBlenderUpdate)
    {
		  pSkeletalAnimationBlender->Sample( pSkeletonWrapper->GetSampledMatrix() );
    }
		pSkeletonWrapper->Update( worldMatrix );

		render_Skel_Update.AddValue(1);
	}

  void SkeletalMesh::RenderToQueue( BatchQueue& queue )
	{
    if(!s_drawSkeletal)
      return;

		render_Skel_Render.AddValue(1);

    UINT numPointLights = 0;
    if (lightsFlags != 0)
      numPointLights = lightsData.Fill(worldAABB);

    for(int slotNumber = 0, mask = 1; slotNumber < maxSlotsCount; ++slotNumber, mask <<= 1)
    {
      SkeletalMeshElement &slot = elementSlots[slotNumber];
      if( slot.IsEmpty() || !slot.IsEnabled() )
        continue;
      BaseMaterial* const pMaterial = slot.pMaterialInstance;
      if( lightsFlags & mask )
        pMaterial->SetLightingPin( NDb::LightingPin(NDb::LIGHTINGPIN_LIGHTINGDYNAMIC + numPointLights) );

      //pMaterial->SetSkeletalMeshPin(s_NoAnimation ? NDb::BOOLEANPIN_NONE : NDb::BOOLEANPIN_PRESENT);

			queue.Push( pMaterial->GetPriority(), this, slot.GetPrimitive(), slotNumber, pMaterial ); 
		}
  }

	void SkeletalMesh::PrepareRendererAfterMaterial( unsigned int slotNumber ) const
	{
		RenderComponent::PrepareRendererAfterMaterial( slotNumber );

		SHMatrix world;
		Copy( &world, worldMatrix );
		Render::GetRenderer()->SetVertexShaderConstantsMatrix( WORLD, world );

    if(!s_NoAnimation)
    {
      NI_ASSERT( !elementSlots[slotNumber].IsEmpty(), "slot is empty!" );
      const unsigned int currentBoneCount = elementSlots[slotNumber].GetFragmentReindex()->size();
      NI_VERIFY( currentBoneCount <= maxBoneCountPerFragment, "bone count exceed the limit", return );
      const Matrix43* matrixArray = pSkeletonWrapper->GetSkinWorldMatrices() + elementSlots[slotNumber].GetMatrixIndex();
      Render::GetRenderer()->SetVertexShaderConstantsMatrix43( BONES, currentBoneCount, matrixArray );
    }

    // set lighting parameters
    if (lightsFlags & (1 << slotNumber))
      lightsData.Setup();
	}

  SkeletalMesh::SkeletalMesh() : pSkeletonWrapper(0), pSkeletalAnimationBlender(0), lightsFlags(0), needLightingSH( false )
	{
		Identity(&worldMatrix);
    localAABB.Init2Empty();
    worldAABB.center.Set(0.0f, 0.0f, 0.0f);
    worldAABB.halfSize.Set(0.5f, 0.5f, 0.5f);
	}

	void SkeletalMesh::Initialize( const Matrix43& _worldMatrix, const nstl::string& skeletonFileName )
	{
		worldMatrix = _worldMatrix;
#ifdef _DEBUG
		skeletonSourceFileName = skeletonFileName;
#endif
		pSkeletonWrapper = new SkeletonWrapper( RenderResourceManager::LoadSkeletonData( skeletonFileName ) );
	}

	void SkeletalMesh::AddSkinPart( const NDb::SkinPartBase* pDBSkinPartResource, unsigned int* pPartIndexes, unsigned int* pPartsCount )
	{
		NI_VERIFY( pDBSkinPartResource, "pDBSkinPartResource is null", return );
		NI_ASSERT( pPartIndexes, "null ptr to pPartIndexes" );
		NI_ASSERT( pPartsCount, "null ptr to pPartsCount" );

		const MeshGeometry* pMeshGeometry = Render::RenderResourceManager::LoadSkeletalMeshGeometry(pDBSkinPartResource->geometryFileName);
		NI_VERIFY( pMeshGeometry, "Cannot create mesh geometry", return );

		const int primitiveCount = pMeshGeometry->primitives.size();
		NI_ASSERT( pMeshGeometry->fragmentCount < maxSlotsCount , "execeeded the limit of elements per skinpart" );
		NI_ASSERT( pMeshGeometry->materialCount == pDBSkinPartResource->materialsReferences.size() , "wrong number of materials" );
		int counter = 0;
		for ( int i = 0; i < maxSlotsCount; ++i )
		{
			if( !elementSlots[i].IsEmpty() )
				continue;

      BaseMaterial* pMaterial = static_cast<BaseMaterial*>( Render::CreateRenderMaterial( pDBSkinPartResource->materialsReferences[pMeshGeometry->materialID[counter]].GetPtr() ));
			elementSlots[i].Initialize( pMaterial, pMeshGeometry, counter );

      // check material lighting for correctness and initialize
      if (pMaterial)
      {
        BasicMaterial* pBasicMaterial = dynamic_cast<BasicMaterial*>(pMaterial);
        if (pBasicMaterial)
        {
          if (pBasicMaterial->GetLightingPin() == NDb::LIGHTINGPIN_LIGHTINGDYNAMIC)
            lightsFlags |= (1 << i);
          else if (pBasicMaterial->GetLightingPin() == NDb::LIGHTINGPIN_LIGHTINGSH)
            needLightingSH = true;
          else
          {
            NI_DATA_ALWAYS_ASSERT(NStr::StrFmt("Unsupported lighting (%s) is specified in material for animated mesh. See '%s'", 
              NDb::EnumToString(pBasicMaterial->GetLightingPin()), GetDBID() ? GetFormattedDbId(*GetDBID()).c_str() : pDBSkinPartResource->geometryFileName));
          }
        }
      }

			pPartIndexes[counter] = i;
			++counter;
			if(counter == primitiveCount)
				break;
		}
		NI_ASSERT(counter == primitiveCount, "not all elements of skinpart have been added!" );
		*pPartsCount = counter;

    UpdateReindexMap();
	}

  void SkeletalMesh::RemoveSkinPart( unsigned int partsCount, const unsigned int* pPartIndexes )
  {
    NI_ASSERT( partsCount < maxSlotsCount, "parts count more than slots" );
    for ( unsigned int i = 0; i < partsCount; ++i )
    {
      elementSlots[pPartIndexes[i]].Destroy();
    }

    UpdateReindexMap();
  }

  void SkeletalMesh::SetEnableSkinPart( unsigned int partsCount, const unsigned int* pPartIndexes, bool val )
  {
    NI_ASSERT( partsCount < maxSlotsCount, "parts count more than slots" );
    for ( unsigned int i = 0; i < partsCount; ++i )
    {
      elementSlots[pPartIndexes[i]].SetEnabled( val );
    }
  }

  void SkeletalMesh::UpdateReindexMap()
  {
    int startIndex = 0;

    for ( unsigned int slotNumber = 0; slotNumber < maxSlotsCount; ++slotNumber )
    {
      if ( elementSlots[slotNumber].IsEmpty() )
        continue;

      startIndex += elementSlots[slotNumber].GetFragmentReindex()->size();
    }

    pSkeletonWrapper->SetActiveBones( startIndex );

    startIndex = 0;

    for ( unsigned int slotNumber = 0; slotNumber < maxSlotsCount; ++slotNumber )
    {
      if ( elementSlots[slotNumber].IsEmpty() )
        continue;

      elementSlots[slotNumber].SetMatrixIndex( startIndex );

      const unsigned int currentFragmentBoneCount = elementSlots[slotNumber].GetFragmentReindex()->size();

      for( unsigned int boneNumber = 0; boneNumber < currentFragmentBoneCount; ++boneNumber )
      {
        pSkeletonWrapper->SetReindex( *(elementSlots[slotNumber].GetFragmentReindex()->at(boneNumber)), startIndex + boneNumber );
      }

      startIndex += currentFragmentBoneCount;
    }
  }

  /////////////////////////////////////////////////////////////////////////
  bool SkeletalMesh::FillOBB(CVec3 (&_vertices)[8]) const
  {
    if( localAABB.IsEmpty() )
      return false;

    RenderComponent::FillOBB(localAABB, worldMatrix, &_vertices[0]);
    return true;
  }

	void SkeletalMesh::SetAnimationBlender( SkeletalAnimationBlender* pSampler )
	{
		NI_ASSERT(pSampler, "null ptr to SkeletonAnimationSampler");
		pSkeletalAnimationBlender = pSampler;
	}

	//void SkeletalMesh::SetSkeletonWrapper( SkeletonWrapper* _pSkeletonWrapper )
	//{
	//	NI_ASSERT(_pSkeletonWrapper, "null ptr to SkeletonWrapper");
	//	pSkeletonWrapper = _pSkeletonWrapper;
	//}

	void SkeletalMesh::SetWorldMatrix( const Matrix43& transform )
	{
		worldMatrix = transform; 
	}
  
  void SkeletalMesh::SetMaterial( int slotNumber, BaseMaterial* _pMaterial )
  {
    NI_VERIFY( slotNumber < (int)maxSlotsCount && slotNumber >= 0, "", return );

    if ( elementSlots[slotNumber].IsEmpty() )
      return;

    elementSlots[slotNumber].SetMaterial( _pMaterial );
  }

  BaseMaterial* SkeletalMesh::GetMaterial( int slotNumber )
  {
    NI_VERIFY( slotNumber < (int)maxSlotsCount && slotNumber >= 0, "", return 0);

    if ( elementSlots[slotNumber].IsEmpty() )
      return 0;

    return elementSlots[slotNumber].GetMaterial();
  }

	SkeletalMesh::~SkeletalMesh()
	{
    delete pSkeletonWrapper;
		pSkeletonWrapper = NULL;
	}

  void SkeletalMesh::ForAllMaterials(Render::IMaterialProcessor &proc)
  {
    Render::BaseMaterial *pMat = 0; 
    for (int i = 0; i < maxSlotsCount; ++i)
    {
      pMat = GetMaterial(i);
      if (pMat)
        proc(*pMat);
    }
  }

	void SkeletalMeshElement:: Initialize( BaseMaterial* pMaterial, MeshGeometry const *meshGeom, int index )
	{
		NI_ASSERT( pMaterial, "null ptr to material" );
    NI_ASSERT( meshGeom, "null ptr to mesh geometry" );
    NI_ASSERT( index < meshGeom->primitives.size() && index >= 0, "invalid primtive index" );
		NI_ASSERT( meshGeom->primitives[ index ], "null ptr to primitive" );
		NI_ASSERT( meshGeom->pReindex->reindex.at( index ), "null ptr to reindex" );
		
    pMeshGeom = meshGeom;
    primitiveIndex = index;
    SetMaterial(pMaterial);
    isEnabled = true;
	}

	SkeletalMeshElement::~SkeletalMeshElement()
	{
		Destroy();
	}

	void SkeletalMeshElement::Destroy()
	{
		delete pMaterialInstance; 
    pMaterialInstance = 0; 
    pMeshGeom = 0;
	}

	void SkeletalMeshElement::SetMaterial( BaseMaterial* _pMaterial )
	{
		NI_VERIFY( _pMaterial, "null ptr to material", return ); 
    if (pMaterialInstance)
		  delete pMaterialInstance; 

    _pMaterial->SetSkeletalMeshPin(NDb::BOOLEANPIN_PRESENT);

    if (pMeshGeom && !RENDER_DISABLED &&
        pMeshGeom->primitives[primitiveIndex])
    {
      _pMaterial->SetMultiplyVertexColorPin( VertexHasColors(pMeshGeom->primitives[primitiveIndex]->GetVertexDeclaration()) ?
                                             NDb::BOOLEANPIN_PRESENT : NDb::BOOLEANPIN_NONE );
    }

    pMaterialInstance = _pMaterial;
	}

  Primitive const* SkeletalMeshElement::GetPrimitive() const
  {
    NI_ASSERT( primitiveIndex < pMeshGeom->primitives.size() && primitiveIndex >= 0, "invalid primtive index" );
		
    Primitive const *res = pMeshGeom->primitives[ primitiveIndex ];
    NI_ASSERT( res, "null ptr to primitive" );

    return res;
  }

  StaticVector<unsigned short> const* SkeletalMeshElement::GetFragmentReindex() const
  {
    NI_ASSERT( primitiveIndex < pMeshGeom->primitives.size() && primitiveIndex >= 0, "invalid primtive index" );

    StaticVector<unsigned short> const* res = pMeshGeom->pReindex->reindex.at( primitiveIndex );
		NI_ASSERT( res, "null ptr to reindex" );

    return res;
  }

};

#endif
