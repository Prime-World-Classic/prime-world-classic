#include "stdafx.h"

#include "smartrenderer.h"
#include "ImmediateRenderer.h"
#include "SHCoeffs.h"

// #define DEBUG_SH

namespace Render
{

void SHShaderConstants::InitAsConstant(float r, float g, float b)
{
	memset(this, 0, sizeof(SHShaderConstants));
	linearAndConstant[0].w = r;
	linearAndConstant[1].w = g;
	linearAndConstant[2].w = b;
}

void SHShaderConstants::Add(const SHShaderConstants &c)
{
	quadraticRGB += c.quadraticRGB;
	for (int i = 0; i < 3; i++)
	{
		linearAndConstant[i] += c.linearAndConstant[i];
		quadratic[i] += c.quadratic[i];
	}
}


bool SHShaderConstants::Import(const vector<float>& _src)
{
  enum {
    size_linearAndConstant = ARRAY_SIZE(linearAndConstant),
    size_quadratic         = ARRAY_SIZE(quadratic)
  };
  NI_STATIC_ASSERT(sizeof(CVec4) == 4 * sizeof(float), CVec4_is_not_equivalent_to_float4);

  if(_src.size() < (size_linearAndConstant + size_quadratic + 1) * 4)
    return false;

  memcpy( linearAndConstant, &_src[0], sizeof(linearAndConstant) );
  memcpy( quadratic, &_src[size_linearAndConstant * 4], sizeof(quadratic) );
  memcpy( &quadraticRGB, &_src[(size_linearAndConstant + size_quadratic) * 4], sizeof(quadraticRGB) );
  return true;
}


void SHShaderConstants::Export(vector<float>& _dst) const
{
  enum {
    size_linearAndConstant = ARRAY_SIZE(linearAndConstant),
    size_quadratic         = ARRAY_SIZE(quadratic)
  };
  NI_STATIC_ASSERT(sizeof(CVec4) == 4 * sizeof(float), CVec4_is_not_equivalent_to_float4);

  _dst.resize( sizeof(CVec4) * (size_linearAndConstant + size_quadratic + 1) );
  memcpy( &_dst[0], linearAndConstant, sizeof(linearAndConstant) );
  memcpy( &_dst[size_linearAndConstant * 4], quadratic, sizeof(quadratic) );
  memcpy( &_dst[(size_linearAndConstant + size_quadratic) * 4], &quadraticRGB, sizeof(quadraticRGB) );
}


void ConvertSHCoeffs2ShaderConstants(const SHCoeffs &c, SHShaderConstants &s)
{
	// Lighting environment coefficients
	CVec4 vCoefficients[3];

	// These constants are described in the article by Peter-Pike Sloan titled 
	// "Efficient Evaluation of Irradiance Environment Maps" in the book 
	// "ShaderX 2 - Shader Programming Tips and Tricks" by Wolfgang F. Engel.
	static const float s_fSqrtPI = ((float)sqrtf(D3DX_PI));
	const float fC0 = 1.0f/(2.0f*s_fSqrtPI);
	const float fC1 = (float)sqrt(3.0f)/(3.0f*s_fSqrtPI);
	const float fC2 = (float)sqrt(15.0f)/(8.0f*s_fSqrtPI);
	const float fC3 = (float)sqrt(5.0f)/(16.0f*s_fSqrtPI);
	const float fC4 = 0.5f*fC2;

	int iChannel;
	for( iChannel=0; iChannel<3; iChannel++ )
	{
		vCoefficients[iChannel].x = -fC1*c.coeffs[iChannel][3];
		vCoefficients[iChannel].y = -fC1*c.coeffs[iChannel][1];
		vCoefficients[iChannel].z =  fC1*c.coeffs[iChannel][2];
		vCoefficients[iChannel].w =  fC0*c.coeffs[iChannel][0] - fC3*c.coeffs[iChannel][6];
	}

	s.linearAndConstant[0] = vCoefficients[0];
	s.linearAndConstant[1] = vCoefficients[1];
	s.linearAndConstant[2] = vCoefficients[2];

	for( iChannel=0; iChannel<3; iChannel++ )
	{
		vCoefficients[iChannel].x =      fC2*c.coeffs[iChannel][4];
		vCoefficients[iChannel].y =     -fC2*c.coeffs[iChannel][5];
		vCoefficients[iChannel].z = 3.0f*fC3*c.coeffs[iChannel][6];
		vCoefficients[iChannel].w =     -fC2*c.coeffs[iChannel][7];
	}

	s.quadratic[0] = vCoefficients[0];
	s.quadratic[1] = vCoefficients[1];
	s.quadratic[2] = vCoefficients[2];

	vCoefficients[0].x = fC4*c.coeffs[0][8];
	vCoefficients[0].y = fC4*c.coeffs[1][8];
	vCoefficients[0].z = fC4*c.coeffs[2][8];
	vCoefficients[0].w = 1.0f;

	s.quadraticRGB = vCoefficients[0];

  for (int i = 0; i < 3; ++i)
  {
    if ( s.quadratic[i].x < 0.f ) s.quadratic[i].x = 0.f;
    if ( s.quadratic[i].y < 0.f ) s.quadratic[i].y = 0.f;
    if ( s.quadratic[i].z < 0.f ) s.quadratic[i].z = 0.f;
  }
}

void EvaluateLightingBySHShaderConstants(const SHShaderConstants &consts, const CVec3 &normal, HDRColor &color)
{
	CVec4 vN(normal, 1.0f);

	// Linear + constant polynomial terms
	color.R = vN.Dot(consts.linearAndConstant[0]);
	color.G = vN.Dot(consts.linearAndConstant[1]);
	color.B = vN.Dot(consts.linearAndConstant[2]);

	// 4 of the quadratic polynomials
	CVec4 vB(vN.x * vN.y, vN.y * vN.z, vN.z * vN.z, vN.z * vN.x);
	color.R += vB.Dot(consts.quadratic[0]);
	color.G += vB.Dot(consts.quadratic[1]);
	color.B += vB.Dot(consts.quadratic[2]);

	// Final quadratic polynomial
	float c = vN.x*vN.x - vN.y*vN.y;
	color.R += consts.quadraticRGB.x * c;
	color.G += consts.quadraticRGB.y * c;
	color.B += consts.quadraticRGB.z * c;

	color.A = 1.0f;

//   if ( color.R < 0.f ) color.R = 0.f;
//   if ( color.G < 0.f ) color.G = 0.f;
//   if ( color.B < 0.f ) color.B = 0.f;
}

void EvaluateSHCoeffsForDirLight(const HDRColor &color, const CVec3 &dir, SHCoeffs &coeffs)
{
	if( !Renderer::Get()->IsRunningUnderPerfHUD() // Hack, coz PerfHUD can't live with this function
      && !Compatibility::IsRunnedUnderWine() )
	{
		D3DXSHEvalDirectionalLight(3, (const D3DXVECTOR3*)(&dir), color.R, color.G, color.B, 
													  	 coeffs.RCoeffs(), coeffs.GCoeffs(), coeffs.BCoeffs());
	}
	else
	{
		memset(&coeffs, 0, sizeof(SHCoeffs));
		coeffs.RCoeffs()[0] = color.R;
		coeffs.GCoeffs()[0] = color.B;
		coeffs.BCoeffs()[0] = color.G;
	}
}

}