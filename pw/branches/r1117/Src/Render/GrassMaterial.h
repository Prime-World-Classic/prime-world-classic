//generated file
#pragma once
#if defined(PW_LINUX_NULL_RENDER)
#include "IMaterial.h"
#include "DBRender.h"

namespace Render
{
	namespace Materials
	{
		class GrassMaterial : public IMaterial
		{
		public:
			GrassMaterial() = default;
			static Materials::GrassMaterial* CreateMaterialInstance( const NDb::GrassMaterial* pGrassMaterial )
			{
				GrassMaterial* pMaterialInstance = new GrassMaterial();
				pMaterialInstance->FillMaterial( pGrassMaterial, 0, false );
				return pMaterialInstance;
			}
			virtual void PrepareRenderer() override {}
			virtual ~GrassMaterial() { }


			bool IsBlendEnabled() const
			{
				return false;
			}

			bool IsShadowCaster() const
			{
				return false;
			}

			DWORD GetTypeID() const
			{
				return 0x1056F3C0;
			}
			virtual void FillMaterial( const NDb::Material* pDbMaterial, void* texturePoolId, bool savePointer = true ) override
			{
				Material::FillMaterial( pDbMaterial, texturePoolId, savePointer );
			}
		};
	}//namespace Materials
}//namespace Render

#else

#include "IMaterial.h"
#include "MaterialResourceInterface.h"

namespace Render
{
	namespace Materials
	{

		class GrassMaterial : public IMaterial
		{
			NDb::BlendState default_BlendState;
			NDb::AlphaState default_AlphaState;
			NDb::CullingState default_CullingState;
			Render::Sampler FrozenDiffuseMap;
			Render::Sampler NormalDiffuseMap;
			Render::Sampler BurnedDiffuseMap;
			NDb::ShadowReciverPin ShadowReciverPinValue;
			CVec2 bladeSize;
			float elasticity;


		public:
			static Materials::GrassMaterial* CreateMaterialInstance( const NDb::GrassMaterial* pGrassMaterial );
			virtual void PrepareRenderer( NDb::Techniques techniqueID );
			virtual ~GrassMaterial() { }


			bool IsBlendEnabled() const
			{
				return default_BlendState.blendIsEnabled ? true : false;
			}

			bool IsShadowCaster() const
			{
				return false;
			}

			DWORD GetTypeID() const
			{
				return 0x1056F3C0;
			}

			bool IsFrozenDiffuseMapPresent() const { return true; }

			bool IsNormalDiffuseMapPresent() const { return true; }

			bool IsBurnedDiffuseMapPresent() const { return true; }

			void SetShadowReciverPinValue( NDb::ShadowReciverPin _ShadowReciverPinValue ) { ShadowReciverPinValue = _ShadowReciverPinValue; }
			NDb::ShadowReciverPin GetShadowReciverPinValue( ) const { return ShadowReciverPinValue; }

			void SetFrozenDiffuseMapMultiplierAndAdd(const HDRColor& multiplier, const HDRColor& add)
			{
				FrozenDiffuseMap.SetMultiplierAndAdd(multiplier, add);
			}

			void GetFrozenDiffuseMapMultiplierAndAdd(HDRColor& multiplier, HDRColor& add)
			{
				FrozenDiffuseMap.GetMultiplierAndAdd(multiplier, add);
			}

			void SetNormalDiffuseMapMultiplierAndAdd(const HDRColor& multiplier, const HDRColor& add)
			{
				NormalDiffuseMap.SetMultiplierAndAdd(multiplier, add);
			}

			void GetNormalDiffuseMapMultiplierAndAdd(HDRColor& multiplier, HDRColor& add)
			{
				NormalDiffuseMap.GetMultiplierAndAdd(multiplier, add);
			}

			void SetBurnedDiffuseMapMultiplierAndAdd(const HDRColor& multiplier, const HDRColor& add)
			{
				BurnedDiffuseMap.SetMultiplierAndAdd(multiplier, add);
			}

			void GetBurnedDiffuseMapMultiplierAndAdd(HDRColor& multiplier, HDRColor& add)
			{
				BurnedDiffuseMap.GetMultiplierAndAdd(multiplier, add);
			}

		private:
			void BindSamplers( NDb::Techniques techniqueID );
			void BindShaders( NDb::Techniques techniqueID );
			void SetRenderStates( NDb::Techniques techniqueID );

			unsigned int GetShaderIndex() const
			{
				return 1 * ShadowReciverPinValue;
			}
		};

	}//namespace Materials
}//namespace Render

#endif
