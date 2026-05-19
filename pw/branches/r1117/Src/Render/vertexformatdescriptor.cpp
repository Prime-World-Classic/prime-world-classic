#include "stdafx.h"
#include "vertexformatdescriptor.h"

namespace Render
{
/// �����������
/// ���������� �������� �������� �������
void VertexFormatDescriptor::AddVertexElement(const VertexElementDescriptor& descr)
{
	vertexElementDescriptors.push_back(descr);
}
void VertexFormatDescriptor::AssignVertexElements( const VertexElementDescriptor& descr, int count )
{
	//assign of range is not exist in nstl, couse I used to memcpy
	vertexElementDescriptors.resize(count);
	memcpy(&vertexElementDescriptors[0], &descr, sizeof(VertexElementDescriptor)*count );
}
/// ��������� �������� �������� �������
const VertexElementDescriptor& VertexFormatDescriptor::GetVertexElement(unsigned int index) const
{
	NI_ASSERT(index < GetVertexElementsCount(), "Invalid vertex element index!");
	return vertexElementDescriptors[index];
}
/// ��������� ���������� ��������� �������
unsigned int VertexFormatDescriptor::GetVertexElementsCount() const
{
	return vertexElementDescriptors.size();
}
///
int VertexFormatDescriptor::FindMaxUsageIndex(EVertexElementUsage usage) const
{
	int usageIndex = -1;	
	const int size = vertexElementDescriptors.size();
	for (int i = 0; i < size; ++i)
		if (vertexElementDescriptors[i].usage == usage)
			usageIndex = max(usageIndex, int(vertexElementDescriptors[i].usageIndex));
	return usageIndex;
}

bool VertexFormatDescriptor::operator==(const VertexFormatDescriptor& descr) const
{
	return vertexElementDescriptors == descr.vertexElementDescriptors;
}

bool VertexFormatDescriptor::operator<(const VertexFormatDescriptor& descr) const
{
	if (vertexElementDescriptors.size() != descr.vertexElementDescriptors.size())
		return vertexElementDescriptors.size() < descr.vertexElementDescriptors.size();
	for (size_t i = 0; i < vertexElementDescriptors.size(); ++i)
	{
		if (vertexElementDescriptors[i] != descr.vertexElementDescriptors[i])
			return vertexElementDescriptors[i] < descr.vertexElementDescriptors[i];
	}
	return false;
}
}; // namespace Render