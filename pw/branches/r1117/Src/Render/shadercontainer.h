#pragma once

// Формат контейнера шейдеров .shd (PLAN_NVRHI §3.z п.3).
//
// v1 (то, что пишет Src/ShaderCompiler/ShaderCompiler.cpp и читает
// multishader.cpp): u32 shadersCount; {u32 vsOffset; u32 psOffset} ×
// shadersCount; блобы. Магии нет, смещения абсолютные от начала файла,
// блобы дедуплицированы по CRC32+memcmp.
//
// v2 (tools/shader_build/mkshd2.py): v1-часть не трогается, в конец файла
// дописываются секции целей, заголовок секций и трэйлер. Поэтому файл v2
// остаётся читаемым старым кодом: таблица v1 и её блобы на своих местах.
//
//   [v1 часть]                                байт-в-байт как v1, длина v1Size
//   [секция цели]  u32 tag; u32 entryCount;
//                  entryCount × { u32 index; u32 vsOffset; u32 psOffset }
//                  затем блобы (смещения абсолютные; 0 = комбинации нет)
//   [заголовок v2] 'V2H\0'; u32 version; u32 v1Size; u32 targetCount;
//                  targetCount × { u32 tag; u32 offset; u32 count }
//   [трэйлер]      'SHD2'; u32 v2HeaderOffset; u32 version; 'SHD2'   (16 Б)
//
// Таблица цели разреженная: кладутся только ЖИВЫЕ комбинации пинов (замер
// 2026-10-15: 58 пар из 48 816), отсутствующая комбинация компилируется в
// рантайме. Индекс — тот же, что и в v1: сумма pinValue*pinMultiplier.
//
// Парсер заголовочный (без .cpp): x86-контур перечисляет файлы проекта вручную
// и нового исходника не увидит, а формат обязан читаться одинаково везде.

#include "System/nvector.h"		// nstl::vector

namespace Render
{
	namespace Materials
	{
		// Теги цели. Порядок = приоритет: читатель берёт первую поддерживаемую.
		enum ShaderTargetTag
		{
			stSm3		= 0,	// DXBC SM3, то же, что в v1-таблице
			stDxbcSm5	= 1,	// DXBC SM5 (для wined3d/DXVK-обвязок, не для продукта)
			stSpirv		= 2,	// SPIR-V (NVRHI/Vulkan)
			stTargetFirst = stSm3,
			stTargetLast  = stSpirv
		};

		const unsigned kShd2TrailerSize = 16;
		const char kShd2TrailerMagic[4] = { 'S', 'H', 'D', '2' };
		const char kShd2HeaderMagic[4]  = { 'V', '2', 'H', '\0' };
		const unsigned kShd2Version = 1;

		struct Shd2Section
		{
			unsigned int tag;		// ShaderTargetTag
			unsigned int offset;	// начало секции, абсолютное
			unsigned int count;	// записей в таблице секции
		};

		// Чтение little-endian: контейнер пишется на x86/x64, но явное чтение
		// байтов страхует от порядка полей в структуре.
		inline unsigned Shd2U32( const unsigned char *p )
		{
			return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
		}

		// Ищем трэйлер в конце файла. Возвращает false, если это обычный v1.
		inline bool Shd2FindTrailer( const unsigned char *pData, unsigned size,
			unsigned &hdrOffset, unsigned &version )
		{
			if ( size < kShd2TrailerSize )
				return false;
			const unsigned char *tail = pData + size - kShd2TrailerSize;
			if ( memcmp( tail, kShd2TrailerMagic, 4 ) != 0 ||
				 memcmp( tail + 12, kShd2TrailerMagic, 4 ) != 0 )
				return false;
			hdrOffset = Shd2U32( tail + 4 );
			version   = Shd2U32( tail + 8 );
			// Смещение заголовка обязано указывать внутрь файла и стоять перед
			// трэйлером: файл контента, так что проверять надо всё.
			if ( hdrOffset + kShd2TrailerSize > size )
				return false;
			return version <= kShd2Version;
		}

		// Разбирает заголовок v2 и таблицу секций. v1Size — длина v1-части
		// (по ней проверяются смещения v1-таблицы, чтобы не читать SPIR-V).
		inline bool Shd2ParseHeader( const unsigned char *pData, unsigned size,
			unsigned hdrOffset, unsigned &v1Size, nstl::vector<Shd2Section> &out )
		{
			v1Size = 0;
			out.clear();
			if ( hdrOffset + 16 > size )
				return false;
			const unsigned char *h = pData + hdrOffset;
			if ( memcmp( h, kShd2HeaderMagic, 4 ) != 0 )
				return false;
			const unsigned version = Shd2U32( h + 4 );
			if ( version != kShd2Version )
				return false;
			v1Size = Shd2U32( h + 8 );
			const unsigned targetCount = Shd2U32( h + 12 );
			if ( !targetCount || targetCount > 16 )		// тегов три, запас на рост
				return false;
			if ( hdrOffset + 16 + 12 * targetCount > size )
				return false;
			out.resize( targetCount );
			for ( unsigned i = 0; i < targetCount; ++i )
			{
				const unsigned char *e = h + 16 + 12 * i;
				Shd2Section &s = out[i];
				s.tag    = Shd2U32( e );
				s.offset = Shd2U32( e + 4 );
				s.count  = Shd2U32( e + 8 );
				// секция обязана уместиться между своим началом и заголовком v2
				if ( s.tag < (unsigned)stTargetFirst || s.tag > (unsigned)stTargetLast )
					return false;
				if ( s.offset < v1Size || s.offset >= hdrOffset )
					return false;
				if ( s.offset + 8 + 12 * s.count > hdrOffset )
					return false;
			}
			return true;
		}

		// Поиск пары по индексу в секции цели. pBase — начало буфера файла (то же,
		// что для v1: смещения в секции абсолютные). Таблица разреженная и
		// маленькая (десятки записей), поэтому линейный поиск, а не хэш.
		// Возвращает false, если записи нет; vsOffset/psOffset = 0 означает
		// «комбинация в контейнер не положена» (её компилирует рантайм).
		inline bool Shd2Lookup( const unsigned char *pBase, unsigned size,
			const Shd2Section &s, unsigned index, unsigned &vsOffset, unsigned &psOffset )
		{
			vsOffset = psOffset = 0;
			const unsigned table = s.offset + 8;		// за tag/count
			if ( table + 12 * s.count > size )
				return false;
			for ( unsigned i = 0; i < s.count; ++i )
			{
				const unsigned char *e = pBase + table + 12 * i;
				if ( Shd2U32( e ) != index )
					continue;
				vsOffset = Shd2U32( e + 4 );
				psOffset = Shd2U32( e + 8 );
				// блобы секции идут сразу за её таблицей
				const unsigned blobLo = table + 12 * s.count;
				if ( vsOffset && ( vsOffset < blobLo || vsOffset + 4 > size ) )
					return false;
				if ( psOffset && ( psOffset < blobLo || psOffset + 4 > size ) )
					return false;
				return true;
			}
			return false;
		}

		// Есть ли у контейнера секция цели (для диагностики покрытия).
		inline const Shd2Section *Shd2FindSection( const nstl::vector<Shd2Section> &sections,
			unsigned tag )
		{
			// int, а не size_t: operator[] у nstl::vector берёт difference_type,
			// иначе C4267 «conversion from 'size_t' to 'int'»
			const int n = (int)sections.size();
			for ( int i = 0; i < n; ++i )
				if ( sections[i].tag == tag )
					return &sections[i];
			return NULL;
		}
	};
};
