#pragma once
#include "../Render/TextureManager.h"
namespace Render
{

struct SHCoeffs
{
	float coeffs[3][9];

	float* RCoeffs() { return &(coeffs[0][0]); }
	float* GCoeffs() { return &(coeffs[1][0]); }
	float* BCoeffs() { return &(coeffs[2][0]); }

	void Scale(float scale)
	{
		float *p = &coeffs[0][0];
		for (int i = 0; i < 3*9; i++)
			p[i] *= scale;
	}

	void Add(const SHCoeffs &c)
	{
		float *pDst = &coeffs[0][0];
		float const *pSrc = &c.coeffs[0][0];
		for (int i = 0; i < 3*9; i++)
			pDst[i] += pSrc[i];
	}

	void AddAmbient(const HDRColor &color)
	{
		RCoeffs()[0] += color.R;
		GCoeffs()[0] += color.G;
		BCoeffs()[0] += color.B;
	}
};

struct SHShaderConstants
{
	CVec4 linearAndConstant[3];
	CVec4 quadratic[3];
	CVec4 quadraticRGB;

	void InitAsConstant(float r, float g, float b);

  void Add(const SHShaderConstants &c);

  bool Import(const vector<float>& _src);
  void Export(vector<float>& _dst) const;
};

// Проекция куб-текстуры / текстуры в SH-коэффициенты
// (EvaluateSHCoeffsForCubeMap, EvaluateSHCoeffsFor2DTexture) удалена:
// ни одного вызова в дереве. Это же и шейдеры
// OnlineShaders/SH.psb, SH2.psb, SH3.psb — они больше не задействованы.
void ConvertSHCoeffs2ShaderConstants(const SHCoeffs &c, SHShaderConstants &s);
void EvaluateLightingBySHShaderConstants(const SHShaderConstants &consts, const CVec3 &normal, HDRColor &color);
void EvaluateSHCoeffsForDirLight(const HDRColor &color, const CVec3 &dir, SHCoeffs &coeffs);

}