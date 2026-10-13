#include "dds_mip_chain.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using namespace LinuxBootstrap;
unsigned checks=0,failures=0;
void Check(bool ok,const char* message) { ++checks; if (!ok) { ++failures; std::cerr<<"FAIL "<<message<<'\n'; } }
/** Independent little-endian fixture words correspond to the documented legacy DDS header. */
void Word(std::vector<unsigned char>& data,size_t word,std::uint32_t value) {
	for (unsigned i=0;i<4;++i) data.at(word*4+i)=static_cast<unsigned char>(value>>(i*8));
}
std::vector<unsigned char> Fixture(unsigned width,unsigned height,unsigned count,char format) {
	std::vector<unsigned char> data(128,0);
	Word(data,0,0x20534444); Word(data,1,124); Word(data,2,0xa1007);
	Word(data,3,height); Word(data,4,width); Word(data,7,count);
	Word(data,19,32); Word(data,20,4); Word(data,21,0x00545844|(unsigned(format)<<24)); Word(data,27,0x401008);
	for (unsigned i=0;i<std::max(1u,count);++i) {
		data.resize(data.size()+((width+3)/4)*((height+3)/4)*(format=='1'?8:16),static_cast<unsigned char>(i+1));
		width=std::max(1u,width/2); height=std::max(1u,height/2);
	}
	return data;
}
void Mocks() {
	DdsMipChain chain;
	for (char format : {'1','3','5'}) for (const auto dimensions : {std::array<unsigned,2>{8,8},{1,8},{7,3},{1,1}}) {
		unsigned count=1; for(unsigned n=std::max(dimensions[0],dimensions[1]);n>1;n/=2) ++count;
		const auto data=Fixture(dimensions[0],dimensions[1],count,format);
		Check(InspectDdsMipChain(data.data(),data.size(),chain)==DdsMipStatus::Valid,"full authored block mip chain admitted");
		Check(chain.count==count && chain.levels[0].width==dimensions[0] && chain.levels[0].height==dimensions[1],"mip count and dimensions retained");
		Check(chain.format==(format=='1'?DdsBlockFormat::Bc1:format=='3'?DdsBlockFormat::Bc2:DdsBlockFormat::Bc3),"DXT format not substituted");
		size_t bytes=128,rgba=0;
		unsigned width=dimensions[0],height=dimensions[1];
		for (size_t i=0;i<chain.count;++i) {
			const auto& level=chain.levels[i];
			Check(level.offset==bytes && level.width==width && level.height==height && data.at(level.offset)==i+1,"authored subresource order and tiny-level block rounding");
			bytes+=level.bytes; rgba+=width*height*4;
			width=std::max(1u,width/2); height=std::max(1u,height/2);
		}
		Check(bytes==data.size() && chain.rgbaBytes==rgba,"entire chain included in source and decoded accounting");
	}
	for (unsigned count : {0u,1u,2u}) {
		const auto data=Fixture(8,8,count,'1');
		Check(InspectDdsMipChain(data.data(),data.size(),chain)==DdsMipStatus::Valid && chain.count==std::max(1u,count),"absent count and partial chain preserved");
	}
	const auto valid=Fixture(8,8,4,'5');
	for (size_t length=0;length<valid.size();++length) {
		chain.count=9; chain.rgbaBytes=99;
		Check(InspectDdsMipChain(valid.data(),length,chain)==DdsMipStatus::Invalid && !chain.count && !chain.rgbaBytes,"every truncated source rejected atomically");
	}
	for (const auto change : {std::array<unsigned,2>{0,0},{1,123},{2,0},{3,0},{4,4097},{6,2},{7,5},{19,31},{27,0},{28,0x200},{28,0x200000}}) {
		auto data=valid; Word(data,change[0],change[1]);
		Check(InspectDdsMipChain(data.data(),data.size(),chain)==DdsMipStatus::Invalid && !chain.count,"invalid header or impossible mip count rejected");
	}
	for (char format : {'2','4','X'}) {
		auto data=valid; Word(data,21,0x00545844|(unsigned(format)<<24));
		Check(InspectDdsMipChain(data.data(),data.size(),chain)==DdsMipStatus::Unsupported && !chain.count,"premultiplied and unknown codecs not relabeled");
	}
	auto trailing=valid; trailing.push_back(0);
	Check(InspectDdsMipChain(trailing.data(),trailing.size(),chain)==DdsMipStatus::Invalid,"trailing unaccounted bytes rejected");
	Check(InspectDdsMipChain(nullptr,valid.size(),chain)==DdsMipStatus::Invalid,"null source rejected");
	Check(InspectDdsMipChain(valid.data(),SIZE_MAX,chain)==DdsMipStatus::Invalid,"oversized source rejected before access");
}
void Original(const char* path) {
	std::ifstream file(path,std::ios::binary|std::ios::ate);
	if (!file || file.tellg()<=0 || file.tellg()>64*1024*1024) { Check(false,"original DDS fixture available"); return; }
	std::vector<unsigned char> data(static_cast<size_t>(file.tellg())); file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(data.data()),data.size())) { Check(false,"original DDS fixture read"); return; }
	DdsMipChain chain;
	Check(InspectDdsMipChain(data.data(),data.size(),chain)==DdsMipStatus::Valid,"original Bank diffuse mip chain");
	Check(chain.count==10 && chain.format==DdsBlockFormat::Bc1 && chain.levels[0].width==512 && chain.levels[9].width==1,"original Bank dimensions and ten authored mips");
	Check(chain.rgbaBytes==1398100 && chain.levels[9].offset+chain.levels[9].bytes==data.size(),"original decoded and compressed chain extents");
}
}
int main(int argc,char** argv) {
	if (argc>2) return 2;
	if (argc==2) Original(argv[1]); else Mocks();
	std::cout<<"DDS mip chain: "<<checks<<" checks, "<<failures<<" failures\n";
	return failures?1:0;
}
