#include "terrain_layers.h"
#include <algorithm>

namespace LinuxBootstrap {
std::size_t TerrainLayerMaskBytes(std::size_t columns, std::size_t rows) noexcept {
	constexpr size_t maximum=65536;
	if (!columns || !rows || columns>maximum || rows>maximum/columns) return 0;
	return columns*rows*256;
}
bool SelectTerrainLayers(const std::uint8_t* mask, std::size_t bytes, std::size_t columns, std::size_t rows,
	std::size_t column, std::size_t row, TerrainLayerSelection& output) noexcept {
	output = {};
	const auto expected=TerrainLayerMaskBytes(columns,rows);
	if (!mask || !expected || bytes!=expected || column>=columns || row>=rows) return false;
	const size_t width=columns*16, height=rows*16;
	const size_t x0=std::max(column*16,size_t(1))-1, y0=std::max(row*16,size_t(1))-1;
	const size_t x1=std::min(x0+17,width-1), y1=std::min(y0+17,height-1);
	std::array<bool,256> used{};
	for (size_t y=y0; y<=y1; ++y) for (size_t x=x0; x<=x1; ++x) used[mask[y*width+x]]=true;
	for (size_t id=0; id<used.size(); ++id) if (used[id]) {
		++output.total;
		if (output.count<output.ids.size()) output.ids[output.count++]=std::uint8_t(id);
	}
	return output.count!=0;
}
}
