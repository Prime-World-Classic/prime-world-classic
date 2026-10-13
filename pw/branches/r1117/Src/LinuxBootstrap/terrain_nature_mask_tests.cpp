#include "terrain_nature_mask.h"
#include <cmath>
#include <iostream>

namespace {
unsigned checks=0,failures=0;
void Check(bool ok, const char* text) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
struct World {
	unsigned locations=0,queries=0;
	int nature[4]={0,1,2,0};
	bool invalidLocation=false;
	static int Locate(void* value,float x,float y) noexcept {
		auto& world=*static_cast<World*>(value); ++world.locations;
		return world.invalidLocation?-1:int(x>=50)+2*int(y>=50);
	}
	static int Query(void* value,int id) noexcept {
		auto& world=*static_cast<World*>(value); ++world.queries;
		return id>=0 && id<4?world.nature[id]:-1;
	}
};
}
int main() {
	using LinuxBootstrap::TerrainNatureMask;
	TerrainNatureMask mask; World world; bool changed=true;
	Check(!mask.Refresh(World::Query,&world,changed) && !changed,"uninitialized nature mask rejected");
	Check(mask.Initialize(16,16,100,100,World::Locate,&world),"mock world mapping initialized");
	Check(world.locations==256 && mask.CellCount()==4,"duplicate pixel cells compacted once");
	Check(mask.Refresh(World::Query,&world,changed) && changed && world.queries==4,"each original cell queried once");
	Check(mask.AllChannels()==7 && mask.Pixels().size()==1024,"three nature channels and bounded pixel extent");
	const auto pixel=[&](size_t x,size_t y,size_t c) { return mask.Pixels()[(y*16+x)*4+c]; };
	Check(pixel(0,0,0)==255 && pixel(15,0,1)==255 && pixel(0,15,1)==255 && pixel(15,15,2)==255,
		"world Y inverted; N/A/B encode green/blue/red");
	for (size_t i=3; i<mask.Pixels().size(); i+=4) Check(mask.Pixels()[i]==0,"current nature has no invented overburn");
	const auto before=mask.Pixels();
	Check(mask.Refresh(World::Query,&world,changed) && !changed && world.locations==256 && world.queries==8 && mask.Pixels()==before,
		"unchanged nature retains pixels and cached lookup");
	world.nature[2]=1;
	Check(mask.Refresh(World::Query,&world,changed) && changed && pixel(0,0,2)==255 && mask.AllChannels()==3,
		"live gameplay nature change updates image");
	const auto changedPixels=mask.Pixels();
	world.nature[0]=2; world.nature[3]=7;
	Check(!mask.Refresh(World::Query,&world,changed) && !changed && mask.Pixels()==changedPixels && mask.AllChannels()==3,
		"late invalid nature preserves entire prior image");
	world.nature[3]=0;
	Check(mask.Refresh(World::Query,&world,changed) && changed,"valid refresh after rejected update");
	Check(mask.Channels(10,10,20,20)==4,"interior burn tile selects only burn pass");
	Check(mask.Channels(45,10,55,20)==6,"nature boundary includes both bilinear neighbors");
	Check(mask.Channels(0,0,100,100)==7 && mask.Channels(-1,0,1,1)==7,"full and wrapping bounds conservative");
	Check(!mask.Channels(NAN,0,1,1) && !mask.Channels(1,0,0,1),"invalid tile bounds reject without integer conversion");
	for (int kind=0; kind<9; ++kind) {
		world.invalidLocation=kind==8;
		Check(!mask.Initialize(kind==0?0:kind==1?SIZE_MAX:16,kind==2?1025:16,
			kind==3?NAN:kind==4?0:kind==5?1e10f:100,kind==6?-1:100,kind==7?nullptr:World::Locate,&world),
			"invalid nature image initialization rejected");
		Check(!mask.Width() && !mask.Height() && mask.Pixels().empty() && !mask.CellCount() && !mask.AllChannels(),
			"failed initialization discards old map identity");
	}
	std::cout << "Terrain nature mask: " << checks << " checks, " << failures << " failures\n";
	return failures?1:0;
}
