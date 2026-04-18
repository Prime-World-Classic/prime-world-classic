#pragma once

namespace Render
{
	struct ShaderConstantTable;

	namespace ShaderConstantsBinder
	{
		///
		void BindPixelShaderConstants( ShaderConstantTable& table);
		///
		void BindVertexShaderConstants( ShaderConstantTable& table);
	}; // namespace ShaderConstantsBinder
}; // namespace Render
