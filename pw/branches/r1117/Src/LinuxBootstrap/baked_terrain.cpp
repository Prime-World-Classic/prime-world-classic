#include "baked_terrain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace LinuxBootstrap
{
namespace
{
using Bytes = std::vector<std::uint8_t>;

std::uint32_t U32(const Bytes& bytes, std::size_t at)
{
	return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8) |
		(std::uint32_t(bytes[at + 2]) << 16) | (std::uint32_t(bytes[at + 3]) << 24);
}

float F32(const Bytes& bytes, std::size_t at)
{
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE binary32 required");
	const auto bits = U32(bytes, at);
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

bool Range(std::size_t offset, std::size_t size, std::size_t end)
{
	return offset <= end && size <= end - offset;
}

using Point=BakedTerrain::Point;
Point Subtract(const Point& a,const Point& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Point Cross(const Point& a,const Point& b) {
	return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
double Dot(const Point& a,const Point& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
bool Bounded(double value) { return std::isfinite(value) && std::abs(value)<=1e12; }

/** Slab rejection is independent of triangle winding and includes degenerate-height boxes. */
bool RayBox(const BakedTerrain::Tile& tile,const Point& origin,const Point& direction,double maximum) {
	double near=0,far=maximum;
	for (size_t axis=0; axis<3; ++axis) {
		if (direction[axis]==0) {
			if (origin[axis]<tile.minimum[axis] || origin[axis]>tile.maximum[axis]) return false;
			continue;
		}
		double a=(tile.minimum[axis]-origin[axis])/direction[axis],b=(tile.maximum[axis]-origin[axis])/direction[axis];
		if (a>b) std::swap(a,b);
		near=std::max(near,a); far=std::min(far,b);
		if (near>far) return false;
	}
	return true;
}

/** Original HeightsController.cpp::IntersectTrinagle (Moller-Trumbore), in double precision.
 * Scale the parallel threshold with the ray/edges; retain two-sided collision semantics.
 */
bool RayTriangle(const Point& origin,const Point& direction,const Point& a,const Point& b,const Point& c,
	double maximum,double& distance) {
	const auto edge1=Subtract(b,a),edge2=Subtract(c,a),p=Cross(direction,edge2);
	const double determinant=Dot(edge1,p),scale=Dot(edge1,edge1)*Dot(p,p);
	if (!(scale>0) || determinant*determinant<=1e-24*scale) return false;
	const double inverse=1/determinant;
	const auto delta=Subtract(origin,a);
	const double u=Dot(delta,p)*inverse;
	constexpr double epsilon=1e-10;
	if (u<-epsilon || u>1+epsilon) return false;
	const auto q=Cross(delta,edge1);
	const double v=Dot(direction,q)*inverse;
	if (v<-epsilon || u+v>1+epsilon) return false;
	const double t=Dot(edge2,q)*inverse;
	if (!std::isfinite(t) || t<0 || t>maximum) return false;
	distance=t; return true;
}
}

bool BakedTerrain::Decode(Bytes&& bytes, std::size_t expected, Ptr& output,
	BakedTerrainError& error, const BakedTerrainLimits& requested) noexcept
{
	output.reset(); error = {};
	using Code = BakedTerrainError::Code;
	const auto fail = [&](Code code, std::size_t offset) { error = {code, offset}; return false; };
	const BakedTerrainLimits maximum;
	const auto sourceLimit = std::min(requested.sourceBytes, maximum.sourceBytes);
	const auto vertexLimit = std::min(requested.vertices, maximum.vertices);
	const auto indexLimit = std::min(requested.indices, maximum.indices);
	if (bytes.size() > sourceLimit || bytes.capacity() > sourceLimit || !expected ||
		expected > std::min(requested.tiles, maximum.tiles)) return fail(Code::Limit, 0);
	if (bytes.size() < 4) return fail(Code::Truncated, 0);
	if (U32(bytes, 0) != 0x14554u) return fail(Code::Format, 0);
	// At least a header, one fragment, six descriptors and one indexed vertex.
	if (expected > (bytes.size() - 4) / (44 + 40 + 120 + 44 + 12)) return fail(Code::Truncated, 4);
	try {
		auto result = std::make_shared<BakedTerrain>();
		result->tiles_.reserve(expected);
		std::size_t at = 4, totalIndices = 0;
		for (std::size_t number = 0; number < expected; ++number) {
			if (!Range(at, 44, bytes.size())) return fail(Code::Truncated, at);
			const auto word = [&](std::size_t offset) { return U32(bytes, at + offset); };
			if (word(0) != 0x4c455458u || word(4) != 1 || word(8) != 1 ||
				word(28) || word(32) || word(36) != 1) return fail(Code::Format, at);
			const std::size_t vb = word(20), ib = word(24), vbBytes = word(12), ibBytes = word(16);
			const std::size_t available = bytes.size() - at;
			if (vb < 44 || vb % 4 || ib % 4 || !vbBytes || vbBytes % 44 || !ibBytes || ibBytes % 12 ||
				!Range(vb, vbBytes, ib) || !Range(ib, ibBytes, available)) return fail(Code::Geometry, at);
			const std::size_t fragment = 36u + std::size_t(word(40));
			if (fragment < 44 || fragment % 4 || !Range(fragment, 40, vb)) return fail(Code::Format, at + 36);
			const auto field = [&](std::size_t offset) { return U32(bytes, at + fragment + offset); };
			if (field(0) != 44 || field(4) || field(8) || field(16) || field(24) || field(28) ||
				field(32) != 6) return fail(Code::Format, at + fragment);
			const std::size_t declaration = fragment + 32 + std::size_t(field(36));
			if (declaration < fragment + 40 || declaration % 4 || !Range(declaration, 120, vb))
				return fail(Code::Format, at + fragment + 32);
			constexpr std::uint32_t declarations[6][5] = {
				{0, 0, 2, 0, 0}, {0, 12, 2, 3, 0}, {0, 24, 1, 5, 0},
				{0, 32, 4, 10, 0}, {0, 36, 4, 10, 1}, {0, 40, 4, 10, 2}};
			for (std::size_t i = 0; i < 6; ++i) for (std::size_t j = 0; j < 5; ++j)
				if (U32(bytes, at + declaration + i * 20 + j * 4) != declarations[i][j])
					return fail(Code::Format, at + declaration + i * 20 + j * 4);
			Tile tile;
			tile.vertexOffset = at + vb; tile.indexOffset = at + ib;
			tile.vertexCount = vbBytes / 44; tile.indexCount = ibBytes / 4; tile.triangleCount = field(20);
			if (tile.vertexCount > vertexLimit - result->vertices_ || tile.indexCount > indexLimit - totalIndices)
				return fail(Code::Limit, at);
			if (!tile.triangleCount || tile.triangleCount > tile.indexCount / 3) return fail(Code::Geometry, at + fragment);
			for (std::size_t i = 0; i < tile.vertexCount; ++i) for (std::size_t j = 0; j < 8; ++j) {
				const auto offset = tile.vertexOffset + i * 44 + j * 4;
				const float value = F32(bytes, offset);
				if (!std::isfinite(value)) return fail(Code::Nonfinite, offset);
				if (j < 3) {
					// Bound world-space arithmetic used by visibility and preview transforms.
					if (std::abs(value) > 1000000.f) return fail(Code::Geometry, offset);
					if (!i) tile.minimum[j] = tile.maximum[j] = value;
					else { tile.minimum[j] = std::min(tile.minimum[j], value); tile.maximum[j] = std::max(tile.maximum[j], value); }
				}
			}
			for (std::size_t i = 0; i < tile.indexCount; ++i)
				if (U32(bytes, tile.indexOffset + i * 4) >= tile.vertexCount)
					return fail(Code::Geometry, tile.indexOffset + i * 4);
			result->vertices_ += tile.vertexCount; totalIndices += tile.indexCount;
			result->triangles_ += tile.triangleCount;
			result->tiles_.push_back(tile);
			at += ib + ibBytes;
		}
		if (at != bytes.size()) return fail(Code::Trailing, at);
		result->bytes_ = std::move(bytes);
		output = std::move(result);
		return true;
	} catch (const std::bad_alloc&) { return fail(Code::Allocation, 0); }
	catch (...) { return fail(Code::Allocation, 0); }
}

const BakedTerrain::Tile* BakedTerrain::GetTile(std::size_t index) const noexcept
{
	return index < tiles_.size() ? &tiles_[index] : nullptr;
}
const std::uint8_t* BakedTerrain::VertexData(std::size_t index) const noexcept
{
	const auto* tile = GetTile(index);
	return tile ? bytes_.data() + tile->vertexOffset : nullptr;
}
const std::uint8_t* BakedTerrain::IndexData(std::size_t index) const noexcept
{
	const auto* tile = GetTile(index);
	return tile ? bytes_.data() + tile->indexOffset : nullptr;
}
bool BakedTerrain::Trace(const Point& origin,const Point& direction,double maximum,SurfaceHit& output) const noexcept
{
	if (!Bounded(maximum) || maximum<=0 || direction==Point{}) return false;
	for (size_t axis=0; axis<3; ++axis) if (!Bounded(origin[axis]) || !Bounded(direction[axis])) return false;
	bool found=false; SurfaceHit nearest; nearest.distance=maximum;
	for (size_t number=0; number<tiles_.size(); ++number) {
		const auto& tile=tiles_[number];
		if (!RayBox(tile,origin,direction,nearest.distance)) continue;
		for (size_t triangle=0; triangle<tile.triangleCount; ++triangle) {
			std::array<Point,3> vertices{};
			for (size_t corner=0; corner<3; ++corner) {
				const size_t at=tile.vertexOffset+size_t(U32(bytes_,tile.indexOffset+(triangle*3+corner)*4))*44;
				for (size_t axis=0; axis<3; ++axis) vertices[corner][axis]=F32(bytes_,at+axis*4);
			}
			double distance=0;
			if (!RayTriangle(origin,direction,vertices[0],vertices[1],vertices[2],nearest.distance,distance) ||
				(found && distance>=nearest.distance)) continue;
			nearest.distance=distance; nearest.tile=number; nearest.triangle=triangle; found=true;
		}
	}
	if (!found) return false;
	for (size_t axis=0; axis<3; ++axis) nearest.position[axis]=origin[axis]+nearest.distance*direction[axis];
	output=nearest; return true;
}
bool BakedTerrain::Height(double x,double y,double& output) const noexcept
{
	// Decode bounds every authored coordinate to +/-1e6, so this segment spans the whole source.
	SurfaceHit hit;
	if (!Trace({x,y,1000001},{0,0,-1},2000002,hit)) return false;
	output=hit.position[2]; return true;
}
}
