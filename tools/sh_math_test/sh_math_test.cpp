// sh_math_test — оракул для замены D3DX-математики своими реализациями
// (этап 1 плана docs/PLAN_NVRHI.md: убрать зависимость от d3dx9_43.dll).
//
// Печатает то, что реально выдаёт d3dx9_43.dll (настоящая MS-библиотека,
// x64 из DirectX End-User Runtime JUN2010), восстанавливает базис и
// сравнивает кандидатскую реализацию. Переключать вызов в клиенте можно
// только когда расхождение кандидата равно нулю (иначе плывёт картинка).
//
// Сборка/запуск: ./run.sh (Wine + WINEDLLOVERRIDES=d3dx9_43=n -> настоящий
// MS d3dx9_43.dll, а не builtin Wine, у которого математика своя).

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "Vendor/DirectX/Include/d3dx9.h"

// Мономиальный базис (порядок индексов D3DX):
//   0:(l0,m0)=1   1:(1,-1)=y  2:(1,0)=z  3:(1,1)=x
//   4:(2,-2)=xy   5:(2,-1)=yz 6:(2,0)=z^2 7:(2,1)=xz 8:(2,2)=x^2-y^2
static const char *g_names[9] = { "1", "y", "z", "x", "xy", "yz", "z*z", "xz", "x*x-y*y" };

static void Monomials(const float *d, float *m)
{
  const float x = d[0], y = d[1], z = d[2];
  m[0] = 1.f; m[1] = y; m[2] = z; m[3] = x;
  m[4] = x * y; m[5] = y * z; m[6] = z * z; m[7] = x * z; m[8] = x * x - y * y;
}

// ------------------------------------------------------------------ кандидат

// Константы сняты с d3dx9_43.dll (x64). Базис — ортонормированные вещественные
// сферические гармоники; ЗНАК у x и y отрицательный (D3DX работает в левой
// системе координат), l=2,m=0 = (3z^2-1)/2.
// Значения заданы битами, а не десятичными литералами: сняты с
// d3dx9_43.dll (x64) подбором направлений, при которых моном == 1, т.е.
// sh[i] = константа без округления на умножение.
static float BitsF(unsigned b) { float f; memcpy(&f, &b, sizeof(f)); return f; }
static const float c0 = BitsF(0x3F558751);  // 0.834096014  l=0
static const float c1 = BitsF(0x3FB8EBD2);  // 1.444696665  l=1 (x,y со знаком -)
static const float c2 = BitsF(0x404EBF89);  // 3.230440378  l=2: xy, yz, xz
static const float c3 = BitsF(0x3FEEBB70);  // 1.865095139  l=2, m=0: (3z^2-1)/2
static const float c4 = BitsF(0x3FCEBF88);  // 1.615220070  l=2: x^2-y^2

// mode 0: (c*comp)*I   mode 1: c*(comp*I)   mode 2: (c*I)*comp
static void EvalDirLightSH(int mode, const float *d, float I, float *sh)
{
  const float x = d[0], y = d[1], z = d[2];
  const float q6 = 0.5f * (3.f * z * z - 1.f);
  const float q8 = x * x - y * y;

  if (mode == 0)
  {
    sh[0] = c0 * I;
    sh[1] = (-c1 * y) * I; sh[2] = (c1 * z) * I; sh[3] = (-c1 * x) * I;
    sh[4] = ((c2 * x) * y) * I; sh[5] = ((-c2 * y) * z) * I;
    sh[6] = (c3 * q6) * I; sh[7] = ((-c2 * x) * z) * I;
    sh[8] = (c4 * q8) * I;
  }
  else if (mode == 1)
  {
    sh[0] = c0 * I;
    sh[1] = -c1 * (y * I); sh[2] = c1 * (z * I); sh[3] = -c1 * (x * I);
    sh[4] = c2 * (x * (y * I)); sh[5] = -c2 * (y * (z * I));
    sh[6] = c3 * (q6 * I); sh[7] = -c2 * (x * (z * I));
    sh[8] = c4 * (q8 * I);
  }
  else
  {
    sh[0] = c0 * I;
    sh[1] = (-c1 * I) * y; sh[2] = (c1 * I) * z; sh[3] = (-c1 * I) * x;
    sh[4] = (c2 * I) * (x * y); sh[5] = (-c2 * I) * (y * z);
    sh[6] = (c3 * I) * q6; sh[7] = (-c2 * I) * (x * z);
    sh[8] = (c4 * I) * q8;
  }
}

// ------------------------------------------------------------------ main

int main()
{
  const float dirs[][3] = {
    { 0.f, 0.f, 1.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f },
    { 0.57735026919f, 0.57735026919f, 0.57735026919f },
    { 0.371f, -0.902f, 0.214f },
  };

  printf("== D3DXSHEvalDirectionalLight(order=3, R=1,G=0,B=0)\n");
  for (size_t d = 0; d < sizeof(dirs)/sizeof(dirs[0]); ++d)
  {
    float shR[9] = {}, shG[9] = {}, shB[9] = {};
    const HRESULT hr = D3DXSHEvalDirectionalLight(3, (const D3DXVECTOR3*)&dirs[d], 1.f, 0.f, 0.f,
                                                  shR, shG, shB);
    printf("dir=(% .7f,% .7f,% .7f) hr=0x%08X\n", dirs[d][0], dirs[d][1], dirs[d][2], (unsigned)hr);
    float mono[9];
    Monomials(dirs[d], mono);
    for (int i = 0; i < 9; ++i)
      printf("   [%d] %-9s sh=%13.8f  mono=%9.6f\n", i, g_names[i], shR[i], mono[i]);
  }

  // Точные биты констант: подбираем направления так, чтобы моном был ровно 1
  // (тогда sh[i] = константа без округления на умножение).
  printf("\n== точные биты констант d3dx9\n");
  {
    struct Case { const char *name; float dir[3]; int idx; float mul; };
    const Case cases[] = {
      { "c0 (l=0)",        { 0.f, 0.f, 1.f }, 0, 1.f },
      { "c1 (l=1, z)",     { 0.f, 0.f, 1.f }, 2, 1.f },
      { "c1 (l=1, x)",     { 1.f, 0.f, 0.f }, 3, -1.f },
      { "c3 (l=2,m=0)",    { 0.f, 0.f, 1.f }, 6, 1.f },
      { "c4 (l=2, x2-y2)", { 1.f, 0.f, 0.f }, 8, 1.f },
      { "c2 (l=2, xy)",    { 0.6f, 0.8f, 0.f }, 4, 1.f / 0.48f },
    };
    for (const Case &cs : cases)
    {
      float sh[9] = {};
      D3DXSHEvalDirectionalLight(3, (const D3DXVECTOR3*)cs.dir, 1.f, 0.f, 0.f, sh, 0, 0);
      const float v = sh[cs.idx] * cs.mul;
      unsigned bits = 0;
      memcpy(&bits, &v, sizeof(bits));
      printf("   %-16s sh=%-14.9g как float=0x%08X  (10 знач. %%.17g: %.17g)\n",
             cs.name, v, bits, (double)v);
    }
  }

  printf("\n== кандидат vs d3dx9: 400 случайных направлений, I случайна\n");
  {
    srand(12345);
    float maxDiff[3] = { 0.f, 0.f, 0.f };
    float maxRel[3]  = { 0.f, 0.f, 0.f };
    for (int t = 0; t < 400; ++t)
    {
      float d[3];
      do {
        d[0] = 2.f * (rand() / (float)RAND_MAX) - 1.f;
        d[1] = 2.f * (rand() / (float)RAND_MAX) - 1.f;
        d[2] = 2.f * (rand() / (float)RAND_MAX) - 1.f;
      } while (d[0]*d[0] + d[1]*d[1] + d[2]*d[2] < 0.05f);
      const float inv = 1.f / sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
      d[0] *= inv; d[1] *= inv; d[2] *= inv;

      const float I = 0.1f + 3.7f * (rand() / (float)RAND_MAX);
      float ref[9] = {};
      D3DXSHEvalDirectionalLight(3, (const D3DXVECTOR3*)d, I, 0.f, 0.f, ref, 0, 0);

      for (int m = 0; m < 3; ++m)
      {
        float mine[9];
        EvalDirLightSH(m, d, I, mine);
        for (int i = 0; i < 9; ++i)
        {
          const float df = fabsf(ref[i] - mine[i]);
          if (df > maxDiff[m]) maxDiff[m] = df;
          const float scale = fabsf(ref[i]) > 1e-4f ? fabsf(ref[i]) : 1e-4f;
          if (df / scale > maxRel[m]) maxRel[m] = df / scale;
        }
      }
    }
    printf("   mode0 (c*comp)*I : maxAbs=%.9g  maxRel=%.9g\n", maxDiff[0], maxRel[0]);
    printf("   mode1 c*(comp*I) : maxAbs=%.9g  maxRel=%.9g\n", maxDiff[1], maxRel[1]);
    printf("   mode2 (c*I)*comp : maxAbs=%.9g  maxRel=%.9g\n", maxDiff[2], maxRel[2]);
  }

  printf("\n== D3DXMatrixInverse\n");
  {
    for (int t = 0; t < 4; ++t)
    {
      float m[16];
      for (int i = 0; i < 16; ++i)
        m[i] = (float)((i * 7 + t * 13) % 11) * 0.25f - 1.0f;
      m[15] = 1.f;
      if (t == 1) { m[0] = 2.f; m[5] = 3.f; m[10] = 4.f; }
      if (t == 2) { m[12] = 5.f; m[13] = -2.f; m[14] = 7.f; }
      if (t == 3) { m[1] = 0.3f; m[6] = -0.7f; m[9] = 0.9f; }

      D3DXMATRIX mm, inv;
      FLOAT det = 0.f;
      memcpy(&mm, m, sizeof(m));
      const D3DXMATRIX *r = D3DXMatrixInverse(&inv, &det, &mm);
      printf("case %d det=%.9f ret=%s\n", t, det, r == &inv ? "ok" : "NULL");
      for (int row = 0; row < 4; ++row)
        printf("   %13.9f %13.9f %13.9f %13.9f\n", inv(row,0), inv(row,1), inv(row,2), inv(row,3));
    }
  }
  return 0;
}
