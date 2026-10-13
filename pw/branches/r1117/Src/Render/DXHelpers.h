#pragma once
#include "ShaderInitializers.h"

namespace Render
{




class CubeRenderer
{
  static const UINT numVerts = 8;
  static const UINT numPrims = 12;

  static const WORD* GetIndices()
  {
    static const WORD indices[numPrims * 3] = { 0, 2, 1,
      0, 3, 2,
      3, 7, 2,
      3, 4, 7,
      3, 5, 4,
      5, 7, 4,
      5, 6, 7,
      6, 2, 7,
      2, 6, 1,
      0, 5, 3,
      0, 1, 5,
      5, 1, 6 };
    return indices;
  }

  struct CubeIBInit : public IntrusivePtrDeleter
  {
    typedef DXIndexBufferRef Type;

    void Init(Type &p)
    {
      p.Attach( CreateIB16(numPrims * 3 * sizeof(WORD), RENDER_POOL_MANAGED, GetIndices()).Detach() );
    }
  };

  struct CubeVBInit : public IntrusivePtrDeleter
  {
    typedef DXVertexBufferRef Type;

    static void Init(Type &p)
    {
      static const CVec3 Vertices[numVerts] = { CVec3(-1.0f,-1.0f,-1.0f),
                                                CVec3( 1.0f,-1.0f,-1.0f),
                                                CVec3( 1.0f, 1.0f,-1.0f),
                                                CVec3(-1.0f, 1.0f,-1.0f),
                                                CVec3(-1.0f, 1.0f, 1.0f),
                                                CVec3(-1.0f,-1.0f, 1.0f),
                                                CVec3( 1.0f,-1.0f, 1.0f),
                                                CVec3( 1.0f, 1.0f, 1.0f), };

      p.Attach( CreateVB(sizeof(Vertices), RENDER_POOL_MANAGED, Vertices).Detach() );
    }
  };

  static IDirect3DVertexBuffer9* GetVB()
  {
    static ManagedResource<CubeVBInit> vb;

    return vb.Get();
  }

  static IDirect3DIndexBuffer9* GetIB()
  {
    static ManagedResource<CubeIBInit> ib;

    return ib.Get();
  }

public:
  static void Bind()
  {
    SmartRenderer::BindVertexBuffer( 0, GetVB(), sizeof(CVec3) );
    SmartRenderer::BindIndexBuffer( GetIB() );
  }

  static void Draw()
  {
    static const DipDescriptor desc(RENDERPRIMITIVE_TRIANGLELIST, 0, 0, numVerts, 0, numPrims);

    SmartRenderer::DrawIndexedPrimitive(desc);
  }

  static void DrawIndexedPrimitiveUP(CVec3 (&_vertices)[numVerts])
  {
    static const DipDescriptor desc(RENDERPRIMITIVE_TRIANGLELIST, 0, 0, numVerts, 0, numPrims);

    SmartRenderer::DrawIndexedPrimitiveUP( desc, GetIndices(), _vertices, sizeof(CVec3) );
  }
};

} // namespace Render
