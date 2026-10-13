#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace LinuxBootstrap
{
/** Tightenable caps for original version-one baked XTEL geometry, not editor meshes. */
struct BakedTerrainLimits
{
	std::size_t sourceBytes = 64 * 1024 * 1024;
	std::size_t tiles = 65536;
	std::size_t vertices = 1024 * 1024;
	std::size_t indices = 4 * 1024 * 1024;
};

/** Allocation-free decode failure location; every failure clears the output owner. */
struct BakedTerrainError
{
	enum class Code { None, Limit, Truncated, Format, Geometry, Nonfinite, Trailing, Allocation };
	Code code = Code::None;
	std::size_t offset = 0;
};

/** Immutable bounded index of the shipped .baked_terrain stream.
 * Tiles retain original descriptor order and already-deformed WORLD coordinates.
 * Vertex bytes are the original little-endian 44-byte position/normal/UV/BGRA
 * day/night/mask layout. Index bytes are little-endian uint32, including hidden
 * triangles; triangleCount selects the original visible prefix. No GL or file IO.
 */
class BakedTerrain
{
public:
	using Ptr = std::shared_ptr<const BakedTerrain>;
	struct Tile
	{
		std::size_t vertexOffset = 0, indexOffset = 0;
		std::size_t vertexCount = 0, indexCount = 0, triangleCount = 0;
		std::array<float, 3> minimum{}, maximum{};
	};

	/** Consume bytes without copying their payload; give up mutable input aliases.
	 * The DB supplies the exact tile count because the stream does not contain it.
	 * Validate all records, declarations, finite vertex floats and indices before
	 * publication. Limits above defaults are clamped. Size AND capacity are capped.
	 * Legacy dip.numVertices is not a VB length: shipped records overstate it.
	 * Unsupported versions/layouts fail closed, as do nonzero base/stream offsets.
	 */
	static bool Decode(std::vector<std::uint8_t>&& bytes, std::size_t expectedTiles,
		Ptr& output, BakedTerrainError& error, const BakedTerrainLimits& limits = {}) noexcept;
	const Tile* GetTile(std::size_t index) const noexcept;
	const std::uint8_t* VertexData(std::size_t index) const noexcept;
	const std::uint8_t* IndexData(std::size_t index) const noexcept;
	std::size_t TileCount() const noexcept { return tiles_.size(); }
	std::size_t SourceBytes() const noexcept { return bytes_.size(); }
	std::size_t VertexCount() const noexcept { return vertices_; }
	std::size_t TriangleCount() const noexcept { return triangles_; }

	using Point = std::array<double, 3>;
	/** Nearest original visible-prefix triangle, in world XYZ and ray parameter units. */
	struct SurfaceHit {
		Point position{};
		double distance = 0;
		std::size_t tile = 0, triangle = 0;
	};
	/** Allocation-free, two-sided geometry query using original collision ray/triangle math.
	 * Ignore hidden triangles, bound the segment to [0,maximum], and keep output on failure.
	 * Tile AABBs reject irrelevant geometry before decoding any triangle. No GL/readback,
	 * alpha masks, props, roads or collision-world mutation. Input magnitudes are capped at 1e12.
	 */
	bool Trace(const Point& origin, const Point& direction, double maximum, SurfaceHit& output) const noexcept;
	/** Highest visible-prefix triangle at XY; preserve output when no surface covers the point. */
	bool Height(double x, double y, double& output) const noexcept;

private:
	std::vector<std::uint8_t> bytes_;
	std::vector<Tile> tiles_;
	std::size_t vertices_ = 0, triangles_ = 0;
};
}
