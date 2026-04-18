#pragma once

#include "primitive.h"

namespace Render
{

class SubMesh : public Primitive
{
public:
  void AddVertexBuffer( const DXVertexBufferRef& pVB, unsigned int stride = 0, unsigned int offset = 0 )
  {
    SetVertexStream( pVB, stride, offset );
  }

  void SetDipDescriptor( const DipDescriptor& dipDescriptor )
  {
    GetDipDescriptor() = dipDescriptor;
  }
};

} // namespace Render
