#pragma once

#include "vertexelementusage.h"
#include "vertexelementtype.h"

namespace Render
{
	/// �������� ������� �������� �������
	struct VertexElementDescriptor
	{
		/// ����������� 
		VertexElementDescriptor(): stream(0), offset(0), type(VERTEXELEMENTTYPE_FLOAT3), usage(VERETEXELEMENTUSAGE_POSITION), usageIndex(0) {}
		/// ����������� 
		VertexElementDescriptor( unsigned int _stream, unsigned int _offset, EVertexElementType _type, EVertexElementUsage _usage, unsigned int _usageindex ): 
			stream(_stream), offset(_offset), type(_type), usage(_usage), usageIndex(_usageindex) {}

		bool operator == (const VertexElementDescriptor& other) const
		{
			return (stream == other.stream) && (offset == other.offset) && (type == other.type) && 
						 (usage == other.usage) && (usageIndex == other.usageIndex);
		}
		bool operator != (const VertexElementDescriptor& other) const { return !operator == (other); }

		bool operator < (const VertexElementDescriptor& other) const
		{
			if (stream != other.stream) return stream < other.stream;
			if (offset != other.offset) return offset < other.offset;
			if (type != other.type) return type < other.type;
			if (usage != other.usage) return usage < other.usage;
			return usageIndex < other.usageIndex;
		}


		/// ����� ������
		unsigned int stream;
		/// ��������
		unsigned int offset;
		/// ��� 
		EVertexElementType type;
		/// ��� �������������
		EVertexElementUsage usage;
		/// ������ �������������
		unsigned int usageIndex;
	};
}; // namespace Render