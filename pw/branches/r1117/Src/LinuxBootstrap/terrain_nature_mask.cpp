#include "terrain_nature_mask.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace LinuxBootstrap {
void TerrainNatureMask::Clear() noexcept {
	width_=height_=0; worldWidth_=worldHeight_=0; allChannels_=0;
	cells_.clear(); pixelCells_.clear(); current_.clear(); pending_.clear(); pixels_.clear();
}
bool TerrainNatureMask::Initialize(std::size_t width, std::size_t height, float worldWidth, float worldHeight,
	Locate locate, void* user) noexcept {
	Clear();
	if (!locate || !width || !height || width>1024 || height>1024 ||
		!std::isfinite(worldWidth) || !std::isfinite(worldHeight) || worldWidth<=0 || worldHeight<=0 ||
		worldWidth>1e6f || worldHeight>1e6f) return false;
	try {
		std::vector<int> ids(width*height);
		for (size_t y=0; y<height; ++y) for (size_t x=0; x<width; ++x) {
			const float wx=float((double(x)+.5)*worldWidth/width);
			const float wy=float((double(height-y)-.5)*worldHeight/height);
			const int id=locate(user,wx,wy);
			if (id<0) return false;
			ids[y*width+x]=id;
		}
		auto cells=ids;
		std::sort(cells.begin(),cells.end()); cells.erase(std::unique(cells.begin(),cells.end()),cells.end());
		std::vector<std::uint32_t> pixelCells(ids.size());
		for (size_t i=0; i<ids.size(); ++i)
			pixelCells[i]=std::uint32_t(std::lower_bound(cells.begin(),cells.end(),ids[i])-cells.begin());
		std::vector<std::uint8_t> current(cells.size(),255),pending(cells.size(),255),pixels(ids.size()*4,0);
		cells_=std::move(cells); pixelCells_=std::move(pixelCells);
		current_=std::move(current); pending_=std::move(pending); pixels_=std::move(pixels);
		width_=width; height_=height; worldWidth_=worldWidth; worldHeight_=worldHeight;
		return true;
	} catch (const std::bad_alloc&) { Clear(); return false; }
}
bool TerrainNatureMask::Refresh(Query query, void* user, bool& changed) noexcept {
	changed=false;
	if (!query || cells_.empty()) return false;
	unsigned channels=0;
	for (size_t i=0; i<cells_.size(); ++i) {
		const int nature=query(user,cells_[i]);
		if (nature<0 || nature>2) return false;
		pending_[i]=std::uint8_t(nature); channels|=1u<<unsigned(nature);
	}
	if (current_==pending_) return true;
	constexpr size_t channelForNature[]={1,2,0};
	for (size_t i=0; i<pixelCells_.size(); ++i) {
		auto* pixel=pixels_.data()+i*4;
		pixel[0]=pixel[1]=pixel[2]=pixel[3]=0;
		pixel[channelForNature[pending_[pixelCells_[i]]]]=255;
	}
	current_.swap(pending_); allChannels_=channels; changed=true;
	return true;
}
unsigned TerrainNatureMask::Channels(float minX, float minY, float maxX, float maxY) const noexcept {
	if (!allChannels_ || !std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) ||
		!std::isfinite(maxY) || minX>maxX || minY>maxY) return 0;
	// Out-of-world geometry uses wrap sampling. Returning the full mask is conservative
	// without converting arbitrary out-of-range float coordinates to integer indices.
	if (minX<0 || minY<0 || maxX>worldWidth_ || maxY>worldHeight_) return allChannels_;
	// Linear filtering reads floor(uv * size - 0.5) and its immediate successor.
	const int x0=int(std::floor(double(minX)/worldWidth_*width_-.5));
	const int x1=int(std::floor(double(maxX)/worldWidth_*width_-.5))+1;
	const int y0=int(std::floor((1-double(maxY)/worldHeight_)*height_-.5));
	const int y1=int(std::floor((1-double(minY)/worldHeight_)*height_-.5))+1;
	if (x1-x0>=int(width_) || y1-y0>=int(height_)) return allChannels_;
	unsigned channels=0;
	for (int y=y0; y<=y1; ++y) for (int x=x0; x<=x1; ++x) {
		const size_t col=size_t((x+int(width_))%int(width_)), row=size_t((y+int(height_))%int(height_));
		channels|=1u<<current_[pixelCells_[row*width_+col]];
	}
	return channels;
}
}
