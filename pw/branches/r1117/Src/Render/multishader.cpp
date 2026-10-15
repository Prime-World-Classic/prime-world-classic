#include "stdafx.h"
#include "multishader.h"
#include "ImmediateRenderer.h"

#include "System/InlineProfiler.h"
#include "System/FileSystem/FileStream.h"
#include "System/FileSystem/FileSystem.h"
#include "shadercompiler.h"
#include "shadercontainer.h"

NDebug::PerformanceDebugVar render_TranslationTime( "ShaderTransTime", "RenderOffline", 100, 100, false );
NDebug::DebugVar<int> render_TranslationCount( "ShaderTransCount", "RenderOffline" );

namespace Render
{
	namespace Materials
	{

		struct ShaderObjInfo
		{
			unsigned int vsOffset;
			unsigned int psOffset;
		};

		// Шаг 11.0 (PLAN_NVRHI §3.z п.3): список ЖИВЫХ пар (материал, индекс пинов).
		// Нужен как входной набор генератора при переходе на .shd v2: в контейнер
		// кладутся только используемые комбинации, остальные компилируются в
		// рантайме. Печатается один раз на пару, включается dev-var'ом
		// `setvar dump_shader_index 1` (по умолчанию выключен).
		static int s_dumpShaderIndex = 0;
		REGISTER_VAR( "dump_shader_index", s_dumpShaderIndex, STORAGE_NONE );

		struct PairKeyHash { size_t operator()(UINT64 _k) const { return size_t(_k ^ (_k >> 32)); } };
		static hash_set<UINT64, PairKeyHash> s_seenShaderPairs;

		static UINT64 FileNameKey( const char *name )
		{
			UINT64 h = 0xcbf29ce484222325ull;
			for ( ; name && *name; ++name )
				h = ( h ^ (unsigned char)*name ) * 0x100000001b3ull;
			return h;
		}

		// Шаг 11 (PLAN_NVRHI §3.z п.3): проверка, что контейнер v2 читается и
		// покрывает живые пары. 0 = SM3 (v1-таблица, как раньше), 2 = SPIR-V.
		// Создавать шейдеры из SPIR-V бэкенд ещё не умеет (это шаг 12), поэтому
		// при nonzero цели только находятся и логируются — мёртвая ветка в
		// продукт не идёт, а читатель v2 прогоняется на реальном контенте.
		// Включается dev-var'ом `setvar shader_target 2`.
		static int s_shaderTarget = 0;
		REGISTER_VAR( "shader_target", s_shaderTarget, STORAGE_NONE );
		static hash_set<UINT64, PairKeyHash> s_seenTargetPairs;

		static ShaderPair defaultShaderPair;

		void MultiShader::InitializeDefaultShaderPair()
		{
      defaultShaderPair.pVertexShader = ImmRenderer::Get()->GetVShader("default_shader");
			defaultShaderPair.pPixelShader = ImmRenderer::Get()->GetPShader("default_shader");
		}

		void MultiShader::ReleaseDefaultShaderPair()
		{
			defaultShaderPair.pVertexShader = NULL;
			defaultShaderPair.pPixelShader  = NULL;
		}

		bool MultiShader::OpenFile(  )
		{
			char buf[256];
			// .shd2 (SM2.0-вариант) удалён: у .shd один профиль, гейт запуска
			// без SM3.0 не пускает железо и так.
			sprintf_s( buf, "%s.shd", fileName.c_str() );

			CObj<Stream> pShaderFileStream = RootFileSystem::OpenFile( buf, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
			NI_VERIFY( pShaderFileStream && pShaderFileStream->IsOk(), NStr::StrFmt( "Cannot open obj file: %s", buf), pairs.resize(1); return false; );

			unsigned char* pBegin = (unsigned char*)pShaderFileStream->GetBuffer();

			int shadersCount = *(unsigned int*)pBegin;
			NI_VERIFY(shadersCount, "fatal error: cant find uber shader objs!", pairs.resize(1); return false; );
			pairs.resize( shadersCount );

			int effectiveSize =  pShaderFileStream->GetSize()-4;
			pInfo = (ShaderObjInfo*)Aligned_MAlloc(effectiveSize, 16);
			memcpy(pInfo, pBegin+4, effectiveSize);
			imageSize = (unsigned)pShaderFileStream->GetSize();

			// .shd v2: дописанные секции целей + заголовок + трэйлер. v1-таблица
			// и её блобы не тронуты, поэтому ниже всё читается как раньше; здесь
			// только разбирается хвост. Образ файла — тот же блок (копируется весь
			// файл), так что смещения секций остаются абсолютными.
			v1Size = 0;
			targets.clear();
			unsigned hdrOffset = 0, version = 0;
			if ( Shd2FindTrailer( pBegin, imageSize, hdrOffset, version ) )
			{
				if ( !Shd2ParseHeader( pBegin, imageSize, hdrOffset, v1Size, targets ) )
				{
					// битый/непонятный v2: цели отбрасываем, SM3-часть на месте —
					// клиент работает как с обычным v1
					v1Size = 0;
					targets.clear();
					DebugTrace( "ShaderContainer: %s — заголовок v2 не разобран, читаю как v1", buf );
				}
				else if ( v1Size > imageSize )
				{
					v1Size = 0;
					targets.clear();
				}
				else
					DebugTrace( "ShaderContainer: %s — v2, v1 %u Б, целей %d", fileName.c_str(),
						v1Size, (int)targets.size() );
			}
			
			/*for( int i = 0; i < shadersCount; ++i)
			{
					ASSERT(pInfo->vsOffset && pInfo->psOffset);
					pairs[i].pVertexShader = Render::GetRenderer()->CreateVertexShader( pBegin + pInfo->vsOffset );
					pairs[i].pPixelShader = Render::GetRenderer()->CreatePixelShader( pBegin + pInfo->psOffset );
					++pInfo;
			}*/

			return true;
		}

		bool MultiShader::HasShaderTarget( unsigned tag ) const
		{
			return v1Size && Shd2FindSection( targets, tag ) != NULL;
		}

		bool MultiShader::GetShaderTargetOffsets( unsigned tag, unsigned index,
			unsigned &vsOffset, unsigned &vsSize, unsigned &psOffset, unsigned &psSize ) const
		{
			vsOffset = vsSize = psOffset = psSize = 0;
			if ( !pInfo || !v1Size )
				return false;
			const Shd2Section *sec = Shd2FindSection( targets, tag );
			if ( !sec )
				return false;
			// тот же трюк, что в GetShaderPairByIndex: образ файла начинается на 4
			// байта раньше таблицы, смещения в контейнере абсолютные
			const unsigned char *pBase = (const unsigned char*)pInfo - 4;
			return Shd2Lookup( pBase, imageSize, *sec, index, vsOffset, vsSize, psOffset, psSize );
		}

		ShaderPair& MultiShader::GetShaderPairByIndex(unsigned int index)
		{
			NI_VERIFY(pInfo, "pInfo is invalid!", return defaultShaderPair; );
			if ( !(0 <= index && (int)index < pairs.size()) )
				return defaultShaderPair;
			if ( s_dumpShaderIndex )
			{
				const UINT64 key = FileNameKey( fileName.c_str() ) ^
					( (UINT64)index * 0x9e3779b97f4a7c15ull );
				// nstl::hash_set::find отдаёт итератор (_Ht_it), проверять его на «!»
				// нельзя — факт вставки определяем по изменению размера.
				const size_t before = s_seenShaderPairs.size();
				s_seenShaderPairs.insert( key );
				if ( s_seenShaderPairs.size() != before )
					DebugTrace( "ShaderIndex: %s #%u (of %u)", fileName.c_str(), index,
						(unsigned int)pairs.size() );
			}
			//NI_VERIFY((int)index < pairs.size() && index >= 0, NStr::StrFmt("out of pins! #%d", index), return defaultShaderPair; );
			if ( s_shaderTarget )
			{
				unsigned vsOff = 0, vsSz = 0, psOff = 0, psSz = 0;
				const bool found = GetShaderTargetOffsets( (unsigned)s_shaderTarget, index,
					vsOff, vsSz, psOff, psSz );
				const UINT64 key = FileNameKey( fileName.c_str() ) ^
					( (UINT64)index * 0x9e3779b97f4a7c15ull ) ^ ((UINT64)s_shaderTarget << 1);
				const size_t before = s_seenTargetPairs.size();
				if ( found )
				{
					s_seenTargetPairs.insert( key );
					if ( s_seenTargetPairs.size() != before )
						DebugTrace( "ShaderTarget: %s #%u — блоб цели %u+%u / %u+%u Б, "
							"бэкенд цель ещё не умеет (шаг 12), беру SM3", fileName.c_str(), index,
							vsOff, vsSz, psOff, psSz );
				}
				else if ( HasShaderTarget( (unsigned)s_shaderTarget ) )
				{
					// секция есть, но этой комбинации в ней нет — нужен рантайм-
					// компилятор (шаг 12/13); пока это не ошибка: SM3-таблица плотная
					s_seenTargetPairs.insert( key );
					if ( s_seenTargetPairs.size() != before )
						DebugTrace( "ShaderTarget: %s #%u — комбинации нет в цели, нужен "
							"рантайм-компилятор", fileName.c_str(), index );
				}
			}
			if( !pairs[index].pVertexShader )
			{
        NI_ASSERT(Get(pairs[index].pPixelShader) == NULL, "Both shaders should be either missing or present at the same time");
				NDebug::PerformanceDebugVarGuard render_TranslationTimeGuard( render_TranslationTime, true );
        NI_PROFILE_BLOCK("create shaders");
				render_TranslationCount.AddValue( 1 );
				unsigned char* pBegin = (unsigned char*)pInfo - 4;
				pairs[index].pVertexShader = Render::GetRenderer()->CreateVertexShader( pBegin + pInfo[index].vsOffset );
				pairs[index].pPixelShader = Render::GetRenderer()->CreatePixelShader( pBegin + pInfo[index].psOffset );
			}
      else
      {
        NI_ASSERT(Get(pairs[index].pPixelShader) != NULL, "Both shaders should be either missing or present at the same time");
      }
			return pairs[index];
		}

		void MultiShader::Destroy()
		{
			const int shadersCount = pairs.size();
			for (int i = 0; i < shadersCount; ++i)
			{
				pairs[i].pVertexShader = 0;
				pairs[i].pPixelShader = 0;
			}
			pairs.clear();
			targets.clear();
			v1Size = 0;
			imageSize = 0;
			Aligned_Free(pInfo);
			pInfo = 0;
		}

		MultiShader::~MultiShader()
		{
			Destroy();
		}

		MultiShader::MultiShader(const nstl::string& _fileName ) : pInfo(0), fileName(_fileName)
			, v1Size(0), imageSize(0)
		{
			OpenFile( );
		}

		void MultiShader::ReloadShaders()
		{
			Destroy();
			OpenFile();
		}

		const nstl::string& MultiShader::GetShaderName() const
		{
			return fileName;
		}
	};
};

