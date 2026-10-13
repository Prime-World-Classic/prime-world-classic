#include "baked_terrain.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace LinuxBootstrap;
using Bytes=std::vector<std::uint8_t>;
unsigned checks=0,failures=0;
void Check(bool good,const char* label) { ++checks; if (!good) { ++failures; std::cerr<<"FAIL "<<label<<'\n'; } }
void Word(Bytes& bytes,size_t offset,std::uint32_t value) {
	for (unsigned i=0; i<4; ++i) bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(i*8));
}
void Float(Bytes& bytes,size_t offset,float value) { std::uint32_t bits=0; std::memcpy(&bits,&value,4); Word(bytes,offset,bits); }
/** Independent original XTEL record: visible triangle at z, hidden triangle ten units above. */
Bytes Fixture(float z,bool reverse=false,bool degenerate=false) {
	Bytes bytes(4+224+264+24,0); Word(bytes,0,0x14554);
	const std::uint32_t header[]={0x4c455458,1,1,264,24,224,488,0,0,1,12};
	for (size_t i=0; i<11; ++i) Word(bytes,4+i*4,header[i]);
	const std::uint32_t fragment[]={44,0,0,600,0,1,0,0,6,16};
	for (size_t i=0; i<10; ++i) Word(bytes,52+i*4,fragment[i]);
	const std::uint32_t declarations[6][5]={{0,0,2,0,0},{0,12,2,3,0},{0,24,1,5,0},{0,32,4,10,0},{0,36,4,10,1},{0,40,4,10,2}};
	for (size_t i=0; i<6; ++i) for (size_t j=0; j<5; ++j) Word(bytes,100+i*20+j*4,declarations[i][j]);
	for (unsigned i=0; i<6; ++i) {
		const size_t at=228+i*44;
		Float(bytes,at,!degenerate && i%3==1 ? 4.f : 0.f);
		Float(bytes,at+4,!degenerate && i%3==2 ? 4.f : 0.f);
		Float(bytes,at+8,z+(i>=3 ? 10.f : 0.f)); Float(bytes,at+20,1);
	}
	for (unsigned i=0; i<6; ++i) Word(bytes,492+i*4,reverse ? i/3*3+2-i%3 : i);
	return bytes;
}
BakedTerrain::Ptr Decode(Bytes bytes,size_t count=1) {
	BakedTerrain::Ptr terrain; BakedTerrainError error;
	Check(BakedTerrain::Decode(std::move(bytes),count,terrain,error),"mock baked stream admitted");
	return terrain;
}
bool Same(const BakedTerrain::SurfaceHit& a,const BakedTerrain::SurfaceHit& b) {
	return a.position==b.position && a.distance==b.distance && a.tile==b.tile && a.triangle==b.triangle;
}
/** Independent vertical oracle: 2D signed-area barycentrics, not the production ray algorithm. */
bool ReferenceHeight(const BakedTerrain& terrain,double x,double y,double& height) {
	bool found=false;
	for (size_t index=0; index<terrain.TileCount(); ++index) {
		const auto* tile=terrain.GetTile(index);
		if (x<tile->minimum[0] || x>tile->maximum[0] || y<tile->minimum[1] || y>tile->maximum[1]) continue;
		for (size_t triangle=0; triangle<tile->triangleCount; ++triangle) {
			std::array<std::array<float,3>,3> p{};
			for (size_t corner=0; corner<3; ++corner) {
				std::uint32_t vertex=0; std::memcpy(&vertex,terrain.IndexData(index)+(triangle*3+corner)*4,4);
				std::memcpy(p[corner].data(),terrain.VertexData(index)+size_t(vertex)*44,12);
			}
			const double bx=double(p[1][0])-p[0][0],by=double(p[1][1])-p[0][1];
			const double cx=double(p[2][0])-p[0][0],cy=double(p[2][1])-p[0][1],area=bx*cy-by*cx;
			if (std::abs(area)<1e-12) continue;
			const double u=((x-p[0][0])*cy-(y-p[0][1])*cx)/area;
			const double v=(bx*(y-p[0][1])-by*(x-p[0][0]))/area;
			if (u<-1e-10 || v<-1e-10 || u+v>1+1e-10) continue;
			const double z=p[0][2]+u*(double(p[1][2])-p[0][2])+v*(double(p[2][2])-p[0][2]);
			if (!found || z>height) height=z;
			found=true;
		}
	}
	return found;
}
void Mocks() {
	for (bool reverse : {false,true}) {
		const auto terrain=Decode(Fixture(10,reverse)); if (!terrain) continue;
		BakedTerrain::SurfaceHit hit;
		for (const auto xy : {std::array<double,2>{1,1},{0,0},{4,0},{0,4},{2,2}}) {
			Check(terrain->Trace({xy[0],xy[1],50},{0,0,-1},100,hit) && hit.position==BakedTerrain::Point{xy[0],xy[1],10} &&
				hit.distance==40 && hit.tile==0 && hit.triangle==0,"two-sided interior, edges and vertices hit visible prefix only");
			double z=-99;
			Check(terrain->Height(xy[0],xy[1],z) && z==10,"height ignores hidden geometry above visible surface");
		}
		Check(terrain->Trace({1,1,50},{0,0,-10},4,hit) && hit.distance==4 && hit.position[2]==10,"unnormalized ray parameter and inclusive far endpoint");
		Check(terrain->Trace({1,1,10},{0,0,-1},100,hit) && hit.distance==0,"inclusive near endpoint");
		Check(terrain->Trace({1,1,-10},{0,0,1},100,hit) && hit.distance==20,"two-sided original collision contract");
		Check(terrain->Trace({-9,1,20},{1,0,-1},20,hit) && hit.position==BakedTerrain::Point{1,1,10} &&
			hit.distance==10,"oblique ray enters AABB from outside and hits the visible triangle");
		const BakedTerrain::SurfaceHit sentinel{{7,8,9},42,7,9};
		const auto reject=[&](BakedTerrain::Point origin,BakedTerrain::Point direction,double maximum) {
			hit=sentinel;
			Check(!terrain->Trace(origin,direction,maximum,hit) && Same(hit,sentinel),"miss or invalid ray preserves caller output");
		};
		reject({3,3,50},{0,0,-1},100); reject({1,1,50},{0,0,-1},39.999);
		reject({1,1,50},{0,0,1},100); reject({1,1,50},{1,0,0},100); reject({1,1,50},{0,0,0},100);
		for (double invalid : {0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1e13})
			reject({1,1,50},{0,0,-1},invalid);
		for (unsigned axis=0; axis<3; ++axis) for (double invalid : {std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1e13,-1e13}) {
			BakedTerrain::Point origin{1,1,50},direction{0,0,-1}; origin[axis]=invalid; reject(origin,direction,100);
			origin={1,1,50}; direction[axis]=invalid; reject(origin,direction,100);
		}
		double z=123; Check(!terrain->Height(3,3,z) && z==123,"uncovered height does not invent a ground plane");
		Check(!terrain->Height(NAN,1,z) && z==123,"nonfinite height query preserves output");
	}
	{
		auto bytes=Fixture(10); const auto upper=Fixture(30);
		bytes.insert(bytes.end(),upper.begin()+4,upper.end()); const auto terrain=Decode(std::move(bytes),2);
		BakedTerrain::SurfaceHit hit;
		Check(terrain && terrain->Trace({1,1,100},{0,0,-1},100,hit) && hit.position[2]==30 && hit.tile==1,
			"nearest hit is selected independently of tile submission order");
	}
	{
		auto bytes=Fixture(10); const auto tied=Fixture(10);
		bytes.insert(bytes.end(),tied.begin()+4,tied.end()); const auto terrain=Decode(std::move(bytes),2);
		BakedTerrain::SurfaceHit hit;
		Check(terrain && terrain->Trace({1,1,100},{0,0,-1},100,hit) && hit.tile==0,"equal distances retain first descriptor identity");
	}
	const auto degenerate=Decode(Fixture(10,false,true)); BakedTerrain::SurfaceHit hit;
	Check(degenerate && !degenerate->Trace({0,0,100},{0,0,-1},100,hit),"degenerate authored triangle cannot produce a hit");
	{
		auto bytes=Fixture(10); Float(bytes,228+44+8,14); Float(bytes,228+88+8,18);
		const auto terrain=Decode(std::move(bytes)); double height=0;
		Check(terrain && terrain->Height(1,1,height) && height==13,"sloped triangle interpolates its actual vertex heights");
		Check(terrain && terrain->Trace({-9,1,23},{1,0,-1},100,hit) && hit.position==BakedTerrain::Point{1,1,13},
			"oblique ray intersects a sloped triangle at the analytic point");
	}
	{
		auto bytes=Fixture(10); Word(bytes,72,2); const auto terrain=Decode(std::move(bytes));
		Check(terrain && terrain->Trace({1,1,100},{0,0,-1},100,hit) && hit.position[2]==20 && hit.triangle==1,
			"nearest triangle in one record is independent of index order");
	}
}
void Original(const char* path) {
	std::ifstream file(path,std::ios::binary|std::ios::ate);
	if (!file || file.tellg()!=14254504) { Check(false,"shipped baked fixture available"); return; }
	Bytes bytes(static_cast<size_t>(file.tellg())); file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) { Check(false,"shipped bytes read"); return; }
	const auto terrain=Decode(std::move(bytes),1681); if (!terrain) return;
	unsigned tested=0,overlapping=0;
	for (size_t tileIndex=0; tileIndex<terrain->TileCount(); ++tileIndex) {
		const auto* tile=terrain->GetTile(tileIndex);
		for (size_t triangle=0; triangle<tile->triangleCount; ++triangle) {
			std::array<std::array<float,3>,3> v{};
			for (size_t corner=0; corner<3; ++corner) {
				std::uint32_t index=0; std::memcpy(&index,terrain->IndexData(tileIndex)+(triangle*3+corner)*4,4);
				std::memcpy(v[corner].data(),terrain->VertexData(tileIndex)+size_t(index)*44,12);
			}
			const double area=(double(v[1][0])-v[0][0])*(double(v[2][1])-v[0][1])-(double(v[1][1])-v[0][1])*(double(v[2][0])-v[0][0]);
			if (std::abs(area)<.1) continue;
			const double x=(double(v[0][0])+v[1][0]+v[2][0])/3,y=(double(v[0][1])+v[1][1]+v[2][1])/3;
			const double expected=(double(v[0][2])+v[1][2]+v[2][2])/3;
			double height=-999,reference=-999;
			Check(ReferenceHeight(*terrain,x,y,reference) && terrain->Height(x,y,height) && std::abs(height-reference)<1e-6 &&
				height>=expected-1e-6,"highest authored surface matches independent signed-area oracle across every tile");
			if (reference>expected+1e-6) ++overlapping;
			++tested; break;
		}
	}
	Check(tested>1600,"original nondegenerate surface coverage");
	Check(overlapping==16,"shipped overlapping/folded triangles select their higher surface");
	std::cout<<"Baked original surface: tiles="<<tested<<" overlapping="<<overlapping<<'\n';
}
}
int main(int argc,char** argv) {
	if (argc>2) return 2;
	if (argc==2) Original(argv[1]); else Mocks();
	std::cout<<"Baked surface: "<<checks<<" checks, "<<failures<<" failures\n";
	return failures ? 1 : 0;
}
