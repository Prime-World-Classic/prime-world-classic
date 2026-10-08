#include "stdafx.h"
#include "ssememcopy.h"
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>   // _mm_stream_si128 / _mm_prefetch / _mm_sfence
#endif

void CompileTimeCheck()
{
	NI_STATIC_ASSERT( BUS_SEGMENT_SIZE > 128, BUS_SEGMENT_SIZE_SHOULD_BE_MORE_THAN_128 );
	NI_STATIC_ASSERT( !( BUS_SEGMENT_SIZE & (BUS_SEGMENT_SIZE-1) ), BUS_SEGMENT_SIZE_SHOULD_BE_POWER_OF_TWO );
}

void GuardedSSEMemCopy(void* _pDestination, void* _pSource, unsigned __int32 _size)
{
	NI_ASSERT( !((size_t) _pDestination & 0xF), "destination memory is NOT 16-byte aligned" );
	NI_ASSERT( !((size_t) _pSource & 0xF), "source memory is NOT 16-byte aligned" );
	NI_ASSERT( _size > BUS_SEGMENT_SIZE, "size should be more than BUS_SEGMENT_SIZE" );
	NI_ASSERT( !(_size % BUS_SEGMENT_SIZE), "size should be divisible by BUS_SEGMENT_SIZE" );	
	ssememcopy(_pDestination, _pSource, _size);
}

#if defined(_M_X64) || defined(__x86_64__)
// x64: MSVC не компилирует __asm (C4235), а __declspec(naked) на x64 не
// поддерживаетс€. Ёквивалент на intrinsics: тот же цикл по сегментам
// BUS_SEGMENT_SIZE, те же non-temporal stores (_mm_stream_si128 == movntdq).
// ¬ызов идЄт только через GuardedSSEMemCopy, котора€ требует 16-байтной
// выравниваемости и кратности BUS_SEGMENT_SIZE Ч условие то же, что дл€ asm.void __stdcall ssememcopy(void* _pDestination, void* _pSource, unsigned __int32 _size)
{
	char* dst = (char*)_pDestination;
	const char* src = (const char*)_pSource;
	for (unsigned __int32 done = 0; done < _size; done += BUS_SEGMENT_SIZE)
	{
		for (unsigned __int32 off = 0; off < BUS_SEGMENT_SIZE; off += 128)
		{
			// prefetch (как movaps-чтени€ в asm-варианте)
			_mm_prefetch( src + done + off, _MM_HINT_T0 );
			_mm_prefetch( src + done + off + 64, _MM_HINT_T0 );
		}
		for (unsigned __int32 off = 0; off < BUS_SEGMENT_SIZE; off += 128)
		{
			__m128i r0 = _mm_load_si128( (const __m128i*)(src + done + off + 0) );
			__m128i r1 = _mm_load_si128( (const __m128i*)(src + done + off + 16) );
			__m128i r2 = _mm_load_si128( (const __m128i*)(src + done + off + 32) );
			__m128i r3 = _mm_load_si128( (const __m128i*)(src + done + off + 48) );
			__m128i r4 = _mm_load_si128( (const __m128i*)(src + done + off + 64) );
			__m128i r5 = _mm_load_si128( (const __m128i*)(src + done + off + 80) );
			__m128i r6 = _mm_load_si128( (const __m128i*)(src + done + off + 96) );
			__m128i r7 = _mm_load_si128( (const __m128i*)(src + done + off + 112) );
			_mm_stream_si128( (__m128i*)(dst + done + off + 0), r0 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 16), r1 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 32), r2 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 48), r3 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 64), r4 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 80), r5 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 96), r6 );
			_mm_stream_si128( (__m128i*)(dst + done + off + 112), r7 );
		}
	}
	_mm_sfence();
}
#else
__declspec(naked) void __stdcall ssememcopy(void* _pDestination, void* _pSource, unsigned __int32 _size)
{
	__asm
	{
		push edx
		push esi
		push edi
		push ecx
		///
		mov edi, [esp+20]
		mov esi, [esp+24]
		mov edx, [esp+28]
		align 16
main_loop:
		xor ecx, ecx
		align 16
prefetch_loop:
		movaps xmm0, [esi+ecx]
		movaps xmm0, [esi+ecx+64]
		add ecx, 128
		cmp ecx, BUS_SEGMENT_SIZE
		jne prefetch_loop
		xor ecx, ecx
		align 16
copy_loop:
		movdqa xmm0, [esi+ecx+0]//movntdqa
		movdqa xmm1, [esi+ecx+16]
		movdqa xmm2, [esi+ecx+32]
		movdqa xmm3, [esi+ecx+48]
		movdqa xmm4, [esi+ecx+64]
		movdqa xmm5, [esi+ecx+80]
		movdqa xmm6, [esi+ecx+96]
		movdqa xmm7, [esi+ecx+112]
		movntdq [edi+ecx+0], xmm0
		movntdq [edi+ecx+16], xmm1
		movntdq [edi+ecx+32], xmm2
		movntdq [edi+ecx+48], xmm3
		movntdq [edi+ecx+64], xmm4
		movntdq [edi+ecx+80], xmm5
		movntdq [edi+ecx+96], xmm6
		movntdq [edi+ecx+112], xmm7
		add ecx, 128
		cmp ecx, BUS_SEGMENT_SIZE
		jne copy_loop
		add esi, ecx
		add edi, ecx
		sub edx, ecx
		jnz main_loop
		sfence
		///
		pop ecx
		pop edi
		pop esi
		pop edx
		ret 12
	}
}
#endif

