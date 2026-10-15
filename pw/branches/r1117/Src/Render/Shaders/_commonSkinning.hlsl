#if SkeletalMeshPinValue == 1
#if PW_MODERN_ARRAYS
// SSBO-форма (шаг 10, §3.z п.2): индекс — In.Indices (атрибут потока), т.е.
// неконстантный и расходящийся; в uniform-буфере такая индексация в Vulkan
// запрещена, в SSBO — законна. Чтение, не запись: BufferDeviceParameter (p.1).
StructuredBuffer<float4x3> BonesMatrices;
#else
float4x3 BonesMatrices[MAX_BONES] : register( BONES );
#endif

float4x3 MakeSkinnedMatrix(float4 weights, int4 idxs)
{
	float4x3 skinMatrix;

	int4 indices = 255 * idxs;

	skinMatrix  = weights[0] * BonesMatrices[ indices[0] ];
	skinMatrix += weights[1] * BonesMatrices[ indices[1] ];
	skinMatrix += weights[2] * BonesMatrices[ indices[2] ];
	skinMatrix += weights[3] * BonesMatrices[ indices[3] ];

	return skinMatrix;
}
#endif
