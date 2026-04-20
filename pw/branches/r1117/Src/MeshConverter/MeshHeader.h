#pragma once

#include "../Render/vertexformatdescriptor.h"
#include "../System/staticstring.h"
#include "../System/staticvector.h"

namespace Signs
{
	static const unsigned statSIGN = 0x54415458;
	static const unsigned skinSIGN = 0x4e494b58;
	static const unsigned skelSIGN = 0x4c454b53;
	static const unsigned animSIGN = 0x4d494e41;
	static const unsigned partSIGN = 0x54524150;
	static const unsigned lghtSIGN = 0x5448474c;
	static const unsigned statSIGNOld = 0x54415453;
	static const unsigned skinSIGNOld = 0x4e494b53;
  static const unsigned telmSIGN = 0x4c455458; // XTEL - terrain element
}

//////////////////////////////////////////////////////////////////////////

struct TerrainElementStride
{
	CVec3 position;
	CVec3 normal;
  Render::Color color;
	Render::Color mask;
	CVec2 texcoord0;
};

struct TerrainElementStrideNew
{
  CVec3 position;
  CVec3 normal;
  CVec2 texcoord0;
  Render::Color color0;
  Render::Color color1;
  Render::Color mask;
};

struct TerrainElementAddonStride
{
	Render::Color color;
};

//////////////////////////////////////////////////////////////////////////
struct H6FragmentHeader
{
	uint32_t vertexStrideSizeInBytes;
	int32_t baseVertexIndex;
	uint32_t minIndex;
	uint32_t numVertices;
	uint32_t startIndex;
	uint32_t primitiveCount;
	uint32_t materialID;
	uint32_t streamOffset;
	StaticVector< Render::VertexElementDescriptor > vertexElementDescriptor;
};
//////////////////////////////////////////////////////////////////////////
struct H6GeometryFileHeader
{
	uint32_t signature;
	uint32_t fragmentCount;
	uint32_t materialCount; 
	//unsigned __int32 commonVertexCount;
	uint32_t commonVertexBufferSizeInBytes;
	//unsigned __int32 commonIndexCount;
	uint32_t commonIndexBufferSizeInBytes;
	uint32_t vbOffset;
	uint32_t ibOffset;
	uint32_t reindexOffset;
	uint32_t jointsCount;
	StaticVector< H6FragmentHeader > fragments;
};
//////////////////////////////////////////////////////////////////////////


struct CubeVertexStride
{
	float X, Y, Z;
	unsigned char a, r, g, b;
};

struct MayaStaticMeshFileHeader
{
	uint32_t signature;
	uint32_t vertexCount;
};

struct MayaSkeletalMeshFileHeader
{
	uint32_t signature;
	uint32_t vertexCount;
};

struct H6StaticMeshFileHeader
{
	uint32_t signature;
	uint32_t fragmentCount;
	uint32_t commonVertexCount;
	uint32_t commonVertexBufferSizeInBytes;
	uint32_t commonIndexCount;
	uint32_t commonIndexBufferSizeInBytes;
};

struct H6StaticMeshFragmentHeader
{
	uint32_t vertexElementCount; // took into account D3DDECL_END 
	uint32_t vertexStrideSizeInBytes;
	uint32_t numVertices;
	//unsigned __int32 vertexCount;
	uint32_t firstIndexNumber;
	uint32_t indexCount;
};

struct MayaVertexStride
{
	CVec3 position;
	CVec3 normal;
	CVec3 tangent;
	CVec3 binormal;
	CVec2 texcoord0;
	CVec2 weights;
	uint32_t indices[2];
};

struct MayaVertexStrideStaticOld
{
	CVec3 position;
	CVec3 normal;
	CVec3 tangent;
	CVec3 binormal;
	CVec2 texcoord[4];
};

struct MayaVertexStrideStatic
{
	CVec3 position;
	CVec4 color;
	CVec3 normal;
	CVec3 tangent;
	CVec3 binormal;
	CVec2 texcoord[4];
};

template <typename Base>
struct MayaVertexStrideSkeletalTemplate : public Base
{
	float weights[8];
	uint32_t indices[8];
};

typedef MayaVertexStrideSkeletalTemplate<MayaVertexStrideStaticOld> MayaVertexStrideSkeletalOld;
typedef MayaVertexStrideSkeletalTemplate<MayaVertexStrideStatic> MayaVertexStrideSkeletal;

struct MayaVertexStrideNew
{
	CVec3 position;
	CVec3 normal;
	CVec3 tangent;
	CVec3 binormal;
	CVec2 texcoord[4];
	float weights[8];
	uint32_t indices[8];
};


__declspec(align(16)) struct H6SkeletalMeshVertexStride
{
	CVec3 position;
	CVec3 tangent;
	CVec3 binormal;
	CVec3 normal;
	uint32_t color;
	CVec2 texcoord[4];
	float weights[8];
	unsigned char indices[8];
};



struct H6StaticMeshVertexStride
{
	CVec3 position;
	CVec3 tangent;
	CVec3 binormal;
	CVec3 normal;
	CVec2 texcoord0;
	//CVec4 weights;
	//unsigned char boneIndices[4];
};

#include <stdint.h>
struct MayaIndexHeader
{
	uint32_t subMeshCount;
};

struct MayaIndexBufferHeader
{
	uint32_t triangleCount;
	uint32_t unicVertexCountsInSubmeshes;
};
