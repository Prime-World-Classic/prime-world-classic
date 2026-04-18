//generated file
// Automatically generated file, don't change it manually!
#include "stdafx.h"
#include "GrassMaterial.h"
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

		Materials::GrassMaterial* GrassMaterial::CreateMaterialInstance( const NDb::GrassMaterial* pGrassMaterial )
		{
			GrassMaterial* pMaterialInstance = new GrassMaterial();
			pMaterialInstance->default_BlendState = pGrassMaterial->default_BlendState;
			pMaterialInstance->default_AlphaState = pGrassMaterial->default_AlphaState;
			pMaterialInstance->default_CullingState = pGrassMaterial->default_CullingState;
			pMaterialInstance->FrozenDiffuseMap = pGrassMaterial->FrozenDiffuseMap;
			pMaterialInstance->NormalDiffuseMap = pGrassMaterial->NormalDiffuseMap;
			pMaterialInstance->BurnedDiffuseMap = pGrassMaterial->BurnedDiffuseMap;
			pMaterialInstance->ShadowReciverPinValue = pGrassMaterial->ShadowReciverPinValue;
			pMaterialInstance->bladeSize = pGrassMaterial->bladeSize;
			pMaterialInstance->elasticity = pGrassMaterial->elasticity;
			pMaterialInstance->subpriority = pGrassMaterial->subpriority;
			return pMaterialInstance;
		}

		void GrassMaterial::PrepareRenderer( NDb::Techniques techniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			#include "GrassMaterial.inl"
			SetRenderStates( techniqueID );
			BindSamplers( techniqueID );
			BindShaders( techniqueID );
			return;
#endif
		}

		void GrassMaterial::BindSamplers( NDb::Techniques currentTechniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			if( IsFrozenDiffuseMapPresent() )
				FrozenDiffuseMap.Bind();
			if( IsNormalDiffuseMapPresent() )
				NormalDiffuseMap.Bind();
			if( IsBurnedDiffuseMapPresent() )
				BurnedDiffuseMap.Bind();
#endif
		}

		void GrassMaterial::BindShaders( NDb::Techniques currentTechniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			switch ( currentTechniqueID )
			{
				case NDb::TECHNIQUES_SHADOWPASS:
					RenderResourceManager::GetMultiShader( 109 )->GetShaderPairByIndex( GetShaderIndex() ).Bind();
					break;
				case NDb::TECHNIQUES_MAINPASS:
					RenderResourceManager::GetMultiShader( 9 )->GetShaderPairByIndex( GetShaderIndex() ).Bind();
					break;
			}
#endif
		}

		void GrassMaterial::SetRenderStates( NDb::Techniques currentTechniqueID )
		{
#if defined(PW_LINUX_NULL_RENDER)
			return;
#else
			switch ( currentTechniqueID )
			{
				case NDb::TECHNIQUES_SHADOWPASS:
					GetStatesManager()->SetAlphaState( default_AlphaState );
					GetStatesManager()->SetBlendState( default_BlendState );
					GetStatesManager()->SetCullingState( default_CullingState );
					return;
				case NDb::TECHNIQUES_MAINPASS:
					GetStatesManager()->SetAlphaState( default_AlphaState );
					GetStatesManager()->SetBlendState( default_BlendState );
					GetStatesManager()->SetCullingState( default_CullingState );
					return;
				default:
					NI_ALWAYS_ASSERT( "wrong technique" );
			}
#endif
		}


	}//namespace Materials
}//namespace Render

#endif
