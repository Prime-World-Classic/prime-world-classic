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
	static const float s_fSqrtPI = ((float)sqrtf(3.14159265358979323846f)); // = D3DX_PI
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

namespace
{
  // Константы сняты битами с настоящей d3dx9_43.dll (x64) — базис, знаки и
  // значения восстанавливает прогон tools/sh_math_test (README там же).
  // Базис: ортонормированные вещественные сферические гармоники в порядке
  // индексов D3DX; ЗНАК у x и y отрицательный (D3DX — левая система
  // координат), l=2,m=0 = (3z^2-1)/2.
  inline float FBits(unsigned b) { float f; memcpy(&f, &b, sizeof(f)); return f; }

  void EvalDirLightSH(const CVec3 &dir, float I, float *sh)
  {
    const float c0 = FBits(0x3F558751);  // 0.834096014
    const float c1 = FBits(0x3FB8EBD2);  // 1.444696665
    const float c2 = FBits(0x404EBF89);  // 3.230440378
    const float c3 = FBits(0x3FEEBB70);  // 1.865095139
    const float c4 = FBits(0x3FCEBF88);  // 1.615220070

    const float x = dir.x, y = dir.y, z = dir.z;
    sh[0] = c0 * I;
    sh[1] = (-c1 * y) * I;
    sh[2] = ( c1 * z) * I;
    sh[3] = (-c1 * x) * I;
    sh[4] = (( c2 * x) * y) * I;
    sh[5] = ((-c2 * y) * z) * I;
    sh[6] = (c3 * (0.5f * (3.0f * z * z - 1.0f))) * I;
    sh[7] = ((-c2 * x) * z) * I;
    sh[8] = (c4 * (x * x - y * y)) * I;
  }
}

void EvaluateSHCoeffsForDirLight(const HDRColor &color, const CVec3 &dir, SHCoeffs &coeffs)
{
  // D3DXSHEvalDirectionalLight убрана. Причина не только в зависимости от
  // d3dx9_математики: её же Wine/PerfHUD-фолбэк писал только DC-член и по
  // пути менял каналы местами (G<->B), т.е. под Wine и под нативным Windows
  // свет считался по-разному. Теперь считается одинаково. Точность замены
  // измерена: maxAbs = 9.5e-7 (~1 ULP), maxRel = 4.3e-6 только на вычитании
  // в члене (3z^2-1)/2 вблизи нуля.
  EvalDirLightSH(dir, color.R, coeffs.RCoeffs());
  EvalDirLightSH(dir, color.G, coeffs.GCoeffs());
  EvalDirLightSH(dir, color.B, coeffs.BCoeffs());
}

}