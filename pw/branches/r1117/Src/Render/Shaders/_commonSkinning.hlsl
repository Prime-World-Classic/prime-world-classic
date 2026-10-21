#if SkeletalMeshPinValue == 1
#if PW_MODERN_ARRAYS
// SSBO-форма (шаг 10, §3.z п.2): индекс — In.Indices (атрибут потока), т.е.
// неконстантный и расходящийся; в uniform-буфере такая индексация в Vulkan
// запрещена, в SSBO — законна. Чтение, не запись: BufferDeviceParameter (p.1).
StructuredBuffer<float4x3> BonesMatrices;
#else
float4x3 BonesMatrices[MAX_BONES] : register( BONES );
#endif

// Индексы костей приходят атрибутом BLENDINDICES (клиент кладёт их как
// D3DCOLOR: 4 байта-индекса, нормализованные в [0,1]), поэтому вход ОБЯЗАН
// быть float. FXC (SM3) и так кладёт вершинный вход в float-регистр, и
// 255*idxs восстанавливает байт. При int-объявлении DXC генерирует
// %v4int-вход и OpIMul: на Vulkan вершинный fetch приводит UNORM-атрибут к
// целому, и индекс уезжает за пределы массива костей (255*23 = 5865 при
// MAX_BONES = 70) — скиннинг читает мусор, модель размазывает по экрану
// (замер A/B ab20-ab22, 2026-10-22: герой вместо модели — «шип» в
// несколько сотен пикселей, жёлтый контур SilhouetteMaterial тянется за
// ним). Арифметика та же, что делал FXC: float-умножение с последующим
// отбрасыванием дробной части.
float4x3 MakeSkinnedMatrix(float4 weights, float4 idxs)
{
	float4x3 skinMatrix;

	int4 indices = (int4)(255.0f * idxs);

	skinMatrix  = weights[0] * BonesMatrices[ indices[0] ];
	skinMatrix += weights[1] * BonesMatrices[ indices[1] ];
	skinMatrix += weights[2] * BonesMatrices[ indices[2] ];
	skinMatrix += weights[3] * BonesMatrices[ indices[3] ];

	return skinMatrix;
}
#endif
