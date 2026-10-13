#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace LinuxBootstrap
{
/** Tightenable budgets for the fixed object/scene/component/colour schema.
 * Values above these defaults are clamped to the defaults, never unbounded.
 * Metadata means index-vector element storage, excluding allocator overhead.
 * Hash buckets are validated legacy metadata, never allocated or reconstructed.
 */
struct BakedVertexColorLimits
{
	std::size_t maxSourceBytes = 64u * 1024u * 1024u;
	std::size_t maxMetadataBytes = 16u * 1024u * 1024u;
	std::size_t maxObjects = 65536;
	std::size_t maxScenesPerObject = 256;
	std::size_t maxScenes = 262144;
	std::size_t maxComponentsPerScene = 4096;
	std::size_t maxComponents = 262144;
	std::size_t maxColorsPerStream = 4u * 1024u * 1024u;
	std::size_t maxColors = 8u * 1024u * 1024u;
	std::size_t maxHashBuckets = 1048576;
};

/** Allocation-free diagnostics. Offset is a source byte offset for decode,
 * or zero for lookup, allocation failure, and whole-source size rejection.
 */
struct BakedVertexColorError
{
	enum class Code
	{
		None, SourceLimit, Truncated, NegativeCount, CountLimit, MetadataLimit,
		DuplicateScene, NonzeroPadding, AllocationFailure,
		MissingObject, MissingScene, MissingComponent, EmptyColors,
		GeometryMismatch, VertexCountMismatch
	};
	Code code = Code::None;
	std::size_t offset = 0;
};

/** Immutable admitted pair, sharing the source bytes rather than copying colours.
 * Bytes remain original BGRA8 (including HDR alpha), with no channel conversion.
 * Copies outlive the decoded map and belong to one placed component, not merely
 * a geometry CRC. A default, moved-from, or rejected view has null data/zero sizes.
 */
class BakedVertexColorView
{
public:
	bool Valid() const noexcept { return static_cast<bool>(bytes_); }
	const std::uint8_t* DayData() const noexcept;
	const std::uint8_t* NightData() const noexcept;
	std::size_t ColorCount() const noexcept { return Valid() ? count_ : 0; }
	std::size_t ByteCount() const noexcept { return ColorCount() * 4; }
	std::size_t DayOffset() const noexcept { return Valid() ? day_ : 0; }
	std::size_t NightOffset() const noexcept { return Valid() ? night_ : 0; }
	std::uint32_t GeometryCRC() const noexcept { return Valid() ? crc_ : 0; }

private:
	friend class BakedVertexColors;
	std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
	std::size_t day_ = 0, night_ = 0, count_ = 0;
	std::uint32_t crc_ = 0;
};

/** Immutable, bounded index of the original ChunklessSaver .vtxcolor asset.
 * Decode rejects malformed structure, not empty or unequal day/night records:
 * those can belong to unbaked components and are rejected only by Lookup.
 * No hashing/CRC algorithm, filesystem access, engine dependency, or GL calls.
 */
class BakedVertexColors
{
public:
	using Ptr = std::shared_ptr<const BakedVertexColors>;

	/** Consume one byte vector without copying its payload. The caller must give
	 * up any mutable aliases into it. Size AND retained capacity obey source cap.
	 * Preflight validates the complete fixed-depth schema and zero-only trailing
	 * padding before index allocation. On ANY failure output is reset and error
	 * is set; no partial result is published. Input can be moved even on a late
	 * allocation failure. Allocation failures do not escape.
	 */
	static bool Decode(std::vector<std::uint8_t>&& bytes, Ptr& output,
		BakedVertexColorError& error, const BakedVertexColorLimits& limits = {}) noexcept;

	/** Lookup by original map object index, DBID::GetHashKey(), static-component
	 * traversal index, and caller-computed common-vertex-buffer CRC/count.
	 * Both streams must be nonempty and exactly match vertexCount. Every failure
	 * clears output and sets error; success clears any previous error.
	 */
	bool Lookup(std::size_t objectIndex, std::uint32_t sceneHash,
		std::size_t componentIndex, std::uint32_t geometryCRC, std::size_t vertexCount,
		BakedVertexColorView& output, BakedVertexColorError& error) const noexcept;

	std::size_t ObjectCount() const noexcept;
	std::size_t SceneCount() const noexcept;
	std::size_t ComponentCount() const noexcept;
	/** Cumulative packed colours in BOTH streams, including inadmissible records. */
	std::size_t ColorCount() const noexcept;
	std::size_t SourceBytes() const noexcept;
	std::size_t ParsedBytes() const noexcept;
	std::size_t MetadataBytes() const noexcept;

private:
	struct Data;
	explicit BakedVertexColors(std::shared_ptr<const Data> data) noexcept;
	std::shared_ptr<const Data> data_;
};
}
