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


// ------------------------------------------------------------------ математика

// D3DMATRIX в этом заголовке хранит m[4][4], а не m[16] — линейный доступ
// через ML(), иначе «array type 'float [4]' is not assignable».
static float *ML(D3DXMATRIX *p) { return &p->m[0][0]; }
static const float *ML(const D3DXMATRIX *p) { return &p->m[0][0]; }

// Кандидаты на замену d3dx9_43.dll. Смысл раздела: померить, совпадает ли
// кандидата с настоящей MS-библиотекой ПОБИТОВО. Менять вызов в клиенте можно
// только для тех функций, где diff == 0 (иначе плывёт картинка, а на x64 у нас
// ещё и инвариант FP-детерминизма).

static void pwMatrixMultiply(D3DXMATRIX *o, const D3DXMATRIX *a, const D3DXMATRIX *b)
{
  const float *A = ML(a), *B = ML(b);
  float *O = ML(o);
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      O[r*4+c] = A[r*4+0]*B[0*4+c] + A[r*4+1]*B[1*4+c] + A[r*4+2]*B[2*4+c] + A[r*4+3]*B[3*4+c];
}

static D3DXMATRIX *pwMatrixTranspose(D3DXMATRIX *o, const D3DXMATRIX *m)
{
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      ML(o)[r*4+c] = ML(m)[c*4+r];
  return o;
}

static D3DXMATRIX *pwMatrixTranslation(D3DXMATRIX *o, float x, float y, float z)
{
  memset(ML(o), 0, 16*sizeof(float));
  ML(o)[0] = ML(o)[5] = ML(o)[10] = ML(o)[15] = 1.f;
  ML(o)[12] = x; ML(o)[13] = y; ML(o)[14] = z;
  return o;
}

static D3DXMATRIX *pwMatrixOrthoLH(D3DXMATRIX *o, float w, float h, float zn, float zf)
{
  memset(ML(o), 0, 16*sizeof(float));
  // снято с DLL: масштаб 2/w, 2/h (не 1/w), _43 = -zn/(zf-zn)
  ML(o)[0] = 2.f/w; ML(o)[5] = 2.f/h; ML(o)[10] = 1.f/(zf-zn);
  ML(o)[14] = -zn/(zf-zn); ML(o)[15] = 1.f;
  return o;
}

static D3DXMATRIX *pwMatrixOrthoOffCenterLH(D3DXMATRIX *o, float l, float r, float b, float t, float zn, float zf)
{
  memset(ML(o), 0, 16*sizeof(float));
  // порядок параметров в заголовке — (l, r, b, t), а не (l, b, r, t)
  ML(o)[0] = 2.f/(r-l); ML(o)[5] = 2.f/(t-b); ML(o)[10] = 1.f/(zf-zn);
  ML(o)[12] = -(l+r)/(r-l); ML(o)[13] = -(b+t)/(t-b); ML(o)[14] = -zn/(zf-zn); ML(o)[15] = 1.f;
  return o;
}

static D3DXMATRIX *pwMatrixPerspectiveLH(D3DXMATRIX *o, float w, float h, float zn, float zf)
{
  memset(ML(o), 0, 16*sizeof(float));
  // снято с DLL: _33 = zf/(zf-zn), _43 = -zf*zn/(zf-zn) (без множителя 2)
  ML(o)[0] = 2.f*zn/w; ML(o)[5] = 2.f*zn/h;
  ML(o)[10] = zf/(zf-zn); ML(o)[11] = 1.f;
  ML(o)[14] = -zf*zn/(zf-zn);
  return o;
}

static D3DXVECTOR3 *pwVec3Normalize(D3DXVECTOR3 *o, const D3DXVECTOR3 *v)
{
  const float l2 = v->x*v->x + v->y*v->y + v->z*v->z;
  const float inv = 1.f / sqrtf(l2);
  o->x = v->x*inv; o->y = v->y*inv; o->z = v->z*inv;
  return o;
}

static D3DXVECTOR3 *pwVec3TransformCoord(D3DXVECTOR3 *o, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
  const float x = v->x, y = v->y, z = v->z;
  const float w = x*ML(m)[3] + y*ML(m)[7] + z*ML(m)[11] + ML(m)[15];
  o->x = (x*ML(m)[0] + y*ML(m)[4] + z*ML(m)[8]  + ML(m)[12]) / w;
  o->y = (x*ML(m)[1] + y*ML(m)[5] + z*ML(m)[9]  + ML(m)[13]) / w;
  o->z = (x*ML(m)[2] + y*ML(m)[6] + z*ML(m)[10] + ML(m)[14]) / w;
  return o;
}

static D3DXMATRIX *pwMatrixLookAtLH(D3DXMATRIX *o, const D3DXVECTOR3 *eye, const D3DXVECTOR3 *at, const D3DXVECTOR3 *up)
{
  D3DXVECTOR3 z, x, y;
  z.x = at->x - eye->x; z.y = at->y - eye->y; z.z = at->z - eye->z;
  pwVec3Normalize(&z, &z);
  x.x = up->y*z.z - up->z*z.y; x.y = up->z*z.x - up->x*z.z; x.z = up->x*z.y - up->y*z.x;
  pwVec3Normalize(&x, &x);
  y.x = z.y*x.z - z.z*x.y; y.y = z.z*x.x - z.x*x.z; y.z = z.x*x.y - z.y*x.x;
  ML(o)[0] = x.x; ML(o)[1] = y.x; ML(o)[2] = z.x; ML(o)[3] = 0.f;
  ML(o)[4] = x.y; ML(o)[5] = y.y; ML(o)[6] = z.y; ML(o)[7] = 0.f;
  ML(o)[8] = x.z; ML(o)[9] = y.z; ML(o)[10] = z.z; ML(o)[11] = 0.f;
  ML(o)[12] = -(x.x*eye->x + x.y*eye->y + x.z*eye->z);
  ML(o)[13] = -(y.x*eye->x + y.y*eye->y + y.z*eye->z);
  ML(o)[14] = -(z.x*eye->x + z.y*eye->y + z.z*eye->z);
  ML(o)[15] = 1.f;
  return o;
}

// Классическое разложение D3DX (кофакторы 2x2 попарно) — порядок операций
// подобран так, чтобы совпасть побитово; проверяется этим же тестом.
static D3DXMATRIX *pwMatrixInverse(D3DXMATRIX *o, float *pDet, const D3DXMATRIX *mm)
{
  const float *m = ML(mm);
  const float A00 = m[0]*m[5]  - m[1]*m[4];
  const float A01 = m[0]*m[9]  - m[1]*m[8];
  const float A02 = m[0]*m[13] - m[1]*m[12];
  const float A03 = m[4]*m[9]  - m[5]*m[8];
  const float A04 = m[4]*m[13] - m[5]*m[12];
  const float A05 = m[8]*m[13] - m[9]*m[12];
  const float A06 = m[2]*m[7]  - m[3]*m[6];
  const float A07 = m[2]*m[11] - m[3]*m[10];
  const float A08 = m[2]*m[15] - m[3]*m[14];
  const float A09 = m[6]*m[11] - m[7]*m[10];
  const float A10 = m[6]*m[15] - m[7]*m[14];
  const float A11 = m[10]*m[15] - m[11]*m[14];
  float det = A00*A11 - A01*A10 + A02*A09 + A03*A08 - A04*A07 + A05*A06;
  if (pDet) *pDet = det;
  if (det == 0.f) { memset(ML(o), 0, 16*sizeof(float)); return o; }
  det = 1.f / det;
  ML(o)[0]  = ( m[5]*A11 - m[9]*A10 + m[13]*A09) * det;
  ML(o)[1]  = (-m[1]*A11 + m[9]*A08 - m[13]*A07) * det;
  ML(o)[2]  = ( m[1]*A10 - m[5]*A08 + m[13]*A06) * det;
  ML(o)[3]  = (-m[1]*A09 + m[5]*A07 - m[9]*A06) * det;
  ML(o)[4]  = (-m[4]*A11 + m[8]*A10 - m[12]*A09) * det;
  ML(o)[5]  = ( m[0]*A11 - m[8]*A08 + m[12]*A07) * det;
  ML(o)[6]  = (-m[0]*A10 + m[4]*A08 - m[12]*A06) * det;
  ML(o)[7]  = ( m[0]*A09 - m[4]*A07 + m[8]*A06) * det;
  ML(o)[8]  = ( m[7]*A05 - m[11]*A04 + m[15]*A03) * det;
  ML(o)[9]  = (-m[3]*A05 + m[11]*A02 - m[15]*A01) * det;
  ML(o)[10] = ( m[3]*A04 - m[7]*A02 + m[15]*A00) * det;
  ML(o)[11] = (-m[3]*A03 + m[7]*A01 - m[11]*A00) * det;
  ML(o)[12] = (-m[6]*A05 + m[10]*A04 - m[14]*A03) * det;
  ML(o)[13] = ( m[2]*A05 - m[10]*A02 + m[14]*A01) * det;
  ML(o)[14] = (-m[2]*A04 + m[6]*A02 - m[14]*A00) * det;
  ML(o)[15] = ( m[2]*A03 - m[6]*A01 + m[10]*A00) * det;
  return o;
}


// Второй кандидат на Inverse: прямой кофакторный метод (миноры 3x3), без
// «хитрой» группировки D3DX. Смысл — отделить ошибку формулы от ошибки
// порядка операций.
static D3DXMATRIX *pwInverseCofactor(D3DXMATRIX *o, float *pDet, const D3DXMATRIX *mm)
{
  const float *m = ML(mm);
  float cof[16];
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
    {
      int i1[3], i2[3], i3[3];
      { int k = 0; for (int i = 0; i < 4; ++i) if (i != r) i1[k++] = i; }
      { int k = 0; for (int i = 0; i < 4; ++i) if (i != c) i2[k++] = i; }
      const float a = m[i1[0]*4+i2[0]], b = m[i1[0]*4+i2[1]], cc = m[i1[0]*4+i2[2]];
      const float d = m[i1[1]*4+i2[0]], e = m[i1[1]*4+i2[1]], f = m[i1[1]*4+i2[2]];
      const float g = m[i1[2]*4+i2[0]], h = m[i1[2]*4+i2[1]], i = m[i1[2]*4+i2[2]];
      const float minor = a*(e*i - f*h) - b*(d*i - f*g) + cc*(d*h - e*g);
      cof[r*4+c] = ((r + c) & 1) ? -minor : minor;
    }
  float det = m[0]*cof[0] + m[1]*cof[1] + m[2]*cof[2] + m[3]*cof[3];
  if (pDet) *pDet = det;
  if (det == 0.f) { memset(ML(o), 0, 16*sizeof(float)); return o; }
  const float inv = 1.f / det;
  // присоединённая матрица — транспонированная матрица кофакторов
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      ML(o)[r*4+c] = cof[c*4+r] * inv;
  return o;
}

static void CmpMats(const char *name, const D3DXMATRIX &ref, const D3DXMATRIX &mine,
                    float *maxAbs, unsigned *badCnt)
{
  for (int i = 0; i < 16; ++i)
  {
    const float d = fabsf(ML(&ref)[i] - ML(&mine)[i]);
    if (d > *maxAbs) *maxAbs = d;
    if (memcmp(&ML(&ref)[i], &ML(&mine)[i], 4) != 0) ++*badCnt;
  }
}

static void CmpVecs(const char *name, const D3DXVECTOR3 &ref, const D3DXVECTOR3 &mine,
                    float *maxAbs, unsigned *badCnt)
{
  const float *a = (const float*)&ref, *b = (const float*)&mine;
  for (int i = 0; i < 3; ++i)
  {
    const float d = fabsf(a[i] - b[i]);
    if (d > *maxAbs) *maxAbs = d;
    if (memcmp(&a[i], &b[i], 4) != 0) ++*badCnt;
  }
}

static float Rnd(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }

static void MathSection()
{
  printf("\n== D3DX-математика: кандидат vs d3dx9_43 (1000 случайных входов)\n");
  srand(20261013);
  struct Row { const char *name; float maxAbs; unsigned bad, total; };
  Row rows[] = {
    {"D3DXMatrixMultiply", 0.f, 0, 0}, {"D3DXMatrixTranspose", 0.f, 0, 0},
    {"D3DXMatrixTranslation", 0.f, 0, 0}, {"D3DXMatrixInverse", 0.f, 0, 0},
    {"D3DXMatrixLookAtLH", 0.f, 0, 0}, {"D3DXMatrixOrthoLH", 0.f, 0, 0},
    {"D3DXMatrixOrthoOffCenterLH", 0.f, 0, 0}, {"D3DXMatrixPerspectiveLH", 0.f, 0, 0},
    {"D3DXVec3Normalize", 0.f, 0, 0}, {"D3DXVec3TransformCoord", 0.f, 0, 0},
    {"D3DXMatrixInverse (кофакторы 3x3)", 0.f, 0, 0},
  };
  for (int t = 0; t < 1000; ++t)
  {
    D3DXMATRIX A, B, mRef, mMine;
    for (int i = 0; i < 16; ++i) { ML(&A)[i] = Rnd(-4.f, 4.f); ML(&B)[i] = Rnd(-4.f, 4.f); }

    D3DXMatrixMultiply(&mRef, &A, &B); pwMatrixMultiply(&mMine, &A, &B);
    CmpMats(rows[0].name, mRef, mMine, &rows[0].maxAbs, &rows[0].bad); rows[0].total += 16;

    D3DXMatrixTranspose(&mRef, &A); pwMatrixTranspose(&mMine, &A);
    CmpMats(rows[1].name, mRef, mMine, &rows[1].maxAbs, &rows[1].bad); rows[1].total += 16;

    D3DXMatrixTranslation(&mRef, Rnd(-9,9), Rnd(-9,9), Rnd(-9,9));
    pwMatrixTranslation(&mMine, ((const float*)&mRef)[12], ((const float*)&mRef)[13], ((const float*)&mRef)[14]);
    CmpMats(rows[2].name, mRef, mMine, &rows[2].maxAbs, &rows[2].bad); rows[2].total += 16;

    // Inverse мёрится на хорошо обусловленных матрицах (сдвиг*поворот), как в
    // клиенте: на случайных 4x4 детерминант близок к нулю, и инверсия
    // legitimate усиливает последние биты — мерить на них бессмысленно.
    {
      D3DXMATRIX T, R;
      const float ca = Rnd(-1.f, 1.f), sa = sqrtf(1.f - ca*ca);
      memset(ML(&R), 0, 16*sizeof(float));
      ML(&R)[0] = ca;  ML(&R)[2] = -sa; ML(&R)[5] = 1.f;
      ML(&R)[8] = sa;  ML(&R)[10] = ca; ML(&R)[15] = 1.f;
      D3DXMatrixTranslation(&T, Rnd(-100,100), Rnd(-100,100), Rnd(-100,100));
      D3DXMatrixMultiply(&A, &R, &T);
    }
    D3DXMatrixInverse(&mRef, 0, &A); pwMatrixInverse(&mMine, 0, &A);
    CmpMats(rows[3].name, mRef, mMine, &rows[3].maxAbs, &rows[3].bad); rows[3].total += 16;
    pwInverseCofactor(&mMine, 0, &A);
    CmpMats("D3DXMatrixInverse (кофакторы 3x3)", mRef, mMine, &rows[10].maxAbs, &rows[10].bad); rows[10].total += 16;

    D3DXVECTOR3 eye(Rnd(-20,20), Rnd(-20,20), Rnd(-20,20)), at(Rnd(-20,20), Rnd(-20,20), Rnd(-20,20)),
                up(Rnd(-1,1), Rnd(0.2f,1.f), Rnd(-1,1));
    D3DXMatrixLookAtLH(&mRef, &eye, &at, &up); pwMatrixLookAtLH(&mMine, &eye, &at, &up);
    CmpMats(rows[4].name, mRef, mMine, &rows[4].maxAbs, &rows[4].bad); rows[4].total += 16;

    const float w = Rnd(0.5f, 50.f), h = Rnd(0.5f, 50.f), zn = Rnd(0.01f, 5.f), zf = zn + Rnd(1.f, 200.f);
    D3DXMatrixOrthoLH(&mRef, w, h, zn, zf); pwMatrixOrthoLH(&mMine, w, h, zn, zf);
    CmpMats(rows[5].name, mRef, mMine, &rows[5].maxAbs, &rows[5].bad); rows[5].total += 16;

    const float l = Rnd(-30,0), b = Rnd(-30,0), r = l + Rnd(1,60), tt = b + Rnd(1,60);
    D3DXMatrixOrthoOffCenterLH(&mRef, l, r, b, tt, zn, zf); pwMatrixOrthoOffCenterLH(&mMine, l, r, b, tt, zn, zf);
    CmpMats(rows[6].name, mRef, mMine, &rows[6].maxAbs, &rows[6].bad); rows[6].total += 16;

    D3DXMatrixPerspectiveLH(&mRef, w, h, zn, zf); pwMatrixPerspectiveLH(&mMine, w, h, zn, zf);
    CmpMats(rows[7].name, mRef, mMine, &rows[7].maxAbs, &rows[7].bad); rows[7].total += 16;

    D3DXVECTOR3 v(Rnd(-100,100), Rnd(-100,100), Rnd(-100,100)), r1, r2;
    D3DXVec3Normalize(&r1, &v); pwVec3Normalize(&r2, &v);
    CmpVecs(rows[8].name, r1, r2, &rows[8].maxAbs, &rows[8].bad); rows[8].total += 3;

    D3DXVec3TransformCoord(&r1, &v, &A); pwVec3TransformCoord(&r2, &v, &A);
    CmpVecs(rows[9].name, r1, r2, &rows[9].maxAbs, &rows[9].bad); rows[9].total += 3;
  }
  for (unsigned i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i)
    printf("   %-26s max|d|=%-12.9g побитово не совпало: %u/%u %s\n",
           rows[i].name, rows[i].maxAbs, rows[i].bad, rows[i].total,
           rows[i].bad == 0 ? "  <- можно менять побитово безопасно" : "");
}


// ------------------------------------------------------------------ вывод формул
// Свободные D3DX-функции не инлайнятся (в d3dx9math.inl только операторы
// класса), поэтому формулу нельзя скопировать из заголовка — её снимают этим
// разделом: фиксированные простые входы, по которым формула читается глазами.

static void PrintM(const char *name, const D3DXMATRIX &m)
{
  printf("   %s\n", name);
  for (int r = 0; r < 4; ++r)
    printf("     %12.7f %12.7f %12.7f %12.7f\n", ML(&m)[r*4], ML(&m)[r*4+1], ML(&m)[r*4+2], ML(&m)[r*4+3]);
}

static void FormulasSection()
{
  printf("\n== формулы d3dx9 по фиксированным входам\n");
  D3DXMATRIX m;
  D3DXMatrixOrthoLH(&m, 2.f, 4.f, 1.f, 7.f);            PrintM("OrthoLH(w=2,h=4,zn=1,zf=7)", m);
  D3DXMatrixOrthoOffCenterLH(&m, -1.f, 3.f, -2.f, 5.f, 1.f, 7.f); PrintM("OrthoOffCenterLH(l=-1,r=3,b=-2,t=5,zn=1,zf=7)", m);
  D3DXMatrixPerspectiveLH(&m, 2.f, 4.f, 1.f, 7.f);      PrintM("PerspectiveLH(w=2,h=4,zn=1,zf=7)", m);
  {
    D3DXMATRIX a;
    for (int i = 0; i < 16; ++i) ML(&a)[i] = (float)(i + 1) * 0.1f;
    ML(&a)[0] = 1.f; ML(&a)[5] = 2.f; ML(&a)[10] = 3.f; ML(&a)[15] = 1.f;
    float det = 0.f;
    D3DXMatrixInverse(&m, &det, &a);
    PrintM("Inverse(a = 0.1..1.6, диагональ 1,2,3,1)", m);
    printf("     det DLL = %.9f   det кандидата = %.9f\n", det, [&]{ float d = 0.f; pwMatrixInverse(&m, &d, &a); return d; }());
  }
  {
    D3DXVECTOR3 eye(1.f, 2.f, 3.f), at(0.f, 0.f, 0.f), up(0.f, 1.f, 0.f);
    D3DXMatrixLookAtLH(&m, &eye, &at, &up);             PrintM("LookAtLH(eye=1,2,3 at=0,0,0 up=0,1,0)", m);
  }

  printf("\n== варианты кандидатов (2000 случайных входов, ищем побитовое совпадение)\n");
  srand(777);
  struct Row { const char *name; unsigned bad, total; float maxAbs; };
  Row rows[] = { {"Multiply: построчно слева направо", 0, 0, 0.f},
                 {"Multiply: попарно ((a0b0+a1b1)+(a2b2+a3b3))", 0, 0, 0.f},
                 {"Vec3Normalize: x*inv(sqrt(l2))", 0, 0, 0.f},
                 {"Vec3Normalize: x/sqrt(l2)", 0, 0, 0.f},
                 {"Vec3TransformCoord: с делением на w", 0, 0, 0.f} };

  for (int t = 0; t < 2000; ++t)
  {
    D3DXMATRIX A, B, r1, r2;
    for (int i = 0; i < 16; ++i) { ML(&A)[i] = Rnd(-4.f, 4.f); ML(&B)[i] = Rnd(-4.f, 4.f); }

    D3DXMatrixMultiply(&r1, &A, &B);
    // вариант 1 — как в первом разделе
    pwMatrixMultiply(&r2, &A, &B);
    for (int i = 0; i < 16; ++i) { float d = fabsf(ML(&r1)[i]-ML(&r2)[i]); if(d>rows[0].maxAbs) rows[0].maxAbs=d; if(memcmp(&ML(&r1)[i],&ML(&r2)[i],4)) rows[0].bad++; }
    rows[0].total += 16;
    // вариант 2 — попарная группировка
    {
      const float *a = ML(&A), *b = ML(&B); float *o = ML(&r2);
      for (int rr = 0; rr < 4; ++rr)
        for (int cc = 0; cc < 4; ++cc)
          o[rr*4+cc] = (a[rr*4+0]*b[0*4+cc] + a[rr*4+1]*b[1*4+cc]) +
                       (a[rr*4+2]*b[2*4+cc] + a[rr*4+3]*b[3*4+cc]);
    }
    for (int i = 0; i < 16; ++i) { float d = fabsf(ML(&r1)[i]-ML(&r2)[i]); if(d>rows[1].maxAbs) rows[1].maxAbs=d; if(memcmp(&ML(&r1)[i],&ML(&r2)[i],4)) rows[1].bad++; }
    rows[1].total += 16;

    D3DXVECTOR3 v(Rnd(-100,100), Rnd(-100,100), Rnd(-100,100)), u1, u2;
    D3DXVec3Normalize(&u1, &v);
    { const float l2 = v.x*v.x + v.y*v.y + v.z*v.z; const float inv = 1.f/sqrtf(l2);
      u2.x = v.x*inv; u2.y = v.y*inv; u2.z = v.z*inv; }
    for (int i = 0; i < 3; ++i) { float d = fabsf(((float*)&u1)[i]-((float*)&u2)[i]); if(d>rows[2].maxAbs) rows[2].maxAbs=d; if(memcmp(((float*)&u1)+i,((float*)&u2)+i,4)) rows[2].bad++; }
    rows[2].total += 3;
    { const float l2 = v.x*v.x + v.y*v.y + v.z*v.z; const float s = sqrtf(l2);
      u2.x = v.x/s; u2.y = v.y/s; u2.z = v.z/s; }
    for (int i = 0; i < 3; ++i) { float d = fabsf(((float*)&u1)[i]-((float*)&u2)[i]); if(d>rows[3].maxAbs) rows[3].maxAbs=d; if(memcmp(((float*)&u1)+i,((float*)&u2)+i,4)) rows[3].bad++; }
    rows[3].total += 3;

    D3DXVec3TransformCoord(&u1, &v, &A);
    {
      const float x = v.x, y = v.y, z = v.z; const float *mm = ML(&A);
      const float w = x*mm[3] + y*mm[7] + z*mm[11] + mm[15];
      u2.x = (x*mm[0] + y*mm[4] + z*mm[8]  + mm[12]) / w;
      u2.y = (x*mm[1] + y*mm[5] + z*mm[9]  + mm[13]) / w;
      u2.z = (x*mm[2] + y*mm[6] + z*mm[10] + mm[14]) / w;
    }
    for (int i = 0; i < 3; ++i) { float d = fabsf(((float*)&u1)[i]-((float*)&u2)[i]); if(d>rows[4].maxAbs) rows[4].maxAbs=d; if(memcmp(((float*)&u1)+i,((float*)&u2)+i,4)) rows[4].bad++; }
    rows[4].total += 3;
  }
  for (unsigned i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i)
    printf("   %-42s max|d|=%-12.9g не совпало: %u/%u %s\n", rows[i].name, rows[i].maxAbs,
           rows[i].bad, rows[i].total, rows[i].bad == 0 ? "  <- ПОБИТОВО" : "");
}


static void ProbeSection()
{
  printf("\n== пробы: OrthoOffCenterLH и PerspectiveLH (ищем фактическую формулу)\n");
  D3DXMATRIX m;
  D3DXMatrixOrthoOffCenterLH(&m, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f);
  PrintM("OrthoOffCenterLH(0,0,1,1, zn=0,zf=1)", m);
  D3DXMatrixOrthoOffCenterLH(&m, 2.f, 3.f, 5.f, 7.f, 0.f, 10.f);
  PrintM("OrthoOffCenterLH(2,3,5,7, zn=0,zf=10)", m);
  D3DXMatrixPerspectiveLH(&m, 3.f, 5.f, 0.5f, 1000.f);
  PrintM("PerspectiveLH(w=3,h=5,zn=0.5,zf=1000)", m);
  {
    D3DXMATRIX mine, ref;
    pwMatrixPerspectiveLH(&mine, 3.f, 5.f, 0.5f, 1000.f);
    D3DXMatrixPerspectiveLH(&ref, 3.f, 5.f, 0.5f, 1000.f);
    printf("   кандидат PerspectiveLH:\n");
    for (int r = 0; r < 4; ++r)
      printf("     %12.7f %12.7f %12.7f %12.7f\n", ML(&mine)[r*4], ML(&mine)[r*4+1], ML(&mine)[r*4+2], ML(&mine)[r*4+3]);
    (void)ref;
  }
  {
    D3DXMATRIX ref, mine;
    D3DXMatrixOrthoLH(&ref, 3.f, 5.f, 0.5f, 1000.f);
    printf("   OrthoLH(3,5,0.5,1000) из DLL:\n");
    for (int r = 0; r < 4; ++r)
      printf("     %12.7f %12.7f %12.7f %12.7f\n", ML(&ref)[r*4], ML(&ref)[r*4+1], ML(&ref)[r*4+2], ML(&ref)[r*4+3]);
    (void)mine;
  }
}

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

  MathSection();
  FormulasSection();
  ProbeSection();
  return 0;
}
