//generated file
// Automatically generated file, don't change it manually!
#include "stdafx.h"
#include "DebugMaterial.h"
#if defined(PW_LINUX_NULL_RENDER)

namespace Render
{
	namespace Materials
	{
	}
}

#else

#include "renderresourcemanager.h"

namespace Render
{
	namespace Materials
	{

		Materials::DebugMaterial* DebugMaterial::CreateMaterialInstance( const NDb::DebugMaterial* pDebugMaterial )
		{
			DebugMaterial* pMaterialInstance = new DebugMaterial();
			pMaterialInstance->default_DepthState = pDebugMaterial->default_DepthState;
			pMaterialInstance->default_CullingState = pDebugMaterial->default_CullingState;
			pMaterialInstance->default_FillState = pDebugMaterial->default_FillState;
			pMaterialInstance->DarkMapPinValue = pDebugMaterial->DarkMapPinValue;
			pMaterialInstance->SkeletalMeshPinValue = pDebugMaterial->SkeletalMeshPinValue;
			pMaterialInstance->DebugPinValue = pDebugMaterial->DebugPinValue;
			pMaterialInstance->subpriority = pDebugMaterial->subpriority;
			return pMaterialInstance;
		}

		void DebugMaterial::PrepareRenderer( NDb::Techniques techniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			#include "DebugMaterial.inl"
			SetRenderStates( techniqueID );
			BindSamplers( techniqueID );
			BindShaders( techniqueID );
			return;
#endif
		}

		void DebugMaterial::BindSamplers( NDb::Techniques currentTechniqueID )
		{
		}

		void DebugMaterial::BindShaders( NDb::Techniques currentTechniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			switch ( currentTechniqueID )
			{
				case NDb::TECHNIQUES_MAINPASS:
					RenderResourceManager::GetMultiShader( 7 )->GetShaderPairByIndex( GetShaderIndex() ).Bind();
					break;
			}
#endif
		}

		void DebugMaterial::SetRenderStates( NDb::Techniques currentTechniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			switch ( currentTechniqueID )
			{
				case NDb::TECHNIQUES_MAINPASS:
					GetStatesManager()->SetDepthState( default_DepthState );
					GetStatesManager()->SetCullingState( default_CullingState );
					GetStatesManager()->SetFillState( default_FillState );
					return;
				default:
					NI_ALWAYS_ASSERT( "wrong technique" );
			}
#endif
		}


	}//namespace Materials
}//namespace Render

#endif
