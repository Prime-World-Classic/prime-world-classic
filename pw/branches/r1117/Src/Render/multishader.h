#pragma once

#include "shaderpair.h"
#include "shadercontainer.h"	// .shd v2: дописанные секции целей (§3.z п.3)

namespace Render
{
	namespace Materials
	{
		class MultiShader
		{
			struct ShaderObjInfo* pInfo;
			nstl::vector<ShaderPair> pairs;
			nstl::string fileName;
			// .shd v2: v1-часть отделена от дописанных секций. v1Size = 0 значит
			// «файл v1, целей нет». Образ файла лежит одним блоком за pInfo
			// (копируется весь файл), поэтому смещения секций абсолютные и
			// проверяются против imageSize.
			unsigned v1Size;
			unsigned imageSize;
			nstl::vector<Shd2Section> targets;
			bool OpenFile();
		public:
			static void InitializeDefaultShaderPair();
			static void ReleaseDefaultShaderPair();
			const nstl::string& GetShaderName() const;
			MultiShader( const nstl::string& fileName );
			
			Render::ShaderPair& GetShaderPairByIndex( unsigned int index );

			// .shd v2: есть ли у контейнера секция цели tag и что в ней для
			// комбинации пинов index. Смещения абсолютные в образе файла;
			// 0 = комбинация в контейнер не положена (её компилирует рантайм).
			// Нужно шагу 12/13: выбирать блоб под бэкенд (SPIR-V), а не SM3.
			bool HasShaderTarget( unsigned tag ) const;
			bool GetShaderTargetOffsets( unsigned tag, unsigned index,
				unsigned &vsOffset, unsigned &psOffset ) const;
			unsigned GetV1Size() const { return v1Size; }

			void ReloadShaders();

			~MultiShader();

			void Destroy();
		};
	};
};
