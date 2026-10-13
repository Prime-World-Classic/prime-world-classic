#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace LinuxBootstrap {

/** Bounded native texture input sampled from the original world's hex-grid queries.
 * This represents current gameplay nature only, not NatureMapVisual attack/retreat,
 * overburn, noise or BlurDistort effects. Texture row zero maps to maximum world Y,
 * matching the original shader's negative-Y nature mapping. No GL or owner is retained.
 */
class TerrainNatureMask {
public:
	using Locate = int (*)(void*, float, float) noexcept;
	using Query = int (*)(void*, int) noexcept;
	/** Rebuild pixel-to-cell mapping, querying each pixel center once; failure clears all.
	 * Resolution is capped at 1024 per axis/1Mi pixels. Duplicate cell IDs are compressed
	 * so subsequent Refresh calls query each original cell only once, not once per pixel.
	 */
	bool Initialize(std::size_t width, std::size_t height, float worldWidth, float worldHeight,
		Locate locate, void* user) noexcept;
	/** N=0/A=1/B=2 encode green/blue/red, alpha zero. Invalid queries preserve prior pixels.
	 * An unchanged cell set does not rebuild the image. All output becomes valid atomically.
	 */
	bool Refresh(Query query, void* user, bool& changed) noexcept;
	/** Conservative bilinear-repeat coverage for world-space tile bounds; bits N/A/B.
	 * Includes neighboring texels, wraps at map edges, and returns zero for invalid bounds.
	 */
	unsigned Channels(float minX, float minY, float maxX, float maxY) const noexcept;
	void Clear() noexcept;
	const std::vector<std::uint8_t>& Pixels() const noexcept { return pixels_; }
	std::size_t Width() const noexcept { return width_; }
	std::size_t Height() const noexcept { return height_; }
	std::size_t CellCount() const noexcept { return cells_.size(); }
	unsigned AllChannels() const noexcept { return allChannels_; }
private:
	std::size_t width_ = 0, height_ = 0;
	float worldWidth_ = 0, worldHeight_ = 0;
	unsigned allChannels_ = 0;
	std::vector<int> cells_;
	std::vector<std::uint32_t> pixelCells_;
	std::vector<std::uint8_t> current_, pending_, pixels_;
};

}
