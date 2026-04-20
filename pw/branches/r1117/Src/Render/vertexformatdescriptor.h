#pragma once

#include "../System/nlist.h"
#include "../System/nvector.h"
#include "vertexelementtype.h"
#include "vertexelementusage.h"
#include "vertexelementdescriptor.h"

namespace Render
{

	class VertexFormatDescriptor
	{
	public:
		VertexFormatDescriptor() {}
		~VertexFormatDescriptor() {}

		bool operator==(const VertexFormatDescriptor& descr) const;
		bool operator<(const VertexFormatDescriptor& descr) const;

		///    
		void AddVertexElement(const VertexElementDescriptor& descr);
		void AssignVertexElements(const VertexElementDescriptor& descr, int count );
		///    
		const VertexElementDescriptor& GetVertexElement(unsigned int index) const;
		///    
		unsigned int GetVertexElementsCount() const;
		///
		int FindMaxUsageIndex(EVertexElementUsage usage) const;
	private:
		///   
		nstl::vector<VertexElementDescriptor> vertexElementDescriptors;
	};
} // namespace Render

namespace nstl {
template<> struct hash<Render::VertexFormatDescriptor> 
{
	size_t operator() (const Render::VertexFormatDescriptor& descr) const 
	{ 
		size_t res = 0;

		unsigned int vertexElementsCount = descr.GetVertexElementsCount();

		res += vertexElementsCount;

		for (unsigned int i = 0; i < vertexElementsCount; ++i)
		{
			res += descr.GetVertexElement(i).stream;
			res += descr.GetVertexElement(i).offset;
			res += descr.GetVertexElement(i).usageIndex;
		}

		return res;
	}
};
}
