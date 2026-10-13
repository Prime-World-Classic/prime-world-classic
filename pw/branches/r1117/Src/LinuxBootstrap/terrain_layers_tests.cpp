#include "terrain_layers.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using namespace LinuxBootstrap;
unsigned checks=0,failures=0;
void Check(bool ok, const char* text) {
	++checks;
	if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; }
}
/** Independent scalar oracle follows the original inclusive border coordinates. */
std::vector<unsigned> Oracle(const std::vector<std::uint8_t>& mask, size_t columns, size_t rows, size_t x, size_t y) {
	bool seen[256]{};
	const size_t x0=x?x*16-1:0, y0=y?y*16-1:0;
	for (size_t row=y0; row<=std::min(y0+17,rows*16-1); ++row)
		for (size_t col=x0; col<=std::min(x0+17,columns*16-1); ++col) seen[mask.at(row*columns*16+col)]=true;
	std::vector<unsigned> result;
	for (unsigned i=0; i<256; ++i) if (seen[i]) result.push_back(i);
	return result;
}
void Mocks() {
	Check(TerrainLayerMaskBytes(41,41)==430336,"original map mask dimensions");
	Check(TerrainLayerMaskBytes(65536,1)==16777216,"exact allocation cap");
	Check(!TerrainLayerMaskBytes(65537,1) && !TerrainLayerMaskBytes(SIZE_MAX,SIZE_MAX) &&
		!TerrainLayerMaskBytes(0,1) && !TerrainLayerMaskBytes(1,0),"dimension rejection before multiplication");
	TerrainLayerSelection selected;
	std::vector<std::uint8_t> mask(48*32,9);
	mask[0]=1; mask[17]=4; mask[18]=3; mask[17*48+17]=250;
	Check(SelectTerrainLayers(mask.data(),mask.size(),3,2,0,0,selected),"first-cell inclusive border selection");
	Check(selected.count==4 && selected.total==4 && selected.ids==std::array<std::uint8_t,4>{{1,4,9,250}} && selected.LocalUV(),
		"ascending IDs include border17 but exclude18; never rank by frequency");
	mask[1]=0;
	Check(SelectTerrainLayers(mask.data(),mask.size(),3,2,0,0,selected),"five-layer selection");
	Check(selected.total==5 && selected.count==4 && selected.ids==std::array<std::uint8_t,4>{{0,1,4,9}} && !selected.LocalUV(),
		"first-four truncation retains reserved zero and selects global UV");
	for (size_t y=0; y<2; ++y) for (size_t x=0; x<3; ++x) {
		const auto expected=Oracle(mask,3,2,x,y);
		Check(SelectTerrainLayers(mask.data(),mask.size(),3,2,x,y,selected),"all boundary cells admitted");
		bool same=selected.total==expected.size() && selected.count==std::min(size_t(4),expected.size());
		for (size_t i=0; i<selected.count; ++i) same=same && selected.ids[i]==expected[i];
		Check(same,"original border footprint and ascending selection oracle");
	}
	for (unsigned value=0; value<256; ++value) {
		std::fill(mask.begin(),mask.end(),std::uint8_t(value));
		Check(SelectTerrainLayers(mask.data(),mask.size(),3,2,2,1,selected) && selected.count==1 && selected.total==1 &&
			selected.ids[0]==value && selected.LocalUV()==(value==1),"all byte IDs, including reserved values");
	}
	for (int kind=0; kind<8; ++kind) {
		selected.ids.fill(99); selected.count=selected.total=4;
		const auto* data=kind==0?nullptr:mask.data();
		const size_t bytes=kind==1?mask.size()-1:kind==2?mask.size()+1:mask.size();
		Check(!SelectTerrainLayers(data,bytes,kind==3?SIZE_MAX:3,kind==4?0:2,kind==5?3:0,kind==6?2:kind==7?SIZE_MAX:0,selected),
			"invalid mask/cell rejected");
		Check(!selected.count && !selected.total && selected.ids==std::array<std::uint8_t,4>{},"failure clears caller selection");
	}
}
void Original(const char* path) {
	std::ifstream file(path,std::ios::binary|std::ios::ate);
	if (!file || file.tellg()!=430336) { Check(false,"original mask fixture size"); return; }
	std::vector<std::uint8_t> mask(430336); file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(mask.data()),mask.size())) { Check(false,"original mask read"); return; }
	TerrainLayerSelection selected;
	Check(SelectTerrainLayers(mask.data(),mask.size(),41,41,38,16,selected) && selected.count==1 && selected.ids[0]==6 && !selected.LocalUV(),
		"original spawn tile uses global Bank layer6, not its local editor material");
	size_t overflow=0;
	for (size_t row=0; row<41; ++row) for (size_t column=0; column<41; ++column) {
		const auto expected=Oracle(mask,41,41,column,row);
		bool ok=SelectTerrainLayers(mask.data(),mask.size(),41,41,column,row,selected) &&
			selected.total==expected.size() && selected.count==std::min(size_t(4),expected.size());
		for (size_t i=0; i<selected.count; ++i) ok=ok && selected.ids[i]==expected[i];
		Check(ok,"shipped tile matches original layer footprint");
		if (selected.total>4) ++overflow;
	}
	Check(overflow==9,"nine shipped five-ID tiles retain original first-four behavior");
}
}
int main(int argc, char** argv) {
	if (argc>2) return 2;
	if (argc==2) Original(argv[1]); else Mocks();
	std::cout << "Terrain layers: " << checks << " checks, " << failures << " failures\n";
	return failures?1:0;
}
